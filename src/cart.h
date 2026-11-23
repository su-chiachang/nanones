#ifndef NES_CART_H
#define NES_CART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MIRROR_HORIZONTAL, /* nametables 0,1 share page 0; 2,3 share page 1 */
    MIRROR_VERTICAL,   /* nametables 0,2 share page 0; 1,3 share page 1 */
    MIRROR_SINGLE0,
    MIRROR_SINGLE1,
    MIRROR_FOUR,
} Mirroring;

typedef struct Cart Cart;

struct Cart {
    uint8_t *prg;
    size_t prg_size;
    uint8_t *chr;
    size_t chr_size;
    bool chr_is_ram;
    uint8_t prg_ram[0x2000];
    int mapper;
    Mirroring mirroring;

    /* Mapper hooks. cpu_* see $4020-$FFFF, ppu_* see $0000-$1FFF. */
    uint8_t (*cpu_read)(Cart *, uint16_t addr);
    void (*cpu_write)(Cart *, uint16_t addr, uint8_t value);
    uint8_t (*ppu_read)(Cart *, uint16_t addr);
    void (*ppu_write)(Cart *, uint16_t addr, uint8_t value);

    /* Called for every PPU fetch below $3000 (pattern and nametable) so a
     * mapper can watch address line A12, as MMC3 does for its IRQ counter. */
    void (*ppu_observe)(Cart *, uint16_t addr);

    uint64_t ppu_clock; /* PPU dots since power-on, advanced by the PPU */
    bool irq;           /* level of the mapper's IRQ output */

    void *mstate; /* mapper-private state */
    size_t mstate_size;
};

/* Returns 0 on success; prints a message to stderr and returns -1 otherwise. */
int cart_load(Cart *c, const char *path);
void cart_free(Cart *c);

#endif
