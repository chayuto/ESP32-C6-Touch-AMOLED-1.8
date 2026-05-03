/*
 * noise_player.c — Continuous-playback noise generator for sleep/soothing.
 *
 * Generates White / Pink / Brown / Womb noise on the fly through the
 * existing ES8311 + I2S TX channel owned by audio_player. Coordination
 * is exclusive: starting noise calls audio_player_yield_to_noise() so the
 * song path stops writing; stopping noise calls audio_player_resume_from_noise().
 *
 * Phase 1: engine + state machine + linear fade-in/fade-out + auto-off timer.
 * Hard swap on voice change; UI integration and cross-fade land in later phases.
 */

#include "noise_player.h"
#include "audio_player.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

static const char *TAG = "noise_player";

#define SAMPLE_RATE     16000
#define CHUNK_SAMPLES   256                       /* 16 ms per chunk */
#define CHUNKS_PER_SEC  (SAMPLE_RATE / CHUNK_SAMPLES) /* = 62 (rounded) */

#define FADE_IN_SEC          3
#define FADE_OUT_GENTLE_SEC  12   /* timer-expired: gentle for sleeping baby */
#define FADE_OUT_FAST_SEC    1    /* user-stop: snappy, parent wants silence now */

#ifndef CONFIG_NOISE_VOL_CAP
#define CONFIG_NOISE_VOL_CAP 80
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ── Shared state (volatile: written by API, read by task) ────────────── */

static volatile bool          s_play_req      = false;
static volatile bool          s_stop_req      = false;
static volatile bool          s_fast_stop     = false;  /* true = user stop, false = timer */
static volatile noise_voice_t s_voice         = NOISE_VOICE_PINK;
static volatile uint8_t       s_vol_target    = 60;   /* % of cap */
static volatile uint16_t      s_timer_target_sec = NOISE_TIMER_INF;

static volatile bool          s_playing       = false;
static volatile uint16_t      s_timer_remaining_sec = NOISE_TIMER_INF;

/* ── Task-local synth state (no contention — read/written only by noise_task) */

static int16_t s_buf[CHUNK_SAMPLES * 2];

/* White */
static uint32_t s_white_state = 0xC0FFEEu;

/* Pink (Voss-McCartney, 7 octaves) */
static int16_t  s_pink_rows[7];
static int32_t  s_pink_run = 0;
static uint32_t s_pink_n   = 0;

/* Brown (leaky integrator, Q15) */
static int32_t  s_brown_y = 0;

/* Womb (uses brown_y above, plus LFO + heartbeat phase accumulators) */
static float    s_womb_t_mod = 0.0f;
static float    s_womb_t_hb  = 0.0f;

/* ── Voice synthesis ──────────────────────────────────────────────────── */

static inline int16_t white_sample(void)
{
    /* xorshift32 → take top 16 bits as signed sample */
    uint32_t x = s_white_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_white_state = x;
    return (int16_t)(x >> 16);
}

static inline int16_t pink_sample(void)
{
    s_pink_n++;
    int k = __builtin_ctz(s_pink_n);
    if (k < 7) {
        int16_t new_val = white_sample();
        s_pink_run += new_val - s_pink_rows[k];
        s_pink_rows[k] = new_val;
    }
    /* /7 normalises to roughly the same RMS as white. */
    return (int16_t)(s_pink_run / 7);
}

static inline int16_t brown_sample(void)
{
    /* y = (alpha * y + beta * white) >> 15
     * alpha = 32440/32768 = 0.99
     * beta  =   800/32768 = 0.024
     */
    s_brown_y = (32440 * s_brown_y + 800 * (int32_t)white_sample()) >> 15;
    if (s_brown_y >  20000) s_brown_y =  20000;
    if (s_brown_y < -20000) s_brown_y = -20000;
    return (int16_t)s_brown_y;
}

