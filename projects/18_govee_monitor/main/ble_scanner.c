#include "ble_scanner.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"

static const char *TAG = "ble";

static void start_scan(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    const struct ble_gap_disc_desc *d = &event->disc;

    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0) {
        return 0;
    }
    if (f.mfg_data == NULL || f.mfg_data_len < 2) {
        return 0;
    }

    uint16_t company_id = (uint16_t)f.mfg_data[0] | ((uint16_t)f.mfg_data[1] << 8);

    char name[32] = {0};
    if (f.name && f.name_len) {
        size_t n = f.name_len < sizeof(name) - 1 ? f.name_len : sizeof(name) - 1;
        memcpy(name, f.name, n);
    }

    /* Hex-dump up to 16 mfg-data bytes for protocol inspection. */
    char hex[64] = {0};
    int hl = f.mfg_data_len > 16 ? 16 : f.mfg_data_len;
    for (int i = 0; i < hl; i++) {
        snprintf(hex + i * 3, 4, "%02x ", f.mfg_data[i]);
    }

    ESP_LOGI(TAG, "%-16s cid=0x%04X len=%u rssi=%d  %s",
             name[0] ? name : "(no name)",
             company_id, f.mfg_data_len, d->rssi, hex);

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
    } else {
        ESP_LOGI(TAG, "scan started");
    }
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
    nimble_port_run();
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
