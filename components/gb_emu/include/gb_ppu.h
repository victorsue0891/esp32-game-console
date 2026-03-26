#pragma once

/**
 * Game Boy PPU (LCD Controller)
 */

#include <stdint.h>
#include <stdbool.h>

/* PPU modes */
#define GB_PPU_MODE_HBLANK  0
#define GB_PPU_MODE_VBLANK  1
#define GB_PPU_MODE_OAM     2
#define GB_PPU_MODE_DRAW    3

typedef struct {
    /* LCD registers */
    uint8_t lcdc;       /* FF40 - LCD Control */
    uint8_t stat;       /* FF41 - LCD Status */
    uint8_t scy;        /* FF42 - Scroll Y */
    uint8_t scx;        /* FF43 - Scroll X */
    uint8_t ly;         /* FF44 - Current line */
    uint8_t lyc;        /* FF45 - LY Compare */
    uint8_t bgp;        /* FF47 - BG Palette */
    uint8_t obp0;       /* FF48 - OBJ Palette 0 */
    uint8_t obp1;       /* FF49 - OBJ Palette 1 */
    uint8_t wy;         /* FF4A - Window Y */
    uint8_t wx;         /* FF4B - Window X */
    uint8_t dma;        /* FF46 - DMA trigger */

    /* Video RAM and OAM */
    uint8_t vram[8192];
    uint8_t oam[160];

    /* Internal state */
    int     mode;
    int     dot_counter;
    int     window_line;
    bool    stat_irq;
    bool    vblank_irq;

    /* Output */
    uint16_t *framebuffer;
} gb_ppu_t;

void gb_ppu_init(gb_ppu_t *ppu, uint16_t *fb);
void gb_ppu_reset(gb_ppu_t *ppu);
void gb_ppu_tick(gb_ppu_t *ppu, int cycles);
uint8_t gb_ppu_read(gb_ppu_t *ppu, uint16_t addr);
void gb_ppu_write(gb_ppu_t *ppu, uint16_t addr, uint8_t val);
