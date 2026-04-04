/*
 * cry_detector.c — Baby cry detection using pure DSP (FFT + adaptive threshold + periodicity)
 *
 * Algorithm:
 *   1. Adaptive energy VAD: learns noise floor, skips silence
 *   2. 512-point FFT on overlapping frames → magnitude spectrum
 *   3. Compute cry-band (250-600 Hz) energy ratio
 *   4. Adaptive threshold: adjusts based on ambient noise
 *   5. Periodicity detection: cry-pause pattern at 0.5-1 Hz
 *   6. Temporal smoothing: consecutive detections before alert/clear
 */

#include "cry_detector.h"
#include "audio_capture.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "cry_det";

/* ── Configuration ──────────────────────────────────────── */

#define SAMPLE_RATE         16000
#define FFT_SIZE            512
#define HOP_SIZE            256
#define NUM_BINS            (FFT_SIZE / 2)

/* Frequency bins: bin_freq = bin * 16000 / 512 = bin * 31.25 Hz */
#define CRY_BIN_LOW         8         /* 250 Hz */
#define CRY_BIN_HIGH        19        /* ~594 Hz */

/* Adaptive thresholds */
#define INITIAL_NOISE_FLOOR      300.0f
#define NOISE_ADAPT_RATE         0.03f     /* Slow adaptation during silence */
#define MIN_NOISE_FLOOR          50.0f
#define VAD_NOISE_MULT           2.0f      /* Voice activity = noise_floor × this */
#define CRY_BAND_RATIO_BASE      0.10f    /* Base cry band ratio threshold (2x more sensitive) */
#define CONFIRM_COUNT            2          /* Faster detection */
#define CLEAR_COUNT              3

/* Periodicity */
#define ENERGY_HISTORY_LEN       20
#define PERIODICITY_THRESHOLD    2         /* Lowered for sensitivity */

/* Analysis window */
#define ANALYSIS_SAMPLES    (SAMPLE_RATE * 3 / 2)  /* 1.5 seconds */

/* ── State ──────────────────────────────────────────────── */

static cry_detector_status_t s_status;
static int s_positive_count = 0;
static int s_negative_count = 0;
static float s_noise_floor = INITIAL_NOISE_FLOOR;

/* Energy history for periodicity */
static float s_energy_history[ENERGY_HISTORY_LEN];
static int   s_energy_hist_idx = 0;
static int   s_energy_hist_count = 0;

/* FFT workspace */
static float s_fft_input[FFT_SIZE];
static float s_fft_real[FFT_SIZE];
static float s_fft_imag[FFT_SIZE];
static float s_magnitude[NUM_BINS + 1];

/* Averaged spectrum for UI (accumulated across frames in one analysis block) */
static float s_avg_spectrum[NUM_BINS + 1];

/* Hanning window */
static float s_window[FFT_SIZE];

/* Audio analysis buffer */
static int16_t s_analysis_buf[ANALYSIS_SAMPLES];

/* ── Hanning Window ─────────────────────────────────────── */

static void init_hanning_window(void)
{
    for (int i = 0; i < FFT_SIZE; i++) {
        s_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }
}

/* ── Radix-2 FFT ────────────────────────────────────────── */

