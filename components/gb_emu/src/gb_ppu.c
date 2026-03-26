/**
 * Game Boy PPU (LCD Controller) Emulation
 * Implements scanline rendering with background, window, and sprites.
 */

#include "gb_ppu.h"
#include <string.h>

/* DMG palette: 4 shades of green -> RGB565 */
static const uint16_t dmg_colors[4] = {
    0xCF39,  /* Lightest (white-green) */
    0x8E0C,  /* Light green */
    0x3186,  /* Dark green */
    0x0861,  /* Darkest (near-black) */
};

/* Timing constants (in T-cycles) */
#define OAM_CYCLES    80
#define DRAW_CYCLES   172   /* Variable, but using fixed approximation */
#define HBLANK_CYCLES 204   /* Variable complement */
#define LINE_CYCLES   456
#define VBLANK_LINES  10

void gb_ppu_init(gb_ppu_t *ppu, uint16_t *fb) {
    memset(ppu, 0, sizeof(*ppu));
    ppu->framebuffer = fb;
}

void gb_ppu_reset(gb_ppu_t *ppu) {
    ppu->lcdc = 0x91;
    ppu->stat = 0;
    ppu->scy = 0;
    ppu->scx = 0;
    ppu->ly = 0;
    ppu->lyc = 0;
    ppu->bgp = 0xFC;
    ppu->obp0 = 0xFF;
    ppu->obp1 = 0xFF;
    ppu->wy = 0;
    ppu->wx = 0;
    ppu->mode = GB_PPU_MODE_OAM;
    ppu->dot_counter = 0;
    ppu->window_line = 0;
    ppu->stat_irq = false;
    ppu->vblank_irq = false;
    memset(ppu->vram, 0, sizeof(ppu->vram));
    memset(ppu->oam, 0, sizeof(ppu->oam));
}

uint8_t gb_ppu_read(gb_ppu_t *ppu, uint16_t addr) {
    if (addr >= 0x8000 && addr < 0xA000) {
        if (ppu->mode == GB_PPU_MODE_DRAW) return 0xFF;
        return ppu->vram[addr - 0x8000];
    }
    if (addr >= 0xFE00 && addr < 0xFEA0) {
        if (ppu->mode == GB_PPU_MODE_OAM || ppu->mode == GB_PPU_MODE_DRAW) return 0xFF;
        return ppu->oam[addr - 0xFE00];
    }
    switch (addr) {
    case 0xFF40: return ppu->lcdc;
    case 0xFF41: return (ppu->stat & 0xF8) | (ppu->ly == ppu->lyc ? 0x04 : 0) | (ppu->mode & 3);
    case 0xFF42: return ppu->scy;
    case 0xFF43: return ppu->scx;
    case 0xFF44: return ppu->ly;
    case 0xFF45: return ppu->lyc;
    case 0xFF47: return ppu->bgp;
    case 0xFF48: return ppu->obp0;
    case 0xFF49: return ppu->obp1;
    case 0xFF4A: return ppu->wy;
    case 0xFF4B: return ppu->wx;
    }
    return 0xFF;
}

void gb_ppu_write(gb_ppu_t *ppu, uint16_t addr, uint8_t val) {
    if (addr >= 0x8000 && addr < 0xA000) {
        if (ppu->mode != GB_PPU_MODE_DRAW)
            ppu->vram[addr - 0x8000] = val;
        return;
    }
    if (addr >= 0xFE00 && addr < 0xFEA0) {
        if (ppu->mode != GB_PPU_MODE_OAM && ppu->mode != GB_PPU_MODE_DRAW)
            ppu->oam[addr - 0xFE00] = val;
        return;
    }
    switch (addr) {
    case 0xFF40: ppu->lcdc = val; break;
    case 0xFF41: ppu->stat = (ppu->stat & 0x07) | (val & 0xF8); break;
    case 0xFF42: ppu->scy = val; break;
    case 0xFF43: ppu->scx = val; break;
    case 0xFF44: /* LY is read-only */ break;
    case 0xFF45: ppu->lyc = val; break;
    case 0xFF46: ppu->dma = val; break; /* DMA handled externally */
    case 0xFF47: ppu->bgp = val; break;
    case 0xFF48: ppu->obp0 = val; break;
    case 0xFF49: ppu->obp1 = val; break;
    case 0xFF4A: ppu->wy = val; break;
    case 0xFF4B: ppu->wx = val; break;
    }
}

