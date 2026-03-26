/**
 * Sharp LR35902 CPU Emulation (Game Boy)
 * Complete instruction set including CB-prefixed bit operations.
 */

#include "gb_cpu.h"
#include <string.h>

/* Internal state */
static gb_read_fn  mem_rd;
static gb_write_fn mem_wr;

#define FLAG_Z  0x80
#define FLAG_N  0x40
#define FLAG_H  0x20
#define FLAG_C  0x10

static inline uint8_t rb(uint16_t a) { return mem_rd(a); }
static inline void wb(uint16_t a, uint8_t v) { mem_wr(a, v); }
static inline uint16_t rw(uint16_t a) { return rb(a) | ((uint16_t)rb(a+1) << 8); }

/* Stack helpers */
static inline void push16(gb_cpu_t *c, uint16_t val) {
    c->sp -= 2;
    wb(c->sp, val & 0xFF);
    wb(c->sp + 1, val >> 8);
}
static inline uint16_t pop16(gb_cpu_t *c) {
    uint16_t val = rb(c->sp) | ((uint16_t)rb(c->sp + 1) << 8);
    c->sp += 2;
    return val;
}

void gb_cpu_init(gb_cpu_t *cpu) {
    memset(cpu, 0, sizeof(*cpu));
}

void gb_cpu_reset(gb_cpu_t *cpu) {
    /* Post-boot ROM state */
    cpu->af = 0x01B0;
    cpu->bc = 0x0013;
    cpu->de = 0x00D8;
    cpu->hl = 0x014D;
    cpu->sp = 0xFFFE;
    cpu->pc = 0x0100;
    cpu->ime = false;
    cpu->halted = false;
    cpu->total_cycles = 0;
}

/* ---- ALU Helpers ---- */
static inline void set_flag(gb_cpu_t *c, uint8_t flag, bool val) {
    if (val) c->f |= flag; else c->f &= ~flag;
}

static inline uint8_t add8(gb_cpu_t *c, uint8_t a, uint8_t b, bool carry) {
    int ci = carry ? ((c->f & FLAG_C) ? 1 : 0) : 0;
    int result = a + b + ci;
    uint8_t r = result & 0xFF;
    c->f = 0;
    if (r == 0) c->f |= FLAG_Z;
    if ((a & 0xF) + (b & 0xF) + ci > 0xF) c->f |= FLAG_H;
    if (result > 0xFF) c->f |= FLAG_C;
    return r;
}

static inline uint8_t sub8(gb_cpu_t *c, uint8_t a, uint8_t b, bool carry) {
    int ci = carry ? ((c->f & FLAG_C) ? 1 : 0) : 0;
    int result = a - b - ci;
    uint8_t r = result & 0xFF;
    c->f = FLAG_N;
    if (r == 0) c->f |= FLAG_Z;
    if ((a & 0xF) < (b & 0xF) + ci) c->f |= FLAG_H;
    if (result < 0) c->f |= FLAG_C;
    return r;
}

static inline uint16_t add16(gb_cpu_t *c, uint16_t a, uint16_t b) {
    int result = a + b;
    c->f &= FLAG_Z;  /* Preserve Z */
    if ((a & 0xFFF) + (b & 0xFFF) > 0xFFF) c->f |= FLAG_H;
    if (result > 0xFFFF) c->f |= FLAG_C;
    return result & 0xFFFF;
}

static inline void and8(gb_cpu_t *c, uint8_t val) {
    c->a &= val;
    c->f = FLAG_H | (c->a == 0 ? FLAG_Z : 0);
}

static inline void or8(gb_cpu_t *c, uint8_t val) {
    c->a |= val;
    c->f = (c->a == 0 ? FLAG_Z : 0);
}

static inline void xor8(gb_cpu_t *c, uint8_t val) {
    c->a ^= val;
    c->f = (c->a == 0 ? FLAG_Z : 0);
}

static inline void cp8(gb_cpu_t *c, uint8_t val) {
    sub8(c, c->a, val, false);  /* Result discarded, flags set */
}

static inline uint8_t inc8(gb_cpu_t *c, uint8_t val) {
    val++;
    c->f = (c->f & FLAG_C) | (val == 0 ? FLAG_Z : 0) |
           ((val & 0xF) == 0 ? FLAG_H : 0);
    return val;
}

static inline uint8_t dec8(gb_cpu_t *c, uint8_t val) {
    val--;
    c->f = (c->f & FLAG_C) | FLAG_N | (val == 0 ? FLAG_Z : 0) |
           ((val & 0xF) == 0xF ? FLAG_H : 0);
    return val;
}