static inline int16_t womb_sample(void)
{
    /* Brown noise shaped by ~4 Hz amplitude wash + occasional 60 Hz heartbeat. */
    int16_t s = brown_sample();

    /* (0.5 + 0.5*sin(2π·4·t))^2 → range 0..1, peak around 1, valleys ~0. */
    float e = sinf(2.0f * M_PI * 4.0f * s_womb_t_mod);
    e = 0.5f + 0.5f * e;
    e = e * e;
    float out = (float)s * e;

    /* Heartbeat: lub-dub at ~70 BPM (cycle = 0.860 s). Two short 60 Hz
     * thumps with windowed envelopes. Amplitudes deliberately modest so
     * the heartbeat colours the texture rather than dominating it. */
    const float lub_t  = 0.00f;
    const float dub_t  = 0.30f;
    const float thump_dur = 0.06f;
    const float hb_amp = 4500.0f;

    if (s_womb_t_hb >= lub_t && s_womb_t_hb < lub_t + thump_dur) {
        float u = (s_womb_t_hb - lub_t) / thump_dur;     /* 0..1 */
        float w = sinf(M_PI * u);                         /* 0..1..0 window */
        out += hb_amp * w * sinf(2.0f * M_PI * 60.0f * s_womb_t_hb);
    } else if (s_womb_t_hb >= dub_t && s_womb_t_hb < dub_t + thump_dur) {
        float u = (s_womb_t_hb - dub_t) / thump_dur;
        float w = sinf(M_PI * u);
        out += hb_amp * 0.7f * w * sinf(2.0f * M_PI * 60.0f * s_womb_t_hb);
    }

    s_womb_t_mod += 1.0f / (float)SAMPLE_RATE;
    s_womb_t_hb  += 1.0f / (float)SAMPLE_RATE;
    if (s_womb_t_mod > 1.0f)  s_womb_t_mod -= 1.0f;
    if (s_womb_t_hb  > 0.860f) s_womb_t_hb -= 0.860f;

    if (out >  32000.0f) out =  32000.0f;
    if (out < -32000.0f) out = -32000.0f;
    return (int16_t)out;
}

static inline int16_t synth_sample(noise_voice_t v)
{
    switch (v) {
    case NOISE_VOICE_WHITE: return white_sample();
    case NOISE_VOICE_PINK:  return pink_sample();
    case NOISE_VOICE_BROWN: return brown_sample();
    case NOISE_VOICE_WOMB:  return womb_sample();
    default:                return 0;
    }
}

/* Per-voice gain (Q12; multiply then >>12). Calibrated so each voice
 * lands at roughly the same RMS (~11k) at full fade — White is the
 * reference. The leaky-integrator Brown only reaches RMS ~3.3k on its
 * own and Womb's envelope drops it another ~4 dB, so they need 2.5–3.5×
 * boost to match White. Pink at /7 has RMS ~7k so 1.5× is enough; the
 * old 4.4× was hard-clipping into the post-shape limiter. */
static int voice_gain_q12(noise_voice_t v)
{
    switch (v) {
    case NOISE_VOICE_WHITE: return  2503;   /* ~0.61 — raw white peaks at ±32k */
    case NOISE_VOICE_PINK:  return  6144;   /* ~1.5  — was clipping at 4.4× */
    case NOISE_VOICE_BROWN: return 10240;   /* ~2.5  — Brown RMS only ~3.3k raw */
    case NOISE_VOICE_WOMB:  return 14336;   /* ~3.5  — Brown × ~0.625 envelope */
    default:                return  4096;
    }
}

static void reset_synth_state(void)
{
    s_white_state = 0xC0FFEEu ^ (uint32_t)esp_timer_get_time();
    if (s_white_state == 0) s_white_state = 0xDEADBEEFu;
    memset(s_pink_rows, 0, sizeof(s_pink_rows));
    s_pink_run = 0;
    s_pink_n   = 0;
    s_brown_y  = 0;
    s_womb_t_mod = 0.0f;
    s_womb_t_hb  = 0.0f;
}

/* ── Audio task ───────────────────────────────────────────────────────── */

