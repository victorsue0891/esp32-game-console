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
static int      timer_counter;  /* Fractional cycles for TIMA increment */

static bool initialized = false;

/* ---- Timer ---- */

static const int timer_clocks[] = {1024, 16, 64, 256}; /* DIV bits for TAC clock select */

static void timer_tick(int cycles) {
    timer_div += cycles;

    if (timer_tac & 0x04) { /* Timer enabled */
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
    timer_counter = 0;

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

/* ---- Save State ---- */

#define GB_STATE_MAGIC  0x47425353u  /* "GBSS" */
#define GB_SAVE_RAM_MAX 32768        /* MBC5 max cart RAM */

typedef struct __attribute__((packed)) {
    uint32_t magic;

    /* CPU */
    uint16_t cpu_af, cpu_bc, cpu_de, cpu_hl, cpu_sp, cpu_pc;
    uint8_t  cpu_ime, cpu_ime_pending, cpu_halted;
    uint64_t cpu_total_cycles;

    /* Work RAM + High RAM */
    uint8_t wram[8192];
    uint8_t hram[127];

    /* IO registers */
    uint8_t reg_if, reg_ie, reg_joypad, joypad_state;

    /* Timer */
    uint16_t timer_div;
    uint8_t  timer_tima, timer_tma, timer_tac;
    int32_t  timer_counter;

    /* PPU registers */
    uint8_t ppu_lcdc, ppu_stat, ppu_scy, ppu_scx;
    uint8_t ppu_ly, ppu_lyc, ppu_bgp, ppu_obp0, ppu_obp1, ppu_wy, ppu_wx, ppu_dma;
    uint8_t ppu_vram[8192];
    uint8_t ppu_oam[160];
    int32_t ppu_mode, ppu_dot_counter, ppu_window_line;

    /* APU channel 1 */
    uint8_t  ch1_sweep_period, ch1_sweep_negate, ch1_sweep_shift;
    uint8_t  ch1_duty, ch1_length_load, ch1_env_init, ch1_env_dir, ch1_env_period;
    uint16_t ch1_freq;
    uint8_t  ch1_trigger, ch1_length_enable;
    uint16_t ch1_timer;
    uint8_t  ch1_duty_pos, ch1_volume, ch1_env_timer;
    uint16_t ch1_sweep_shadow;
    uint8_t  ch1_sweep_timer, ch1_sweep_enable;
    uint16_t ch1_length_counter;
    uint8_t  ch1_enabled;

    /* APU channel 2 */
    uint8_t  ch2_duty, ch2_length_load, ch2_env_init, ch2_env_dir, ch2_env_period;
    uint16_t ch2_freq;
    uint8_t  ch2_trigger, ch2_length_enable;
    uint16_t ch2_timer;
    uint8_t  ch2_duty_pos, ch2_volume, ch2_env_timer;
    uint16_t ch2_length_counter;
    uint8_t  ch2_enabled;

    /* APU channel 3 */
    uint8_t  ch3_dac_enable;
    uint16_t ch3_length_load;
    uint8_t  ch3_volume_code;
    uint16_t ch3_freq;
    uint8_t  ch3_trigger, ch3_length_enable;
    uint8_t  ch3_wave_ram[16];
    uint16_t ch3_timer;
    uint8_t  ch3_position;
    uint16_t ch3_length_counter;
    uint8_t  ch3_enabled;

    /* APU channel 4 */
    uint8_t  ch4_length_load, ch4_env_init, ch4_env_dir, ch4_env_period;
    uint8_t  ch4_clock_shift, ch4_width_mode, ch4_divisor_code;
    uint8_t  ch4_trigger, ch4_length_enable;
    uint16_t ch4_timer, ch4_lfsr;
    uint8_t  ch4_volume, ch4_env_timer;
    uint16_t ch4_length_counter;
    uint8_t  ch4_enabled;

    /* APU master */
    uint8_t  apu_nr50, apu_nr51, apu_nr52;
    uint32_t apu_frame_seq_counter;
    uint8_t  apu_frame_seq_step;
    uint64_t apu_cycle_count;

    /* Cartridge MBC state + RAM */
    uint8_t  cart_mbc_type;
    uint8_t  cart_ram_enable;
    uint16_t cart_rom_bank;
    uint8_t  cart_ram_bank, cart_mode;
    uint32_t cart_ram_size;
    uint8_t  cart_ram[GB_SAVE_RAM_MAX];

} gb_save_t;

size_t gb_get_state_size(void)
{
    return sizeof(gb_save_t);
}

void gb_save_state(void *buf)
{
    gb_save_t *s = (gb_save_t *)buf;
    memset(s, 0, sizeof(*s));
    s->magic = GB_STATE_MAGIC;

    /* CPU */
    s->cpu_af = cpu.af;  s->cpu_bc = cpu.bc;
    s->cpu_de = cpu.de;  s->cpu_hl = cpu.hl;
    s->cpu_sp = cpu.sp;  s->cpu_pc = cpu.pc;
    s->cpu_ime         = (uint8_t)cpu.ime;
    s->cpu_ime_pending = (uint8_t)cpu.ime_pending;
    s->cpu_halted      = (uint8_t)cpu.halted;
    s->cpu_total_cycles = cpu.total_cycles;

    /* RAM */
    memcpy(s->wram, wram, sizeof(wram));
    memcpy(s->hram, hram, sizeof(hram));

    /* IO */
    s->reg_if = reg_if;  s->reg_ie = reg_ie;
    s->reg_joypad = reg_joypad;  s->joypad_state = joypad_state;

    /* Timer */
    s->timer_div = timer_div;
    s->timer_tima = timer_tima;  s->timer_tma = timer_tma;
    s->timer_tac = timer_tac;
    s->timer_counter = (int32_t)timer_counter;

    /* PPU registers */
    s->ppu_lcdc = ppu.lcdc;  s->ppu_stat = ppu.stat;
    s->ppu_scy  = ppu.scy;   s->ppu_scx  = ppu.scx;
    s->ppu_ly   = ppu.ly;    s->ppu_lyc  = ppu.lyc;
    s->ppu_bgp  = ppu.bgp;   s->ppu_obp0 = ppu.obp0;
    s->ppu_obp1 = ppu.obp1;  s->ppu_wy   = ppu.wy;
    s->ppu_wx   = ppu.wx;    s->ppu_dma  = ppu.dma;
    memcpy(s->ppu_vram, ppu.vram, sizeof(ppu.vram));
    memcpy(s->ppu_oam,  ppu.oam,  sizeof(ppu.oam));
    s->ppu_mode        = (int32_t)ppu.mode;
    s->ppu_dot_counter = (int32_t)ppu.dot_counter;
    s->ppu_window_line = (int32_t)ppu.window_line;

    /* APU channel 1 */
    s->ch1_sweep_period  = apu.ch1.sweep_period;
    s->ch1_sweep_negate  = apu.ch1.sweep_negate;
    s->ch1_sweep_shift   = apu.ch1.sweep_shift;
    s->ch1_duty          = apu.ch1.duty;
    s->ch1_length_load   = apu.ch1.length_load;
    s->ch1_env_init      = apu.ch1.env_init;
    s->ch1_env_dir       = apu.ch1.env_dir;
    s->ch1_env_period    = apu.ch1.env_period;
    s->ch1_freq          = apu.ch1.freq;
    s->ch1_trigger       = apu.ch1.trigger;
    s->ch1_length_enable = apu.ch1.length_enable;
    s->ch1_timer         = apu.ch1.timer;
    s->ch1_duty_pos      = apu.ch1.duty_pos;
    s->ch1_volume        = apu.ch1.volume;
    s->ch1_env_timer     = apu.ch1.env_timer;
    s->ch1_sweep_shadow  = apu.ch1.sweep_shadow;
    s->ch1_sweep_timer   = apu.ch1.sweep_timer;
    s->ch1_sweep_enable  = (uint8_t)apu.ch1.sweep_enable;
    s->ch1_length_counter = apu.ch1.length_counter;
    s->ch1_enabled       = (uint8_t)apu.ch1.enabled;

    /* APU channel 2 */
    s->ch2_duty          = apu.ch2.duty;
    s->ch2_length_load   = apu.ch2.length_load;
    s->ch2_env_init      = apu.ch2.env_init;
    s->ch2_env_dir       = apu.ch2.env_dir;
    s->ch2_env_period    = apu.ch2.env_period;
    s->ch2_freq          = apu.ch2.freq;
    s->ch2_trigger       = apu.ch2.trigger;
    s->ch2_length_enable = apu.ch2.length_enable;
    s->ch2_timer         = apu.ch2.timer;
    s->ch2_duty_pos      = apu.ch2.duty_pos;
    s->ch2_volume        = apu.ch2.volume;
    s->ch2_env_timer     = apu.ch2.env_timer;
    s->ch2_length_counter = apu.ch2.length_counter;
    s->ch2_enabled       = (uint8_t)apu.ch2.enabled;

    /* APU channel 3 */
    s->ch3_dac_enable    = apu.ch3.dac_enable;
    s->ch3_length_load   = apu.ch3.length_load;
    s->ch3_volume_code   = apu.ch3.volume_code;
    s->ch3_freq          = apu.ch3.freq;
    s->ch3_trigger       = apu.ch3.trigger;
    s->ch3_length_enable = apu.ch3.length_enable;
    memcpy(s->ch3_wave_ram, apu.ch3.wave_ram, sizeof(apu.ch3.wave_ram));
    s->ch3_timer         = apu.ch3.timer;
    s->ch3_position      = apu.ch3.position;
    s->ch3_length_counter = apu.ch3.length_counter;
    s->ch3_enabled       = (uint8_t)apu.ch3.enabled;

    /* APU channel 4 */
    s->ch4_length_load   = apu.ch4.length_load;
    s->ch4_env_init      = apu.ch4.env_init;
    s->ch4_env_dir       = apu.ch4.env_dir;
    s->ch4_env_period    = apu.ch4.env_period;
    s->ch4_clock_shift   = apu.ch4.clock_shift;
    s->ch4_width_mode    = apu.ch4.width_mode;
    s->ch4_divisor_code  = apu.ch4.divisor_code;
    s->ch4_trigger       = apu.ch4.trigger;
    s->ch4_length_enable = apu.ch4.length_enable;
    s->ch4_timer         = apu.ch4.timer;
    s->ch4_lfsr          = apu.ch4.lfsr;
    s->ch4_volume        = apu.ch4.volume;
    s->ch4_env_timer     = apu.ch4.env_timer;
    s->ch4_length_counter = apu.ch4.length_counter;
    s->ch4_enabled       = (uint8_t)apu.ch4.enabled;

    /* APU master */
    s->apu_nr50 = apu.nr50;  s->apu_nr51 = apu.nr51;  s->apu_nr52 = apu.nr52;
    s->apu_frame_seq_counter = apu.frame_seq_counter;
    s->apu_frame_seq_step    = apu.frame_seq_step;
    s->apu_cycle_count       = apu.cycle_count;

    /* Cart MBC state */
    s->cart_mbc_type   = cart.mbc_type;
    s->cart_ram_enable = (uint8_t)cart.ram_enable;
    s->cart_rom_bank   = cart.rom_bank;
    s->cart_ram_bank   = cart.ram_bank;
    s->cart_mode       = cart.mode;
    if (cart.ram && cart.ram_size > 0) {
        uint32_t save_sz = (cart.ram_size < GB_SAVE_RAM_MAX)
                           ? (uint32_t)cart.ram_size : GB_SAVE_RAM_MAX;
        s->cart_ram_size = save_sz;
        memcpy(s->cart_ram, cart.ram, save_sz);
    }
}

bool gb_load_state(const void *buf)
{
    const gb_save_t *s = (const gb_save_t *)buf;
    if (s->magic != GB_STATE_MAGIC) {
        ESP_LOGE(TAG, "GB load_state: bad magic 0x%08lX", (unsigned long)s->magic);
        return false;
    }

    /* CPU */
    cpu.af = s->cpu_af;  cpu.bc = s->cpu_bc;
    cpu.de = s->cpu_de;  cpu.hl = s->cpu_hl;
    cpu.sp = s->cpu_sp;  cpu.pc = s->cpu_pc;
    cpu.ime         = (bool)s->cpu_ime;
    cpu.ime_pending = (bool)s->cpu_ime_pending;
    cpu.halted      = (bool)s->cpu_halted;
    cpu.total_cycles = s->cpu_total_cycles;

    /* RAM */
    memcpy(wram, s->wram, sizeof(wram));
    memcpy(hram, s->hram, sizeof(hram));

    /* IO */
    reg_if = s->reg_if;  reg_ie = s->reg_ie;
    reg_joypad = s->reg_joypad;  joypad_state = s->joypad_state;

    /* Timer */
    timer_div     = s->timer_div;
    timer_tima    = s->timer_tima;
    timer_tma     = s->timer_tma;
    timer_tac     = s->timer_tac;
    timer_counter = (int)s->timer_counter;

    /* PPU */
    ppu.lcdc = s->ppu_lcdc;  ppu.stat = s->ppu_stat;
    ppu.scy  = s->ppu_scy;   ppu.scx  = s->ppu_scx;
    ppu.ly   = s->ppu_ly;    ppu.lyc  = s->ppu_lyc;
    ppu.bgp  = s->ppu_bgp;   ppu.obp0 = s->ppu_obp0;
    ppu.obp1 = s->ppu_obp1;  ppu.wy   = s->ppu_wy;
    ppu.wx   = s->ppu_wx;    ppu.dma  = s->ppu_dma;
    memcpy(ppu.vram, s->ppu_vram, sizeof(ppu.vram));
    memcpy(ppu.oam,  s->ppu_oam,  sizeof(ppu.oam));
    ppu.mode        = (int)s->ppu_mode;
    ppu.dot_counter = (int)s->ppu_dot_counter;
    ppu.window_line = (int)s->ppu_window_line;
    ppu.stat_irq    = false;
    ppu.vblank_irq  = false;

    /* APU channel 1 */
    apu.ch1.sweep_period  = s->ch1_sweep_period;
    apu.ch1.sweep_negate  = s->ch1_sweep_negate;
    apu.ch1.sweep_shift   = s->ch1_sweep_shift;
    apu.ch1.duty          = s->ch1_duty;
    apu.ch1.length_load   = s->ch1_length_load;
    apu.ch1.env_init      = s->ch1_env_init;
    apu.ch1.env_dir       = s->ch1_env_dir;
    apu.ch1.env_period    = s->ch1_env_period;
    apu.ch1.freq          = s->ch1_freq;
    apu.ch1.trigger       = s->ch1_trigger;
    apu.ch1.length_enable = s->ch1_length_enable;
    apu.ch1.timer         = s->ch1_timer;
    apu.ch1.duty_pos      = s->ch1_duty_pos;
    apu.ch1.volume        = s->ch1_volume;
    apu.ch1.env_timer     = s->ch1_env_timer;
    apu.ch1.sweep_shadow  = s->ch1_sweep_shadow;
    apu.ch1.sweep_timer   = s->ch1_sweep_timer;
    apu.ch1.sweep_enable  = (bool)s->ch1_sweep_enable;
    apu.ch1.length_counter = s->ch1_length_counter;
    apu.ch1.enabled       = (bool)s->ch1_enabled;

    /* APU channel 2 */
    apu.ch2.duty          = s->ch2_duty;
    apu.ch2.length_load   = s->ch2_length_load;
    apu.ch2.env_init      = s->ch2_env_init;
    apu.ch2.env_dir       = s->ch2_env_dir;
    apu.ch2.env_period    = s->ch2_env_period;
    apu.ch2.freq          = s->ch2_freq;
    apu.ch2.trigger       = s->ch2_trigger;
    apu.ch2.length_enable = s->ch2_length_enable;
    apu.ch2.timer         = s->ch2_timer;
    apu.ch2.duty_pos      = s->ch2_duty_pos;
    apu.ch2.volume        = s->ch2_volume;
    apu.ch2.env_timer     = s->ch2_env_timer;
    apu.ch2.length_counter = s->ch2_length_counter;
    apu.ch2.enabled       = (bool)s->ch2_enabled;

    /* APU channel 3 */
    apu.ch3.dac_enable    = s->ch3_dac_enable;
    apu.ch3.length_load   = s->ch3_length_load;
    apu.ch3.volume_code   = s->ch3_volume_code;
    apu.ch3.freq          = s->ch3_freq;
    apu.ch3.trigger       = s->ch3_trigger;
    apu.ch3.length_enable = s->ch3_length_enable;
    memcpy(apu.ch3.wave_ram, s->ch3_wave_ram, sizeof(apu.ch3.wave_ram));
    apu.ch3.timer         = s->ch3_timer;
    apu.ch3.position      = s->ch3_position;
    apu.ch3.length_counter = s->ch3_length_counter;
    apu.ch3.enabled       = (bool)s->ch3_enabled;

    /* APU channel 4 */
    apu.ch4.length_load   = s->ch4_length_load;
    apu.ch4.env_init      = s->ch4_env_init;
    apu.ch4.env_dir       = s->ch4_env_dir;
    apu.ch4.env_period    = s->ch4_env_period;
    apu.ch4.clock_shift   = s->ch4_clock_shift;
    apu.ch4.width_mode    = s->ch4_width_mode;
    apu.ch4.divisor_code  = s->ch4_divisor_code;
    apu.ch4.trigger       = s->ch4_trigger;
    apu.ch4.length_enable = s->ch4_length_enable;
    apu.ch4.timer         = s->ch4_timer;
    apu.ch4.lfsr          = s->ch4_lfsr;
    apu.ch4.volume        = s->ch4_volume;
    apu.ch4.env_timer     = s->ch4_env_timer;
    apu.ch4.length_counter = s->ch4_length_counter;
    apu.ch4.enabled       = (bool)s->ch4_enabled;

    /* APU master */
    apu.nr50 = s->apu_nr50;  apu.nr51 = s->apu_nr51;  apu.nr52 = s->apu_nr52;
    apu.frame_seq_counter = s->apu_frame_seq_counter;
    apu.frame_seq_step    = s->apu_frame_seq_step;
    apu.cycle_count       = s->apu_cycle_count;
    apu.sample_count = 0;  /* don't restore in-progress audio buffer */

    /* Cart MBC state */
    cart.mbc_type   = s->cart_mbc_type;
    cart.ram_enable = (bool)s->cart_ram_enable;
    cart.rom_bank   = s->cart_rom_bank;
    cart.ram_bank   = s->cart_ram_bank;
    cart.mode       = s->cart_mode;
    if (cart.ram && s->cart_ram_size > 0 && s->cart_ram_size <= cart.ram_size) {
        memcpy(cart.ram, s->cart_ram, s->cart_ram_size);
    }

    ESP_LOGI(TAG, "GB state loaded (PC=0x%04X)", cpu.pc);
    return true;
}
