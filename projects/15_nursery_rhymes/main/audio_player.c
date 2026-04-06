/*
 * audio_player.c — Melody sequencer for nursery rhyme playback.
 *
 * Plays songs note-by-note through the ES8311 DAC + NS4150B speaker amp.
 * Uses sine wave synthesis with ADSR envelope for clean musical tones.
 * Runs in a background FreeRTOS task; controlled via play/stop commands.
 */

#include "audio_player.h"
#include "song_data.h"
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

static const char *TAG = "audio_player";

#define I2S_MCK_IO      19
#define I2S_BCK_IO      20
#define I2S_WS_IO       22
#define I2S_DI_IO       21
#define I2S_DO_IO       23
#define ES8311_ADDR     0x30
#define SAMPLE_RATE     16000
#define MCLK_MULTIPLE   256

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static i2s_chan_handle_t s_rx_handle = NULL;
static i2s_chan_handle_t s_tx_handle = NULL;
static esp_codec_dev_handle_t s_codec = NULL;

/* Playback state — accessed from main + audio task */
static volatile int  s_play_song   = -1;   /* song to start playing (-1=none) */
static volatile bool s_stop_req    = false; /* request to stop */
static volatile bool s_playing     = false;
static volatile int  s_current_song = -1;
static volatile int  s_note_index  = 0;
static volatile int  s_note_total  = 0;
static volatile int  s_volume      = 70;   /* 0-100 */

/* ── I2S Init ─────────────────────────────────────────── */

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

/* ── ES8311 Codec Init ────────────────────────────────── */

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

    esp_codec_dev_set_out_vol(s_codec, s_volume);

    ESP_LOGI(TAG, "ES8311 codec initialized");
    return ESP_OK;
}

/* ── Tone Synthesis ───────────────────────────────────── */

#define CHUNK_SAMPLES  256
static int16_t s_stereo_buf[CHUNK_SAMPLES * 2];

static void write_stereo(int16_t *buf, size_t stereo_samples)
{
    size_t bytes = stereo_samples * 2 * sizeof(int16_t);
    size_t written = 0;
    i2s_channel_write(s_tx_handle, buf, bytes, &written, pdMS_TO_TICKS(100));
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

/*
 * Play a single tone with ADSR envelope.
 * Returns true if stop was requested during playback.
 */
static bool play_note(float freq, int dur_ms)
{
    if (freq <= 0.0f) {
        /* Rest note */
        play_silence(dur_ms);
        return s_stop_req;
    }

    int total_samples = (SAMPLE_RATE * dur_ms) / 1000;
    float phase = 0.0f;
    float phase_inc = 2.0f * M_PI * freq / (float)SAMPLE_RATE;
    float vol = (float)s_volume / 100.0f;
    int16_t amplitude = (int16_t)(vol * 24000.0f);

    /* ADSR: 15ms attack, sustain, 30ms release — smooth for melody */
    int attack_samples = SAMPLE_RATE * 15 / 1000;
    int release_samples = SAMPLE_RATE * 30 / 1000;
    int release_start = total_samples - release_samples;
    if (release_start < attack_samples) release_start = attack_samples;

    int pos = 0;
    while (pos < total_samples) {
        if (s_stop_req) return true;

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

            s_stereo_buf[i * 2]     = sample;
            s_stereo_buf[i * 2 + 1] = sample;
        }
        write_stereo(s_stereo_buf, chunk);
        pos += chunk;
    }
    return s_stop_req;
}

/* ── Audio Task ───────────────────────────────────────── */

static void audio_task(void *arg)
{
    ESP_LOGI(TAG, "Audio player task started");

    /* Startup chime */
    play_note(523.25f, 100);
    play_note(659.25f, 100);
    play_note(783.99f, 150);

    while (1) {
        /* Wait for play command */
        if (s_play_song >= 0) {
            int idx = s_play_song;
            s_play_song = -1;
            s_stop_req = false;

            if (idx >= 0 && idx < g_song_count) {
                const song_t *song = &g_songs[idx];
                s_current_song = idx;
                s_note_total = song->note_count;
                s_note_index = 0;
                s_playing = true;

                ESP_LOGI(TAG, "Playing: %s (%d notes)", song->title, song->note_count);

                /* Small gap before song starts */
                play_silence(200);

                for (int i = 0; i < song->note_count; i++) {
                    if (s_stop_req || s_play_song >= 0) break;

                    s_note_index = i;
                    const note_t *n = &song->notes[i];
                    float freq = midi_to_freq(n->midi_note);

                    if (play_note(freq, n->duration_ms)) break;

                    /* Tiny gap between notes for articulation */
                    play_silence(15);
                }

                s_playing = false;
                s_current_song = -1;
                ESP_LOGI(TAG, "Song finished");
            }
        } else {
            /* Idle — output silence to keep I2S DMA happy */
            play_silence(50);
        }
    }
}

/* ── Public API ───────────────────────────────────────── */

esp_err_t audio_player_init(void)
{
    ESP_RETURN_ON_ERROR(i2s_driver_init(), TAG, "I2S init failed");
    ESP_RETURN_ON_ERROR(es8311_codec_init(), TAG, "ES8311 init failed");
    return ESP_OK;
}

void audio_player_start_task(void)
{
    xTaskCreate(audio_task, "audio_play", 4096, NULL, 3, NULL);
}

void audio_player_amp_enable(bool enable)
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

void audio_player_play(int song_index)
{
    s_stop_req = true;      /* stop current song */
    vTaskDelay(pdMS_TO_TICKS(30));
    s_stop_req = false;
    s_play_song = song_index;
}

void audio_player_stop(void)
{
    s_stop_req = true;
    s_play_song = -1;
}

bool audio_player_is_playing(void)
{
    return s_playing;
}

int audio_player_current_song(void)
{
    return s_current_song;
}

int audio_player_progress(void)
{
    if (!s_playing || s_note_total == 0) return 0;
    return (s_note_index * 100) / s_note_total;
}

void audio_player_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    s_volume = vol;
    if (s_codec) {
        esp_codec_dev_set_out_vol(s_codec, vol);
    }
}

int audio_player_get_volume(void)
{
    return s_volume;
}
