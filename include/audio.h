#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Initialize I2S driver for MAX98357 DAC.
 */
esp_err_t audio_init(void);

/**
 * Write PCM samples to the I2S output buffer.
 * @param samples  Pointer to signed 16-bit PCM samples.
 * @param count    Number of samples.
 */
void audio_write(const int16_t *samples, size_t count);

/**
 * Set master volume (0-100).
 */
void audio_set_volume(int vol);

/**
 * Get current master volume (0-100).
 */
int audio_get_volume(void);

/**
 * Mute/unmute audio.
 */
void audio_set_mute(bool mute);

/**
 * Return the current A/V sync credit (0–4).
 * Values > 2 mean audio is running ahead of video; the emulator may delay
 * video slightly to keep A/V aligned.
 */
int audio_get_av_credit(void);

/**
 * Reset A/V sync credit to the neutral value (2).
 * Must be called at the start of each emulator session so stale credit
 * from a previous session does not bias the new one.
 */
void audio_reset_av_credit(void);

/**
 * De-initialize audio driver.
 */
void audio_deinit(void);
