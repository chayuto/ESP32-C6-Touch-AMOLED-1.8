#include "ui_display.h"
#include "drawing_engine.h"
#include "esp_log.h"

static const char *TAG = "ui";

static lv_obj_t *s_draw_screen = NULL;
static int        s_obj_count  = 0;

void ui_display_init(void)
{
    /* Create a clean black screen for drawing */
    s_draw_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_draw_screen, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(s_draw_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_draw_screen, 0, 0);
    lv_obj_set_style_pad_all(s_draw_screen, 0, 0);
    lv_obj_clear_flag(s_draw_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_scr_load(s_draw_screen);

    s_obj_count = 0;
    ESP_LOGI(TAG, "Draw screen ready (obj cap: %d)", MAX_OBJECTS);
}

void ui_display_show_connecting(void)
{
    drawing_push_clear(0, 0, 0);
    drawing_push_text(20, 200, 0, 180, 100, 14, "Connecting to Wi-Fi...");
}

void ui_display_show_ip(const char *ip_str)
{
    char line[64];
    drawing_push_clear(0, 0, 0);
    snprintf(line, sizeof(line), "IP: %s", ip_str);
    drawing_push_text(20, 160, 0, 220, 120, 20, line);
    drawing_push_text(20, 195, 60, 60, 60, 14, "POST /mcp");
    drawing_push_text(20, 215, 60, 60, 60, 14, "esp32-canvas.local");
    drawing_push_text(20, 250, 0, 160, 80, 20, "MCP Ready");
}

int ui_display_get_obj_count(void)
{
    return s_obj_count;
}

/* ── Render timer: drain queue, create widgets ───────────── */

void ui_render_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_draw_screen) return;

    draw_cmd_t cmd;
    while (xQueueReceive(g_draw_queue, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {

        case CMD_CLEAR: {
            lv_obj_clean(s_draw_screen);
            s_obj_count = 0;
            lv_obj_set_style_bg_color(s_draw_screen,
                lv_color_make(cmd.clear_color.r, cmd.clear_color.g, cmd.clear_color.b), 0);
            lv_obj_set_style_bg_opa(s_draw_screen, LV_OPA_COVER, 0);
            ESP_LOGD(TAG, "clear (bg=%d,%d,%d)",
                     cmd.clear_color.r, cmd.clear_color.g, cmd.clear_color.b);
            break;
        }

        case CMD_DRAW_RECT: {
            if (s_obj_count >= MAX_OBJECTS) {
                ESP_LOGW(TAG, "Object cap reached (%d/%d) — rect dropped", s_obj_count, MAX_OBJECTS);
                break;
            }
            lv_obj_t *obj = lv_obj_create(s_draw_screen);
            lv_obj_remove_style_all(obj);
            lv_obj_set_pos(obj, cmd.rect.x, cmd.rect.y);
            lv_obj_set_size(obj, cmd.rect.w, cmd.rect.h);
            lv_obj_set_style_radius(obj, cmd.rect.radius, 0);
            if (cmd.rect.filled) {
                lv_obj_set_style_bg_color(obj,
                    lv_color_make(cmd.rect.color.r, cmd.rect.color.g, cmd.rect.color.b), 0);
                lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_color(obj,
                    lv_color_make(cmd.rect.color.r, cmd.rect.color.g, cmd.rect.color.b), 0);
                lv_obj_set_style_border_width(obj, 1, 0);
                lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
            }
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            s_obj_count++;
            ESP_LOGD(TAG, "+rect obj_count=%d/%d", s_obj_count, MAX_OBJECTS);
            break;
        }

        case CMD_DRAW_TEXT: {
            if (s_obj_count >= MAX_OBJECTS) {
                ESP_LOGW(TAG, "Object cap reached (%d/%d) — text dropped", s_obj_count, MAX_OBJECTS);
                break;
            }
            lv_obj_t *label = lv_label_create(s_draw_screen);
            lv_label_set_text(label, cmd.text.text);
            lv_obj_set_pos(label, cmd.text.x, cmd.text.y);
            lv_obj_set_style_text_color(label,
                lv_color_make(cmd.text.color.r, cmd.text.color.g, cmd.text.color.b), 0);
            lv_obj_set_style_text_font(label,
                (cmd.text.font_size >= 20) ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
            s_obj_count++;
            ESP_LOGD(TAG, "+text obj_count=%d/%d", s_obj_count, MAX_OBJECTS);
            break;
        }

        case CMD_DRAW_LINE: {
            if (s_obj_count >= MAX_OBJECTS) break;
            lv_obj_t *line = lv_line_create(s_draw_screen);
            /* lv_line needs persistent point arrays — allocate from LVGL heap */
            lv_point_t *pts = lv_mem_alloc(2 * sizeof(lv_point_t));
            if (pts) {
                pts[0].x = cmd.line.x1; pts[0].y = cmd.line.y1;
                pts[1].x = cmd.line.x2; pts[1].y = cmd.line.y2;
                lv_line_set_points(line, pts, 2);
            }
            lv_obj_set_style_line_color(line,
                lv_color_make(cmd.line.color.r, cmd.line.color.g, cmd.line.color.b), 0);
            lv_obj_set_style_line_width(line, cmd.line.width, 0);
            s_obj_count++;
            break;
        }

        case CMD_DRAW_ARC: {
            if (s_obj_count >= MAX_OBJECTS) break;
            lv_obj_t *arc = lv_arc_create(s_draw_screen);
            lv_obj_remove_style_all(arc);
            /* Size the arc widget to encompass the circle */
            int size = cmd.arc.radius * 2 + cmd.arc.width;
            lv_obj_set_size(arc, size, size);
            lv_obj_set_pos(arc, cmd.arc.cx - size / 2, cmd.arc.cy - size / 2);
            lv_arc_set_bg_angles(arc, cmd.arc.start_angle, cmd.arc.end_angle);
            lv_arc_set_rotation(arc, 0);
            lv_obj_set_style_arc_color(arc,
                lv_color_make(cmd.arc.color.r, cmd.arc.color.g, cmd.arc.color.b),
                LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(arc, cmd.arc.width, LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_arc_set_value(arc, 100);
            lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
            s_obj_count++;
            break;
        }

        case CMD_DRAW_PATH: {
            if (s_obj_count >= MAX_OBJECTS) break;
            if (!cmd.path.closed) {
                /* Open polyline → lv_line */
                lv_obj_t *line = lv_line_create(s_draw_screen);
                lv_point_t *pts = lv_mem_alloc(cmd.path.pt_cnt * sizeof(lv_point_t));
                if (pts) {
                    memcpy(pts, cmd.path.pts, cmd.path.pt_cnt * sizeof(lv_point_t));
                    lv_line_set_points(line, pts, cmd.path.pt_cnt);
                }
                lv_obj_set_style_line_color(line,
                    lv_color_make(cmd.path.color.r, cmd.path.color.g, cmd.path.color.b), 0);
                lv_obj_set_style_line_width(line, cmd.path.width, 0);
            } else {
                /* Closed polygon — approximate with bordered rectangle at bounding box.
                   Full polygon rendering not supported by LVGL widgets without canvas. */
                int min_x = 32767, min_y = 32767, max_x = 0, max_y = 0;
                for (int i = 0; i < cmd.path.pt_cnt; i++) {
                    if (cmd.path.pts[i].x < min_x) min_x = cmd.path.pts[i].x;
                    if (cmd.path.pts[i].y < min_y) min_y = cmd.path.pts[i].y;
                    if (cmd.path.pts[i].x > max_x) max_x = cmd.path.pts[i].x;
                    if (cmd.path.pts[i].y > max_y) max_y = cmd.path.pts[i].y;
                }
                lv_obj_t *obj = lv_obj_create(s_draw_screen);
                lv_obj_remove_style_all(obj);
                lv_obj_set_pos(obj, min_x, min_y);
                lv_obj_set_size(obj, max_x - min_x + 1, max_y - min_y + 1);
                if (cmd.path.filled) {
                    lv_obj_set_style_bg_color(obj,
                        lv_color_make(cmd.path.color.r, cmd.path.color.g, cmd.path.color.b), 0);
                    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
                } else {
                    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_color(obj,
                        lv_color_make(cmd.path.color.r, cmd.path.color.g, cmd.path.color.b), 0);
                    lv_obj_set_style_border_width(obj, cmd.path.width, 0);
                }
                lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            }
            s_obj_count++;
            break;
        }

        } /* switch */
    } /* while */
}
