/**
 * NES Cartridge / iNES ROM Loader / Mapper Support
 * Supports: Mapper 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM)
 */

#include "nes_cart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "nes_cart";

/* ---- iNES header ---- */
typedef struct {
    uint8_t magic[4];   /* "NES\x1A" */
    uint8_t prg_size;   /* 16K units */
    uint8_t chr_size;   /* 8K units */
    uint8_t flags6;
    uint8_t flags7;
    uint8_t prg_ram;
    uint8_t flags9;
    uint8_t flags10;
    uint8_t padding[5];
} ines_header_t;

/* ==== Mapper 0 (NROM) ==== */
static uint8_t nrom_cpu_read(nes_cart_t *c, uint16_t addr) {
    if (addr >= 0x6000 && addr < 0x8000)
        return c->prg_ram[addr - 0x6000];
    if (addr >= 0x8000) {
        if (c->prg_rom_size == 1)
            return c->prg_rom[(addr - 0x8000) & 0x3FFF];
        return c->prg_rom[addr - 0x8000];
    }
    return 0;
}
static void nrom_cpu_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr >= 0x6000 && addr < 0x8000)
        c->prg_ram[addr - 0x6000] = val;
}
static uint8_t nrom_chr_read(nes_cart_t *c, uint16_t addr) {
    if (c->chr_rom_size == 0)
        return c->chr_ram[addr & 0x1FFF];
    return c->chr_rom[addr & 0x1FFF];
}
static void nrom_chr_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    if (c->chr_rom_size == 0)
        c->chr_ram[addr & 0x1FFF] = val;
}

/* ==== Mapper 1 (MMC1) ==== */

static uint8_t mmc1_cpu_read(nes_cart_t *c, uint16_t addr) {
    if (addr >= 0x6000 && addr < 0x8000)
        return c->prg_ram[addr - 0x6000];
    if (addr >= 0x8000) {
        uint8_t prg_mode = (c->state.mmc1.control >> 2) & 3;
        uint8_t bank = c->state.mmc1.prg_bank & 0x0F;
        int prg_banks = c->prg_rom_size;

        switch (prg_mode) {
        case 0: case 1: { /* 32K mode */
            int b = (bank >> 1) % (prg_banks / 2 > 0 ? prg_banks / 2 : 1);
            return c->prg_rom[b * 0x8000 + (addr - 0x8000)];
        }
        case 2: /* Fix first, switch second */
            if (addr < 0xC000)
                return c->prg_rom[addr - 0x8000];
            return c->prg_rom[(bank % prg_banks) * 0x4000 + (addr - 0xC000)];
        case 3: /* Switch first, fix last */
            if (addr < 0xC000)
                return c->prg_rom[(bank % prg_banks) * 0x4000 + (addr - 0x8000)];
            return c->prg_rom[(prg_banks - 1) * 0x4000 + (addr - 0xC000)];
        }
    }
    return 0;
}

static void mmc1_cpu_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr >= 0x6000 && addr < 0x8000) {
        c->prg_ram[addr - 0x6000] = val;
        return;
    }
    if (addr < 0x8000) return;

    if (val & 0x80) {
        c->state.mmc1.shift_reg = 0;
        c->state.mmc1.shift_count = 0;
        c->state.mmc1.control |= 0x0C;
        return;
    }

    c->state.mmc1.shift_reg |= (val & 1) << c->state.mmc1.shift_count;
    c->state.mmc1.shift_count++;

    if (c->state.mmc1.shift_count == 5) {
        uint8_t reg_val = c->state.mmc1.shift_reg;
        if (addr < 0xA000) {
            c->state.mmc1.control = reg_val;
            /* Update mirroring */
            switch (reg_val & 3) {
            case 0: c->mirroring = 2; break; /* Single lower */
            case 1: c->mirroring = 3; break; /* Single upper */
            case 2: c->mirroring = 1; break; /* Vertical */
            case 3: c->mirroring = 0; break; /* Horizontal */
            }
        } else if (addr < 0xC000) {
            c->state.mmc1.chr_bank0 = reg_val;
        } else if (addr < 0xE000) {
            c->state.mmc1.chr_bank1 = reg_val;
        } else {
            c->state.mmc1.prg_bank = reg_val;
        }
        c->state.mmc1.shift_reg = 0;
        c->state.mmc1.shift_count = 0;
    }
}

