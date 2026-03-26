/**
 * NES PPU (Picture Processing Unit) Emulation
 * Implements scanline-accurate rendering with background and sprite support.
 */

#include "nes_ppu.h"
#include <string.h>

/* NES system palette -> RGB565 lookup table (2C02 palette) */
static const uint16_t nes_palette_rgb565[64] = {
    0x4228, 0x1089, 0x0889, 0x3025, 0x4803, 0x5001, 0x4800, 0x3900,
    0x2180, 0x0200, 0x0240, 0x0220, 0x0228, 0x0000, 0x0000, 0x0000,
    0x6B6D, 0x194F, 0x28CF, 0x504B, 0x7007, 0x7803, 0x7840, 0x6180,
    0x4240, 0x0B00, 0x0380, 0x0360, 0x0350, 0x0000, 0x0000, 0x0000,
    0xAE59, 0x3DBF, 0x4D3F, 0x7CBF, 0xA41B, 0xAC17, 0xAC93, 0x9D31,
    0x8DD0, 0x5650, 0x2E90, 0x2EB0, 0x26D5, 0x2965, 0x0000, 0x0000,
    0xDEFC, 0x9EBF, 0xAE5F, 0xBDFF, 0xD5BF, 0xDDBF, 0xDE1B, 0xD699,
    0xCED8, 0xAF58, 0x9F78, 0x8F9B, 0x97BC, 0x8C71, 0x0000, 0x0000,
};

/* Nametable mirroring address translation */
static uint16_t mirror_nt_addr(nes_ppu_t *ppu, uint16_t addr) {
    addr &= 0x0FFF;
    switch (ppu->mirroring) {
    case 0: /* Horizontal */
        if (addr < 0x0800) return addr & 0x03FF;
        return 0x0400 + (addr & 0x03FF);
    case 1: /* Vertical */
        return addr & 0x07FF;
    case 2: /* Single screen lower */
        return addr & 0x03FF;
    case 3: /* Single screen upper */
        return 0x0400 + (addr & 0x03FF);
    default: /* Four screen */
        return addr;
    }
}

static uint8_t ppu_vram_read(nes_ppu_t *ppu, uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        return ppu->chr_read ? ppu->chr_read(addr) : 0;
    } else if (addr < 0x3F00) {
        return ppu->vram[mirror_nt_addr(ppu, addr - 0x2000)];
    } else {
        /* Palette */
        uint16_t pa = addr & 0x1F;
        if (pa == 0x10 || pa == 0x14 || pa == 0x18 || pa == 0x1C)
            pa &= 0x0F;
        return ppu->palette[pa];
    }
}

static void ppu_vram_write(nes_ppu_t *ppu, uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        if (ppu->chr_write) ppu->chr_write(addr, val);
    } else if (addr < 0x3F00) {
        ppu->vram[mirror_nt_addr(ppu, addr - 0x2000)] = val;
    } else {
        uint16_t pa = addr & 0x1F;
        if (pa == 0x10 || pa == 0x14 || pa == 0x18 || pa == 0x1C)
            pa &= 0x0F;
        ppu->palette[pa] = val & 0x3F;
    }
}

void nes_ppu_init(nes_ppu_t *ppu, uint16_t *fb) {
    memset(ppu, 0, sizeof(*ppu));
    ppu->framebuffer = fb;
}

void nes_ppu_reset(nes_ppu_t *ppu) {
    ppu->ctrl = 0;
    ppu->mask = 0;
    ppu->status = 0;
    ppu->oam_addr = 0;
    ppu->v = 0;
    ppu->t = 0;
    ppu->x_fine = 0;
    ppu->w = 0;
    ppu->scanline = 0;
    ppu->dot = 0;
    ppu->frame_count = 0;
    ppu->nmi_occurred = false;
    ppu->nmi_output = false;
    ppu->odd_frame = false;
    ppu->data_buf = 0;
}

