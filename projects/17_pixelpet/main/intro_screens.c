/*
 * intro_screens.c — first-run onboarding: welcome → species → name.
 *
 * Three full-screen cards stacked over the main UI. Each is its own
 * lv_obj container; only the active one is visible. The whole module
 * is gated by pet_state_t.intro_done so it shows exactly once per
 * fresh save, then hands control back to ui_screens via the on_done
 * callback.
 */

#include "intro_screens.h"
#include "asset_loader.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "intro";

typedef enum {
    INTRO_HIDDEN = 0,
    INTRO_WELCOME,
    INTRO_SPECIES,
    INTRO_NAME,
} intro_phase_t;

/* Letter alphabet: A-Z plus a trailing blank for short names. */
#define LETTER_COUNT  27
#define LETTER_BLANK  ' '

static char letter_at(int idx)
{
    if (idx < 0) idx = 0;
    if (idx >= LETTER_COUNT) idx = LETTER_COUNT - 1;
    return (idx == LETTER_COUNT - 1) ? LETTER_BLANK : (char)('A' + idx);
}

/* ── Module state ─────────────────────────────────────────── */

static lv_obj_t *s_root;
static lv_obj_t *s_welcome;
static lv_obj_t *s_species_card;
static lv_obj_t *s_name_card;
static lv_obj_t *s_species_sprite;
static lv_obj_t *s_species_label;
static lv_obj_t *s_letter_lbl[3];

static intro_phase_t  s_phase = INTRO_HIDDEN;
static species_id_t   s_picked_species = 0;
static int            s_letter_idx[3] = { 0, 0, 0 };   /* "AAA" */
static intro_done_cb_t s_on_done;

/* ── Card builders ────────────────────────────────────────── */

static lv_obj_t *make_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(card, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    return card;
}

static lv_obj_t *make_text(lv_obj_t *parent, const char *text,
                           const lv_font_t *font, lv_color_t color,
                           lv_align_t align, int x_ofs, int y_ofs)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, align, x_ofs, y_ofs);
    return lbl;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *label,
                          lv_color_t color, int width,
                          lv_align_t align, int x_ofs, int y_ofs,
                          lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, 56);
    lv_obj_align(btn, align, x_ofs, y_ofs);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    return btn;
}

static void set_egg_sprite(lv_obj_t *img, species_id_t sp)
{
    char buf[48];
    const species_def_t *s = species_get(sp);
    snprintf(buf, sizeof(buf), "lifecycle/egg_%s", s->id_str);
    const asset_anim_t *a = asset_get_by_name(buf);
    if (!a) {
        ESP_LOGW(TAG, "egg sprite missing: %s", buf);
        return;
    }
    lv_img_set_src(img, a->frames[0]);
    lv_obj_set_size(img, a->width, a->height);
    /* Re-align after set_src (LVGL invalidates layout on source swap). */
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -10);
}

static void refresh_species_card(void)
{
    set_egg_sprite(s_species_sprite, s_picked_species);
    const species_def_t *s = species_get(s_picked_species);
    lv_label_set_text(s_species_label, s->display_name);
}

static void refresh_name_card(void)
{
    for (int i = 0; i < 3; i++) {
        char buf[2] = { letter_at(s_letter_idx[i]), '\0' };
        if (buf[0] == ' ') buf[0] = '_';   /* visible placeholder for blank */
        lv_label_set_text(s_letter_lbl[i], buf);
    }
}

/* ── Phase transitions ────────────────────────────────────── */

static void show_phase(intro_phase_t phase)
{
    lv_obj_add_flag(s_welcome,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_species_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_name_card,    LV_OBJ_FLAG_HIDDEN);
    s_phase = phase;
    switch (phase) {
    case INTRO_WELCOME: lv_obj_clear_flag(s_welcome,      LV_OBJ_FLAG_HIDDEN); break;
    case INTRO_SPECIES: lv_obj_clear_flag(s_species_card, LV_OBJ_FLAG_HIDDEN);
                        refresh_species_card(); break;
    case INTRO_NAME:    lv_obj_clear_flag(s_name_card,    LV_OBJ_FLAG_HIDDEN);
                        refresh_name_card(); break;
    case INTRO_HIDDEN:  break;
    }
    /* Keep the overlay on top of any pre-existing UI. */
    if (phase != INTRO_HIDDEN) lv_obj_move_foreground(s_root);
}

static void finish_intro(void)
{
    char name[PET_NAME_LEN + 1];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        char c = letter_at(s_letter_idx[i]);
        if (c == LETTER_BLANK && n == 0) continue;   /* trim leading blanks */
        name[n++] = c;
    }
    while (n > 0 && name[n - 1] == LETTER_BLANK) n--;  /* trim trailing */
    if (n == 0) { strcpy(name, "PET"); n = 3; }
    name[n] = '\0';
    ESP_LOGI(TAG, "onboarding done: species=%d name=\"%s\"",
             (int)s_picked_species, name);
    intro_screens_hide();
    if (s_on_done) s_on_done(s_picked_species, name);
}

/* ── Event callbacks ──────────────────────────────────────── */

static void welcome_tap_cb(lv_event_t *e)
{
    (void)e;
    show_phase(INTRO_SPECIES);
}

static void species_prev_cb(lv_event_t *e)
{
    (void)e;
    s_picked_species = (s_picked_species == 0)
        ? (SPECIES_COUNT - 1) : (s_picked_species - 1);
    refresh_species_card();
}

