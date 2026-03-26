#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Emulator type */
typedef enum {
    EMU_TYPE_NONE = 0,
    EMU_TYPE_NES,
    EMU_TYPE_GB,
} emu_type_t;

/**
 * Detect emulator type from ROM file extension.
 */
emu_type_t emulator_detect_type(const char *rom_path);

/**
 * Load a ROM and start the emulator on Core 1.
 * @param rom_path  Full path to the ROM file on SD card.
 * @return true if started successfully.
 */
bool emulator_start(const char *rom_path);

/**
 * Stop the running emulator and return to the menu.
 */
void emulator_stop(void);

/**
 * Check if an emulator is currently running.
 */
bool emulator_is_running(void);

/**
 * Check if emulator is paused.
 */
bool emulator_is_paused(void);

/**
 * Pause the running emulator (keeps state, suspends frame loop).
 */
void emulator_pause(void);

/**
 * Resume a paused emulator.
 */
void emulator_resume(void);

/**
 * Feed button state to the emulator.
 * @param button_mask  Bitmask from button_get_state().
 */
void emulator_set_input(uint16_t button_mask);

/**
 * Get the ROM path currently loaded (if running).
 */
const char *emulator_get_rom_path(void);

/**
 * Get the current emulator type.
 */
emu_type_t emulator_get_type(void);

/**
 * Save emulator state to SD card.
 * Must be called while emulator is paused.
 * Saves to /sdcard/saves/<rom_basename>.sav
 * @return true on success.
 */
bool emulator_save_state(void);

/**
 * Load emulator state from SD card.
 * Must be called while emulator is paused.
 * @return true on success.
 */
bool emulator_load_state(void);

/**
 * Check if a save state exists for current ROM.
 */
bool emulator_has_save_state(void);
