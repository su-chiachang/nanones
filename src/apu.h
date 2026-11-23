#ifndef NES_APU_H
#define NES_APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APU_RING 16384

typedef struct {
    bool enabled, halt, constant, env_start, sweep_en, sweep_neg, sweep_reload, ones_complement;
    uint8_t duty, duty_pos, volume, env_div, env_decay, sweep_period, sweep_shift, sweep_div, length;
    uint16_t period, timer;
} ApuPulse;

typedef struct {
    bool enabled, control, reload_flag;
    uint8_t linear_reload, linear, seq, length;
    uint16_t period, timer;
} ApuTriangle;

typedef struct {
    bool enabled, halt, constant, env_start, mode;
    uint8_t volume, env_div, env_decay, length;
    uint16_t period, timer, lfsr;
} ApuNoise;

typedef struct {
    bool enabled, irq_enable, loop, irq, silence, buffer_empty;
    uint8_t rate_idx, level, shift, bits_left, buffer;
    uint16_t addr, cur_addr, length, bytes_left, timer;
} ApuDmc;

typedef struct Apu {
    ApuPulse p[2];
    ApuTriangle tri;
    ApuNoise noise;
    ApuDmc dmc;

    int32_t frame_cycle;
    bool wrapped;           /* the 4-step sequence just wrapped: IRQ flag is also set on cycle 0 */
    bool pending_clock;     /* $4017 with the 5-step bit: quarter+half clock after the reset delay */
    bool five_step, irq_inhibit, frame_irq;
    bool odd_cycle;

    /* audio output */
    double sample_rate, cycles_per_sample, sample_acc;
    double mix_sum; int mix_count;
    float ring[APU_RING];
    size_t ring_head, ring_count;

    uint8_t (*dmc_read)(void *ctx, uint16_t addr);
    void *ctx;
} Apu;

void apu_init(Apu *a, double sample_rate, uint8_t (*dmc_read)(void *, uint16_t), void *ctx);
void apu_tick(Apu *a);                       /* one CPU cycle */
uint8_t apu_read_status(Apu *a);             /* $4015 */
void apu_write(Apu *a, uint16_t addr, uint8_t v); /* $4000-$4013, $4015, $4017 */
bool apu_irq(const Apu *a);
size_t apu_read_samples(Apu *a, float *out, size_t max);

#endif
