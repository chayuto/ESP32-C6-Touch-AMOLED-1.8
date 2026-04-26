/*
 * stat_engine.c — time-aware stat decay, care actions, and stage transitions.
 *
 * Decay rates are per real second. Phase 3 ships with FAST_AGING enabled by
 * default so iteration is bearable (10s = 1 hour of game time). Phase 7 will
 * surface this as a Kconfig toggle and tune the slow rates.
 */

#include "stat_engine.h"
#include "audio_jingles.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "stat_engine";

#ifndef CONFIG_PIXELPET_TIME_SCALE
#define CONFIG_PIXELPET_TIME_SCALE 360   /* 1 real-sec ≈ 6 game-min during dev */
#endif

/* Per-(scaled-)second decay; integer math, accumulator carried in *_acc fields
 * is overkill for a Phase 3 toy, so we just step by ticks. */

static uint8_t clamp_sub(uint8_t v, int n) { return (v > n) ? (v - n) : 0; }
static uint8_t clamp_add(uint8_t v, int n) { int r = v + n; return r > 100 ? 100 : (uint8_t)r; }

static const char *CARE_NAMES[CARE_COUNT] = {
    "feed_meal", "feed_snack", "play", "clean", "sleep_toggle", "discipline", "medicine",
};

const char *care_action_name(care_action_t a)
{
    if (a >= CARE_COUNT) return "?";
    return CARE_NAMES[a];
}

void stat_engine_decay(pet_state_t *p, int64_t dt_us)
{
    if (p->stage == STAGE_EGG || p->stage == STAGE_DEAD) return;

    /* Convert real-time to "game-seconds" using the dev time scale. */
    int64_t game_seconds = (dt_us / 1000000) * CONFIG_PIXELPET_TIME_SCALE;
    if (game_seconds <= 0) return;

    /* Hunger:  -1 per 30 game-min  = -1 per 1800 game-s */
    /* Happy:   -1 per 45 game-min  = -1 per 2700 game-s */
    /* Energy:  -1 per 20 game-min awake / +1 per 5 game-min asleep */
    static int64_t acc_hunger = 0, acc_happy = 0, acc_energy = 0;

    acc_hunger += game_seconds;
    while (acc_hunger >= 1800) { p->hunger = clamp_sub(p->hunger, 1); acc_hunger -= 1800; }

    acc_happy  += game_seconds;
    while (acc_happy  >= 2700) { p->happy  = clamp_sub(p->happy,  1); acc_happy  -= 2700; }

    acc_energy += game_seconds;
    if (p->is_sleeping) {
        while (acc_energy >= 300)  { p->energy = clamp_add(p->energy, 1); acc_energy -= 300; }
    } else {
        while (acc_energy >= 1200) { p->energy = clamp_sub(p->energy, 1); acc_energy -= 1200; }
    }

    /* Random poop event ~ once per game-hour while awake */
    if (!p->is_sleeping && p->poop_count < 3) {
        if ((rand() % 3600) < game_seconds) {
            p->poop_count++;
            audio_jingles_play(JINGLE_POOP);
        }
    }

    /* Cleanliness drops slightly with each existing poop */
    if (p->poop_count > 0) {
        int drop = (int)((game_seconds * p->poop_count) / 600);
        if (drop > 0) p->clean = clamp_sub(p->clean, drop);
    }

    /* Health:
     *  - drops when ≥2 stats are critical (<20)
     *  - recovers when all three care stats are above 60
     */
    int critical = (p->hunger < 20) + (p->happy < 20) + (p->clean < 20);
    if (critical >= 2) {
        int drop = (int)(game_seconds / 600);
        if (drop > 0) p->health = clamp_sub(p->health, drop);
    } else if (p->hunger > 60 && p->happy > 60 && p->clean > 60) {
        int gain = (int)(game_seconds / 1200);
        if (gain > 0) p->health = clamp_add(p->health, gain);
    }

    if (p->health == 0 && p->stage != STAGE_DEAD) {
        ESP_LOGW(TAG, "pet died");
        p->stage = STAGE_DEAD;
        audio_jingles_play(JINGLE_DEATH);
    }
}

