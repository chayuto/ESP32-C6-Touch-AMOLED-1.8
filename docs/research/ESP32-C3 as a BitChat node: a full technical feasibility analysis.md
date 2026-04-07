# ESP32-C3 as a BitChat node: a full technical feasibility analysis

**The ESP32-C3 can absolutely run as a BitChat node** — and multiple working implementations already prove it. The hackerhouse-opensource/bitchat-esp32 project runs the full BitChat protocol (Noise XX handshakes, encrypted messaging, peer discovery) on the closely related ESP32-C6, while ScreamingEagleUSA/bitchat-esp32 provides a working Arduino-based relay node on standard ESP32 hardware that is directly portable to the C3. The ESP32-C3's BLE 5.0 stack supports simultaneous central/peripheral operation — the exact dual-role architecture BitChat requires — and its 384KB usable SRAM leaves **~180–210KB free** after the NimBLE stack, sufficient for BitChat's message buffers, Noise protocol state, and Bloom filter deduplication. The main constraints are a ~300–400ms Curve25519 handshake (acceptable for a chat application), the absence of PSRAM limiting store-and-forward to roughly 100–200 cached messages, and the single RISC-V core requiring careful task scheduling when bridging BLE and WiFi simultaneously.

---

## BitChat's BLE architecture maps cleanly to ESP32-C3 hardware

BitChat uses a custom dual-role BLE architecture — every device runs as both **central** (scanning for peers, initiating connections) and **peripheral** (advertising presence, accepting connections). It defines a single GATT service (UUID `F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C` for mainnet) with one characteristic supporting Write-Without-Response and Notify for bidirectional communication. This is precisely the pattern the ESP32-C3's NimBLE stack supports.

The NimBLE stack on ESP32-C3 supports all four BLE roles concurrently — broadcaster, observer, peripheral, and central — and handles up to **8–9 simultaneous connections** (configurable via `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`). BitChat caps central links at 6 (`bleMaxCentralLinks`), well within this limit. NimBLE consumes approximately **120–130KB of SRAM** in an optimized configuration (47KB static IRAM, 14KB static DRAM, ~60–70KB runtime heap), compared to Bluedroid's ~220KB+ footprint, making it the clear choice for the resource-constrained C3.

BLE 5.0 on the C3 adds meaningful capabilities for BitChat: **Extended Advertising** supports payloads up to ~1,650 bytes (vs. 31 bytes for legacy), **Coded PHY** provides roughly 4× range at 125 kbps, and **2M PHY** doubles throughput for nearby peers. BitChat's default fragment size of **469 bytes** fits cleanly within the BLE Data Length Extension maximum of 251 bytes per PDU, requiring 2–3 link-layer fragments per BitChat fragment.

BitChat's scanning behavior — 5 seconds on, 10 seconds off, with continuous scanning when traffic is recent — translates directly to NimBLE's `ble_gap_disc()` API with configurable scan interval and window parameters. The -90 dBm RSSI threshold (relaxing to -92 dBm in isolation) maps to NimBLE's scan filter capabilities.

## Cryptographic feasibility: all primitives available, performance is adequate

BitChat requires **Noise_XX_25519_ChaChaPoly_SHA256** — a specific combination of Curve25519 key exchange, ChaCha20-Poly1305 AEAD encryption, and SHA-256 hashing. Every one of these primitives is available on the ESP32-C3, through multiple library paths:

**The recommended crypto stack** combines three components. First, **libsodium** (available as ESP-IDF component `espressif/libsodium` v1.0.21) provides production-quality Curve25519/X25519, ChaCha20-Poly1305, and Ed25519 — all the primitives BitChat needs. It runs in pure software with no hardware acceleration, but ChaCha20-Poly1305 achieves an estimated **1–2 MB/s** at 160 MHz, orders of magnitude above BLE throughput. Second, **noise-c** (the Noise Protocol Framework C library by rweather, already integrated by ESPHome for ESP32 targets via PlatformIO) implements all Noise handshake patterns including XX, and can use libsodium as its crypto backend. Third, **mbedTLS** (built into ESP-IDF) provides **hardware-accelerated SHA-256** at ~12.6 MB/s via the C3's DMA-capable SHA accelerator — a 7–10× speedup over software.

