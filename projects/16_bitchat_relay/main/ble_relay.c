/*
 * ble_relay.c — NimBLE dual-role BitChat opaque relay
 *
 * Peripheral: advertises BitChat service UUID, accepts GATT writes
 * Central: scans for BitChat peripherals, connects and receives notifications
 *
 * All packets are forwarded opaquely — no decryption, no Noise handshake.
 * We parse ONLY the outer unencrypted header for telemetry and TTL decrement.
 *
 * Outer header format (13 bytes minimum):
 *   [0]    version (1 byte)
 *   [1]    msg_type (1 byte)
 *   [2]    ttl (1 byte)
 *   [3-10] timestamp (8 bytes, uint64 LE)
 *   [11]   flags (1 byte) — bit0=hasRecipient, bit1=hasSignature, bit2=isCompressed
 *   [12-13] payload_len (2 bytes, uint16 LE)
 *   [14-21] sender_id (8 bytes)
 *   [22-29] recipient_id (8 bytes, conditional on hasRecipient flag)
 */

#include "ble_relay.h"
#include "packet_buffer.h"
#include "telemetry.h"
#include "sd_logger.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>

static const char *TAG = "ble_relay";

/* ── BitChat UUIDs ───────────────────────────────────────── */

/* Service: f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c */
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x5c, 0x4b, 0x3a, 0x2c, 0x1d, 0x8e, 0x3f, 0x9b,
    0x5a, 0x4c, 0x9e, 0x4a, 0x2d, 0x5e, 0x7b, 0xf4
);

/* Characteristic: a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d */
static const ble_uuid128_t s_chr_uuid = BLE_UUID128_INIT(
    0x5d, 0x4c, 0x3b, 0x2a, 0x1f, 0x0e, 0x9d, 0x8c,
    0x5b, 0x4a, 0xf6, 0xe5, 0xd4, 0xc3, 0xb2, 0xa1
);

/* ── Connection tracking ─────────────────────────────────── */

#define MAX_CONNECTIONS     8

typedef struct {
    uint16_t conn_handle;
    uint8_t  peer_id[8];       /* Extracted from advertisement */
    char     nickname[20];     /* Extracted from advertisement */
    bool     active;
    bool     is_central;       /* We initiated this connection */
    uint16_t chr_val_handle;   /* For writing to remote */
} conn_entry_t;

static conn_entry_t s_conns[MAX_CONNECTIONS];
static uint8_t s_conn_count = 0;
static uint16_t s_chr_val_handle;  /* Our local characteristic value handle */

/* ── Reassembly buffer for incoming fragments ────────────── */

#define REASSEMBLY_MAX      4
#define REASSEMBLY_BUF_SIZE 2048

typedef struct {
    uint16_t conn_handle;
    uint8_t  buf[REASSEMBLY_BUF_SIZE];
    uint16_t len;
    uint16_t expected_len;
    bool     active;
} reassembly_t;

static reassembly_t s_reassembly[REASSEMBLY_MAX];

/* ── Outer header parsing ────────────────────────────────── */

#define OUTER_HEADER_MIN    22  /* version+type+ttl+ts+flags+paylen+sender */
#define FLAG_HAS_RECIPIENT  0x01
#define FLAG_HAS_SIGNATURE  0x02
#define FLAG_IS_COMPRESSED  0x04

static bool parse_outer_header(const uint8_t *data, uint16_t len, parsed_header_t *hdr)
{
    if (len < OUTER_HEADER_MIN) return false;

    hdr->version = data[0];
    hdr->msg_type = data[1];
    hdr->ttl = data[2];

    /* Timestamp: 8 bytes little-endian */
    hdr->timestamp_ms = 0;
    for (int i = 0; i < 8; i++) {
        hdr->timestamp_ms |= ((uint64_t)data[3 + i]) << (i * 8);
    }

    hdr->flags = data[11];
    hdr->payload_len = (uint16_t)data[12] | ((uint16_t)data[13] << 8);

    memcpy(hdr->sender_id, &data[14], 8);

    hdr->has_recipient = (hdr->flags & FLAG_HAS_RECIPIENT) != 0;
    hdr->has_signature = (hdr->flags & FLAG_HAS_SIGNATURE) != 0;

    if (hdr->has_recipient && len >= 30) {
        memcpy(hdr->recipient_id, &data[22], 8);
        /* Check if broadcast: all 0xFF */
        hdr->is_broadcast = true;
        for (int i = 0; i < 8; i++) {
            if (hdr->recipient_id[i] != 0xFF) {
                hdr->is_broadcast = false;
                break;
            }
        }
    } else {
        memset(hdr->recipient_id, 0, 8);
        hdr->is_broadcast = !hdr->has_recipient; /* No recipient = broadcast */
    }

    return true;
}