static uint8_t mmc1_chr_read(nes_cart_t *c, uint16_t addr) {
    if (c->chr_rom_size == 0)
        return c->chr_ram[addr & 0x1FFF];

    uint8_t chr_mode = (c->state.mmc1.control >> 4) & 1;
    int chr_banks = c->chr_rom_size * 2; /* In 4K units */

    if (chr_mode == 0) {
        /* 8K mode */
        int bank = (c->state.mmc1.chr_bank0 >> 1) % (chr_banks / 2 > 0 ? chr_banks / 2 : 1);
        return c->chr_rom[bank * 0x2000 + (addr & 0x1FFF)];
    } else {
        /* 4K mode */
        if (addr < 0x1000) {
            int bank = c->state.mmc1.chr_bank0 % chr_banks;
            return c->chr_rom[bank * 0x1000 + (addr & 0xFFF)];
        } else {
            int bank = c->state.mmc1.chr_bank1 % chr_banks;
            return c->chr_rom[bank * 0x1000 + (addr & 0xFFF)];
        }
    }
}

static void mmc1_chr_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    if (c->chr_rom_size == 0)
        c->chr_ram[addr & 0x1FFF] = val;
}

/* ==== Mapper 2 (UxROM) ==== */
static uint8_t uxrom_cpu_read(nes_cart_t *c, uint16_t addr) {
    if (addr >= 0x6000 && addr < 0x8000)
        return c->prg_ram[addr - 0x6000];
    if (addr >= 0xC000)
        return c->prg_rom[(c->prg_rom_size - 1) * 0x4000 + (addr - 0xC000)];
    if (addr >= 0x8000) {
        int bank = c->state.uxrom.prg_bank % c->prg_rom_size;
        return c->prg_rom[bank * 0x4000 + (addr - 0x8000)];
    }
    return 0;
}
static void uxrom_cpu_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr >= 0x8000) c->state.uxrom.prg_bank = val;
    else if (addr >= 0x6000) c->prg_ram[addr - 0x6000] = val;
}

/* ==== Mapper 3 (CNROM) ==== */
static uint8_t cnrom_cpu_read(nes_cart_t *c, uint16_t addr) {
    if (addr >= 0x6000 && addr < 0x8000)
        return c->prg_ram[addr - 0x6000];
    if (addr >= 0x8000) {
        if (c->prg_rom_size == 1)
            return c->prg_rom[(addr - 0x8000) & 0x3FFF];
        return c->prg_rom[addr - 0x8000];
    }
    return 0;
}
static void cnrom_cpu_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr >= 0x8000) c->state.cnrom.chr_bank = val & 0x03;
    else if (addr >= 0x6000) c->prg_ram[addr - 0x6000] = val;
}
static uint8_t cnrom_chr_read(nes_cart_t *c, uint16_t addr) {
    int bank = c->state.cnrom.chr_bank % (c->chr_rom_size > 0 ? c->chr_rom_size : 1);
    return c->chr_rom[bank * 0x2000 + (addr & 0x1FFF)];
}
static void cnrom_chr_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    (void)c; (void)addr; (void)val;
}

/* ==== Mapper 4 (MMC3) ==== */

static int mmc3_prg_bank_count(nes_cart_t *c) {
    return c->prg_rom_size * 2; /* 8K units */
}
static int mmc3_chr_bank_count(nes_cart_t *c) {
    return c->chr_rom_size * 8; /* 1K units */
}

static uint8_t mmc3_cpu_read(nes_cart_t *c, uint16_t addr) {
    if (addr >= 0x6000 && addr < 0x8000)
        return c->prg_ram[addr - 0x6000];
    if (addr < 0x8000) return 0;

    int prg_count = mmc3_prg_bank_count(c);
    if (prg_count == 0) return 0;
    int bank;

    if (addr < 0xA000) {
        /* $8000-$9FFF */
        if (c->state.mmc3.prg_mode)
            bank = prg_count - 2;  /* second-to-last */
        else
            bank = c->state.mmc3.bank_regs[6] % prg_count;
    } else if (addr < 0xC000) {
        /* $A000-$BFFF - always R7 */
        bank = c->state.mmc3.bank_regs[7] % prg_count;
    } else if (addr < 0xE000) {
        /* $C000-$DFFF */
        if (c->state.mmc3.prg_mode)
            bank = c->state.mmc3.bank_regs[6] % prg_count;
        else
            bank = prg_count - 2;
    } else {
        /* $E000-$FFFF - always last */
        bank = prg_count - 1;
    }
    return c->prg_rom[bank * 0x2000 + (addr & 0x1FFF)];
}

