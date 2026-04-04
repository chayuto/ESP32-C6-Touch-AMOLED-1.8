/*
 * ntp_time.c — Opportunistic NTP time sync with WiFi power management
 *
 * Strategy: Sync NTP on boot → turn off WiFi → re-enable every 6 hours to re-sync.
 * WiFi is the biggest power drain (~80-100mA). Turning it off saves major battery.
 * The ESP32-C6 RTC keeps time between syncs (drift ~1-2s per 6 hours is acceptable).
 */

#include "ntp_time.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ntp";

static volatile bool s_synced = false;
static volatile bool s_wifi_on = true;
static int64_t s_last_sync_time = 0;

/* Re-sync interval: 6 hours */
#define RESYNC_INTERVAL_US  (6LL * 3600 * 1000000LL)
/* How long to keep WiFi on for re-sync attempt */
#define RESYNC_WIFI_TIMEOUT_S  45

static void time_sync_notification(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    s_last_sync_time = esp_timer_get_time();
    ESP_LOGI(TAG, "NTP time synchronized");
}

static void wifi_off(void)
{
    if (!s_wifi_on) return;
    esp_sntp_stop();
    esp_wifi_stop();
    s_wifi_on = false;
    ESP_LOGI(TAG, "WiFi OFF (power save) — re-sync in 6h");
}

static void wifi_on_and_sync(void)
{
    if (s_wifi_on) return;
    ESP_LOGI(TAG, "WiFi ON for NTP re-sync...");
    esp_wifi_start();
    s_wifi_on = true;

    /* Restart SNTP */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification);
    esp_sntp_init();
}

static void ntp_task(void *arg)
{
    (void)arg;

    setenv("TZ", "AEST-10", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started, waiting for initial sync...");

    /* Wait up to 30s for initial sync */
    for (int i = 0; i < 30 && !s_synced; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (s_synced) {
        char buf[32];
        ntp_time_get_str(buf, sizeof(buf));
        ESP_LOGI(TAG, "Time: %s — turning off WiFi to save power", buf);
        /* Give a moment for any pending network ops */
        vTaskDelay(pdMS_TO_TICKS(2000));
        wifi_off();
    } else {
        ESP_LOGW(TAG, "NTP sync failed — keeping WiFi on, will retry");
        /* Keep WiFi on and retry via SNTP polling */
    }

    /* Periodic re-sync loop */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000)); /* Check every minute */

        int64_t now = esp_timer_get_time();
        int64_t since_sync = now - s_last_sync_time;

        if (!s_wifi_on && s_synced && since_sync >= RESYNC_INTERVAL_US) {
            /* Time to re-sync */
            s_synced = false; /* Will be set true by callback */
            wifi_on_and_sync();

            /* Wait up to RESYNC_WIFI_TIMEOUT_S for sync */
            for (int i = 0; i < RESYNC_WIFI_TIMEOUT_S && !s_synced; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            if (s_synced) {
                char buf[32];
                ntp_time_get_str(buf, sizeof(buf));
                ESP_LOGI(TAG, "Re-synced: %s — WiFi off again", buf);
            } else {
                ESP_LOGW(TAG, "Re-sync failed — WiFi off, will retry in 6h");
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            wifi_off();
        }

        /* If WiFi is on but we haven't synced (initial failure), keep trying */
        if (s_wifi_on && !s_synced) {
            /* SNTP is polling in background, just wait */
            if (since_sync > 0 || s_last_sync_time == 0) {
                /* Still waiting for first sync or re-sync */
            }
        }
    }
}

void ntp_time_start(void)
{
    xTaskCreate(ntp_task, "ntp", 4096, NULL, 1, NULL);
}

bool ntp_time_is_synced(void)
{
    return s_synced;
}

bool ntp_time_get_str(char *buf, size_t len)
{
    if (!s_synced) {
        strncpy(buf, "--:--:--", len);
        return false;
    }
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    strftime(buf, len, "%H:%M:%S", &ti);
    return true;
}

time_t ntp_time_get(void)
{
    if (!s_synced) return 0;
    return time(NULL);
}

bool ntp_time_is_wifi_on(void)
{
    return s_wifi_on;
}
