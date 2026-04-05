#pragma once

#include "esp_err.h"
#include "cry_detector.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SD_STATE_NOT_PRESENT = 0,
    SD_STATE_MOUNTED,
    SD_STATE_ERROR,
} sd_state_t;

/** Init SPIFFS logging. */
void sd_logger_init(void);

/**
 * Write a unified log row. Both periodic metrics and cry events use the same CSV.
 *   event = "periodic" | "cry"
 * Periodic rows are rate-limited by CONFIG_LOG_INTERVAL_SEC.
 * Cry rows are written immediately (no rate limit).
 */
void sd_logger_log(const char *event, const char *timestamp,
                   const cry_detector_status_t *status,
                   uint16_t batt_mv, uint8_t batt_pct, bool charging, bool usb,
                   uint32_t free_heap, uint32_t min_heap, int wifi_rssi,
                   uint8_t brightness);

/** Get current SD state. */
sd_state_t sd_logger_get_state(void);

/** Periodically retry mounting if card was absent. */
void sd_logger_check(void);

/** Get log counters */
uint32_t sd_logger_get_metrics_count(void);
uint32_t sd_logger_get_cry_count(void);
uint32_t sd_logger_get_sd_export_count(void);

/**
 * Export SPIFFS logs to SD card. ONLY call when display is OFF (SPI2 is free).
 * Returns true if export succeeded.
 */
bool sd_logger_export_to_sd(void);

#ifdef __cplusplus
}
#endif
