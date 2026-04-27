/*
 * audio_output.c — ES8311 DAC + NS4150B speaker amp for audio playback
 *
 * Generates tones and noise bursts in a background task, writes to I2S TX.
 * Sound requests are queued; only one sound plays at a time (newest wins).
 */

#include "audio_output.h"
#include "amoled.h"

#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
#include <string.h>

static const char *TAG = "audio_out";

#define I2S_MCK_IO      19
#define I2S_BCK_IO      20
#define I2S_WS_IO       22
#define I2S_DI_IO       21
#define I2S_DO_IO       23
#define ES8311_ADDR     0x30
#define SAMPLE_RATE     16000
#define MCLK_MULTIPLE   256

static i2s_chan_handle_t s_rx_handle = NULL;
static i2s_chan_handle_t s_tx_handle = NULL;
static esp_codec_dev_handle_t s_codec = NULL;

/* Sound command queue */
typedef enum { SND_TONE, SND_NOISE, SND_SILENCE } snd_type_t;

typedef struct {
    snd_type_t type;
    float freq;
    float volume;
    int duration_ms;
} snd_cmd_t;

static QueueHandle_t s_snd_queue = NULL;

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

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
            .invert_flags = { false, false, false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = MCLK_MULTIPLE;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));

    ESP_LOGI(TAG, "I2S initialized: %d Hz, 16-bit stereo", SAMPLE_RATE);
    return ESP_OK;
}

/* ── ES8311 Codec Init ──────────────────────────────────── */

static esp_err_t es8311_codec_init(void)
{
    i2c_master_bus_handle_t i2c_bus = amoled_get_i2c_bus();
    if (!i2c_bus) return ESP_FAIL;

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = ES8311_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) return ESP_FAIL;

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = s_rx_handle,
        .tx_handle = s_tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) return ESP_FAIL;

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!gpio_if) return ESP_FAIL;

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if     = ctrl_if,
        .gpio_if     = gpio_if,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk    = true,
        .digital_mic = false,
        .pa_pin      = -1,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    if (!es8311_if) return ESP_FAIL;

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if  = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) return ESP_FAIL;

    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = 16,
        .channel         = 2,
        .channel_mask    = 0x03,
        .sample_rate     = SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_codec, &sample_cfg) != ESP_CODEC_DEV_OK) return ESP_FAIL;

    /* Set output volume (0-100) */
    esp_codec_dev_set_out_vol(s_codec, 70);

    ESP_LOGI(TAG, "ES8311 codec initialized for playback");
    return ESP_OK;
}

/* ── Audio Output Task ──────────────────────────────────── */

/* LFSR for noise generation */
static uint16_t s_lfsr = 0xACE1;

static int16_t noise_sample(void)
{
    uint16_t bit = ((s_lfsr >> 0) ^ (s_lfsr >> 2) ^ (s_lfsr >> 3) ^ (s_lfsr >> 5)) & 1;
    s_lfsr = (s_lfsr >> 1) | (bit << 15);
    return (int16_t)(s_lfsr & 0x7FFF) - 16384;
}

/* Write stereo PCM buffer to I2S */
static void write_stereo(int16_t *buf, size_t stereo_samples)
{
    size_t bytes = stereo_samples * 2 * sizeof(int16_t);
    size_t written = 0;
    i2s_channel_write(s_tx_handle, buf, bytes, &written, pdMS_TO_TICKS(100));
}

#define CHUNK_SAMPLES  256
static int16_t s_stereo_buf[CHUNK_SAMPLES * 2];

static void play_tone(float freq, int dur_ms, float vol)
{
    int total_samples = (SAMPLE_RATE * dur_ms) / 1000;
    float phase = 0.0f;
    float phase_inc = 2.0f * M_PI * freq / (float)SAMPLE_RATE;
    int16_t amplitude = (int16_t)(vol * 24000.0f);

    /* Simple ADSR: 10ms attack, sustain, 20ms release */
    int attack_samples = SAMPLE_RATE / 100;  /* 10ms */
    int release_samples = SAMPLE_RATE / 50;  /* 20ms */
    int release_start = total_samples - release_samples;
    if (release_start < attack_samples) release_start = attack_samples;

    int pos = 0;
    while (pos < total_samples) {
        int chunk = CHUNK_SAMPLES;
        if (pos + chunk > total_samples) chunk = total_samples - pos;

        for (int i = 0; i < chunk; i++) {
            float env = 1.0f;
            int s = pos + i;
            if (s < attack_samples) {
                env = (float)s / (float)attack_samples;
            } else if (s >= release_start) {
                env = 1.0f - (float)(s - release_start) / (float)release_samples;
                if (env < 0.0f) env = 0.0f;
            }

            int16_t sample = (int16_t)(env * amplitude * sinf(phase));
            phase += phase_inc;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;

            s_stereo_buf[i * 2]     = sample;  /* L */
            s_stereo_buf[i * 2 + 1] = sample;  /* R */
        }
        write_stereo(s_stereo_buf, chunk);
        pos += chunk;
    }
}

