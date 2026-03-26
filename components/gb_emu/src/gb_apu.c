/**
 * Game Boy APU (Sound Controller) - Simplified implementation
 * 4 channels: Pulse+Sweep, Pulse, Wave, Noise
 */

#include "gb_apu.h"
#include <string.h>

#define GB_CPU_CLOCK   4194304
#define SAMPLE_RATE    22050
#define CYCLES_PER_SAMPLE (GB_CPU_CLOCK / SAMPLE_RATE)

static const uint8_t duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 0},
};

static const uint8_t divisor_table[8] = {8, 16, 32, 48, 64, 80, 96, 112};

void gb_apu_init(gb_apu_t *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->ch4.lfsr = 0x7FFF;
    apu->nr52 = 0x80;
}

void gb_apu_reset(gb_apu_t *apu) {
    gb_apu_init(apu);
}

void gb_apu_write(gb_apu_t *apu, uint16_t addr, uint8_t val) {
    if (!(apu->nr52 & 0x80) && addr != 0xFF26) return;

    switch (addr) {
    /* Channel 1: Pulse + Sweep */
    case 0xFF10:
        apu->ch1.sweep_period = (val >> 4) & 7;
        apu->ch1.sweep_negate = (val >> 3) & 1;
        apu->ch1.sweep_shift = val & 7;
        break;
    case 0xFF11:
        apu->ch1.duty = (val >> 6) & 3;
        apu->ch1.length_load = val & 0x3F;
        apu->ch1.length_counter = 64 - apu->ch1.length_load;
        break;
    case 0xFF12:
        apu->ch1.env_init = (val >> 4) & 0xF;
        apu->ch1.env_dir = (val >> 3) & 1;
        apu->ch1.env_period = val & 7;
        break;
    case 0xFF13:
        apu->ch1.freq = (apu->ch1.freq & 0x700) | val;
        break;
    case 0xFF14:
        apu->ch1.freq = (apu->ch1.freq & 0xFF) | ((val & 7) << 8);
        apu->ch1.length_enable = (val >> 6) & 1;
        if (val & 0x80) { /* Trigger */
            apu->ch1.enabled = true;
            apu->ch1.volume = apu->ch1.env_init;
            apu->ch1.env_timer = apu->ch1.env_period;
            apu->ch1.timer = (2048 - apu->ch1.freq) * 4;
            apu->ch1.sweep_shadow = apu->ch1.freq;
            apu->ch1.sweep_timer = apu->ch1.sweep_period;
            apu->ch1.sweep_enable = (apu->ch1.sweep_period > 0 || apu->ch1.sweep_shift > 0);
            if (apu->ch1.length_counter == 0) apu->ch1.length_counter = 64;
        }
        break;

    /* Channel 2: Pulse */
    case 0xFF16:
        apu->ch2.duty = (val >> 6) & 3;
        apu->ch2.length_load = val & 0x3F;
        apu->ch2.length_counter = 64 - apu->ch2.length_load;
        break;
    case 0xFF17:
        apu->ch2.env_init = (val >> 4) & 0xF;
        apu->ch2.env_dir = (val >> 3) & 1;
        apu->ch2.env_period = val & 7;
        break;
    case 0xFF18:
        apu->ch2.freq = (apu->ch2.freq & 0x700) | val;
        break;
    case 0xFF19:
        apu->ch2.freq = (apu->ch2.freq & 0xFF) | ((val & 7) << 8);
        apu->ch2.length_enable = (val >> 6) & 1;
        if (val & 0x80) {
            apu->ch2.enabled = true;
            apu->ch2.volume = apu->ch2.env_init;
            apu->ch2.env_timer = apu->ch2.env_period;
            apu->ch2.timer = (2048 - apu->ch2.freq) * 4;
            if (apu->ch2.length_counter == 0) apu->ch2.length_counter = 64;
        }
        break;

    /* Channel 3: Wave */
    case 0xFF1A:
        apu->ch3.dac_enable = (val >> 7) & 1;
        if (!apu->ch3.dac_enable) apu->ch3.enabled = false;
        break;
    case 0xFF1B:
        apu->ch3.length_load = val;
        apu->ch3.length_counter = 256 - val;
        break;
    case 0xFF1C:
        apu->ch3.volume_code = (val >> 5) & 3;
        break;
    case 0xFF1D:
        apu->ch3.freq = (apu->ch3.freq & 0x700) | val;
        break;
    case 0xFF1E:
        apu->ch3.freq = (apu->ch3.freq & 0xFF) | ((val & 7) << 8);
        apu->ch3.length_enable = (val >> 6) & 1;
        if (val & 0x80) {
            apu->ch3.enabled = true;
            apu->ch3.timer = (2048 - apu->ch3.freq) * 2;
            apu->ch3.position = 0;
            if (apu->ch3.length_counter == 0) apu->ch3.length_counter = 256;
        }
        break;

    /* Wave RAM */
    case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
    case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
    case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
    case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
        apu->ch3.wave_ram[addr - 0xFF30] = val;
        break;

    /* Channel 4: Noise */
    case 0xFF20:
        apu->ch4.length_load = val & 0x3F;
        apu->ch4.length_counter = 64 - apu->ch4.length_load;
        break;
    case 0xFF21:
        apu->ch4.env_init = (val >> 4) & 0xF;
        apu->ch4.env_dir = (val >> 3) & 1;
        apu->ch4.env_period = val & 7;
        break;
    case 0xFF22:
        apu->ch4.clock_shift = (val >> 4) & 0xF;
        apu->ch4.width_mode = (val >> 3) & 1;
        apu->ch4.divisor_code = val & 7;
        break;
    case 0xFF23:
        apu->ch4.length_enable = (val >> 6) & 1;
        if (val & 0x80) {
            apu->ch4.enabled = true;
            apu->ch4.volume = apu->ch4.env_init;
            apu->ch4.env_timer = apu->ch4.env_period;
            apu->ch4.lfsr = 0x7FFF;
            apu->ch4.timer = divisor_table[apu->ch4.divisor_code] << apu->ch4.clock_shift;
            if (apu->ch4.length_counter == 0) apu->ch4.length_counter = 64;
        }
        break;

    /* Master control */
    case 0xFF24: apu->nr50 = val; break;
    case 0xFF25: apu->nr51 = val; break;
    case 0xFF26:
        apu->nr52 = (apu->nr52 & 0x0F) | (val & 0x80);
        if (!(val & 0x80)) {
            apu->ch1.enabled = false;
            apu->ch2.enabled = false;
            apu->ch3.enabled = false;
            apu->ch4.enabled = false;
        }
        break;
    }
}

