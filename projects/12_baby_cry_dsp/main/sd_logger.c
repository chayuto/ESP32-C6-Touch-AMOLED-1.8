/*
 * sd_logger.c — Unified SPIFFS logging + SD card export
 *
 * Single CSV file with an "event" column:
 *   "periodic" — rate-limited snapshot every CONFIG_LOG_INTERVAL_SEC
 *   "cry"      — immediate snapshot at each cry detection trigger
 *
 * Same columns for both event types → easy filtering, no schema mismatch.
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
#define LOG_FILE     LOG_MOUNT "/log.csv"

/* SD card paths + pins */
#define SD_MOUNT     "/sdcard"
#define SD_MOSI      10
#define SD_MISO      18
#define SD_CLK       11
#define SD_CS        6

#define LOG_HEADER \
    "timestamp,event,uptime_s,rms,cry_ratio,noise_floor,threshold,periodicity," \
    "state,cry_count,score,low_ratio,high_ratio,crest,harmonic_pct," \
    "f0_hz,cry_dominant,gated,pos_streak,neg_streak," \
    "f0_variance,voiced_ratio,max_consec_f0," \
    "batt_mv,batt_pct,charging,usb_power," \
    "free_heap,min_heap,wifi_rssi,brightness"

#ifndef CONFIG_LOG_INTERVAL_SEC
#define CONFIG_LOG_INTERVAL_SEC 300
#endif

static sd_state_t s_state = SD_STATE_NOT_PRESENT;
static int64_t s_last_periodic_log = 0;
static uint32_t s_metrics_count = 0;
static uint32_t s_cry_log_count = 0;
static uint32_t s_sd_export_count = 0;
#define PERIODIC_LOG_US  ((int64_t)CONFIG_LOG_INTERVAL_SEC * 1000000LL)

/* ── Helpers ──────────────────────────────────��──────────── */

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

/* Write one unified CSV row */
static void write_log_row(const char *event, const char *timestamp,
                          const cry_detector_status_t *s,
                          uint16_t batt_mv, uint8_t batt_pct, bool charging, bool usb,
                          uint32_t free_heap, uint32_t min_heap, int wifi_rssi,
                          uint8_t brightness)
{
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;

    int64_t now = esp_timer_get_time();
    uint32_t uptime_s = (uint32_t)(now / 1000000LL);
    const char *state_str = (s->state == CRY_STATE_DETECTED) ? "crying" : "idle";

    fprintf(f, "%s,%s,%lu,%.1f,%.4f,%.1f,%.1f,%d,"
               "%s,%lu,%d,%.4f,%.4f,%.1f,%d,"
               "%d,%d,%d,%d,%d,"
               "%.2f,%.2f,%d,"
               "%u,%u,%d,%d,"
               "%lu,%lu,%d,%u\n",
            timestamp ? timestamp : "unknown",
            event,
            (unsigned long)uptime_s,
            s->rms_energy, s->cry_band_ratio, s->noise_floor, s->threshold, s->periodicity,
            state_str,
            (unsigned long)s->cry_count,
            s->score, s->low_ratio, s->high_ratio, s->crest, s->harmonic_pct,
            s->f0_hz, s->cry_dominant ? 1 : 0, s->gated ? 1 : 0,
            s->pos_streak, s->neg_streak,
            s->f0_variance, s->voiced_ratio, s->max_consec_f0,
            batt_mv, batt_pct, charging ? 1 : 0, usb ? 1 : 0,
            (unsigned long)free_heap, (unsigned long)min_heap,
            wifi_rssi, brightness);
    fclose(f);
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
    ensure_header(LOG_FILE, LOG_HEADER);
    s_last_periodic_log = esp_timer_get_time();
}

/* ── Unified Logging ─────────────────────────────────────── */

void sd_logger_log(const char *event, const char *timestamp,
                   const cry_detector_status_t *status,
                   uint16_t batt_mv, uint8_t batt_pct, bool charging, bool usb,
                   uint32_t free_heap, uint32_t min_heap, int wifi_rssi,
                   uint8_t brightness)
{
    if (s_state != SD_STATE_MOUNTED) return;

    bool is_cry = (strcmp(event, "cry") == 0);

    if (!is_cry) {
        /* Rate-limit periodic logs */
        int64_t now = esp_timer_get_time();
        if (now - s_last_periodic_log < PERIODIC_LOG_US) return;
        s_last_periodic_log = now;
    }

    write_log_row(event, timestamp, status,
                  batt_mv, batt_pct, charging, usb,
                  free_heap, min_heap, wifi_rssi, brightness);

    if (is_cry) {
        s_cry_log_count++;
        ESP_LOGW(TAG, "CRY #%lu logged (%s)",
                 (unsigned long)status->cry_count, timestamp);
    } else {
        s_metrics_count++;
        int64_t now = esp_timer_get_time();
        uint32_t uptime_s = (uint32_t)(now / 1000000LL);
        ESP_LOGI(TAG, "Metrics #%lu (uptime=%lus batt=%umV/%u%%)",
                 (unsigned long)s_metrics_count, (unsigned long)uptime_s,
                 batt_mv, batt_pct);
    }
}

/* ── SD Card Export (call ONLY when display is OFF) ──────── */

bool sd_logger_export_to_sd(void)
{
    ESP_LOGI(TAG, "=== SD export ===");

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

    int rows = copy_data_rows(LOG_FILE, SD_MOUNT "/log.csv", LOG_HEADER);
    ESP_LOGI(TAG, "Exported %d rows to SD", rows);

    if (rows > 0) {
        FILE *f = fopen(LOG_FILE, "w");
        if (f) { fprintf(f, "%s\n", LOG_HEADER); fclose(f); }
        ESP_LOGI(TAG, "SPIFFS log cleared after export");
    }

    s_sd_export_count++;

    esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    spi_bus_free(SPI2_HOST);

    ESP_LOGI(TAG, "=== SD export done (%d rows), SPI2 free ===", rows);
    return (rows > 0);
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
