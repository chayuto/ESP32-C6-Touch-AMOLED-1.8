/*
 * sd_logger.c — SPIFFS logging + SD card export
 *
 * Architecture:
 *   - Continuous logging to SPIFFS (internal flash, no SPI conflict)
 *   - When display is OFF, SPI2 is free → export SPIFFS data to SD card
 *   - SD card uses SPI2 with different GPIO pins than display QSPI
 *   - Display GRAM holds last frame while SPI2 is temporarily reassigned
 *
 * Files:
 *   SPIFFS: /log/cry.csv, /log/metrics.csv
 *   SD:     /sdcard/cry.csv, /sdcard/metrics.csv (exported copies)
 */

#include "sd_logger.h"
#include "amoled.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "logger";

/* SPIFFS paths */
#define LOG_MOUNT    "/log"
#define CRY_LOG      LOG_MOUNT "/cry.csv"
#define METRICS_LOG  LOG_MOUNT "/metrics.csv"

/* SD card paths + pins */
#define SD_MOUNT     "/sdcard"
#define SD_MOSI      10
#define SD_MISO      18
#define SD_CLK       11
#define SD_CS        6

#define METRICS_HEADER "timestamp,uptime_s,rms,cry_ratio,noise_floor,threshold,periodicity," \
                       "state,cry_count,batt_mv,batt_pct,charging,usb_power," \
                       "free_heap,min_heap,wifi_rssi,brightness"

#ifndef CONFIG_LOG_INTERVAL_SEC
#define CONFIG_LOG_INTERVAL_SEC 300
#endif

static sd_state_t s_state = SD_STATE_NOT_PRESENT;
static int64_t s_last_metrics_log = 0;
static uint32_t s_metrics_count = 0;
static uint32_t s_cry_log_count = 0;
static uint32_t s_sd_export_count = 0;
#define METRICS_LOG_US  ((int64_t)CONFIG_LOG_INTERVAL_SEC * 1000000LL)

/* ── Helpers ─────────────────────────────────────────────── */

static void ensure_header(const char *path, const char *header)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%s\n", header);
            fclose(f);
        }
    }
}

/* Copy data rows from src to dst (appending). Skips header lines.
 * If dst doesn't exist, writes header first. Returns number of data rows copied. */
static int copy_data_rows(const char *src, const char *dst, const char *header)
{
    FILE *fin = fopen(src, "r");
    if (!fin) return 0;

    /* Check if dst needs a header */
    struct stat st;
    bool need_header = (stat(dst, &st) != 0 || st.st_size == 0);

    FILE *fout = fopen(dst, "a");
    if (!fout) { fclose(fin); return 0; }

    if (need_header && header) {
        fprintf(fout, "%s\n", header);
    }

    char buf[512];
    int rows = 0;
    while (fgets(buf, sizeof(buf), fin)) {
        /* Skip header lines (start with "timestamp" or are empty) */
        if (strncmp(buf, "timestamp", 9) == 0) continue;
        if (buf[0] == '\n' || buf[0] == '\r') continue;
        fputs(buf, fout);
        rows++;
    }

    fclose(fin);
    fclose(fout);
    ESP_LOGI(TAG, "Copied %d data rows: %s -> %s", rows, src, dst);
    return rows;
}

/* ── SPIFFS Init ─────────────────────────────────────────── */

void sd_logger_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = LOG_MOUNT,
        .partition_label = "storage",
        .max_files = 3,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        s_state = SD_STATE_NOT_PRESENT;
        return;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %uKB total, %uKB used, %uKB free",
             (unsigned)(total / 1024), (unsigned)(used / 1024),
             (unsigned)((total - used) / 1024));

    s_state = SD_STATE_MOUNTED;
    ensure_header(CRY_LOG, "timestamp,event_num,note");
    ensure_header(METRICS_LOG, METRICS_HEADER);
    s_last_metrics_log = esp_timer_get_time();
}

/* ── Cry Event Logging ───────────────────────────────────── */

void sd_logger_log_cry(uint32_t event_num, const char *timestamp)
{
    if (s_state != SD_STATE_MOUNTED) {
        ESP_LOGW(TAG, "Cry #%lu NOT logged — SPIFFS not mounted", (unsigned long)event_num);
        return;
    }

    FILE *f = fopen(CRY_LOG, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", CRY_LOG);
        return;
    }
    fprintf(f, "%s,%lu,cry_detected\n", timestamp ? timestamp : "unknown",
            (unsigned long)event_num);
    fclose(f);
    s_cry_log_count++;
    ESP_LOGW(TAG, "CRY #%lu logged to SPIFFS (%s)", (unsigned long)event_num, timestamp);
}

