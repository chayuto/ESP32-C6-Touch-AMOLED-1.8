#include "drawing_engine.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "draw_eng";

QueueHandle_t g_draw_queue;

static StaticQueue_t  s_queue_struct;
static uint8_t        s_queue_storage[DRAW_QUEUE_LEN * sizeof(draw_cmd_t)];

void drawing_engine_init(void)
{
    g_draw_queue = xQueueCreateStatic(DRAW_QUEUE_LEN, sizeof(draw_cmd_t),
                                      s_queue_storage, &s_queue_struct);
    ESP_LOGI(TAG, "Draw queue ready (depth=%d, cmd_size=%u bytes)",
             DRAW_QUEUE_LEN, (unsigned)sizeof(draw_cmd_t));
}

static bool push(draw_cmd_t *cmd)
{
    if (xQueueSend(g_draw_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full — command dropped");
        return false;
    }
    return true;
}

bool drawing_push_clear(uint8_t r, uint8_t g, uint8_t b)
{
    draw_cmd_t cmd = { .type = CMD_CLEAR, .clear_color = {r, g, b} };
    return push(&cmd);
}

bool drawing_push_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint8_t r, uint8_t g, uint8_t b,
                       uint8_t filled, uint8_t radius)
{
    draw_cmd_t cmd = {
        .type = CMD_DRAW_RECT,
        .rect = { .color={r,g,b}, .x=x, .y=y, .w=w, .h=h,
                  .filled=filled, .radius=radius },
    };
    return push(&cmd);
}

bool drawing_push_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t width)
{
    draw_cmd_t cmd = {
        .type = CMD_DRAW_LINE,
        .line = { .color={r,g,b}, .x1=x1, .y1=y1, .x2=x2, .y2=y2,
                  .width=width ? width : 1 },
    };
    return push(&cmd);
}

bool drawing_push_arc(int16_t cx, int16_t cy, int16_t radius,
                      int16_t start_angle, int16_t end_angle,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t width)
{
    draw_cmd_t cmd = {
        .type = CMD_DRAW_ARC,
        .arc  = { .color={r,g,b}, .cx=cx, .cy=cy, .radius=radius,
                  .start_angle=start_angle, .end_angle=end_angle,
                  .width=width ? width : 2 },
    };
    return push(&cmd);
}

bool drawing_push_text(int16_t x, int16_t y,
                       uint8_t r, uint8_t g, uint8_t b,
                       uint8_t font_size, const char *text)
{
    draw_cmd_t cmd = {
        .type = CMD_DRAW_TEXT,
        .text = { .color={r,g,b}, .x=x, .y=y, .font_size=font_size },
    };
    strncpy(cmd.text.text, text, MAX_TEXT_LEN);
    cmd.text.text[MAX_TEXT_LEN] = '\0';
    return push(&cmd);
}

bool drawing_push_path(const lv_point_t *pts, uint8_t pt_cnt,
                       uint8_t closed, uint8_t filled,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t width)
{
    if (pt_cnt < 2 || pt_cnt > MAX_POLY_PTS) return false;
    draw_cmd_t cmd = {
        .type = CMD_DRAW_PATH,
        .path = { .color={r,g,b}, .pt_cnt=pt_cnt,
                  .closed=closed, .filled=filled,
                  .width=width ? width : 1 },
    };
    memcpy(cmd.path.pts, pts, pt_cnt * sizeof(lv_point_t));
    return push(&cmd);
}
