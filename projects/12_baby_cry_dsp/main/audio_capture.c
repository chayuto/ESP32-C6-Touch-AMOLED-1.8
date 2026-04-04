/*
 * audio_capture.c — ES8311 codec init via esp_codec_dev + I2S capture into ring buffer
 *
 * Hardware: ES8311 on I2C 0x18 (shared bus from amoled_driver), I2S pins per board spec.
 * Captures 16kHz / 16-bit / mono audio via DMA.
 */

#include "audio_capture.h"
#include "amoled.h"

#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "audio";

/* ── Pin Definitions ────────────────────────────────────── */

#define I2S_MCK_IO      19
#define I2S_BCK_IO      20
#define I2S_WS_IO       22
#define I2S_DI_IO       21   /* mic data → ESP32 */
#define I2S_DO_IO       23   /* ESP32 → speaker */
#define ES8311_ADDR     0x30   /* 8-bit format: 7-bit 0x18 << 1 (esp_codec_dev convention) */
#define SAMPLE_RATE     16000
#define MCLK_MULTIPLE   256

/* ── Ring Buffer ────────────────────────────────────────── */

#define RING_BUF_SAMPLES  25600  /* ~1.6s at 16kHz — must be > ANALYSIS_SAMPLES (24000) */

static int16_t  s_ring_buf[RING_BUF_SAMPLES];
static volatile size_t s_write_pos = 0;
static volatile size_t s_read_pos  = 0;
static SemaphoreHandle_t s_data_ready;
static volatile int16_t s_current_rms = 0;

/* ── I2S + Codec Handles ────────────────────────────────── */

static i2s_chan_handle_t s_rx_handle = NULL;
static i2s_chan_handle_t s_tx_handle = NULL;

/* ── Ring Buffer Helpers ────────────────────────────────── */

static size_t ring_available(void)
{
    size_t w = s_write_pos;
    size_t r = s_read_pos;
    if (w >= r) return w - r;
    return RING_BUF_SAMPLES - r + w;
}

static void ring_write(const int16_t *data, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        s_ring_buf[s_write_pos] = data[i];
        s_write_pos = (s_write_pos + 1) % RING_BUF_SAMPLES;
    }
}

/* ── Public API ─────────────────────────────────────────── */

int16_t audio_capture_get_rms(void)
{
    return s_current_rms;
}

size_t audio_capture_read(int16_t *buf, size_t samples)
{
    /* Wait until enough data is available */
    while (ring_available() < samples) {
        xSemaphoreTake(s_data_ready, pdMS_TO_TICKS(100));
    }

    size_t count = 0;
    while (count < samples && ring_available() > 0) {
        buf[count++] = s_ring_buf[s_read_pos];
        s_read_pos = (s_read_pos + 1) % RING_BUF_SAMPLES;
    }
    return count;
}

/* ── I2S Init ───────────────────────────────────────────── */

static esp_err_t i2s_driver_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din  = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = MCLK_MULTIPLE;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));

    ESP_LOGI(TAG, "I2S driver initialized: %d Hz, 16-bit stereo", SAMPLE_RATE);
    return ESP_OK;
}

/* ── ES8311 Codec Init via esp_codec_dev ────────────────── */

static esp_err_t es8311_codec_init(void)
{
    i2c_master_bus_handle_t i2c_bus = amoled_get_i2c_bus();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "I2C bus not available");
        return ESP_FAIL;
    }

    /* Create I2C control interface */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = ES8311_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) {
        ESP_LOGE(TAG, "Failed to create I2C ctrl interface");
        return ESP_FAIL;
    }

    /* Create I2S data interface */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = s_rx_handle,
        .tx_handle = s_tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) {
        ESP_LOGE(TAG, "Failed to create I2S data interface");
        return ESP_FAIL;
    }

    /* Create GPIO interface (for PA control — we'll pass -1 for no PA pin) */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!gpio_if) {
        ESP_LOGE(TAG, "Failed to create GPIO interface");
        return ESP_FAIL;
    }

    /* Configure ES8311 */
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if     = ctrl_if,
        .gpio_if     = gpio_if,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,    /* ES8311 in slave mode */
        .use_mclk    = true,
        .digital_mic = false,    /* Analog mic */
        .pa_pin      = -1,       /* No PA control from codec — NS4150B is via TCA9554 */
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    if (!es8311_if) {
        ESP_LOGE(TAG, "Failed to create ES8311 interface");
        return ESP_FAIL;
    }

    /* Create top-level codec device */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if  = data_if,
    };
    esp_codec_dev_handle_t codec = esp_codec_dev_new(&dev_cfg);
    if (!codec) {
        ESP_LOGE(TAG, "Failed to create codec device");
        return ESP_FAIL;
    }

    /* Open with our sample config */
    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = 16,
        .channel         = 2,
        .channel_mask    = 0x03,
        .sample_rate     = SAMPLE_RATE,
    };
    if (esp_codec_dev_open(codec, &sample_cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec device");
        return ESP_FAIL;
    }

    /* Set microphone gain:
     * esp_codec_dev_set_in_gain only sets PGA (max 30dB).
     * For full gain chain we also need ADC digital volume via direct I2C register writes.
     * ES8311 gain chain: PGA (0-30dB) → ADC → Digital Volume (-95.5 to +32dB) */
    esp_codec_dev_set_in_gain(codec, 30.0);  /* Max PGA gain */
    ESP_LOGI(TAG, "PGA gain set to 30 dB");

    /* Boost ADC digital volume via direct I2C register write.
     * Reg 0x17 (ADC_VOLUME): 0xBF=0dB, 0xFF=+32dB, 0x00=-95.5dB
     * Set to 0xDF = +16dB (above 0dB default) */
    {
        i2c_master_dev_handle_t es_dev = NULL;
        i2c_device_config_t es_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x18,  /* 7-bit address for direct I2C */
            .scl_speed_hz = 100000,
        };
        if (i2c_master_bus_add_device(i2c_bus, &es_cfg, &es_dev) == ESP_OK) {
            /* Reg 0x14: PGA gain + mic select. 0x1A = analog mic, max PGA */
            uint8_t reg14[] = {0x14, 0x1A};
            i2c_master_transmit(es_dev, reg14, 2, 100);
            /* Reg 0x17: ADC volume = 0xCF (+8dB digital boost, halved from +16dB) */
            uint8_t reg17[] = {0x17, 0xCF};
            i2c_master_transmit(es_dev, reg17, 2, 100);
            /* Reg 0x16 bits[2:0]: ADC gain scale. 0x03 = 18dB (halved from 24dB) */
            uint8_t reg16[] = {0x16, 0x23};
            i2c_master_transmit(es_dev, reg16, 2, 100);
            i2c_master_bus_rm_device(es_dev);
            ESP_LOGI(TAG, "ES8311 direct: PGA=max, ADC vol=+8dB, gain scale=18dB");
        }
    }

    ESP_LOGI(TAG, "ES8311 codec initialized: %d Hz, 16-bit, mic gain 24dB", SAMPLE_RATE);
    return ESP_OK;
}

