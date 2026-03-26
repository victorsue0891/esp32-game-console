/**
 * MOS 6502 CPU Emulation (NES variant - no BCD mode)
 * Complete implementation of all official opcodes and addressing modes.
 */

#include "nes_cpu.h"
#include <string.h>

/* Addressing mode helpers */
static cpu_read_fn  mem_read;
static cpu_write_fn mem_write;

static inline uint8_t rd(uint16_t a) { return mem_read(a); }
static inline void    wr(uint16_t a, uint8_t v) { mem_write(a, v); }

static inline uint16_t rd16(uint16_t a) {
    return (uint16_t)rd(a) | ((uint16_t)rd(a + 1) << 8);
}

/* Bug-accurate 16-bit read wrapping within page (for indirect JMP) */
static inline uint16_t rd16_bug(uint16_t a) {
    uint16_t lo = rd(a);
    uint16_t hi_addr = (a & 0xFF00) | ((a + 1) & 0x00FF);
    return lo | ((uint16_t)rd(hi_addr) << 8);
}

/* Stack operations */
static inline void push(nes_cpu_t *c, uint8_t v) {
    wr(0x0100 | c->sp, v);
    c->sp--;
}

static inline uint8_t pull(nes_cpu_t *c) {
    c->sp++;
    return rd(0x0100 | c->sp);
}

static inline void push16(nes_cpu_t *c, uint16_t v) {
    push(c, (v >> 8) & 0xFF);
    push(c, v & 0xFF);
}

static inline uint16_t pull16(nes_cpu_t *c) {
    uint16_t lo = pull(c);
    uint16_t hi = pull(c);
    return lo | (hi << 8);
}

/* Flag helpers */
static inline void set_zn(nes_cpu_t *c, uint8_t v) {
    c->flag_z = (v == 0);
    c->flag_n = (v >> 7) & 1;
}

uint8_t nes_cpu_get_status(const nes_cpu_t *c) {
    return (c->flag_c)      |
           (c->flag_z << 1) |
           (c->flag_i << 2) |
           (c->flag_d << 3) |
           (1 << 4)         |  /* B flag (always 1 when pushed by PHP/BRK) */
           (1 << 5)         |  /* Unused, always 1 */
           (c->flag_v << 6) |
           (c->flag_n << 7);
}

void nes_cpu_set_status(nes_cpu_t *c, uint8_t v) {
    c->flag_c = v & 1;
    c->flag_z = (v >> 1) & 1;
    c->flag_i = (v >> 2) & 1;
    c->flag_d = (v >> 3) & 1;
    c->flag_v = (v >> 6) & 1;
    c->flag_n = (v >> 7) & 1;
}

void nes_cpu_init(nes_cpu_t *c) {
    memset(c, 0, sizeof(*c));
    c->sp = 0xFD;
    c->flag_i = 1;
}

void nes_cpu_reset(nes_cpu_t *c, cpu_read_fn r, cpu_write_fn w) {
    mem_read = r;
    mem_write = w;
    c->pc = rd16(0xFFFC);
    c->sp = 0xFD;
    c->flag_i = 1;
    c->total_cycles = 0;
}

void nes_cpu_nmi(nes_cpu_t *c) {
    c->nmi_pending = true;
}

void nes_cpu_irq(nes_cpu_t *c) {
    if (!c->flag_i) {
        c->irq_pending = true;
    }
}

/* Helper: check page crossing (adds 1 cycle) */
static inline bool page_cross(uint16_t a, uint16_t b) {
    return (a & 0xFF00) != (b & 0xFF00);
}

/* ---- Addressing Modes ---- */
/* Each returns the effective address and may add to cycles for page crossing */

static uint16_t addr_imm(nes_cpu_t *c) { return c->pc++; }

static uint16_t addr_zp(nes_cpu_t *c) { return rd(c->pc++); }

static uint16_t addr_zpx(nes_cpu_t *c) { return (rd(c->pc++) + c->x) & 0xFF; }

static uint16_t addr_zpy(nes_cpu_t *c) { return (rd(c->pc++) + c->y) & 0xFF; }

static uint16_t addr_abs(nes_cpu_t *c) {
    uint16_t a = rd16(c->pc); c->pc += 2; return a;
}

static uint16_t addr_abx(nes_cpu_t *c, bool check_page) {
    uint16_t base = rd16(c->pc); c->pc += 2;
    uint16_t addr = base + c->x;
    if (check_page && page_cross(base, addr)) c->cycles++;
    return addr;
}