Performance benchmarks from wolfSSL on the architecturally identical ESP32-C6 (same RISC-V core, same 160 MHz clock) provide concrete numbers:

| Operation | Time | Notes |
|-----------|------|-------|
| Curve25519 key gen/agree | **~319 ms** | Software only, acceptable for one-time handshake |
| Ed25519 sign | **~13 ms** | Used for announce packet signatures |
| Ed25519 verify | **~19.4 ms** | Verifying peer signatures |
| SHA-256 | **~12.6 MB/s** | Hardware accelerated |
| ChaCha20-Poly1305 | **~1–2 MB/s** | Software only, far exceeds BLE throughput |

The **Noise XX handshake requires three round-trips** with two Curve25519 DH operations each, meaning the full handshake takes roughly **1–2 seconds** of computation on the C3. For a chat application where sessions persist for minutes to hours, this is entirely acceptable. Per-message encryption/decryption at 1–2 MB/s adds negligible latency to BLE-sized packets (typically under 500 bytes).

One critical caveat: the ESP32-C3's hardware accelerates AES and SHA-256 but **not** ChaCha20. BitChat's iOS implementation uses ChaCha20-Poly1305 while the Android implementation reportedly uses AES-256-GCM — if the ESP32-C3 implementation uses AES-256-GCM instead (which the Noise framework supports), it would benefit from hardware acceleration. However, since BLE throughput (~250 KB/s maximum) is the bottleneck, not crypto throughput, this optimization is unnecessary.

## The ESP32-C3 cannot passively sniff BitChat traffic in any meaningful way

The ESP32-C3 **cannot enter true BLE promiscuous mode**. Unlike its well-documented WiFi promiscuous mode (`esp_wifi_set_promiscuous(true)`), the BLE radio operates through the NimBLE/Bluedroid host stack at the HCI level and only returns parsed advertisement reports — not raw link-layer PDUs. The BLE controller firmware contains proprietary closed-source binary blobs from Espressif with no public API for raw packet capture.

What the C3 *can* observe passively is limited to **BLE advertising packets** on the three primary channels (37, 38, 39). This reveals BitChat device presence (via the service UUID in advertisements), device RSSI, and timing patterns — but no message content. Once two BitChat peers establish a GATT connection, their data exchange hops across 37 data channels using a pseudo-random sequence determined by parameters exchanged in the `CONNECT_IND` packet. Following this hopping requires link-layer access that the ESP32 does not expose.

For true BLE sniffing, dedicated tools are required: the **nRF52840 USB Dongle** (~$10) with Nordic's sniffer firmware, or **Sniffle** on a TI CC26x2 LaunchPad (~$15–20), both of which integrate with Wireshark. Even these can follow only one connection at a time.

Crucially, even with a capable sniffer, **BitChat's Noise Protocol encrypted payloads cannot be decrypted passively**. The Noise XX pattern provides forward secrecy through ephemeral Curve25519 keys — each session generates fresh key material. A passive observer can determine device presence, communication timing, packet sizes (which BitChat mitigates via PKCS#7-style padding to 256/512/1,024/2,048-byte blocks), and the ephemeral public keys from the first handshake message, but message content remains opaque.

## Relay nodes and store-and-forward within 400KB of RAM

The ESP32-C3 is well-suited as a **dedicated BitChat relay/repeater**. The ScreamingEagleUSA relay implementation already demonstrates this architecture on standard ESP32: the node advertises the BitChat service UUID as a peripheral, accepts packet writes from nearby phones, scans for other BitChat peripherals as a central, and forwards packets with TTL decremented. It requires **no cryptographic processing** — relay nodes forward encrypted Noise packets opaquely without decrypting them.

A realistic **memory budget for a C3 relay node** looks like this:

