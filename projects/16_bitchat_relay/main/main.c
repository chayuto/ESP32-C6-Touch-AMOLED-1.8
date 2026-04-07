/*
 * main.c — BitChat Opaque Relay Node
 *
 * ESP32-C6-Touch-AMOLED-1.8 as a BitChat BLE mesh relay:
 *   - Dual-role BLE: advertise + scan for BitChat peers
 *   - Opaque packet forwarding (no decryption)
 *   - Outer header parsing for telemetry (public metadata only)
 *   - LVGL dashboard on AMOLED for live mesh stats
 *   - SPIFFS buffered logging + periodic SD card export
 *
 * SD/Display SPI2 sharing: display paused during SD writes.
 * BOOT button: short press = brightness cycle, long press = SD export.
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"
#include "ble_relay.h"
#include "packet_buffer.h"
#include "telemetry.h"
#include "sd_logger.h"
#include "ui_dashboard.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "driver/gpio.h"

#include <string.h>

static const char *TAG = "main";

/* ── BOOT Button (GPIO 9) ───────────────────────────────── */

#define BOOT_BTN_GPIO       GPIO_NUM_9
#define BRIGHTNESS_STEPS    4
static const uint8_t s_brightness_levels[BRIGHTNESS_STEPS] = {0, 40, 120, 220};
static int s_brightness_idx = 3;
static volatile bool s_display_on = true;

/* Long-press detection for SD export */
#define LONG_PRESS_MS       2000
static int64_t s_btn_press_start = 0;
static bool s_btn_was_long = false;

static void boot_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOOT_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

/* ── SPI2 bus state machine ──────────────────────────────── */
/*
 * SPI2 is shared between QSPI display and SD card (SDSPI).
 * Only one can own the bus at a time.
 *
 * States:
 *   DISPLAY  — SPI2 owned by SH8601 QSPI (normal operation)
 *   RELEASED — SPI2 freed, panel IO destroyed (safe for SD)
 *   SD_ACTIVE — SPI2 owned by SDSPI (SD export in progress)
 *
 * Transitions:
 *   DISPLAY  → RELEASED  (amoled_display_on_off(false) + amoled_release_spi)
 *   RELEASED → SD_ACTIVE  (sd_logger_export_to_sd — internally inits/frees SPI2)
 *   SD_ACTIVE → RELEASED  (sd_logger_export_to_sd returns — SPI2 freed internally)
 *   RELEASED → DISPLAY    (amoled_reclaim_spi + amoled_display_on_off(true))
 *
 * CRITICAL: Never call amoled_display_on_off/set_brightness/LVGL flush
 *           when state != DISPLAY. The panel IO handle is NULL.
 */
typedef enum {
    SPI_STATE_DISPLAY = 0,
    SPI_STATE_RELEASED,
    SPI_STATE_SD_ACTIVE,
} spi_state_t;

static volatile spi_state_t s_spi_state = SPI_STATE_DISPLAY;

/* ── SD Export (display must be paused) ──────────────────── */

#define RECLAIM_RETRY_MAX  3
#define RECLAIM_RETRY_MS   100