static void mmc3_cpu_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr >= 0x6000 && addr < 0x8000) {
        c->prg_ram[addr - 0x6000] = val;
        return;
    }
    if (addr < 0x8000) return;

    bool even = (addr & 1) == 0;

    if (addr < 0xA000) {
        if (even) {
            /* $8000 - Bank select */
            c->state.mmc3.bank_select = val & 0x07;
            c->state.mmc3.prg_mode    = (val & 0x40) != 0;
            c->state.mmc3.chr_invert  = (val & 0x80) != 0;
        } else {
            /* $8001 - Bank data */
            c->state.mmc3.bank_regs[c->state.mmc3.bank_select] = val;
        }
    } else if (addr < 0xC000) {
        if (even) {
            /* $A000 - Mirroring */
            c->mirroring = (val & 1) ? 0 : 1; /* 0=H, 1=V */
        }
        /* $A001 - PRG RAM protect (ignored) */
    } else if (addr < 0xE000) {
        if (even) {
            /* $C000 - IRQ latch */
            c->state.mmc3.irq_latch = val;
        } else {
            /* $C001 - IRQ reload */
            c->state.mmc3.irq_reload = true;
        }
    } else {
        if (even) {
            /* $E000 - IRQ disable + acknowledge */
            c->state.mmc3.irq_enabled = false;
            c->state.mmc3.irq_pending = false;
        } else {
            /* $E001 - IRQ enable */
            c->state.mmc3.irq_enabled = true;
        }
    }
}

static uint8_t mmc3_chr_read(nes_cart_t *c, uint16_t addr) {
    if (c->chr_rom_size == 0)
        return c->chr_ram[addr & 0x1FFF];

    int chr_count = mmc3_chr_bank_count(c);
    if (chr_count == 0) return 0;

    /* 8 x 1K CHR banks, order depends on chr_invert */
    int slot = (addr >> 10) & 7; /* 0-7 for $0000-$1FFF in 1K steps */
    int bank;

    if (!c->state.mmc3.chr_invert) {
        /* Normal: R0 at $0000 (2K), R1 at $0800 (2K), R2-R5 at $1000-$1C00 (1K each) */
        switch (slot) {
        case 0: bank = (c->state.mmc3.bank_regs[0] & 0xFE);     break;
        case 1: bank = (c->state.mmc3.bank_regs[0] & 0xFE) | 1; break;
        case 2: bank = (c->state.mmc3.bank_regs[1] & 0xFE);     break;
        case 3: bank = (c->state.mmc3.bank_regs[1] & 0xFE) | 1; break;
        case 4: bank =  c->state.mmc3.bank_regs[2]; break;
        case 5: bank =  c->state.mmc3.bank_regs[3]; break;
        case 6: bank =  c->state.mmc3.bank_regs[4]; break;
        case 7: bank =  c->state.mmc3.bank_regs[5]; break;
        default: bank = 0; break;
        }
    } else {
        /* Inverted: R2-R5 at $0000-$0C00 (1K each), R0 at $1000 (2K), R1 at $1800 (2K) */
        switch (slot) {
        case 0: bank =  c->state.mmc3.bank_regs[2]; break;
        case 1: bank =  c->state.mmc3.bank_regs[3]; break;
        case 2: bank =  c->state.mmc3.bank_regs[4]; break;
        case 3: bank =  c->state.mmc3.bank_regs[5]; break;
        case 4: bank = (c->state.mmc3.bank_regs[0] & 0xFE);     break;
        case 5: bank = (c->state.mmc3.bank_regs[0] & 0xFE) | 1; break;
        case 6: bank = (c->state.mmc3.bank_regs[1] & 0xFE);     break;
        case 7: bank = (c->state.mmc3.bank_regs[1] & 0xFE) | 1; break;
        default: bank = 0; break;
        }
    }
    bank %= chr_count;
    return c->chr_rom[bank * 0x0400 + (addr & 0x03FF)];
}

static void mmc3_chr_write(nes_cart_t *c, uint16_t addr, uint8_t val) {
    if (c->chr_rom_size == 0)
        c->chr_ram[addr & 0x1FFF] = val;
}

static void mmc3_scanline_tick(nes_cart_t *c) {
    if (c->state.mmc3.irq_counter == 0 || c->state.mmc3.irq_reload) {
        c->state.mmc3.irq_counter = c->state.mmc3.irq_latch;
        c->state.mmc3.irq_reload = false;
    } else {
        c->state.mmc3.irq_counter--;
    }
    if (c->state.mmc3.irq_counter == 0 && c->state.mmc3.irq_enabled) {
        c->state.mmc3.irq_pending = true;
    }
}

/* ==== Public API ==== */

