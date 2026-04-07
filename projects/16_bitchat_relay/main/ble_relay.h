#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BitChat BLE service UUID: f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c */
/* BitChat characteristic UUID: a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d */

/**
 * Initialize NimBLE stack in dual-role mode:
 *   - Peripheral: advertise BitChat service UUID, accept writes
 *   - Central: scan for BitChat peripherals, connect and relay
 */
void ble_relay_init(void);

/**
 * Start BLE scanning and advertising. Call after ble_relay_init().
 */
void ble_relay_start(void);

/**
 * Get number of active BLE connections.
 */
uint8_t ble_relay_connection_count(void);

#ifdef __cplusplus
}
#endif
