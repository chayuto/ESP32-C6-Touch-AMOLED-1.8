/*
 * sd_logger.c — SPIFFS packet log accumulator + SD card batch export
 *
 * Two CSV files in SPIFFS:
 *   /log/packets.csv — per-packet event rows (rx, relay, drop, expire)
 *   /log/summary.csv — periodic telemetry snapshots
 *
 * SD export: caller must release SPI2 (amoled_release_spi) before calling
 * sd_logger_export_to_sd(), then reclaim (amoled_reclaim_spi) after.
 */

#include "sd_logger.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "sd_log";
static SemaphoreHandle_t s_log_mutex = NULL;

/* SPIFFS */
#define LOG_MOUNT       "/log"
#define PKT_LOG_FILE    LOG_MOUNT "/packets.csv"
#define SUM_LOG_FILE    LOG_MOUNT "/summary.csv"

/* SD card pins (same as board spec) */
#define SD_MOUNT        "/sdcard"
#define SD_MOSI         10
#define SD_MISO         18
#define SD_CLK          11
#define SD_CS           6

#define PKT_HEADER \
    "uptime_ms,direction,peer_id,sender_id,rssi,ttl,size,msg_type,flags,has_recip,is_broadcast"

#define SUM_HEADER \
    "uptime_s,packets_rx,packets_tx,packets_dropped," \
    "peer_count,free_heap,bloom_fill_pct,batt_mv,batt_pct"

static sd_state_t s_state = SD_STATE_NOT_PRESENT;
static uint32_t s_pkt_row_count = 0;
static uint32_t s_sum_row_count = 0;
static uint32_t s_export_count = 0;

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

    char buf[256];
    int rows = 0;
    while (fgets(buf, sizeof(buf), fin)) {
        /* Skip header lines */
        if (strncmp(buf, "uptime", 6) == 0) continue;
        if (buf[0] == '\n' || buf[0] == '\r') continue;
        fputs(buf, fout);
        rows++;
    }

    fclose(fin);
    fclose(fout);
    return rows;
}

/* ── Public API ──────────────────────────────────────────── */

void sd_logger_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = LOG_MOUNT,
        .partition_label = "storage",
        .max_files = 5,
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
    s_log_mutex = xSemaphoreCreateMutex();
    ensure_header(PKT_LOG_FILE, PKT_HEADER);
    ensure_header(SUM_LOG_FILE, SUM_HEADER);
}

void sd_logger_log_packet(const char *direction, const uint8_t *peer_id,
                          const uint8_t *sender_id, int8_t rssi,
                          uint8_t ttl, uint16_t size, uint8_t msg_type,
                          uint8_t flags, bool has_recipient, bool is_broadcast)
{
    if (s_state != SD_STATE_MOUNTED) return;
    if (!s_log_mutex || xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    FILE *f = fopen(PKT_LOG_FILE, "a");
    if (!f) { xSemaphoreGive(s_log_mutex); return; }

    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);

    static const uint8_t zero8[8] = {0};
    const uint8_t *sid = sender_id ? sender_id : zero8;

    fprintf(f, "%lu,%s,"
            "%02x%02x%02x%02x%02x%02x%02x%02x,"
            "%02x%02x%02x%02x%02x%02x%02x%02x,"
            "%d,%u,%u,%u,0x%02x,%d,%d\n",
            (unsigned long)uptime_ms,
            direction,
            peer_id[0], peer_id[1], peer_id[2], peer_id[3],
            peer_id[4], peer_id[5], peer_id[6], peer_id[7],
            sid[0], sid[1], sid[2], sid[3],
            sid[4], sid[5], sid[6], sid[7],
            rssi, ttl, size, msg_type, flags,
            has_recipient ? 1 : 0, is_broadcast ? 1 : 0);
    fclose(f);
    s_pkt_row_count++;
    xSemaphoreGive(s_log_mutex);
}

void sd_logger_log_summary(uint32_t uptime_s, uint32_t packets_rx,
                           uint32_t packets_tx, uint32_t packets_dropped,
                           uint8_t peer_count, uint32_t free_heap,
                           float bloom_fill_pct, uint16_t batt_mv,
                           uint8_t batt_pct)
{
    if (s_state != SD_STATE_MOUNTED) return;
    if (!s_log_mutex || xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    FILE *f = fopen(SUM_LOG_FILE, "a");
    if (!f) { xSemaphoreGive(s_log_mutex); return; }

    fprintf(f, "%lu,%lu,%lu,%lu,%u,%lu,%.1f,%u,%u\n",
            (unsigned long)uptime_s,
            (unsigned long)packets_rx,
            (unsigned long)packets_tx,
            (unsigned long)packets_dropped,
            peer_count,
            (unsigned long)free_heap,
            bloom_fill_pct,
            batt_mv, batt_pct);
    fclose(f);
    s_sum_row_count++;
    xSemaphoreGive(s_log_mutex);
}

int sd_logger_export_to_sd(void)
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
        return -1;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_CS;
    slot_cfg.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot_cfg, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s (no card?)", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return -1;
    }

    ESP_LOGI(TAG, "SD mounted: %s", card->cid.name);

    /* Hold the log mutex during copy+truncate to prevent BLE task writes
     * from interleaving with our read of the SPIFFS files */
    if (s_log_mutex) xSemaphoreTake(s_log_mutex, portMAX_DELAY);

    /* Export both CSV files */
    int pkt_rows = copy_data_rows(PKT_LOG_FILE, SD_MOUNT "/bitchat_packets.csv", PKT_HEADER);
    int sum_rows = copy_data_rows(SUM_LOG_FILE, SD_MOUNT "/bitchat_summary.csv", SUM_HEADER);
    int total = pkt_rows + sum_rows;

    ESP_LOGI(TAG, "Exported %d packet rows + %d summary rows to SD", pkt_rows, sum_rows);

    /* Clear SPIFFS logs after successful export */
    if (pkt_rows > 0) {
        FILE *f = fopen(PKT_LOG_FILE, "w");
        if (f) { fprintf(f, "%s\n", PKT_HEADER); fclose(f); }
        s_pkt_row_count = 0;
    }
    if (sum_rows > 0) {
        FILE *f = fopen(SUM_LOG_FILE, "w");
        if (f) { fprintf(f, "%s\n", SUM_HEADER); fclose(f); }
        s_sum_row_count = 0;
    }

    if (s_log_mutex) xSemaphoreGive(s_log_mutex);

    s_export_count++;

    esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    spi_bus_free(SPI2_HOST);

    ESP_LOGI(TAG, "=== SD export done (%d rows), SPI2 free ===", total);
    return total;
}

uint32_t sd_logger_get_row_count(void)
{
    return s_pkt_row_count + s_sum_row_count;
}

sd_state_t sd_logger_get_state(void)
{
    return s_state;
}

uint32_t sd_logger_get_export_count(void)
{
    return s_export_count;
}
