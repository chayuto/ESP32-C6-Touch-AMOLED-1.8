#pragma once

#include <stdbool.h>

/* BOOT-button (GPIO 9, active low) toggles a power-save mode that:
 *   - turns the AMOLED display off (SH8601 sleep cmd 0x28),
 *   - pauses the BLE scanner (NimBLE host stays up; scan re-arms instantly),
 *   - disables touch input so stray touches don't wake anything,
 *   - stops the LVGL refresh timer (no work while screen is dark).
 *
 * Press again → reverse all four; first packet from any nearby H5075
 * repaints the tile within 1–3 s (Govee's advertisement interval). */

void power_save_init(void);
void power_save_poll(void);     /* call from the LVGL task at ~10 ms cadence */
void power_save_toggle(void);   /* manual trigger */
bool power_save_is_active(void);