/* Decrement TTL in-place in the outer header (byte offset 2) */
static bool decrement_ttl(uint8_t *data, uint16_t len)
{
    if (len < 3) return false;
    if (data[2] == 0) return false;  /* Already expired */
    data[2]--;
    return true;
}

/* ── Connection helpers ──────────────────────────────────── */

static conn_entry_t *find_conn(uint16_t conn_handle)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (s_conns[i].active && s_conns[i].conn_handle == conn_handle) {
            return &s_conns[i];
        }
    }
    return NULL;
}

static conn_entry_t *alloc_conn(uint16_t conn_handle)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!s_conns[i].active) {
            memset(&s_conns[i], 0, sizeof(conn_entry_t));
            s_conns[i].conn_handle = conn_handle;
            s_conns[i].active = true;
            s_conn_count++;
            return &s_conns[i];
        }
    }
    return NULL;
}

static void free_conn(uint16_t conn_handle)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (s_conns[i].active && s_conns[i].conn_handle == conn_handle) {
            s_conns[i].active = false;
            if (s_conn_count > 0) s_conn_count--;
            return;
        }
    }
}

/* ── Reassembly helpers ──────────────────────────────────── */

static reassembly_t *find_reassembly(uint16_t conn_handle)
{
    for (int i = 0; i < REASSEMBLY_MAX; i++) {
        if (s_reassembly[i].active && s_reassembly[i].conn_handle == conn_handle) {
            return &s_reassembly[i];
        }
    }
    return NULL;
}

/* Reserve for fragment reassembly (future) */
static reassembly_t *alloc_reassembly(uint16_t conn_handle) __attribute__((unused));
static reassembly_t *alloc_reassembly(uint16_t conn_handle)
{
    for (int i = 0; i < REASSEMBLY_MAX; i++) {
        if (!s_reassembly[i].active) {
            memset(&s_reassembly[i], 0, sizeof(reassembly_t));
            s_reassembly[i].conn_handle = conn_handle;
            s_reassembly[i].active = true;
            return &s_reassembly[i];
        }
    }
    return NULL;
}

/* ── Packet relay logic ──────────────────────────────────── */

static void process_incoming_packet(const uint8_t *data, uint16_t len,
                                    uint16_t source_conn, int8_t rssi)
{
    conn_entry_t *src = find_conn(source_conn);
    uint8_t *peer_id = src ? src->peer_id : (uint8_t *)"\0\0\0\0\0\0\0\0";

    /* Try to parse outer header for telemetry */
    parsed_header_t hdr;
    bool parsed = parse_outer_header(data, len, &hdr);

    /* Check bloom filter for deduplication */
    if (packet_buffer_is_duplicate(data, len)) {
        telemetry_on_bloom_hit();
        if (parsed) {
            sd_logger_log_packet("drop", peer_id, hdr.sender_id, rssi,
                                 hdr.ttl, len, hdr.msg_type, hdr.flags,
                                 hdr.has_recipient, hdr.is_broadcast);
        }
        return;
    }
    telemetry_on_bloom_miss();

    /* Record RX telemetry */
    telemetry_on_packet_rx(peer_id, rssi, parsed ? &hdr : NULL, len);

    /* Log to SPIFFS */
    if (parsed) {
        sd_logger_log_packet("rx", peer_id, hdr.sender_id, rssi,
                             hdr.ttl, len, hdr.msg_type, hdr.flags,
                             hdr.has_recipient, hdr.is_broadcast);
    }

    /* Check TTL */
    if (parsed && hdr.ttl == 0) {
        telemetry_on_packet_expired();
        return;
    }

    /* Make a mutable copy, decrement TTL, and relay.
     * Static buffer — safe because NimBLE host callbacks are single-threaded. */
    static uint8_t relay_buf[PACKET_MAX_SIZE];
    if (len > PACKET_MAX_SIZE) return;
    memcpy(relay_buf, data, len);
    decrement_ttl(relay_buf, len);

    /* Store in ring buffer for async relay */
    packet_buffer_push(relay_buf, len, parsed ? hdr.ttl - 1 : 0, rssi, peer_id);

    /* Relay to all other connected peers immediately */
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!s_conns[i].active) continue;
        if (s_conns[i].conn_handle == source_conn) continue;  /* Don't echo back */

        struct os_mbuf *om = ble_hs_mbuf_from_flat(relay_buf, len);
        if (!om) break;

        int rc;
        if (s_conns[i].is_central && s_conns[i].chr_val_handle) {
            /* We're central — write to their characteristic */
            rc = ble_gattc_write_no_rsp_flat(s_conns[i].conn_handle,
                                             s_conns[i].chr_val_handle,
                                             relay_buf, len);
        } else {
            /* We're peripheral — send notification */
            rc = ble_gatts_notify_custom(s_conns[i].conn_handle,
                                        s_chr_val_handle, om);
            om = NULL;  /* ble_gatts_notify_custom consumes om */
        }
        if (om) os_mbuf_free_chain(om);

        if (rc == 0) {
            telemetry_on_packet_tx();
        }
    }
}

