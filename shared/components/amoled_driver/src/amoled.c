#include "amoled.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_sh8601.h"

static const char *TAG = "amoled";

#define LCD_HOST  SPI2_HOST
#define LCD_BIT_PER_PIXEL  16

static i2c_master_bus_handle_t   s_i2c_bus  = NULL;
static esp_lcd_panel_handle_t    s_panel    = NULL;
static esp_lcd_panel_io_handle_t s_panel_io = NULL;

/* ── AXP2101 registers ───────────────────────────────────── */
#define AXP2101_ADDR            0x34
#define AXP2101_STATUS1         0x00
#define AXP2101_STATUS2         0x01
#define AXP2101_COMMON_CFG      0x10  /* bit 0: software shutdown */
#define AXP2101_PWROFF_EN       0x22  /* bit 1: long press shutdown enable */
#define AXP2101_IRQ_OFF_ON_LVL  0x27  /* bits [1:0]: long press duration */
#define AXP2101_INTEN2          0x41  /* power key interrupt enable */
#define AXP2101_INTSTS2         0x49  /* power key interrupt status */
#define AXP2101_BAT_V_H         0x34  /* battery voltage high byte */
#define AXP2101_BAT_V_L         0x35  /* battery voltage low byte */
#define AXP2101_BAT_PERCENT     0xA4  /* fuel gauge percentage */

static i2c_master_dev_handle_t s_axp_dev = NULL;

/* SH8601 init commands from Waveshare official reference */
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xD0}, 1, 0},
};

/* ── AXP2101 I2C helpers ─────────────────────────────────── */

static esp_err_t axp_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_axp_dev, &reg, 1, val, 1, 100);
}

static esp_err_t axp_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_axp_dev, buf, 2, 100);
}

static esp_err_t axp_set_bits(uint8_t reg, uint8_t mask)
{
    uint8_t val;
    ESP_RETURN_ON_ERROR(axp_read_reg(reg, &val), TAG, "AXP read 0x%02x", reg);
    val |= mask;
    return axp_write_reg(reg, val);
}

static esp_err_t axp_init(void)
{
    /* Add AXP2101 device to I2C bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = AMOLED_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_axp_dev),
        TAG, "AXP2101 add device");

    /* Verify communication — read status register */
    uint8_t status;
    ESP_RETURN_ON_ERROR(axp_read_reg(AXP2101_STATUS1, &status), TAG, "AXP2101 read status");
    ESP_LOGI(TAG, "AXP2101 status1=0x%02x", status);

    /* Enable long press shutdown (2.5s default) */
    ESP_RETURN_ON_ERROR(axp_set_bits(AXP2101_PWROFF_EN, 0x02), TAG, "AXP long press enable");
    ESP_LOGI(TAG, "AXP2101 long-press shutdown enabled (2.5s)");

    /* Enable power key IRQs (short + long press) */
    ESP_RETURN_ON_ERROR(axp_set_bits(AXP2101_INTEN2, 0x0C), TAG, "AXP IRQ enable");
    ESP_LOGI(TAG, "AXP2101 power key IRQ enabled");

    /* Clear any pending IRQ */
    axp_write_reg(AXP2101_INTSTS2, 0xFF);

    return ESP_OK;
}

/* ── Public API: init ────────────────────────────────────── */

