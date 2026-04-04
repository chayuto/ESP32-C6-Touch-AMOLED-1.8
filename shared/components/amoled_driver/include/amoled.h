#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pin definitions */
#define AMOLED_QSPI_SCLK    0
#define AMOLED_QSPI_DATA0   1
#define AMOLED_QSPI_DATA1   2
#define AMOLED_QSPI_DATA2   3
#define AMOLED_QSPI_DATA3   4
#define AMOLED_QSPI_CS      5

#define AMOLED_I2C_SCL       7
#define AMOLED_I2C_SDA       8
#define AMOLED_I2C_NUM       I2C_NUM_0
#define AMOLED_I2C_FREQ_HZ   200000

#define AMOLED_LCD_H_RES     368
#define AMOLED_LCD_V_RES     448

/**
 * Initialize hardware: I2C master bus, TCA9554 IO expander (power on display + touch),
 * AXP2101 PMIC (battery + long-press shutdown), SPI2 QSPI bus, SH8601 AMOLED panel.
 */
esp_err_t amoled_init(void);

/** Get the I2C master bus handle (for touch and other I2C peripherals). */
i2c_master_bus_handle_t amoled_get_i2c_bus(void);

/** Get the initialized panel handle. */
esp_lcd_panel_handle_t amoled_get_panel(void);

/** Get the panel IO handle. */
esp_lcd_panel_io_handle_t amoled_get_panel_io(void);

/** Set AMOLED brightness via SH8601 register 0x51. level: 0-255. */
esp_err_t amoled_set_brightness(uint8_t level);

/** Turn display on/off. Off = low-power mode (0x28), On = resume (0x29). */
esp_err_t amoled_display_on_off(bool on);

/** Release SPI2 bus — deletes panel IO device, frees bus. Call when display is OFF. */
esp_err_t amoled_release_spi(void);

/** Reclaim SPI2 bus — reinits QSPI, recreates panel IO. Call before display ON. */
esp_err_t amoled_reclaim_spi(void);

/** Get touch INT GPIO number for strapping pin reference */
#define AMOLED_TOUCH_INT_GPIO  15

/* ── AXP2101 Power Management ─────────────────────────────── */

/** Battery info structure. */
typedef struct {
    uint16_t voltage_mv;    /* Battery voltage in mV */
    uint8_t  percentage;    /* State of charge 0-100% */
    bool     charging;      /* True if charging */
    bool     vbus_present;  /* True if USB power present */
    bool     battery_present; /* True if battery detected */
} amoled_battery_info_t;

/** Read battery status from AXP2101. */
esp_err_t amoled_get_battery_info(amoled_battery_info_t *info);

/** Trigger software power-off via AXP2101. Cuts all power rails. */
esp_err_t amoled_power_off(void);

#ifdef __cplusplus
}
#endif