/* ── Periodic Metrics Logging ────────────────────────────── */

void sd_logger_log_metrics(const char *timestamp, float rms, float cry_ratio,
                           float noise_floor, float threshold, int periodicity,
                           const char *state, uint32_t cry_count,
                           uint16_t batt_mv, uint8_t batt_pct, bool charging, bool usb,
                           uint32_t free_heap, uint32_t min_heap, int wifi_rssi,
                           uint8_t brightness)
{
    if (s_state != SD_STATE_MOUNTED) return;

    int64_t now = esp_timer_get_time();
    if (now - s_last_metrics_log < METRICS_LOG_US) return;
    s_last_metrics_log = now;

    FILE *f = fopen(METRICS_LOG, "a");
    if (!f) return;

    uint32_t uptime_s = (uint32_t)(now / 1000000LL);
    fprintf(f, "%s,%lu,%.1f,%.4f,%.1f,%.1f,%d,%s,%lu,%u,%u,%d,%d,%lu,%lu,%d,%u\n",
            timestamp ? timestamp : "unknown",
            (unsigned long)uptime_s,
            rms, cry_ratio, noise_floor, threshold, periodicity,
            state ? state : "unknown",
            (unsigned long)cry_count,
            batt_mv, batt_pct, charging ? 1 : 0, usb ? 1 : 0,
            (unsigned long)free_heap, (unsigned long)min_heap,
            wifi_rssi, brightness);
    fclose(f);
    s_metrics_count++;
    ESP_LOGI(TAG, "Metrics #%lu (uptime=%lus batt=%umV/%u%%)",
             (unsigned long)s_metrics_count, (unsigned long)uptime_s, batt_mv, batt_pct);
}

/* ── SD Card Export (call ONLY when display is OFF) ──────── */

bool sd_logger_export_to_sd(void)
{
    ESP_LOGI(TAG, "=== SD export ===");

    /* 1. Init SPI2 with SD card pins (call before display init, SPI2 is free) */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI2 init for SD failed: %s", esp_err_to_name(ret));
        return false;
    }

    /* 3. Mount SD card */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_CS;
    slot_cfg.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 3,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot_cfg, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s (no card?)", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return false;
    }

    ESP_LOGI(TAG, "SD mounted: %s", card->cid.name);

    /* 4. Copy SPIFFS data rows to SD (skip headers, no duplicates) */
    int cry_rows = copy_data_rows(CRY_LOG, SD_MOUNT "/cry.csv",
                                   "timestamp,event_num,note");
    int met_rows = copy_data_rows(METRICS_LOG, SD_MOUNT "/metrics.csv",
                                   METRICS_HEADER);

    ESP_LOGI(TAG, "Exported %d cry rows, %d metrics rows to SD", cry_rows, met_rows);

    /* Only clear SPIFFS if we actually exported something */
    if (cry_rows > 0 || met_rows > 0) {
        FILE *f = fopen(CRY_LOG, "w");
        if (f) { fprintf(f, "timestamp,event_num,note\n"); fclose(f); }
        f = fopen(METRICS_LOG, "w");
        if (f) { fprintf(f, "%s\n", METRICS_HEADER); fclose(f); }
        ESP_LOGI(TAG, "SPIFFS logs cleared after export");
    }

    s_sd_export_count++;

    /* 5. Unmount SD + free SPI2 */
    esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    spi_bus_free(SPI2_HOST);

    bool exported = (cry_rows > 0 || met_rows > 0);
    ESP_LOGI(TAG, "=== SD export done (%d+%d rows), SPI2 free ===", cry_rows, met_rows);
    return exported;
}

sd_state_t sd_logger_get_state(void)
{
    return s_state;
}

void sd_logger_check(void)
{
    /* SPIFFS is always mounted — nothing to retry */
}

uint32_t sd_logger_get_metrics_count(void) { return s_metrics_count; }
uint32_t sd_logger_get_cry_count(void) { return s_cry_log_count; }
uint32_t sd_logger_get_sd_export_count(void) { return s_sd_export_count; }