bool stat_engine_apply_care(pet_state_t *p, care_action_t action)
{
    if (p->stage == STAGE_EGG || p->stage == STAGE_DEAD) return false;

    switch (action) {
    case CARE_FEED_MEAL:
        if (p->hunger >= 95) return false;
        p->hunger = clamp_add(p->hunger, 30);
        p->happy  = clamp_add(p->happy,  5);
        p->care_score += 5;
        audio_jingles_play(JINGLE_EAT);
        break;

    case CARE_FEED_SNACK:
        p->hunger = clamp_add(p->hunger, 10);
        p->happy  = clamp_add(p->happy,  15);
        p->care_score += 1;  /* less nutritious */
        audio_jingles_play(JINGLE_EAT);
        break;

    case CARE_PLAY:
        if (p->energy < 10) return false;
        p->happy  = clamp_add(p->happy,  20);
        p->energy = clamp_sub(p->energy, 15);
        p->care_score += 4;
        audio_jingles_play(JINGLE_HAPPY);
        break;

    case CARE_CLEAN_ONE:
        if (p->poop_count == 0) return false;
        p->poop_count--;
        p->clean = clamp_add(p->clean, 25);
        p->care_score += 3;
        break;

    case CARE_SLEEP_TOGGLE:
        p->is_sleeping = !p->is_sleeping;
        break;

    case CARE_DISCIPLINE:
        p->disc  = clamp_add(p->disc,  20);
        p->happy = clamp_sub(p->happy,  5);
        audio_jingles_play(JINGLE_DISCIPLINE);
        break;

    case CARE_MEDICINE:
        if (p->health > 70) return false;
        p->health = clamp_add(p->health, 30);
        p->care_score += 2;
        break;

    default:
        return false;
    }
    return true;
}

/* Stage transition ages (game-hours into life) */
static const struct {
    pet_stage_t stage;
    int hours;
} STAGE_TABLE[] = {
    { STAGE_BABY,    0   },  /* immediately on hatch */
    { STAGE_CHILD,   6   },
    { STAGE_TEEN,    24  },
    { STAGE_ADULT,   72  },
    { STAGE_SENIOR,  168 },
};

static pet_adult_form_t pick_adult_form(const pet_state_t *p)
{
    /* care_score earned during teen stage drives form */
    if (p->care_score > 200 && p->disc > 60) return ADULT_HAPPY;
    if (p->disc < 30)                        return ADULT_NAUGHTY;
    if (p->energy < 40 || p->care_score < 80) return ADULT_LAZY;
    if (p->health < 50)                       return ADULT_SICK;
    return ADULT_HAPPY;
}

void stat_engine_check_transitions(pet_state_t *p, int64_t now_us)
{
    if (p->stage == STAGE_DEAD) return;

    /* Egg hatches after 30 game-min — handled separately. */
    if (p->stage == STAGE_EGG) {
        int64_t age_real_s = (now_us - p->hatched_unix) / 1000000;
        int64_t age_game_s = age_real_s * CONFIG_PIXELPET_TIME_SCALE;
        if (age_game_s >= 30 * 60) {
            p->stage = STAGE_BABY;
            ESP_LOGI(TAG, "egg hatched → BABY");
            audio_jingles_play(JINGLE_HATCH);
        }
        return;
    }

    int64_t age_real_s = (now_us - p->hatched_unix) / 1000000;
    int64_t age_game_h = (age_real_s * CONFIG_PIXELPET_TIME_SCALE) / 3600;

    pet_stage_t next = p->stage;
    for (int i = 0; i < (int)(sizeof(STAGE_TABLE) / sizeof(STAGE_TABLE[0])); i++) {
        if (age_game_h >= STAGE_TABLE[i].hours) next = STAGE_TABLE[i].stage;
    }

    if (next != p->stage) {
        if (next == STAGE_ADULT) {
            p->adult_form = pick_adult_form(p);
            ESP_LOGI(TAG, "adult form: %d", (int)p->adult_form);
        }
        ESP_LOGI(TAG, "stage %d → %d (age %lldh)", (int)p->stage, (int)next, age_game_h);
        p->stage = next;
        audio_jingles_play(JINGLE_LEVELUP);
    }
}