static unsigned int bit_reverse(unsigned int x, int log2n)
{
    unsigned int result = 0;
    for (int i = 0; i < log2n; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

static void fft_compute(const float *input, float *out_real, float *out_imag, int n)
{
    int log2n = 0;
    for (int tmp = n; tmp > 1; tmp >>= 1) log2n++;

    for (int i = 0; i < n; i++) {
        int j = bit_reverse(i, log2n);
        out_real[i] = input[j];
        out_imag[i] = 0.0f;
    }

    for (int s = 1; s <= log2n; s++) {
        int m = 1 << s;
        int m2 = m >> 1;
        float w_real = cosf(-2.0f * M_PI / m);
        float w_imag = sinf(-2.0f * M_PI / m);

        for (int k = 0; k < n; k += m) {
            float wn_real = 1.0f;
            float wn_imag = 0.0f;

            for (int j = 0; j < m2; j++) {
                int e = k + j;
                int o = k + j + m2;

                float t_real = wn_real * out_real[o] - wn_imag * out_imag[o];
                float t_imag = wn_real * out_imag[o] + wn_imag * out_real[o];

                out_real[o] = out_real[e] - t_real;
                out_imag[o] = out_imag[e] - t_imag;
                out_real[e] += t_real;
                out_imag[e] += t_imag;

                float new_wr = wn_real * w_real - wn_imag * w_imag;
                float new_wi = wn_real * w_imag + wn_imag * w_real;
                wn_real = new_wr;
                wn_imag = new_wi;
            }
        }
    }
}

/* ── Spectrum to UI bins ────────────────────────────────── */

static void spectrum_to_ui_bins(const float *mag, int num_mag_bins, uint8_t *out, int num_out)
{
    /* Map NUM_BINS FFT magnitude bins → CRY_SPECTRUM_BINS for display */
    /* Use logarithmic grouping: lower frequencies get more resolution */
    float max_val = 1.0f;

    /* Group FFT bins into display bins */
    int bins_per_group = num_mag_bins / num_out;
    if (bins_per_group < 1) bins_per_group = 1;

    for (int i = 0; i < num_out; i++) {
        int start = 1 + i * bins_per_group;  /* skip DC */
        int end = start + bins_per_group;
        if (end > num_mag_bins) end = num_mag_bins;
        if (i == num_out - 1) end = num_mag_bins; /* last bin gets remainder */

        float sum = 0.0f;
        for (int b = start; b < end; b++) {
            sum += mag[b];
        }
        float avg = sum / (end - start);
        if (avg > max_val) max_val = avg;
        out[i] = 0; /* temporary */
        /* Store float temporarily via a trick — we'll normalize in second pass */
        ((float *)out)[0] = 0; /* can't do this, use separate array */
    }

    /* Two-pass: first find max, then normalize */
    float group_vals[CRY_SPECTRUM_BINS];
    max_val = 0.001f;
    for (int i = 0; i < num_out; i++) {
        int start = 1 + i * bins_per_group;
        int end = start + bins_per_group;
        if (end > num_mag_bins) end = num_mag_bins;
        if (i == num_out - 1) end = num_mag_bins;

        float sum = 0.0f;
        for (int b = start; b < end; b++) {
            sum += mag[b];
        }
        group_vals[i] = sum / (end - start);
        if (group_vals[i] > max_val) max_val = group_vals[i];
    }

    for (int i = 0; i < num_out; i++) {
        float normalized = group_vals[i] / max_val * 255.0f;
        out[i] = (uint8_t)(normalized > 255.0f ? 255 : normalized);
    }
}

/* ── Analyze One Frame ──────────────────────────────────── */

typedef struct {
    float cry_band_energy;
    float total_energy;
    float rms;
} frame_result_t;

static void analyze_frame(const int16_t *samples, frame_result_t *result)
{
    float rms_sum = 0.0f;
    for (int i = 0; i < FFT_SIZE; i++) {
        float s = (float)samples[i] / 32768.0f;
        rms_sum += s * s;
        s_fft_input[i] = s * s_window[i];
    }
    result->rms = sqrtf(rms_sum / FFT_SIZE);

    fft_compute(s_fft_input, s_fft_real, s_fft_imag, FFT_SIZE);

    /* Compute magnitude */
    float cry_energy = 0.0f;
    float total_energy = 0.0f;

    for (int bin = 1; bin <= NUM_BINS; bin++) {
        float mag = sqrtf(s_fft_real[bin] * s_fft_real[bin] +
                          s_fft_imag[bin] * s_fft_imag[bin]);
        s_magnitude[bin] = mag;
        float e = mag * mag;
        total_energy += e;
        if (bin >= CRY_BIN_LOW && bin <= CRY_BIN_HIGH) {
            cry_energy += e;
        }
    }

    result->cry_band_energy = cry_energy;
    result->total_energy = total_energy;
}

/* ── Periodicity Detection ──────────────────────────────── */

static int detect_periodicity(void)
{
    if (s_energy_hist_count < 4) return 0;

    int count = s_energy_hist_count < ENERGY_HISTORY_LEN ? s_energy_hist_count : ENERGY_HISTORY_LEN;
    float sum = 0.0f;
    for (int i = 0; i < count; i++) sum += s_energy_history[i];
    float mean = sum / count;

    int transitions = 0;
    bool prev_loud = (s_energy_history[0] > mean);
    for (int i = 1; i < count; i++) {
        bool loud = (s_energy_history[i] > mean);
        if (loud != prev_loud) transitions++;
        prev_loud = loud;
    }
    return transitions;
}

/* ── Main Analysis ──────────────────────────────────────── */

static bool analyze_audio_block(const int16_t *audio, size_t num_samples)
{
    /* Overall RMS */
    int64_t sum_sq = 0;
    for (size_t i = 0; i < num_samples; i++) {
        int32_t s = audio[i];
        sum_sq += s * s;
    }
    float block_rms = sqrtf((float)sum_sq / (float)num_samples);
    s_status.rms_energy = block_rms;

    /* Adaptive noise floor */
    float vad_threshold = s_noise_floor * VAD_NOISE_MULT;
    s_status.noise_floor = s_noise_floor;
    s_status.threshold = vad_threshold;

    if (block_rms < vad_threshold) {
        /* Silence — adapt noise floor */
        s_noise_floor = s_noise_floor * (1.0f - NOISE_ADAPT_RATE) +
                        block_rms * NOISE_ADAPT_RATE;
        if (s_noise_floor < MIN_NOISE_FLOOR) s_noise_floor = MIN_NOISE_FLOOR;

        s_energy_history[s_energy_hist_idx] = block_rms;
        s_energy_hist_idx = (s_energy_hist_idx + 1) % ENERGY_HISTORY_LEN;
        if (s_energy_hist_count < ENERGY_HISTORY_LEN) s_energy_hist_count++;

        /* Zero the spectrum for UI */
        memset(s_avg_spectrum, 0, sizeof(s_avg_spectrum));
        spectrum_to_ui_bins(s_avg_spectrum, NUM_BINS, s_status.spectrum, CRY_SPECTRUM_BINS);
        return false;
    }

    /* FFT analysis on overlapping frames */
    float total_cry_ratio = 0.0f;
    int frame_count = 0;
    memset(s_avg_spectrum, 0, sizeof(s_avg_spectrum));

    for (size_t offset = 0; offset + FFT_SIZE <= num_samples; offset += HOP_SIZE) {
        frame_result_t result;
        analyze_frame(&audio[offset], &result);

        if (result.total_energy > 1e-10f) {
            total_cry_ratio += result.cry_band_energy / result.total_energy;
        }

        /* Accumulate magnitude for averaged spectrum */
        for (int b = 1; b <= NUM_BINS; b++) {
            s_avg_spectrum[b] += s_magnitude[b];
        }
        frame_count++;
    }

    /* Average the spectrum */
    if (frame_count > 0) {
        for (int b = 1; b <= NUM_BINS; b++) {
            s_avg_spectrum[b] /= frame_count;
        }
    }

    /* Export spectrum to UI bins */
    spectrum_to_ui_bins(s_avg_spectrum, NUM_BINS, s_status.spectrum, CRY_SPECTRUM_BINS);

    float avg_cry_ratio = (frame_count > 0) ? total_cry_ratio / frame_count : 0.0f;
    s_status.cry_band_ratio = avg_cry_ratio;

    /* Record energy for periodicity */
    s_energy_history[s_energy_hist_idx] = block_rms;
    s_energy_hist_idx = (s_energy_hist_idx + 1) % ENERGY_HISTORY_LEN;
    if (s_energy_hist_count < ENERGY_HISTORY_LEN) s_energy_hist_count++;

    int periodicity = detect_periodicity();
    s_status.periodicity = periodicity;

    /* Decision with adaptive threshold */
    bool is_cry = (avg_cry_ratio > CRY_BAND_RATIO_BASE) &&
                  (periodicity >= PERIODICITY_THRESHOLD);

    ESP_LOGI(TAG, "RMS=%.0f nf=%.0f ratio=%.2f period=%d -> %s",
             block_rms, s_noise_floor, avg_cry_ratio, periodicity,
             is_cry ? "CRY" : "no");

    return is_cry;
}

/* ── State Machine ──────────────────────────────────────── */

static void update_state_machine(bool is_cry)
{
    if (is_cry) {
        s_positive_count++;
        s_negative_count = 0;
        if (s_positive_count >= CONFIRM_COUNT && s_status.state != CRY_STATE_DETECTED) {
            s_status.state = CRY_STATE_DETECTED;
            s_status.cry_count++;
            s_status.last_cry_time = esp_timer_get_time();
            ESP_LOGW(TAG, "*** BABY CRYING DETECTED (event #%lu) ***",
                     (unsigned long)s_status.cry_count);
        }
    } else {
        s_negative_count++;
        s_positive_count = 0;
        if (s_negative_count >= CLEAR_COUNT && s_status.state == CRY_STATE_DETECTED) {
            s_status.state = CRY_STATE_IDLE;
            ESP_LOGI(TAG, "Cry event ended");
        }
    }
}

/* ── Detection Task ─────────────────────────────────────── */

static void cry_detector_task(void *arg)
{
    ESP_LOGI(TAG, "Cry detector task started");

    while (1) {
        size_t rd = audio_capture_read(s_analysis_buf, ANALYSIS_SAMPLES);
        if (rd < ANALYSIS_SAMPLES) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bool is_cry = analyze_audio_block(s_analysis_buf, ANALYSIS_SAMPLES);
        update_state_machine(is_cry);
    }
}

/* ── Public API ─────────────────────────────────────────── */

esp_err_t cry_detector_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = CRY_STATE_IDLE;
    s_status.cry_bin_low = CRY_BIN_LOW * CRY_SPECTRUM_BINS / NUM_BINS;
    s_status.cry_bin_high = CRY_BIN_HIGH * CRY_SPECTRUM_BINS / NUM_BINS;
    s_noise_floor = INITIAL_NOISE_FLOOR;

    init_hanning_window();

    ESP_LOGI(TAG, "Cry detector initialized (FFT=%d, cry band %d-%d Hz, adaptive threshold)",
             FFT_SIZE, CRY_BIN_LOW * SAMPLE_RATE / FFT_SIZE,
             CRY_BIN_HIGH * SAMPLE_RATE / FFT_SIZE);
    return ESP_OK;
}

void cry_detector_start_task(void)
{
    xTaskCreate(cry_detector_task, "cry_det", 8192, NULL, 1, NULL);
}

void cry_detector_get_status(cry_detector_status_t *out)
{
    if (out) *out = s_status;
}
