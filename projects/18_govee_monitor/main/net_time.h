#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * WiFi association and opportunistic wall-clock sync.
 *
 * Readings are never stamped with wall-clock time as they are captured — they
 * carry uptime, and are converted on the way out (see net_time_epoch_ms_at).
 * That is what makes the sync "opportunistic": a clock that arrives ten
 * minutes after boot still back-dates every bucket collected before it,
 * because nothing was committed to a timestamp in the meantime.
 *
 * Three clock sources, in increasing order of trust:
 *   NONE  no idea what time it is — uploads must wait
 *   RTC   PCF85063 read at boot; survives power loss, drifts
 *   NTP   authoritative; also written back to the RTC so the next boot
 *         starts from RTC rather than NONE
 *
 * Everything here degrades rather than fails: no credentials configured, no
 * AP, or no internet all leave the monitor collecting exactly as before.
 */

typedef enum {
    CLOCK_SRC_NONE = 0,
    CLOCK_SRC_RTC,
    CLOCK_SRC_NTP,
} clock_src_t;

/* Brings up the RTC, then WiFi + SNTP if credentials are configured.
 * Never blocks: association and sync happen on their own event handlers. */
void net_time_init(void);

bool        net_time_wifi_up(void);
clock_src_t net_time_clock_src(void);

/* Wall-clock epoch milliseconds for an esp_timer timestamp, or 0 when the
 * clock is still CLOCK_SRC_NONE. Pass the uptime recorded when the reading
 * was captured, not the current uptime. */
int64_t net_time_epoch_ms_at(int64_t uptime_us);

/* Seconds since the last successful NTP sync, or -1 if never. For the health
 * row: a clock that stopped syncing days ago is worth seeing. */
int64_t net_time_since_sync_s(void);
