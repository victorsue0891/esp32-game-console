#pragma once

/**
 * NES Cartridge / iNES ROM loader / Mapper support
 * Supports: Mapper 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM)
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NES_MAPPER_MAX 5

typedef struct nes_cart nes_cart_t;

struct nes_cart {
    /* ROM data */
    uint8_t *prg_rom;
    uint8_t *chr_rom;
    uint8_t  chr_ram[8192];   /* 8K CHR RAM for mapper 0 with no CHR ROM */
    uint8_t  prg_ram[8192];   /* 8K PRG RAM ($6000-$7FFF) */

    /* ROM info */
    int      prg_rom_size;    /* In 16K units */
    int      chr_rom_size;    /* In 8K units */
    int      mapper_id;
    int      mirroring;       /* 0=H, 1=V */

    /* Mapper state */
    union {
        /* Mapper 1 (MMC1) */
        struct {
            uint8_t  shift_reg;
            uint8_t  shift_count;
            uint8_t  control;
            uint8_t  chr_bank0;
            uint8_t  chr_bank1;
            uint8_t  prg_bank;
        } mmc1;

        /* Mapper 2 (UxROM) */
        struct {
            uint8_t prg_bank;
        } uxrom;

        /* Mapper 3 (CNROM) */
        struct {
            uint8_t chr_bank;
        } cnrom;

        /* Mapper 4 (MMC3) */
        struct {
            uint8_t bank_select;   /* $8000 */
            uint8_t bank_regs[8];  /* R0-R7 */
            uint8_t irq_latch;
            uint8_t irq_counter;
            bool    irq_enabled;
            bool    irq_reload;
            bool    irq_pending;
            bool    prg_mode;      /* bit 6 of bank_select */
            bool    chr_invert;    /* bit 7 of bank_select */
        } mmc3;
    } state;

    /* Mapper function pointers */
    uint8_t (*cpu_read)(nes_cart_t *cart, uint16_t addr);
    void    (*cpu_write)(nes_cart_t *cart, uint16_t addr, uint8_t val);
    uint8_t (*chr_read)(nes_cart_t *cart, uint16_t addr);
    void    (*chr_write)(nes_cart_t *cart, uint16_t addr, uint8_t val);

    /* Scanline tick callback (MMC3 IRQ counter) - called once per visible scanline by PPU */
    void    (*scanline_tick)(nes_cart_t *cart);
};

/**
 * Load an iNES ROM file.
 * @param rom_data  Raw .nes file bytes.
 * @param rom_size  Size in bytes.
 * @param cart      Output cartridge structure.
 * @return true on success.
 */
bool nes_cart_load(const uint8_t *rom_data, size_t rom_size, nes_cart_t *cart);

/**
 * Free cartridge resources.
 */
void nes_cart_free(nes_cart_t *cart);
