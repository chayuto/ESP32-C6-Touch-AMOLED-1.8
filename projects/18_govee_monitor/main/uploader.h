#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * Ships closed history buckets to Supabase.
 *
 * Collection never waits on this. The uploader runs in its own task, and if
 * WiFi, the clock, or the server are unavailable it simply does not advance
 * its watermark — the readings stay in the history ring and go out later.
 *
 * There is no queue: the watermark is an index into history that already
 * exists, so an outage costs no extra memory and the tolerance is exactly the
 * tier's depth (120 x 3 min = 6 hours). Beyond that the oldest buckets roll
 * off unsent, which is the documented limit until NVS persistence lands.
 *
 * Retries are safe because ids are deterministic — a replayed row is provably
 * the same row, so the server's duplicate rejection (409 / 23505) is treated
 * as success rather than an error.
 */

void uploader_start(void);

/* Health, for the heartbeat line. */
bool     uploader_configured(void);
uint32_t uploader_rows_sent(void);
uint32_t uploader_rows_duplicate(void);
uint32_t uploader_failures(void);
int64_t  uploader_since_success_s(void);   /* -1 if never */
uint16_t uploader_backlog(void);           /* buckets waiting to go out */
