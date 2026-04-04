#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

#define SCREEN_W        368
#define SCREEN_H        448
#define DRAW_QUEUE_LEN  16
#define MAX_POLY_PTS    8
#define MAX_TEXT_LEN    255

typedef enum {
    CMD_CLEAR,
    CMD_DRAW_RECT,
    CMD_DRAW_LINE,
    CMD_DRAW_ARC,
    CMD_DRAW_TEXT,
    CMD_DRAW_PATH,
} draw_cmd_type_t;

typedef struct { uint8_t r, g, b; } cmd_color_t;

typedef struct {
    cmd_color_t color;
    int16_t x, y, w, h;
    uint8_t filled;
    uint8_t radius;
} cmd_rect_t;

typedef struct {
    cmd_color_t color;
    int16_t x1, y1, x2, y2;
    uint8_t width;
} cmd_line_t;

typedef struct {
    cmd_color_t color;
    int16_t cx, cy, radius;
    int16_t start_angle, end_angle;
    uint8_t width;
} cmd_arc_t;

typedef struct {
    cmd_color_t color;
    int16_t x, y;
    uint8_t font_size;
    char text[MAX_TEXT_LEN + 1];
} cmd_text_t;

typedef struct {
    cmd_color_t  color;
    lv_point_t   pts[MAX_POLY_PTS];
    uint8_t      pt_cnt;
    uint8_t      closed;
    uint8_t      filled;
    uint8_t      width;
} cmd_path_t;

typedef struct {
    draw_cmd_type_t type;
    union {
        cmd_color_t clear_color;
        cmd_rect_t  rect;
        cmd_line_t  line;
        cmd_arc_t   arc;
        cmd_text_t  text;
        cmd_path_t  path;
    };
} draw_cmd_t;

extern QueueHandle_t g_draw_queue;

void drawing_engine_init(void);

bool drawing_push_clear(uint8_t r, uint8_t g, uint8_t b);
bool drawing_push_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint8_t r, uint8_t g, uint8_t b,
                       uint8_t filled, uint8_t radius);
bool drawing_push_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t width);
bool drawing_push_arc(int16_t cx, int16_t cy, int16_t radius,
                      int16_t start_angle, int16_t end_angle,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t width);
bool drawing_push_text(int16_t x, int16_t y,
                       uint8_t r, uint8_t g, uint8_t b,
                       uint8_t font_size, const char *text);
bool drawing_push_path(const lv_point_t *pts, uint8_t pt_cnt,
                       uint8_t closed, uint8_t filled,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t width);
