/**
 * NES Emulator Core - Main integration module
 * Connects CPU, PPU, APU, and Cartridge into a working NES system.
 */

#include "nes_emu.h"
#include "nes_cpu.h"
#include "nes_ppu.h"
#include "nes_apu.h"
#include "nes_cart.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "nes_emu";

/* NES subsystems */
static nes_cpu_t  cpu;
static nes_ppu_t  ppu;
static nes_apu_t  apu;
static nes_cart_t  cart;

/* Internal RAM */
static uint8_t cpu_ram[2048];

/* Joypad */
static uint8_t joypad_state = 0;
static uint8_t joypad_shift = 0;
static bool    joypad_strobe = false;

/* Framebuffer */
static uint16_t *framebuffer = NULL;

/* Audio buffer for the frame — sized to match APU max output */
static int16_t audio_buf[NES_APU_BUF_SIZE];
static int audio_count = 0;

/* PPU CHR access wrappers (uses cart) */
static uint8_t chr_read_wrapper(uint16_t addr) {
    return cart.chr_read(&cart, addr);
}
static void chr_write_wrapper(uint16_t addr, uint8_t val) {
    cart.chr_write(&cart, addr, val);
}

/* ---- CPU Memory Map ---- */
static uint8_t cpu_mem_read(uint16_t addr) {
    if (addr < 0x2000) {
        return cpu_ram[addr & 0x07FF];
    }
    if (addr < 0x4000) {
        return nes_ppu_read(&ppu, addr);
    }
    if (addr == 0x4015) {
        return nes_apu_read(&apu, addr);
    }
    if (addr == 0x4016) {
        /* Joypad 1 read */
        uint8_t val;
        if (joypad_strobe) {
            val = joypad_state & 1;
        } else {
            val = (joypad_shift & 1);
            joypad_shift >>= 1;
        }
        return val | 0x40;
    }
    if (addr == 0x4017) {
        return 0x40; /* Joypad 2 not connected */
    }
    if (addr >= 0x4020) {
        return cart.cpu_read(&cart, addr);
    }
    return 0;
}

static void cpu_mem_write(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        cpu_ram[addr & 0x07FF] = val;
        return;
    }
    if (addr < 0x4000) {
        nes_ppu_write(&ppu, addr, val);
        return;
    }
    if (addr == 0x4014) {
        /* OAM DMA */
        uint8_t page[256];
        uint16_t base = (uint16_t)val << 8;
        for (int i = 0; i < 256; i++) {
            page[i] = cpu_mem_read(base + i);
        }
        nes_ppu_oam_dma(&ppu, page);
        /* DMA takes ~513 cycles */
        cpu.total_cycles += 513;
        return;
    }
    if (addr == 0x4016) {
        /* Joypad strobe */
        joypad_strobe = (val & 1) != 0;
        if (joypad_strobe) {
            joypad_shift = joypad_state;
        }
        return;
    }
    if (addr >= 0x4000 && addr <= 0x4017) {
        nes_apu_write(&apu, addr, val);
        return;
    }
    if (addr >= 0x4020) {
        cart.cpu_write(&cart, addr, val);
        return;
    }
}

/* ---- Public API ---- */

bool nes_init(const uint8_t *rom_data, size_t rom_size) {
    ESP_LOGI(TAG, "Initializing NES emulator...");

    /* Load cartridge */
    if (!nes_cart_load(rom_data, rom_size, &cart)) {
        ESP_LOGE(TAG, "Failed to load ROM");
        return false;
    }

    /* Allocate framebuffer in PSRAM */
    framebuffer = heap_caps_malloc(NES_WIDTH * NES_HEIGHT * sizeof(uint16_t),
                                   MALLOC_CAP_SPIRAM);
    if (!framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        nes_cart_free(&cart);
        return false;
    }
    memset(framebuffer, 0, NES_WIDTH * NES_HEIGHT * sizeof(uint16_t));

    /* Initialize subsystems */
    memset(cpu_ram, 0, sizeof(cpu_ram));

    nes_cpu_init(&cpu);
    nes_ppu_init(&ppu, framebuffer);
    nes_ppu_reset(&ppu);
    nes_apu_init(&apu);

    /* Set PPU CHR access and mirroring from cartridge */
    ppu.chr_read = chr_read_wrapper;
    ppu.chr_write = chr_write_wrapper;
    ppu.mirroring = cart.mirroring;

    /* Reset CPU (reads reset vector) */
    nes_cpu_reset(&cpu, cpu_mem_read, cpu_mem_write);

    joypad_state = 0;
    joypad_shift = 0;
    joypad_strobe = false;

    ESP_LOGI(TAG, "NES emulator initialized, PC=$%04X", cpu.pc);
    return true;
}

void nes_run_frame(void) {
    /* One NTSC frame = 29780.5 CPU cycles (262 scanlines * 341 PPU dots / 3) */
    /* PPU runs at 3x CPU clock */

    apu.sample_count = 0;

    int frame_ppu_dots = 341 * 262;  /* Total PPU dots per frame */

    for (int dot = 0; dot < frame_ppu_dots; dot++) {
        /* PPU tick */
        if (nes_ppu_tick(&ppu)) {
            nes_cpu_nmi(&cpu);
        }

        /* Update mirroring from cart (mapper may change it) */
        ppu.mirroring = cart.mirroring;

        /* MMC3 scanline counter: tick at dot 260 of each visible scanline */
        if (dot % 341 == 260 && ppu.scanline < 240 &&
            (ppu.mask & 0x18) != 0 && cart.scanline_tick) {
            cart.scanline_tick(&cart);
            /* Deliver mapper IRQ to CPU */
            if (cart.state.mmc3.irq_pending) {
                nes_cpu_irq(&cpu);
                cart.state.mmc3.irq_pending = false;
            }
        }

        /* CPU runs every 3 PPU dots */
        if (dot % 3 == 0) {
            nes_cpu_step(&cpu, cpu_mem_read, cpu_mem_write);
            nes_apu_tick(&apu);
        }
    }

    /* Copy APU samples to output (capped to buffer size) */
    audio_count = apu.sample_count;
    if (audio_count > NES_APU_BUF_SIZE)
        audio_count = NES_APU_BUF_SIZE;
    memcpy(audio_buf, apu.sample_buf, audio_count * sizeof(int16_t));
}

void nes_set_joypad(uint8_t buttons) {
    joypad_state = buttons;
    if (joypad_strobe) {
        joypad_shift = joypad_state;
    }
}

const uint16_t *nes_get_framebuffer(void) {
    return framebuffer;
}

const int16_t *nes_get_audio(int *out_count) {
    if (out_count) *out_count = audio_count;
    return audio_buf;
}

void nes_shutdown(void) {
    if (framebuffer) {
        free(framebuffer);
        framebuffer = NULL;
    }
    nes_cart_free(&cart);
    ESP_LOGI(TAG, "NES emulator shut down");
}
