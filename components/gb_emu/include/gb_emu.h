#pragma once

/**
 * Game Boy Emulator Core - Public Interface
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define GB_WIDTH    160
#define GB_HEIGHT   144
#define GB_FPS      60

#define GB_AUDIO_SAMPLE_RATE 22050
#define GB_AUDIO_SAMPLES_PER_FRAME (GB_AUDIO_SAMPLE_RATE / GB_FPS)

/* Joypad bits */
#define GB_BTN_A      0x01
#define GB_BTN_B      0x02
#define GB_BTN_SELECT 0x04
#define GB_BTN_START  0x08
#define GB_BTN_RIGHT  0x10
#define GB_BTN_LEFT   0x20
#define GB_BTN_UP     0x40
#define GB_BTN_DOWN   0x80

bool gb_init(const uint8_t *rom_data, size_t rom_size);
void gb_run_frame(void);
void gb_set_joypad(uint8_t buttons);
const uint16_t *gb_get_framebuffer(void);
const int16_t *gb_get_audio(int *out_count);
void gb_shutdown(void);

/* ---- Save State API ---- */

/** Return the number of bytes required for a full state snapshot. */
size_t gb_get_state_size(void);

/**
 * Serialise the complete emulator state into @p buf.
 * @p buf must be at least gb_get_state_size() bytes.
 */
void gb_save_state(void *buf);

/**
 * Restore the emulator state from a previously saved snapshot.
 * @return true if the buffer looks valid (magic check).
 */
bool gb_load_state(const void *buf);
