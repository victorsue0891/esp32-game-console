/**
 * Game Boy Cartridge / MBC Support
 * ROM-only, MBC1, MBC2, MBC3, MBC5
 */

#include "gb_cart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "gb_cart";

/* ROM size lookup (header byte 0x148) */
static size_t rom_size_from_code(uint8_t code) {
    if (code <= 8) return (size_t)32768 << code;
    return 32768;
}

/* RAM size lookup (header byte 0x149) */
static size_t ram_size_from_code(uint8_t code) {
    switch (code) {
    case 0: return 0;
    case 1: return 2048;
    case 2: return 8192;
    case 3: return 32768;
    case 4: return 131072;
    case 5: return 65536;
    default: return 0;
    }
}

/* ==== ROM-only ==== */
static uint8_t rom_only_read(gb_cart_t *c, uint16_t addr) {
    if (addr < 0x8000 && addr < (uint16_t)c->rom_size) return c->rom[addr];
    if (addr >= 0xA000 && addr < 0xC000 && c->ram && c->ram_enable)
        return c->ram[addr - 0xA000];
    return 0xFF;
}
static void rom_only_write(gb_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr >= 0xA000 && addr < 0xC000 && c->ram && c->ram_enable)
        c->ram[addr - 0xA000] = val;
}

/* ==== MBC1 ==== */
static uint8_t mbc1_read(gb_cart_t *c, uint16_t addr) {
    if (addr < 0x4000) return c->rom[addr];
    if (addr < 0x8000) {
        int bank = c->rom_bank;
        if (bank == 0) bank = 1;
        if (c->mode == 0) bank |= (c->ram_bank << 5);
        size_t offset = (size_t)bank * 0x4000 + (addr - 0x4000);
        return c->rom[offset % c->rom_size];
    }
    if (addr >= 0xA000 && addr < 0xC000 && c->ram && c->ram_enable) {
        int rbank = (c->mode == 1) ? c->ram_bank : 0;
        size_t offset = (size_t)rbank * 0x2000 + (addr - 0xA000);
        return c->ram[offset % c->ram_size];
    }
    return 0xFF;
}
static void mbc1_write(gb_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr < 0x2000) { c->ram_enable = ((val & 0x0F) == 0x0A); }
    else if (addr < 0x4000) { c->rom_bank = val & 0x1F; if (c->rom_bank == 0) c->rom_bank = 1; }
    else if (addr < 0x6000) { c->ram_bank = val & 0x03; }
    else if (addr < 0x8000) { c->mode = val & 1; }
    else if (addr >= 0xA000 && addr < 0xC000 && c->ram && c->ram_enable) {
        int rbank = (c->mode == 1) ? c->ram_bank : 0;
        size_t offset = (size_t)rbank * 0x2000 + (addr - 0xA000);
        c->ram[offset % c->ram_size] = val;
    }
}

/* ==== MBC3 ==== */
static uint8_t mbc3_read(gb_cart_t *c, uint16_t addr) {
    if (addr < 0x4000) return c->rom[addr];
    if (addr < 0x8000) {
        int bank = c->rom_bank ? c->rom_bank : 1;
        size_t offset = (size_t)bank * 0x4000 + (addr - 0x4000);
        return c->rom[offset % c->rom_size];
    }
    if (addr >= 0xA000 && addr < 0xC000 && c->ram && c->ram_enable && c->ram_bank < 4) {
        size_t offset = (size_t)c->ram_bank * 0x2000 + (addr - 0xA000);
        return c->ram[offset % c->ram_size];
    }
    return 0xFF;
}
static void mbc3_write(gb_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr < 0x2000) { c->ram_enable = ((val & 0x0F) == 0x0A); }
    else if (addr < 0x4000) { c->rom_bank = val & 0x7F; }
    else if (addr < 0x6000) { c->ram_bank = val; }
    else if (addr < 0x8000) { /* RTC latch - ignored */ }
    else if (addr >= 0xA000 && addr < 0xC000 && c->ram && c->ram_enable && c->ram_bank < 4) {
        size_t offset = (size_t)c->ram_bank * 0x2000 + (addr - 0xA000);
        c->ram[offset % c->ram_size] = val;
    }
}