static void do_sd_export(void)
{
    if (s_spi_state != SPI_STATE_DISPLAY) {
        ESP_LOGW(TAG, "SD export skipped — SPI not in DISPLAY state (%d)", s_spi_state);
        return;
    }

    ESP_LOGW(TAG, "SD export: DISPLAY → RELEASED");

    /* Step 1: Wait for any in-flight LVGL DMA flush to complete.
     * The LVGL driver uses double-buffered DMA — after lv_timer_handler(),
     * one buffer may still be DMA'ing to the display via QSPI.
     * A full 368x45x2 = 33KB QSPI transfer at 40MHz takes ~7ms.
     * We poll lv_disp_flush_is_last() isn't available in LVGL 8,
     * so use a conservative delay of 30ms (4x worst-case DMA). */
    vTaskDelay(pdMS_TO_TICKS(30));

    /* Step 1b: Turn display off while SPI2 is still valid */
    amoled_display_on_off(false);

    /* Step 2: Release SPI2 — destroys panel IO, frees bus */
    esp_err_t ret = amoled_release_spi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "release_spi failed: %s — display may be dead", esp_err_to_name(ret));
        /* s_panel_io is now NULL regardless (driver sets it NULL before bus free).
         * Do NOT call amoled_display_on_off here — it would crash.
         * Try to reclaim immediately to recover. */
    }
    s_spi_state = SPI_STATE_RELEASED;

    /* Step 3: SD card export (inits SPI2 for SDSPI internally, frees when done) */
    s_spi_state = SPI_STATE_SD_ACTIVE;
    int rows = sd_logger_export_to_sd();
    s_spi_state = SPI_STATE_RELEASED;
    /* sd_logger_export_to_sd always frees SPI2 before returning (even on error) */

    /* Step 4: Reclaim SPI2 for QSPI display — with retry */
    bool reclaimed = false;
    for (int attempt = 0; attempt < RECLAIM_RETRY_MAX; attempt++) {
        ret = amoled_reclaim_spi();
        if (ret == ESP_OK) {
            reclaimed = true;
            break;
        }
        ESP_LOGE(TAG, "reclaim_spi attempt %d/%d failed: %s",
                 attempt + 1, RECLAIM_RETRY_MAX, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(RECLAIM_RETRY_MS));
    }

    if (!reclaimed) {
        ESP_LOGE(TAG, "FATAL: Cannot reclaim SPI2 — display permanently dead");
        /* Stay in RELEASED state — LVGL task will skip drawing */
        return;
    }

    s_spi_state = SPI_STATE_DISPLAY;

    /* Step 5: Restore display */
    amoled_display_on_off(true);
    lv_obj_invalidate(lv_scr_act());
    amoled_set_brightness(s_brightness_levels[s_brightness_idx]);

    ESP_LOGW(TAG, "SD export: RELEASED → DISPLAY restored");

    if (rows >= 0) {
        telemetry_on_sd_flush();
        char msg[64];
        snprintf(msg, sizeof(msg), "SD: %d rows exported", rows);
        ui_dashboard_show_sd_status(msg);
        ESP_LOGI(TAG, "SD export complete: %d rows", rows);
    } else {
        ui_dashboard_show_sd_status("SD: no card");
        ESP_LOGW(TAG, "SD export failed (no card?)");
    }
}

/* ── Button polling (from LVGL task) ─────────────────────── */

static int s_btn_debounce = 0;
static bool s_btn_last_state = false;

static void boot_button_poll(void)
{
    bool raw = (gpio_get_level(BOOT_BTN_GPIO) == 0);

    if (raw != s_btn_last_state) {
        s_btn_debounce++;
        if (s_btn_debounce >= 5) {
            s_btn_last_state = raw;
            s_btn_debounce = 0;

            if (raw) {
                /* Button pressed */
                s_btn_press_start = esp_timer_get_time();
                s_btn_was_long = false;
            } else {
                /* Button released */
                if (!s_btn_was_long) {
                    /* Short press — cycle brightness */
                    s_brightness_idx = (s_brightness_idx + 1) % BRIGHTNESS_STEPS;
                    uint8_t level = s_brightness_levels[s_brightness_idx];

                    /* Guard: only touch display if SPI2 is in DISPLAY state */
                    if (s_spi_state != SPI_STATE_DISPLAY) {
                        /* Skip display ops — SPI2 not available */
                    } else if (level == 0) {
                        amoled_display_on_off(false);
                        s_display_on = false;
                    } else {
                        if (!s_display_on) {
                            amoled_display_on_off(true);
                            s_display_on = true;
                            lv_obj_invalidate(lv_scr_act());
                        }
                        amoled_set_brightness(level);
                    }
                }
            }
        }
    } else {
        s_btn_debounce = 0;

        /* Check for long press while held */
        if (raw && !s_btn_was_long && s_btn_press_start > 0) {
            int64_t held_ms = (esp_timer_get_time() - s_btn_press_start) / 1000;
            if (held_ms >= LONG_PRESS_MS) {
                s_btn_was_long = true;
                ESP_LOGW(TAG, "Long press — triggering SD export");
                do_sd_export();
            }
        }
    }
}

