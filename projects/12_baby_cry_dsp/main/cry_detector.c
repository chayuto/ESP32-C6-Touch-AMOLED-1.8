/*
 * cry_detector.c — Baby cry detection v3 (FFT + gated multi-feature scoring)
 *
 * v3 changes (data-driven from real household logging):
 *   - Hard gates: minimum RMS, minimum cry_ratio, maximum low_ratio
 *     → prevents scoring near-silence or bass-heavy sounds
 *   - Stricter harmonic check: higher thresholds + cry band must dominate bass
 *     → harmonic_pct was 69-96% on ALL sounds in v2 (useless)
 *   - Rebalanced scoring: cry_ratio & low_reject weighted higher,
 *     always-passing features (crest, periodicity) weighted lower
 *   - Higher thresholds: trigger=65, clear=45
 *     → v2 clear=25 was unreachable (even silence scored 50+)
 *   - More debug fields exported for CSV analysis
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
#define CRY_BIN_LOW         11        /* 344 Hz */
#define CRY_BIN_HIGH        17        /* 531 Hz */
#define LOW_BAND_HIGH       7         /* 0-219 Hz (sub-bass) */
#define HIGH_BAND_LOW       32        /* 1000 Hz */
#define HIGH_BAND_HIGH      112       /* 3500 Hz */

/* Adaptive noise floor */
#define INITIAL_NOISE_FLOOR      30.0f
#define NOISE_ADAPT_RATE         0.05f
#define MIN_NOISE_FLOOR          5.0f
#define VAD_NOISE_MULT           1.5f

/* ── Hard gates (must pass ALL before scoring) ──────────── */
#define GATE_MIN_RMS_MULT        2.5f    /* RMS must be > noise_floor * this */
#define GATE_MIN_CRY_RATIO       0.08f   /* Minimum cry band energy ratio */
#define GATE_MAX_LOW_RATIO        0.35f   /* Maximum bass energy (rejects adult speech) */

/* ── Harmonic verification (stricter than v2) ───────────── */
#define HARMONIC_MIN_RATIO       0.35f   /* 2nd harmonic >= 35% of F0 (was 20%) */
#define HARMONIC3_MIN_RATIO      0.20f   /* 3rd harmonic >= 20% of F0 (was 12%) */

/* ── Scoring thresholds ─────────────────────────────────── */
#define CRY_RATIO_THRESH_1       0.12f   /* Moderate cry band energy */
#define CRY_RATIO_THRESH_2       0.25f   /* Strong cry band energy */
#define HIGH_BAND_MIN_RATIO      0.08f   /* Baby cry formant region */
#define CREST_MIN                6.0f    /* Raised from 4.0 — was always passing */
#define LOW_BAND_MAX_RATIO       0.12f   /* Stricter low rejection for scoring */

/* Scoring weights — rebalanced based on data */
#define SCORE_CRY_RATIO_1        20      /* cry band present (was 15) */
#define SCORE_CRY_RATIO_2        10      /* cry band strong (bonus) */
#define SCORE_F0_HARMONIC        20      /* F0 + verified harmonics + cry dominant */
#define SCORE_F0_ONLY            5       /* F0 in range but no harmonics (was 8) */
#define SCORE_HIGH_BAND          10      /* formant energy present (was 15) */
#define SCORE_CREST              5       /* tonal (was 10 — always passed) */
#define SCORE_LOW_REJECT         20      /* little low-freq = not speech (was 10) */
#define SCORE_PERIODICITY        10      /* cry-pause pattern (was 15 — always passed) */

#define TRIGGER_THRESHOLD        65      /* Score to trigger (was 55) */
#define CLEAR_THRESHOLD          45      /* Score to clear (was 25 — unreachable!) */

/* State machine */
#define CONFIRM_COUNT            4
#define CLEAR_COUNT              4

/* Periodicity */
#define ENERGY_HISTORY_LEN       20
#define PERIODICITY_THRESHOLD    5       /* Raised from 3 — was always passing */

/* Analysis window */
#define ANALYSIS_SAMPLES    (SAMPLE_RATE * 3 / 2)  /* 1.5 seconds */

/* ── State ──────────────────────────────────────────────── */

static cry_detector_status_t s_status;
static int s_positive_count = 0;
static int s_negative_count = 0;
static float s_noise_floor = INITIAL_NOISE_FLOOR;

static float s_energy_history[ENERGY_HISTORY_LEN];
static int   s_energy_hist_idx = 0;
static int   s_energy_hist_count = 0;