/* ── GATT Server (Peripheral role) ───────────────────────── */

static int gatt_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        /* Received a write from a peer — this is an incoming BitChat packet */
        struct os_mbuf *om = ctxt->om;
        uint16_t len = OS_MBUF_PKTLEN(om);

        if (len > 0 && len <= PACKET_MAX_SIZE) {
            static uint8_t buf[PACKET_MAX_SIZE];
            os_mbuf_copydata(om, 0, len, buf);

            int8_t rssi = 0;
            ble_gap_conn_rssi(conn_handle, &rssi);

            process_incoming_packet(buf, len, conn_handle, rssi);
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_chr_uuid.u,
                .access_cb = gatt_chr_access,
                .val_handle = &s_chr_val_handle,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },  /* Terminator */
        },
    },
    { 0 },  /* Terminator */
};

/* ── GAP event handler ───────────────────────────────────── */

static int gap_event_handler(struct ble_gap_event *event, void *arg);
static void start_advertise(void);
static void start_scan(void);
static int svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr, void *arg);

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s, handle=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.conn_handle);
        if (event->connect.status == 0) {
            conn_entry_t *c = alloc_conn(event->connect.conn_handle);
            if (c) {
                /* Determine role: if we initiated, we're central */
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                    c->is_central = (desc.role == BLE_GAP_ROLE_MASTER);
                }
            }
            /* Request MTU exchange for larger packets */
            ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);

            /* If we're central, discover the BitChat characteristic */
            if (c && c->is_central) {
                ble_gattc_disc_chrs_by_uuid(event->connect.conn_handle,
                    1, 0xFFFF, &s_chr_uuid.u, svc_disc_cb, NULL);
            }
        }
        /* Resume advertising/scanning */
        start_advertise();
        start_scan();
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect, handle=%d, reason=0x%02x",
                 event->disconnect.conn.conn_handle,
                 event->disconnect.reason);
        free_conn(event->disconnect.conn.conn_handle);
        /* Free any reassembly buffer */
        reassembly_t *r = find_reassembly(event->disconnect.conn.conn_handle);
        if (r) r->active = false;
        start_advertise();
        start_scan();
        break;

    case BLE_GAP_EVENT_DISC: {
        /* Scan result — look for BitChat service UUID in advertisements */
        const struct ble_gap_disc_desc *disc = &event->disc;

        /* Filter by RSSI threshold (-90 dBm per BitChat spec) */
        if (disc->rssi < -90) break;

        /* Parse advertisement data for service UUID and peer info */
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
            break;
        }

        /* Check for BitChat service UUID in 128-bit UUID list */
        bool is_bitchat = false;
        for (int i = 0; i < fields.num_uuids128; i++) {
            if (ble_uuid_cmp(&fields.uuids128[i].u, &s_svc_uuid.u) == 0) {
                is_bitchat = true;
                break;
            }
        }
        if (!is_bitchat) break;

        ESP_LOGI(TAG, "BitChat peer found, RSSI=%d, addr=%02x:%02x:%02x:%02x:%02x:%02x",
                 disc->rssi,
                 disc->addr.val[5], disc->addr.val[4], disc->addr.val[3],
                 disc->addr.val[2], disc->addr.val[1], disc->addr.val[0]);

        /* Extract PeerID and nickname from manufacturer-specific or service data */
        uint8_t discovered_peer_id[8] = {0};
        char discovered_nickname[20] = {0};

        if (fields.svc_data_uuid128_len >= 8) {
            memcpy(discovered_peer_id, fields.svc_data_uuid128, 8);
            int nick_len = fields.svc_data_uuid128_len - 8;
            if (nick_len > 0 && nick_len < (int)sizeof(discovered_nickname)) {
                memcpy(discovered_nickname, fields.svc_data_uuid128 + 8, nick_len);
                discovered_nickname[nick_len] = '\0';
            }
        }

        /* Update telemetry with peer info */
        telemetry_on_packet_rx(discovered_peer_id, disc->rssi, NULL, 0);
        if (discovered_nickname[0]) {
            telemetry_update_peer_nickname(discovered_peer_id, discovered_nickname);
            ESP_LOGI(TAG, "  Nickname: %s, PeerID: %02x%02x%02x%02x%02x%02x%02x%02x",
                     discovered_nickname,
                     discovered_peer_id[0], discovered_peer_id[1],
                     discovered_peer_id[2], discovered_peer_id[3],
                     discovered_peer_id[4], discovered_peer_id[5],
                     discovered_peer_id[6], discovered_peer_id[7]);
        }

        /* Check if already connected */
        bool already_connected = false;
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (s_conns[i].active &&
                memcmp(s_conns[i].peer_id, discovered_peer_id, 8) == 0) {
                already_connected = true;
                break;
            }
        }

        /* Connect if not already connected and we have capacity */
        if (!already_connected && s_conn_count < MAX_CONNECTIONS) {
            ESP_LOGI(TAG, "Connecting to BitChat peer...");
            ble_gap_disc_cancel();
            int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &disc->addr,
                                     10000, NULL, gap_event_handler, NULL);
            if (rc != 0) {
                ESP_LOGW(TAG, "Connect failed: %d", rc);
                start_scan();
            }
        }
        break;
    }

    case BLE_GAP_EVENT_NOTIFY_RX: {
        /* Received notification from a peripheral we connected to */
        struct os_mbuf *om = event->notify_rx.om;
        uint16_t len = OS_MBUF_PKTLEN(om);

        if (len > 0 && len <= PACKET_MAX_SIZE) {
            static uint8_t buf[PACKET_MAX_SIZE];
            os_mbuf_copydata(om, 0, len, buf);

            int8_t rssi = 0;
            ble_gap_conn_rssi(event->notify_rx.conn_handle, &rssi);

            process_incoming_packet(buf, len, event->notify_rx.conn_handle, rssi);
        }
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        /* Scan finished — restart */
        start_scan();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: conn=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    default:
        break;
    }

    return 0;
}

