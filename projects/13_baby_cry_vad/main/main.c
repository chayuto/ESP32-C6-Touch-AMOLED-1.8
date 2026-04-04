/*
 * main.c — Baby Cry Detection using VAD + DSP
 *
 * Startup sequence:
 * 1. NVS init
 * 2. amoled_init()        — I2C, AXP2101, TCA9554, QSPI, SH8601
 * 3. amoled_touch_init()  — FT3168
 * 4. amoled_lvgl_init()   — LVGL display driver
 * 5. ui_monitor_init()    — Create LVGL UI
 * 6. audio_capture_init() — ES8311 + I2S
 * 7. vad_filter_init()    — Voice activity detection
 * 8. cry_detector_init()  — FFT workspace
 * 9. Start LVGL task
 * 10. WiFi connect
 * 11. Start audio + detection tasks
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"
#include "audio_capture.h"
#include "vad_filter.h"
#include "cry_detector.h"
#include "ui_monitor.h"

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

#include <string.h>

static const char *TAG = "main";

/* ── Wi-Fi ─────────────────────────────────────────────────── */

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      5

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_count = 0;

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
            .ssid     = CONFIG_BABY_CRY_WIFI_SSID,
            .password = CONFIG_BABY_CRY_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi connecting to \"%s\"...", CONFIG_BABY_CRY_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi connection failed");
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);
    snprintf(ip_out, ip_len, IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Connected, IP: %s", ip_out);
    return ESP_OK;
}

/* ── LVGL task ─────────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    lv_timer_create(ui_monitor_timer_cb, 200, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

/* ── Main ──────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Baby Cry Detector (VAD+DSP) for ESP32-C6 ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    /* Suppress noisy I2C/touch logs */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);

    /* 1. NVS — required by Wi-Fi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. Display hardware: I2C → TCA9554 → AXP2101 → QSPI → SH8601 */
    ESP_ERROR_CHECK(amoled_init());

    /* 3. Touch controller */
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_ret = amoled_touch_init(&touch);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s) — continuing without touch",
                 esp_err_to_name(touch_ret));
        touch = NULL;
    }

    /* 4. LVGL: display driver + draw buffers + tick + touch */
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), touch));

    /* 5. Create LVGL UI */
    ui_monitor_init();

    /* 6. Audio: ES8311 codec + I2S */
    ret = audio_capture_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio init failed: %s", esp_err_to_name(ret));
    }

    /* 7. VAD filter */
    ESP_ERROR_CHECK(vad_filter_init());

    /* 8. Cry detector (FFT) */
    ESP_ERROR_CHECK(cry_detector_init());

    /* 9. Start LVGL task */
    ui_monitor_show_connecting();
    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    ESP_LOGI(TAG, "Free heap after init: %lu bytes (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());

    /* 10. Wi-Fi STA — blocks up to 30s */
    char ip[16] = {0};
    if (wifi_init_sta(ip, sizeof(ip)) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed — continuing without network");
        /* Don't halt — baby monitoring still works without WiFi */
    } else {
        ui_monitor_show_ip(ip);
    }

    /* 11. Set display brightness */
    amoled_set_brightness(180);

    /* 12. Start audio capture + detection tasks */
    audio_capture_start_task();
    cry_detector_start_task();

    ESP_LOGI(TAG, "Baby cry detector running!");
    ESP_LOGI(TAG, "Free heap: %lu bytes (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