static float s_fft_input[FFT_SIZE];
static float s_fft_real[FFT_SIZE];
static float s_fft_imag[FFT_SIZE];
static float s_magnitude[NUM_BINS + 1];
static float s_avg_spectrum[NUM_BINS + 1];
static float s_window[FFT_SIZE];
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

static float s_spectrum_peak = 0.01f;

static void spectrum_to_ui_bins(const float *mag, int num_mag_bins, uint8_t *out, int num_out)
{
    int bins_per_group = num_mag_bins / num_out;
    if (bins_per_group < 1) bins_per_group = 1;

    float group_vals[CRY_SPECTRUM_BINS];
    float frame_peak = 0.01f;

    for (int i = 0; i < num_out; i++) {
        int start = 1 + i * bins_per_group;
        int end = start + bins_per_group;
        if (end > num_mag_bins) end = num_mag_bins;
        if (i == num_out - 1) end = num_mag_bins;

        float sum = 0.0f;
        int count = 0;
        for (int b = start; b < end; b++) {
            sum += mag[b];
            count++;
        }
        group_vals[i] = count > 0 ? sum / count : 0.0f;
        if (group_vals[i] > frame_peak) frame_peak = group_vals[i];
    }

    if (frame_peak > s_spectrum_peak) {
        s_spectrum_peak = frame_peak;
    } else {
        s_spectrum_peak = s_spectrum_peak * 0.95f + frame_peak * 0.05f;
    }
    if (s_spectrum_peak < 0.01f) s_spectrum_peak = 0.01f;

    for (int i = 0; i < num_out; i++) {
        float normalized = group_vals[i] / s_spectrum_peak * 255.0f;
        if (normalized > 255.0f) normalized = 255.0f;
        out[i] = (uint8_t)normalized;
    }
}

/* ── Per-Frame Feature Extraction ──────────────────────── */

