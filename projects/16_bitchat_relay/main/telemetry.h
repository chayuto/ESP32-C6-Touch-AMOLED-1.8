#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_MAX_PEERS     32
#define TELEMETRY_TTL_BUCKETS    8   /* TTL 0..7 */
#define TELEMETRY_SIZE_BUCKETS   4   /* 256, 512, 1024, 2048 */

#define PEER_NICKNAME_MAX   20

typedef struct {
    uint8_t  peer_id[8];        /* Truncated PeerID from advertisement */
    char     nickname[PEER_NICKNAME_MAX]; /* Plaintext nickname from adv */
    int8_t   rssi;              /* Last seen RSSI */
    uint32_t last_seen_ms;      /* Uptime ms when last seen */
    uint32_t packets_from;      /* Packets received from this peer */
    bool     active;
} peer_entry_t;

typedef struct {
    /* Packet counters */
    uint32_t packets_rx;
    uint32_t packets_tx;        /* Relayed out */
    uint32_t packets_dropped;   /* Bloom duplicate */
    uint32_t packets_expired;   /* TTL=0 */
    uint32_t packets_buffered;  /* In store-and-forward */

    /* Fragment tracking */
    uint32_t fragments_rx;
    uint32_t fragment_starts;
    uint32_t fragment_ends;

    /* Message type distribution (from outer header) */
    uint32_t msg_type_counts[8]; /* 0=message,1=deliveryAck,2=noiseInit,3=fragStart,4=fragCont,5=fragEnd,6=announce,7=other */
    uint32_t broadcasts;        /* Recipient = 0xFFFFFFFF... */
    uint32_t directed;          /* Has specific recipient */
    uint32_t signed_packets;    /* hasSignature flag set */

    /* Bloom filter */
    uint32_t bloom_hits;        /* Duplicate detected */
    uint32_t bloom_misses;      /* Novel packet */
    float    bloom_fill_pct;    /* Current fill percentage */

    /* Distributions */
    uint32_t ttl_histogram[TELEMETRY_TTL_BUCKETS];
    uint32_t size_histogram[TELEMETRY_SIZE_BUCKETS]; /* 256/512/1024/2048 */

    /* Peers */
    peer_entry_t peers[TELEMETRY_MAX_PEERS];
    uint8_t  peer_count;        /* Active peers */
    uint8_t  peer_count_total;  /* All-time unique */

    /* Throughput (computed) */
    float    packets_per_sec;   /* Rolling average */

    /* System */
    uint32_t uptime_sec;
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t sd_flush_count;
    uint32_t spiffs_rows;
} telemetry_t;

void telemetry_init(void);

/* Parsed outer header info (public, unencrypted) */
typedef struct {
    uint8_t  version;
    uint8_t  msg_type;       /* 0=message,1=ack,2=noiseInit,3=fragStart,4=fragCont,5=fragEnd,6=announce */
    uint8_t  ttl;
    uint64_t timestamp_ms;   /* Packet creation epoch ms */
    uint8_t  flags;          /* hasRecipient(0x01), hasSignature(0x02), isCompressed(0x04) */
    uint16_t payload_len;
    uint8_t  sender_id[8];
    uint8_t  recipient_id[8]; /* Present if hasRecipient */
    bool     has_recipient;
    bool     is_broadcast;    /* Recipient = 0xFFFFFFFFFFFFFFFF */
    bool     has_signature;
} parsed_header_t;

/* Thread-safe increment functions (called from BLE task) */
void telemetry_on_packet_rx(const uint8_t *peer_id, int8_t rssi,
                            const parsed_header_t *hdr, uint16_t raw_size);
void telemetry_on_packet_tx(void);
void telemetry_on_bloom_hit(void);
void telemetry_on_bloom_miss(void);
void telemetry_on_packet_expired(void);
void telemetry_on_fragment(uint8_t type); /* 0=start, 1=continue, 2=end */
void telemetry_update_peer_nickname(const uint8_t *peer_id, const char *nickname);
void telemetry_on_sd_flush(void);
void telemetry_set_spiffs_rows(uint32_t rows);
void telemetry_set_bloom_fill(float pct);
void telemetry_set_packets_buffered(uint32_t count);

/* Snapshot for UI (copies under lock) */
void telemetry_get_snapshot(telemetry_t *out);

/* Update computed fields (call periodically) */
void telemetry_update(void);

#ifdef __cplusplus
}
#endif
