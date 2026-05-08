#include "ui.h"
#include "slot_store.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_W 368
#define SCREEN_H 448

typedef struct {
    lv_obj_t *root;
    lv_obj_t *label_name;
    lv_obj_t *label_rssi;
    lv_obj_t *label_temp;
    lv_obj_t *label_humid;
    lv_obj_t *bar_battery;
    lv_obj_t *label_battery;
    lv_obj_t *spinner;        /* shown when slot is empty */
} tile_t;

static tile_t s_tiles[SLOT_STORE_MAX];

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static void build_tile(tile_t *t, lv_obj_t *parent, int y, int h)
{
    t->root = lv_obj_create(parent);
    lv_obj_set_size(t->root, SCREEN_W, h);
    lv_obj_set_pos(t->root, 0, y);
    lv_obj_set_style_radius(t->root, 0, 0);
    lv_obj_set_style_border_width(t->root, 0, 0);
    lv_obj_set_style_pad_all(t->root, 8, 0);
    lv_obj_set_style_bg_color(t->root, lv_color_black(), 0);
    lv_obj_clear_flag(t->root, LV_OBJ_FLAG_SCROLLABLE);

    /* Header row: label (left) + RSSI (right) */
    t->label_name = make_label(t->root, &lv_font_montserrat_16, lv_color_white());
    lv_obj_align(t->label_name, LV_ALIGN_TOP_LEFT, 0, 0);

    t->label_rssi = make_label(t->root, &lv_font_montserrat_14, lv_color_hex(0xAAAAAA));
    lv_obj_align(t->label_rssi, LV_ALIGN_TOP_RIGHT, 0, 2);

    /* Big temperature */
    t->label_temp = make_label(t->root, &lv_font_montserrat_48, lv_color_hex(0xFFC857));
    lv_obj_align(t->label_temp, LV_ALIGN_LEFT_MID, 4, 0);

    /* Humidity, smaller, right of temp */
    t->label_humid = make_label(t->root, &lv_font_montserrat_28, lv_color_hex(0x4FC3F7));
    lv_obj_align(t->label_humid, LV_ALIGN_RIGHT_MID, -8, 0);

    /* Battery bar across the bottom */
    t->bar_battery = lv_bar_create(t->root);
    lv_obj_set_size(t->bar_battery, SCREEN_W - 90, 6);
    lv_obj_align(t->bar_battery, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    lv_bar_set_range(t->bar_battery, 0, 100);
    lv_obj_set_style_bg_color(t->bar_battery, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_color(t->bar_battery, lv_color_hex(0x66BB6A), LV_PART_INDICATOR);
    lv_obj_set_style_radius(t->bar_battery, 2, 0);
    lv_obj_set_style_radius(t->bar_battery, 2, LV_PART_INDICATOR);

    t->label_battery = make_label(t->root, &lv_font_montserrat_14, lv_color_hex(0xAAAAAA));
    lv_obj_align(t->label_battery, LV_ALIGN_BOTTOM_RIGHT, 0, -2);

    /* Spinner for empty slots */
    t->spinner = lv_spinner_create(t->root, 1500, 60);
    lv_obj_set_size(t->spinner, 40, 40);
    lv_obj_center(t->spinner);
    lv_obj_add_flag(t->spinner, LV_OBJ_FLAG_HIDDEN);
}

static void render_slot(tile_t *t, const slot_t *s)
{
    char buf[32];

    /* Empty slot — show "Searching..." with spinner, hide data labels. */
    if (!s->valid) {
        lv_label_set_text(t->label_name, "Searching...");
        lv_obj_set_style_text_color(t->label_name, lv_color_hex(0x666666), 0);
        lv_label_set_text(t->label_rssi, "");
        lv_label_set_text(t->label_temp, "");
        lv_label_set_text(t->label_humid, "");
        lv_label_set_text(t->label_battery, "");
        lv_bar_set_value(t->bar_battery, 0, LV_ANIM_OFF);
        lv_obj_clear_flag(t->spinner, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(t->spinner, LV_OBJ_FLAG_HIDDEN);

    lv_color_t name_color = s->stale ? lv_color_hex(0x666666) : lv_color_white();
    lv_obj_set_style_text_color(t->label_name, name_color, 0);
    lv_label_set_text(t->label_name, s->label[0] ? s->label : "H5075");

    snprintf(buf, sizeof(buf), "%d dBm", s->rssi);
    lv_label_set_text(t->label_rssi, buf);

#if CONFIG_GOVEE_TEMP_UNIT_F
    float t_disp = s->temp_c * 9.0f / 5.0f + 32.0f;
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""F", t_disp);
#else
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", s->temp_c);
#endif
    lv_label_set_text(t->label_temp, buf);

    snprintf(buf, sizeof(buf), "%.1f%%", s->humid_pct);
    lv_label_set_text(t->label_humid, buf);

    lv_bar_set_value(t->bar_battery, s->battery_pct, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%u%%", s->battery_pct);
    lv_label_set_text(t->label_battery, buf);

    /* Grey-out values when stale */
    lv_color_t fg = s->stale ? lv_color_hex(0x444444) : lv_color_hex(0xFFC857);
    lv_obj_set_style_text_color(t->label_temp, fg, 0);
    fg = s->stale ? lv_color_hex(0x333333) : lv_color_hex(0x4FC3F7);
    lv_obj_set_style_text_color(t->label_humid, fg, 0);
}

void ui_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    int n = CONFIG_GOVEE_MAX_SLOTS;
    int tile_h = SCREEN_H / n;
    for (int i = 0; i < n; i++) {
        build_tile(&s_tiles[i], parent, i * tile_h, tile_h);
    }

    ui_refresh();
}

void ui_refresh(void)
{
    slot_t snap[SLOT_STORE_MAX];
    slot_store_snapshot(snap);
    for (int i = 0; i < CONFIG_GOVEE_MAX_SLOTS; i++) {
        render_slot(&s_tiles[i], &snap[i]);
    }
}