esp_err_t amoled_init(void)
{
    /* ── 1. I2C master bus (new driver) ──────────────────────── */
    ESP_LOGI(TAG, "I2C master bus init (SCL=%d, SDA=%d, %dHz)",
             AMOLED_I2C_SCL, AMOLED_I2C_SDA, AMOLED_I2C_FREQ_HZ);
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = AMOLED_I2C_NUM,
        .sda_io_num = AMOLED_I2C_SDA,
        .scl_io_num = AMOLED_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "I2C bus create");
    ESP_LOGI(TAG, "I2C master bus ready");

    /* ── 2. AXP2101 PMIC ────────────────────────────────────── */
    ESP_LOGI(TAG, "AXP2101 PMIC init (addr=0x%02x)", AXP2101_ADDR);
    esp_err_t axp_ret = axp_init();
    if (axp_ret != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 init failed (%s) — continuing without PMIC",
                 esp_err_to_name(axp_ret));
    }

    /* ── 3. TCA9554 IO Expander ──────────────────────────────── */
    ESP_LOGI(TAG, "TCA9554 init (addr=0x20)");
    esp_io_expander_handle_t io_expander = NULL;
    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_tca9554(s_i2c_bus,
            ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander),
        TAG, "TCA9554 create");

    /* Power cycle: LOW → 200ms → HIGH */
    esp_io_expander_set_dir(io_expander,
        IO_EXPANDER_PIN_NUM_4 | IO_EXPANDER_PIN_NUM_5, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_4, 0);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_5, 0);
    ESP_LOGI(TAG, "TCA9554 P4/P5 LOW — power cycling display+touch");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_4, 1);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_5, 1);
    ESP_LOGI(TAG, "TCA9554 P4/P5 HIGH — display+touch powered");

    /* ── 4. QSPI bus ─────────────────────────────────────────── */
    ESP_LOGI(TAG, "SPI2 QSPI bus init");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        AMOLED_QSPI_SCLK,
        AMOLED_QSPI_DATA0, AMOLED_QSPI_DATA1,
        AMOLED_QSPI_DATA2, AMOLED_QSPI_DATA3,
        AMOLED_LCD_H_RES * AMOLED_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init");

    /* ── 5. SH8601 panel ─────────────────────────────────────── */
    ESP_LOGI(TAG, "SH8601 panel init (CS=%d, QSPI 40MHz)", AMOLED_QSPI_CS);
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
        AMOLED_QSPI_CS, NULL, NULL);
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_panel_io),
        TAG, "Panel IO create");

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_panel_io, &panel_config, &s_panel),
                        TAG, "Panel create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "Panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Panel on");

    ESP_LOGI(TAG, "SH8601 AMOLED ready (%dx%d)", AMOLED_LCD_H_RES, AMOLED_LCD_V_RES);
    return ESP_OK;
}

i2c_master_bus_handle_t amoled_get_i2c_bus(void)
{
    return s_i2c_bus;
}

esp_lcd_panel_handle_t amoled_get_panel(void)
{
    return s_panel;
}

esp_lcd_panel_io_handle_t amoled_get_panel_io(void)
{
    return s_panel_io;
}

esp_err_t amoled_set_brightness(uint8_t level)
{
    ESP_LOGI(TAG, "Brightness → %d", level);
    const uint8_t data = level;
    return esp_lcd_panel_io_tx_param(s_panel_io, 0x51, &data, 1);
}

/* ── AXP2101 battery + power ────────────────────────────── */

esp_err_t amoled_get_battery_info(amoled_battery_info_t *info)
{
    if (!s_axp_dev || !info) return ESP_ERR_INVALID_STATE;
    memset(info, 0, sizeof(*info));

    /* Status registers */
    uint8_t s1, s2;
    ESP_RETURN_ON_ERROR(axp_read_reg(AXP2101_STATUS1, &s1), TAG, "read status1");
    ESP_RETURN_ON_ERROR(axp_read_reg(AXP2101_STATUS2, &s2), TAG, "read status2");
    info->vbus_present    = (s1 >> 5) & 1;
    info->charging        = ((s2 >> 5) & 0x07) == 1; /* charger state: 1 = charging */
    info->battery_present = (s1 >> 3) & 1;

    /* Battery voltage: 14-bit value split across two registers */
    uint8_t vh, vl;
    ESP_RETURN_ON_ERROR(axp_read_reg(AXP2101_BAT_V_H, &vh), TAG, "read bat_v_h");
    ESP_RETURN_ON_ERROR(axp_read_reg(AXP2101_BAT_V_L, &vl), TAG, "read bat_v_l");
    info->voltage_mv = ((uint16_t)(vh & 0x3F) << 8) | vl;

    /* Battery percentage */
    uint8_t pct;
    ESP_RETURN_ON_ERROR(axp_read_reg(AXP2101_BAT_PERCENT, &pct), TAG, "read bat_pct");
    info->percentage = (pct > 100) ? 100 : pct;

    return ESP_OK;
}

esp_err_t amoled_power_off(void)
{
    if (!s_axp_dev) return ESP_ERR_INVALID_STATE;
    ESP_LOGW(TAG, "Software power off — cutting all power rails");
    /* Set bit 0 of COMMON_CFG (0x10) to trigger shutdown */
    return axp_set_bits(AXP2101_COMMON_CFG, 0x01);
}