uint8_t gb_apu_read(gb_apu_t *apu, uint16_t addr) {
    switch (addr) {
    case 0xFF10: return 0x80 | (apu->ch1.sweep_period << 4) | (apu->ch1.sweep_negate << 3) | apu->ch1.sweep_shift;
    case 0xFF11: return (apu->ch1.duty << 6) | 0x3F;
    case 0xFF12: return (apu->ch1.env_init << 4) | (apu->ch1.env_dir << 3) | apu->ch1.env_period;
    case 0xFF14: return (apu->ch1.length_enable << 6) | 0xBF;
    case 0xFF16: return (apu->ch2.duty << 6) | 0x3F;
    case 0xFF17: return (apu->ch2.env_init << 4) | (apu->ch2.env_dir << 3) | apu->ch2.env_period;
    case 0xFF19: return (apu->ch2.length_enable << 6) | 0xBF;
    case 0xFF1A: return (apu->ch3.dac_enable << 7) | 0x7F;
    case 0xFF1C: return (apu->ch3.volume_code << 5) | 0x9F;
    case 0xFF1E: return (apu->ch3.length_enable << 6) | 0xBF;
    case 0xFF21: return (apu->ch4.env_init << 4) | (apu->ch4.env_dir << 3) | apu->ch4.env_period;
    case 0xFF22: return (apu->ch4.clock_shift << 4) | (apu->ch4.width_mode << 3) | apu->ch4.divisor_code;
    case 0xFF23: return (apu->ch4.length_enable << 6) | 0xBF;
    case 0xFF24: return apu->nr50;
    case 0xFF25: return apu->nr51;
    case 0xFF26:
        return (apu->nr52 & 0x80) | 0x70 |
               (apu->ch1.enabled ? 1 : 0) | (apu->ch2.enabled ? 2 : 0) |
               (apu->ch3.enabled ? 4 : 0) | (apu->ch4.enabled ? 8 : 0);
    case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
    case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
    case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
    case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
        return apu->ch3.wave_ram[addr - 0xFF30];
    default:
        return 0xFF;
    }
}