/* ── Audio Capture Task ─────────────────────────────────── */

/* I2S reads stereo frames (L+R), we extract mono (left channel) */
#define I2S_READ_SAMPLES  256
#define I2S_READ_BYTES    (I2S_READ_SAMPLES * 2 * sizeof(int16_t))  /* stereo */

static void audio_capture_task(void *arg)
{
    ESP_LOGI(TAG, "Audio capture task started");

    int16_t *stereo_buf = heap_caps_malloc(I2S_READ_BYTES, MALLOC_CAP_DEFAULT);
    if (!stereo_buf) {
        ESP_LOGE(TAG, "Failed to allocate stereo buffer");
        vTaskDelete(NULL);
        return;
    }

    int16_t mono_buf[I2S_READ_SAMPLES];
    int log_counter = 0;

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_handle, stereo_buf, I2S_READ_BYTES,
                                          &bytes_read, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG, "I2S read: ret=%s bytes=%u", esp_err_to_name(ret), (unsigned)bytes_read);
            continue;
        }

        /* Extract left channel from stereo interleaved data */
        size_t stereo_samples = bytes_read / sizeof(int16_t);
        size_t mono_count = stereo_samples / 2;
        if (mono_count > I2S_READ_SAMPLES) mono_count = I2S_READ_SAMPLES;

        int64_t sum_sq = 0;
        for (size_t i = 0; i < mono_count; i++) {
            mono_buf[i] = stereo_buf[i * 2];  /* Left channel */
            int32_t s = mono_buf[i];
            sum_sq += s * s;
        }

        /* Compute RMS */
        if (mono_count > 0) {
            float rms_f = sqrtf((float)sum_sq / (float)mono_count);
            s_current_rms = (int16_t)(rms_f > 32767.0f ? 32767 : rms_f);
        }

        /* Write to ring buffer */
        ring_write(mono_buf, mono_count);

        /* Debug log every ~3 seconds */
        log_counter++;
        if (log_counter % 200 == 1) {
            /* Check both channels to find where mic data is */
            int64_t sum_l = 0, sum_r = 0;
            for (size_t i = 0; i < mono_count && i < 256; i++) {
                int32_t l = stereo_buf[i * 2];
                int32_t r = stereo_buf[i * 2 + 1];
                sum_l += l * l;
                sum_r += r * r;
            }
            int rms_l = (int)sqrtf((float)sum_l / mono_count);
            int rms_r = (int)sqrtf((float)sum_r / mono_count);
            ESP_LOGI(TAG, "capture: bytes=%u mono=%u rms_L=%d rms_R=%d ring=%u",
                     (unsigned)bytes_read, (unsigned)mono_count,
                     rms_l, rms_r, (unsigned)ring_available());
        }

        /* Signal data ready */
        if (s_data_ready) {
            xSemaphoreGive(s_data_ready);
        }
    }

    free(stereo_buf);
    vTaskDelete(NULL);
}

/* ── Public Init ────────────────────────────────────────── */

esp_err_t audio_capture_init(void)
{
    s_data_ready = xSemaphoreCreateBinary();
    if (!s_data_ready) {
        return ESP_ERR_NO_MEM;
    }

    /* Init I2S first (creates tx/rx handles), then codec (uses those handles) */
    ESP_RETURN_ON_ERROR(i2s_driver_init(), TAG, "I2S init failed");
    ESP_RETURN_ON_ERROR(es8311_codec_init(), TAG, "ES8311 codec init failed");

    return ESP_OK;
}

void audio_capture_start_task(void)
{
    xTaskCreate(audio_capture_task, "audio_cap", 4096, NULL, 3, NULL);
}
