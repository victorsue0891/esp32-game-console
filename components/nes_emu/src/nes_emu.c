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

/* ================================================================
 * Save State — complete CPU / PPU / APU / Cart serialisation
 * ================================================================ */

#define NES_STATE_MAGIC  0x4E455353u  /* "NESS" */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    /* CPU */
    uint16_t cpu_pc;
    uint8_t  cpu_sp, cpu_a, cpu_x, cpu_y, cpu_p;
    uint64_t cpu_cycles;
    uint8_t  cpu_nmi, cpu_irq;
    /* Internal RAM */
    uint8_t  ram[2048];
    /* Joypad */
    uint8_t  joy_state, joy_shift, joy_strobe;
    /* PPU registers */
    uint8_t  ppu_ctrl, ppu_mask, ppu_status, ppu_oam_addr;
    uint16_t ppu_v, ppu_t;
    uint8_t  ppu_xfine, ppu_w;
    uint8_t  ppu_vram[2048];
    uint8_t  ppu_palette[32];
    uint8_t  ppu_oam[256];
    int32_t  ppu_scanline, ppu_dot;
    uint64_t ppu_frame;
    uint8_t  ppu_nmi_occ, ppu_nmi_out, ppu_spr0, ppu_odd;
    uint16_t ppu_bgslo, ppu_bgshi, ppu_atslo, ppu_atshi;
    uint8_t  ppu_atlalo, ppu_atlahi, ppu_databuf;
    uint8_t  ppu_sprites[8];
    int32_t  ppu_spr_cnt;
    uint8_t  ppu_mirror;
    /* APU pulse 0 */
    uint8_t  p0_duty, p0_lhalt, p0_cvol, p0_vol;
    uint8_t  p0_swen, p0_swper, p0_swneg, p0_swsh;
    uint16_t p0_tper, p0_tval;
    uint8_t  p0_dpos, p0_len, p0_ecnt, p0_ediv, p0_est;
    /* APU pulse 1 */
    uint8_t  p1_duty, p1_lhalt, p1_cvol, p1_vol;
    uint8_t  p1_swen, p1_swper, p1_swneg, p1_swsh;
    uint16_t p1_tper, p1_tval;
    uint8_t  p1_dpos, p1_len, p1_ecnt, p1_ediv, p1_est;
    /* APU triangle */
    uint8_t  tri_ctrl, tri_ll;
    uint16_t tri_tper, tri_tval;
    uint8_t  tri_seq, tri_len, tri_lin, tri_lrel;
    /* APU noise */
    uint8_t  nz_lhalt, nz_cvol, nz_vol, nz_mode;
    uint16_t nz_tper, nz_tval, nz_shift;
    uint8_t  nz_len, nz_ecnt, nz_ediv, nz_est;
    /* APU frame counter */
    uint8_t  apu_fmode, apu_fstep;
    uint16_t apu_ftimer;
    uint8_t  apu_firq, apu_status;
    uint64_t apu_cycles;
    /* Cart */
    uint8_t  chr_ram[8192];
    uint8_t  prg_ram[8192];
    int32_t  cart_mirror, cart_mapper;
    /* Mapper state (union — save all, only relevant one used) */
    uint8_t  mmc1_shift, mmc1_cnt, mmc1_ctrl, mmc1_chr0, mmc1_chr1, mmc1_prg;
    uint8_t  ux_prg;
    uint8_t  cn_chr;
    uint8_t  mmc3_sel, mmc3_regs[8], mmc3_latch, mmc3_cnt;
    uint8_t  mmc3_irqen, mmc3_irqrel, mmc3_irqpend, mmc3_prg, mmc3_chr;
} nes_save_t;

size_t nes_get_state_size(void) { return sizeof(nes_save_t); }