static void noise_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "noise_task started");

    /* Fade gain in 0..1 (scaled by vol_target%/100 effectively). Using float
     * here keeps the per-chunk ramp arithmetic simple; the inner loop is int. */
    float gain = 0.0f;
    float gain_target = 0.0f;
    float gain_step_per_chunk = 0.0f;
    int   chunk_count_in_sec  = 0;

    while (1) {
        if (!s_playing && !s_play_req) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (s_play_req) {
            audio_player_yield_to_noise();
            reset_synth_state();
            s_play_req = false;
            s_playing  = true;
            s_timer_remaining_sec = s_timer_target_sec;
            chunk_count_in_sec = 0;
            gain = 0.0f;
            gain_target = (float)s_vol_target / 100.0f;
            gain_step_per_chunk = gain_target / (float)(FADE_IN_SEC * CHUNKS_PER_SEC);
            ESP_LOGI(TAG, "play voice=%s vol=%u%% timer=%us",
                     noise_player_voice_name(s_voice), s_vol_target,
                     (s_timer_target_sec == NOISE_TIMER_INF) ? 0 : s_timer_target_sec);
        }

        /* ── Synthesise one chunk ─────────────────────────────────── */
        noise_voice_t v = s_voice;
        int vg = voice_gain_q12(v);
        /* Combine voice gain and fade into one Q12 multiplier per chunk. */
        int chunk_gain_q12 = (int)((float)vg * gain);

        for (int i = 0; i < CHUNK_SAMPLES; i++) {
            int32_t raw = synth_sample(v);
            int32_t shaped = (raw * chunk_gain_q12) >> 12;
            if (shaped >  32000) shaped =  32000;
            if (shaped < -32000) shaped = -32000;
            int16_t s = (int16_t)shaped;
            s_buf[i * 2]     = s;
            s_buf[i * 2 + 1] = s;
        }
        audio_player_write_stereo(s_buf, CHUNK_SAMPLES);

        /* ── Per-chunk housekeeping ───────────────────────────────── */
        chunk_count_in_sec++;
        if (chunk_count_in_sec >= CHUNKS_PER_SEC) {
            chunk_count_in_sec = 0;
            if (s_timer_remaining_sec != NOISE_TIMER_INF && s_timer_remaining_sec > 0) {
                s_timer_remaining_sec--;
                if (s_timer_remaining_sec == 0) {
                    ESP_LOGI(TAG, "timer expired — fading out");
                    s_fast_stop = false;     /* gentle fade for sleeping baby */
                    s_stop_req = true;
                }
            }
        }

        /* Track live volume changes (UI may move the slider while running) */
        float vol_target_now = (float)s_vol_target / 100.0f;

        if (s_stop_req) {
            if (gain_target != 0.0f) {
                gain_target = 0.0f;
                int fade_sec = s_fast_stop ? FADE_OUT_FAST_SEC : FADE_OUT_GENTLE_SEC;
                gain_step_per_chunk = gain / (float)(fade_sec * CHUNKS_PER_SEC);
                if (gain_step_per_chunk <= 0.0f) gain_step_per_chunk = 1.0f / 64.0f;
            }
        } else if (gain_target != vol_target_now) {
            /* Volume slider moved — retarget the ramp at fade-in slope so
             * the change is audible but not instant. */
            gain_target = vol_target_now;
            gain_step_per_chunk = vol_target_now / (float)(FADE_IN_SEC * CHUNKS_PER_SEC);
            if (gain_step_per_chunk <= 0.0f) gain_step_per_chunk = 1.0f / 64.0f;
        }

        if (gain < gain_target) {
            gain += gain_step_per_chunk;
            if (gain > gain_target) gain = gain_target;
        } else if (gain > gain_target) {
            gain -= gain_step_per_chunk;
            if (gain < gain_target) gain = gain_target;
        }

        if (s_stop_req && gain <= 0.0001f) {
            gain = 0.0f;
            s_playing = false;
            s_stop_req = false;
            s_fast_stop = false;
            audio_player_resume_from_noise();
            ESP_LOGI(TAG, "stopped");
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

esp_err_t noise_player_init(void)
{
    BaseType_t ok = xTaskCreate(noise_task, "noise", 4096, NULL, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create noise_task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "initialised (vol_cap=%d%%)", CONFIG_NOISE_VOL_CAP);
    return ESP_OK;
}

void noise_player_play(noise_voice_t voice, uint16_t timer_min)
{
    if (voice >= NOISE_VOICE_COUNT) voice = NOISE_VOICE_PINK;
    s_voice = voice;
    s_timer_target_sec = (timer_min == NOISE_TIMER_INF) ? NOISE_TIMER_INF
                                                        : (uint16_t)(timer_min * 60u);
    s_play_req = true;
    s_stop_req = false;
}

void noise_player_stop(void)
{
    if (s_playing || s_play_req) {
        s_play_req = false;
        s_fast_stop = true;
        s_stop_req = true;
    }
}

void noise_player_set_voice(noise_voice_t voice)
{
    if (voice >= NOISE_VOICE_COUNT) return;
    s_voice = voice;
}

void noise_player_set_volume(uint8_t vol)
{
    if (vol > CONFIG_NOISE_VOL_CAP) vol = CONFIG_NOISE_VOL_CAP;
    s_vol_target = vol;
}

void noise_player_set_timer(uint16_t min)
{
    s_timer_target_sec = (min == NOISE_TIMER_INF) ? NOISE_TIMER_INF
                                                  : (uint16_t)(min * 60u);
    s_timer_remaining_sec = s_timer_target_sec;
}

bool          noise_player_is_playing(void)     { return s_playing; }
bool          noise_player_is_stopping(void)    { return s_playing && s_stop_req; }
noise_voice_t noise_player_get_voice(void)      { return s_voice; }
uint8_t       noise_player_get_volume(void)     { return s_vol_target; }
uint16_t      noise_player_get_timer_remaining_sec(void) { return s_timer_remaining_sec; }

const char *noise_player_voice_name(noise_voice_t v)
{
    switch (v) {
    case NOISE_VOICE_WHITE: return "White";
    case NOISE_VOICE_PINK:  return "Pink";
    case NOISE_VOICE_BROWN: return "Brown";
    case NOISE_VOICE_WOMB:  return "Womb";
    default:                return "?";
    }
}
