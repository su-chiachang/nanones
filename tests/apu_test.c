/* Unit tests for the APU: pitch of pulse and triangle, length counters,
 * frame-counter IRQ and DMC playback. Values follow the NESdev APU docs. */
#include "../src/apu.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static uint8_t dmc_stub(void *ctx, uint16_t addr) { (void)ctx; (void)addr; return 0xFF; }

static void run(Apu *a, long cycles) { while (cycles-- > 0) apu_tick(a); }

/* Counts rising crossings of the mid level over one emulated second. */
static double measure_hz(Apu *a) {
    static float buf[APU_RING];
    double secs = 1.0;
    run(a, 20000);                       /* settle */
    apu_read_samples(a, buf, APU_RING);  /* discard */
    long total = 0;
    int crossings = 0;
    float lo = 1e9f, hi = -1e9f;
    static float all[48000 * 2];
    long want = (long)(48000 * secs);
    while (total < want) {
        run(a, 1789773 / 100);
        size_t n = apu_read_samples(a, buf, APU_RING);
        for (size_t i = 0; i < n && total < want; i++) all[total++] = buf[i];
    }
    for (long i = 0; i < total; i++) { if (all[i] < lo) lo = all[i]; if (all[i] > hi) hi = all[i]; }
    float mid = (lo + hi) / 2;
    for (long i = 1; i < total; i++)
        if (all[i - 1] < mid && all[i] >= mid) crossings++;
    return crossings / secs;
}

int main(void) {
    Apu a;

    apu_init(&a, 48000.0, dmc_stub, NULL);
    apu_write(&a, 0x4015, 0x01);
    apu_write(&a, 0x4000, 0xBF);   /* duty 50%, halt, constant volume 15 */
    apu_write(&a, 0x4002, 0xFD);   /* period 253 -> 1789773 / (16 * 254) = 440.4 Hz */
    apu_write(&a, 0x4003, 0x08);
    double hz = measure_hz(&a);
    CHECK(fabs(hz - 440.4) < 3.0, "pulse 1 pitch is about 440 Hz");
    if (fabs(hz - 440.4) >= 3.0) printf("  measured %.1f Hz\n", hz);

    apu_init(&a, 48000.0, dmc_stub, NULL);
    apu_write(&a, 0x4015, 0x04);
    apu_write(&a, 0x4008, 0xFF);   /* control + linear reload 127 */
    apu_write(&a, 0x400A, 0x7D);   /* period 125 -> 1789773 / (32 * 126) = 443.9 Hz */
    apu_write(&a, 0x400B, 0x08);
    hz = measure_hz(&a);
    CHECK(fabs(hz - 443.9) < 3.0, "triangle pitch is about 444 Hz");
    if (fabs(hz - 443.9) >= 3.0) printf("  measured %.1f Hz\n", hz);

    /* length counter: index 3 -> length 2, ticks on half frames (cycles 14913 and 29829) */
    apu_init(&a, 48000.0, dmc_stub, NULL);
    apu_write(&a, 0x4015, 0x01);
    apu_write(&a, 0x4000, 0x1F);
    apu_write(&a, 0x4003, 0x18);
    CHECK(apu_read_status(&a) & 1, "length counter loaded");
    run(&a, 20000);
    CHECK(apu_read_status(&a) & 1, "still playing after the first half frame");
    run(&a, 10000);
    CHECK(!(apu_read_status(&a) & 1), "silenced after the second half frame");

    /* disabling a channel clears its length counter */
    apu_write(&a, 0x4015, 0x01); apu_write(&a, 0x4003, 0x18);
    apu_write(&a, 0x4015, 0x00);
    CHECK(!(apu_read_status(&a) & 1), "$4015 bit clear zeroes the length counter");

    /* frame IRQ */
    apu_init(&a, 48000.0, dmc_stub, NULL);
    run(&a, 29827);
    CHECK(!apu_irq(&a), "no frame IRQ before cycle 29828");
    run(&a, 3);
    CHECK(apu_irq(&a), "frame IRQ raised at the end of the 4-step sequence");
    CHECK(apu_read_status(&a) & 0x40, "frame IRQ visible in $4015");
    CHECK(!apu_irq(&a), "reading $4015 acknowledges the frame IRQ");
    apu_write(&a, 0x4017, 0x40);
    run(&a, 40000);
    CHECK(!apu_irq(&a), "$4017 bit 6 inhibits the frame IRQ");

    /* DMC: all-ones sample ramps the level up by 2 per bit */
    apu_init(&a, 48000.0, dmc_stub, NULL);
    apu_write(&a, 0x4010, 0x8F);   /* IRQ enabled, fastest rate */
    apu_write(&a, 0x4012, 0x00);
    apu_write(&a, 0x4013, 0x01);   /* 17 bytes */
    apu_write(&a, 0x4015, 0x10);
    CHECK(apu_read_status(&a) & 0x10, "DMC reports bytes remaining");
    run(&a, 54 * 8 * 20);
    CHECK(a.dmc.level > 100, "DMC output level ramped up");
    CHECK(!(apu_read_status(&a) & 0x10), "DMC finished the sample");
    CHECK(apu_irq(&a), "DMC IRQ raised at the end of the sample");

    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("apu tests passed\n");
    return 0;
}