static void play_noise(int dur_ms, float vol)
{
    int total_samples = (SAMPLE_RATE * dur_ms) / 1000;

    int release_start = total_samples - SAMPLE_RATE / 50;
    if (release_start < 0) release_start = 0;

    int pos = 0;
    while (pos < total_samples) {
        int chunk = CHUNK_SAMPLES;
        if (pos + chunk > total_samples) chunk = total_samples - pos;

        for (int i = 0; i < chunk; i++) {
            float env = 1.0f;
            int s = pos + i;
            if (s >= release_start) {
                env = 1.0f - (float)(s - release_start) / (float)(total_samples - release_start);
                if (env < 0.0f) env = 0.0f;
            }
            int16_t raw = noise_sample();
            int16_t sample = (int16_t)((float)raw * vol * env * 0.5f);
            /* Crude band-pass: attenuate by averaging with previous */
            s_stereo_buf[i * 2]     = sample;
            s_stereo_buf[i * 2 + 1] = sample;
        }
        write_stereo(s_stereo_buf, chunk);
        pos += chunk;
    }
}

static void play_silence(int dur_ms)
{
    int total_samples = (SAMPLE_RATE * dur_ms) / 1000;
    memset(s_stereo_buf, 0, sizeof(s_stereo_buf));
    int pos = 0;
    while (pos < total_samples) {
        int chunk = CHUNK_SAMPLES;
        if (pos + chunk > total_samples) chunk = total_samples - pos;
        write_stereo(s_stereo_buf, chunk);
        pos += chunk;
    }
}

static void audio_task(void *arg)
{
    ESP_LOGI(TAG, "Audio output task started");
    snd_cmd_t cmd;

    /* Play a brief startup chime */
    play_tone(523.25f, 100, 0.3f);  /* C5 */
    play_tone(659.25f, 100, 0.3f);  /* E5 */
    play_tone(783.99f, 150, 0.3f);  /* G5 */

    while (1) {
        if (xQueueReceive(s_snd_queue, &cmd, pdMS_TO_TICKS(50))) {
            /* Drain queue — take newest command if multiple queued */
            snd_cmd_t newer;
            while (xQueueReceive(s_snd_queue, &newer, 0)) {
                cmd = newer;
            }

            switch (cmd.type) {
            case SND_TONE:
                play_tone(cmd.freq, cmd.duration_ms, cmd.volume);
                break;
            case SND_NOISE:
                play_noise(cmd.duration_ms, cmd.volume);
                break;
            case SND_SILENCE:
                play_silence(cmd.duration_ms);
                break;
            }
        } else {
            /* No command — output silence to keep I2S DMA running */
            play_silence(50);
        }
    }
}

/* ── Public API ─────────────────────────────────────────── */

esp_err_t audio_output_init(void)
{
    s_snd_queue = xQueueCreate(8, sizeof(snd_cmd_t));
    if (!s_snd_queue) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(i2s_driver_init(), TAG, "I2S init failed");
    ESP_RETURN_ON_ERROR(es8311_codec_init(), TAG, "ES8311 init failed");

    return ESP_OK;
}

void audio_output_start_task(void)
{
    xTaskCreate(audio_task, "audio_out", 4096, NULL, 3, NULL);
}

void audio_output_play_tone(float freq_hz, int duration_ms, float volume)
{
    if (!s_snd_queue) return;   /* called before init / after init failed */
    snd_cmd_t cmd = {
        .type = SND_TONE,
        .freq = freq_hz,
        .volume = volume,
        .duration_ms = duration_ms,
    };
    xQueueSend(s_snd_queue, &cmd, 0);
}

void audio_output_play_noise(int duration_ms, float volume)
{
    if (!s_snd_queue) return;
    snd_cmd_t cmd = {
        .type = SND_NOISE,
        .volume = volume,
        .duration_ms = duration_ms,
    };
    xQueueSend(s_snd_queue, &cmd, 0);
}

void audio_output_amp_enable(bool enable)
{
    esp_io_expander_handle_t exp = amoled_get_io_expander();
    if (!exp) {
        ESP_LOGW(TAG, "IO expander not available");
        return;
    }
    esp_io_expander_set_dir(exp, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(exp, IO_EXPANDER_PIN_NUM_7, enable ? 1 : 0);
    ESP_LOGI(TAG, "Speaker amp %s", enable ? "ON" : "OFF");
}