bool nes_cart_load(const uint8_t *rom_data, size_t rom_size, nes_cart_t *cart) {
    if (rom_size < 16) return false;

    const ines_header_t *hdr = (const ines_header_t *)rom_data;
    if (hdr->magic[0] != 'N' || hdr->magic[1] != 'E' ||
        hdr->magic[2] != 'S' || hdr->magic[3] != 0x1A) {
        ESP_LOGE(TAG, "Invalid iNES header");
        return false;
    }

    memset(cart, 0, sizeof(*cart));
    cart->prg_rom_size = hdr->prg_size;  /* In 16K units */
    cart->chr_rom_size = hdr->chr_size;  /* In 8K units */
    cart->mapper_id = (hdr->flags6 >> 4) | (hdr->flags7 & 0xF0);
    cart->mirroring = (hdr->flags6 & 1) ? 1 : 0;

    bool has_trainer = (hdr->flags6 & 0x04) != 0;
    size_t offset = 16 + (has_trainer ? 512 : 0);

    size_t prg_bytes = cart->prg_rom_size * 16384;
    size_t chr_bytes = cart->chr_rom_size * 8192;

    if (offset + prg_bytes + chr_bytes > rom_size) {
        ESP_LOGE(TAG, "ROM too small for declared PRG/CHR size");
        return false;
    }

    /* Allocate PRG ROM in PSRAM */
    cart->prg_rom = heap_caps_malloc(prg_bytes, MALLOC_CAP_SPIRAM);
    if (!cart->prg_rom) {
        ESP_LOGE(TAG, "Failed to allocate PRG ROM (%d bytes)", (int)prg_bytes);
        return false;
    }
    memcpy(cart->prg_rom, rom_data + offset, prg_bytes);
    offset += prg_bytes;

    /* Allocate CHR ROM if present */
    if (chr_bytes > 0) {
        cart->chr_rom = heap_caps_malloc(chr_bytes, MALLOC_CAP_SPIRAM);
        if (!cart->chr_rom) {
            ESP_LOGE(TAG, "Failed to allocate CHR ROM (%d bytes)", (int)chr_bytes);
            free(cart->prg_rom);
            return false;
        }
        memcpy(cart->chr_rom, rom_data + offset, chr_bytes);
    }

    memset(cart->chr_ram, 0, sizeof(cart->chr_ram));
    memset(cart->prg_ram, 0, sizeof(cart->prg_ram));

    /* Set up mapper function pointers */
    cart->scanline_tick = NULL;

    switch (cart->mapper_id) {
    case 0:
        cart->cpu_read = nrom_cpu_read;
        cart->cpu_write = nrom_cpu_write;
        cart->chr_read = nrom_chr_read;
        cart->chr_write = nrom_chr_write;
        break;
    case 1:
        cart->cpu_read = mmc1_cpu_read;
        cart->cpu_write = mmc1_cpu_write;
        cart->chr_read = mmc1_chr_read;
        cart->chr_write = mmc1_chr_write;
        cart->state.mmc1.control = 0x0C;
        break;
    case 2:
        cart->cpu_read = uxrom_cpu_read;
        cart->cpu_write = uxrom_cpu_write;
        cart->chr_read = nrom_chr_read;
        cart->chr_write = nrom_chr_write;
        break;
    case 3:
        cart->cpu_read = cnrom_cpu_read;
        cart->cpu_write = cnrom_cpu_write;
        cart->chr_read = cnrom_chr_read;
        cart->chr_write = cnrom_chr_write;
        break;
    case 4:
        cart->cpu_read = mmc3_cpu_read;
        cart->cpu_write = mmc3_cpu_write;
        cart->chr_read = mmc3_chr_read;
        cart->chr_write = mmc3_chr_write;
        cart->scanline_tick = mmc3_scanline_tick;
        memset(&cart->state.mmc3, 0, sizeof(cart->state.mmc3));
        break;
    default:
        ESP_LOGW(TAG, "Unsupported mapper %d, falling back to NROM", cart->mapper_id);
        cart->cpu_read = nrom_cpu_read;
        cart->cpu_write = nrom_cpu_write;
        cart->chr_read = nrom_chr_read;
        cart->chr_write = nrom_chr_write;
        break;
    }

    ESP_LOGI(TAG, "ROM loaded: PRG=%dK CHR=%dK Mapper=%d Mirror=%s",
             cart->prg_rom_size * 16, cart->chr_rom_size * 8,
             cart->mapper_id, cart->mirroring ? "V" : "H");
    return true;
}

void nes_cart_free(nes_cart_t *cart) {
    if (cart->prg_rom) { free(cart->prg_rom); cart->prg_rom = NULL; }
    if (cart->chr_rom) { free(cart->chr_rom); cart->chr_rom = NULL; }
}
