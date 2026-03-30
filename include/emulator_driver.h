#pragma once

/**
 * emulator_driver_t — vtable interface for emulator backends.
 *
 * Adding a new system requires:
 *   1. Implement all function pointers below.
 *   2. Declare a const emulator_driver_t instance in your component.
 *   3. Add a pointer to it in the drivers[] array in emulator.c.
 *   No other file needs to change.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    /* Human-readable name, e.g. "NES" */
    const char *name;

    /* File extension handled by this driver, e.g. ".nes" (lower-case) */
    const char *ext;

    /* Native framebuffer dimensions */
    int fb_width;
    int fb_height;

    /**
     * Load ROM data and initialise hardware state.
     * @return true on success.
     */
    bool (*init)(const uint8_t *rom_data, size_t rom_size);

    /** Execute one full video frame (~1/60 s of emulated time). */
    void (*run_frame)(void);

    /**
     * Feed button state.  The driver is responsible for mapping the
     * generic 16-bit bitmask (button_id_t) to its own joypad format.
     */
    void (*set_input)(uint16_t buttons);

    /** Return pointer to the native RGB565 framebuffer after run_frame(). */
    const uint16_t *(*get_framebuffer)(void);

    /**
     * Return pointer to this frame's PCM audio (signed 16-bit mono).
     * @param out_count  Receives sample count.
     */
    const int16_t *(*get_audio)(int *out_count);

    /**
     * Return the number of bytes required by save_state().
     * Must be constant for a given driver.
     */
    size_t (*get_state_size)(void);

    /**
     * Serialise complete emulator state into @p buf.
     * @p buf must be at least get_state_size() bytes.
     */
    void (*save_state)(void *buf);

    /**
     * Restore emulator state from @p buf.
     * @return true if the buffer appears valid.
     */
    bool (*load_state)(const void *buf);

    /** Shut down emulator and free all resources. */
    void (*shutdown)(void);
} emulator_driver_t;
