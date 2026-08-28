#include "uploader.h"

#include "history.h"
#include "slot_store.h"
#include "net_time.h"
#include "uuid7.h"
#include "ble_scanner.h"
#include "amoled.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_crt_bundle.h"

static const char *TAG = "upload";

/* The 3-minute tier: enough resolution to see a room respond to a dehumidifier,
 * and 120 buckets of it is 6 hours of offline tolerance. */
#define UPLOAD_TIER      HISTORY_RANGE_6H

#define UPLOAD_PERIOD_MS   60000
#define MAX_ROWS_PER_POST     40      /* ~200 B/row -> ~8 KB of JSON */
#define JSON_CAP           12288
#define STATUS_PERIOD_S      300      /* board telemetry cadence */
#define RETRY_MIN_MS        5000
#define RETRY_MAX_MS      300000

/* One watermark PER SLOT, not one shared.
 *
 * With a single watermark a backlog is silently destructive: build_batch fills
 * the row budget from slot 0, slots further down contribute nothing, and the
 * shared watermark still advances past THEIR buckets -- which then never
 * upload and roll off the ring. Per-slot, a starved slot simply keeps its own
 * position and is served on the next pass.
 *
 * Keyed by slot index, so it shares the aliasing hazard history has: a slot
 * that changes owner must reset this too. */
static int64_t  s_watermark_us[SLOT_STORE_MAX];
static int64_t  s_last_ok_us = -1;
static uint32_t s_rows_sent, s_rows_dup, s_failures;
static uint32_t s_backoff_ms = RETRY_MIN_MS;
static uint16_t s_backlog;            /* buckets pending across all slots */
/* Drops to 1 after an ambiguous 409 so each row gets its own verdict, then
 * returns to full batches once one succeeds. */
static uint16_t s_max_rows = MAX_ROWS_PER_POST;
static int64_t  s_last_status_us = -1;

static bool configured(void)
{
    return CONFIG_GOVEE_SUPABASE_URL[0] && CONFIG_GOVEE_SUPABASE_KEY[0];
}

bool     uploader_configured(void)     { return configured(); }
uint32_t uploader_rows_sent(void)      { return s_rows_sent; }
uint32_t uploader_rows_duplicate(void) { return s_rows_dup; }
uint32_t uploader_failures(void)       { return s_failures; }
uint16_t uploader_backlog(void)        { return s_backlog; }

int64_t uploader_since_success_s(void)
{
    if (s_last_ok_us < 0) return -1;
    return (esp_timer_get_time() - s_last_ok_us) / 1000000;
}

static void iso8601(int64_t epoch_ms, char *out, size_t len)
{
    time_t    secs = (time_t)(epoch_ms / 1000);
    struct tm tm;
    gmtime_r(&secs, &tm);
    snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(epoch_ms % 1000));
}

/* Build one JSON array of rows newer than the watermark. Returns the number of
 * rows written and, via newest_us, how far the watermark may advance if the
 * POST is accepted. */
static uint16_t build_batch(char *json, size_t cap, int64_t *newest_us,
                           uint16_t max_rows)
{
    slot_t slots[SLOT_STORE_MAX];
    slot_store_snapshot(slots);

    size_t   n = 0;
    uint16_t rows = 0;
    for (int i = 0; i < SLOT_STORE_MAX; i++) newest_us[i] = s_watermark_us[i];
    n += snprintf(json + n, cap - n, "[");

    /* Rotate the starting slot each pass so a slot with a deep backlog cannot
     * hold the row budget forever and starve the others of freshness. */
    static int start = 0;
    for (int k = 0; k < CONFIG_GOVEE_MAX_SLOTS && rows < max_rows; k++) {
        int i = (start + k) % CONFIG_GOVEE_MAX_SLOTS;
        if (!slots[i].valid) continue;

        history_bucket_t buckets[MAX_ROWS_PER_POST];
        uint16_t got = history_since(i, UPLOAD_TIER, s_watermark_us[i], buckets,
                                     max_rows - rows);

        char mac[24];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 slots[i].mac[0], slots[i].mac[1], slots[i].mac[2],
                 slots[i].mac[3], slots[i].mac[4], slots[i].mac[5]);

        for (uint16_t b = 0; b < got && rows < max_rows; b++) {
            int64_t ms = net_time_epoch_ms_at(buckets[b].end_uptime_us);
            if (ms <= 0) continue;             /* clock not trustworthy yet */

            char id[UUID7_STR_LEN], ts[48];
            uuid7_deterministic(ms, CONFIG_GOVEE_DEVICE_ID, slots[i].mac, id);
            iso8601(ms, ts, sizeof(ts));

            const history_point_t *p = &buckets[b].p;

            /* Last gate before an append-only archive. Anything published here
             * is permanent -- deterministic ids mean a bad row cannot even be
             * replaced by a corrected one -- so refuse physically impossible
             * values rather than trusting every upstream path to stay correct.
             * Cheap insurance against the next bug in this chain. */
            if (p->temp_cx100 == HISTORY_NO_DATA || p->humid_x100 == HISTORY_NO_DATA ||
                p->n == 0 ||
                p->temp_cx100 < -4000 || p->temp_cx100 > 7000 ||
                p->humid_x100 < 0     || p->humid_x100 > 10000) {
                ESP_LOGW(TAG, "slot %d: refusing implausible bucket "
                         "(t=%d h=%d n=%u), not archiving it",
                         i, p->temp_cx100, p->humid_x100, p->n);
                if (buckets[b].end_uptime_us > newest_us[i]) {
                    newest_us[i] = buckets[b].end_uptime_us;   /* drop, don't retry forever */
                }
                continue;
            }
            int written = snprintf(json + n, cap - n,
                "%s{\"id\":\"%s\",\"ts\":\"%s\",\"device_id\":\"%s\",\"mac\":\"%s\","
                "\"temp_c\":%.2f,\"humid\":%.2f,\"battery\":%u,\"rssi\":%d,"
                "\"n_samples\":%u}",
                rows ? "," : "", id, ts, CONFIG_GOVEE_DEVICE_ID, mac,
                p->temp_cx100 / 100.0, p->humid_x100 / 100.0,
                p->batt_pct, p->rssi, p->n);
            if (written < 0 || (size_t)written >= cap - n) {
                ESP_LOGW(TAG, "batch buffer full at %u rows", rows);
                goto done;
            }
            n += written;
            rows++;
            if (buckets[b].end_uptime_us > newest_us[i]) {
                newest_us[i] = buckets[b].end_uptime_us;
            }
        }
    }