typedef struct {
    float cry_band_energy;
    float total_energy;
    float low_band_energy;
    float high_band_energy;
    float rms;
    float spectral_crest;
    int   f0_bin;
    bool  harmonic_verified;
    bool  cry_dominant;        /* cry band energy > low band energy */
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

    float cry_energy = 0.0f;
    float total_energy = 0.0f;
    float low_energy = 0.0f;
    float high_energy = 0.0f;
    float max_mag = 0.0f;
    float mag_sum = 0.0f;
    float f0_peak_mag = 0.0f;
    int f0_peak_bin = 0;

    for (int bin = 1; bin <= NUM_BINS; bin++) {
        float mag = sqrtf(s_fft_real[bin] * s_fft_real[bin] +
                          s_fft_imag[bin] * s_fft_imag[bin]);
        s_magnitude[bin] = mag;
        float e = mag * mag;
        total_energy += e;
        mag_sum += mag;
        if (mag > max_mag) max_mag = mag;

        if (bin >= CRY_BIN_LOW && bin <= CRY_BIN_HIGH) {
            cry_energy += e;
            if (mag > f0_peak_mag) {
                f0_peak_mag = mag;
                f0_peak_bin = bin;
            }
        }
        if (bin <= LOW_BAND_HIGH) {
            low_energy += e;
        }
        if (bin >= HIGH_BAND_LOW && bin <= HIGH_BAND_HIGH) {
            high_energy += e;
        }
    }

    result->cry_band_energy = cry_energy;
    result->total_energy = total_energy;
    result->low_band_energy = low_energy;
    result->high_band_energy = high_energy;
    result->f0_bin = f0_peak_bin;

    /* Baby cries have MORE energy in cry band than in sub-bass.
     * Adult speech has the opposite — strong fundamental below 250 Hz. */
    result->cry_dominant = (cry_energy > low_energy * 1.2f);

    float mean_mag = mag_sum / NUM_BINS;
    result->spectral_crest = (mean_mag > 1e-10f) ? (max_mag / mean_mag) : 0.0f;

    /* Stricter harmonic verification:
     * - Higher thresholds (35%/20% vs old 20%/12%)
     * - Only count if cry band dominates over bass */
    result->harmonic_verified = false;
    if (f0_peak_bin > 0 && f0_peak_mag > 1e-6f && result->cry_dominant) {
        int h2_bin = f0_peak_bin * 2;
        float h2_mag = 0.0f;
        if (h2_bin > 0 && h2_bin < NUM_BINS) {
            h2_mag = s_magnitude[h2_bin];
            if (h2_bin > 1 && s_magnitude[h2_bin - 1] > h2_mag)
                h2_mag = s_magnitude[h2_bin - 1];
            if (h2_bin < NUM_BINS - 1 && s_magnitude[h2_bin + 1] > h2_mag)
                h2_mag = s_magnitude[h2_bin + 1];
        }

        int h3_bin = f0_peak_bin * 3;
        float h3_mag = 0.0f;
        if (h3_bin > 0 && h3_bin < NUM_BINS) {
            h3_mag = s_magnitude[h3_bin];
            if (h3_bin > 1 && s_magnitude[h3_bin - 1] > h3_mag)
                h3_mag = s_magnitude[h3_bin - 1];
            if (h3_bin < NUM_BINS - 1 && s_magnitude[h3_bin + 1] > h3_mag)
                h3_mag = s_magnitude[h3_bin + 1];
        }

        result->harmonic_verified =
            (h2_mag >= f0_peak_mag * HARMONIC_MIN_RATIO) &&
            (h3_mag >= f0_peak_mag * HARMONIC3_MIN_RATIO);
    }
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

/* ── Gated Multi-Feature Scoring ────────────────────────── */

static int compute_cry_score(float cry_ratio, float low_ratio, float high_ratio,
                             float crest, bool harmonic_verified, bool f0_found,
                             bool cry_dominant, int periodicity)
{
    int score = 0;

    /* Cry band energy — the most discriminating feature */
    if (cry_ratio > CRY_RATIO_THRESH_1) score += SCORE_CRY_RATIO_1;
    if (cry_ratio > CRY_RATIO_THRESH_2) score += SCORE_CRY_RATIO_2;

    /* F0 with harmonics — only counts if cry band dominates bass */
    if (f0_found && harmonic_verified && cry_dominant) {
        score += SCORE_F0_HARMONIC;
    } else if (f0_found && cry_dominant) {
        score += SCORE_F0_ONLY;
    }

    /* High-band formant energy (baby F1 ~1100 Hz) */
    if (high_ratio > HIGH_BAND_MIN_RATIO) score += SCORE_HIGH_BAND;

    /* Spectral crest — raised threshold, lower weight */
    if (crest > CREST_MIN) score += SCORE_CREST;

    /* Low-frequency rejection — strongest penalty/reward */
    if (low_ratio < LOW_BAND_MAX_RATIO) score += SCORE_LOW_REJECT;

    /* Cry-pause periodicity — raised threshold */
    if (periodicity >= PERIODICITY_THRESHOLD) score += SCORE_PERIODICITY;

    return score;
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

        /* Zero debug fields for silence */
        s_status.score = 0;
        s_status.low_ratio = 0;
        s_status.high_ratio = 0;
        s_status.crest = 0;
        s_status.harmonic_pct = 0;
        s_status.f0_hz = 0;
        s_status.cry_dominant = false;
        s_status.gated = true;  /* gated by VAD */

        memset(s_avg_spectrum, 0, sizeof(s_avg_spectrum));
        spectrum_to_ui_bins(s_avg_spectrum, NUM_BINS, s_status.spectrum, CRY_SPECTRUM_BINS);

        ESP_LOGD(TAG, "RMS=%.0f (silence, nf=%.0f)", block_rms, s_noise_floor);
        return false;
    }

    /* FFT analysis on overlapping frames */
    float total_cry_ratio = 0.0f;
    float total_low_ratio = 0.0f;
    float total_high_ratio = 0.0f;
    float total_crest = 0.0f;
    int   harmonic_yes_count = 0;
    int   f0_found_count = 0;
    int   cry_dominant_count = 0;
    int   frame_count = 0;
    float f0_bin_sum = 0.0f;
    memset(s_avg_spectrum, 0, sizeof(s_avg_spectrum));

    for (size_t offset = 0; offset + FFT_SIZE <= num_samples; offset += HOP_SIZE) {
        frame_result_t result;
        analyze_frame(&audio[offset], &result);

        if (result.total_energy > 1e-10f) {
            total_cry_ratio += result.cry_band_energy / result.total_energy;
            total_low_ratio += result.low_band_energy / result.total_energy;
            total_high_ratio += result.high_band_energy / result.total_energy;
        }
        total_crest += result.spectral_crest;
        if (result.f0_bin > 0) {
            f0_found_count++;
            f0_bin_sum += result.f0_bin;
        }
        if (result.harmonic_verified) harmonic_yes_count++;
        if (result.cry_dominant) cry_dominant_count++;

        for (int b = 1; b <= NUM_BINS; b++) {
            s_avg_spectrum[b] += s_magnitude[b];
        }
        frame_count++;
    }

    if (frame_count > 0) {
        for (int b = 1; b <= NUM_BINS; b++) {
            s_avg_spectrum[b] /= frame_count;
        }
    }
    spectrum_to_ui_bins(s_avg_spectrum, NUM_BINS, s_status.spectrum, CRY_SPECTRUM_BINS);

    /* Average features */
    float avg_cry_ratio = (frame_count > 0) ? total_cry_ratio / frame_count : 0.0f;
    float avg_low_ratio = (frame_count > 0) ? total_low_ratio / frame_count : 0.0f;
    float avg_high_ratio = (frame_count > 0) ? total_high_ratio / frame_count : 0.0f;
    float avg_crest = (frame_count > 0) ? total_crest / frame_count : 0.0f;
    bool harmonics_ok = (frame_count > 0) && (harmonic_yes_count > frame_count / 3);
    bool f0_ok = (frame_count > 0) && (f0_found_count > frame_count / 2);
    bool cry_dom = (frame_count > 0) && (cry_dominant_count > frame_count / 2);
    float avg_f0_bin = (f0_found_count > 0) ? f0_bin_sum / f0_found_count : 0.0f;

    /* Store debug fields */
    s_status.cry_band_ratio = avg_cry_ratio;
    s_status.low_ratio = avg_low_ratio;
    s_status.high_ratio = avg_high_ratio;
    s_status.crest = avg_crest;
    s_status.harmonic_pct = (frame_count > 0) ? (harmonic_yes_count * 100 / frame_count) : 0;
    s_status.f0_hz = (int)(avg_f0_bin * SAMPLE_RATE / FFT_SIZE + 0.5f);
    s_status.cry_dominant = cry_dom;

    /* Record energy for periodicity */
    s_energy_history[s_energy_hist_idx] = block_rms;
    s_energy_hist_idx = (s_energy_hist_idx + 1) % ENERGY_HISTORY_LEN;
    if (s_energy_hist_count < ENERGY_HISTORY_LEN) s_energy_hist_count++;

    int periodicity = detect_periodicity();
    s_status.periodicity = periodicity;

    /* ── HARD GATES — must pass ALL before scoring ────── */
    bool gated = false;
    const char *gate_reason = NULL;

    if (block_rms < s_noise_floor * GATE_MIN_RMS_MULT) {
        gated = true;
        gate_reason = "low_rms";
    } else if (avg_cry_ratio < GATE_MIN_CRY_RATIO) {
        gated = true;
        gate_reason = "low_cry";
    } else if (avg_low_ratio > GATE_MAX_LOW_RATIO) {
        gated = true;
        gate_reason = "high_bass";
    }

    s_status.gated = gated;

    int score;
    if (gated) {
        score = 0;
    } else {
        score = compute_cry_score(avg_cry_ratio, avg_low_ratio, avg_high_ratio,
                                  avg_crest, harmonics_ok, f0_ok, cry_dom, periodicity);
    }
    s_status.score = score;

    /* Decision with hysteresis */
    bool is_cry;
    if (s_status.state == CRY_STATE_DETECTED) {
        is_cry = (score >= CLEAR_THRESHOLD);
    } else {
        is_cry = (score >= TRIGGER_THRESHOLD);
    }

    ESP_LOGI(TAG, "RMS=%.0f s=%d cr=%.2f lo=%.2f hi=%.2f C=%.1f h=%d%% f0=%dHz dom=%d prd=%d %s%s-> %s",
             block_rms, score, avg_cry_ratio, avg_low_ratio, avg_high_ratio,
             avg_crest, s_status.harmonic_pct, s_status.f0_hz,
             cry_dom ? 1 : 0, periodicity,
             gated ? "GATED:" : "", gated ? gate_reason : "",
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

    s_status.pos_streak = s_positive_count;
    s_status.neg_streak = s_negative_count;
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

    ESP_LOGI(TAG, "Cry detector v3: gated scoring (FFT=%d, cry=%d-%dHz, trig>=%d, clr>=%d)",
             FFT_SIZE, CRY_BIN_LOW * SAMPLE_RATE / FFT_SIZE,
             CRY_BIN_HIGH * SAMPLE_RATE / FFT_SIZE,
             TRIGGER_THRESHOLD, CLEAR_THRESHOLD);
    ESP_LOGI(TAG, "Gates: RMS>nf*%.1f, cry_ratio>%.2f, low_ratio<%.2f",
             GATE_MIN_RMS_MULT, GATE_MIN_CRY_RATIO, GATE_MAX_LOW_RATIO);
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
