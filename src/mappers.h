#ifndef NES_MAPPERS_H
#define NES_MAPPERS_H

#include "cart.h"

/* Installs the hooks and state for the mappers that live in mappers.c
 * (everything except NROM and MMC3). Returns 0 on success, 1 if the mapper
 * number is not one of them, -1 on an allocation failure. */
int mappers_setup(Cart *c);

#endif
