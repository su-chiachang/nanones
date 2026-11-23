/* Tool (not a unit test): plays SMB3 World 1-1 with a look-ahead search over
 * cloned machine states, to show the emulator can be driven through a level.
 * It reads Mario's X position from RAM ($75 hi / $90 lo) and keeps the input
 * that makes the most progress over the next few dozen frames.
 *
 * usage: smb3_bot rom.nes out_prefix [total_frames]
 * writes out_prefix_<frame>.ppm every 300 frames and prints progress. */
#include "../src/nes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t scripted(int f) {
    if (f >= 420 && f < 430) return BTN_START;
    if (f >= 600 && f < 610) return BTN_START;
    if (f >= 900 && f < 910) return BTN_RIGHT;
    if (f >= 1000 && f < 1010) return BTN_UP;
    if (f >= 1100 && f < 1110) return BTN_A;
    return 0;
}

static uint8_t hist[20000]; /* buttons held on every emulated frame, saved for replay */
static int hist_len;
static void record(int f, uint8_t b) { if (f < (int)sizeof hist) { hist[f] = b; hist_len = f + 1; } }

static int mario_x(const Nes *n) { return n->ram[0x75] * 256 + n->ram[0x90]; }

static void dump(const Nes *n, const char *prefix, int frame) {
    char path[512];
    snprintf(path, sizeof path, "%s_%05d.ppm", prefix, frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", PPU_W, PPU_H);
    for (int i = 0; i < PPU_W * PPU_H; i++) {
        uint32_t px = n->ppu.fb[i];
        fputc((px >> 16) & 0xFF, f); fputc((px >> 8) & 0xFF, f); fputc(px & 0xFF, f);
    }
    fclose(f);
}

/* Candidate plans: wait `delay` frames running, then hold A for `jump` frames, then run on. */
typedef struct { int delay, jump; uint8_t run; } Plan;
static const Plan PLANS[] = {
    {0, 0, BTN_RIGHT | BTN_B},
    {0, 14, BTN_RIGHT | BTN_B}, {0, 26, BTN_RIGHT | BTN_B}, {0, 40, BTN_RIGHT | BTN_B},
    {8, 26, BTN_RIGHT | BTN_B}, {8, 40, BTN_RIGHT | BTN_B},
    {16, 26, BTN_RIGHT | BTN_B}, {16, 40, BTN_RIGHT | BTN_B},
    {24, 26, BTN_RIGHT | BTN_B}, {24, 40, BTN_RIGHT | BTN_B},
    {0, 0, BTN_RIGHT}, {0, 0, BTN_B}, {0, 26, BTN_RIGHT},
};
#define NPLANS (int)(sizeof PLANS / sizeof PLANS[0])
#define HORIZON 80
#define STEP 8

static uint8_t plan_buttons(const Plan *p, int i) {
    if (i >= p->delay && i < p->delay + p->jump) return p->run | BTN_A;
    return p->run;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s rom.nes out_prefix [frames]\n", argv[0]); return 2; }
    int total = argc > 3 ? atoi(argv[3]) : 6000;
    static Nes nes, trial, snap;
    if (nes_load(&nes, argv[1]) != 0) return 2;

    int f = 0;
    for (; f < 1500; f++) { nes.buttons = scripted(f); record(f, nes.buttons); nes_run_frame(&nes); }

    int best_x = 0;
    while (f < total) {
        if (mario_x(&nes) > 4000) { printf("left the level at frame %d (best x=%d)\n", f, best_x);
            /* let the end-of-level sequence play out, pressing A now and then to skip screens */
            for (int i = 0; i < 900; i++, f++) {
                nes.buttons = (i % 120) < 6 ? BTN_A : 0;
                record(f, nes.buttons);
                nes_run_frame(&nes);
                if (i % 100 == 99) { dump(&nes, argv[2], f); printf("after level: frame %d dumped\n", f); }
            }
            break;
        }
        nes_clone(&snap, &nes);
        int best = 0, best_score = -1;
        for (int p = 0; p < NPLANS; p++) {
            nes_clone(&trial, &snap);
            for (int i = 0; i < HORIZON; i++) {
                trial.buttons = plan_buttons(&PLANS[p], i);
                nes_run_frame(&trial);
            }
            int score = mario_x(&trial) * 4 - (trial.ram[0xA2] > 0xB8 ? 100 : 0);
            if (score > best_score) { best_score = score; best = p; }
        }
        for (int i = 0; i < STEP; i++, f++) {
            nes.buttons = plan_buttons(&PLANS[best], i);
            record(f, nes.buttons);
            nes_run_frame(&nes);
            if (f % 300 == 0) dump(&nes, argv[2], f);
            if (getenv("BOT_TRACE_FROM") && f >= atoi(getenv("BOT_TRACE_FROM")) && f < atoi(getenv("BOT_TRACE_TO")) && f % 6 == 0) {
                dump(&nes, argv[2], f);
                printf("trace f=%d x=%d y=%02X\n", f, mario_x(&nes), nes.ram[0xA2]);
                int vis = 0;
                for (int k = 0; k < 64; k++) if (nes.ppu.oam[k * 4] < 0xEF) vis++;
                printf("  visible sprites=%d pc=%04X ppuctrl=%02X mask=%02X scroll=%02X/%02X timer=%02X%02X%02X\n",
                       vis, nes.cpu.pc, nes.ppu.ctrl, nes.ppu.mask, nes.ram[0xFD], nes.ram[0xFC],
                       nes.ram[0x5EE], nes.ram[0x5EF], nes.ram[0x5F0]);
                printf("  code@96F8:");
                for (int k = 0; k < 10; k++) printf(" %02X", nes.cpu.read(nes.cpu.ctx, (uint16_t)(0x96F8 + k)));
                printf("  irq=%d cart.irq=%d sp=%02X p=%02X zp00-1F:", nes.cpu.irq_line, nes.cart.irq, nes.cpu.sp, nes.cpu.p);
                for (int k = 0; k < 32; k++) printf(" %02X", nes.ram[k]);
                printf("\n");
            }
        }
        if (mario_x(&nes) > best_x) best_x = mario_x(&nes);
        if (f % 300 < STEP) printf("frame %d  x=%d  best=%d\n", f, mario_x(&nes), best_x);
    }
    {
        char path[512];
        snprintf(path, sizeof path, "%s.inp", argv[2]);
        FILE *out = fopen(path, "wb");
        if (out) { fwrite(hist, 1, (size_t)hist_len, out); fclose(out); printf("saved %d frames of input to %s\n", hist_len, path); }
    }
    printf("furthest x = %d\n", best_x);
    return 0;
}
