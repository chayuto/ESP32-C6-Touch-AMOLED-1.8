/*
 * pet_renderer.c — sprite-driven layered composition (v2 rework).
 *
 * Layers stacked top-to-bottom over the screen:
 *   accessory_back   (currently unused, reserved for poop/food bowl)
 *   pet_body         lv_animimg, mood + species drives the source
 *   particle_overlay lv_image, mood-conditional (heart/Z/etc.)
 *
 * On every state change:
 *   1. Compute (species_id, mood) → animation name (e.g. "idle", "happy",
 *      "sleeping").
 *   2. asset_loader returns frame array + total duration; lv_animimg_set_src
 *      swaps the source array, lv_animimg_set_duration adjusts pace,
 *      lv_animimg_start kicks the loop.
 *   3. Mood-appropriate particle is shown/hidden.
 *
 * The previous procedural primitives (circle bodies, eye/mouth shapes)
 * are gone — bodies now live entirely in the asset bundle.
 */

#include "pet_renderer.h"
#include "asset_loader.h"
#include "species.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "pet_renderer";

/* Layer widgets */
static lv_obj_t *s_root;
static lv_obj_t *s_body;          /* lv_animimg */
static lv_obj_t *s_particle;      /* lv_image, hidden when no overlay */

/* Cached state — used to avoid restarting animations every tick */
static species_id_t s_last_species   = SPECIES_COUNT;   /* sentinel */
static pet_mood_t   s_last_mood      = MOOD_COUNT;
static pet_stage_t  s_last_stage     = STAGE_COUNT;
static asset_id_t   s_last_particle  = 0xFFFF;

/* Animation name table — index by mood. Stage logic chooses egg vs body. */
static const char *MOOD_ANIM_NAMES[MOOD_COUNT] = {
    [MOOD_HAPPY]    = "happy",
    [MOOD_NEUTRAL]  = "idle",
    [MOOD_SAD]      = "sad",
    [MOOD_HUNGRY]   = "sad",        /* fallback while we lack a hungry anim */
    [MOOD_TIRED]    = "sad",
    [MOOD_SICK]     = "sick",
    [MOOD_PLAYING]  = "happy",
    [MOOD_SLEEPING] = "sleeping",
    [MOOD_DEAD]     = "dead",
};

/* Particle overlay name per mood; NULL == no overlay. */
static const char *MOOD_PARTICLES[MOOD_COUNT] = {
    [MOOD_HAPPY]    = "particles/heart",
    [MOOD_HUNGRY]   = "particles/exclaim",
    [MOOD_TIRED]    = "particles/zzz",
    [MOOD_SICK]     = "particles/sweat",
    [MOOD_SLEEPING] = "particles/zzz",
    [MOOD_PLAYING]  = "particles/sparkle",
};

/* ── Helpers ───────────────────────────────────────────────────────────── */

static void show_anim(const asset_anim_t *anim)
{
    if (!anim) {
        lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_animimg_set_src(s_body, anim->frames, anim->frame_count);
    lv_animimg_set_duration(s_body, anim->total_duration_ms);
    lv_animimg_set_repeat_count(s_body, LV_ANIM_REPEAT_INFINITE);
    lv_animimg_start(s_body);
}

static void show_particle(const char *name)
{
    if (!name) {
        lv_obj_add_flag(s_particle, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const asset_anim_t *anim = asset_get_by_name(name);
    if (!anim) {
        lv_obj_add_flag(s_particle, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    /* Particles stay simple: show the first frame as a static image.
     * (We can swap to lv_animimg when particles need motion.) */
    lv_img_set_src(s_particle, anim->frames[0]);
    lv_obj_clear_flag(s_particle, LV_OBJ_FLAG_HIDDEN);
}

static const asset_anim_t *resolve_body_anim(species_id_t species,
                                             pet_stage_t stage,
                                             pet_mood_t mood)
{
    if (stage == STAGE_EGG) {
        char buf[48];
        const species_def_t *s = species_get(species);
        snprintf(buf, sizeof(buf), "lifecycle/egg_%s", s->id_str);
        return asset_get_by_name(buf);
    }

    const char *anim_name = MOOD_ANIM_NAMES[mood] ? MOOD_ANIM_NAMES[mood] : "idle";
    char buf[64];
    if (!species_anim_asset_name(species, anim_name, buf, sizeof(buf))) {
        return NULL;
    }
    const asset_anim_t *a = asset_get_by_name(buf);
    if (a) return a;

    /* Fallback to idle if the requested mood anim doesn't exist for this species */
    if (strcmp(anim_name, "idle") != 0) {
        species_anim_asset_name(species, "idle", buf, sizeof(buf));
        a = asset_get_by_name(buf);
    }
    return a;
}

/* ── Public API ────────────────────────────────────────────────────────── */

void pet_renderer_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, 200, 200);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_root, LV_ALIGN_CENTER, 0, 0);

    s_body = lv_animimg_create(s_root);
    lv_obj_set_size(s_body, 64, 64);
    lv_obj_align(s_body, LV_ALIGN_CENTER, 0, 0);
    /* Crisp pixel scaling: nearest-neighbour, scale ×2 (32×32 sprite → 64×64 visible). */
    lv_img_set_antialias(s_body, false);
    lv_img_set_zoom(s_body, 256 * 2);

    s_particle = lv_img_create(s_root);
    lv_obj_align(s_particle, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_img_set_antialias(s_particle, false);
    lv_img_set_zoom(s_particle, 256 * 2);
    lv_obj_add_flag(s_particle, LV_OBJ_FLAG_HIDDEN);

    s_last_species = SPECIES_COUNT;   /* force first-call diff */
    s_last_mood    = MOOD_COUNT;
    s_last_stage   = STAGE_COUNT;

    ESP_LOGI(TAG, "pet renderer ready (sprite-driven)");
}

pet_mood_t pet_renderer_derive_mood(const pet_state_t *p)
{
    if (p->stage == STAGE_DEAD)        return MOOD_DEAD;
    if (p->is_sleeping)                return MOOD_SLEEPING;
    if (p->health < 30 || p->clean < 20) return MOOD_SICK;
    if (p->hunger < 25)                return MOOD_HUNGRY;
    if (p->energy < 20)                return MOOD_TIRED;
    if (p->happy  < 30)                return MOOD_SAD;
    if (p->happy  > 80 && p->hunger > 60) return MOOD_HAPPY;
    return MOOD_NEUTRAL;
}

void pet_renderer_set_state(const pet_state_t *p)
{
    pet_mood_t mood = pet_renderer_derive_mood(p);

    bool body_changed = (p->species_id != s_last_species) ||
                        (p->stage      != s_last_stage)   ||
                        (mood          != s_last_mood);
    if (body_changed) {
        const asset_anim_t *anim = resolve_body_anim(p->species_id,
                                                     p->stage, mood);
        show_anim(anim);
        s_last_species = p->species_id;
        s_last_stage   = p->stage;
        s_last_mood    = mood;
    }

    /* Particle overlay — keyed by mood only */
    if (mood != s_last_mood || s_last_particle == 0xFFFF) {
        show_particle(MOOD_PARTICLES[mood]);
    }
}

void pet_renderer_tick(void)
{
    /* Animation is now driven by lv_animimg's internal timer, not us.
     * Reserved for future ambient effects (eye dart, big-idle picker). */
}
