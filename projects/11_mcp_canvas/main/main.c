#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"
#include "drawing_engine.h"
#include "ui_display.h"
#include "snapshot.h"
#include "mcp_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

static const char *TAG = "main";

/*
 * LVGL task — drives lv_timer_handler every 10 ms.
 * ui_render_timer_cb (50 ms) drains the draw queue.
 * snapshot_process() checks for pending snapshot requests.
 */
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    lv_timer_create(ui_render_timer_cb, 50, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
        snapshot_process();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== MCP Canvas for ESP32-C6-Touch-AMOLED-1.8 ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    /* Suppress noisy touch/I2C polling logs — errors are non-fatal */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);

    /* 1. NVS — required by Wi-Fi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. Display hardware: I2C → TCA9554 → QSPI → SH8601 */
    ESP_ERROR_CHECK(amoled_init());

    /* 3. Touch controller */
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_ret = amoled_touch_init(&touch);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s) — continuing without touch",
                 esp_err_to_name(touch_ret));
        touch = NULL;
    }

    /* 4. LVGL: display driver + draw buffers + tick + touch input */
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), touch));

    /* 5. Drawing pipeline */
    drawing_engine_init();
    ui_display_init();
    snapshot_init();

    /* 6. Show "Connecting..." before Wi-Fi blocks */
    ui_display_show_connecting();

    /* 7. Start LVGL task — display animates while app_main blocks on Wi-Fi */
    xTaskCreate(lvgl_task, "lvgl", 16384, NULL, 2, NULL);

    ESP_LOGI(TAG, "Free heap after init: %lu bytes (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());

    /* 8. Wi-Fi STA — blocks up to 30s */
    char ip[16] = {0};
    if (wifi_init_sta(ip, sizeof(ip)) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed — halting");
        drawing_push_clear(60, 0, 0);
        drawing_push_text(20, 200, 255, 80, 80, 20, "Wi-Fi Failed");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* 9. Update display */
    ui_display_show_ip(ip);

    /* 10. MCP server: mDNS + HTTP */
    ESP_ERROR_CHECK(mcp_server_start());

    ESP_LOGI(TAG, "MCP server ready at http://%s/mcp  (esp32-canvas.local)", ip);
    ESP_LOGI(TAG, "Free heap: %lu bytes (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