/* ---- Rendering a scanline ---- */

static inline uint8_t get_palette_color(uint8_t palette, uint8_t color_id) {
    return (palette >> (color_id * 2)) & 3;
}

static void render_bg_line(gb_ppu_t *ppu, int line) {
    if (!(ppu->lcdc & 0x01)) {
        /* BG disabled, fill white */
        for (int x = 0; x < 160; x++) {
            ppu->framebuffer[line * 160 + x] = dmg_colors[0];
        }
        return;
    }

    uint16_t tile_map  = (ppu->lcdc & 0x08) ? 0x1C00 : 0x1800;
    uint16_t tile_data = (ppu->lcdc & 0x10) ? 0x0000 : 0x0800;
    bool     signed_addr = !(ppu->lcdc & 0x10);

    uint8_t y = (line + ppu->scy) & 0xFF;
    uint8_t tile_row = y >> 3;
    uint8_t pixel_row = y & 7;

    for (int x = 0; x < 160; x++) {
        uint8_t px = (x + ppu->scx) & 0xFF;
        uint8_t tile_col = px >> 3;
        uint8_t pixel_col = px & 7;

        uint16_t map_addr = tile_map + tile_row * 32 + tile_col;
        uint8_t tile_id = ppu->vram[map_addr];

        uint16_t data_addr;
        if (signed_addr) {
            data_addr = tile_data + ((int8_t)tile_id + 128) * 16;
        } else {
            data_addr = tile_data + tile_id * 16;
        }
        data_addr += pixel_row * 2;

        uint8_t lo = ppu->vram[data_addr];
        uint8_t hi = ppu->vram[data_addr + 1];
        uint8_t bit = 7 - pixel_col;
        uint8_t color_id = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
        uint8_t color = get_palette_color(ppu->bgp, color_id);

        ppu->framebuffer[line * 160 + x] = dmg_colors[color];
    }
}

static void render_window_line(gb_ppu_t *ppu, int line) {
    if (!(ppu->lcdc & 0x20)) return;  /* Window disabled */
    if (line < ppu->wy) return;
    if (ppu->wx > 166) return;

    uint16_t tile_map  = (ppu->lcdc & 0x40) ? 0x1C00 : 0x1800;
    uint16_t tile_data = (ppu->lcdc & 0x10) ? 0x0000 : 0x0800;
    bool     signed_addr = !(ppu->lcdc & 0x10);

    uint8_t win_y = ppu->window_line;
    uint8_t tile_row = win_y >> 3;
    uint8_t pixel_row = win_y & 7;

    int start_x = ppu->wx - 7;
    if (start_x < 0) start_x = 0;

    bool rendered = false;
    for (int x = start_x; x < 160; x++) {
        int win_x = x - (ppu->wx - 7);
        if (win_x < 0) continue;
        rendered = true;

        uint8_t tile_col = win_x >> 3;
        uint8_t pixel_col = win_x & 7;

        uint16_t map_addr = tile_map + tile_row * 32 + tile_col;
        uint8_t tile_id = ppu->vram[map_addr];

        uint16_t data_addr;
        if (signed_addr) {
            data_addr = tile_data + ((int8_t)tile_id + 128) * 16;
        } else {
            data_addr = tile_data + tile_id * 16;
        }
        data_addr += pixel_row * 2;

        uint8_t lo = ppu->vram[data_addr];
        uint8_t hi = ppu->vram[data_addr + 1];
        uint8_t bit = 7 - pixel_col;
        uint8_t color_id = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
        uint8_t color = get_palette_color(ppu->bgp, color_id);

        ppu->framebuffer[line * 160 + x] = dmg_colors[color];
    }

    if (rendered) ppu->window_line++;
}

