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

/* Candidate plans: {buttons, frames} segments, run for HORIZON frames in total. */
typedef struct { uint8_t b1; int n1; uint8_t b2; } Plan;
static const Plan PLANS[] = {
    {BTN_RIGHT | BTN_B, 1000, 0},
    {BTN_RIGHT | BTN_B | BTN_A, 14, BTN_RIGHT | BTN_B},
    {BTN_RIGHT | BTN_B | BTN_A, 26, BTN_RIGHT | BTN_B},
    {BTN_RIGHT | BTN_B | BTN_A, 40, BTN_RIGHT | BTN_B},
    {BTN_B, 6, BTN_RIGHT | BTN_B},
    {BTN_RIGHT, 1000, 0},
    {BTN_LEFT | BTN_B, 8, BTN_RIGHT | BTN_B},
};
#define NPLANS (int)(sizeof PLANS / sizeof PLANS[0])
#define HORIZON 50
#define STEP 10

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s rom.nes out_prefix [frames]\n", argv[0]); return 2; }
    int total = argc > 3 ? atoi(argv[3]) : 6000;
    static Nes nes, trial, snap;
    if (nes_load(&nes, argv[1]) != 0) return 2;

    int f = 0;
    for (; f < 1500; f++) { nes.buttons = scripted(f); nes_run_frame(&nes); }

    int best_x = 0;
    while (f < total) {
        nes_clone(&snap, &nes);
        int best = 0, best_score = -1;
        for (int p = 0; p < NPLANS; p++) {
            nes_clone(&trial, &snap);
            for (int i = 0; i < HORIZON; i++) {
                trial.buttons = i < PLANS[p].n1 ? PLANS[p].b1 : PLANS[p].b2;
                nes_run_frame(&trial);
            }
            int score = mario_x(&trial) * 4 - (trial.ram[0xA2] > 0xB8 ? 100 : 0);
            if (score > best_score) { best_score = score; best = p; }
        }
        for (int i = 0; i < STEP; i++, f++) {
            nes.buttons = i < PLANS[best].n1 ? PLANS[best].b1 : PLANS[best].b2;
            nes_run_frame(&nes);
            if (f % 300 == 0) dump(&nes, argv[2], f);
        }
        if (mario_x(&nes) > best_x) best_x = mario_x(&nes);
        if (f % 300 < STEP) printf("frame %d  x=%d  best=%d\n", f, mario_x(&nes), best_x);
    }
    printf("furthest x = %d\n", best_x);
    return 0;
}
