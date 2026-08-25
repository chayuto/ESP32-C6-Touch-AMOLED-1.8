/*
 * rtc_manager.c — PCF85063 wall-clock driver.
 *
 * The I2C bus is shared with the rest of the board (touch, PMIC, etc.) and
 * has already been brought up by amoled_init(). We add ourselves as a new
 * device on that bus, read the time once at boot, and from then on serve
 * "current unix time" from a snapshot + esp_timer offset to avoid hitting
 * the RTC every tick. A periodic resync corrects drift.
 *
 * The PCF85063 stores time as packed BCD across registers 0x04-0x0A. Bit 7
 * of register 0x04 is the OS (oscillator stop) flag — set on first power-up
 * with a fresh backup battery, and the cue that we need to seed the time.
 */

#include "rtc_manager.h"
#include "amoled.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <time.h>
#include <stdlib.h>

static const char *TAG = "rtc";

#define PCF85063_ADDR     0x51
#define PCF85063_REG_CTRL1   0x00
#define PCF85063_REG_SECONDS 0x04

static i2c_master_dev_handle_t s_dev;
static int64_t s_unix_at_boot;
static int64_t s_us_at_boot;
static bool    s_is_valid;

/* ── BCD helpers ───────────────────────────────────────── */
static uint8_t bcd_to_bin(uint8_t v) { return (v & 0x0F) + ((v >> 4) * 10); }
static uint8_t bin_to_bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

/* ── I2C helpers ───────────────────────────────────────── */
static esp_err_t rtc_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 100);
}

static esp_err_t rtc_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[16];
    if (len + 1 > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[1 + i] = data[i];
    return i2c_master_transmit(s_dev, buf, len + 1, 100);
}

/* ── Time conversion ───────────────────────────────────── */

/* Convert PCF85063 7-byte time register block to unix epoch (UTC). */
static int64_t regs_to_unix(const uint8_t r[7])
{
    struct tm t = {0};
    t.tm_sec   = bcd_to_bin(r[0] & 0x7F);
    t.tm_min   = bcd_to_bin(r[1] & 0x7F);
    t.tm_hour  = bcd_to_bin(r[2] & 0x3F);
    t.tm_mday  = bcd_to_bin(r[3] & 0x3F);
    /* r[4] is weekday — ignored, mktime computes it */
    t.tm_mon   = bcd_to_bin(r[5] & 0x1F) - 1;
    t.tm_year  = bcd_to_bin(r[6]) + 100;     /* 2000-2099 → tm years (since 1900) */
    return (int64_t)mktime(&t);
}

static void unix_to_regs(int64_t u, uint8_t r[7])
{
    time_t tt = (time_t)u;
    struct tm t;
    gmtime_r(&tt, &t);
    r[0] = bin_to_bcd((uint8_t)t.tm_sec)  & 0x7F; /* clear OS bit */
    r[1] = bin_to_bcd((uint8_t)t.tm_min);
    r[2] = bin_to_bcd((uint8_t)t.tm_hour);
    r[3] = bin_to_bcd((uint8_t)t.tm_mday);
    r[4] = (uint8_t)t.tm_wday;
    r[5] = bin_to_bcd((uint8_t)(t.tm_mon + 1));
    r[6] = bin_to_bcd((uint8_t)(t.tm_year - 100));
}

/* ── Public API ────────────────────────────────────────── */

esp_err_t rtc_manager_init(void)
{
    /* Configure timezone as UTC so mktime/gmtime agree. */
    setenv("TZ", "UTC0", 1);
    tzset();

    i2c_master_bus_handle_t bus = amoled_get_i2c_bus();
    if (!bus) return ESP_FAIL;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PCF85063_ADDR,
        .scl_speed_hz    = AMOLED_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Read 7 time bytes starting at SECONDS */
    uint8_t r[7];
    err = rtc_read(PCF85063_REG_SECONDS, r, sizeof(r));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
        return err;
    }

    bool oscillator_stopped = (r[0] & 0x80) != 0;
    if (oscillator_stopped) {
        ESP_LOGW(TAG, "PCF85063 oscillator was stopped — seeding 2026-01-01");
        /* Seed: 2026-01-01 00:00:00 UTC.
         * Build the seed without re-using mktime so we do not depend on the
         * environment for this single magic date. */
        struct tm t = {0};
        t.tm_year = 126;   /* 2026 - 1900 */
        t.tm_mon  = 0;
        t.tm_mday = 1;
        int64_t seed = (int64_t)mktime(&t);
        err = rtc_manager_set_unix(seed);
        if (err != ESP_OK) return err;
        s_unix_at_boot = seed;
        s_is_valid = false;        /* still flag as "not real" */
    } else {
        s_unix_at_boot = regs_to_unix(r);
        s_is_valid = true;
    }
    s_us_at_boot = esp_timer_get_time();

    ESP_LOGI(TAG, "RTC %s: unix=%lld",
             s_is_valid ? "ok" : "seeded",
             s_unix_at_boot);
    return ESP_OK;
}

bool rtc_manager_is_valid(void) { return s_is_valid; }

int64_t rtc_manager_now_unix(void)
{
    int64_t dt_us = esp_timer_get_time() - s_us_at_boot;
    return s_unix_at_boot + (dt_us / 1000000);
}

esp_err_t rtc_manager_resync(void)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t r[7];
    esp_err_t err = rtc_read(PCF85063_REG_SECONDS, r, sizeof(r));
    if (err != ESP_OK) return err;
    if (r[0] & 0x80) return ESP_ERR_INVALID_STATE;  /* oscillator stopped */
    s_unix_at_boot = regs_to_unix(r);
    s_us_at_boot   = esp_timer_get_time();
    s_is_valid     = true;
    return ESP_OK;
}

esp_err_t rtc_manager_set_unix(int64_t u)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t r[7];
    unix_to_regs(u, r);

    /* PCF85063 datasheet: STOP must be set before writing time, then cleared.
     * Doing it inline saves a little code. */
    uint8_t ctrl;
    rtc_read(PCF85063_REG_CTRL1, &ctrl, 1);
    uint8_t stopped = ctrl | 0x20;
    rtc_write(PCF85063_REG_CTRL1, &stopped, 1);

    esp_err_t err = rtc_write(PCF85063_REG_SECONDS, r, sizeof(r));

    uint8_t running = ctrl & ~0x20;
    rtc_write(PCF85063_REG_CTRL1, &running, 1);

    if (err == ESP_OK) {
        s_unix_at_boot = u;
        s_us_at_boot   = esp_timer_get_time();
        s_is_valid     = true;
    }
    return err;
}