static void render_sprites_line(gb_ppu_t *ppu, int line) {
    if (!(ppu->lcdc & 0x02)) return;  /* Sprites disabled */

    int sprite_height = (ppu->lcdc & 0x04) ? 16 : 8;
    int sprite_count = 0;

    /* Find up to 10 sprites on this line (lowest X has priority) */
    typedef struct { int x; int idx; } sprite_entry_t;
    sprite_entry_t sprites[10];

    for (int i = 0; i < 40 && sprite_count < 10; i++) {
        int sy = ppu->oam[i * 4] - 16;
        if (line >= sy && line < sy + sprite_height) {
            sprites[sprite_count].x = ppu->oam[i * 4 + 1];
            sprites[sprite_count].idx = i;
            sprite_count++;
        }
    }

    /* Draw in reverse order (so lower index has higher priority) */
    for (int s = sprite_count - 1; s >= 0; s--) {
        int i = sprites[s].idx;
        int sy  = ppu->oam[i * 4] - 16;
        int sx  = ppu->oam[i * 4 + 1] - 8;
        uint8_t tile = ppu->oam[i * 4 + 2];
        uint8_t attr = ppu->oam[i * 4 + 3];

        bool flip_y = (attr & 0x40) != 0;
        bool flip_x = (attr & 0x20) != 0;
        bool behind_bg = (attr & 0x80) != 0;
        uint8_t palette = (attr & 0x10) ? ppu->obp1 : ppu->obp0;

        int row = line - sy;
        if (flip_y) row = sprite_height - 1 - row;

        if (sprite_height == 16) tile &= 0xFE;

        uint16_t data_addr = tile * 16 + row * 2;
        uint8_t lo = ppu->vram[data_addr];
        uint8_t hi = ppu->vram[data_addr + 1];

        for (int px = 0; px < 8; px++) {
            int screen_x = sx + px;
            if (screen_x < 0 || screen_x >= 160) continue;

            int bit = flip_x ? px : (7 - px);
            uint8_t color_id = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            if (color_id == 0) continue; /* Transparent */

            /* BG priority check */
            if (behind_bg) {
                uint16_t existing = ppu->framebuffer[line * 160 + screen_x];
                if (existing != dmg_colors[0]) continue;
            }

            uint8_t color = get_palette_color(palette, color_id);
            ppu->framebuffer[line * 160 + screen_x] = dmg_colors[color];
        }
    }
}

static void render_scanline(gb_ppu_t *ppu, int line) {
    if (!(ppu->lcdc & 0x80)) return;  /* LCD off */

    render_bg_line(ppu, line);
    render_window_line(ppu, line);
    render_sprites_line(ppu, line);
}

/* ---- PPU tick ---- */

void gb_ppu_tick(gb_ppu_t *ppu, int cycles) {
    if (!(ppu->lcdc & 0x80)) {
        /* LCD disabled */
        ppu->mode = GB_PPU_MODE_HBLANK;
        ppu->ly = 0;
        ppu->dot_counter = 0;
        return;
    }

    ppu->stat_irq = false;
    ppu->vblank_irq = false;

    ppu->dot_counter += cycles;

    while (ppu->dot_counter >= LINE_CYCLES) {
        ppu->dot_counter -= LINE_CYCLES;
        ppu->ly++;

        if (ppu->ly >= 154) {
            ppu->ly = 0;
            ppu->window_line = 0;
        }

        /* LYC check */
        if (ppu->ly == ppu->lyc && (ppu->stat & 0x40)) {
            ppu->stat_irq = true;
        }

        if (ppu->ly < 144) {
            /* Visible line: render at the start */
            render_scanline(ppu, ppu->ly);
            ppu->mode = GB_PPU_MODE_HBLANK;
            if (ppu->stat & 0x08) ppu->stat_irq = true;
        } else if (ppu->ly == 144) {
            ppu->mode = GB_PPU_MODE_VBLANK;
            ppu->vblank_irq = true;
            if (ppu->stat & 0x10) ppu->stat_irq = true;
        }
    }

    /* Update mode within current line */
    if (ppu->ly < 144) {
        if (ppu->dot_counter < OAM_CYCLES) {
            if (ppu->mode != GB_PPU_MODE_OAM) {
                ppu->mode = GB_PPU_MODE_OAM;
                if (ppu->stat & 0x20) ppu->stat_irq = true;
            }
        } else if (ppu->dot_counter < OAM_CYCLES + DRAW_CYCLES) {
            ppu->mode = GB_PPU_MODE_DRAW;
        } else {
            if (ppu->mode != GB_PPU_MODE_HBLANK) {
                ppu->mode = GB_PPU_MODE_HBLANK;
                if (ppu->stat & 0x08) ppu->stat_irq = true;
            }
        }
    }
}
