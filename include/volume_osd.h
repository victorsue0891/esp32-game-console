#pragma once

/**
 * Initialize the volume OSD bar overlay.
 * Must be called after ui_init().
 */
void volume_osd_init(void);

/**
 * Show the volume OSD and adjust volume up.
 */
void volume_osd_up(void);

/**
 * Show the volume OSD and adjust volume down.
 */
void volume_osd_down(void);

/**
 * Check and hide OSD after timeout. Call periodically.
 */
void volume_osd_tick(void);
