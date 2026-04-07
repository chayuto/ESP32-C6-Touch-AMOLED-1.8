#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SD_STATE_NOT_PRESENT = 0,
    SD_STATE_MOUNTED,
    SD_STATE_ERROR,
} sd_state_t;

/**
 * Init SPIFFS for packet metadata logging.
 */
void sd_logger_init(void);

/**
 * Log a single packet event row to SPIFFS CSV.
 *   direction: "rx", "relay", "drop", "expire"
 *   peer_id: 8-byte source peer ID (logged as hex)
 *   sender_id: 8-byte sender from outer header (logged as hex)
 *   rssi, ttl, size: packet metadata
 *   msg_type: BitChat message type from outer header
 *   flags: outer header flags byte
 *   has_recipient: whether packet has specific recipient
 *   is_broadcast: whether recipient is 0xFFFF...
 */
void sd_logger_log_packet(const char *direction, const uint8_t *peer_id,
                          const uint8_t *sender_id, int8_t rssi,
                          uint8_t ttl, uint16_t size, uint8_t msg_type,
                          uint8_t flags, bool has_recipient, bool is_broadcast);

/**
 * Log a periodic telemetry summary row to SPIFFS CSV.
 */
void sd_logger_log_summary(uint32_t uptime_s, uint32_t packets_rx,
                           uint32_t packets_tx, uint32_t packets_dropped,
                           uint8_t peer_count, uint32_t free_heap,
                           float bloom_fill_pct, uint16_t batt_mv,
                           uint8_t batt_pct);

/**
 * Export SPIFFS logs to SD card.
 * MUST call amoled_release_spi() before and amoled_reclaim_spi() after.
 * Returns number of rows exported, or -1 on failure.
 */
int sd_logger_export_to_sd(void);

/**
 * Get number of rows currently buffered in SPIFFS.
 */
uint32_t sd_logger_get_row_count(void);

sd_state_t sd_logger_get_state(void);
uint32_t sd_logger_get_export_count(void);

#ifdef __cplusplus
}
#endif
