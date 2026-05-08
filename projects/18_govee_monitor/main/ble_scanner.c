#include "ble_scanner.h"
#include "govee_decoder.h"
#include "slot_store.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"

static const char *TAG = "ble";

static bool s_scanning = false;

static void start_scan(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    const struct ble_gap_disc_desc *d = &event->disc;

    /* Parse the advertisement TLV fields. NimBLE returns the first
     * manufacturer-specific block; that's enough for Govee thermo-hygrometers. */
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0) {
        return 0;
    }
    if (f.mfg_data == NULL || f.mfg_data_len < 6) {
        return 0;
    }

    uint16_t company_id = (uint16_t)f.mfg_data[0] | ((uint16_t)f.mfg_data[1] << 8);

    /* Local-name extraction (may be shortened or complete). */
    char name[32] = {0};
    if (f.name && f.name_len) {
        size_t n = f.name_len < sizeof(name) - 1 ? f.name_len : sizeof(name) - 1;
        memcpy(name, f.name, n);
    }

    /* Govee H5075 quick filter: company ID 0xEC88 + local name starting with "GVH5075"
     * OR raw mfg_data length 6. We accept by name to avoid noise from other 0xEC88
     * Govee variants in range. */
    bool is_govee_h5075 =
        (company_id == 0xEC88) &&
        ((name[0] && strncmp(name, "GVH5075", 7) == 0) ||
         (f.mfg_data_len - 2 == 6));
    if (!is_govee_h5075) {
        return 0;
    }

    govee_reading_t r = {0};
    if (!govee_decode_h5075(f.mfg_data + 2, (uint8_t)(f.mfg_data_len - 2), &r)) {
        return 0;
    }

    /* MAC arrives little-endian in d->addr.val[0..5]; reverse for printable + storage. */
    uint8_t mac_be[6];
    for (int i = 0; i < 6; i++) {
        mac_be[i] = d->addr.val[5 - i];
    }

    ESP_LOGI(TAG, "%s %02X:%02X:%02X:%02X:%02X:%02X T=%.1f H=%.1f bat=%u rssi=%d",
             name[0] ? name : "GVH5075",
             mac_be[0], mac_be[1], mac_be[2], mac_be[3], mac_be[4], mac_be[5],
             r.temp_c, r.humid_pct, r.battery_pct, d->rssi);

    slot_store_update(mac_be, &r, d->rssi);
    return 0;
}

static void start_scan(void)
{
    struct ble_gap_disc_params p = {
        .itvl              = BLE_GAP_LIM_DISC_SCAN_INT,
        .window            = BLE_GAP_LIM_DISC_SCAN_WINDOW,
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .limited           = 0,
        .passive           = 1,
        .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        s_scanning = false;
    } else {
        ESP_LOGI(TAG, "scan started");
        s_scanning = true;
    }
}

esp_err_t ble_scanner_pause(void)
{
    int rc = ble_gap_disc_cancel();
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        s_scanning = false;
        ESP_LOGI(TAG, "scan paused");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "ble_gap_disc_cancel rc=%d", rc);
    return ESP_FAIL;
}

esp_err_t ble_scanner_resume(void)
{
    if (s_scanning) return ESP_OK;
    start_scan();
    return s_scanning ? ESP_OK : ESP_FAIL;
}

bool ble_scanner_is_running(void)
{
    return s_scanning;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "addr init failed: %d", rc);
        return;
    }
    start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "stack reset, reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();      /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

esp_err_t ble_scanner_start(void)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