/* ==== MBC5 ==== */
static uint8_t mbc5_read(gb_cart_t *c, uint16_t addr) {
    if (addr < 0x4000) return c->rom[addr];
    if (addr < 0x8000) {
        size_t offset = (size_t)c->rom_bank * 0x4000 + (addr - 0x4000);
        return c->rom[offset % c->rom_size];
    }
    if (addr >= 0xA000 && addr < 0xC000 && c->ram && c->ram_enable) {
        size_t offset = (size_t)c->ram_bank * 0x2000 + (addr - 0xA000);
        return c->ram[offset % c->ram_size];
    }
    return 0xFF;
}
static void mbc5_write(gb_cart_t *c, uint16_t addr, uint8_t val) {
    if (addr < 0x2000) { c->ram_enable = ((val & 0x0F) == 0x0A); }
    else if (addr < 0x3000) { c->rom_bank = (c->rom_bank & 0x100) | val; }
    else if (addr < 0x4000) { c->rom_bank = (c->rom_bank & 0xFF) | ((val & 1) << 8); }
    else if (addr < 0x6000) { c->ram_bank = val & 0x0F; }
    else if (addr >= 0xA000 && addr < 0xC000 && c->ram && c->ram_enable) {
        size_t offset = (size_t)c->ram_bank * 0x2000 + (addr - 0xA000);
        c->ram[offset % c->ram_size] = val;
    }
}

/* ==== Public API ==== */

bool gb_cart_load(const uint8_t *rom_data, size_t rom_size, gb_cart_t *cart) {
    if (rom_size < 0x150) {
        ESP_LOGE(TAG, "ROM too small");
        return false;
    }

    memset(cart, 0, sizeof(*cart));

    /* Read header */
    memcpy(cart->title, rom_data + 0x134, 16);
    cart->title[16] = '\0';

    uint8_t cart_type = rom_data[0x147];
    size_t exp_rom_size = rom_size_from_code(rom_data[0x148]);
    size_t ext_ram_size = ram_size_from_code(rom_data[0x149]);

    /* Determine MBC type */
    switch (cart_type) {
    case 0x00: case 0x08: case 0x09:
        cart->mbc_type = 0; break;
    case 0x01: case 0x02: case 0x03:
        cart->mbc_type = 1; break;
    case 0x05: case 0x06:
        cart->mbc_type = 2; break;
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13:
        cart->mbc_type = 3; break;
    case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
        cart->mbc_type = 5; break;
    default:
        ESP_LOGW(TAG, "Unknown cart type 0x%02X, assuming ROM-only", cart_type);
        cart->mbc_type = 0;
        break;
    }

    /* Allocate ROM in PSRAM */
    cart->rom_size = (rom_size > exp_rom_size) ? rom_size : exp_rom_size;
    cart->rom = heap_caps_malloc(cart->rom_size, MALLOC_CAP_SPIRAM);
    if (!cart->rom) {
        ESP_LOGE(TAG, "Failed to allocate ROM (%d bytes)", (int)cart->rom_size);
        return false;
    }
    memcpy(cart->rom, rom_data, rom_size);
    if (cart->rom_size > rom_size) {
        memset(cart->rom + rom_size, 0xFF, cart->rom_size - rom_size);
    }

    /* Allocate external RAM if needed */
    if (ext_ram_size > 0 || cart->mbc_type == 2) {
        cart->ram_size = (ext_ram_size > 0) ? ext_ram_size : 512; /* MBC2 has 512 nibbles */
        cart->ram = heap_caps_malloc(cart->ram_size, MALLOC_CAP_SPIRAM);
        if (!cart->ram) {
            ESP_LOGE(TAG, "Failed to allocate ext RAM");
            free(cart->rom);
            return false;
        }
        memset(cart->ram, 0, cart->ram_size);
    }

    /* Set mapper functions */
    cart->rom_bank = 1;
    cart->ram_bank = 0;
    cart->ram_enable = false;
    cart->mode = 0;

    switch (cart->mbc_type) {
    case 0: cart->read = rom_only_read; cart->write = rom_only_write; break;
    case 1: cart->read = mbc1_read;     cart->write = mbc1_write; break;
    case 2: cart->read = mbc1_read;     cart->write = mbc1_write; break; /* MBC2 is MBC1-like */
    case 3: cart->read = mbc3_read;     cart->write = mbc3_write; break;
    case 5: cart->read = mbc5_read;     cart->write = mbc5_write; break;
    default: cart->read = rom_only_read; cart->write = rom_only_write; break;
    }

    ESP_LOGI(TAG, "GB ROM loaded: \"%s\" Type=0x%02X MBC=%d ROM=%dK RAM=%dK",
             cart->title, cart_type, cart->mbc_type,
             (int)(cart->rom_size / 1024), (int)(cart->ram_size / 1024));
    return true;
}

void gb_cart_free(gb_cart_t *cart) {
    if (cart->rom) { free(cart->rom); cart->rom = NULL; }
    if (cart->ram) { free(cart->ram); cart->ram = NULL; }
}
