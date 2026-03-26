#pragma once

/**
 * NES PPU (Picture Processing Unit) Emulation
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* PPU registers */
    uint8_t ctrl;       /* $2000 PPUCTRL */
    uint8_t mask;       /* $2001 PPUMASK */
    uint8_t status;     /* $2002 PPUSTATUS */
    uint8_t oam_addr;   /* $2003 OAMADDR */

    /* Internal registers */
    uint16_t v;         /* Current VRAM address (15 bit) */
    uint16_t t;         /* Temporary VRAM address */
    uint8_t  x_fine;    /* Fine X scroll (3 bit) */
    uint8_t  w;         /* Write toggle */

    /* Memory */
    uint8_t  vram[2048];       /* 2 nametables */
    uint8_t  palette[32];      /* Palette RAM */
    uint8_t  oam[256];         /* OAM (sprite data) */

    /* Rendering state */
    int      scanline;
    int      dot;
    uint64_t frame_count;
    bool     nmi_occurred;
    bool     nmi_output;
    bool     sprite0_hit;
    bool     odd_frame;

    /* Tile fetch shift registers */
    uint16_t bg_shift_lo;
    uint16_t bg_shift_hi;
    uint16_t attr_shift_lo;
    uint16_t attr_shift_hi;
    uint8_t  attr_latch_lo;
    uint8_t  attr_latch_hi;

    /* Data buffer for PPUDATA reads */
    uint8_t  data_buf;

    /* Pre-filtered sprite list for current scanline (max 8) */
    uint8_t  scanline_sprites[8];
    int      scanline_sprite_count;

    /* CHR ROM/RAM access callback */
    uint8_t (*chr_read)(uint16_t addr);
    void    (*chr_write)(uint16_t addr, uint8_t val);

    /* Mirroring mode: 0=horizontal, 1=vertical, 2=single0, 3=single1, 4=four */
    uint8_t  mirroring;

    /* Output framebuffer (RGB565) */
    uint16_t *framebuffer;
} nes_ppu_t;

void nes_ppu_init(nes_ppu_t *ppu, uint16_t *fb);
void nes_ppu_reset(nes_ppu_t *ppu);

/** Read PPU register ($2000-$2007 mirrored) */
uint8_t nes_ppu_read(nes_ppu_t *ppu, uint16_t addr);

/** Write PPU register */
void nes_ppu_write(nes_ppu_t *ppu, uint16_t addr, uint8_t val);

/** Execute one PPU dot. Returns true if NMI triggered. */
bool nes_ppu_tick(nes_ppu_t *ppu);

/** OAM DMA write */
void nes_ppu_oam_dma(nes_ppu_t *ppu, const uint8_t *page);
