#include "apu.h"

#include <string.h>

#define CPU_HZ 1789773.0
#ifndef FC_D
#define FC_D 34 /* tens digit: delay on even cycles, ones digit: on odd cycles */
#endif
#define FC_DELAY_EVEN (FC_D / 10)
#define FC_DELAY_ODD (FC_D % 10)

static const uint8_t LENGTHS[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
static const uint8_t DUTY[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 1, 1, 1, 1, 1}};
static const uint16_t NOISE_PERIOD[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
static const uint16_t DMC_RATE[16] = {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};

void apu_init(Apu *a, double sample_rate, uint8_t (*dmc_read)(void *, uint16_t), void *ctx) {
    memset(a, 0, sizeof *a);
    a->p[0].ones_complement = true; /* pulse 1 negates with one's complement */
    a->noise.lfsr = 1;
    a->dmc.silence = true; a->dmc.buffer_empty = true; a->dmc.bits_left = 8;
    a->dmc.timer = DMC_RATE[0] - 1;
    a->sample_rate = sample_rate;
    a->cycles_per_sample = CPU_HZ / sample_rate;
    a->dmc_read = dmc_read;
    a->ctx = ctx;
}

/* ---- envelope / sweep helpers ---- */

static void env_clock(bool *start, uint8_t *div, uint8_t *decay, uint8_t vol, bool loop) {
    if (*start) { *start = false; *decay = 15; *div = vol; }
    else if (*div == 0) {
        *div = vol;
        if (*decay > 0) (*decay)--; else if (loop) *decay = 15;
    } else (*div)--;
}

static unsigned sweep_target(const ApuPulse *p) {
    unsigned change = p->period >> p->sweep_shift;
    if (!p->sweep_neg) return p->period + change;
    return p->period - change - (p->ones_complement ? 1 : 0);
}

static bool sweep_muted(const ApuPulse *p) { return p->period < 8 || sweep_target(p) > 0x7FF; }

static void sweep_clock(ApuPulse *p) {
    if (p->sweep_div == 0 && p->sweep_en && p->sweep_shift && !sweep_muted(p))
        p->period = (uint16_t)sweep_target(p);
    if (p->sweep_div == 0 || p->sweep_reload) { p->sweep_div = p->sweep_period; p->sweep_reload = false; }
    else p->sweep_div--;
}

static void quarter_frame(Apu *a) {
    for (int i = 0; i < 2; i++)
        env_clock(&a->p[i].env_start, &a->p[i].env_div, &a->p[i].env_decay, a->p[i].volume, a->p[i].halt);
    env_clock(&a->noise.env_start, &a->noise.env_div, &a->noise.env_decay, a->noise.volume, a->noise.halt);
    if (a->tri.reload_flag) a->tri.linear = a->tri.linear_reload;
    else if (a->tri.linear) a->tri.linear--;
    if (!a->tri.control) a->tri.reload_flag = false;
}

static void half_frame(Apu *a) {
    for (int i = 0; i < 2; i++) {
        if (!a->p[i].halt && a->p[i].length) a->p[i].length--;
        sweep_clock(&a->p[i]);
    }
    if (!a->tri.control && a->tri.length) a->tri.length--;
    if (!a->noise.halt && a->noise.length) a->noise.length--;
}

/* ---- registers ---- */

void apu_write(Apu *a, uint16_t addr, uint8_t v) {
    if (addr >= 0x4000 && addr <= 0x4007) {
        ApuPulse *p = &a->p[(addr >> 2) & 1];
        switch (addr & 3) {
        case 0: p->duty = v >> 6; p->halt = v & 0x20; p->constant = v & 0x10; p->volume = v & 0x0F; break;
        case 1: p->sweep_en = v & 0x80; p->sweep_period = (v >> 4) & 7; p->sweep_neg = v & 8;
                p->sweep_shift = v & 7; p->sweep_reload = true; break;
        case 2: p->period = (uint16_t)((p->period & 0x700) | v); break;
        case 3: p->period = (uint16_t)((p->period & 0x0FF) | ((v & 7) << 8));
                if (p->enabled) p->length = LENGTHS[v >> 3];
                p->duty_pos = 0; p->env_start = true; break;
        }
        return;
    }
    switch (addr) {
    case 0x4008: a->tri.control = v & 0x80; a->tri.linear_reload = v & 0x7F; break;
    case 0x400A: a->tri.period = (uint16_t)((a->tri.period & 0x700) | v); break;
    case 0x400B: a->tri.period = (uint16_t)((a->tri.period & 0x0FF) | ((v & 7) << 8));
                 if (a->tri.enabled) a->tri.length = LENGTHS[v >> 3];
                 a->tri.reload_flag = true; break;
    case 0x400C: a->noise.halt = v & 0x20; a->noise.constant = v & 0x10; a->noise.volume = v & 0x0F; break;
    case 0x400E: a->noise.mode = v & 0x80; a->noise.period = NOISE_PERIOD[v & 0x0F]; break;
    case 0x400F: if (a->noise.enabled) a->noise.length = LENGTHS[v >> 3];
                 a->noise.env_start = true; break;
    case 0x4010: a->dmc.irq_enable = v & 0x80; a->dmc.loop = v & 0x40; a->dmc.rate_idx = v & 0x0F;
                 if (!a->dmc.irq_enable) a->dmc.irq = false; break;
    case 0x4011: a->dmc.level = v & 0x7F; break;
    case 0x4012: a->dmc.addr = (uint16_t)(0xC000 + v * 64); break;
    case 0x4013: a->dmc.length = (uint16_t)(v * 16 + 1); break;
    case 0x4015:
        a->p[0].enabled = v & 1; a->p[1].enabled = v & 2;
        a->tri.enabled = v & 4; a->noise.enabled = v & 8;
        if (!a->p[0].enabled) a->p[0].length = 0;
        if (!a->p[1].enabled) a->p[1].length = 0;
        if (!a->tri.enabled) a->tri.length = 0;
        if (!a->noise.enabled) a->noise.length = 0;
        a->dmc.irq = false;
        if (!(v & 0x10)) { a->dmc.enabled = false; a->dmc.bytes_left = 0; }
        else if (!a->dmc.enabled || a->dmc.bytes_left == 0) {
            a->dmc.enabled = true;
            a->dmc.cur_addr = a->dmc.addr; a->dmc.bytes_left = a->dmc.length;
        }
        break;
    case 0x4017:
        a->five_step = v & 0x80; a->irq_inhibit = v & 0x40;
        if (a->irq_inhibit) a->frame_irq = false;
        /* The sequencer restarts 3 or 4 cycles after the write, depending on parity. */
        a->frame_cycle = a->odd_cycle ? -FC_DELAY_ODD : -FC_DELAY_EVEN;
        a->pending_clock = a->five_step;
        break;
    }
}

uint8_t apu_read_status(Apu *a) {
    uint8_t v = (uint8_t)((a->p[0].length ? 1 : 0) | (a->p[1].length ? 2 : 0) |
                          (a->tri.length ? 4 : 0) | (a->noise.length ? 8 : 0) |
                          (a->dmc.bytes_left ? 0x10 : 0) | (a->frame_irq ? 0x40 : 0) |
                          (a->dmc.irq ? 0x80 : 0));
    a->frame_irq = false;
    return v;
}

bool apu_irq(const Apu *a) { return a->frame_irq || a->dmc.irq; }

/* ---- per-cycle clocking ---- */

static void dmc_tick(Apu *a) {
    ApuDmc *d = &a->dmc;
    if (d->buffer_empty && d->bytes_left > 0) {
        d->buffer = a->dmc_read(a->ctx, d->cur_addr);
        d->buffer_empty = false;
        d->cur_addr = (uint16_t)((d->cur_addr + 1) | 0x8000);
        if (--d->bytes_left == 0) {
            if (d->loop) { d->cur_addr = d->addr; d->bytes_left = d->length; }
            else if (d->irq_enable) d->irq = true;
        }
    }
    if (d->timer-- != 0) return;
    d->timer = (uint16_t)(DMC_RATE[d->rate_idx] - 1);
    if (!d->silence) {
        if (d->shift & 1) { if (d->level <= 125) d->level += 2; }
        else if (d->level >= 2) d->level -= 2;
        d->shift >>= 1;
    }
    if (--d->bits_left == 0) {
        d->bits_left = 8;
        if (d->buffer_empty) d->silence = true;
        else { d->silence = false; d->shift = d->buffer; d->buffer_empty = true; }
    }
}

static float mix(const Apu *a) {
    int pulse = 0;
    for (int i = 0; i < 2; i++) {
        const ApuPulse *p = &a->p[i];
        if (p->length && !sweep_muted(p) && DUTY[p->duty][p->duty_pos])
            pulse += p->constant ? p->volume : p->env_decay;
    }
    static const uint8_t TRI[32] = {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
                                    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    int tri = TRI[a->tri.seq];
    int noise = (a->noise.length && !(a->noise.lfsr & 1))
                    ? (a->noise.constant ? a->noise.volume : a->noise.env_decay) : 0;
    float pulse_out = pulse ? 95.88f / (8128.0f / (float)pulse + 100.0f) : 0.0f;
    float d = (float)tri / 8227.0f + (float)noise / 12241.0f + (float)a->dmc.level / 22638.0f;
    float tnd_out = d > 0.0f ? 159.79f / (1.0f / d + 100.0f) : 0.0f;
    return pulse_out + tnd_out;
}

void apu_tick(Apu *a) {
    /* frame counter */
    a->frame_cycle++;
    if (a->frame_cycle == 0 && a->wrapped) {
        a->wrapped = false;
        if (!a->irq_inhibit) a->frame_irq = true;
    }
    if (a->frame_cycle == 0 && a->pending_clock) {
        a->pending_clock = false;
        quarter_frame(a); half_frame(a);
    }
    if (!a->five_step) {
        switch (a->frame_cycle) {
        case 7457: quarter_frame(a); break;
        case 14913: quarter_frame(a); half_frame(a); break;
        case 22371: quarter_frame(a); break;
        case 29828: if (!a->irq_inhibit) a->frame_irq = true; break;
        case 29829: quarter_frame(a); half_frame(a);
                    if (!a->irq_inhibit) a->frame_irq = true;
                    a->wrapped = true;
                    a->frame_cycle = -1; break;
        }
    } else {
        switch (a->frame_cycle) {
        case 7457: quarter_frame(a); break;
        case 14913: quarter_frame(a); half_frame(a); break;
        case 22371: quarter_frame(a); break;
        case 37281: quarter_frame(a); half_frame(a); a->frame_cycle = -1; break;
        }
    }

    /* triangle runs at CPU rate */
    if (a->tri.timer == 0) {
        a->tri.timer = a->tri.period;
        if (a->tri.length && a->tri.linear) a->tri.seq = (a->tri.seq + 1) & 31;
    } else a->tri.timer--;

    /* noise: period is in CPU cycles */
    if (a->noise.timer == 0) {
        a->noise.timer = (uint16_t)(a->noise.period ? a->noise.period - 1 : 0);
        uint16_t fb = (uint16_t)((a->noise.lfsr & 1) ^ ((a->noise.lfsr >> (a->noise.mode ? 6 : 1)) & 1));
        a->noise.lfsr = (uint16_t)((a->noise.lfsr >> 1) | (fb << 14));
    } else a->noise.timer--;

    /* pulses run at half CPU rate */
    a->odd_cycle = !a->odd_cycle;
    if (!a->odd_cycle) {
        for (int i = 0; i < 2; i++) {
            ApuPulse *p = &a->p[i];
            if (p->timer == 0) { p->timer = p->period; p->duty_pos = (p->duty_pos + 1) & 7; }
            else p->timer--;
        }
    }

    dmc_tick(a);

    /* resample: average the mixer output over each output sample */
    a->mix_sum += (double)mix(a);
    a->mix_count++;
    a->sample_acc += 1.0;
    if (a->sample_acc >= a->cycles_per_sample) {
        a->sample_acc -= a->cycles_per_sample;
        double x = a->mix_sum / a->mix_count;
        double hp = 0.99516 * (a->hp_prev_out + x - a->hp_prev_in); /* exp(-2*pi*37/48000) */
        a->hp_prev_in = x; a->hp_prev_out = hp;
        float s = (float)hp;
        a->mix_sum = 0; a->mix_count = 0;
        a->ring[(a->ring_head + a->ring_count) % APU_RING] = s;
        if (a->ring_count < APU_RING) a->ring_count++;
        else a->ring_head = (a->ring_head + 1) % APU_RING; /* drop the oldest */
    }
}

size_t apu_read_samples(Apu *a, float *out, size_t max) {
    size_t n = a->ring_count < max ? a->ring_count : max;
    for (size_t i = 0; i < n; i++) out[i] = a->ring[(a->ring_head + i) % APU_RING];
    a->ring_head = (a->ring_head + n) % APU_RING;
    a->ring_count -= n;
    return n;
}