/* ── Periodic tasks ──────────────────────────────────────── */

#define SUMMARY_LOG_INTERVAL_SEC    300  /* Log summary every 5 min */
#define BLOOM_MAINTAIN_INTERVAL_SEC  60  /* Check bloom TTL every 1 min */
#define AUTO_SD_EXPORT_INTERVAL_SEC 1800 /* Auto-export every 30 min */

static int64_t s_last_summary = 0;
static int64_t s_last_bloom_maint = 0;
static int64_t s_last_auto_export = 0;

static void periodic_tasks(void)
{
    int64_t now = esp_timer_get_time();
    uint32_t uptime_s = (uint32_t)(now / 1000000LL);

    /* Periodic summary log */
    if (now - s_last_summary > (int64_t)SUMMARY_LOG_INTERVAL_SEC * 1000000LL) {
        s_last_summary = now;
        telemetry_t t;
        telemetry_get_snapshot(&t);

        amoled_battery_info_t batt;
        amoled_get_battery_info(&batt);

        sd_logger_log_summary(uptime_s, t.packets_rx, t.packets_tx,
                              t.packets_dropped, t.peer_count, t.free_heap,
                              t.bloom_fill_pct, batt.voltage_mv, batt.percentage);

        telemetry_set_spiffs_rows(sd_logger_get_row_count());
    }

    /* Bloom filter maintenance */
    if (now - s_last_bloom_maint > (int64_t)BLOOM_MAINTAIN_INTERVAL_SEC * 1000000LL) {
        s_last_bloom_maint = now;
        packet_buffer_bloom_maintain();
        telemetry_set_bloom_fill(packet_buffer_bloom_fill_pct());
        telemetry_set_packets_buffered(packet_buffer_count());
    }

    /* Auto SD export */
    if (now - s_last_auto_export > (int64_t)AUTO_SD_EXPORT_INTERVAL_SEC * 1000000LL) {
        s_last_auto_export = now;
        if (sd_logger_get_row_count() > 0) {
            do_sd_export();
        }
    }
}

/* ── LVGL Task ───────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    lv_timer_create(ui_dashboard_timer_cb, 500, NULL);

    while (1) {
        boot_button_poll();
        periodic_tasks();

        if (s_display_on && s_spi_state == SPI_STATE_DISPLAY) {
            vTaskDelay(pdMS_TO_TICKS(10));
            lv_timer_handler();
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ── App Main ────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== BitChat Relay Node ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    /* Suppress noisy logs */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("I2C_If", ESP_LOG_WARN);
    esp_log_level_set("NimBLE", ESP_LOG_WARN);
    esp_log_level_set("NIMBLE_NVS", ESP_LOG_WARN);
    esp_log_level_set("ble_hs", ESP_LOG_WARN);

    /* 1. NVS (required by NimBLE) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. Button */
    boot_button_init();

    /* 3. SPIFFS logger + initial SD export (SPI2 free before display) */
    sd_logger_init();
    sd_logger_export_to_sd();  /* Export any data from previous session */

    /* 4. Display hardware (claims SPI2) */
    ESP_ERROR_CHECK(amoled_init());

    /* 5. Touch */
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_ret = amoled_touch_init(&touch);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed: %s", esp_err_to_name(touch_ret));
        touch = NULL;
    }

    /* 6. LVGL (allocates 2x 33KB DMA draw buffers + LVGL heap) */
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), touch));
    ESP_LOGI(TAG, "After LVGL: heap=%lu min=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());

    /* 7. Dashboard UI */
    ui_dashboard_init();

    /* 8. Data layer */
    telemetry_init();
    packet_buffer_init();

    /* 9. BLE relay (NimBLE allocates ~120KB for dual-role stack) */
    ble_relay_init();
    ble_relay_start();
    ESP_LOGI(TAG, "After BLE: heap=%lu min=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());

    /* 10. LVGL task */
    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    amoled_set_brightness(180);

    ESP_LOGI(TAG, "BitChat Relay running");
    ESP_LOGI(TAG, "Free heap: %lu bytes (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
