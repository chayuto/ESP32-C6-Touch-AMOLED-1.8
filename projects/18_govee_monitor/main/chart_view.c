#include "chart_view.h"

#include "history.h"
#include "slot_store.h"

#include <stdio.h>

#define SCREEN_W 368
#define SCREEN_H 448

/* Humidity is plotted on a fixed axis, deliberately. Auto-scaling would render
 * a 74 -> 76 %RH wiggle as a mountain range and a flat 78 %RH as unremarkable;
 * what matters is the absolute distance from the mould threshold. */
#define HUMID_AXIS_MIN   3000    /* 30 %RH, x100 */
#define HUMID_AXIS_MAX  10000    /* 100 %RH */
#define HUMID_THRESHOLD  6500    /* 65 %RH — sustained growth risk above here */

/* Temperature is the opposite case: the absolute value matters less than the
 * shape, so its axis tracks the data. */
#define TEMP_AXIS_PAD     100    /* 1 °C of headroom, x100 */

/* Red is reserved for the threshold line, so no room borrows it. */
static const uint32_t SERIES_COLORS[SLOT_STORE_MAX] = {
    0x4FC3F7,   /* light blue */
    0xFFD54F,   /* amber */
    0x81C784,   /* green */
    0xBA68C8,   /* purple */
    0xFF8A65,   /* coral — 5th slot, typically the outdoor sensor */
};
_Static_assert(sizeof(SERIES_COLORS) / sizeof(SERIES_COLORS[0]) == SLOT_STORE_MAX,
               "every slot needs its own colour: a missing one is black on black");
#define THRESHOLD_COLOR 0xE53935

static lv_obj_t *s_page;
static lv_obj_t *s_title;
static lv_obj_t *s_chart;
static lv_obj_t *s_lbl_axis_top;
static lv_obj_t *s_lbl_axis_bot;
static lv_obj_t *s_lbl_axis_thresh;
static lv_obj_t *s_lbl_x_left;
static lv_obj_t *s_legend_name[SLOT_STORE_MAX];
static lv_obj_t *s_legend_dot[SLOT_STORE_MAX];
static lv_obj_t *s_btn[HISTORY_RANGE_COUNT];

static lv_chart_series_t *s_series[SLOT_STORE_MAX];
static lv_chart_series_t *s_thresh_series;

/* Series data lives in our own arrays via lv_chart_set_ext_y_array, so LVGL
 * neither allocates nor copies 600 points on every repaint. */
static lv_coord_t s_ydata[SLOT_STORE_MAX][HISTORY_POINTS];
static lv_coord_t s_ythresh[HISTORY_POINTS];

static chart_metric_t  s_metric = CHART_METRIC_HUMIDITY;
/* Default to the 1 h tier: it lands a point every 30 s, so the chart has
 * something to show a minute after boot. The 24 h tier would sit empty for
 * its first 12-minute bucket. */
static history_range_t s_range  = HISTORY_RANGE_1H;

static const char *RANGE_TEXT[HISTORY_RANGE_COUNT] = { "1h", "6h", "24h" };
static const char *X_LEFT_TEXT[HISTORY_RANGE_COUNT] = { "-1h", "-6h", "-24h" };

static int active_slots(void)
{
    int n = CONFIG_GOVEE_MAX_SLOTS;
    if (n > SLOT_STORE_MAX) n = SLOT_STORE_MAX;
    return n;
}