static void species_next_cb(lv_event_t *e)
{
    (void)e;
    s_picked_species = (s_picked_species + 1) % SPECIES_COUNT;
    refresh_species_card();
}

static void species_pick_cb(lv_event_t *e)
{
    (void)e;
    show_phase(INTRO_NAME);
}

static void letter_up_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    s_letter_idx[slot] = (s_letter_idx[slot] + 1) % LETTER_COUNT;
    refresh_name_card();
}

static void letter_down_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    s_letter_idx[slot] = (s_letter_idx[slot] + LETTER_COUNT - 1) % LETTER_COUNT;
    refresh_name_card();
}

static void name_hatch_cb(lv_event_t *e)
{
    (void)e;
    finish_intro();
}

/* ── Card construction ────────────────────────────────────── */

static void build_welcome(lv_obj_t *parent)
{
    s_welcome = make_card(parent);
    lv_obj_add_flag(s_welcome, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_welcome, welcome_tap_cb, LV_EVENT_PRESSED, NULL);

    make_text(s_welcome, "PixelPet",
              &lv_font_montserrat_28, lv_color_hex(0x00FFAA),
              LV_ALIGN_TOP_MID, 0, 60);
    make_text(s_welcome, "something washed up\nin the bowl...",
              &lv_font_montserrat_20, lv_color_hex(0xCCCCCC),
              LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *l = make_text(s_welcome, "tap to begin",
              &lv_font_montserrat_14, lv_color_hex(0x808080),
              LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_species(lv_obj_t *parent)
{
    s_species_card = make_card(parent);

    make_text(s_species_card, "PICK YOUR EGG",
              &lv_font_montserrat_20, lv_color_hex(0xFFD166),
              LV_ALIGN_TOP_MID, 0, 30);

    s_species_sprite = lv_img_create(s_species_card);
    lv_img_set_antialias(s_species_sprite, false);
    lv_obj_align(s_species_sprite, LV_ALIGN_CENTER, 0, -10);

    s_species_label = make_text(s_species_card, "",
              &lv_font_montserrat_20, lv_color_hex(0xFFFFFF),
              LV_ALIGN_CENTER, 0, 90);

    make_btn(s_species_card, LV_SYMBOL_LEFT, lv_color_hex(0x444444), 64,
             LV_ALIGN_CENTER, -120, 0, species_prev_cb);
    make_btn(s_species_card, LV_SYMBOL_RIGHT, lv_color_hex(0x444444), 64,
             LV_ALIGN_CENTER,  120, 0, species_next_cb);
    make_btn(s_species_card, "PICK", lv_color_hex(0x06D6A0), 220,
             LV_ALIGN_BOTTOM_MID, 0, -40, species_pick_cb);
}

static void build_name(lv_obj_t *parent)
{
    s_name_card = make_card(parent);

    make_text(s_name_card, "NAME YOUR PET",
              &lv_font_montserrat_20, lv_color_hex(0xFFAFCC),
              LV_ALIGN_TOP_MID, 0, 30);

    /* 3 columns, slot width ≈ 80 px, total ≈ 240 px centred. */
    static const int X_OFS[3] = { -84, 0, 84 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *up = lv_btn_create(s_name_card);
        lv_obj_set_size(up, 64, 44);
        lv_obj_align(up, LV_ALIGN_CENTER, X_OFS[i], -70);
        lv_obj_set_style_bg_color(up, lv_color_hex(0x444444), 0);
        lv_obj_add_event_cb(up, letter_up_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *up_lbl = lv_label_create(up);
        lv_label_set_text(up_lbl, LV_SYMBOL_UP);
        lv_obj_center(up_lbl);

        s_letter_lbl[i] = lv_label_create(s_name_card);
        lv_obj_set_style_text_font(s_letter_lbl[i], &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_letter_lbl[i], lv_color_hex(0x00FFAA), 0);
        lv_obj_align(s_letter_lbl[i], LV_ALIGN_CENTER, X_OFS[i], -10);

        lv_obj_t *down = lv_btn_create(s_name_card);
        lv_obj_set_size(down, 64, 44);
        lv_obj_align(down, LV_ALIGN_CENTER, X_OFS[i], 50);
        lv_obj_set_style_bg_color(down, lv_color_hex(0x444444), 0);
        lv_obj_add_event_cb(down, letter_down_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *dn_lbl = lv_label_create(down);
        lv_label_set_text(dn_lbl, LV_SYMBOL_DOWN);
        lv_obj_center(dn_lbl);
    }

    make_btn(s_name_card, "HATCH", lv_color_hex(0x06D6A0), 220,
             LV_ALIGN_BOTTOM_MID, 0, -40, name_hatch_cb);
}

/* ── Public API ───────────────────────────────────────────── */

void intro_screens_init(lv_obj_t *parent, intro_done_cb_t on_done)
{
    s_on_done = on_done;
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    build_welcome(s_root);
    build_species(s_root);
    build_name(s_root);

    ESP_LOGI(TAG, "intro overlay built");
}

void intro_screens_show(void)
{
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    show_phase(INTRO_WELCOME);
}

void intro_screens_hide(void)
{
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_phase = INTRO_HIDDEN;
}

bool intro_screens_is_visible(void)
{
    return s_phase != INTRO_HIDDEN;
}