| Component | RAM usage | Notes |
|-----------|-----------|-------|
| ESP-IDF core (FreeRTOS, drivers) | ~50–70 KB | Baseline OS overhead |
| NimBLE stack (optimized) | ~120–130 KB | Dual-role central + peripheral |
| Bloom filter deduplication | ~1–2 KB | 1,000 entries, 300s TTL (matching BitChat's spec) |
| Fragment reassembly buffers | ~15–30 KB | Up to 128 concurrent assemblies at 469 bytes each |
| Message store-and-forward cache | ~50–100 KB | ~100–200 messages at 500 bytes average |
| Application logic, peer tracking | ~10–20 KB | Connection state, routing tables |
| **Total** | **~250–350 KB** | Fits within 384KB usable SRAM |

Without PSRAM, store-and-forward is constrained to approximately **100–200 messages** in RAM, with LRU eviction and TTL-based expiry (the ScreamingEagleUSA relay uses 12-hour TTL and 100-message cap). For persistent storage across reboots, the C3's **4MB flash** can be used via LittleFS or NVS (Non-Volatile Storage), trading write speed and flash wear for durability. FreeRTOS ring buffers (`xRingbufferCreate`) handle variable-size message queuing efficiently with zero-copy reads.

**BLE-to-WiFi Nostr bridging** is architecturally feasible on the C3, which supports simultaneous BLE and WiFi operation through time-division multiplexing of the shared 2.4GHz radio. The bridge would run the BitChat BLE mesh on one side and a WebSocket client to Nostr relays on the other, translating BitChat packets to Nostr kind 20000 ephemeral events with geohash tags (the format defined by the `bitchat-nostr` Python package). The main challenge is **radio contention**: heavy WiFi activity starves BLE scanning and connections. A duty-cycling approach — BLE mesh active most of the time with periodic WiFi bursts for Nostr sync — mitigates this. The single RISC-V core makes task scheduling more critical than on dual-core ESP32 variants. Adding WiFi to the memory budget consumes roughly **40–60KB additional RAM**, tightening the store-and-forward cache but remaining feasible.

## Mesh analytics: what an ESP32-C3 monitoring node can measure

While the C3 cannot passively sniff encrypted traffic, it can collect valuable mesh network metrics by **participating in the BitChat mesh as a node** (either a full peer or a stealth/relay node):

From **BLE scanning** (no connection required): peer count from BitChat service UUID advertisements, RSSI per peer (enabling distance estimation via log-distance path loss model with Kalman filtering to reduce noise — one study achieved 0.265m mean absolute error), device arrival/departure events, and advertising interval analysis. Projects like **ESPresense** and **ESP32-Paxcounter** demonstrate production-quality BLE presence monitoring on ESP32 hardware.

From **active mesh participation**: message throughput (messages relayed per unit time), hop counts (by reading the TTL field — original TTL of 7 minus current TTL equals hops traversed), Bloom filter collision rates from the deduplication layer, fragment reassembly success/failure rates, Noise handshake timing, and ingress/egress link utilization. The hackerhouse-opensource implementation includes a debug mode with full packet dissection, providing exactly this kind of diagnostic output over UART.

A **dedicated monitoring node** could aggregate these metrics and report them via WiFi — pushing data to MQTT, InfluxDB, or a Grafana dashboard. The architecture mirrors existing ESP32-based BLE monitoring systems like OpenMQTTGateway/Theengs (which supports 100+ BLE sensor types) and esp32-ble2mqtt (bidirectional GATT-to-MQTT bridge). BitChat-specific metrics would require custom firmware but the infrastructure patterns are well-established.

## Existing ESP32-C3/C6 BitChat implementations and community projects

The ecosystem of ESP32-based BitChat implementations is surprisingly active. The most important projects, ranked by maturity:

**hackerhouse-opensource/bitchat-esp32** is the most complete native implementation. It runs on the Seeed Studio XIAO ESP32-C6 under Zephyr RTOS, implementing the full BitChat protocol: Noise XX handshakes for end-to-end encrypted chat, Ed25519/X25519 identity key management, geohash support, and an IRC-style CLI over UART with commands for status, peer listing, messaging, stealth mode, and debug packet dissection. Porting to ESP32-C3 requires primarily a Zephyr board overlay change — the C6 and C3 share the same RISC-V architecture, with the C6 adding BLE 5.3 and 802.15.4 (Thread/Zigbee) that BitChat doesn't use.

**ScreamingEagleUSA/bitchat-esp32** provides an Arduino-based BLE relay with dual-role operation, TTL management, store-and-forward (100 messages, 12-hour cache), and fragmentation handling. It uses only the built-in ESP32 BLE Arduino library with no additional dependencies, making it **directly portable to ESP32-C3** with minimal changes.

**"Bitle"** by Mark Soares is a solar-powered standalone ESP32 relay that operates indefinitely on solar or 25–50 days on battery, extending Bluetooth range 300–500 feet outdoors. It implements the full BitChat binary protocol, GATT server connections, Noise handshakes, and operates as an invisible relay (not visible as a peer).

**jooray/MeshCore-BitChat** bridges BitChat (BLE) and MeshCore (LoRa) using ESP32-based boards like the Heltec Wireless Stick Lite V3. Messages from BitChat's #mesh channel relay onto LoRa and vice versa. Working alpha with pre-built firmware binaries.

The **blakete/open-bitchat-mesh-station** project (part of the Boston Mesh Project) envisions an always-on base station with relay, store-and-forward, and internet backhaul bridging, but remains in the planning stage — the repository contains only a README and LICENSE with no source code.

The **Meshtastic-BitChat bridge** discussion (GitHub issue #7540, converted to discussion #7542) produced evansmj's alpha firmware plugin for RAK4631 and a community-contributed ESP32 Arduino bridge sketch in permissionlesstech discussion #307. A separate Python bridge script (GigaProjects/meshtastic-bitchat-bridge) runs on a Raspberry Pi connecting a Meshtastic USB device with BitChat BLE.

## Lessons from related BLE mesh chat projects

Several parallel projects illuminate the design space. **Standard Bluetooth SIG Mesh is the wrong foundation for chat** — its client/server model with provisioning requirements is designed for IoT sensor networks, not peer-to-peer messaging. Every successful BLE chat implementation (including BitChat) bypasses it entirely, using custom GATT services with dual-role central/peripheral operation and TTL-based flooding. This is the proven architecture for ESP32-C3.

**Meshtastic's store-and-forward requires PSRAM** that the C3 lacks, but its managed flooding approach for broadcasts (with SNR-based contention windows) and next-hop routing for direct messages (since v2.6) offer routing lessons. Meshtastic limits its NodeDB to ~100 nodes, suggesting a practical upper bound for mesh peer tracking on constrained devices.

**ESP-NOW mesh chat projects** like EspNowFloodingMesh demonstrate that flooding-based mesh with AES encryption, TTL support, and duplicate suppression works reliably on ESP32 with ~40–60ms latency — but ESP-NOW is ESP-specific with no phone interoperability, making it unsuitable as BitChat's primary transport.

The **ESPHome project's integration of the noise-c library** on ESP32 is perhaps the most directly relevant precedent — it proves that the Noise Protocol Framework (specifically Noise_NNpsk0_25519_ChaChaPoly_SHA256, a simpler pattern than BitChat's XX) runs in production on ESP32 hardware with the noise-c C library.

## Conclusion

The ESP32-C3 is a viable and practical platform for BitChat — not just theoretically, but demonstrated by working code. The most efficient path to a full BitChat node on ESP32-C3 is to port the hackerhouse-opensource Zephyr implementation (changing the board overlay from C6 to C3) or adapt the ScreamingEagleUSA Arduino relay (adding Noise XX crypto via noise-c + libsodium for full peer capability rather than relay-only operation). The hardware's real constraints are **not** the crypto (which is fast enough) or the BLE stack (which supports the required dual-role architecture), but rather the **384KB SRAM ceiling** that limits store-and-forward capacity and makes simultaneous BLE+WiFi bridging tight, and the **single core** that demands disciplined task scheduling. For a dedicated relay node without WiFi bridging, the C3 is arguably the ideal platform — cheap (~$5), low-power, and purpose-built for BLE applications. For a full-featured bridge node combining BLE mesh relay, Nostr WiFi bridge, and store-and-forward, the ESP32-S3 (with PSRAM and dual cores) would be more comfortable, but the C3 can do it within tighter margins.