static void style_range_buttons(void)
{
    for (int i = 0; i < HISTORY_RANGE_COUNT; i++) {
        bool on = (i == (int)s_range);
        lv_obj_set_style_bg_color(s_btn[i],
                                  on ? lv_color_hex(0x37474F) : lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(s_btn[i],
                                      on ? lv_color_hex(0x4FC3F7) : lv_color_hex(0x333333), 0);
    }
}

static void range_btn_cb(lv_event_t *e)
{
    s_range = (history_range_t)(intptr_t)lv_event_get_user_data(e);
    style_range_buttons();
    chart_view_refresh();
}

lv_obj_t *chart_view_create(lv_obj_t *parent)
{
    s_page = lv_obj_create(parent);
    lv_obj_set_size(s_page, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_page, 0, 0);
    lv_obj_set_style_radius(s_page, 0, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_style_bg_color(s_page, lv_color_black(), 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    /* Taps must reach the screen handler that cycles views. */
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_CLICKABLE);

    s_title = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
    lv_obj_set_pos(s_title, 10, 6);

    /* Chart. Left inset leaves room for the axis labels drawn beside it. */
    s_chart = lv_chart_create(s_page);
    lv_obj_set_size(s_chart, SCREEN_W - 60, 268);
    lv_obj_set_pos(s_chart, 50, 34);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, HISTORY_POINTS);
    lv_chart_set_div_line_count(s_chart, 5, 0);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_border_color(s_chart, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_chart, 2, 0);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR);   /* no point markers */
    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_CLICKABLE);

    /* Threshold first so the room traces draw over it. */
    s_thresh_series = lv_chart_add_series(s_chart, lv_color_hex(THRESHOLD_COLOR),
                                          LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_ext_y_array(s_chart, s_thresh_series, s_ythresh);

    for (int i = 0; i < active_slots(); i++) {
        s_series[i] = lv_chart_add_series(s_chart, lv_color_hex(SERIES_COLORS[i]),
                                          LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_ext_y_array(s_chart, s_series[i], s_ydata[i]);
    }

    /* Axis labels, placed by hand — cheaper and more predictable than LVGL's
     * tick drawing, and only three numbers ever need showing. */
    s_lbl_axis_top = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_lbl_axis_top, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_axis_top, lv_color_hex(0x888888), 0);
    lv_obj_set_pos(s_lbl_axis_top, 4, 34);

    s_lbl_axis_bot = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_lbl_axis_bot, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_axis_bot, lv_color_hex(0x888888), 0);
    lv_obj_set_pos(s_lbl_axis_bot, 4, 286);

    s_lbl_axis_thresh = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_lbl_axis_thresh, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_axis_thresh, lv_color_hex(THRESHOLD_COLOR), 0);

    s_lbl_x_left = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_lbl_x_left, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_x_left, lv_color_hex(0x888888), 0);
    lv_obj_set_pos(s_lbl_x_left, 50, 304);

    lv_obj_t *x_right = lv_label_create(s_page);
    lv_obj_set_style_text_font(x_right, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(x_right, lv_color_hex(0x888888), 0);
    lv_label_set_text(x_right, "now");
    lv_obj_align(x_right, LV_ALIGN_TOP_RIGHT, -8, 304);

    /* Legend sits between the plot and the range buttons at y=386, so it gets
     * two rows and no more. Past four slots that means three columns rather
     * than a third row, which would draw the 5th name over the "1h" button. */
    const int cols = active_slots() > 4 ? 3 : 2;
    const int col_w = active_slots() > 4 ? 118 : 180;
    for (int i = 0; i < active_slots(); i++) {
        int col = i % cols, row = i / cols;
        int x = 12 + col * col_w;
        int y = 326 + row * 24;

        s_legend_dot[i] = lv_obj_create(s_page);
        lv_obj_set_size(s_legend_dot[i], 10, 10);
        lv_obj_set_pos(s_legend_dot[i], x, y + 4);
        lv_obj_set_style_radius(s_legend_dot[i], 2, 0);
        lv_obj_set_style_border_width(s_legend_dot[i], 0, 0);
        lv_obj_set_style_bg_color(s_legend_dot[i], lv_color_hex(SERIES_COLORS[i]), 0);
        lv_obj_clear_flag(s_legend_dot[i], LV_OBJ_FLAG_CLICKABLE);

        s_legend_name[i] = lv_label_create(s_page);
        lv_obj_set_style_text_font(s_legend_name[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_legend_name[i], lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_pos(s_legend_name[i], x + 16, y);
    }

    /* Range buttons. These stay clickable; LVGL does not bubble their events,
     * so they never trip the tap-to-cycle handler on the screen. */
    for (int i = 0; i < HISTORY_RANGE_COUNT; i++) {
        s_btn[i] = lv_btn_create(s_page);
        lv_obj_set_size(s_btn[i], 100, 44);
        lv_obj_set_pos(s_btn[i], 12 + i * 116, 386);
        lv_obj_set_style_radius(s_btn[i], 6, 0);
        lv_obj_set_style_border_width(s_btn[i], 1, 0);
        lv_obj_add_event_cb(s_btn[i], range_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *l = lv_label_create(s_btn[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_label_set_text(l, RANGE_TEXT[i]);
        lv_obj_center(l);
    }
    style_range_buttons();

    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    return s_page;
}

void chart_view_set_metric(chart_metric_t metric)
{
    s_metric = metric;
}

void chart_view_refresh(void)
{
    if (!s_page || lv_obj_has_flag(s_page, LV_OBJ_FLAG_HIDDEN)) return;

    history_point_t pts[HISTORY_POINTS];
    slot_t          slots[SLOT_STORE_MAX];
    slot_store_snapshot(slots);

    bool humidity = (s_metric == CHART_METRIC_HUMIDITY);
    int  n        = active_slots();

    int32_t lo = INT32_MAX, hi = INT32_MIN;

    for (int i = 0; i < n; i++) {
        history_snapshot(i, s_range, pts);
        for (int p = 0; p < HISTORY_POINTS; p++) {
            int16_t v = humidity ? pts[p].humid_x100 : pts[p].temp_cx100;
            if (v == HISTORY_NO_DATA) {
                /* Break the line rather than plotting a drop to zero — weak
                 * sensors genuinely miss buckets. */
                s_ydata[i][p] = LV_CHART_POINT_NONE;
            } else {
                s_ydata[i][p] = (lv_coord_t)v;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        }

        const char *name = slots[i].valid && slots[i].label[0] ? slots[i].label : "--";
        lv_label_set_text(s_legend_name[i], name);
    }

    if (humidity) {
        lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y,
                           HUMID_AXIS_MIN, HUMID_AXIS_MAX);
        for (int p = 0; p < HISTORY_POINTS; p++) s_ythresh[p] = HUMID_THRESHOLD;

        lv_label_set_text(s_title, "Humidity");
        lv_label_set_text(s_lbl_axis_top, "100%");
        lv_label_set_text(s_lbl_axis_bot, "30%");
        lv_label_set_text(s_lbl_axis_thresh, "65%");
        lv_obj_clear_flag(s_lbl_axis_thresh, LV_OBJ_FLAG_HIDDEN);

        /* Park the threshold caption next to the line it labels. */
        int span = HUMID_AXIS_MAX - HUMID_AXIS_MIN;
        int y = 34 + 268 - (int)((int64_t)(HUMID_THRESHOLD - HUMID_AXIS_MIN) * 268 / span);
        lv_obj_set_pos(s_lbl_axis_thresh, 4, y - 8);
    } else {
        for (int p = 0; p < HISTORY_POINTS; p++) s_ythresh[p] = LV_CHART_POINT_NONE;
        lv_obj_add_flag(s_lbl_axis_thresh, LV_OBJ_FLAG_HIDDEN);

        if (lo > hi) { lo = 1500; hi = 2500; }        /* nothing collected yet */
        int32_t amin = lo - TEMP_AXIS_PAD;
        int32_t amax = hi + TEMP_AXIS_PAD;
        if (amax - amin < 200) amax = amin + 200;     /* never squash to a line */
        lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, amin, amax);

        char buf[16];
        lv_label_set_text(s_title, "Temperature");
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", amax / 100.0);
        lv_label_set_text(s_lbl_axis_top, buf);
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", amin / 100.0);
        lv_label_set_text(s_lbl_axis_bot, buf);
    }

    lv_label_set_text(s_lbl_x_left, X_LEFT_TEXT[s_range]);
    lv_chart_refresh(s_chart);
}
