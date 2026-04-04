/*
 * main.c — Baby Cry Detection Monitor (Pure DSP) — Enhanced
 *
 * Features: AMOLED display with FFT spectrum, adaptive detection,
 *           NTP time sync, SD card logging, battery monitoring
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"
#include "audio_capture.h"
#include "cry_detector.h"
#include "ui_monitor.h"
#include "ntp_time.h"
#include "sd_logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lvgl.h"

#include "driver/gpio.h"

#include <string.h>

static const char *TAG = "main";

/* ── BOOT Button (GPIO 9) — brightness cycle ────────────── */

#define BOOT_BTN_GPIO       GPIO_NUM_9
#define BRIGHTNESS_STEPS    4
/* Step 0 = display off (power save), steps 1-3 = dim/med/bright */
static const uint8_t s_brightness_levels[BRIGHTNESS_STEPS] = {0, 40, 120, 220};
static int s_brightness_idx = 3;  /* Start at max */
static volatile bool s_display_on = true;

static void boot_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOOT_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    ESP_LOGI(TAG, "BOOT button init GPIO %d: %s (level=%d)",
             BOOT_BTN_GPIO, esp_err_to_name(ret), gpio_get_level(BOOT_BTN_GPIO));
}

/* Poll from LVGL task — debounce with counter */
static int s_btn_debounce = 0;
static bool s_btn_last_state = false;
static int s_btn_press_count = 0;

static void boot_button_poll(void)
{
    bool raw = (gpio_get_level(BOOT_BTN_GPIO) == 0);

    if (raw != s_btn_last_state) {
        s_btn_debounce++;
        if (s_btn_debounce >= 5) { /* 50ms debounce at 10ms poll */
            s_btn_last_state = raw;
            s_btn_debounce = 0;

            if (raw) { /* Just pressed */
                s_btn_press_count++;
                s_brightness_idx = (s_brightness_idx + 1) % BRIGHTNESS_STEPS;
                uint8_t level = s_brightness_levels[s_brightness_idx];

                if (level == 0) {
                    /* Display off — AMOLED low power, GRAM holds frame */
                    amoled_display_on_off(false);
                    s_display_on = false;
                    ESP_LOGW(TAG, "BUTTON #%d -> Display OFF", s_btn_press_count);
                } else {
                    if (!s_display_on) {
                        /* Display back on */
                        amoled_display_on_off(true);
                        s_display_on = true;
                        lv_obj_invalidate(lv_scr_act());
                        ESP_LOGW(TAG, "Display ON");
                    }
                    amoled_set_brightness(level);
                    ESP_LOGW(TAG, "BUTTON #%d -> Brightness: %d", s_btn_press_count, level);
                }
                ui_monitor_set_btn_count(s_btn_press_count, level);
            }
        }
    } else {
        s_btn_debounce = 0;
    }
}

/* ── Wi-Fi ──────────────────────────────────────────────── */

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(char *ip_out, size_t ip_len)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_wifi, inst_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = CONFIG_CANVAS_WIFI_SSID,
            .password = CONFIG_CANVAS_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi connecting to \"%s\"...", CONFIG_CANVAS_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi connection failed");
        return ESP_FAIL;
    }

    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);
    snprintf(ip_out, ip_len, IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Connected, IP: %s", ip_out);
    return ESP_OK;
}

/* ── LVGL Task ──────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    lv_timer_create(ui_monitor_timer_cb, 200, NULL);

    while (1) {
        boot_button_poll();
        if (s_display_on) {
            vTaskDelay(pdMS_TO_TICKS(10));
            lv_timer_handler();
        } else {
            /* Display off — slow poll to save CPU, only check button */
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ── App Main ───────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Baby Cry Monitor (Enhanced DSP) ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    /* Suppress noisy I2C logs */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("I2C_If", ESP_LOG_WARN);

    /* 1. NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. BOOT button init */
    boot_button_init();

    /* 3. SPIFFS init + SD export (before display claims SPI2).
     * Exports any data from previous session to SD card. */
    sd_logger_init();
    sd_logger_export_to_sd();  /* SPI2 is free — no display yet */

    /* 4. Display hardware (claims SPI2 for QSPI) */
    ESP_ERROR_CHECK(amoled_init());

    /* 4. Touch */
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_ret = amoled_touch_init(&touch);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s)", esp_err_to_name(touch_ret));
        touch = NULL;
    }

    /* 6. LVGL */
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), touch));

    /* 7. Create UI */
    ui_monitor_init();

    /* 8. Audio capture */
    bool audio_ok = (audio_capture_init() == ESP_OK);
    if (!audio_ok) {
        ESP_LOGE(TAG, "Audio init failed — detection disabled");
    }

    /* 10. Cry detector */
    ESP_ERROR_CHECK(cry_detector_init());

    /* 9. Start LVGL task */
    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);
    ui_monitor_show_connecting();

    ESP_LOGI(TAG, "Free heap after init: %lu bytes (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());

    /* 10. Wi-Fi */
    char ip[16] = {0};
    if (wifi_init_sta(ip, sizeof(ip)) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed");
        ui_monitor_show_ip("No WiFi");
    } else {
        ui_monitor_show_ip(ip);
        /* 11. NTP (after WiFi) */
        ntp_time_start();
    }

    /* 12. Start audio + detection tasks */
    if (audio_ok) {
        audio_capture_start_task();
    }
    cry_detector_start_task();

    amoled_set_brightness(180);

    ESP_LOGI(TAG, "Baby Cry Monitor running. IP: %s", ip[0] ? ip : "none");
    ESP_LOGI(TAG, "Free heap: %lu bytes (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
