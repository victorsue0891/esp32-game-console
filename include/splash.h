#pragma once

#include <stdbool.h>

/**
 * Show the boot splash screen from /system/boot.jpg on SD card.
 * Blocks until the Start button is pressed.
 */
void splash_show(void);

/**
 * Returns true after the splash screen has been dismissed.
 */
bool splash_is_done(void);
