#pragma once

/**
 * Game Boy APU (Sound Controller) - Simplified
 */

#include <stdint.h>
#include <stdbool.h>

#define GB_APU_BUF_SIZE 1024

typedef struct {
    /* Channel 1: Pulse with sweep */
    struct {
        uint8_t  sweep_period, sweep_negate, sweep_shift;
        uint8_t  duty, length_load, env_init, env_dir, env_period;
        uint16_t freq;
        uint8_t  trigger, length_enable;
        /* Internal */
        uint16_t timer;
        uint8_t  duty_pos;
        uint8_t  volume;
        uint8_t  env_timer;
        uint16_t sweep_shadow;
        uint8_t  sweep_timer;
        bool     sweep_enable;
        uint16_t length_counter;
        bool     enabled;
    } ch1;

    /* Channel 2: Pulse */
    struct {
        uint8_t  duty, length_load, env_init, env_dir, env_period;
        uint16_t freq;
        uint8_t  trigger, length_enable;
        uint16_t timer;
        uint8_t  duty_pos;
        uint8_t  volume;
        uint8_t  env_timer;
        uint16_t length_counter;
        bool     enabled;
    } ch2;

    /* Channel 3: Wave */
    struct {
        uint8_t  dac_enable;
        uint16_t length_load;
        uint8_t  volume_code;
        uint16_t freq;
        uint8_t  trigger, length_enable;
        uint8_t  wave_ram[16];
        uint16_t timer;
        uint8_t  position;
        uint16_t length_counter;
        bool     enabled;
    } ch3;

    /* Channel 4: Noise */
    struct {
        uint8_t  length_load, env_init, env_dir, env_period;
        uint8_t  clock_shift, width_mode, divisor_code;
        uint8_t  trigger, length_enable;
        uint16_t timer;
        uint16_t lfsr;
        uint8_t  volume;
        uint8_t  env_timer;
        uint16_t length_counter;
        bool     enabled;
    } ch4;

    /* Master control */
    uint8_t nr50;   /* FF24 - Channel control */
    uint8_t nr51;   /* FF25 - Sound panning */
    uint8_t nr52;   /* FF26 - Master control */

    /* Frame sequencer */
    uint32_t frame_seq_counter;
    uint8_t  frame_seq_step;

    /* Output buffer */
    int16_t  sample_buf[GB_APU_BUF_SIZE];
    int      sample_count;
    int      sample_timer;

    uint64_t cycle_count;
} gb_apu_t;

void gb_apu_init(gb_apu_t *apu);
void gb_apu_reset(gb_apu_t *apu);
uint8_t gb_apu_read(gb_apu_t *apu, uint16_t addr);
void gb_apu_write(gb_apu_t *apu, uint16_t addr, uint8_t val);
void gb_apu_tick(gb_apu_t *apu, int cycles);
