#pragma once

#include "lvgl.h"
#include "pet_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the story-card overlay widgets. The card is hidden by default
 *  and dismisses on tap. Cards queued while one is showing replace the
 *  current one (most-recent wins) — story beats should be rare enough
 *  that this matters in theory only. */
void story_card_init(lv_obj_t *parent);

/** Show a generic story card. `sprite_name` is an asset name (or NULL
 *  for text-only). `accent_hex` colours the title. */
void story_card_show(const char *title, const char *body,
                     const char *sprite_name, uint32_t accent_hex);

/** Stage-up beat: shows the new stage name + a pet-specific tip line. */
void story_card_show_stageup(const char *pet_name, pet_stage_t new_stage);

/** Adult-form reveal: explains how the pet's care shaped them. */
void story_card_show_adult(const char *pet_name, pet_adult_form_t form);

/** True while the card is on screen. Use to suppress care/touch handlers. */
bool story_card_is_visible(void);

void story_card_hide(void);

#ifdef __cplusplus
}
#endif
