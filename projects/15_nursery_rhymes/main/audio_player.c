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
#include "esp_timer.h"
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
static volatile bool s_yielded_to_noise = false;

/*
 * Transpose offset in semitones. +12 = one octave up.
 *
 * Song data is written at concert pitch (C4=middle C, ~262 Hz) which is
 * standard for sheet music but sounds muddy on the NS4150B tiny speaker.
 * Children's songs are naturally sung an octave higher (C5, ~523 Hz)
 * which also happens to be the sweet spot for small speaker frequency
 * response (500 Hz – 4 kHz). Two octaves up would work too but risks
 * sounding tinny on the highs (Itsy Bitsy Spider would peak at ~3 kHz).
 */
#define TRANSPOSE_SEMITONES  12

static volatile play_mode_t s_play_mode = PLAY_MODE_OFF;

/* Shuffle state: Fisher-Yates on a local index array */
static int  s_shuffle_order[20];
static int  s_shuffle_pos = 0;
static bool s_shuffle_init = false;

static uint32_t simple_rand(void)
{
    /* xorshift32 — seeded from esp_timer */
    static uint32_t state = 0;
    if (state == 0) state = (uint32_t)esp_timer_get_time();
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void shuffle_reset(int exclude)
{
    int n = g_song_count;
    for (int i = 0; i < n; i++) s_shuffle_order[i] = i;
    /* Fisher-Yates shuffle */
    for (int i = n - 1; i > 0; i--) {
        int j = simple_rand() % (i + 1);
        int tmp = s_shuffle_order[i];
        s_shuffle_order[i] = s_shuffle_order[j];
        s_shuffle_order[j] = tmp;
    }
    /* Move excluded song (just played) away from position 0 to avoid repeat */
    if (exclude >= 0 && n > 1 && s_shuffle_order[0] == exclude) {
        int swap = 1 + (simple_rand() % (n - 1));
        s_shuffle_order[0] = s_shuffle_order[swap];
        s_shuffle_order[swap] = exclude;
    }
    s_shuffle_pos = 0;
    s_shuffle_init = true;
}

static int next_song_for_mode(int just_finished)
{
    switch (s_play_mode) {
    case PLAY_MODE_LOOP_ONE:
        return just_finished;

    case PLAY_MODE_LOOP_ALL: {
        int next = just_finished + 1;
        if (next >= g_song_count) next = 0;
        return next;
    }

    case PLAY_MODE_SHUFFLE:
        if (!s_shuffle_init) shuffle_reset(just_finished);
        s_shuffle_pos++;
        if (s_shuffle_pos >= g_song_count) {
            shuffle_reset(just_finished);
        }
        return s_shuffle_order[s_shuffle_pos];

    default: /* PLAY_MODE_OFF */
        return -1;
    }
}

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

/* ── Realistic Tone Synthesis ─────────────────────────── */
/*
 * Music box / glockenspiel timbre with:
 *   - Harmonic-rich waveform (fundamental + overtones)
 *   - Duration-proportional ADSR envelope
 *   - Optional vibrato on sustained notes
 *   - Metric accent (strong/weak beats from time signature)
 */

#define CHUNK_SAMPLES  256
static int16_t s_stereo_buf[CHUNK_SAMPLES * 2];

/* Harmonic mix: music-box timbre. Amplitudes sum to ~1.0 */
#define H1_AMP  0.55f   /* fundamental */
#define H2_AMP  0.25f   /* 2nd harmonic (octave above) */
#define H3_AMP  0.13f   /* 3rd harmonic (octave + fifth) */
#define H4_AMP  0.07f   /* 4th harmonic (2 octaves above) */

/* Vibrato: gentle pitch wobble on long notes */
#define VIBRATO_RATE_HZ   4.5f    /* wobble speed */
#define VIBRATO_DEPTH     0.003f  /* ±0.3% pitch deviation */
#define VIBRATO_ONSET_MS  200     /* vibrato fades in after this */

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
 * Compute a single sample with harmonic-rich waveform + vibrato.
 * phase = current fundamental phase (radians)
 * vib_phase = vibrato LFO phase
 */
static inline float synth_sample(float phase, float vib_mod)
{
    float p = phase * vib_mod;
    return H1_AMP * sinf(p)
         + H2_AMP * sinf(p * 2.0f)
         + H3_AMP * sinf(p * 3.0f)
         + H4_AMP * sinf(p * 4.0f);
}

/*
 * Play a single tone with realistic envelope and timbre.
 *   freq:       fundamental frequency in Hz (0 = rest)
 *   dur_ms:     total note duration including articulation gap
 *   accent:     0.0–1.0, metric accent strength (1.0 = downbeat)
 * Returns true if stop was requested during playback.
 */
static bool play_note_ex(float freq, int dur_ms, float accent)
{
    if (freq <= 0.0f) {
        play_silence(dur_ms);
        return s_stop_req;
    }

    /* Articulation: sound for 88% of duration, silence for 12%
     * This creates natural phrasing (legato but not connected) */
    int sound_ms = (dur_ms * 88) / 100;
    int gap_ms = dur_ms - sound_ms;
    if (sound_ms < 30) { sound_ms = dur_ms; gap_ms = 0; }

    int total_samples = (SAMPLE_RATE * sound_ms) / 1000;

    /* Duration-proportional ADSR:
     *   Attack:  3% of duration, clamped 5–40ms
     *   Release: 8% of duration, clamped 15–80ms
     * Short notes get snappy attacks; long notes breathe. */
    int attack_ms = (sound_ms * 3) / 100;
    if (attack_ms < 5) attack_ms = 5;
    if (attack_ms > 40) attack_ms = 40;
    int release_ms = (sound_ms * 8) / 100;
    if (release_ms < 15) release_ms = 15;
    if (release_ms > 80) release_ms = 80;

    int attack_samples = (SAMPLE_RATE * attack_ms) / 1000;
    int release_samples = (SAMPLE_RATE * release_ms) / 1000;
    int release_start = total_samples - release_samples;
    if (release_start < attack_samples) release_start = attack_samples;

    /* Volume: base volume * accent factor
     * Downbeats (accent=1.0) play at full volume.
     * Off-beats (accent=0.6) play softer.
     * This creates the natural "pulse" of music. */
    float vol = (float)s_volume / 100.0f;
    float accent_vol = 0.75f + 0.25f * accent;  /* Range: 0.75x – 1.0x */
    float amplitude = vol * accent_vol * 22000.0f;

    /* Phase accumulators */
    float phase = 0.0f;
    float phase_inc = 2.0f * M_PI * freq / (float)SAMPLE_RATE;
    float vib_phase = 0.0f;
    float vib_phase_inc = 2.0f * M_PI * VIBRATO_RATE_HZ / (float)SAMPLE_RATE;
    int vibrato_onset_samples = (SAMPLE_RATE * VIBRATO_ONSET_MS) / 1000;

    int pos = 0;
    while (pos < total_samples) {
        if (s_stop_req) {
            /* Stop tapped mid-note — fade the current tone down over ~80 ms
             * so the speaker hits zero before the I2S DMA buffer drains.
             * Otherwise the tone truncates abruptly and feels sluggish
             * because the user hears ~90 ms of buffered audio before silence. */
            int fade_samples = SAMPLE_RATE * 80 / 1000;
            int written_in_fade = 0;
            while (written_in_fade < fade_samples) {
                int chunk = CHUNK_SAMPLES;
                if (written_in_fade + chunk > fade_samples) chunk = fade_samples - written_in_fade;
                for (int i = 0; i < chunk; i++) {
                    float t = (float)(written_in_fade + i) / (float)fade_samples;
                    float fade = 1.0f - t;
                    float raw = synth_sample(phase, 1.0f);
                    int16_t sample = (int16_t)(fade * amplitude * raw);
                    phase += phase_inc;
                    if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
                    s_stereo_buf[i * 2]     = sample;
                    s_stereo_buf[i * 2 + 1] = sample;
                }
                write_stereo(s_stereo_buf, chunk);
                written_in_fade += chunk;
            }
            return true;
        }

        int chunk = CHUNK_SAMPLES;
        if (pos + chunk > total_samples) chunk = total_samples - pos;

        for (int i = 0; i < chunk; i++) {
            int s = pos + i;

            /* ADSR envelope */
            float env;
            if (s < attack_samples) {
                /* Slightly curved attack (quadratic ease-in) for softer onset */
                float t = (float)s / (float)attack_samples;
                env = t * t;
            } else if (s >= release_start) {
                /* Cosine release for smooth fade */
                float t = (float)(s - release_start) / (float)release_samples;
                if (t > 1.0f) t = 1.0f;
                env = 0.5f * (1.0f + cosf(M_PI * t));
            } else {
                env = 1.0f;
            }

            /* Vibrato: fade in after onset, only on notes long enough */
            float vib_mod = 1.0f;
            if (sound_ms > 300 && s > vibrato_onset_samples) {
                float vib_blend = (float)(s - vibrato_onset_samples) /
                                  (float)(SAMPLE_RATE * 100 / 1000);  /* 100ms fade-in */
                if (vib_blend > 1.0f) vib_blend = 1.0f;
                vib_mod = 1.0f + VIBRATO_DEPTH * vib_blend * sinf(vib_phase);
                vib_phase += vib_phase_inc;
                if (vib_phase > 2.0f * M_PI) vib_phase -= 2.0f * M_PI;
            }

            /* Synthesize with harmonics */
            float raw = synth_sample(phase, vib_mod);
            int16_t sample = (int16_t)(env * amplitude * raw);

            phase += phase_inc;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;

            s_stereo_buf[i * 2]     = sample;
            s_stereo_buf[i * 2 + 1] = sample;
        }
        write_stereo(s_stereo_buf, chunk);
        pos += chunk;
    }

    /* Anti-click: write 2ms of rapid fade to guarantee zero-crossing
     * before the articulation silence. Prevents DC offset pop. */
    {
        int fade_samples = SAMPLE_RATE * 2 / 1000;  /* 2ms = 32 samples */
        if (fade_samples > CHUNK_SAMPLES) fade_samples = CHUNK_SAMPLES;
        for (int i = 0; i < fade_samples; i++) {
            float fade = 1.0f - (float)i / (float)fade_samples;
            float raw = synth_sample(phase, 1.0f);
            int16_t sample = (int16_t)(fade * amplitude * 0.1f * raw);
            phase += phase_inc;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
            s_stereo_buf[i * 2]     = sample;
            s_stereo_buf[i * 2 + 1] = sample;
        }
        write_stereo(s_stereo_buf, fade_samples);
    }

    /* Articulation gap (silence between notes) */
    if (gap_ms > 0) {
        play_silence(gap_ms);
    }

    return s_stop_req;
}

/* Legacy wrapper for startup chime (no accent) */
static bool play_note(float freq, int dur_ms)
{
    return play_note_ex(freq, dur_ms, 0.8f);
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

                /* Tempo scaling: song data written at 120 BPM.
                 * Scale durations to the song's actual tempo.
                 * tempo_scale > 1.0 = slower, < 1.0 = faster */
                float tempo_scale = 120.0f / (float)song->tempo_bpm;

                /* Metric accent: compute beat position within measure.
                 * Strong beat (beat 1) = accent 1.0
                 * Medium beat = 0.7 (beat 3 in 4/4, beat 4 in 6/8)
                 * Weak beat = 0.5 */
                int beat_ms;
                if (song->time_sig_den == 8) {
                    beat_ms = (int)(250.0f * tempo_scale); /* eighth note */
                } else {
                    beat_ms = (int)(500.0f * tempo_scale); /* quarter note */
                }
                int measure_ms;
                if (song->time_sig_den == 4) {
                    measure_ms = beat_ms * song->time_sig_num;
                } else {
                    measure_ms = (int)(1500.0f * tempo_scale); /* 6/8 */
                }

                ESP_LOGI(TAG, "Playing: %s (%d notes, %d BPM, %d/%d, scale=%.2f)",
                         song->title, song->note_count, song->tempo_bpm,
                         song->time_sig_num, song->time_sig_den, tempo_scale);

                play_silence(200);

                int beat_pos_ms = 0; /* Position within current measure */

                for (int i = 0; i < song->note_count; i++) {
                    if (s_stop_req || s_play_song >= 0) break;

                    s_note_index = i;
                    const note_t *n = &song->notes[i];
                    uint8_t note = n->midi_note;
                    if (note > 0) note += TRANSPOSE_SEMITONES;
                    float freq = midi_to_freq(note);

                    /* Scale duration by tempo */
                    int dur_ms = (int)((float)n->duration_ms * tempo_scale);

                    /* Rallentando: last 3 notes slow down for natural ending */
                    int remaining = song->note_count - i;
                    if (remaining <= 3 && remaining > 0) {
                        float rit = 1.0f + 0.15f * (float)(4 - remaining) / 3.0f;
                        dur_ms = (int)((float)dur_ms * rit);
                    }

                    /* Compute metric accent from beat position */
                    float accent;
                    if (measure_ms > 0) {
                        int pos_in_measure = beat_pos_ms % measure_ms;
                        if (pos_in_measure < beat_ms / 2) {
                            accent = 1.0f;  /* Beat 1 (downbeat) — strongest */
                        } else if (song->time_sig_num == 4 && song->time_sig_den == 4 &&
                                   pos_in_measure >= measure_ms / 2 &&
                                   pos_in_measure < measure_ms / 2 + beat_ms / 2) {
                            accent = 0.75f; /* Beat 3 in 4/4 — medium accent */
                        } else if (song->time_sig_den == 8 &&
                                   pos_in_measure >= measure_ms / 2 &&
                                   pos_in_measure < measure_ms / 2 + beat_ms / 2) {
                            accent = 0.75f; /* Beat 4 in 6/8 — secondary strong */
                        } else {
                            accent = 0.55f; /* Off-beat — soft */
                        }
                    } else {
                        accent = 0.8f;
                    }

                    if (play_note_ex(freq, dur_ms, accent)) break;

                    beat_pos_ms += dur_ms;
                }

                s_playing = false;
                ESP_LOGI(TAG, "Song finished: %s", song->title);

                /* Auto-advance based on play mode */
                if (!s_stop_req && s_play_song < 0) {
                    int next = next_song_for_mode(idx);
                    if (next >= 0) {
                        /* Brief pause between songs */
                        play_silence(250);
                        ESP_LOGI(TAG, "Auto-next: song %d (%s)", next, g_songs[next].title);
                        s_play_song = next;
                    }
                }
                s_current_song = -1;
            }
        } else if (s_yielded_to_noise) {
            /* Noise generator owns I2S. Don't write zeros — yield CPU. */
            vTaskDelay(pdMS_TO_TICKS(50));
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

bool audio_player_is_song_queued(void)
{
    return s_play_song >= 0;
}

int audio_player_note_index(void)
{
    return s_note_index;
}

void audio_player_cycle_mode(void)
{
    int m = (int)s_play_mode + 1;
    if (m >= PLAY_MODE_COUNT) m = 0;
    s_play_mode = (play_mode_t)m;
    if (s_play_mode == PLAY_MODE_SHUFFLE) {
        shuffle_reset(s_current_song);
    }
    static const char *names[] = {"Off", "Loop All", "Loop One", "Shuffle"};
    ESP_LOGI(TAG, "Play mode: %s", names[s_play_mode]);
}

void audio_player_set_mode(play_mode_t mode)
{
    if (mode >= PLAY_MODE_COUNT) mode = PLAY_MODE_OFF;
    s_play_mode = mode;
    if (mode == PLAY_MODE_SHUFFLE) shuffle_reset(s_current_song);
}

play_mode_t audio_player_get_mode(void)
{
    return s_play_mode;
}

void audio_player_yield_to_noise(void)
{
    s_stop_req = true;
    s_play_song = -1;
    /* Wait for the song path to bail. Worst case ~30 ms (chunk write). */
    for (int i = 0; i < 20 && s_playing; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_stop_req = false;
    s_yielded_to_noise = true;
    ESP_LOGI(TAG, "I2S yielded to noise generator");
}

void audio_player_resume_from_noise(void)
{
    s_yielded_to_noise = false;
    ESP_LOGI(TAG, "I2S reclaimed from noise generator");
}

bool audio_player_is_yielded_to_noise(void)
{
    return s_yielded_to_noise;
}

void audio_player_write_stereo(const int16_t *buf, size_t stereo_samples)
{
    size_t bytes = stereo_samples * 2 * sizeof(int16_t);
    size_t written = 0;
    i2s_channel_write(s_tx_handle, (void *)buf, bytes, &written, pdMS_TO_TICKS(100));
}
