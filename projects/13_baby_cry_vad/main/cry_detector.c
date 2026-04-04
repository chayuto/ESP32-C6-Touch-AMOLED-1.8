/*
 * cry_detector.c — Baby cry detection via VAD gate + FFT frequency analysis
 *
 * Two-stage pipeline:
 * 1. VAD gate: Energy-based voice activity detection filters out non-vocal noise
 * 2. FFT analysis: When VAD detects voice, 512-point FFT checks for baby cry
 *    frequency signature (F0: 250-600 Hz, with harmonics)
 *
 * Detection criteria:
 * - cry_band_ratio > 0.25 (25% of spectral energy in 250-600 Hz band)
 * - Temporal smoothing: 3 consecutive positive frames to trigger alert
 * - 3 consecutive negative frames to clear alert
 */

#include "cry_detector.h"
#include "audio_capture.h"
#include "vad_filter.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_dsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <math.h>

static const char *TAG = "cry_det";

/* ── FFT Configuration ────────────────────────────────────── */
#define FFT_SIZE            512
#define SAMPLE_RATE         16000
#define FRAME_SAMPLES       480      /* 30ms at 16kHz */

/* Frequency bins: bin = freq * FFT_SIZE / SAMPLE_RATE */
#define CRY_FREQ_LOW_HZ    250
#define CRY_FREQ_HIGH_HZ   600
#define CRY_BIN_LOW         (CRY_FREQ_LOW_HZ * FFT_SIZE / SAMPLE_RATE)   /* ~8 */
#define CRY_BIN_HIGH        (CRY_FREQ_HIGH_HZ * FFT_SIZE / SAMPLE_RATE)  /* ~19 */

/* Detection thresholds */
#define CRY_BAND_RATIO_THRESHOLD  0.25f
#define CONFIRM_FRAMES            3   /* Consecutive positive frames to confirm */
#define CLEAR_FRAMES              3   /* Consecutive negative frames to clear */

/* ── State ────────────────────────────────────────────────── */

/* FFT workspace — esp_dsp requires float arrays */
static float s_fft_input[FFT_SIZE * 2];   /* Interleaved real/imag for complex FFT */
static float s_fft_window[FRAME_SAMPLES]; /* Hann window (sized for actual frame, not FFT) */
static int16_t s_audio_buf[FFT_SIZE];     /* Raw audio buffer */

/* Detection state */
static cry_detector_status_t s_status = {0};
static int s_positive_count = 0;
static int s_negative_count = 0;
static bool s_initialized = false;

/* ── Hann window generation ───────────────────────────────── */

static void generate_hann_window(float *win, int len)
{
    for (int i = 0; i < len; i++) {
        win[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (len - 1)));
    }
}

/* ── FFT-based cry band analysis ──────────────────────────── */

static float analyze_cry_band(const int16_t *samples, size_t count)
{
    /* Zero-pad to FFT_SIZE, apply Hann window to actual samples */
    memset(s_fft_input, 0, sizeof(s_fft_input));
    size_t n = (count > FRAME_SAMPLES) ? FRAME_SAMPLES : count;
    for (size_t i = 0; i < n; i++) {
        s_fft_input[i * 2] = (float)samples[i] * s_fft_window[i] / 32768.0f;
        s_fft_input[i * 2 + 1] = 0.0f;
    }

    /* In-place complex FFT */
    dsps_fft2r_fc32(s_fft_input, FFT_SIZE);
    dsps_bit_rev_fc32(s_fft_input, FFT_SIZE);

    /* Compute magnitude spectrum and band energies */
    float total_energy = 0.0f;
    float cry_energy = 0.0f;
    int half = FFT_SIZE / 2;

    for (int i = 1; i < half; i++) {
        float re = s_fft_input[i * 2];
        float im = s_fft_input[i * 2 + 1];
        float mag_sq = re * re + im * im;

        total_energy += mag_sq;
        if (i >= CRY_BIN_LOW && i <= CRY_BIN_HIGH) {
            cry_energy += mag_sq;
        }
    }

    if (total_energy < 1e-10f) return 0.0f;
    return cry_energy / total_energy;
}

/* ── Detection task ───────────────────────────────────────── */