done:
    start = (start + 1) % CONFIG_GOVEE_MAX_SLOTS;
    snprintf(json + n, cap - n, "]");
    return rows;
}

/* How many buckets are waiting across every slot, for the heartbeat. This is
 * the number a human wants during an outage, so it must not be the size of the
 * batch we happened to build (which is capped at MAX_ROWS_PER_POST). */
static uint16_t pending_rows(void)
{
    slot_t slots[SLOT_STORE_MAX];
    slot_store_snapshot(slots);
    uint16_t total = 0;
    for (int i = 0; i < CONFIG_GOVEE_MAX_SLOTS; i++) {
        if (!slots[i].valid) continue;
        history_bucket_t buckets[HISTORY_POINTS];
        total += history_since(i, UPLOAD_TIER, s_watermark_us[i],
                               buckets, HISTORY_POINTS);
    }
    return total;
}

/* A 409 does NOT mean "all of these were already stored". PostgREST inserts a
 * batch as one statement, so a single duplicate id aborts the whole thing and
 * any genuinely new rows alongside it are rejected too. Verified against the
 * live project: a two-row batch with one duplicate returns 409 and the new row
 * is absent afterwards. The caller has to tell the two apart. */
typedef enum { POST_OK, POST_DUPLICATE, POST_FAILED } post_result_t;

static post_result_t post_json(const char *table, const char *json, uint16_t rows)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/rest/v1/%s", CONFIG_GOVEE_SUPABASE_URL, table);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,

    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        ESP_LOGE(TAG, "client init failed");
        return POST_FAILED;
    }

    esp_http_client_set_header(c, "apikey", CONFIG_GOVEE_SUPABASE_KEY);
    char auth[128];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_GOVEE_SUPABASE_KEY);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    /* return=minimal matters: returning the inserted rows would require SELECT,
     * which the device deliberately does not have. */
    esp_http_client_set_header(c, "Prefer", "return=minimal");
    esp_http_client_set_post_field(c, json, strlen(json));

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    esp_http_client_cleanup(c);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST %s %u rows failed: %s (%d ms)",
                 table, rows, esp_err_to_name(err), ms);
        return POST_FAILED;
    }

    if (status == 201 || status == 200) {
        ESP_LOGI(TAG, "POST %s %u rows -> %d in %d ms", table, rows, status, ms);
        return POST_OK;
    }
    if (status == 409) {
        ESP_LOGI(TAG, "POST %s %u rows -> 409 (%d ms)", table, rows, ms);
        return POST_DUPLICATE;
    }

    ESP_LOGW(TAG, "POST %s %u rows -> HTTP %d (%d ms)", table, rows, status, ms);
    return POST_FAILED;
}

/* Board telemetry. Once the monitor is off USB, a flat battery, a crash and a
 * WiFi outage are indistinguishable from the far end — rows just stop. This is
 * what makes the difference visible before the silence starts.
 *
 * The timestamp is floored to the status period so a retry mints the same id
 * and is rejected as a duplicate, exactly like a reading. */