/* ── Service discovery callback (central role) ───────────── */

static int svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == 0 && chr) {
        conn_entry_t *c = find_conn(conn_handle);
        if (c) {
            c->chr_val_handle = chr->val_handle;
            ESP_LOGI(TAG, "Found BitChat characteristic, val_handle=%d", chr->val_handle);

            /* Subscribe to notifications */
            uint8_t val[2] = {1, 0};  /* Enable notifications */
            ble_gattc_write_flat(conn_handle, chr->val_handle + 1,
                                val, sizeof(val), NULL, NULL);
        }
    }
    return 0;
}

/* ── Advertising (Peripheral role) ───────────────────────── */

static void start_advertise(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 160,   /* 100ms */
        .itvl_max = 320,   /* 200ms */
    };

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv_params, gap_event_handler, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Advertising started");
    } else if (rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "Advertise start failed: %d", rc);
    }
}

/* ── Scanning (Central role) ─────────────────────────────── */

static void start_scan(void)
{
    struct ble_gap_disc_params scan_params = {
        .itvl = 800,            /* 500ms interval */
        .window = 400,          /* 250ms window */
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,           /* Active scan to get scan response */
        .filter_duplicates = 1,
    };

    /* Scan for 5 seconds, then restart (BitChat spec: 5s on, 10s off) */
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 5000, &scan_params,
                          gap_event_handler, NULL);
    if (rc == 0) {
        ESP_LOGD(TAG, "Scan started");
    } else if (rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "Scan start failed: %d", rc);
    }
}

/* ── NimBLE host task + sync callback ────────────────────── */

static void ble_on_sync(void)
{
    /* Use best available address */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE host synced — starting relay");
    start_advertise();
    start_scan();
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ── Public API ──────────────────────────────────────────── */

void ble_relay_init(void)
{
    memset(s_conns, 0, sizeof(s_conns));
    memset(s_reassembly, 0, sizeof(s_reassembly));

    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* Register GATT services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    assert(rc == 0);

    ble_svc_gap_device_name_set("BitChat-Relay");

    ESP_LOGI(TAG, "NimBLE initialized, GATT services registered");
}

void ble_relay_start(void)
{
    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "NimBLE host task started");
}

uint8_t ble_relay_connection_count(void)
{
    return s_conn_count;
}