void nes_save_state(void *buf)
{
    nes_save_t *s = (nes_save_t *)buf;
    s->magic = NES_STATE_MAGIC;
    /* CPU */
    s->cpu_pc     = cpu.pc;
    s->cpu_sp     = cpu.sp;
    s->cpu_a      = cpu.a;
    s->cpu_x      = cpu.x;
    s->cpu_y      = cpu.y;
    s->cpu_p      = nes_cpu_get_status(&cpu);
    s->cpu_cycles = cpu.total_cycles;
    s->cpu_nmi    = cpu.nmi_pending ? 1 : 0;
    s->cpu_irq    = cpu.irq_pending ? 1 : 0;
    memcpy(s->ram, cpu_ram, 2048);
    s->joy_state  = joypad_state;
    s->joy_shift  = joypad_shift;
    s->joy_strobe = joypad_strobe ? 1 : 0;
    /* PPU */
    s->ppu_ctrl   = ppu.ctrl;
    s->ppu_mask   = ppu.mask;
    s->ppu_status = ppu.status;
    s->ppu_oam_addr = ppu.oam_addr;
    s->ppu_v      = ppu.v;
    s->ppu_t      = ppu.t;
    s->ppu_xfine  = ppu.x_fine;
    s->ppu_w      = ppu.w;
    memcpy(s->ppu_vram,    ppu.vram,    2048);
    memcpy(s->ppu_palette, ppu.palette, 32);
    memcpy(s->ppu_oam,     ppu.oam,     256);
    s->ppu_scanline = ppu.scanline;
    s->ppu_dot      = ppu.dot;
    s->ppu_frame    = ppu.frame_count;
    s->ppu_nmi_occ  = ppu.nmi_occurred ? 1 : 0;
    s->ppu_nmi_out  = ppu.nmi_output   ? 1 : 0;
    s->ppu_spr0     = ppu.sprite0_hit  ? 1 : 0;
    s->ppu_odd      = ppu.odd_frame    ? 1 : 0;
    s->ppu_bgslo    = ppu.bg_shift_lo;
    s->ppu_bgshi    = ppu.bg_shift_hi;
    s->ppu_atslo    = ppu.attr_shift_lo;
    s->ppu_atshi    = ppu.attr_shift_hi;
    s->ppu_atlalo   = ppu.attr_latch_lo;
    s->ppu_atlahi   = ppu.attr_latch_hi;
    s->ppu_databuf  = ppu.data_buf;
    memcpy(s->ppu_sprites, ppu.scanline_sprites, 8);
    s->ppu_spr_cnt  = ppu.scanline_sprite_count;
    s->ppu_mirror   = ppu.mirroring;
    /* APU pulse 0 */
    s->p0_duty = apu.pulse[0].duty;           s->p0_lhalt = apu.pulse[0].length_halt;
    s->p0_cvol = apu.pulse[0].constant_vol;   s->p0_vol   = apu.pulse[0].volume;
    s->p0_swen = apu.pulse[0].sweep_enable;   s->p0_swper = apu.pulse[0].sweep_period;
    s->p0_swneg= apu.pulse[0].sweep_negate;   s->p0_swsh  = apu.pulse[0].sweep_shift;
    s->p0_tper = apu.pulse[0].timer_period;   s->p0_tval  = apu.pulse[0].timer_val;
    s->p0_dpos = apu.pulse[0].duty_pos;       s->p0_len   = apu.pulse[0].length_counter;
    s->p0_ecnt = apu.pulse[0].envelope_counter; s->p0_ediv = apu.pulse[0].envelope_divider;
    s->p0_est  = apu.pulse[0].envelope_start;
    /* APU pulse 1 */
    s->p1_duty = apu.pulse[1].duty;           s->p1_lhalt = apu.pulse[1].length_halt;
    s->p1_cvol = apu.pulse[1].constant_vol;   s->p1_vol   = apu.pulse[1].volume;
    s->p1_swen = apu.pulse[1].sweep_enable;   s->p1_swper = apu.pulse[1].sweep_period;
    s->p1_swneg= apu.pulse[1].sweep_negate;   s->p1_swsh  = apu.pulse[1].sweep_shift;
    s->p1_tper = apu.pulse[1].timer_period;   s->p1_tval  = apu.pulse[1].timer_val;
    s->p1_dpos = apu.pulse[1].duty_pos;       s->p1_len   = apu.pulse[1].length_counter;
    s->p1_ecnt = apu.pulse[1].envelope_counter; s->p1_ediv = apu.pulse[1].envelope_divider;
    s->p1_est  = apu.pulse[1].envelope_start;
    /* APU triangle */
    s->tri_ctrl = apu.triangle.control;      s->tri_ll   = apu.triangle.linear_load;
    s->tri_tper = apu.triangle.timer_period; s->tri_tval = apu.triangle.timer_val;
    s->tri_seq  = apu.triangle.seq_pos;      s->tri_len  = apu.triangle.length_counter;
    s->tri_lin  = apu.triangle.linear_counter; s->tri_lrel = apu.triangle.linear_reload;
    /* APU noise */
    s->nz_lhalt = apu.noise.length_halt;     s->nz_cvol  = apu.noise.constant_vol;
    s->nz_vol   = apu.noise.volume;          s->nz_mode  = apu.noise.mode;
    s->nz_tper  = apu.noise.timer_period;    s->nz_tval  = apu.noise.timer_val;
    s->nz_shift = apu.noise.shift_reg;       s->nz_len   = apu.noise.length_counter;
    s->nz_ecnt  = apu.noise.envelope_counter; s->nz_ediv  = apu.noise.envelope_divider;
    s->nz_est   = apu.noise.envelope_start;
    /* APU frame */
    s->apu_fmode  = apu.frame_mode;   s->apu_fstep  = apu.frame_step;
    s->apu_ftimer = apu.frame_timer;  s->apu_firq   = apu.frame_irq_inhibit;
    s->apu_status = apu.status;       s->apu_cycles = apu.cycle_count;
    /* Cart */
    memcpy(s->chr_ram,  cart.chr_ram,  8192);
    memcpy(s->prg_ram,  cart.prg_ram,  8192);
    s->cart_mirror  = cart.mirroring;
    s->cart_mapper  = cart.mapper_id;
    s->mmc1_shift   = cart.state.mmc1.shift_reg;
    s->mmc1_cnt     = cart.state.mmc1.shift_count;
    s->mmc1_ctrl    = cart.state.mmc1.control;
    s->mmc1_chr0    = cart.state.mmc1.chr_bank0;
    s->mmc1_chr1    = cart.state.mmc1.chr_bank1;
    s->mmc1_prg     = cart.state.mmc1.prg_bank;
    s->ux_prg       = cart.state.uxrom.prg_bank;
    s->cn_chr       = cart.state.cnrom.chr_bank;
    s->mmc3_sel     = cart.state.mmc3.bank_select;
    memcpy(s->mmc3_regs, cart.state.mmc3.bank_regs, 8);
    s->mmc3_latch   = cart.state.mmc3.irq_latch;
    s->mmc3_cnt     = cart.state.mmc3.irq_counter;
    s->mmc3_irqen   = cart.state.mmc3.irq_enabled  ? 1 : 0;
    s->mmc3_irqrel  = cart.state.mmc3.irq_reload   ? 1 : 0;
    s->mmc3_irqpend = cart.state.mmc3.irq_pending  ? 1 : 0;
    s->mmc3_prg     = cart.state.mmc3.prg_mode     ? 1 : 0;
    s->mmc3_chr     = cart.state.mmc3.chr_invert   ? 1 : 0;
}

