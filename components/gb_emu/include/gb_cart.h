#pragma once

/**
 * Game Boy Cartridge / MBC Support
 * ROM-only, MBC1, MBC2, MBC3, MBC5
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct gb_cart gb_cart_t;

struct gb_cart {
    uint8_t *rom;
    size_t   rom_size;
    uint8_t *ram;
    size_t   ram_size;
    uint8_t  mbc_type;   /* 0=ROM-only, 1=MBC1, 2=MBC2, 3=MBC3, 5=MBC5 */
    char     title[17];

    /* MBC state */
    bool     ram_enable;
    uint16_t rom_bank;
    uint8_t  ram_bank;
    uint8_t  mode;       /* MBC1 banking mode */

    /* Mapper function pointers */
    uint8_t (*read)(gb_cart_t *c, uint16_t addr);
    void    (*write)(gb_cart_t *c, uint16_t addr, uint8_t val);
};

bool gb_cart_load(const uint8_t *rom_data, size_t rom_size, gb_cart_t *cart);
void gb_cart_free(gb_cart_t *cart);
