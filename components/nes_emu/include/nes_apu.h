#pragma once

/**
 * NES APU (Audio Processing Unit) - Simplified
 */

#include <stdint.h>

#define NES_APU_BUF_SIZE  1024

typedef struct {
    /* Pulse channels */
    struct {
        uint8_t  duty;
        uint8_t  length_halt;
        uint8_t  constant_vol;
        uint8_t  volume;
        uint8_t  sweep_enable;
        uint8_t  sweep_period;
        uint8_t  sweep_negate;
        uint8_t  sweep_shift;
        uint16_t timer_period;
        uint16_t timer_val;
        uint8_t  duty_pos;
        uint8_t  length_counter;
        uint8_t  envelope_counter;
        uint8_t  envelope_divider;
        uint8_t  envelope_start;
        uint8_t  output;
    } pulse[2];

    /* Triangle channel */
    struct {
        uint8_t  control;
        uint8_t  linear_load;
        uint16_t timer_period;
        uint16_t timer_val;
        uint8_t  seq_pos;
        uint8_t  length_counter;
        uint8_t  linear_counter;
        uint8_t  linear_reload;
        uint8_t  output;
    } triangle;

    /* Noise channel */
    struct {
        uint8_t  length_halt;
        uint8_t  constant_vol;
        uint8_t  volume;
        uint8_t  mode;
        uint16_t timer_period;
        uint16_t timer_val;
        uint16_t shift_reg;
        uint8_t  length_counter;
        uint8_t  envelope_counter;
        uint8_t  envelope_divider;
        uint8_t  envelope_start;
        uint8_t  output;
    } noise;

    /* Frame counter */
    uint8_t  frame_mode;
    uint8_t  frame_step;
    uint16_t frame_timer;
    uint8_t  frame_irq_inhibit;

    uint8_t  status;
    uint64_t cycle_count;

    /* Audio output buffer */
    int16_t  sample_buf[NES_APU_BUF_SIZE];
    int      sample_count;
    int      sample_timer;
} nes_apu_t;

void nes_apu_init(nes_apu_t *apu);
void nes_apu_reset(nes_apu_t *apu);
uint8_t nes_apu_read(nes_apu_t *apu, uint16_t addr);
void nes_apu_write(nes_apu_t *apu, uint16_t addr, uint8_t val);
void nes_apu_tick(nes_apu_t *apu);