bool nes_load_state(const void *buf)
{
    const nes_save_t *s = (const nes_save_t *)buf;
    if (s->magic != NES_STATE_MAGIC) return false;
    /* CPU */
    cpu.pc           = s->cpu_pc;
    cpu.sp           = s->cpu_sp;
    cpu.a            = s->cpu_a;
    cpu.x            = s->cpu_x;
    cpu.y            = s->cpu_y;
    nes_cpu_set_status(&cpu, s->cpu_p);
    cpu.total_cycles = s->cpu_cycles;
    cpu.nmi_pending  = s->cpu_nmi != 0;
    cpu.irq_pending  = s->cpu_irq != 0;
    memcpy(cpu_ram, s->ram, 2048);
    joypad_state  = s->joy_state;
    joypad_shift  = s->joy_shift;
    joypad_strobe = s->joy_strobe != 0;
    /* PPU */
    ppu.ctrl     = s->ppu_ctrl;   ppu.mask      = s->ppu_mask;
    ppu.status   = s->ppu_status; ppu.oam_addr  = s->ppu_oam_addr;
    ppu.v        = s->ppu_v;      ppu.t         = s->ppu_t;
    ppu.x_fine   = s->ppu_xfine; ppu.w          = s->ppu_w;
    memcpy(ppu.vram,    s->ppu_vram,    2048);
    memcpy(ppu.palette, s->ppu_palette, 32);
    memcpy(ppu.oam,     s->ppu_oam,     256);
    ppu.scanline       = s->ppu_scanline;
    ppu.dot            = s->ppu_dot;
    ppu.frame_count    = s->ppu_frame;
    ppu.nmi_occurred   = s->ppu_nmi_occ != 0;
    ppu.nmi_output     = s->ppu_nmi_out != 0;
    ppu.sprite0_hit    = s->ppu_spr0    != 0;
    ppu.odd_frame      = s->ppu_odd     != 0;
    ppu.bg_shift_lo    = s->ppu_bgslo;  ppu.bg_shift_hi   = s->ppu_bgshi;
    ppu.attr_shift_lo  = s->ppu_atslo;  ppu.attr_shift_hi = s->ppu_atshi;
    ppu.attr_latch_lo  = s->ppu_atlalo; ppu.attr_latch_hi = s->ppu_atlahi;
    ppu.data_buf       = s->ppu_databuf;
    memcpy(ppu.scanline_sprites, s->ppu_sprites, 8);
    ppu.scanline_sprite_count = s->ppu_spr_cnt;
    ppu.mirroring      = s->ppu_mirror;
    /* APU pulse 0 */
    apu.pulse[0].duty=s->p0_duty; apu.pulse[0].length_halt=s->p0_lhalt;
    apu.pulse[0].constant_vol=s->p0_cvol; apu.pulse[0].volume=s->p0_vol;
    apu.pulse[0].sweep_enable=s->p0_swen; apu.pulse[0].sweep_period=s->p0_swper;
    apu.pulse[0].sweep_negate=s->p0_swneg; apu.pulse[0].sweep_shift=s->p0_swsh;
    apu.pulse[0].timer_period=s->p0_tper; apu.pulse[0].timer_val=s->p0_tval;
    apu.pulse[0].duty_pos=s->p0_dpos; apu.pulse[0].length_counter=s->p0_len;
    apu.pulse[0].envelope_counter=s->p0_ecnt; apu.pulse[0].envelope_divider=s->p0_ediv;
    apu.pulse[0].envelope_start=s->p0_est;
    /* APU pulse 1 */
    apu.pulse[1].duty=s->p1_duty; apu.pulse[1].length_halt=s->p1_lhalt;
    apu.pulse[1].constant_vol=s->p1_cvol; apu.pulse[1].volume=s->p1_vol;
    apu.pulse[1].sweep_enable=s->p1_swen; apu.pulse[1].sweep_period=s->p1_swper;
    apu.pulse[1].sweep_negate=s->p1_swneg; apu.pulse[1].sweep_shift=s->p1_swsh;
    apu.pulse[1].timer_period=s->p1_tper; apu.pulse[1].timer_val=s->p1_tval;
    apu.pulse[1].duty_pos=s->p1_dpos; apu.pulse[1].length_counter=s->p1_len;
    apu.pulse[1].envelope_counter=s->p1_ecnt; apu.pulse[1].envelope_divider=s->p1_ediv;
    apu.pulse[1].envelope_start=s->p1_est;
    /* APU triangle */
    apu.triangle.control=s->tri_ctrl; apu.triangle.linear_load=s->tri_ll;
    apu.triangle.timer_period=s->tri_tper; apu.triangle.timer_val=s->tri_tval;
    apu.triangle.seq_pos=s->tri_seq; apu.triangle.length_counter=s->tri_len;
    apu.triangle.linear_counter=s->tri_lin; apu.triangle.linear_reload=s->tri_lrel;
    /* APU noise */
    apu.noise.length_halt=s->nz_lhalt; apu.noise.constant_vol=s->nz_cvol;
    apu.noise.volume=s->nz_vol; apu.noise.mode=s->nz_mode;
    apu.noise.timer_period=s->nz_tper; apu.noise.timer_val=s->nz_tval;
    apu.noise.shift_reg=s->nz_shift; apu.noise.length_counter=s->nz_len;
    apu.noise.envelope_counter=s->nz_ecnt; apu.noise.envelope_divider=s->nz_ediv;
    apu.noise.envelope_start=s->nz_est;
    /* APU frame */
    apu.frame_mode=s->apu_fmode; apu.frame_step=s->apu_fstep;
    apu.frame_timer=s->apu_ftimer; apu.frame_irq_inhibit=s->apu_firq;
    apu.status=s->apu_status; apu.cycle_count=s->apu_cycles;
    /* Cart */
    memcpy(cart.chr_ram, s->chr_ram, 8192);
    memcpy(cart.prg_ram, s->prg_ram, 8192);
    cart.mirroring = s->cart_mirror;
    /* Mapper state — restore the union matching the loaded mapper_id */
    cart.state.mmc1.shift_reg   = s->mmc1_shift;
    cart.state.mmc1.shift_count = s->mmc1_cnt;
    cart.state.mmc1.control     = s->mmc1_ctrl;
    cart.state.mmc1.chr_bank0   = s->mmc1_chr0;
    cart.state.mmc1.chr_bank1   = s->mmc1_chr1;
    cart.state.mmc1.prg_bank    = s->mmc1_prg;
    cart.state.uxrom.prg_bank   = s->ux_prg;
    cart.state.cnrom.chr_bank   = s->cn_chr;
    cart.state.mmc3.bank_select = s->mmc3_sel;
    memcpy(cart.state.mmc3.bank_regs, s->mmc3_regs, 8);
    cart.state.mmc3.irq_latch   = s->mmc3_latch;
    cart.state.mmc3.irq_counter = s->mmc3_cnt;
    cart.state.mmc3.irq_enabled = s->mmc3_irqen  != 0;
    cart.state.mmc3.irq_reload  = s->mmc3_irqrel != 0;
    cart.state.mmc3.irq_pending = s->mmc3_irqpend!= 0;
    cart.state.mmc3.prg_mode    = s->mmc3_prg    != 0;
    cart.state.mmc3.chr_invert  = s->mmc3_chr    != 0;
    return true;
}