/* ---- CB-prefixed operations ---- */
static inline uint8_t rlc(gb_cpu_t *c, uint8_t val) {
    uint8_t carry = (val >> 7) & 1;
    val = (val << 1) | carry;
    c->f = (val == 0 ? FLAG_Z : 0) | (carry ? FLAG_C : 0);
    return val;
}

static inline uint8_t rrc(gb_cpu_t *c, uint8_t val) {
    uint8_t carry = val & 1;
    val = (val >> 1) | (carry << 7);
    c->f = (val == 0 ? FLAG_Z : 0) | (carry ? FLAG_C : 0);
    return val;
}

static inline uint8_t rl(gb_cpu_t *c, uint8_t val) {
    uint8_t old_carry = (c->f & FLAG_C) ? 1 : 0;
    uint8_t new_carry = (val >> 7) & 1;
    val = (val << 1) | old_carry;
    c->f = (val == 0 ? FLAG_Z : 0) | (new_carry ? FLAG_C : 0);
    return val;
}

static inline uint8_t rr(gb_cpu_t *c, uint8_t val) {
    uint8_t old_carry = (c->f & FLAG_C) ? 1 : 0;
    uint8_t new_carry = val & 1;
    val = (val >> 1) | (old_carry << 7);
    c->f = (val == 0 ? FLAG_Z : 0) | (new_carry ? FLAG_C : 0);
    return val;
}

static inline uint8_t sla(gb_cpu_t *c, uint8_t val) {
    uint8_t carry = (val >> 7) & 1;
    val <<= 1;
    c->f = (val == 0 ? FLAG_Z : 0) | (carry ? FLAG_C : 0);
    return val;
}

static inline uint8_t sra(gb_cpu_t *c, uint8_t val) {
    uint8_t carry = val & 1;
    val = (val >> 1) | (val & 0x80);
    c->f = (val == 0 ? FLAG_Z : 0) | (carry ? FLAG_C : 0);
    return val;
}

static inline uint8_t swap_op(gb_cpu_t *c, uint8_t val) {
    val = ((val & 0x0F) << 4) | ((val & 0xF0) >> 4);
    c->f = (val == 0 ? FLAG_Z : 0);
    return val;
}

static inline uint8_t srl(gb_cpu_t *c, uint8_t val) {
    uint8_t carry = val & 1;
    val >>= 1;
    c->f = (val == 0 ? FLAG_Z : 0) | (carry ? FLAG_C : 0);
    return val;
}

static inline void bit_op(gb_cpu_t *c, uint8_t bit, uint8_t val) {
    c->f = (c->f & FLAG_C) | FLAG_H | ((val & (1 << bit)) ? 0 : FLAG_Z);
}

/* Get/set register by 3-bit encoding */
static uint8_t get_reg8(gb_cpu_t *c, uint8_t r) {
    switch (r) {
    case 0: return c->b; case 1: return c->c;
    case 2: return c->d; case 3: return c->e;
    case 4: return c->h; case 5: return c->l;
    case 6: return rb(c->hl);
    case 7: return c->a;
    }
    return 0;
}

static void set_reg8(gb_cpu_t *c, uint8_t r, uint8_t val) {
    switch (r) {
    case 0: c->b = val; break; case 1: c->c = val; break;
    case 2: c->d = val; break; case 3: c->e = val; break;
    case 4: c->h = val; break; case 5: c->l = val; break;
    case 6: wb(c->hl, val); break;
    case 7: c->a = val; break;
    }
}

/* Execute CB-prefixed instruction */
static int exec_cb(gb_cpu_t *c) {
    uint8_t op = rb(c->pc++);
    uint8_t r = op & 7;
    uint8_t val = get_reg8(c, r);
    int extra = (r == 6) ? 8 : 0;  /* (HL) takes extra cycles */

    switch (op >> 3) {
    case 0: val = rlc(c, val); break;
    case 1: val = rrc(c, val); break;
    case 2: val = rl(c, val); break;
    case 3: val = rr(c, val); break;
    case 4: val = sla(c, val); break;
    case 5: val = sra(c, val); break;
    case 6: val = swap_op(c, val); break;
    case 7: val = srl(c, val); break;
    default: {
        uint8_t bit = (op >> 3) & 7;
        uint8_t group = op >> 6;
        if (group == 1) {
            bit_op(c, bit, val);
            return 8 + (r == 6 ? 4 : 0);  /* BIT doesn't write back */
        } else if (group == 2) {
            val &= ~(1 << bit);  /* RES */
        } else {
            val |= (1 << bit);   /* SET */
        }
    } break;
    }

    set_reg8(c, r, val);
    return 8 + extra;
}