static int16_t mix_sample(gb_apu_t *apu) {
    if (!(apu->nr52 & 0x80)) return 0;

    int ch1_out = 0, ch2_out = 0, ch3_out = 0, ch4_out = 0;

    /* Channel 1 */
    if (apu->ch1.enabled) {
        ch1_out = duty_table[apu->ch1.duty][apu->ch1.duty_pos] ? apu->ch1.volume : 0;
    }
    /* Channel 2 */
    if (apu->ch2.enabled) {
        ch2_out = duty_table[apu->ch2.duty][apu->ch2.duty_pos] ? apu->ch2.volume : 0;
    }
    /* Channel 3 */
    if (apu->ch3.enabled && apu->ch3.dac_enable) {
        uint8_t sample = apu->ch3.wave_ram[apu->ch3.position / 2];
        if (apu->ch3.position & 1) sample &= 0xF; else sample >>= 4;
        static const uint8_t vol_shift[] = {4, 0, 1, 2};
        ch3_out = sample >> vol_shift[apu->ch3.volume_code];
    }
    /* Channel 4 */
    if (apu->ch4.enabled) {
        ch4_out = (apu->ch4.lfsr & 1) ? 0 : apu->ch4.volume;
    }

    /* Simple mix */
    int left = 0, right = 0;
    uint8_t vol_l = ((apu->nr50 >> 4) & 7) + 1;
    uint8_t vol_r = (apu->nr50 & 7) + 1;

    if (apu->nr51 & 0x10) left += ch1_out;
    if (apu->nr51 & 0x20) left += ch2_out;
    if (apu->nr51 & 0x40) left += ch3_out;
    if (apu->nr51 & 0x80) left += ch4_out;
    if (apu->nr51 & 0x01) right += ch1_out;
    if (apu->nr51 & 0x02) right += ch2_out;
    if (apu->nr51 & 0x04) right += ch3_out;
    if (apu->nr51 & 0x08) right += ch4_out;

    int mono = ((left * vol_l) + (right * vol_r)) / 2;
    return (int16_t)(mono * 400);
}