/* PPU register read ($2000-$2007) */
uint8_t nes_ppu_read(nes_ppu_t *ppu, uint16_t addr) {
    switch (addr & 7) {
    case 2: { /* PPUSTATUS */
        uint8_t ret = (ppu->nmi_occurred ? 0x80 : 0) |
                      (ppu->sprite0_hit ? 0x40 : 0) |
                      (ppu->status & 0x1F);
        ppu->nmi_occurred = false;
        ppu->w = 0;
        return ret;
    }
    case 4: /* OAMDATA */
        return ppu->oam[ppu->oam_addr];
    case 7: { /* PPUDATA */
        uint8_t data;
        if ((ppu->v & 0x3FFF) >= 0x3F00) {
            data = ppu_vram_read(ppu, ppu->v);
            ppu->data_buf = ppu_vram_read(ppu, ppu->v - 0x1000);
        } else {
            data = ppu->data_buf;
            ppu->data_buf = ppu_vram_read(ppu, ppu->v);
        }
        ppu->v += (ppu->ctrl & 0x04) ? 32 : 1;
        return data;
    }
    default:
        return 0;
    }
}

/* PPU register write ($2000-$2007) */
void nes_ppu_write(nes_ppu_t *ppu, uint16_t addr, uint8_t val) {
    switch (addr & 7) {
    case 0: /* PPUCTRL */
        ppu->ctrl = val;
        ppu->nmi_output = (val & 0x80) != 0;
        ppu->t = (ppu->t & 0xF3FF) | ((uint16_t)(val & 0x03) << 10);
        break;
    case 1: /* PPUMASK */
        ppu->mask = val;
        break;
    case 3: /* OAMADDR */
        ppu->oam_addr = val;
        break;
    case 4: /* OAMDATA */
        ppu->oam[ppu->oam_addr++] = val;
        break;
    case 5: /* PPUSCROLL */
        if (ppu->w == 0) {
            ppu->t = (ppu->t & 0xFFE0) | (val >> 3);
            ppu->x_fine = val & 0x07;
            ppu->w = 1;
        } else {
            ppu->t = (ppu->t & 0x8C1F) |
                     ((uint16_t)(val & 0x07) << 12) |
                     ((uint16_t)(val >> 3) << 5);
            ppu->w = 0;
        }
        break;
    case 6: /* PPUADDR */
        if (ppu->w == 0) {
            ppu->t = (ppu->t & 0x00FF) | ((uint16_t)(val & 0x3F) << 8);
            ppu->w = 1;
        } else {
            ppu->t = (ppu->t & 0xFF00) | val;
            ppu->v = ppu->t;
            ppu->w = 0;
        }
        break;
    case 7: /* PPUDATA */
        ppu_vram_write(ppu, ppu->v, val);
        ppu->v += (ppu->ctrl & 0x04) ? 32 : 1;
        break;
    }
}

void nes_ppu_oam_dma(nes_ppu_t *ppu, const uint8_t *page) {
    memcpy(ppu->oam, page, 256);
}

/* ---- Rendering helpers ---- */

static inline void increment_x(nes_ppu_t *ppu) {
    if ((ppu->v & 0x001F) == 31) {
        ppu->v &= ~0x001F;
        ppu->v ^= 0x0400;
    } else {
        ppu->v++;
    }
}

static inline void increment_y(nes_ppu_t *ppu) {
    if ((ppu->v & 0x7000) != 0x7000) {
        ppu->v += 0x1000;
    } else {
        ppu->v &= ~0x7000;
        int y = (ppu->v & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            ppu->v ^= 0x0800;
        } else if (y == 31) {
            y = 0;
        } else {
            y++;
        }
        ppu->v = (ppu->v & ~0x03E0) | (y << 5);
    }
}

static inline void copy_x(nes_ppu_t *ppu) {
    ppu->v = (ppu->v & 0xFBE0) | (ppu->t & 0x041F);
}

static inline void copy_y(nes_ppu_t *ppu) {
    ppu->v = (ppu->v & 0x841F) | (ppu->t & 0x7BE0);
}

