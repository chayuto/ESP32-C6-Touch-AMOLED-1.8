#include "net_time.h"
#include "rtc_manager.h"

#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include <time.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "net";

/* The Kconfig defaults. Matching them means "not configured" — run offline
 * rather than burning power retrying an association that cannot succeed. */
#define SSID_PLACEHOLDER "YOUR_SSID_HERE"

/* The PCF85063 keeps running across reflashes and can hold a stale seed from
 * whatever project ran on this board last — it reported 2026-01-01 as "valid"
 * on first bring-up here. A clock cannot legitimately predate the firmware
 * reading it, so anything older than the build date is treated as no clock at
 * all rather than silently misdating months of readings. */
static int64_t build_epoch_floor(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int day = 0, year = 0;
    /* __DATE__ is "Mmm dd yyyy" */
    if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3) return 0;
    const char *p = strstr(months, mon);
    if (!p) return 0;

    struct tm t = {0};
    t.tm_mon  = (int)((p - months) / 3);
    t.tm_mday = day;
    t.tm_year = year - 1900;
    time_t e = mktime(&t);
    if (e == (time_t)-1) return 0;
    /* __DATE__ is the build machine's *local* date and mktime resolves it
     * against the device's TZ (UTC), so the two can disagree by up to a day
     * in either direction. Back the floor off by two days: still catches a
     * seed that is months stale, without rejecting a good clock. */
    return (int64_t)e - 2 * 86400;
}

#define RECONNECT_MIN_MS   2000
#define RECONNECT_MAX_MS  60000     /* cap the backoff; keep trying forever */
#define RTC_WRITE_MIN_S    3600     /* don't wear the RTC on every sync */

static esp_timer_handle_t s_retry_timer;
static bool        s_wifi_up;
static clock_src_t s_src = CLOCK_SRC_NONE;

/* epoch_us - uptime_us. Adding uptime to this yields wall clock, which is how
 * a reading captured before any sync still resolves to the right instant. */
static int64_t s_epoch_offset_us;
static int64_t s_last_sync_us      = -1;
static int64_t s_last_rtc_write_us = -1;
static uint32_t s_backoff_ms       = RECONNECT_MIN_MS;

/* The offset is 64-bit and this is a 32-bit core, so a store is two words. The
 * uploader task reads it to stamp timestamps that become deterministic UUIDs in
 * an append-only archive: a half-updated value read across the RTC->NTP
 * handover would be baked in permanently and could never be corrected. The
 * critical section is a few instructions and runs a handful of times per boot.
 *
 * s_src is written last, and read first, so a reader either sees the previous
 * consistent clock or the new one -- never a torn mixture. */
static portMUX_TYPE s_clock_mux = portMUX_INITIALIZER_UNLOCKED;

static void adopt_clock(int64_t epoch_us, clock_src_t src)
{
    portENTER_CRITICAL(&s_clock_mux);
    s_epoch_offset_us = epoch_us - esp_timer_get_time();
    s_src = src;
    portEXIT_CRITICAL(&s_clock_mux);
}

static void sntp_sync_cb(struct timeval *tv)
{
    int64_t epoch_us = (int64_t)tv->tv_sec * 1000000LL + tv->tv_usec;
    adopt_clock(epoch_us, CLOCK_SRC_NTP);
    s_last_sync_us = esp_timer_get_time();

    ESP_LOGI(TAG, "NTP sync: epoch=%lld", (long long)tv->tv_sec);

    /* Push it into the PCF85063 so the next boot starts from RTC instead of
     * having no clock at all until WiFi comes back. */
    if (s_last_rtc_write_us < 0 ||
        (s_last_sync_us - s_last_rtc_write_us) > (int64_t)RTC_WRITE_MIN_S * 1000000LL) {
        if (rtc_manager_set_unix(tv->tv_sec) == ESP_OK) {
            s_last_rtc_write_us = s_last_sync_us;
            ESP_LOGI(TAG, "RTC updated from NTP");
        }
    }
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void wifi_event_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_up = false;
        /* Retry on a one-shot timer rather than sleeping here — this runs on
         * the default event loop, and blocking it would stall every other
         * handler. Backoff is capped, and we never stop trying: the AP may
         * come back at any hour and nothing else depends on this succeeding. */
        ESP_LOGW(TAG, "WiFi down, retry in %lu ms", (unsigned long)s_backoff_ms);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
        s_backoff_ms = s_backoff_ms * 2 > RECONNECT_MAX_MS
                     ? RECONNECT_MAX_MS : s_backoff_ms * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_up = true;
        s_backoff_ms = RECONNECT_MIN_MS;
        ESP_LOGI(TAG, "WiFi up");
        esp_netif_sntp_start();
    }
}

void net_time_init(void)
{
    /* RTC first: it may already know the time, which means uploads can carry
     * real timestamps before WiFi has associated (or without WiFi at all). */
    esp_err_t rerr = rtc_manager_init();
    int64_t   floor_s = build_epoch_floor();
    int64_t   rtc_s   = 0;

    if (rerr != ESP_OK) {
        ESP_LOGE(TAG, "RTC init failed: %s — clock unknown until NTP",
                 esp_err_to_name(rerr));
    } else if (!rtc_manager_is_valid()) {
        ESP_LOGW(TAG, "RTC reports oscillator stopped — clock unknown until NTP");
    } else if ((rtc_s = rtc_manager_now_unix()) < floor_s) {
        ESP_LOGW(TAG, "RTC time %lld predates build %lld — stale seed, ignoring",
                 (long long)rtc_s, (long long)floor_s);
    } else {
        adopt_clock(rtc_s * 1000000LL, CLOCK_SRC_RTC);
        ESP_LOGI(TAG, "clock from RTC: %lld (build floor %lld)",
                 (long long)rtc_s, (long long)floor_s);
    }

    if (strcmp(CONFIG_GOVEE_WIFI_SSID, SSID_PLACEHOLDER) == 0) {
        ESP_LOGW(TAG, "WiFi not configured — running offline");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    const esp_timer_create_args_t retry_args = {
        .callback = retry_timer_cb,
        .name     = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_retry_timer));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_cb, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_cb, NULL, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, CONFIG_GOVEE_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, CONFIG_GOVEE_WIFI_PASSWORD,
            sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));

    /* Modem sleep lets the BLE scanner keep more of the radio. The monitor is
     * a passive observer first and a network client second. */
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_GOVEE_NTP_SERVER);
    sntp_cfg.start = false;                 /* started on GOT_IP */
    sntp_cfg.sync_cb = sntp_sync_cb;
    sntp_cfg.server_from_dhcp = false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_cfg));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi starting, SSID=%s", CONFIG_GOVEE_WIFI_SSID);
}

bool net_time_wifi_up(void) { return s_wifi_up; }

clock_src_t net_time_clock_src(void) { return s_src; }

int64_t net_time_epoch_ms_at(int64_t uptime_us)
{
    portENTER_CRITICAL(&s_clock_mux);
    clock_src_t src = s_src;
    int64_t     off = s_epoch_offset_us;
    portEXIT_CRITICAL(&s_clock_mux);

    if (src == CLOCK_SRC_NONE) return 0;
    return (off + uptime_us) / 1000;
}

int64_t net_time_since_sync_s(void)
{
    if (s_last_sync_us < 0) return -1;
    return (esp_timer_get_time() - s_last_sync_us) / 1000000;
}
