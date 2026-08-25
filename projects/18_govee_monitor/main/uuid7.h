#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * Deterministic UUIDv7 (RFC 9562 layout).
 *
 * A datapoint's id is derived from (epoch_ms, device_id, sensor MAC) rather
 * than from randomness. That single choice is what makes the whole upload path
 * safe to retry: re-sending a bucket produces byte-identical ids, so the
 * server-side insert can be "on conflict do nothing" and every retry, replay
 * and overlapping export is idempotent by construction. No dedupe logic, no
 * delivery bookkeeping, and no need to store ids on the device at all.
 *
 * RFC 9562 specifies random rand_a/rand_b; here they are a hash of the same
 * inputs, which is a deliberate deviation. Layout, version and variant bits
 * are conformant, and the time-ordered prefix still sorts correctly — the
 * device_id salt is what keeps two boards from colliding on a shared sensor.
 *
 * Keyed on the sensor's MAC, not its slot index: slots are a display concern
 * and shift whenever device_config.h is reordered, whereas the MAC is the
 * datapoint's real identity and must keep producing the same ids forever.
 */

#define UUID7_STR_LEN 37   /* 36 chars + NUL */

/* Writes a lowercase hyphenated UUIDv7 into out[UUID7_STR_LEN]. */
void uuid7_deterministic(int64_t epoch_ms, const char *device_id,
                         const uint8_t mac[6], char *out);

/* Milliseconds encoded in the leading 48 bits — for tests and diagnostics. */
int64_t uuid7_extract_ms(const char *uuid);
