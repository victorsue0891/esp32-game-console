#pragma once

/**
 * NES Emulator Core - Public Interface
 * Integrates 6502 CPU, PPU, APU, and Cartridge subsystems.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* NES display constants */
#define NES_WIDTH   256
#define NES_HEIGHT  240
#define NES_FPS     60

/* NES audio constants */
#define NES_AUDIO_SAMPLE_RATE  22050
#define NES_AUDIO_SAMPLES_PER_FRAME (NES_AUDIO_SAMPLE_RATE / NES_FPS)

/* Joypad button bitmask (matches standard NES controller) */
#define NES_BTN_A       0x01
#define NES_BTN_B       0x02
#define NES_BTN_SELECT  0x04
#define NES_BTN_START   0x08
#define NES_BTN_UP      0x10
#define NES_BTN_DOWN    0x20
#define NES_BTN_LEFT    0x40
#define NES_BTN_RIGHT   0x80

/**
 * Initialize the NES emulator and load a ROM.
 * @param rom_data   Pointer to the raw .nes ROM data.
 * @param rom_size   Size of the ROM data in bytes.
 * @return true on success.
 */
bool nes_init(const uint8_t *rom_data, size_t rom_size);

/**
 * Run one full frame of NES emulation (~29780 CPU cycles).
 * After this call, the framebuffer and audio buffer are ready.
 */
void nes_run_frame(void);

/**
 * Set joypad 1 button state.
 * @param buttons  Bitmask of NES_BTN_* values.
 */
void nes_set_joypad(uint8_t buttons);

/**
 * Get pointer to the RGB565 framebuffer (NES_WIDTH * NES_HEIGHT).
 */
const uint16_t *nes_get_framebuffer(void);

/**
 * Get pointer to this frame's audio samples (signed 16-bit PCM, mono).
 * @param out_count  Receives the number of samples.
 */
const int16_t *nes_get_audio(int *out_count);

/**
 * Shut down the NES emulator and free resources.
 */
void nes_shutdown(void);
