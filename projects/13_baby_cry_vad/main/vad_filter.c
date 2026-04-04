/*
 * vad_filter.c — Voice Activity Detection filter
 *
 * ESP-SR VADNet Integration Research Findings:
 * =============================================
 * ESP-SR v2.x supports ESP32-C6 ONLY for WakeNet (wake word detection).
 * VADNet is bundled inside the Audio Front-End (AFE) pipeline, which only
 * supports ESP32, ESP32-S3, and ESP32-P4. There is NO standalone VADNet API
 * for ESP32-C6.
 *
 * The older WebRTC-based VAD (esp_vad.h from esp-adf/esp-sr) requires the
 * full esp-sr component which pulls in models and libraries that may not
 * have C6 pre-built binaries.
 *
 * Attempted approaches:
 * 1. espressif/esp-sr component with VADNet — NOT available on C6 (AFE only
 *    supports ESP32, S3, P4 per official docs)
 * 2. espressif/esp-sr with esp_vad.h (WebRTC VAD) — Component registry shows
 *    esp-sr targets include C6 for WakeNet only; the vad_create/vad_process
 *    API may not be exposed for C6 builds
 * 3. Energy-based VAD (this implementation) — Works on any target, no
 *    external dependencies, good enough for baby cry detection use case
 *
 * Implementation: Energy-Based VAD with Adaptive Threshold
 * =========================================================
 * - Computes short-term energy of each 30ms audio frame
 * - Maintains a running estimate of background noise energy
 * - Detects speech when frame energy exceeds noise_floor × threshold_ratio
 * - Uses hangover counter to prevent rapid on/off switching
 * - Tuned for baby cry detection (higher energy than typical speech)
 */

#include "vad_filter.h"
#include "esp_log.h"

#include <string.h>
#include <math.h>

static const char *TAG = "vad";

/* ── Configuration ────────────────────────────────────────── */

/* Energy threshold: frame_energy must exceed noise_floor * this ratio */
#define VAD_THRESHOLD_RATIO     4.0f

/* Noise floor adaptation rate (0..1): smaller = slower adaptation */
#define NOISE_ADAPT_RATE        0.02f

/* Minimum noise floor to prevent divide-by-zero and overly sensitive detection */
#define MIN_NOISE_FLOOR         50.0f

/* Hangover: keep VAD active for this many silent frames after last speech frame */
#define HANGOVER_FRAMES         6   /* 6 × 30ms = 180ms */

/* Minimum energy to consider as possible speech (absolute threshold) */
#define MIN_SPEECH_ENERGY       200.0f

/* ── State ────────────────────────────────────────────────── */

static vad_impl_t s_impl = VAD_IMPL_ENERGY;
static float s_noise_floor = 500.0f;  /* Initial estimate */
static int s_hangover = 0;
static bool s_speech_active = false;
static bool s_initialized = false;

/* ── Energy computation ───────────────────────────────────── */

static float compute_frame_energy(const int16_t *samples, size_t count)
{
    if (count == 0) return 0.0f;
    int64_t sum_sq = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = samples[i];
        sum_sq += s * s;
    }
    return sqrtf((float)sum_sq / count);
}

/* ── Public API ───────────────────────────────────────────── */

esp_err_t vad_filter_init(void)
{
    s_impl = VAD_IMPL_ENERGY;
    s_noise_floor = 500.0f;
    s_hangover = 0;
    s_speech_active = false;
    s_initialized = true;

    ESP_LOGI(TAG, "VAD initialized (energy-based)");
    ESP_LOGI(TAG, "  Threshold ratio: %.1f", VAD_THRESHOLD_RATIO);
    ESP_LOGI(TAG, "  Hangover frames: %d (%d ms)", HANGOVER_FRAMES,
             HANGOVER_FRAMES * 30);
    ESP_LOGW(TAG, "ESP-SR VADNet not available on ESP32-C6 (AFE supports ESP32/S3/P4 only)");
    ESP_LOGI(TAG, "Using energy-based VAD as fallback — suitable for baby cry detection");

    return ESP_OK;
}

vad_result_t vad_filter_process(const int16_t *samples, size_t count)
{
    if (!s_initialized || !samples || count == 0) {
        return VAD_RESULT_SILENCE;
    }

    float energy = compute_frame_energy(samples, count);

    /* Determine if this frame is speech */
    bool is_speech = (energy > s_noise_floor * VAD_THRESHOLD_RATIO) &&
                     (energy > MIN_SPEECH_ENERGY);

    if (is_speech) {
        /* Speech detected: reset hangover counter */
        s_hangover = HANGOVER_FRAMES;
        s_speech_active = true;
    } else {
        /* No speech: update noise floor estimate (only during silence) */
        if (s_hangover <= 0) {
            /* Adapt noise floor slowly toward current energy */
            s_noise_floor = s_noise_floor * (1.0f - NOISE_ADAPT_RATE) +
                            energy * NOISE_ADAPT_RATE;
            if (s_noise_floor < MIN_NOISE_FLOOR) {
                s_noise_floor = MIN_NOISE_FLOOR;
            }
        }

        /* Decrement hangover */
        if (s_hangover > 0) {
            s_hangover--;
        }

        if (s_hangover <= 0) {
            s_speech_active = false;
        }
    }

    return s_speech_active ? VAD_RESULT_SPEECH : VAD_RESULT_SILENCE;
}

vad_impl_t vad_filter_get_impl(void)
{
    return s_impl;
}

bool vad_filter_is_speech(void)
{
    return s_speech_active;
}