static uint16_t addr_aby(nes_cpu_t *c, bool check_page) {
    uint16_t base = rd16(c->pc); c->pc += 2;
    uint16_t addr = base + c->y;
    if (check_page && page_cross(base, addr)) c->cycles++;
    return addr;
}

static uint16_t addr_izx(nes_cpu_t *c) {
    uint8_t z = rd(c->pc++) + c->x;
    return (uint16_t)rd(z & 0xFF) | ((uint16_t)rd((z + 1) & 0xFF) << 8);
}

static uint16_t addr_izy(nes_cpu_t *c, bool check_page) {
    uint8_t z = rd(c->pc++);
    uint16_t base = (uint16_t)rd(z) | ((uint16_t)rd((z + 1) & 0xFF) << 8);
    uint16_t addr = base + c->y;
    if (check_page && page_cross(base, addr)) c->cycles++;
    return addr;
}

/* ---- ALU Operations ---- */

static void op_adc(nes_cpu_t *c, uint8_t val) {
    uint16_t sum = (uint16_t)c->a + val + c->flag_c;
    c->flag_c = (sum > 0xFF);
    c->flag_v = (~(c->a ^ val) & (c->a ^ sum) & 0x80) != 0;
    c->a = sum & 0xFF;
    set_zn(c, c->a);
}

static void op_sbc(nes_cpu_t *c, uint8_t val) {
    op_adc(c, ~val);
}

static void op_cmp(nes_cpu_t *c, uint8_t reg, uint8_t val) {
    uint16_t result = (uint16_t)reg - val;
    c->flag_c = (reg >= val);
    set_zn(c, result & 0xFF);
}

static void op_and(nes_cpu_t *c, uint8_t val) {
    c->a &= val; set_zn(c, c->a);
}

static void op_ora(nes_cpu_t *c, uint8_t val) {
    c->a |= val; set_zn(c, c->a);
}

static void op_eor(nes_cpu_t *c, uint8_t val) {
    c->a ^= val; set_zn(c, c->a);
}

static void op_bit(nes_cpu_t *c, uint8_t val) {
    c->flag_z = (c->a & val) == 0;
    c->flag_v = (val >> 6) & 1;
    c->flag_n = (val >> 7) & 1;
}

static uint8_t op_asl(nes_cpu_t *c, uint8_t val) {
    c->flag_c = (val >> 7) & 1;
    val <<= 1;
    set_zn(c, val);
    return val;
}

static uint8_t op_lsr(nes_cpu_t *c, uint8_t val) {
    c->flag_c = val & 1;
    val >>= 1;
    set_zn(c, val);
    return val;
}

static uint8_t op_rol(nes_cpu_t *c, uint8_t val) {
    uint8_t old_c = c->flag_c;
    c->flag_c = (val >> 7) & 1;
    val = (val << 1) | old_c;
    set_zn(c, val);
    return val;
}

static uint8_t op_ror(nes_cpu_t *c, uint8_t val) {
    uint8_t old_c = c->flag_c;
    c->flag_c = val & 1;
    val = (val >> 1) | (old_c << 7);
    set_zn(c, val);
    return val;
}

static uint8_t op_inc(nes_cpu_t *c, uint8_t val) {
    val++; set_zn(c, val); return val;
}

static uint8_t op_dec(nes_cpu_t *c, uint8_t val) {
    val--; set_zn(c, val); return val;
}

/* Branch helper */
static void branch(nes_cpu_t *c, bool cond) {
    int8_t offset = (int8_t)rd(c->pc++);
    if (cond) {
        uint16_t new_pc = c->pc + offset;
        c->cycles++;
        if (page_cross(c->pc, new_pc)) c->cycles++;
        c->pc = new_pc;
    }
}

/* ---- Main step function ---- */