/* ---- Main step function ---- */
int gb_cpu_step(gb_cpu_t *c, gb_read_fn rd, gb_write_fn wr) {
    mem_rd = rd;
    mem_wr = wr;

    /* Handle IME pending (EI is delayed by one instruction) */
    if (c->ime_pending) {
        c->ime_pending = false;
        c->ime = true;
    }

    /* Handle interrupts */
    uint8_t ie = rb(0xFFFF);
    uint8_t iflag = rb(0xFF0F);
    uint8_t pending = ie & iflag & 0x1F;

    if (c->halted) {
        if (pending) {
            c->halted = false;
        } else {
            c->cycles = 4;
            c->total_cycles += 4;
            return 4;
        }
    }

    if (c->ime && pending) {
        c->ime = false;
        push16(c, c->pc);
        /* Find highest priority interrupt */
        uint16_t vectors[] = {0x0040, 0x0048, 0x0050, 0x0058, 0x0060};
        for (int i = 0; i < 5; i++) {
            if (pending & (1 << i)) {
                wb(0xFF0F, iflag & ~(1 << i));
                c->pc = vectors[i];
                break;
            }
        }
        c->cycles = 20;
        c->total_cycles += 20;
        return 20;
    }

    uint8_t op = rb(c->pc++);
    int cycles = 4;

    switch (op) {
    /* ---- NOP ---- */
    case 0x00: cycles = 4; break;

    /* ---- LD r16, d16 ---- */
    case 0x01: c->bc = rw(c->pc); c->pc += 2; cycles = 12; break;
    case 0x11: c->de = rw(c->pc); c->pc += 2; cycles = 12; break;
    case 0x21: c->hl = rw(c->pc); c->pc += 2; cycles = 12; break;
    case 0x31: c->sp = rw(c->pc); c->pc += 2; cycles = 12; break;

    /* ---- LD (r16), A ---- */
    case 0x02: wb(c->bc, c->a); cycles = 8; break;
    case 0x12: wb(c->de, c->a); cycles = 8; break;
    case 0x22: wb(c->hl++, c->a); cycles = 8; break;
    case 0x32: wb(c->hl--, c->a); cycles = 8; break;

    /* ---- LD A, (r16) ---- */
    case 0x0A: c->a = rb(c->bc); cycles = 8; break;
    case 0x1A: c->a = rb(c->de); cycles = 8; break;
    case 0x2A: c->a = rb(c->hl++); cycles = 8; break;
    case 0x3A: c->a = rb(c->hl--); cycles = 8; break;

    /* ---- INC r16 ---- */
    case 0x03: c->bc++; cycles = 8; break;
    case 0x13: c->de++; cycles = 8; break;
    case 0x23: c->hl++; cycles = 8; break;
    case 0x33: c->sp++; cycles = 8; break;

    /* ---- DEC r16 ---- */
    case 0x0B: c->bc--; cycles = 8; break;
    case 0x1B: c->de--; cycles = 8; break;
    case 0x2B: c->hl--; cycles = 8; break;
    case 0x3B: c->sp--; cycles = 8; break;

    /* ---- ADD HL, r16 ---- */
    case 0x09: c->hl = add16(c, c->hl, c->bc); cycles = 8; break;
    case 0x19: c->hl = add16(c, c->hl, c->de); cycles = 8; break;
    case 0x29: c->hl = add16(c, c->hl, c->hl); cycles = 8; break;
    case 0x39: c->hl = add16(c, c->hl, c->sp); cycles = 8; break;

    /* ---- INC r8 ---- */
    case 0x04: c->b = inc8(c, c->b); cycles = 4; break;
    case 0x0C: c->c = inc8(c, c->c); cycles = 4; break;
    case 0x14: c->d = inc8(c, c->d); cycles = 4; break;
    case 0x1C: c->e = inc8(c, c->e); cycles = 4; break;
    case 0x24: c->h = inc8(c, c->h); cycles = 4; break;
    case 0x2C: c->l = inc8(c, c->l); cycles = 4; break;
    case 0x34: wb(c->hl, inc8(c, rb(c->hl))); cycles = 12; break;
    case 0x3C: c->a = inc8(c, c->a); cycles = 4; break;

    /* ---- DEC r8 ---- */
    case 0x05: c->b = dec8(c, c->b); cycles = 4; break;
    case 0x0D: c->c = dec8(c, c->c); cycles = 4; break;
    case 0x15: c->d = dec8(c, c->d); cycles = 4; break;
    case 0x1D: c->e = dec8(c, c->e); cycles = 4; break;
    case 0x25: c->h = dec8(c, c->h); cycles = 4; break;
    case 0x2D: c->l = dec8(c, c->l); cycles = 4; break;
    case 0x35: wb(c->hl, dec8(c, rb(c->hl))); cycles = 12; break;
    case 0x3D: c->a = dec8(c, c->a); cycles = 4; break;

    /* ---- LD r8, d8 ---- */
    case 0x06: c->b = rb(c->pc++); cycles = 8; break;
    case 0x0E: c->c = rb(c->pc++); cycles = 8; break;
    case 0x16: c->d = rb(c->pc++); cycles = 8; break;
    case 0x1E: c->e = rb(c->pc++); cycles = 8; break;
    case 0x26: c->h = rb(c->pc++); cycles = 8; break;
    case 0x2E: c->l = rb(c->pc++); cycles = 8; break;
    case 0x36: wb(c->hl, rb(c->pc++)); cycles = 12; break;
    case 0x3E: c->a = rb(c->pc++); cycles = 8; break;

    /* ---- Rotate A ---- */
    case 0x07: { /* RLCA */
        uint8_t carry = (c->a >> 7) & 1;
        c->a = (c->a << 1) | carry;
        c->f = carry ? FLAG_C : 0;
        cycles = 4;
    } break;
    case 0x0F: { /* RRCA */
        uint8_t carry = c->a & 1;
        c->a = (c->a >> 1) | (carry << 7);
        c->f = carry ? FLAG_C : 0;
        cycles = 4;
    } break;
    case 0x17: { /* RLA */
        uint8_t old_c = (c->f & FLAG_C) ? 1 : 0;
        uint8_t new_c = (c->a >> 7) & 1;
        c->a = (c->a << 1) | old_c;
        c->f = new_c ? FLAG_C : 0;
        cycles = 4;
    } break;
    case 0x1F: { /* RRA */
        uint8_t old_c = (c->f & FLAG_C) ? 1 : 0;
        uint8_t new_c = c->a & 1;
        c->a = (c->a >> 1) | (old_c << 7);
        c->f = new_c ? FLAG_C : 0;
        cycles = 4;
    } break;

    /* ---- DAA ---- */
    case 0x27: {
        int a = c->a;
        if (!(c->f & FLAG_N)) {
            if ((c->f & FLAG_H) || (a & 0xF) > 9) a += 0x06;
            if ((c->f & FLAG_C) || a > 0x9F) a += 0x60;
        } else {
            if (c->f & FLAG_H) a -= 0x06;
            if (c->f & FLAG_C) a -= 0x60;
        }
        c->a = a & 0xFF;
        c->f = (c->f & (FLAG_N | FLAG_C)) | (c->a == 0 ? FLAG_Z : 0);
        if (a & 0x100) c->f |= FLAG_C;
        c->f &= ~FLAG_H;
        cycles = 4;
    } break;

    /* ---- CPL ---- */
    case 0x2F: c->a = ~c->a; c->f |= FLAG_N | FLAG_H; cycles = 4; break;

    /* ---- SCF/CCF ---- */
    case 0x37: c->f = (c->f & FLAG_Z) | FLAG_C; cycles = 4; break;
    case 0x3F: c->f = (c->f & FLAG_Z) | ((c->f & FLAG_C) ? 0 : FLAG_C); cycles = 4; break;

    /* ---- LD (a16), SP ---- */
    case 0x08: {
        uint16_t addr = rw(c->pc); c->pc += 2;
        wb(addr, c->sp & 0xFF);
        wb(addr + 1, c->sp >> 8);
        cycles = 20;
    } break;

    /* ---- JR ---- */
    case 0x18: c->pc += (int8_t)rb(c->pc) + 1; cycles = 12; break;
    case 0x20:
        if (!(c->f & FLAG_Z)) { c->pc += (int8_t)rb(c->pc) + 1; cycles = 12; }
        else { c->pc++; cycles = 8; } break;
    case 0x28:
        if (c->f & FLAG_Z) { c->pc += (int8_t)rb(c->pc) + 1; cycles = 12; }
        else { c->pc++; cycles = 8; } break;
    case 0x30:
        if (!(c->f & FLAG_C)) { c->pc += (int8_t)rb(c->pc) + 1; cycles = 12; }
        else { c->pc++; cycles = 8; } break;
    case 0x38:
        if (c->f & FLAG_C) { c->pc += (int8_t)rb(c->pc) + 1; cycles = 12; }
        else { c->pc++; cycles = 8; } break;

    /* ---- STOP/HALT ---- */
    case 0x10: c->pc++; cycles = 4; break; /* STOP */
    case 0x76: c->halted = true; cycles = 4; break; /* HALT */

    /* ---- LD r8, r8 (0x40-0x7F block) ---- */
    case 0x40: c->b = c->b; cycles = 4; break;
    case 0x41: c->b = c->c; cycles = 4; break;
    case 0x42: c->b = c->d; cycles = 4; break;
    case 0x43: c->b = c->e; cycles = 4; break;
    case 0x44: c->b = c->h; cycles = 4; break;
    case 0x45: c->b = c->l; cycles = 4; break;
    case 0x46: c->b = rb(c->hl); cycles = 8; break;
    case 0x47: c->b = c->a; cycles = 4; break;
    case 0x48: c->c = c->b; cycles = 4; break;
    case 0x49: c->c = c->c; cycles = 4; break;
    case 0x4A: c->c = c->d; cycles = 4; break;
    case 0x4B: c->c = c->e; cycles = 4; break;
    case 0x4C: c->c = c->h; cycles = 4; break;
    case 0x4D: c->c = c->l; cycles = 4; break;
    case 0x4E: c->c = rb(c->hl); cycles = 8; break;
    case 0x4F: c->c = c->a; cycles = 4; break;
    case 0x50: c->d = c->b; cycles = 4; break;
    case 0x51: c->d = c->c; cycles = 4; break;
    case 0x52: c->d = c->d; cycles = 4; break;
    case 0x53: c->d = c->e; cycles = 4; break;
    case 0x54: c->d = c->h; cycles = 4; break;
    case 0x55: c->d = c->l; cycles = 4; break;
    case 0x56: c->d = rb(c->hl); cycles = 8; break;
    case 0x57: c->d = c->a; cycles = 4; break;
    case 0x58: c->e = c->b; cycles = 4; break;
    case 0x59: c->e = c->c; cycles = 4; break;
    case 0x5A: c->e = c->d; cycles = 4; break;
    case 0x5B: c->e = c->e; cycles = 4; break;
    case 0x5C: c->e = c->h; cycles = 4; break;
    case 0x5D: c->e = c->l; cycles = 4; break;
    case 0x5E: c->e = rb(c->hl); cycles = 8; break;
    case 0x5F: c->e = c->a; cycles = 4; break;
    case 0x60: c->h = c->b; cycles = 4; break;
    case 0x61: c->h = c->c; cycles = 4; break;
    case 0x62: c->h = c->d; cycles = 4; break;
    case 0x63: c->h = c->e; cycles = 4; break;
    case 0x64: c->h = c->h; cycles = 4; break;
    case 0x65: c->h = c->l; cycles = 4; break;
    case 0x66: c->h = rb(c->hl); cycles = 8; break;
    case 0x67: c->h = c->a; cycles = 4; break;
    case 0x68: c->l = c->b; cycles = 4; break;
    case 0x69: c->l = c->c; cycles = 4; break;
    case 0x6A: c->l = c->d; cycles = 4; break;
    case 0x6B: c->l = c->e; cycles = 4; break;
    case 0x6C: c->l = c->h; cycles = 4; break;
    case 0x6D: c->l = c->l; cycles = 4; break;
    case 0x6E: c->l = rb(c->hl); cycles = 8; break;
    case 0x6F: c->l = c->a; cycles = 4; break;
    case 0x70: wb(c->hl, c->b); cycles = 8; break;
    case 0x71: wb(c->hl, c->c); cycles = 8; break;
    case 0x72: wb(c->hl, c->d); cycles = 8; break;
    case 0x73: wb(c->hl, c->e); cycles = 8; break;
    case 0x74: wb(c->hl, c->h); cycles = 8; break;
    case 0x75: wb(c->hl, c->l); cycles = 8; break;
    /* 0x76 = HALT (handled above) */
    case 0x77: wb(c->hl, c->a); cycles = 8; break;
    case 0x78: c->a = c->b; cycles = 4; break;
    case 0x79: c->a = c->c; cycles = 4; break;
    case 0x7A: c->a = c->d; cycles = 4; break;
    case 0x7B: c->a = c->e; cycles = 4; break;
    case 0x7C: c->a = c->h; cycles = 4; break;
    case 0x7D: c->a = c->l; cycles = 4; break;
    case 0x7E: c->a = rb(c->hl); cycles = 8; break;
    case 0x7F: c->a = c->a; cycles = 4; break;

    /* ---- ADD A, r8 ---- */
    case 0x80: c->a = add8(c, c->a, c->b, false); cycles = 4; break;
    case 0x81: c->a = add8(c, c->a, c->c, false); cycles = 4; break;
    case 0x82: c->a = add8(c, c->a, c->d, false); cycles = 4; break;
    case 0x83: c->a = add8(c, c->a, c->e, false); cycles = 4; break;
    case 0x84: c->a = add8(c, c->a, c->h, false); cycles = 4; break;
    case 0x85: c->a = add8(c, c->a, c->l, false); cycles = 4; break;
    case 0x86: c->a = add8(c, c->a, rb(c->hl), false); cycles = 8; break;
    case 0x87: c->a = add8(c, c->a, c->a, false); cycles = 4; break;

    /* ---- ADC A, r8 ---- */
    case 0x88: c->a = add8(c, c->a, c->b, true); cycles = 4; break;
    case 0x89: c->a = add8(c, c->a, c->c, true); cycles = 4; break;
    case 0x8A: c->a = add8(c, c->a, c->d, true); cycles = 4; break;
    case 0x8B: c->a = add8(c, c->a, c->e, true); cycles = 4; break;
    case 0x8C: c->a = add8(c, c->a, c->h, true); cycles = 4; break;
    case 0x8D: c->a = add8(c, c->a, c->l, true); cycles = 4; break;
    case 0x8E: c->a = add8(c, c->a, rb(c->hl), true); cycles = 8; break;
    case 0x8F: c->a = add8(c, c->a, c->a, true); cycles = 4; break;

    /* ---- SUB r8 ---- */
    case 0x90: c->a = sub8(c, c->a, c->b, false); cycles = 4; break;
    case 0x91: c->a = sub8(c, c->a, c->c, false); cycles = 4; break;
    case 0x92: c->a = sub8(c, c->a, c->d, false); cycles = 4; break;
    case 0x93: c->a = sub8(c, c->a, c->e, false); cycles = 4; break;
    case 0x94: c->a = sub8(c, c->a, c->h, false); cycles = 4; break;
    case 0x95: c->a = sub8(c, c->a, c->l, false); cycles = 4; break;
    case 0x96: c->a = sub8(c, c->a, rb(c->hl), false); cycles = 8; break;
    case 0x97: c->a = sub8(c, c->a, c->a, false); cycles = 4; break;

    /* ---- SBC A, r8 ---- */
    case 0x98: c->a = sub8(c, c->a, c->b, true); cycles = 4; break;
    case 0x99: c->a = sub8(c, c->a, c->c, true); cycles = 4; break;
    case 0x9A: c->a = sub8(c, c->a, c->d, true); cycles = 4; break;
    case 0x9B: c->a = sub8(c, c->a, c->e, true); cycles = 4; break;
    case 0x9C: c->a = sub8(c, c->a, c->h, true); cycles = 4; break;
    case 0x9D: c->a = sub8(c, c->a, c->l, true); cycles = 4; break;
    case 0x9E: c->a = sub8(c, c->a, rb(c->hl), true); cycles = 8; break;
    case 0x9F: c->a = sub8(c, c->a, c->a, true); cycles = 4; break;

    /* ---- AND r8 ---- */
    case 0xA0: and8(c, c->b); cycles = 4; break;
    case 0xA1: and8(c, c->c); cycles = 4; break;
    case 0xA2: and8(c, c->d); cycles = 4; break;
    case 0xA3: and8(c, c->e); cycles = 4; break;
    case 0xA4: and8(c, c->h); cycles = 4; break;
    case 0xA5: and8(c, c->l); cycles = 4; break;
    case 0xA6: and8(c, rb(c->hl)); cycles = 8; break;
    case 0xA7: and8(c, c->a); cycles = 4; break;

    /* ---- XOR r8 ---- */
    case 0xA8: xor8(c, c->b); cycles = 4; break;
    case 0xA9: xor8(c, c->c); cycles = 4; break;
    case 0xAA: xor8(c, c->d); cycles = 4; break;
    case 0xAB: xor8(c, c->e); cycles = 4; break;
    case 0xAC: xor8(c, c->h); cycles = 4; break;
    case 0xAD: xor8(c, c->l); cycles = 4; break;
    case 0xAE: xor8(c, rb(c->hl)); cycles = 8; break;
    case 0xAF: xor8(c, c->a); cycles = 4; break;

    /* ---- OR r8 ---- */
    case 0xB0: or8(c, c->b); cycles = 4; break;
    case 0xB1: or8(c, c->c); cycles = 4; break;
    case 0xB2: or8(c, c->d); cycles = 4; break;
    case 0xB3: or8(c, c->e); cycles = 4; break;
    case 0xB4: or8(c, c->h); cycles = 4; break;
    case 0xB5: or8(c, c->l); cycles = 4; break;
    case 0xB6: or8(c, rb(c->hl)); cycles = 8; break;
    case 0xB7: or8(c, c->a); cycles = 4; break;

    /* ---- CP r8 ---- */
    case 0xB8: cp8(c, c->b); cycles = 4; break;
    case 0xB9: cp8(c, c->c); cycles = 4; break;
    case 0xBA: cp8(c, c->d); cycles = 4; break;
    case 0xBB: cp8(c, c->e); cycles = 4; break;
    case 0xBC: cp8(c, c->h); cycles = 4; break;
    case 0xBD: cp8(c, c->l); cycles = 4; break;
    case 0xBE: cp8(c, rb(c->hl)); cycles = 8; break;
    case 0xBF: cp8(c, c->a); cycles = 4; break;

    /* ---- ALU A, d8 ---- */
    case 0xC6: c->a = add8(c, c->a, rb(c->pc++), false); cycles = 8; break;
    case 0xCE: c->a = add8(c, c->a, rb(c->pc++), true);  cycles = 8; break;
    case 0xD6: c->a = sub8(c, c->a, rb(c->pc++), false); cycles = 8; break;
    case 0xDE: c->a = sub8(c, c->a, rb(c->pc++), true);  cycles = 8; break;
    case 0xE6: and8(c, rb(c->pc++)); cycles = 8; break;
    case 0xEE: xor8(c, rb(c->pc++)); cycles = 8; break;
    case 0xF6: or8(c, rb(c->pc++));  cycles = 8; break;
    case 0xFE: cp8(c, rb(c->pc++));  cycles = 8; break;

    /* ---- RET/RETI ---- */
    case 0xC9: c->pc = pop16(c); cycles = 16; break;
    case 0xD9: c->pc = pop16(c); c->ime = true; cycles = 16; break; /* RETI */
    case 0xC0: if (!(c->f & FLAG_Z)) { c->pc = pop16(c); cycles = 20; } else cycles = 8; break;
    case 0xC8: if (c->f & FLAG_Z)    { c->pc = pop16(c); cycles = 20; } else cycles = 8; break;
    case 0xD0: if (!(c->f & FLAG_C)) { c->pc = pop16(c); cycles = 20; } else cycles = 8; break;
    case 0xD8: if (c->f & FLAG_C)    { c->pc = pop16(c); cycles = 20; } else cycles = 8; break;

    /* ---- JP ---- */
    case 0xC3: c->pc = rw(c->pc); cycles = 16; break;
    case 0xC2: if (!(c->f & FLAG_Z)) { c->pc = rw(c->pc); cycles = 16; } else { c->pc += 2; cycles = 12; } break;
    case 0xCA: if (c->f & FLAG_Z)    { c->pc = rw(c->pc); cycles = 16; } else { c->pc += 2; cycles = 12; } break;
    case 0xD2: if (!(c->f & FLAG_C)) { c->pc = rw(c->pc); cycles = 16; } else { c->pc += 2; cycles = 12; } break;
    case 0xDA: if (c->f & FLAG_C)    { c->pc = rw(c->pc); cycles = 16; } else { c->pc += 2; cycles = 12; } break;
    case 0xE9: c->pc = c->hl; cycles = 4; break; /* JP HL */

    /* ---- CALL ---- */
    case 0xCD: { uint16_t a = rw(c->pc); c->pc += 2; push16(c, c->pc); c->pc = a; cycles = 24; } break;
    case 0xC4: if (!(c->f & FLAG_Z)) { uint16_t a = rw(c->pc); c->pc += 2; push16(c, c->pc); c->pc = a; cycles = 24; } else { c->pc += 2; cycles = 12; } break;
    case 0xCC: if (c->f & FLAG_Z)    { uint16_t a = rw(c->pc); c->pc += 2; push16(c, c->pc); c->pc = a; cycles = 24; } else { c->pc += 2; cycles = 12; } break;
    case 0xD4: if (!(c->f & FLAG_C)) { uint16_t a = rw(c->pc); c->pc += 2; push16(c, c->pc); c->pc = a; cycles = 24; } else { c->pc += 2; cycles = 12; } break;
    case 0xDC: if (c->f & FLAG_C)    { uint16_t a = rw(c->pc); c->pc += 2; push16(c, c->pc); c->pc = a; cycles = 24; } else { c->pc += 2; cycles = 12; } break;

    /* ---- RST ---- */
    case 0xC7: push16(c, c->pc); c->pc = 0x00; cycles = 16; break;
    case 0xCF: push16(c, c->pc); c->pc = 0x08; cycles = 16; break;
    case 0xD7: push16(c, c->pc); c->pc = 0x10; cycles = 16; break;
    case 0xDF: push16(c, c->pc); c->pc = 0x18; cycles = 16; break;
    case 0xE7: push16(c, c->pc); c->pc = 0x20; cycles = 16; break;
    case 0xEF: push16(c, c->pc); c->pc = 0x28; cycles = 16; break;
    case 0xF7: push16(c, c->pc); c->pc = 0x30; cycles = 16; break;
    case 0xFF: push16(c, c->pc); c->pc = 0x38; cycles = 16; break;

    /* ---- PUSH/POP ---- */
    case 0xC1: c->bc = pop16(c); cycles = 12; break;
    case 0xD1: c->de = pop16(c); cycles = 12; break;
    case 0xE1: c->hl = pop16(c); cycles = 12; break;
    case 0xF1: c->af = pop16(c) & 0xFFF0; cycles = 12; break; /* Lower 4 bits of F are always 0 */
    case 0xC5: push16(c, c->bc); cycles = 16; break;
    case 0xD5: push16(c, c->de); cycles = 16; break;
    case 0xE5: push16(c, c->hl); cycles = 16; break;
    case 0xF5: push16(c, c->af); cycles = 16; break;

    /* ---- LDH ---- */
    case 0xE0: wb(0xFF00 + rb(c->pc++), c->a); cycles = 12; break;
    case 0xF0: c->a = rb(0xFF00 + rb(c->pc++)); cycles = 12; break;

    /* ---- LD (C), A / LD A, (C) ---- */
    case 0xE2: wb(0xFF00 + c->c, c->a); cycles = 8; break;
    case 0xF2: c->a = rb(0xFF00 + c->c); cycles = 8; break;

    /* ---- LD (a16), A / LD A, (a16) ---- */
    case 0xEA: wb(rw(c->pc), c->a); c->pc += 2; cycles = 16; break;
    case 0xFA: c->a = rb(rw(c->pc)); c->pc += 2; cycles = 16; break;

    /* ---- DI/EI ---- */
    case 0xF3: c->ime = false; cycles = 4; break;
    case 0xFB: c->ime_pending = true; cycles = 4; break;

    /* ---- ADD SP, s8 ---- */
    case 0xE8: {
        int8_t offset = (int8_t)rb(c->pc++);
        c->f = 0;
        if (((c->sp & 0xF) + (offset & 0xF)) > 0xF) c->f |= FLAG_H;
        if (((c->sp & 0xFF) + (offset & 0xFF)) > 0xFF) c->f |= FLAG_C;
        c->sp += offset;
        cycles = 16;
    } break;

    /* ---- LD HL, SP+s8 ---- */
    case 0xF8: {
        int8_t offset = (int8_t)rb(c->pc++);
        c->f = 0;
        if (((c->sp & 0xF) + (offset & 0xF)) > 0xF) c->f |= FLAG_H;
        if (((c->sp & 0xFF) + (offset & 0xFF)) > 0xFF) c->f |= FLAG_C;
        c->hl = c->sp + offset;
        cycles = 12;
    } break;

    /* ---- LD SP, HL ---- */
    case 0xF9: c->sp = c->hl; cycles = 8; break;

    /* ---- CB prefix ---- */
    case 0xCB: cycles = exec_cb(c); break;

    /* ---- Undefined opcodes ---- */
    default: cycles = 4; break;
    }

    c->cycles = cycles;
    c->total_cycles += cycles;
    return cycles;
}
