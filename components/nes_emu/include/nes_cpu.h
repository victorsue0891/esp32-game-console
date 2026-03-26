#pragma once

/**
 * MOS 6502 CPU Emulation (no BCD on NES)
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t pc;    /* Program counter */
    uint8_t  sp;    /* Stack pointer */
    uint8_t  a;     /* Accumulator */
    uint8_t  x;     /* X register */
    uint8_t  y;     /* Y register */

    /* Status flags */
    uint8_t  flag_c;  /* Carry */
    uint8_t  flag_z;  /* Zero */
    uint8_t  flag_i;  /* Interrupt disable */
    uint8_t  flag_d;  /* Decimal (unused on NES) */
    uint8_t  flag_v;  /* Overflow */
    uint8_t  flag_n;  /* Negative */

    int      cycles;    /* Cycles executed this step */
    uint64_t total_cycles;
    bool     nmi_pending;
    bool     irq_pending;
} nes_cpu_t;

/**
 * CPU memory read/write callbacks (set by the system).
 */
typedef uint8_t (*cpu_read_fn)(uint16_t addr);
typedef void    (*cpu_write_fn)(uint16_t addr, uint8_t val);

void nes_cpu_init(nes_cpu_t *cpu);
void nes_cpu_reset(nes_cpu_t *cpu, cpu_read_fn rd, cpu_write_fn wr);

/**
 * Execute one instruction. Returns number of cycles consumed.
 */
int nes_cpu_step(nes_cpu_t *cpu, cpu_read_fn rd, cpu_write_fn wr);

/**
 * Trigger NMI (from PPU vblank).
 */
void nes_cpu_nmi(nes_cpu_t *cpu);

/**
 * Trigger IRQ (from mapper/APU).
 */
void nes_cpu_irq(nes_cpu_t *cpu);

/**
 * Pack status register from flags.
 */
uint8_t nes_cpu_get_status(const nes_cpu_t *cpu);

/**
 * Unpack status register to flags.
 */
void nes_cpu_set_status(nes_cpu_t *cpu, uint8_t val);