void gb_apu_tick(gb_apu_t *apu, int cycles) {
    if (!(apu->nr52 & 0x80)) return;

    for (int t = 0; t < cycles; t++) {
        apu->cycle_count++;

        /* Clock channels */
        /* Channel 1 timer */
        if (apu->ch1.timer > 0) apu->ch1.timer--;
        if (apu->ch1.timer == 0) {
            apu->ch1.timer = (2048 - apu->ch1.freq) * 4;
            apu->ch1.duty_pos = (apu->ch1.duty_pos + 1) & 7;
        }
        /* Channel 2 timer */
        if (apu->ch2.timer > 0) apu->ch2.timer--;
        if (apu->ch2.timer == 0) {
            apu->ch2.timer = (2048 - apu->ch2.freq) * 4;
            apu->ch2.duty_pos = (apu->ch2.duty_pos + 1) & 7;
        }
        /* Channel 3 timer */
        if (apu->ch3.timer > 0) apu->ch3.timer--;
        if (apu->ch3.timer == 0) {
            apu->ch3.timer = (2048 - apu->ch3.freq) * 2;
            apu->ch3.position = (apu->ch3.position + 1) & 31;
        }
        /* Channel 4 timer */
        if (apu->ch4.timer > 0) apu->ch4.timer--;
        if (apu->ch4.timer == 0) {
            apu->ch4.timer = divisor_table[apu->ch4.divisor_code] << apu->ch4.clock_shift;
            uint8_t xor_bit = (apu->ch4.lfsr & 1) ^ ((apu->ch4.lfsr >> 1) & 1);
            apu->ch4.lfsr >>= 1;
            apu->ch4.lfsr |= (xor_bit << 14);
            if (apu->ch4.width_mode) {
                apu->ch4.lfsr &= ~(1 << 6);
                apu->ch4.lfsr |= (xor_bit << 6);
            }
        }

        /* Frame sequencer (512 Hz = every 8192 T-cycles) */
        apu->frame_seq_counter++;
        if (apu->frame_seq_counter >= 8192) {
            apu->frame_seq_counter = 0;

            /* Length counter (steps 0, 2, 4, 6) */
            if ((apu->frame_seq_step & 1) == 0) {
                if (apu->ch1.length_enable && apu->ch1.length_counter > 0) {
                    apu->ch1.length_counter--;
                    if (apu->ch1.length_counter == 0) apu->ch1.enabled = false;
                }
                if (apu->ch2.length_enable && apu->ch2.length_counter > 0) {
                    apu->ch2.length_counter--;
                    if (apu->ch2.length_counter == 0) apu->ch2.enabled = false;
                }
                if (apu->ch3.length_enable && apu->ch3.length_counter > 0) {
                    apu->ch3.length_counter--;
                    if (apu->ch3.length_counter == 0) apu->ch3.enabled = false;
                }
                if (apu->ch4.length_enable && apu->ch4.length_counter > 0) {
                    apu->ch4.length_counter--;
                    if (apu->ch4.length_counter == 0) apu->ch4.enabled = false;
                }
            }

            /* Sweep (step 2, 6) */
            if (apu->frame_seq_step == 2 || apu->frame_seq_step == 6) {
                if (apu->ch1.sweep_enable && apu->ch1.sweep_period > 0) {
                    apu->ch1.sweep_timer--;
                    if (apu->ch1.sweep_timer == 0) {
                        apu->ch1.sweep_timer = apu->ch1.sweep_period;
                        uint16_t new_freq = apu->ch1.sweep_shadow >> apu->ch1.sweep_shift;
                        if (apu->ch1.sweep_negate) new_freq = apu->ch1.sweep_shadow - new_freq;
                        else new_freq = apu->ch1.sweep_shadow + new_freq;
                        if (new_freq < 2048 && apu->ch1.sweep_shift > 0) {
                            apu->ch1.freq = new_freq;
                            apu->ch1.sweep_shadow = new_freq;
                        }
                        if (new_freq >= 2048) apu->ch1.enabled = false;
                    }
                }
            }

            /* Envelope (step 7) */
            if (apu->frame_seq_step == 7) {
                /* Ch1 envelope */
                if (apu->ch1.env_period > 0) {
                    if (apu->ch1.env_timer > 0) apu->ch1.env_timer--;
                    if (apu->ch1.env_timer == 0) {
                        apu->ch1.env_timer = apu->ch1.env_period;
                        if (apu->ch1.env_dir && apu->ch1.volume < 15) apu->ch1.volume++;
                        else if (!apu->ch1.env_dir && apu->ch1.volume > 0) apu->ch1.volume--;
                    }
                }
                /* Ch2 envelope */
                if (apu->ch2.env_period > 0) {
                    if (apu->ch2.env_timer > 0) apu->ch2.env_timer--;
                    if (apu->ch2.env_timer == 0) {
                        apu->ch2.env_timer = apu->ch2.env_period;
                        if (apu->ch2.env_dir && apu->ch2.volume < 15) apu->ch2.volume++;
                        else if (!apu->ch2.env_dir && apu->ch2.volume > 0) apu->ch2.volume--;
                    }
                }
                /* Ch4 envelope */
                if (apu->ch4.env_period > 0) {
                    if (apu->ch4.env_timer > 0) apu->ch4.env_timer--;
                    if (apu->ch4.env_timer == 0) {
                        apu->ch4.env_timer = apu->ch4.env_period;
                        if (apu->ch4.env_dir && apu->ch4.volume < 15) apu->ch4.volume++;
                        else if (!apu->ch4.env_dir && apu->ch4.volume > 0) apu->ch4.volume--;
                    }
                }
            }

            apu->frame_seq_step = (apu->frame_seq_step + 1) & 7;
        }

        /* Downsample */
        apu->sample_timer++;
        if (apu->sample_timer >= CYCLES_PER_SAMPLE) {
            apu->sample_timer -= CYCLES_PER_SAMPLE;
            if (apu->sample_count < GB_APU_BUF_SIZE) {
                apu->sample_buf[apu->sample_count++] = mix_sample(apu);
            }
        }
    }
}
