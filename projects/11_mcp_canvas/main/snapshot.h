#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize snapshot subsystem. Must be called after amoled_lvgl_init().
 */
void snapshot_init(void);

/**
 * Request a snapshot capture. Blocks until the LVGL task completes the capture
 * and JPEG encoding (up to 2s timeout).
 *
 * On success: allocates *b64_out (caller must free()), sets *b64_len_out, returns true.
 * On failure: returns false, *b64_out is NULL.
 */
bool snapshot_encode(unsigned char **b64_out, size_t *b64_len_out);

/**
 * Returns raw JPEG bytes into caller-supplied buffer.
 * out_buf must be at least 20480 bytes. Sets *jpeg_len on success.
 */
bool snapshot_encode_raw(uint8_t *out_buf, size_t out_cap, int *jpeg_len);

/**
 * Called from LVGL render timer to process pending snapshot requests.
 * If a snapshot was requested, this invalidates the screen, captures via
 * flush hook, JPEG encodes, and signals the waiting HTTP task.
 */
void snapshot_process(void);
