#include "govee_decoder.h"

#include <stddef.h>

/*
 * H5075 (Govee BLE thermo-hygrometer) advertisement decoder.
 *
 * Layout after stripping the 2-byte company-ID prefix (0x88 0xEC):
 *   d[0]    = flag (0x00)
 *   d[1..3] = packed 24-bit temp+humid; top bit = sign flag
 *   d[4]    = (err_bit << 7) | battery_pct
 *   d[5]    = trailing/padding
 *
 * Reference: Home Assistant govee-ble parser.py
 *   https://github.com/Bluetooth-Devices/govee-ble/blob/main/src/govee_ble/parser.py
 *
 * Some firmwares append a 25-byte "INTELLI_ROCKS" iBeacon trailer; the
 * upstream length we receive may be 6 or 31. Accept both — only the first
 * 6 bytes are meaningful.
 */

bool govee_decode_h5075(const uint8_t *d, uint8_t n, govee_reading_t *out)
{
    if (d == NULL || out == NULL) return false;
    if (n < 6) return false;

    uint32_t base = ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
    bool     neg  = (base & 0x800000) != 0;
    uint32_t v    = base & 0x7FFFFF;

    /* HA's exact arithmetic: int(v / 1000) / 10.0, then negate for sub-zero. */
    float t = (float)((int)(v / 1000)) / 10.0f;
    if (neg) t = -t;
    float h = (float)(v % 1000) / 10.0f;

    if (d[4] & 0x80) return false;     /* error flag */

    out->temp_c      = t;
    out->humid_pct   = h;
    out->battery_pct = d[4] & 0x7F;

    if (out->temp_c < -40.0f || out->temp_c > 100.0f) return false;
    if (out->humid_pct < 0.0f || out->humid_pct > 100.0f) return false;
    if (out->battery_pct > 100) return false;

    return true;
}
