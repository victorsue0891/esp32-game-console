/**
 * Game Boy Emulator - Main Integration Module
 * Memory map, timer, interrupt handling, frame execution
 */

#include "gb_emu.h"
#include "gb_cpu.h"
#include "gb_ppu.h"
#include "gb_apu.h"
#include "gb_cart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gb_emu";

/* GB clocks 4194304 Hz, 154 scanlines × 456 dots = 70224 T-cycles per frame */
#define CYCLES_PER_FRAME  70224

/* State */
static gb_cpu_t  cpu;
static gb_ppu_t  ppu;
static gb_apu_t  apu;
static gb_cart_t cart;

static uint8_t wram[8192];      /* Work RAM */
static uint8_t hram[127];       /* High RAM (FF80-FFFE) */
static uint16_t framebuffer[GB_WIDTH * GB_HEIGHT];

/* IO registers */
static uint8_t reg_if;          /* FF0F - Interrupt Flag */
static uint8_t reg_ie;          /* FFFF - Interrupt Enable */
static uint8_t reg_joypad;      /* FF00 - Joypad register */
static uint8_t joypad_state;    /* Button state from user */

/* Timer */
static uint16_t timer_div;      /* Internal 16-bit divider counter */
static uint8_t  timer_tima;     /* FF05 - Timer counter */
static uint8_t  timer_tma;      /* FF06 - Timer modulo */
static uint8_t  timer_tac;      /* FF07 - Timer control */

static bool initialized = false;

/* ---- Timer ---- */

static const int timer_clocks[] = {1024, 16, 64, 256}; /* DIV bits for TAC clock select */

static void timer_tick(int cycles) {
    timer_div += cycles;

    if (timer_tac & 0x04) { /* Timer enabled */
        static int timer_counter = 0;
        timer_counter += cycles;
        int period = timer_clocks[timer_tac & 3];
        while (timer_counter >= period) {
            timer_counter -= period;
            timer_tima++;
            if (timer_tima == 0) {
                timer_tima = timer_tma;
                reg_if |= 0x04; /* Timer interrupt */
            }
        }
    }
}

/* ---- Memory Map ---- */

static uint8_t mem_read(uint16_t addr) {
    /* ROM and external RAM - cartridge handles banking */
    if (addr < 0x8000) return cart.read(&cart, addr);

    /* VRAM */
    if (addr < 0xA000) return gb_ppu_read(&ppu, addr);

    /* External RAM */
    if (addr < 0xC000) return cart.read(&cart, addr);

    /* Work RAM */
    if (addr < 0xE000) return wram[addr - 0xC000];

    /* Echo RAM */
    if (addr < 0xFE00) return wram[addr - 0xE000];

    /* OAM */
    if (addr < 0xFEA0) return gb_ppu_read(&ppu, addr);

    /* Unusable */
    if (addr < 0xFF00) return 0xFF;

    /* IO Registers */
    if (addr < 0xFF80) {
        switch (addr) {
        case 0xFF00: { /* Joypad */
            uint8_t result = reg_joypad | 0xCF;
            if (!(reg_joypad & 0x10)) { /* Directions */
                if (joypad_state & GB_BTN_RIGHT) result &= ~0x01;
                if (joypad_state & GB_BTN_LEFT)  result &= ~0x02;
                if (joypad_state & GB_BTN_UP)    result &= ~0x04;
                if (joypad_state & GB_BTN_DOWN)  result &= ~0x08;
            }
            if (!(reg_joypad & 0x20)) { /* Buttons */
                if (joypad_state & GB_BTN_A)      result &= ~0x01;
                if (joypad_state & GB_BTN_B)      result &= ~0x02;
                if (joypad_state & GB_BTN_SELECT) result &= ~0x04;
                if (joypad_state & GB_BTN_START)  result &= ~0x08;
            }
            return result;
        }
        case 0xFF04: return (uint8_t)(timer_div >> 8);
        case 0xFF05: return timer_tima;
        case 0xFF06: return timer_tma;
        case 0xFF07: return timer_tac | 0xF8;
        case 0xFF0F: return reg_if | 0xE0;
        case 0xFF40: case 0xFF41: case 0xFF42: case 0xFF43:
        case 0xFF44: case 0xFF45: case 0xFF47: case 0xFF48:
        case 0xFF49: case 0xFF4A: case 0xFF4B:
            return gb_ppu_read(&ppu, addr);
        /* APU registers */
        case 0xFF10: case 0xFF11: case 0xFF12: case 0xFF13: case 0xFF14:
        case 0xFF16: case 0xFF17: case 0xFF18: case 0xFF19:
        case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:
        case 0xFF20: case 0xFF21: case 0xFF22: case 0xFF23:
        case 0xFF24: case 0xFF25: case 0xFF26:
        case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
        case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
        case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
        case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
            return gb_apu_read(&apu, addr);
        default: return 0xFF;
        }
    }

    /* High RAM */
    if (addr < 0xFFFF) return hram[addr - 0xFF80];

    /* IE register */
    return reg_ie;
}