static void render_pixel(nes_ppu_t *ppu) {
    int x = ppu->dot - 1;
    int y = ppu->scanline;

    if (x < 0 || x >= 256 || y < 0 || y >= 240) return;

    uint8_t bg_pixel = 0;
    uint8_t bg_palette = 0;
    uint8_t sp_pixel = 0;
    uint8_t sp_palette = 0;
    bool sp_priority = false;
    bool sp_zero = false;

    /* Background rendering */
    if (ppu->mask & 0x08) {
        if ((ppu->mask & 0x02) || x >= 8) {
            uint16_t data = (ppu->bg_shift_hi << 1) | ppu->bg_shift_lo;
            /* Not quite right; let's use the shift registers properly */
            uint8_t bit = 15 - ppu->x_fine;
            bg_pixel = ((ppu->bg_shift_lo >> (15 - ppu->x_fine)) & 1) |
                       (((ppu->bg_shift_hi >> (15 - ppu->x_fine)) & 1) << 1);
            bg_palette = ((ppu->attr_shift_lo >> (7 - ppu->x_fine)) & 1) |
                         (((ppu->attr_shift_hi >> (7 - ppu->x_fine)) & 1) << 1);
            (void)data;
            (void)bit;
        }
    }

    /* Sprite rendering (uses pre-filtered scanline sprite list) */
    if (ppu->mask & 0x10) {
        if ((ppu->mask & 0x04) || x >= 8) {
            int sprite_height = (ppu->ctrl & 0x20) ? 16 : 8;
            for (int s = 0; s < ppu->scanline_sprite_count; s++) {
                int i = ppu->scanline_sprites[s];
                int spy = ppu->oam[i * 4 + 0];
                int sptile = ppu->oam[i * 4 + 1];
                int spattr = ppu->oam[i * 4 + 2];
                int spx = ppu->oam[i * 4 + 3];

                if (x < spx || x >= spx + 8) continue;

                int row = y - spy;
                /* Vertical flip */
                if (spattr & 0x80) row = sprite_height - 1 - row;

                uint16_t tile_addr;
                if (sprite_height == 8) {
                    uint16_t base = (ppu->ctrl & 0x08) ? 0x1000 : 0;
                    tile_addr = base + sptile * 16 + row;
                } else {
                    uint16_t base = (sptile & 1) ? 0x1000 : 0;
                    int tile = sptile & 0xFE;
                    if (row >= 8) { tile++; row -= 8; }
                    tile_addr = base + tile * 16 + row;
                }

                int col = x - spx;
                if (spattr & 0x40) col = 7 - col; /* Horizontal flip */

                uint8_t lo = ppu->chr_read ? ppu->chr_read(tile_addr) : 0;
                uint8_t hi = ppu->chr_read ? ppu->chr_read(tile_addr + 8) : 0;
                uint8_t pixel = ((lo >> (7 - col)) & 1) | (((hi >> (7 - col)) & 1) << 1);

                if (pixel == 0) continue;

                if (i == 0 && bg_pixel != 0 && x < 255) {
                    sp_zero = true;
                }

                sp_pixel = pixel;
                sp_palette = (spattr & 0x03) + 4;
                sp_priority = (spattr & 0x20) != 0;
                break;
            }
        }
    }

    /* Priority mux */
    uint8_t color_idx;
    if (bg_pixel == 0 && sp_pixel == 0) {
        color_idx = ppu_vram_read(ppu, 0x3F00);
    } else if (bg_pixel == 0) {
        color_idx = ppu_vram_read(ppu, 0x3F00 + sp_palette * 4 + sp_pixel);
    } else if (sp_pixel == 0) {
        color_idx = ppu_vram_read(ppu, 0x3F00 + bg_palette * 4 + bg_pixel);
    } else {
        if (sp_zero) ppu->sprite0_hit = true;
        if (sp_priority) {
            color_idx = ppu_vram_read(ppu, 0x3F00 + bg_palette * 4 + bg_pixel);
        } else {
            color_idx = ppu_vram_read(ppu, 0x3F00 + sp_palette * 4 + sp_pixel);
        }
    }

    ppu->framebuffer[y * 256 + x] = nes_palette_rgb565[color_idx & 0x3F];
}

/* Pre-filter sprites for the current scanline (max 8, like real NES) */
static void evaluate_sprites_for_scanline(nes_ppu_t *ppu, int scanline) {
    ppu->scanline_sprite_count = 0;
    int sprite_height = (ppu->ctrl & 0x20) ? 16 : 8;
    for (int i = 0; i < 64 && ppu->scanline_sprite_count < 8; i++) {
        int spy = ppu->oam[i * 4 + 0];
        int row = scanline - spy;
        if (row >= 0 && row < sprite_height) {
            ppu->scanline_sprites[ppu->scanline_sprite_count++] = (uint8_t)i;
        }
    }
}

