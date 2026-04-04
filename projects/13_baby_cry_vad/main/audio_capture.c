/*
 * audio_capture.c — ES8311 codec + I2S capture for 16kHz/16-bit mono
 *
 * Uses esp_codec_dev for ES8311 control and ESP-IDF I2S driver for audio data.
 * Audio data is written to a FreeRTOS ring buffer for consumption by the
 * cry detector task.
 *
 * Initialization order:
 * 1. I2S driver (provides MCLK to ES8311)
 * 2. ES8311 codec via esp_codec_dev (I2C control + I2S data interfaces)
 */

#include "audio_capture.h"
#include "amoled.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_vol.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include <string.h>
#include <math.h>

static const char *TAG = "audio_cap";

/* ── Pin definitions ──────────────────────────────────────── */
#define I2S_MCK_GPIO    19
#define I2S_BCK_GPIO    20
#define I2S_WS_GPIO     22
#define I2S_DI_GPIO     21   /* Mic data: ES8311 → ESP32 */
#define I2S_DO_GPIO     23   /* Speaker data: ESP32 → ES8311 */

#define ES8311_I2C_ADDR 0x30   /* 8-bit format: 7-bit 0x18 << 1 (esp_codec_dev convention) */
#define SAMPLE_RATE     16000
#define BITS_PER_SAMPLE 16
#define I2S_PORT        I2S_NUM_0
#define MCLK_MULTIPLE   I2S_MCLK_MULTIPLE_256

/* DMA: 480 samples per 30ms frame, 4 DMA descriptors */
#define DMA_FRAME_NUM   480
#define DMA_DESC_NUM    4

/* Ring buffer: ~8KB = 4096 samples x 2 bytes */
#define RING_BUF_SIZE   (4096 * sizeof(int16_t))

/* I2S read chunk: 30ms = 480 samples */
#define READ_CHUNK_SAMPLES  480
#define READ_CHUNK_BYTES    (READ_CHUNK_SAMPLES * sizeof(int16_t))

static i2s_chan_handle_t s_rx_chan = NULL;
static i2s_chan_handle_t s_tx_chan = NULL;
static esp_codec_dev_handle_t s_codec_dev = NULL;
static RingbufHandle_t s_ring_buf = NULL;
static volatile int16_t s_rms = 0;

/* ── I2S setup ────────────────────────────────────────────── */

static esp_err_t i2s_init(void)
{
    /* Allocate a full-duplex I2S channel pair */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_NUM;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channels: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Standard mode config for 16kHz mono */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCK_GPIO,
            .bclk = I2S_BCK_GPIO,
            .ws   = I2S_WS_GPIO,
            .dout = I2S_DO_GPIO,
            .din  = I2S_DI_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = MCLK_MULTIPLE;

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S TX: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S RX: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Enable TX first (needed for MCLK generation), then RX */
    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S TX: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S RX: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2S initialized: MCK=%d BCK=%d WS=%d DI=%d DO=%d",
             I2S_MCK_GPIO, I2S_BCK_GPIO, I2S_WS_GPIO, I2S_DI_GPIO, I2S_DO_GPIO);
    return ESP_OK;
}

/* ── Codec setup ──────────────────────────────────────────── */

