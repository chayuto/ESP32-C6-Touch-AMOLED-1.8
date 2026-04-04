#pragma once

#include <stddef.h>
#include "esp_err.h"

/**
 * Initialize Wi-Fi STA, block until connected (up to 30s).
 * Writes dotted-decimal IP into ip_out (caller supplies >= 16 bytes).
 * Disables modem sleep after connect to minimize HTTP latency.
 */
esp_err_t wifi_init_sta(char *ip_out, size_t ip_len);

/**
 * Build cached tools/list JSON, start mDNS + HTTP server.
 * Call after wifi_init_sta() succeeds.
 */
esp_err_t mcp_server_start(void);