static bool post_status(char *json, size_t cap)
{
    int64_t now_ms = net_time_epoch_ms_at(esp_timer_get_time());
    if (now_ms <= 0) return false;
    int64_t slot_ms = (now_ms / (STATUS_PERIOD_S * 1000LL)) * (STATUS_PERIOD_S * 1000LL);

    static const uint8_t ZERO_MAC[6] = {0};
    char id[UUID7_STR_LEN], ts[48];
    uuid7_deterministic(slot_ms, CONFIG_GOVEE_DEVICE_ID, ZERO_MAC, id);
    iso8601(slot_ms, ts, sizeof(ts));

    amoled_battery_info_t bat = {0};
    esp_err_t berr = amoled_get_battery_info(&bat);
    if (berr != ESP_OK) {
        ESP_LOGW(TAG, "battery read failed: %s", esp_err_to_name(berr));
    }

    snprintf(json, cap,
        "[{\"id\":\"%s\",\"ts\":\"%s\",\"device_id\":\"%s\","
        "\"batt_mv\":%u,\"batt_pct\":%u,\"charging\":%s,\"vbus\":%s,"
        "\"batt_present\":%s,\"free_heap\":%lu,\"min_heap\":%lu,"
        "\"uptime_s\":%lld,\"adverts\":%lu,\"rows_sent\":%lu,"
        "\"upload_fail\":%lu}]",
        id, ts, CONFIG_GOVEE_DEVICE_ID,
        bat.voltage_mv, bat.percentage,
        bat.charging ? "true" : "false",
        bat.vbus_present ? "true" : "false",
        bat.battery_present ? "true" : "false",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        (long long)(esp_timer_get_time() / 1000000),
        (unsigned long)ble_scanner_advert_count(),
        (unsigned long)s_rows_sent,
        (unsigned long)s_failures);

    post_result_t pr = post_json("device_status", json, 1);
    bool ok = (pr == POST_OK || pr == POST_DUPLICATE);   /* 1 row: 409 is exact */
    if (ok) {
        ESP_LOGI(TAG, "status: batt=%umV/%u%% vbus=%d charging=%d uptime=%llds",
                 bat.voltage_mv, bat.percentage, bat.vbus_present, bat.charging,
                 (long long)(esp_timer_get_time() / 1000000));
    }
    return ok;
}

static void uploader_task(void *arg)
{
    (void)arg;
    char *json = heap_caps_malloc(JSON_CAP, MALLOC_CAP_8BIT);
    if (!json) {
        ESP_LOGE(TAG, "no heap for %d B batch buffer — uploader disabled", JSON_CAP);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "uploader started: %s (device \"%s\", %d-min buckets)",
             CONFIG_GOVEE_SUPABASE_URL, CONFIG_GOVEE_DEVICE_ID,
             (int)(history_bucket_seconds(UPLOAD_TIER) / 60));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(UPLOAD_PERIOD_MS));

        if (!net_time_wifi_up()) {
            ESP_LOGD(TAG, "skip: wifi down");
            continue;
        }
        if (net_time_clock_src() == CLOCK_SRC_NONE) {
            ESP_LOGW(TAG, "skip: no trustworthy clock, holding readings");
            continue;
        }

        int64_t now = esp_timer_get_time();
        if (s_last_status_us < 0 ||
            (now - s_last_status_us) >= (int64_t)STATUS_PERIOD_S * 1000000LL) {
            if (post_status(json, JSON_CAP)) s_last_status_us = now;
        }

        int64_t  newest[SLOT_STORE_MAX];
        uint16_t rows = build_batch(json, JSON_CAP, newest, s_max_rows);
        s_backlog = pending_rows();
        if (rows == 0) continue;

        post_result_t pr = post_json("reading", json, rows);

        if (pr == POST_DUPLICATE && rows > 1) {
            /* Ambiguous: the batch was rejected whole, and it may have carried
             * new rows next to the duplicate. Advancing here is exactly how
             * readings get lost. Drop to one row per POST -- then every answer
             * is unambiguous -- and let the next pass make real progress. */
            ESP_LOGW(TAG, "409 on a %u-row batch: may contain new rows, "
                     "holding watermark and retrying one row at a time", rows);
            s_max_rows = 1;
            continue;
        }

        if (pr == POST_OK || pr == POST_DUPLICATE) {
            if (pr == POST_DUPLICATE) s_rows_dup += rows;   /* rows == 1 here */
            else                      s_rows_sent += rows;
            for (int i = 0; i < SLOT_STORE_MAX; i++) s_watermark_us[i] = newest[i];
            s_last_ok_us = esp_timer_get_time();
            s_backoff_ms = RETRY_MIN_MS;
            s_max_rows   = MAX_ROWS_PER_POST;   /* recovered; batch again */
            s_backlog    = pending_rows();
        } else {
            s_failures++;
            ESP_LOGW(TAG, "holding %u rows (%u pending), retry in %lu ms (failures %lu)",
                     rows, s_backlog, (unsigned long)s_backoff_ms,
                     (unsigned long)s_failures);
            vTaskDelay(pdMS_TO_TICKS(s_backoff_ms));
            s_backoff_ms = s_backoff_ms * 2 > RETRY_MAX_MS
                         ? RETRY_MAX_MS : s_backoff_ms * 2;
        }
    }
}

void uploader_start(void)
{
    if (!configured()) {
        ESP_LOGW(TAG, "Supabase not configured — upload disabled, "
                      "collection continues");
        return;
    }
    xTaskCreate(uploader_task, "uploader", 6144, NULL, 3, NULL);
}