static void mem_write(uint16_t addr, uint8_t val) {
    /* ROM area - MBC register writes */
    if (addr < 0x8000) { cart.write(&cart, addr, val); return; }

    /* VRAM */
    if (addr < 0xA000) { gb_ppu_write(&ppu, addr, val); return; }

    /* External RAM */
    if (addr < 0xC000) { cart.write(&cart, addr, val); return; }

    /* Work RAM */
    if (addr < 0xE000) { wram[addr - 0xC000] = val; return; }

    /* Echo RAM */
    if (addr < 0xFE00) { wram[addr - 0xE000] = val; return; }

    /* OAM */
    if (addr < 0xFEA0) { gb_ppu_write(&ppu, addr, val); return; }

    /* Unusable */
    if (addr < 0xFF00) return;

    /* IO Registers */
    if (addr < 0xFF80) {
        switch (addr) {
        case 0xFF00: reg_joypad = val & 0x30; return;
        case 0xFF04: timer_div = 0; return;
        case 0xFF05: timer_tima = val; return;
        case 0xFF06: timer_tma = val; return;
        case 0xFF07: timer_tac = val & 0x07; return;
        case 0xFF0F: reg_if = val & 0x1F; return;
        case 0xFF46: /* OAM DMA */
        {
            uint16_t src = (uint16_t)val << 8;
            for (int i = 0; i < 160; i++) {
                uint8_t byte = mem_read(src + i);
                ppu.oam[i] = byte;
            }
            return;
        }
        case 0xFF40: case 0xFF41: case 0xFF42: case 0xFF43:
        case 0xFF44: case 0xFF45: case 0xFF47: case 0xFF48:
        case 0xFF49: case 0xFF4A: case 0xFF4B:
            gb_ppu_write(&ppu, addr, val); return;
        /* APU registers */
        case 0xFF10: case 0xFF11: case 0xFF12: case 0xFF13: case 0xFF14:
        case 0xFF16: case 0xFF17: case 0xFF18: case 0xFF19:
        case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:
        case 0xFF20: case 0xFF21: case 0xFF22: case 0xFF23:
        case 0xFF24: case 0xFF25: case 0xFF26:
        case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
        case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
        case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
        case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
            gb_apu_write(&apu, addr, val); return;
        }
        return;
    }

    /* High RAM */
    if (addr < 0xFFFF) { hram[addr - 0xFF80] = val; return; }

    /* IE register */
    reg_ie = val;
}

/* ---- Public API ---- */

bool gb_init(const uint8_t *rom_data, size_t rom_size) {
    if (!gb_cart_load(rom_data, rom_size, &cart)) {
        ESP_LOGE(TAG, "Failed to load ROM");
        return false;
    }

    gb_cpu_init(&cpu);
    gb_cpu_reset(&cpu);
    gb_ppu_init(&ppu, framebuffer);
    gb_ppu_reset(&ppu);
    gb_apu_init(&apu);

    memset(wram, 0, sizeof(wram));
    memset(hram, 0, sizeof(hram));
    memset(framebuffer, 0, sizeof(framebuffer));

    reg_if = 0;
    reg_ie = 0;
    reg_joypad = 0x30;
    joypad_state = 0;
    timer_div = 0;
    timer_tima = 0;
    timer_tma = 0;
    timer_tac = 0;

    /* Skip boot ROM - set PC to entry point */
    cpu.pc = 0x0100;
    cpu.sp = 0xFFFE;
    cpu.af = 0x01B0;
    cpu.bc = 0x0013;
    cpu.de = 0x00D8;
    cpu.hl = 0x014D;

    initialized = true;
    ESP_LOGI(TAG, "GB emulator initialized");
    return true;
}

void gb_run_frame(void) {
    if (!initialized) return;

    apu.sample_count = 0;
    int frame_cycles = 0;

    while (frame_cycles < CYCLES_PER_FRAME) {
        /* Handle interrupts */
        uint8_t pending = reg_if & reg_ie & 0x1F;
        if (pending && cpu.halted) {
            cpu.halted = false;
        }
        if (pending && cpu.ime) {
            cpu.ime = false;
            cpu.sp -= 2;
            mem_write(cpu.sp, cpu.pc & 0xFF);
            mem_write(cpu.sp + 1, cpu.pc >> 8);

            if (pending & 0x01)      { cpu.pc = 0x0040; reg_if &= ~0x01; } /* VBlank */
            else if (pending & 0x02) { cpu.pc = 0x0048; reg_if &= ~0x02; } /* LCD STAT */
            else if (pending & 0x04) { cpu.pc = 0x0050; reg_if &= ~0x04; } /* Timer */
            else if (pending & 0x08) { cpu.pc = 0x0058; reg_if &= ~0x08; } /* Serial */
            else if (pending & 0x10) { cpu.pc = 0x0060; reg_if &= ~0x10; } /* Joypad */
        }

        int cycles;
        if (cpu.halted) {
            cycles = 4;
        } else {
            cycles = gb_cpu_step(&cpu, mem_read, mem_write);
        }

        timer_tick(cycles);
        gb_ppu_tick(&ppu, cycles);
        gb_apu_tick(&apu, cycles);

        /* PPU interrupts */
        if (ppu.vblank_irq) reg_if |= 0x01;
        if (ppu.stat_irq)   reg_if |= 0x02;

        frame_cycles += cycles;
    }
}

void gb_set_joypad(uint8_t buttons) {
    uint8_t old = joypad_state;
    joypad_state = buttons;
    /* Joypad interrupt on any button press */
    if ((~old & buttons) != 0) {
        reg_if |= 0x10;
    }
}

const uint16_t *gb_get_framebuffer(void) {
    return framebuffer;
}

const int16_t *gb_get_audio(int *out_count) {
    if (out_count) *out_count = apu.sample_count;
    return apu.sample_buf;
}

void gb_shutdown(void) {
    if (!initialized) return;
    gb_cart_free(&cart);
    initialized = false;
    ESP_LOGI(TAG, "GB emulator shut down");
}