/* Fetch background tile data and feed shift registers */
static void fetch_bg_tile(nes_ppu_t *ppu) {
    int cycle = ppu->dot;

    /* Shift registers shift every cycle during visible range */
    ppu->bg_shift_lo <<= 1;
    ppu->bg_shift_hi <<= 1;
    ppu->attr_shift_lo = (ppu->attr_shift_lo << 1) | ppu->attr_latch_lo;
    ppu->attr_shift_hi = (ppu->attr_shift_hi << 1) | ppu->attr_latch_hi;

    switch (cycle & 7) {
    case 1: {
        /* Nametable byte */
        uint16_t nt_addr = 0x2000 | (ppu->v & 0x0FFF);
        uint8_t nt_byte = ppu_vram_read(ppu, nt_addr);

        /* Attribute byte */
        uint16_t at_addr = 0x23C0 | (ppu->v & 0x0C00) |
                           ((ppu->v >> 4) & 0x38) | ((ppu->v >> 2) & 0x07);
        uint8_t at_byte = ppu_vram_read(ppu, at_addr);
        int shift = ((ppu->v >> 4) & 4) | (ppu->v & 2);
        uint8_t attr = (at_byte >> shift) & 3;
        ppu->attr_latch_lo = attr & 1;
        ppu->attr_latch_hi = (attr >> 1) & 1;

        /* Pattern table */
        uint16_t base = (ppu->ctrl & 0x10) ? 0x1000 : 0;
        uint16_t fine_y = (ppu->v >> 12) & 7;
        uint16_t pat_addr = base + nt_byte * 16 + fine_y;
        uint8_t lo = ppu->chr_read ? ppu->chr_read(pat_addr) : 0;
        uint8_t hi = ppu->chr_read ? ppu->chr_read(pat_addr + 8) : 0;

        /* Load into shift registers */
        ppu->bg_shift_lo = (ppu->bg_shift_lo & 0xFF00) | lo;
        ppu->bg_shift_hi = (ppu->bg_shift_hi & 0xFF00) | hi;
    } break;
    case 0:
        if (cycle >= 8 && cycle <= 256) {
            increment_x(ppu);
        }
        break;
    }
}

bool nes_ppu_tick(nes_ppu_t *ppu) {
    bool nmi_triggered = false;
    bool rendering = (ppu->mask & 0x18) != 0;

    /* Pre-render scanline (-1 / 261) */
    if (ppu->scanline == 261) {
        if (ppu->dot == 1) {
            ppu->nmi_occurred = false;
            ppu->sprite0_hit = false;
            ppu->status &= ~0x40;
        }
        if (rendering) {
            if (ppu->dot >= 280 && ppu->dot <= 304) {
                copy_y(ppu);
            }
            if (ppu->dot >= 1 && ppu->dot <= 256) {
                fetch_bg_tile(ppu);
            }
            if (ppu->dot == 256) increment_y(ppu);
            if (ppu->dot == 257) copy_x(ppu);
        }
    }
    /* Visible scanlines (0-239) */
    else if (ppu->scanline < 240) {
        if (rendering) {
            if (ppu->dot == 1) {
                evaluate_sprites_for_scanline(ppu, ppu->scanline);
            }
            if (ppu->dot >= 1 && ppu->dot <= 256) {
                render_pixel(ppu);
                fetch_bg_tile(ppu);
            }
            if (ppu->dot == 256) increment_y(ppu);
            if (ppu->dot == 257) copy_x(ppu);
        } else if (ppu->dot >= 1 && ppu->dot <= 256) {
            /* When rendering disabled, show background color */
            int x = ppu->dot - 1;
            uint8_t bg_color = ppu_vram_read(ppu, 0x3F00);
            ppu->framebuffer[ppu->scanline * 256 + x] = nes_palette_rgb565[bg_color & 0x3F];
        }
    }
    /* Post-render scanline (240): idle */
    /* VBlank scanlines (241-260) */
    else if (ppu->scanline == 241 && ppu->dot == 1) {
        ppu->nmi_occurred = true;
        if (ppu->nmi_output) {
            nmi_triggered = true;
        }
    }

    /* Advance dot/scanline */
    ppu->dot++;
    if (ppu->dot > 340) {
        ppu->dot = 0;
        ppu->scanline++;
        if (ppu->scanline > 261) {
            ppu->scanline = 0;
            ppu->frame_count++;
            ppu->odd_frame = !ppu->odd_frame;
            /* Skip dot 0 on odd frames when rendering */
            if (ppu->odd_frame && rendering) {
                ppu->dot = 1;
            }
        }
    }

    return nmi_triggered;
}
