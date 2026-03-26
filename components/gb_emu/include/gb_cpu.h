#pragma once

/**
 * Sharp LR35902 (Game Boy CPU) - similar to Z80
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* Registers (stored as pairs for easy 16-bit access) */
    union { struct { uint8_t f, a; }; uint16_t af; };
    union { struct { uint8_t c, b; }; uint16_t bc; };
    union { struct { uint8_t e, d; }; uint16_t de; };
    union { struct { uint8_t l, h; }; uint16_t hl; };
    uint16_t sp;
    uint16_t pc;

    bool ime;           /* Interrupt Master Enable */
    bool ime_pending;   /* EI delay */
    bool halted;

    int cycles;         /* Cycles consumed by last instruction */
    uint64_t total_cycles;
} gb_cpu_t;

typedef uint8_t (*gb_read_fn)(uint16_t addr);
typedef void    (*gb_write_fn)(uint16_t addr, uint8_t val);

void gb_cpu_init(gb_cpu_t *cpu);
void gb_cpu_reset(gb_cpu_t *cpu);
int  gb_cpu_step(gb_cpu_t *cpu, gb_read_fn rd, gb_write_fn wr);
