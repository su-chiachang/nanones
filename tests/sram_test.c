/* Battery save helpers: .sav path derivation and a save/load round trip.
 * usage: sram_test rom.nes workdir */
#include "../src/cart.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static void path_is(const char *rom, const char *want) {
    char got[256];
    cart_sram_path(rom, got, sizeof got);
    if (strcmp(got, want) != 0) { printf("FAIL: %s -> %s, want %s\n", rom, got, want); failures++; }
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s rom.nes workdir\n", argv[0]); return 2; }

    path_is("roms/dq4.zip", "roms/dq4.sav");
    path_is("game.nes", "game.sav");
    path_is("a.b/game", "a.b/game.sav");
    path_is("dir/.hidden", "dir/.hidden.sav");
    path_is("Dragon Quest IV (v1.07).nes", "Dragon Quest IV (v1.07).sav");

    static Cart c;
    if (cart_load(&c, argv[1]) != 0) return 2;
    char sav[512];
    snprintf(sav, sizeof sav, "%s/sram_test.sav", argv[2]);
    remove(sav);

    CHECK(cart_sram_load(&c, sav) == 0, "missing save reports 0 and is not an error");
    for (size_t i = 0; i < sizeof c.prg_ram; i++) c.prg_ram[i] = (uint8_t)(i * 7 + 3);
    CHECK(cart_sram_save(&c, sav) == 0, "save succeeds");
    uint8_t expect[sizeof c.prg_ram];
    memcpy(expect, c.prg_ram, sizeof expect);
    memset(c.prg_ram, 0, sizeof c.prg_ram);
    CHECK(cart_sram_load(&c, sav) == 1, "save loads back");
    CHECK(memcmp(c.prg_ram, expect, sizeof expect) == 0, "PRG-RAM round-trips");

    cart_free(&c);
    remove(sav);
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("sram tests passed\n");
    return 0;
}
