#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the activity tracker. Call after LVGL is up. */
void power_manager_init(void);

/** 1 Hz tick: apply brightness ramp, screen-off, and amp-off based on
 *  LVGL's inactive-time and the IMU shake flag. */
void power_manager_tick(void);

/** Force a return to active state (full brightness, display on, amp on).
 *  Called from any explicit input pathway not covered by lv_disp_get_inactive_time. */
void power_manager_notify_activity(void);

/** True if the display is currently powered down due to idle. */
bool power_manager_is_screen_off(void);

/** Save the pet, then cut all AXP2101 rails. The board comes back via the
 *  PWR button. Used for the "Power off" action on the sleep screen. */
void power_manager_power_off(void);

#ifdef __cplusplus
}
#endif
