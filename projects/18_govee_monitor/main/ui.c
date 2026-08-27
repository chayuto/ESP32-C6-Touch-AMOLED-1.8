#include "ui.h"
#include "chart_view.h"
#include "slot_store.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "ui";

#define SCREEN_W 368
#define SCREEN_H 448

/* Temperature and humidity share one size — humidity is the reading that
 * actually matters here (damp/mould watch), so it does not get demoted to
 * secondary type.
 *
 * Tile height is fixed at SCREEN_H / TILES_PER_PAGE rather than divided among
 * every slot: past four sensors the tiles page instead of shrinking, so the
 * type never has to get smaller again. At 4 per page a tile is 112 px (96 px
 * inside the padding) and a 48 px value collides with the header above and the
 * battery row below, so step down one size. */
#if CONFIG_GOVEE_MAX_SLOTS >= 4
#define FONT_VALUE  (&lv_font_montserrat_28)
#else
#define FONT_VALUE  (&lv_font_montserrat_48)
#endif

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
    lv_obj_clear_flag(t->root, LV_OBJ_FLAG_CLICKABLE);

    /* Header row: label (left) + RSSI (right) */
    t->label_name = make_label(t->root, &lv_font_montserrat_16, lv_color_white());
    lv_obj_align(t->label_name, LV_ALIGN_TOP_LEFT, 0, 0);

    t->label_rssi = make_label(t->root, &lv_font_montserrat_14, lv_color_hex(0xAAAAAA));
    lv_obj_align(t->label_rssi, LV_ALIGN_TOP_RIGHT, 0, 2);

    /* Big temperature */
    t->label_temp = make_label(t->root, FONT_VALUE, lv_color_hex(0xFFC857));
    lv_obj_align(t->label_temp, LV_ALIGN_LEFT_MID, 4, 0);

    /* Humidity, smaller, right of temp */
    t->label_humid = make_label(t->root, FONT_VALUE, lv_color_hex(0x4FC3F7));
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

/* Four tiles fill the screen at a readable size, so that is the page size.
 * Adding a fifth sensor adds a page, not a smaller tile. */
#define TILES_PER_PAGE  4
#define TILE_PAGES      ((CONFIG_GOVEE_MAX_SLOTS + TILES_PER_PAGE - 1) / TILES_PER_PAGE)

/* A tap anywhere that is not a control advances the view:
 *   tiles page 1 -> ... -> tiles page N -> humidity chart -> temp chart -> p1
 * Screen on/off is the BOOT button's job, deliberately not part of this cycle:
 * a tap that can blank the screen makes every other tap feel risky. */
typedef enum {
    VIEW_TILES_0 = 0,
    VIEW_HUMIDITY = TILE_PAGES,
    VIEW_TEMP,
    VIEW_COUNT,
} view_t;

#define VIEW_IS_TILES(v)  ((v) < TILE_PAGES)

static view_t    s_view = VIEW_TILES_0;
static lv_obj_t *s_page_tiles[TILE_PAGES];
static lv_obj_t *s_page_chart;

static void apply_view(void)
{
    for (int p = 0; p < TILE_PAGES; p++) {
        if (VIEW_IS_TILES(s_view) && (int)s_view == p) {
            lv_obj_clear_flag(s_page_tiles[p], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_page_tiles[p], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (VIEW_IS_TILES(s_view)) {
        lv_obj_add_flag(s_page_chart, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_page_chart, LV_OBJ_FLAG_HIDDEN);
        chart_view_set_metric(s_view == VIEW_HUMIDITY ? CHART_METRIC_HUMIDITY
                                                      : CHART_METRIC_TEMP);
    }
    ESP_LOGI(TAG, "view -> %s%d",
             VIEW_IS_TILES(s_view) ? "tiles page " : "chart ",
             VIEW_IS_TILES(s_view) ? (int)s_view + 1
                                   : (s_view == VIEW_HUMIDITY ? 1 : 2));
    ui_refresh();
}

static void screen_click_cb(lv_event_t *e)
{
    (void)e;
    s_view = (view_t)((s_view + 1) % VIEW_COUNT);
    apply_view();
}

void ui_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(parent, screen_click_cb, LV_EVENT_CLICKED, NULL);

    int tile_h = SCREEN_H / TILES_PER_PAGE;
    for (int p = 0; p < TILE_PAGES; p++) {
        lv_obj_t *page = lv_obj_create(parent);
        lv_obj_set_size(page, SCREEN_W, SCREEN_H);
        lv_obj_set_pos(page, 0, 0);
        lv_obj_set_style_radius(page, 0, 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_set_style_bg_color(page, lv_color_black(), 0);
        lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE);
        s_page_tiles[p] = page;
    }
    for (int i = 0; i < CONFIG_GOVEE_MAX_SLOTS; i++) {
        build_tile(&s_tiles[i], s_page_tiles[i / TILES_PER_PAGE],
                   (i % TILES_PER_PAGE) * tile_h, tile_h);
    }

    s_page_chart = chart_view_create(parent);

    apply_view();
}

void ui_refresh(void)
{
    if (VIEW_IS_TILES(s_view)) {
        slot_t snap[SLOT_STORE_MAX];
        slot_store_snapshot(snap);
        /* Only the visible page's tiles: the hidden pages' widgets are still
         * allocated but redrawing them each second is wasted work. */
        int first = (int)s_view * TILES_PER_PAGE;
        int last  = first + TILES_PER_PAGE;
        if (last > CONFIG_GOVEE_MAX_SLOTS) last = CONFIG_GOVEE_MAX_SLOTS;
        for (int i = first; i < last; i++) {
            render_slot(&s_tiles[i], &snap[i]);
        }
    } else {
        chart_view_refresh();
    }
}