int nes_cpu_step(nes_cpu_t *c, cpu_read_fn r, cpu_write_fn w)
{
    mem_read = r;
    mem_write = w;

    /* Handle NMI */
    if (c->nmi_pending) {
        c->nmi_pending = false;
        push16(c, c->pc);
        push(c, nes_cpu_get_status(c) & ~0x10);  /* Clear B flag */
        c->flag_i = 1;
        c->pc = rd16(0xFFFA);
        c->cycles = 7;
        c->total_cycles += 7;
        return 7;
    }

    /* Handle IRQ */
    if (c->irq_pending && !c->flag_i) {
        c->irq_pending = false;
        push16(c, c->pc);
        push(c, nes_cpu_get_status(c) & ~0x10);
        c->flag_i = 1;
        c->pc = rd16(0xFFFE);
        c->cycles = 7;
        c->total_cycles += 7;
        return 7;
    }

    uint8_t opcode = rd(c->pc++);
    c->cycles = 0;
    uint16_t addr;
    uint8_t val;

    switch (opcode) {
    /* ========= LDA ========= */
    case 0xA9: c->a = rd(addr_imm(c));           set_zn(c, c->a); c->cycles = 2; break;
    case 0xA5: c->a = rd(addr_zp(c));            set_zn(c, c->a); c->cycles = 3; break;
    case 0xB5: c->a = rd(addr_zpx(c));           set_zn(c, c->a); c->cycles = 4; break;
    case 0xAD: c->a = rd(addr_abs(c));           set_zn(c, c->a); c->cycles = 4; break;
    case 0xBD: c->a = rd(addr_abx(c, true));     set_zn(c, c->a); c->cycles += 4; break;
    case 0xB9: c->a = rd(addr_aby(c, true));     set_zn(c, c->a); c->cycles += 4; break;
    case 0xA1: c->a = rd(addr_izx(c));           set_zn(c, c->a); c->cycles = 6; break;
    case 0xB1: c->a = rd(addr_izy(c, true));     set_zn(c, c->a); c->cycles += 5; break;

    /* ========= LDX ========= */
    case 0xA2: c->x = rd(addr_imm(c));           set_zn(c, c->x); c->cycles = 2; break;
    case 0xA6: c->x = rd(addr_zp(c));            set_zn(c, c->x); c->cycles = 3; break;
    case 0xB6: c->x = rd(addr_zpy(c));           set_zn(c, c->x); c->cycles = 4; break;
    case 0xAE: c->x = rd(addr_abs(c));           set_zn(c, c->x); c->cycles = 4; break;
    case 0xBE: c->x = rd(addr_aby(c, true));     set_zn(c, c->x); c->cycles += 4; break;

    /* ========= LDY ========= */
    case 0xA0: c->y = rd(addr_imm(c));           set_zn(c, c->y); c->cycles = 2; break;
    case 0xA4: c->y = rd(addr_zp(c));            set_zn(c, c->y); c->cycles = 3; break;
    case 0xB4: c->y = rd(addr_zpx(c));           set_zn(c, c->y); c->cycles = 4; break;
    case 0xAC: c->y = rd(addr_abs(c));           set_zn(c, c->y); c->cycles = 4; break;
    case 0xBC: c->y = rd(addr_abx(c, true));     set_zn(c, c->y); c->cycles += 4; break;

    /* ========= STA ========= */
    case 0x85: wr(addr_zp(c), c->a);             c->cycles = 3; break;
    case 0x95: wr(addr_zpx(c), c->a);            c->cycles = 4; break;
    case 0x8D: wr(addr_abs(c), c->a);            c->cycles = 4; break;
    case 0x9D: wr(addr_abx(c, false), c->a);     c->cycles = 5; break;
    case 0x99: wr(addr_aby(c, false), c->a);     c->cycles = 5; break;
    case 0x81: wr(addr_izx(c), c->a);            c->cycles = 6; break;
    case 0x91: wr(addr_izy(c, false), c->a);     c->cycles = 6; break;

    /* ========= STX ========= */
    case 0x86: wr(addr_zp(c), c->x);             c->cycles = 3; break;
    case 0x96: wr(addr_zpy(c), c->x);            c->cycles = 4; break;
    case 0x8E: wr(addr_abs(c), c->x);            c->cycles = 4; break;

    /* ========= STY ========= */
    case 0x84: wr(addr_zp(c), c->y);             c->cycles = 3; break;
    case 0x94: wr(addr_zpx(c), c->y);            c->cycles = 4; break;
    case 0x8C: wr(addr_abs(c), c->y);            c->cycles = 4; break;

    /* ========= ADC ========= */
    case 0x69: op_adc(c, rd(addr_imm(c)));       c->cycles = 2; break;
    case 0x65: op_adc(c, rd(addr_zp(c)));        c->cycles = 3; break;
    case 0x75: op_adc(c, rd(addr_zpx(c)));       c->cycles = 4; break;
    case 0x6D: op_adc(c, rd(addr_abs(c)));       c->cycles = 4; break;
    case 0x7D: op_adc(c, rd(addr_abx(c, true))); c->cycles += 4; break;
    case 0x79: op_adc(c, rd(addr_aby(c, true))); c->cycles += 4; break;
    case 0x61: op_adc(c, rd(addr_izx(c)));       c->cycles = 6; break;
    case 0x71: op_adc(c, rd(addr_izy(c, true))); c->cycles += 5; break;

    /* ========= SBC ========= */
    case 0xE9: op_sbc(c, rd(addr_imm(c)));       c->cycles = 2; break;
    case 0xE5: op_sbc(c, rd(addr_zp(c)));        c->cycles = 3; break;
    case 0xF5: op_sbc(c, rd(addr_zpx(c)));       c->cycles = 4; break;
    case 0xED: op_sbc(c, rd(addr_abs(c)));       c->cycles = 4; break;
    case 0xFD: op_sbc(c, rd(addr_abx(c, true))); c->cycles += 4; break;
    case 0xF9: op_sbc(c, rd(addr_aby(c, true))); c->cycles += 4; break;
    case 0xE1: op_sbc(c, rd(addr_izx(c)));       c->cycles = 6; break;
    case 0xF1: op_sbc(c, rd(addr_izy(c, true))); c->cycles += 5; break;

    /* ========= AND ========= */
    case 0x29: op_and(c, rd(addr_imm(c)));       c->cycles = 2; break;
    case 0x25: op_and(c, rd(addr_zp(c)));        c->cycles = 3; break;
    case 0x35: op_and(c, rd(addr_zpx(c)));       c->cycles = 4; break;
    case 0x2D: op_and(c, rd(addr_abs(c)));       c->cycles = 4; break;
    case 0x3D: op_and(c, rd(addr_abx(c, true))); c->cycles += 4; break;
    case 0x39: op_and(c, rd(addr_aby(c, true))); c->cycles += 4; break;
    case 0x21: op_and(c, rd(addr_izx(c)));       c->cycles = 6; break;
    case 0x31: op_and(c, rd(addr_izy(c, true))); c->cycles += 5; break;

    /* ========= ORA ========= */
    case 0x09: op_ora(c, rd(addr_imm(c)));       c->cycles = 2; break;
    case 0x05: op_ora(c, rd(addr_zp(c)));        c->cycles = 3; break;
    case 0x15: op_ora(c, rd(addr_zpx(c)));       c->cycles = 4; break;
    case 0x0D: op_ora(c, rd(addr_abs(c)));       c->cycles = 4; break;
    case 0x1D: op_ora(c, rd(addr_abx(c, true))); c->cycles += 4; break;
    case 0x19: op_ora(c, rd(addr_aby(c, true))); c->cycles += 4; break;
    case 0x01: op_ora(c, rd(addr_izx(c)));       c->cycles = 6; break;
    case 0x11: op_ora(c, rd(addr_izy(c, true))); c->cycles += 5; break;

    /* ========= EOR ========= */
    case 0x49: op_eor(c, rd(addr_imm(c)));       c->cycles = 2; break;
    case 0x45: op_eor(c, rd(addr_zp(c)));        c->cycles = 3; break;
    case 0x55: op_eor(c, rd(addr_zpx(c)));       c->cycles = 4; break;
    case 0x4D: op_eor(c, rd(addr_abs(c)));       c->cycles = 4; break;
    case 0x5D: op_eor(c, rd(addr_abx(c, true))); c->cycles += 4; break;
    case 0x59: op_eor(c, rd(addr_aby(c, true))); c->cycles += 4; break;
    case 0x41: op_eor(c, rd(addr_izx(c)));       c->cycles = 6; break;
    case 0x51: op_eor(c, rd(addr_izy(c, true))); c->cycles += 5; break;

    /* ========= CMP ========= */
    case 0xC9: op_cmp(c, c->a, rd(addr_imm(c)));       c->cycles = 2; break;
    case 0xC5: op_cmp(c, c->a, rd(addr_zp(c)));        c->cycles = 3; break;
    case 0xD5: op_cmp(c, c->a, rd(addr_zpx(c)));       c->cycles = 4; break;
    case 0xCD: op_cmp(c, c->a, rd(addr_abs(c)));       c->cycles = 4; break;
    case 0xDD: op_cmp(c, c->a, rd(addr_abx(c, true))); c->cycles += 4; break;
    case 0xD9: op_cmp(c, c->a, rd(addr_aby(c, true))); c->cycles += 4; break;
    case 0xC1: op_cmp(c, c->a, rd(addr_izx(c)));       c->cycles = 6; break;
    case 0xD1: op_cmp(c, c->a, rd(addr_izy(c, true))); c->cycles += 5; break;

    /* ========= CPX ========= */
    case 0xE0: op_cmp(c, c->x, rd(addr_imm(c)));   c->cycles = 2; break;
    case 0xE4: op_cmp(c, c->x, rd(addr_zp(c)));    c->cycles = 3; break;
    case 0xEC: op_cmp(c, c->x, rd(addr_abs(c)));   c->cycles = 4; break;

    /* ========= CPY ========= */
    case 0xC0: op_cmp(c, c->y, rd(addr_imm(c)));   c->cycles = 2; break;
    case 0xC4: op_cmp(c, c->y, rd(addr_zp(c)));    c->cycles = 3; break;
    case 0xCC: op_cmp(c, c->y, rd(addr_abs(c)));   c->cycles = 4; break;

    /* ========= BIT ========= */
    case 0x24: op_bit(c, rd(addr_zp(c)));    c->cycles = 3; break;
    case 0x2C: op_bit(c, rd(addr_abs(c)));   c->cycles = 4; break;

    /* ========= ASL ========= */
    case 0x0A: c->a = op_asl(c, c->a);            c->cycles = 2; break;
    case 0x06: addr = addr_zp(c);  wr(addr, op_asl(c, rd(addr))); c->cycles = 5; break;
    case 0x16: addr = addr_zpx(c); wr(addr, op_asl(c, rd(addr))); c->cycles = 6; break;
    case 0x0E: addr = addr_abs(c); wr(addr, op_asl(c, rd(addr))); c->cycles = 6; break;
    case 0x1E: addr = addr_abx(c, false); wr(addr, op_asl(c, rd(addr))); c->cycles = 7; break;

    /* ========= LSR ========= */
    case 0x4A: c->a = op_lsr(c, c->a);            c->cycles = 2; break;
    case 0x46: addr = addr_zp(c);  wr(addr, op_lsr(c, rd(addr))); c->cycles = 5; break;
    case 0x56: addr = addr_zpx(c); wr(addr, op_lsr(c, rd(addr))); c->cycles = 6; break;
    case 0x4E: addr = addr_abs(c); wr(addr, op_lsr(c, rd(addr))); c->cycles = 6; break;
    case 0x5E: addr = addr_abx(c, false); wr(addr, op_lsr(c, rd(addr))); c->cycles = 7; break;

    /* ========= ROL ========= */
    case 0x2A: c->a = op_rol(c, c->a);            c->cycles = 2; break;
    case 0x26: addr = addr_zp(c);  wr(addr, op_rol(c, rd(addr))); c->cycles = 5; break;
    case 0x36: addr = addr_zpx(c); wr(addr, op_rol(c, rd(addr))); c->cycles = 6; break;
    case 0x2E: addr = addr_abs(c); wr(addr, op_rol(c, rd(addr))); c->cycles = 6; break;
    case 0x3E: addr = addr_abx(c, false); wr(addr, op_rol(c, rd(addr))); c->cycles = 7; break;

    /* ========= ROR ========= */
    case 0x6A: c->a = op_ror(c, c->a);            c->cycles = 2; break;
    case 0x66: addr = addr_zp(c);  wr(addr, op_ror(c, rd(addr))); c->cycles = 5; break;
    case 0x76: addr = addr_zpx(c); wr(addr, op_ror(c, rd(addr))); c->cycles = 6; break;
    case 0x6E: addr = addr_abs(c); wr(addr, op_ror(c, rd(addr))); c->cycles = 6; break;
    case 0x7E: addr = addr_abx(c, false); wr(addr, op_ror(c, rd(addr))); c->cycles = 7; break;

    /* ========= INC ========= */
    case 0xE6: addr = addr_zp(c);  wr(addr, op_inc(c, rd(addr))); c->cycles = 5; break;
    case 0xF6: addr = addr_zpx(c); wr(addr, op_inc(c, rd(addr))); c->cycles = 6; break;
    case 0xEE: addr = addr_abs(c); wr(addr, op_inc(c, rd(addr))); c->cycles = 6; break;
    case 0xFE: addr = addr_abx(c, false); wr(addr, op_inc(c, rd(addr))); c->cycles = 7; break;

    /* ========= DEC ========= */
    case 0xC6: addr = addr_zp(c);  wr(addr, op_dec(c, rd(addr))); c->cycles = 5; break;
    case 0xD6: addr = addr_zpx(c); wr(addr, op_dec(c, rd(addr))); c->cycles = 6; break;
    case 0xCE: addr = addr_abs(c); wr(addr, op_dec(c, rd(addr))); c->cycles = 6; break;
    case 0xDE: addr = addr_abx(c, false); wr(addr, op_dec(c, rd(addr))); c->cycles = 7; break;

    /* ========= INX/INY/DEX/DEY ========= */
    case 0xE8: c->x++; set_zn(c, c->x); c->cycles = 2; break;
    case 0xC8: c->y++; set_zn(c, c->y); c->cycles = 2; break;
    case 0xCA: c->x--; set_zn(c, c->x); c->cycles = 2; break;
    case 0x88: c->y--; set_zn(c, c->y); c->cycles = 2; break;

    /* ========= Transfers ========= */
    case 0xAA: c->x = c->a;  set_zn(c, c->x); c->cycles = 2; break;  /* TAX */
    case 0xA8: c->y = c->a;  set_zn(c, c->y); c->cycles = 2; break;  /* TAY */
    case 0x8A: c->a = c->x;  set_zn(c, c->a); c->cycles = 2; break;  /* TXA */
    case 0x98: c->a = c->y;  set_zn(c, c->a); c->cycles = 2; break;  /* TYA */
    case 0x9A: c->sp = c->x;                   c->cycles = 2; break;  /* TXS */
    case 0xBA: c->x = c->sp; set_zn(c, c->x); c->cycles = 2; break;  /* TSX */

    /* ========= Stack ========= */
    case 0x48: push(c, c->a);                              c->cycles = 3; break; /* PHA */
    case 0x08: push(c, nes_cpu_get_status(c) | 0x10);     c->cycles = 3; break; /* PHP */
    case 0x68: c->a = pull(c);  set_zn(c, c->a);          c->cycles = 4; break; /* PLA */
    case 0x28: nes_cpu_set_status(c, pull(c));             c->cycles = 4; break; /* PLP */

    /* ========= Branches ========= */
    case 0x10: branch(c, !c->flag_n); c->cycles += 2; break;  /* BPL */
    case 0x30: branch(c,  c->flag_n); c->cycles += 2; break;  /* BMI */
    case 0x50: branch(c, !c->flag_v); c->cycles += 2; break;  /* BVC */
    case 0x70: branch(c,  c->flag_v); c->cycles += 2; break;  /* BVS */
    case 0x90: branch(c, !c->flag_c); c->cycles += 2; break;  /* BCC */
    case 0xB0: branch(c,  c->flag_c); c->cycles += 2; break;  /* BCS */
    case 0xD0: branch(c, !c->flag_z); c->cycles += 2; break;  /* BNE */
    case 0xF0: branch(c,  c->flag_z); c->cycles += 2; break;  /* BEQ */

    /* ========= Jumps ========= */
    case 0x4C: c->pc = addr_abs(c);   c->cycles = 3; break;   /* JMP abs */
    case 0x6C: {  /* JMP indirect (with page-wrapping bug) */
        uint16_t ptr = rd16(c->pc); c->pc += 2;
        c->pc = rd16_bug(ptr);
        c->cycles = 5;
    } break;
    case 0x20: {  /* JSR */
        uint16_t target = addr_abs(c);
        push16(c, c->pc - 1);
        c->pc = target;
        c->cycles = 6;
    } break;
    case 0x60: {  /* RTS */
        c->pc = pull16(c) + 1;
        c->cycles = 6;
    } break;
    case 0x40: {  /* RTI */
        nes_cpu_set_status(c, pull(c));
        c->pc = pull16(c);
        c->cycles = 6;
    } break;

    /* ========= Flags ========= */
    case 0x18: c->flag_c = 0; c->cycles = 2; break;  /* CLC */
    case 0x38: c->flag_c = 1; c->cycles = 2; break;  /* SEC */
    case 0x58: c->flag_i = 0; c->cycles = 2; break;  /* CLI */
    case 0x78: c->flag_i = 1; c->cycles = 2; break;  /* SEI */
    case 0xD8: c->flag_d = 0; c->cycles = 2; break;  /* CLD */
    case 0xF8: c->flag_d = 1; c->cycles = 2; break;  /* SED */
    case 0xB8: c->flag_v = 0; c->cycles = 2; break;  /* CLV */

    /* ========= BRK ========= */
    case 0x00: {
        c->pc++;
        push16(c, c->pc);
        push(c, nes_cpu_get_status(c) | 0x10);
        c->flag_i = 1;
        c->pc = rd16(0xFFFE);
        c->cycles = 7;
    } break;

    /* ========= NOP ========= */
    case 0xEA: c->cycles = 2; break;

    /* ========= Unofficial NOPs (common) ========= */
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
        c->cycles = 2; break;
    case 0x04: case 0x44: case 0x64:
        c->pc++; c->cycles = 3; break;  /* DOP zp */
    case 0x0C:
        c->pc += 2; c->cycles = 4; break;  /* TOP abs */
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
        c->pc++; c->cycles = 4; break;  /* DOP zpx */
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
        addr_abx(c, true); c->cycles += 4; break;  /* TOP abx */

    /* Unofficial SBC */
    case 0xEB: op_sbc(c, rd(addr_imm(c))); c->cycles = 2; break;

    /* ========= Unofficial LAX ========= */
    case 0xA7: val = rd(addr_zp(c));        c->a = c->x = val; set_zn(c, val); c->cycles = 3; break;
    case 0xB7: val = rd(addr_zpy(c));       c->a = c->x = val; set_zn(c, val); c->cycles = 4; break;
    case 0xAF: val = rd(addr_abs(c));       c->a = c->x = val; set_zn(c, val); c->cycles = 4; break;
    case 0xBF: val = rd(addr_aby(c, true)); c->a = c->x = val; set_zn(c, val); c->cycles += 4; break;
    case 0xA3: val = rd(addr_izx(c));       c->a = c->x = val; set_zn(c, val); c->cycles = 6; break;
    case 0xB3: val = rd(addr_izy(c, true)); c->a = c->x = val; set_zn(c, val); c->cycles += 5; break;

    /* ========= Unofficial SAX ========= */
    case 0x87: wr(addr_zp(c),  c->a & c->x); c->cycles = 3; break;
    case 0x97: wr(addr_zpy(c), c->a & c->x); c->cycles = 4; break;
    case 0x8F: wr(addr_abs(c), c->a & c->x); c->cycles = 4; break;
    case 0x83: wr(addr_izx(c), c->a & c->x); c->cycles = 6; break;

    /* ========= Unofficial DCP ========= */
    case 0xC7: addr = addr_zp(c);  val = rd(addr) - 1; wr(addr, val); op_cmp(c, c->a, val); c->cycles = 5; break;
    case 0xD7: addr = addr_zpx(c); val = rd(addr) - 1; wr(addr, val); op_cmp(c, c->a, val); c->cycles = 6; break;
    case 0xCF: addr = addr_abs(c); val = rd(addr) - 1; wr(addr, val); op_cmp(c, c->a, val); c->cycles = 6; break;
    case 0xDF: addr = addr_abx(c, false); val = rd(addr) - 1; wr(addr, val); op_cmp(c, c->a, val); c->cycles = 7; break;
    case 0xDB: addr = addr_aby(c, false); val = rd(addr) - 1; wr(addr, val); op_cmp(c, c->a, val); c->cycles = 7; break;
    case 0xC3: addr = addr_izx(c); val = rd(addr) - 1; wr(addr, val); op_cmp(c, c->a, val); c->cycles = 8; break;
    case 0xD3: addr = addr_izy(c, false); val = rd(addr) - 1; wr(addr, val); op_cmp(c, c->a, val); c->cycles = 8; break;

    /* ========= Unofficial ISB (ISC) ========= */
    case 0xE7: addr = addr_zp(c);  val = rd(addr) + 1; wr(addr, val); op_sbc(c, val); c->cycles = 5; break;
    case 0xF7: addr = addr_zpx(c); val = rd(addr) + 1; wr(addr, val); op_sbc(c, val); c->cycles = 6; break;
    case 0xEF: addr = addr_abs(c); val = rd(addr) + 1; wr(addr, val); op_sbc(c, val); c->cycles = 6; break;
    case 0xFF: addr = addr_abx(c, false); val = rd(addr) + 1; wr(addr, val); op_sbc(c, val); c->cycles = 7; break;
    case 0xFB: addr = addr_aby(c, false); val = rd(addr) + 1; wr(addr, val); op_sbc(c, val); c->cycles = 7; break;
    case 0xE3: addr = addr_izx(c); val = rd(addr) + 1; wr(addr, val); op_sbc(c, val); c->cycles = 8; break;
    case 0xF3: addr = addr_izy(c, false); val = rd(addr) + 1; wr(addr, val); op_sbc(c, val); c->cycles = 8; break;

    /* ========= Unofficial SLO ========= */
    case 0x07: addr = addr_zp(c);  val = op_asl(c, rd(addr)); wr(addr, val); op_ora(c, val); c->cycles = 5; break;
    case 0x17: addr = addr_zpx(c); val = op_asl(c, rd(addr)); wr(addr, val); op_ora(c, val); c->cycles = 6; break;
    case 0x0F: addr = addr_abs(c); val = op_asl(c, rd(addr)); wr(addr, val); op_ora(c, val); c->cycles = 6; break;
    case 0x1F: addr = addr_abx(c, false); val = op_asl(c, rd(addr)); wr(addr, val); op_ora(c, val); c->cycles = 7; break;
    case 0x1B: addr = addr_aby(c, false); val = op_asl(c, rd(addr)); wr(addr, val); op_ora(c, val); c->cycles = 7; break;
    case 0x03: addr = addr_izx(c); val = op_asl(c, rd(addr)); wr(addr, val); op_ora(c, val); c->cycles = 8; break;
    case 0x13: addr = addr_izy(c, false); val = op_asl(c, rd(addr)); wr(addr, val); op_ora(c, val); c->cycles = 8; break;

    /* ========= Unofficial RLA ========= */
    case 0x27: addr = addr_zp(c);  val = op_rol(c, rd(addr)); wr(addr, val); op_and(c, val); c->cycles = 5; break;
    case 0x37: addr = addr_zpx(c); val = op_rol(c, rd(addr)); wr(addr, val); op_and(c, val); c->cycles = 6; break;
    case 0x2F: addr = addr_abs(c); val = op_rol(c, rd(addr)); wr(addr, val); op_and(c, val); c->cycles = 6; break;
    case 0x3F: addr = addr_abx(c, false); val = op_rol(c, rd(addr)); wr(addr, val); op_and(c, val); c->cycles = 7; break;
    case 0x3B: addr = addr_aby(c, false); val = op_rol(c, rd(addr)); wr(addr, val); op_and(c, val); c->cycles = 7; break;
    case 0x23: addr = addr_izx(c); val = op_rol(c, rd(addr)); wr(addr, val); op_and(c, val); c->cycles = 8; break;
    case 0x33: addr = addr_izy(c, false); val = op_rol(c, rd(addr)); wr(addr, val); op_and(c, val); c->cycles = 8; break;

    /* ========= Unofficial SRE ========= */
    case 0x47: addr = addr_zp(c);  val = op_lsr(c, rd(addr)); wr(addr, val); op_eor(c, val); c->cycles = 5; break;
    case 0x57: addr = addr_zpx(c); val = op_lsr(c, rd(addr)); wr(addr, val); op_eor(c, val); c->cycles = 6; break;
    case 0x4F: addr = addr_abs(c); val = op_lsr(c, rd(addr)); wr(addr, val); op_eor(c, val); c->cycles = 6; break;
    case 0x5F: addr = addr_abx(c, false); val = op_lsr(c, rd(addr)); wr(addr, val); op_eor(c, val); c->cycles = 7; break;
    case 0x5B: addr = addr_aby(c, false); val = op_lsr(c, rd(addr)); wr(addr, val); op_eor(c, val); c->cycles = 7; break;
    case 0x43: addr = addr_izx(c); val = op_lsr(c, rd(addr)); wr(addr, val); op_eor(c, val); c->cycles = 8; break;
    case 0x53: addr = addr_izy(c, false); val = op_lsr(c, rd(addr)); wr(addr, val); op_eor(c, val); c->cycles = 8; break;

    /* ========= Unofficial RRA ========= */
    case 0x67: addr = addr_zp(c);  val = op_ror(c, rd(addr)); wr(addr, val); op_adc(c, val); c->cycles = 5; break;
    case 0x77: addr = addr_zpx(c); val = op_ror(c, rd(addr)); wr(addr, val); op_adc(c, val); c->cycles = 6; break;
    case 0x6F: addr = addr_abs(c); val = op_ror(c, rd(addr)); wr(addr, val); op_adc(c, val); c->cycles = 6; break;
    case 0x7F: addr = addr_abx(c, false); val = op_ror(c, rd(addr)); wr(addr, val); op_adc(c, val); c->cycles = 7; break;
    case 0x7B: addr = addr_aby(c, false); val = op_ror(c, rd(addr)); wr(addr, val); op_adc(c, val); c->cycles = 7; break;
    case 0x63: addr = addr_izx(c); val = op_ror(c, rd(addr)); wr(addr, val); op_adc(c, val); c->cycles = 8; break;
    case 0x73: addr = addr_izy(c, false); val = op_ror(c, rd(addr)); wr(addr, val); op_adc(c, val); c->cycles = 8; break;

    /* Remaining unofficial opcodes: treat as NOP with appropriate size */
    default:
        /* KIL/JAM opcodes and other unknowns - just NOP */
        c->cycles = 2;
        break;
    }

    if (c->cycles == 0) c->cycles = 2;  /* Safety: minimum 2 cycles */
    c->total_cycles += c->cycles;
    return c->cycles;
}