static void detection_task(void *arg)
{
    ESP_LOGI(TAG, "Detection task started");
    ESP_LOGI(TAG, "Cry band: %d-%d Hz (bins %d-%d of %d-pt FFT)",
             CRY_FREQ_LOW_HZ, CRY_FREQ_HIGH_HZ, CRY_BIN_LOW, CRY_BIN_HIGH, FFT_SIZE);

    while (1) {
        /* Read a frame of audio (30ms = 480 samples) */
        size_t got = audio_capture_read(s_audio_buf, FRAME_SAMPLES);
        if (got < FRAME_SAMPLES / 2) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* Update RMS energy */
        float rms = 0;
        {
            int64_t sum_sq = 0;
            for (size_t i = 0; i < got; i++) {
                int32_t s = s_audio_buf[i];
                sum_sq += s * s;
            }
            rms = sqrtf((float)sum_sq / got);
        }
        s_status.rms_energy = rms;

        /* Stage 1: VAD gate */
        vad_result_t vad = vad_filter_process(s_audio_buf, got);
        s_status.vad_active = (vad == VAD_RESULT_SPEECH);

        if (vad != VAD_RESULT_SPEECH) {
            /* No voice activity — count toward clearing */
            s_negative_count++;
            s_positive_count = 0;

            if (s_negative_count >= CLEAR_FRAMES &&
                s_status.state == CRY_STATE_DETECTED) {
                s_status.state = CRY_STATE_IDLE;
                ESP_LOGI(TAG, "Cry alert cleared");
            } else if (s_status.state == CRY_STATE_VOICE) {
                s_status.state = CRY_STATE_IDLE;
            }
            continue;
        }

        /* Stage 2: FFT frequency analysis (only when VAD says voice) */
        if (s_status.state == CRY_STATE_IDLE) {
            s_status.state = CRY_STATE_VOICE;
        }

        float ratio = analyze_cry_band(s_audio_buf, got);
        s_status.cry_band_ratio = ratio;

        if (ratio > CRY_BAND_RATIO_THRESHOLD) {
            s_positive_count++;
            s_negative_count = 0;

            if (s_positive_count >= CONFIRM_FRAMES &&
                s_status.state != CRY_STATE_DETECTED) {
                s_status.state = CRY_STATE_DETECTED;
                s_status.cry_count++;
                s_status.last_cry_time = esp_timer_get_time();
                ESP_LOGW(TAG, "BABY CRYING DETECTED! (count=%lu, ratio=%.2f)",
                         (unsigned long)s_status.cry_count, ratio);
            }
        } else {
            /* Voice detected but not cry frequency pattern */
            s_negative_count++;
            s_positive_count = 0;

            if (s_negative_count >= CLEAR_FRAMES &&
                s_status.state == CRY_STATE_DETECTED) {
                s_status.state = CRY_STATE_VOICE;
                ESP_LOGI(TAG, "Cry alert cleared (voice still active)");
            }
        }
    }
}

/* ── Public API ───────────────────────────────────────────── */

esp_err_t cry_detector_init(void)
{
    /* Generate Hann window (sized for actual frame, not FFT size) */
    generate_hann_window(s_fft_window, FRAME_SAMPLES);

    /* Initialize FFT tables */
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FFT init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_positive_count = 0;
    s_negative_count = 0;
    s_initialized = true;

    ESP_LOGI(TAG, "Cry detector initialized");
    ESP_LOGI(TAG, "  FFT size: %d, Sample rate: %d Hz", FFT_SIZE, SAMPLE_RATE);
    ESP_LOGI(TAG, "  Cry band: %d-%d Hz, Threshold: %.0f%%",
             CRY_FREQ_LOW_HZ, CRY_FREQ_HIGH_HZ, CRY_BAND_RATIO_THRESHOLD * 100);
    ESP_LOGI(TAG, "  Confirm: %d frames, Clear: %d frames",
             CONFIRM_FRAMES, CLEAR_FRAMES);

    return ESP_OK;
}

void cry_detector_start_task(void)
{
    xTaskCreate(detection_task, "cry_det", 8192, NULL, 3, NULL);
}

void cry_detector_get_status(cry_detector_status_t *out)
{
    if (out) {
        *out = s_status;
    }
}
