/**
 * NES APU (Audio Processing Unit) - Simplified implementation
 * Pulse 1/2, Triangle, and Noise channels.
 */

#include "nes_apu.h"
#include <string.h>

/* Duty cycle sequences for pulse channels */
static const uint8_t duty_table[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1},
};

/* Triangle sequence */
static const uint8_t tri_table[32] = {
    15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
};

/* Noise period table (NTSC) */
static const uint16_t noise_period_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

/* Length counter lookup */
static const uint8_t length_table[32] = {
    10,254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

/* CPU clock rate / target sample rate for downsampling */
#define CPU_CLOCK_RATE  1789773
#define SAMPLE_RATE     22050
#define CYCLES_PER_SAMPLE (CPU_CLOCK_RATE / SAMPLE_RATE)

void nes_apu_init(nes_apu_t *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->noise.shift_reg = 1;
}

void nes_apu_reset(nes_apu_t *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->noise.shift_reg = 1;
}

/* --- Clock envelope --- */
static void clock_envelope(uint8_t *counter, uint8_t *divider,
                           uint8_t *start, uint8_t volume,
                           uint8_t constant_vol, uint8_t loop_flag) {
    if (*start) {
        *start = 0;
        *counter = 15;
        *divider = volume;
    } else {
        if (*divider == 0) {
            *divider = volume;
            if (*counter > 0) {
                (*counter)--;
            } else if (loop_flag) {
                *counter = 15;
            }
        } else {
            (*divider)--;
        }
    }
}

/* --- Quarter frame --- */
static void quarter_frame(nes_apu_t *apu) {
    /* Pulse envelopes */
    for (int i = 0; i < 2; i++) {
        clock_envelope(&apu->pulse[i].envelope_counter,
                       &apu->pulse[i].envelope_divider,
                       &apu->pulse[i].envelope_start,
                       apu->pulse[i].volume,
                       apu->pulse[i].constant_vol,
                       apu->pulse[i].length_halt);
    }
    /* Noise envelope */
    clock_envelope(&apu->noise.envelope_counter,
                   &apu->noise.envelope_divider,
                   &apu->noise.envelope_start,
                   apu->noise.volume,
                   apu->noise.constant_vol,
                   apu->noise.length_halt);

    /* Triangle linear counter */
    if (apu->triangle.linear_reload) {
        apu->triangle.linear_counter = apu->triangle.linear_load;
    } else if (apu->triangle.linear_counter > 0) {
        apu->triangle.linear_counter--;
    }
    if (!apu->triangle.control) {
        apu->triangle.linear_reload = 0;
    }
}

/* --- Half frame --- */
static void half_frame(nes_apu_t *apu) {
    /* Length counters */
    for (int i = 0; i < 2; i++) {
        if (!apu->pulse[i].length_halt && apu->pulse[i].length_counter > 0)
            apu->pulse[i].length_counter--;
    }
    if (!apu->triangle.control && apu->triangle.length_counter > 0)
        apu->triangle.length_counter--;
    if (!apu->noise.length_halt && apu->noise.length_counter > 0)
        apu->noise.length_counter--;

    /* Pulse sweep units (simplified) */
    for (int i = 0; i < 2; i++) {
        if (apu->pulse[i].sweep_enable) {
            uint16_t period = apu->pulse[i].timer_period;
            uint16_t change = period >> apu->pulse[i].sweep_shift;
            if (apu->pulse[i].sweep_negate) {
                period -= change;
                if (i == 0) period--;
            } else {
                period += change;
            }
            if (period >= 8 && period <= 0x7FF) {
                apu->pulse[i].timer_period = period;
            }
        }
    }
}

/* --- Frame counter --- */
static void frame_counter_tick(nes_apu_t *apu) {
    apu->frame_timer++;

    if (apu->frame_mode == 0) {
        /* 4-step mode */
        switch (apu->frame_timer) {
        case 3729:  quarter_frame(apu); break;
        case 7457:  quarter_frame(apu); half_frame(apu); break;
        case 11186: quarter_frame(apu); break;
        case 14915:
            quarter_frame(apu); half_frame(apu);
            apu->frame_timer = 0;
            break;
        }
    } else {
        /* 5-step mode */
        switch (apu->frame_timer) {
        case 3729:  quarter_frame(apu); break;
        case 7457:  quarter_frame(apu); half_frame(apu); break;
        case 11186: quarter_frame(apu); break;
        case 14915: break;
        case 18641:
            quarter_frame(apu); half_frame(apu);
            apu->frame_timer = 0;
            break;
        }
    }
}

/* Register writes */
void nes_apu_write(nes_apu_t *apu, uint16_t addr, uint8_t val) {
    int ch;
    switch (addr) {
    /* Pulse 1: $4000-$4003 */
    case 0x4000: case 0x4004:
        ch = (addr >= 0x4004) ? 1 : 0;
        apu->pulse[ch].duty = (val >> 6) & 3;
        apu->pulse[ch].length_halt = (val >> 5) & 1;
        apu->pulse[ch].constant_vol = (val >> 4) & 1;
        apu->pulse[ch].volume = val & 0x0F;
        break;
    case 0x4001: case 0x4005:
        ch = (addr >= 0x4004) ? 1 : 0;
        apu->pulse[ch].sweep_enable = (val >> 7) & 1;
        apu->pulse[ch].sweep_period = (val >> 4) & 7;
        apu->pulse[ch].sweep_negate = (val >> 3) & 1;
        apu->pulse[ch].sweep_shift = val & 7;
        break;
    case 0x4002: case 0x4006:
        ch = (addr >= 0x4004) ? 1 : 0;
        apu->pulse[ch].timer_period = (apu->pulse[ch].timer_period & 0x700) | val;
        break;
    case 0x4003: case 0x4007:
        ch = (addr >= 0x4004) ? 1 : 0;
        apu->pulse[ch].timer_period = (apu->pulse[ch].timer_period & 0xFF) | ((val & 7) << 8);
        if (apu->status & (1 << ch))
            apu->pulse[ch].length_counter = length_table[val >> 3];
        apu->pulse[ch].envelope_start = 1;
        apu->pulse[ch].duty_pos = 0;
        break;

    /* Triangle: $4008-$400B */
    case 0x4008:
        apu->triangle.control = (val >> 7) & 1;
        apu->triangle.linear_load = val & 0x7F;
        break;
    case 0x400A:
        apu->triangle.timer_period = (apu->triangle.timer_period & 0x700) | val;
        break;
    case 0x400B:
        apu->triangle.timer_period = (apu->triangle.timer_period & 0xFF) | ((val & 7) << 8);
        if (apu->status & 0x04)
            apu->triangle.length_counter = length_table[val >> 3];
        apu->triangle.linear_reload = 1;
        break;

    /* Noise: $400C-$400F */
    case 0x400C:
        apu->noise.length_halt = (val >> 5) & 1;
        apu->noise.constant_vol = (val >> 4) & 1;
        apu->noise.volume = val & 0x0F;
        break;
    case 0x400E:
        apu->noise.mode = (val >> 7) & 1;
        apu->noise.timer_period = noise_period_table[val & 0x0F];
        break;
    case 0x400F:
        if (apu->status & 0x08)
            apu->noise.length_counter = length_table[val >> 3];
        apu->noise.envelope_start = 1;
        break;

    /* Status: $4015 */
    case 0x4015:
        apu->status = val & 0x1F;
        if (!(val & 0x01)) apu->pulse[0].length_counter = 0;
        if (!(val & 0x02)) apu->pulse[1].length_counter = 0;
        if (!(val & 0x04)) apu->triangle.length_counter = 0;
        if (!(val & 0x08)) apu->noise.length_counter = 0;
        break;

    /* Frame counter: $4017 */
    case 0x4017:
        apu->frame_mode = (val >> 7) & 1;
        apu->frame_irq_inhibit = (val >> 6) & 1;
        apu->frame_timer = 0;
        if (apu->frame_mode) {
            quarter_frame(apu);
            half_frame(apu);
        }
        break;
    }
}

uint8_t nes_apu_read(nes_apu_t *apu, uint16_t addr) {
    if (addr == 0x4015) {
        uint8_t ret = 0;
        if (apu->pulse[0].length_counter > 0)   ret |= 0x01;
        if (apu->pulse[1].length_counter > 0)   ret |= 0x02;
        if (apu->triangle.length_counter > 0)    ret |= 0x04;
        if (apu->noise.length_counter > 0)       ret |= 0x08;
        return ret;
    }
    return 0;
}

/* Generate one sample of output */
static int16_t mix_output(nes_apu_t *apu) {
    /* Pulse outputs */
    int p1 = 0, p2 = 0;
    for (int i = 0; i < 2; i++) {
        if (apu->pulse[i].length_counter == 0) continue;
        if (apu->pulse[i].timer_period < 8) continue;
        uint8_t vol = apu->pulse[i].constant_vol ?
                      apu->pulse[i].volume : apu->pulse[i].envelope_counter;
        uint8_t duty_out = duty_table[apu->pulse[i].duty][apu->pulse[i].duty_pos];
        int out = duty_out ? vol : 0;
        if (i == 0) p1 = out; else p2 = out;
    }

    /* Triangle output */
    int tri_out = 0;
    if (apu->triangle.length_counter > 0 && apu->triangle.linear_counter > 0) {
        tri_out = tri_table[apu->triangle.seq_pos];
    }

    /* Noise output */
    int noise_out = 0;
    if (apu->noise.length_counter > 0 && !(apu->noise.shift_reg & 1)) {
        noise_out = apu->noise.constant_vol ?
                    apu->noise.volume : apu->noise.envelope_counter;
    }

    /* Mixer (approximate linear mix) */
    float pulse_out = 0.00752f * (p1 + p2);
    float tnd_out = 0.00851f * tri_out + 0.00494f * noise_out;
    float mixed = pulse_out + tnd_out;

    return (int16_t)(mixed * 20000.0f);
}

void nes_apu_tick(nes_apu_t *apu) {
    apu->cycle_count++;

    /* Frame counter */
    frame_counter_tick(apu);

    /* Clock pulse timers (every 2 CPU cycles) */
    if (apu->cycle_count & 1) {
        for (int i = 0; i < 2; i++) {
            if (apu->pulse[i].timer_val == 0) {
                apu->pulse[i].timer_val = apu->pulse[i].timer_period;
                apu->pulse[i].duty_pos = (apu->pulse[i].duty_pos + 1) & 7;
            } else {
                apu->pulse[i].timer_val--;
            }
        }
    }

    /* Clock triangle timer (every CPU cycle) */
    if (apu->triangle.timer_val == 0) {
        apu->triangle.timer_val = apu->triangle.timer_period;
        if (apu->triangle.length_counter > 0 && apu->triangle.linear_counter > 0) {
            apu->triangle.seq_pos = (apu->triangle.seq_pos + 1) & 31;
        }
    } else {
        apu->triangle.timer_val--;
    }

    /* Clock noise timer */
    if (apu->noise.timer_val == 0) {
        apu->noise.timer_val = apu->noise.timer_period;
        uint8_t bit = apu->noise.mode ?
                      (apu->noise.shift_reg >> 6) & 1 :
                      (apu->noise.shift_reg >> 1) & 1;
        uint8_t feedback = (apu->noise.shift_reg & 1) ^ bit;
        apu->noise.shift_reg >>= 1;
        apu->noise.shift_reg |= (feedback << 14);
    } else {
        apu->noise.timer_val--;
    }

    /* Downsample to target rate */
    apu->sample_timer++;
    if (apu->sample_timer >= CYCLES_PER_SAMPLE) {
        apu->sample_timer -= CYCLES_PER_SAMPLE;
        if (apu->sample_count < NES_APU_BUF_SIZE) {
            apu->sample_buf[apu->sample_count++] = mix_output(apu);
        }
    }
}