static esp_err_t codec_init(void)
{
    i2c_master_bus_handle_t i2c_bus = amoled_get_i2c_bus();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "I2C bus not available");
        return ESP_FAIL;
    }

    /* Step 1: I2C control interface for ES8311 */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = ES8311_I2C_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) {
        ESP_LOGE(TAG, "Failed to create I2C control interface");
        return ESP_FAIL;
    }

    /* Step 2: GPIO interface (for PA pin control) */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!gpio_if) {
        ESP_LOGE(TAG, "Failed to create GPIO interface");
        return ESP_FAIL;
    }

    /* Step 3: I2S data interface (IDF 5.x: uses channel handles directly) */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = s_rx_chan,
        .tx_handle = s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) {
        ESP_LOGE(TAG, "Failed to create I2S data interface");
        return ESP_FAIL;
    }

    /* Step 4: ES8311 codec configuration */
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,    /* ES8311 in slave mode, ESP32 is I2S master */
        .use_mclk = true,
        .digital_mic = false,    /* Analog microphone */
        .pa_pin = -1,            /* PA controlled via TCA9554 P7, not GPIO */
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
        .mclk_div = 256,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    if (!es8311_if) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec interface");
        return ESP_FAIL;
    }

    /* Step 5: Create codec device handle (input for recording) */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if = data_if,
    };
    s_codec_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_codec_dev) {
        ESP_LOGE(TAG, "Failed to create codec device");
        return ESP_FAIL;
    }

    /* Step 6: Open codec with sample configuration */
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = BITS_PER_SAMPLE,
        .channel = 1,
        .channel_mask = 0x01,
        .sample_rate = SAMPLE_RATE,
        .mclk_multiple = 0,  /* 0 = default 256x */
    };
    int codec_ret = esp_codec_dev_open(s_codec_dev, &sample_info);
    if (codec_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec: %d", codec_ret);
        return ESP_FAIL;
    }

    /* Step 7: Set microphone gain (24 dB) */
    codec_ret = esp_codec_dev_set_in_gain(s_codec_dev, 24.0f);
    if (codec_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set mic gain: %d", codec_ret);
    }

    ESP_LOGI(TAG, "ES8311 codec initialized: %d Hz, %d-bit, mono",
             SAMPLE_RATE, BITS_PER_SAMPLE);
    return ESP_OK;
}

/* ── RMS calculation ──────────────────────────────────────── */

static int16_t compute_rms(const int16_t *buf, size_t count)
{
    if (count == 0) return 0;
    int64_t sum_sq = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = buf[i];
        sum_sq += s * s;
    }
    return (int16_t)sqrtf((float)sum_sq / count);
}

/* ── Capture task ─────────────────────────────────────────── */

static void capture_task(void *arg)
{
    int16_t chunk[READ_CHUNK_SAMPLES];
    size_t bytes_read;

    ESP_LOGI(TAG, "Audio capture task started");

    while (1) {
        esp_err_t ret = i2s_channel_read(s_rx_chan, chunk, READ_CHUNK_BYTES,
                                         &bytes_read, pdMS_TO_TICKS(100));
        if (ret != ESP_OK || bytes_read == 0) {
            continue;
        }

        size_t samples_read = bytes_read / sizeof(int16_t);

        /* Update RMS */
        s_rms = compute_rms(chunk, samples_read);

        /* Push to ring buffer (drop oldest if full) */
        if (xRingbufferSend(s_ring_buf, chunk, bytes_read, 0) != pdTRUE) {
            /* Ring buffer full — consumer not keeping up. Drop this frame. */
        }
    }
}

/* ── Public API ───────────────────────────────────────────── */

esp_err_t audio_capture_init(void)
{
    /* Create ring buffer */
    s_ring_buf = xRingbufferCreate(RING_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!s_ring_buf) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Initialize I2S first (provides MCLK to ES8311) */
    esp_err_t ret = i2s_init();
    if (ret != ESP_OK) return ret;

    /* Initialize codec (needs I2S handles for data interface) */
    ret = codec_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Codec init failed (%s) — I2S still active for raw capture",
                 esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Audio capture initialized");
    return ESP_OK;
}

size_t audio_capture_read(int16_t *buf, size_t samples)
{
    size_t bytes_needed = samples * sizeof(int16_t);
    size_t item_size = 0;

    void *data = xRingbufferReceiveUpTo(s_ring_buf, &item_size,
                                         pdMS_TO_TICKS(100), bytes_needed);
    if (!data || item_size == 0) {
        return 0;
    }

    size_t samples_got = item_size / sizeof(int16_t);
    memcpy(buf, data, item_size);
    vRingbufferReturnItem(s_ring_buf, data);

    return samples_got;
}

int16_t audio_capture_get_rms(void)
{
    return s_rms;
}

void audio_capture_start_task(void)
{
    xTaskCreate(capture_task, "audio_cap", 4096, NULL, 4, NULL);
}
