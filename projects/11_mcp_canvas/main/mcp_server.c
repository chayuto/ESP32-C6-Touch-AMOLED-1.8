#include "mcp_server.h"
#include "drawing_engine.h"
#include "ui_display.h"
#include "snapshot.h"
#include "amoled.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mdns.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static const char *TAG = "mcp";

/* ── Wi-Fi ─────────────────────────────────────────────────── */

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      5

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(char *ip_out, size_t ip_len)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_wifi, inst_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = CONFIG_CANVAS_WIFI_SSID,
            .password = CONFIG_CANVAS_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi connecting to \"%s\"...", CONFIG_CANVAS_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi connection failed");
        return ESP_FAIL;
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "Modem sleep disabled");

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_get_ip_info(netif, &ip_info);
    snprintf(ip_out, ip_len, IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Connected, IP: %s", ip_out);
    return ESP_OK;
}

/* ── Cached tools/list JSON ────────────────────────────────── */

static char *s_tools_json = NULL;

static void build_tools_json(void)
{
    cJSON *tools = cJSON_CreateArray();

#define BEGIN_TOOL(name_str, desc_str) \
    do { \
        cJSON *_t = cJSON_CreateObject(); \
        cJSON_AddStringToObject(_t, "name", name_str); \
        cJSON_AddStringToObject(_t, "description", desc_str); \
        cJSON *_schema = cJSON_CreateObject(); \
        cJSON_AddStringToObject(_schema, "type", "object"); \
        cJSON *props = cJSON_CreateObject();

#define END_TOOL() \
        cJSON_AddItemToObject(_schema, "properties", props); \
        cJSON_AddItemToObject(_t, "inputSchema", _schema); \
        cJSON_AddItemToArray(tools, _t); \
    } while (0)

#define INT_PROP(key) do { \
        cJSON *_p = cJSON_CreateObject(); \
        cJSON_AddStringToObject(_p, "type", "integer"); \
        cJSON_AddItemToObject(props, key, _p); \
    } while (0)

#define BOOL_PROP(key) do { \
        cJSON *_p = cJSON_CreateObject(); \
        cJSON_AddStringToObject(_p, "type", "boolean"); \
        cJSON_AddItemToObject(props, key, _p); \
    } while (0)

#define STR_PROP(key) do { \
        cJSON *_p = cJSON_CreateObject(); \
        cJSON_AddStringToObject(_p, "type", "string"); \
        cJSON_AddItemToObject(props, key, _p); \
    } while (0)

#define ARR_PROP(key) do { \
        cJSON *_p = cJSON_CreateObject(); \
        cJSON_AddStringToObject(_p, "type", "array"); \
        cJSON_AddItemToObject(props, key, _p); \
    } while (0)

    BEGIN_TOOL("clear_canvas",
        "Fill the entire 368x448 AMOLED display with a solid colour. "
        "Call before drawing a new scene to erase previous content. "
        "Params: r, g, b (0-255 each, all optional, default 0).");
    INT_PROP("r"); INT_PROP("g"); INT_PROP("b");
    END_TOOL();

    BEGIN_TOOL("draw_rect",
        "Draw a filled or outlined rectangle. "
        "x,y: top-left corner (x: 0-367, y: 0-447). w,h: size in pixels (min 1). "
        "r,g,b: colour 0-255 (default 255). filled: true=solid, false=outline (default true). "
        "radius: corner rounding 0-20 (optional, default 0).");
    INT_PROP("x"); INT_PROP("y"); INT_PROP("w"); INT_PROP("h");
    INT_PROP("r"); INT_PROP("g"); INT_PROP("b");
    BOOL_PROP("filled"); INT_PROP("radius");
    END_TOOL();

    BEGIN_TOOL("draw_line",
        "Draw a straight line from (x1,y1) to (x2,y2). "
        "Coordinates: x 0-367, y 0-447. r,g,b: colour 0-255 (default 255). "
        "width: stroke width 1-10 pixels (optional, default 1).");
    INT_PROP("x1"); INT_PROP("y1"); INT_PROP("x2"); INT_PROP("y2");
    INT_PROP("r");  INT_PROP("g");  INT_PROP("b");  INT_PROP("width");
    END_TOOL();

    BEGIN_TOOL("draw_arc",
        "Draw a circular arc. cx,cy: centre (x: 0-367, y: 0-447). radius: 1-224. "
        "start_angle/end_angle: 0-360 degrees (0=3-o-clock, increases clockwise). "
        "Full circle: start=0 end=360. r,g,b: colour 0-255 (default 255). "
        "width: stroke width 1-10 (optional, default 2).");
    INT_PROP("cx"); INT_PROP("cy"); INT_PROP("radius");
    INT_PROP("start_angle"); INT_PROP("end_angle");
    INT_PROP("r"); INT_PROP("g"); INT_PROP("b"); INT_PROP("width");
    END_TOOL();

    BEGIN_TOOL("draw_text",
        "Render a text string. x,y: top-left of text box (x: 0-367, y: 0-447). "
        "r,g,b: colour 0-255 (default 255). "
        "font_size: must be exactly 14 or 20 (Montserrat, only these two sizes available). "
        "text: UTF-8 string up to 255 chars; wraps at screen edge.");
    INT_PROP("x"); INT_PROP("y");
    INT_PROP("r"); INT_PROP("g"); INT_PROP("b");
    INT_PROP("font_size"); STR_PROP("text");
    END_TOOL();

    BEGIN_TOOL("draw_path",
        "Draw a polyline (open) or polygon (closed). "
        "points: array of {\"x\":N,\"y\":N} objects, 2 to 8 points. "
        "closed: false=polyline, true=polygon (default false). "
        "filled: true=solid fill, only effective when closed=true (default false). "
        "r,g,b: colour 0-255 (default 255). width: stroke 1-10 (optional, default 1).");
    ARR_PROP("points");
    BOOL_PROP("closed"); BOOL_PROP("filled");
    INT_PROP("r"); INT_PROP("g"); INT_PROP("b"); INT_PROP("width");
    END_TOOL();

    BEGIN_TOOL("get_canvas_info",
        "Returns screen dimensions, object count, draw queue depth, and available primitives. "
        "Call this first to understand the display before drawing. No parameters.");
    (void)props;
    END_TOOL();

    BEGIN_TOOL("get_canvas_snapshot",
        "Captures the current display as a JPEG image (92x112 px, 4x downsampled) "
        "and returns it as a base64-encoded MCP image item so you can see the display. "
        "Encoding takes ~500 ms. Call after drawing to verify results. No parameters.");
    (void)props;
    END_TOOL();

    // ── Device tools ──

    BEGIN_TOOL("get_battery_info",
        "Returns battery voltage (mV), charge percentage (0-100), charging state, "
        "and USB power status from the AXP2101 PMIC. No parameters.");
    (void)props;
    END_TOOL();

    BEGIN_TOOL("set_brightness",
        "Set AMOLED display brightness. level: 0 (off) to 255 (max). Default is 208.");
    INT_PROP("level");
    END_TOOL();

    BEGIN_TOOL("get_system_info",
        "Returns free heap, minimum free heap, uptime, WiFi RSSI, and IP address. "
        "Useful for monitoring device health. No parameters.");
    (void)props;
    END_TOOL();

    BEGIN_TOOL("power_off",
        "Triggers software shutdown via AXP2101 PMIC. Cuts all power rails. "
        "Device can be powered back on by pressing the power button. "
        "WARNING: This will turn off the device immediately.");
    (void)props;
    END_TOOL();

#undef BEGIN_TOOL
#undef END_TOOL
#undef INT_PROP
#undef BOOL_PROP
#undef STR_PROP
#undef ARR_PROP

    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "tools", tools);
    s_tools_json = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    ESP_LOGI(TAG, "Tools JSON cached (%zu bytes)", strlen(s_tools_json));
}

/* ── JSON-RPC helpers ──────────────────────────────────────── */

static void send_json_str(httpd_req_t *req, const char *str)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
}

static void send_json_obj(httpd_req_t *req, cJSON *obj)
{
    char *str = cJSON_PrintUnformatted(obj);
    send_json_str(req, str);
    free(str);
    cJSON_Delete(obj);
}

static void send_result(httpd_req_t *req, int id, cJSON *result)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(r, "id", id);
    cJSON_AddItemToObject(r, "result", result);
    send_json_obj(req, r);
}

static void send_error(httpd_req_t *req, int id, int code, const char *msg)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    if (id >= 0) cJSON_AddNumberToObject(r, "id", id);
    else         cJSON_AddNullToObject(r, "id");
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg);
    cJSON_AddItemToObject(r, "error", err);
    send_json_obj(req, r);
}

static cJSON *text_result(const char *msg)
{
    cJSON *res = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", msg);
    cJSON_AddItemToArray(content, item);
    cJSON_AddItemToObject(res, "content", content);
    return res;
}

/* ── Parameter helpers ─────────────────────────────────────── */

static bool get_int(cJSON *args, const char *key,
                    int min, int max, int def_val, bool required,
                    int *out, char errbuf[128])
{
    if (!args) {
        if (required) { snprintf(errbuf, 128, "Missing required param: %s", key); return false; }
        *out = def_val; return true;
    }
    cJSON *item = cJSON_GetObjectItem(args, key);
    if (!item) {
        if (required) { snprintf(errbuf, 128, "Missing required param: %s", key); return false; }
        *out = def_val; return true;
    }
    if (!cJSON_IsNumber(item)) {
        snprintf(errbuf, 128, "Param '%s' must be integer", key); return false;
    }
    int v = (int)item->valuedouble;
    if (v < min || v > max) {
        snprintf(errbuf, 128, "Param '%s' out of range [%d,%d]: %d", key, min, max, v);
        return false;
    }
    *out = v; return true;
}

static bool get_bool(cJSON *args, const char *key, bool def_val, bool *out)
{
    if (!args) { *out = def_val; return true; }
    cJSON *item = cJSON_GetObjectItem(args, key);
    if (!item) { *out = def_val; return true; }
    *out = cJSON_IsTrue(item);
    return true;
}

/* ── Tool handlers ─────────────────────────────────────────── */

static void handle_clear_canvas(httpd_req_t *req, int id, cJSON *args)
{
    char errbuf[128]; int r, g, b;
    if (!get_int(args, "r", 0, 255, 0, false, &r, errbuf) ||
        !get_int(args, "g", 0, 255, 0, false, &g, errbuf) ||
        !get_int(args, "b", 0, 255, 0, false, &b, errbuf)) {
        send_error(req, id, -32602, errbuf); return;
    }
    drawing_push_clear(r, g, b);
    send_result(req, id, text_result("Canvas cleared"));
}

static void handle_draw_rect(httpd_req_t *req, int id, cJSON *args)
{
    char errbuf[128]; int x, y, w, h, r, g, b, radius; bool filled;
    if (!get_int(args, "x",      0, SCREEN_W - 1, 0,   true,  &x,      errbuf) ||
        !get_int(args, "y",      0, SCREEN_H - 1, 0,   true,  &y,      errbuf) ||
        !get_int(args, "w",      1, SCREEN_W,     10,  true,  &w,      errbuf) ||
        !get_int(args, "h",      1, SCREEN_H,     10,  true,  &h,      errbuf) ||
        !get_int(args, "r",      0, 255,          255, false, &r,      errbuf) ||
        !get_int(args, "g",      0, 255,          255, false, &g,      errbuf) ||
        !get_int(args, "b",      0, 255,          255, false, &b,      errbuf) ||
        !get_int(args, "radius", 0, 20,           0,   false, &radius, errbuf)) {
        send_error(req, id, -32602, errbuf); return;
    }
    get_bool(args, "filled", true, &filled);
    if (ui_display_get_obj_count() >= MAX_OBJECTS) {
        send_error(req, id, -32603, "Canvas full — call clear_canvas"); return;
    }
    drawing_push_rect(x, y, w, h, r, g, b, filled ? 1 : 0, radius);
    send_result(req, id, text_result("Rectangle drawn"));
}

static void handle_draw_line(httpd_req_t *req, int id, cJSON *args)
{
    char errbuf[128]; int x1, y1, x2, y2, r, g, b, width;
    if (!get_int(args, "x1",    0, SCREEN_W - 1, 0,   true,  &x1,   errbuf) ||
        !get_int(args, "y1",    0, SCREEN_H - 1, 0,   true,  &y1,   errbuf) ||
        !get_int(args, "x2",    0, SCREEN_W - 1, 0,   true,  &x2,   errbuf) ||
        !get_int(args, "y2",    0, SCREEN_H - 1, 0,   true,  &y2,   errbuf) ||
        !get_int(args, "r",     0, 255,          255, false, &r,    errbuf) ||
        !get_int(args, "g",     0, 255,          255, false, &g,    errbuf) ||
        !get_int(args, "b",     0, 255,          255, false, &b,    errbuf) ||
        !get_int(args, "width", 1, 10,           1,   false, &width,errbuf)) {
        send_error(req, id, -32602, errbuf); return;
    }
    if (ui_display_get_obj_count() >= MAX_OBJECTS) {
        send_error(req, id, -32603, "Canvas full — call clear_canvas"); return;
    }
    drawing_push_line(x1, y1, x2, y2, r, g, b, width);
    send_result(req, id, text_result("Line drawn"));
}

static void handle_draw_arc(httpd_req_t *req, int id, cJSON *args)
{
    char errbuf[128]; int cx, cy, radius, sa, ea, r, g, b, width;
    if (!get_int(args, "cx",          0, SCREEN_W - 1, 184, true,  &cx,    errbuf) ||
        !get_int(args, "cy",          0, SCREEN_H - 1, 224, true,  &cy,    errbuf) ||
        !get_int(args, "radius",      1, 224,          50,  true,  &radius,errbuf) ||
        !get_int(args, "start_angle", 0, 360,          0,   true,  &sa,    errbuf) ||
        !get_int(args, "end_angle",   0, 360,          360, true,  &ea,    errbuf) ||
        !get_int(args, "r",           0, 255,          255, false, &r,     errbuf) ||
        !get_int(args, "g",           0, 255,          255, false, &g,     errbuf) ||
        !get_int(args, "b",           0, 255,          255, false, &b,     errbuf) ||
        !get_int(args, "width",       1, 10,           2,   false, &width, errbuf)) {
        send_error(req, id, -32602, errbuf); return;
    }
    if (ui_display_get_obj_count() >= MAX_OBJECTS) {
        send_error(req, id, -32603, "Canvas full — call clear_canvas"); return;
    }
    drawing_push_arc(cx, cy, radius, sa, ea, r, g, b, width);
    send_result(req, id, text_result("Arc drawn"));
}

static void handle_draw_text(httpd_req_t *req, int id, cJSON *args)
{
    char errbuf[128]; int x, y, r, g, b, font_size;
    if (!get_int(args, "x",         0, SCREEN_W - 1, 0,  true,  &x,        errbuf) ||
        !get_int(args, "y",         0, SCREEN_H - 1, 0,  true,  &y,        errbuf) ||
        !get_int(args, "r",         0, 255,          255, false, &r,        errbuf) ||
        !get_int(args, "g",         0, 255,          255, false, &g,        errbuf) ||
        !get_int(args, "b",         0, 255,          255, false, &b,        errbuf) ||
        !get_int(args, "font_size", 14, 20,          14,  false, &font_size,errbuf)) {
        send_error(req, id, -32602, errbuf); return;
    }
    if (font_size != 14 && font_size != 20) {
        send_error(req, id, -32602, "font_size must be exactly 14 or 20");
        return;
    }
    cJSON *text_item = args ? cJSON_GetObjectItem(args, "text") : NULL;
    if (!text_item || !cJSON_IsString(text_item)) {
        send_error(req, id, -32602, "Missing required param: text"); return;
    }
    if (ui_display_get_obj_count() >= MAX_OBJECTS) {
        send_error(req, id, -32603, "Canvas full — call clear_canvas"); return;
    }
    drawing_push_text(x, y, r, g, b, font_size, text_item->valuestring);
    send_result(req, id, text_result("Text drawn"));
}

static void handle_draw_path(httpd_req_t *req, int id, cJSON *args)
{
    char errbuf[128];
    cJSON *pts_arr = args ? cJSON_GetObjectItem(args, "points") : NULL;
    if (!pts_arr || !cJSON_IsArray(pts_arr)) {
        send_error(req, id, -32602, "Missing required param: points (array)"); return;
    }
    int pt_cnt = cJSON_GetArraySize(pts_arr);
    if (pt_cnt < 2 || pt_cnt > MAX_POLY_PTS) {
        snprintf(errbuf, sizeof(errbuf),
                 "points must have 2-%d entries, got %d", MAX_POLY_PTS, pt_cnt);
        send_error(req, id, -32602, errbuf); return;
    }
    lv_point_t pts[MAX_POLY_PTS];
    for (int i = 0; i < pt_cnt; i++) {
        cJSON *pt = cJSON_GetArrayItem(pts_arr, i);
        cJSON *px = pt ? cJSON_GetObjectItem(pt, "x") : NULL;
        cJSON *py = pt ? cJSON_GetObjectItem(pt, "y") : NULL;
        if (!px || !py || !cJSON_IsNumber(px) || !cJSON_IsNumber(py)) {
            send_error(req, id, -32602, "Each point must have integer x and y"); return;
        }
        pts[i].x = (lv_coord_t)px->valuedouble;
        pts[i].y = (lv_coord_t)py->valuedouble;
    }
    int r, g, b, width; bool closed, filled;
    if (!get_int(args, "r",     0, 255, 255, false, &r,     errbuf) ||
        !get_int(args, "g",     0, 255, 255, false, &g,     errbuf) ||
        !get_int(args, "b",     0, 255, 255, false, &b,     errbuf) ||
        !get_int(args, "width", 1, 10,  1,   false, &width, errbuf)) {
        send_error(req, id, -32602, errbuf); return;
    }
    get_bool(args, "closed", false, &closed);
    get_bool(args, "filled", false, &filled);
    if (ui_display_get_obj_count() >= MAX_OBJECTS) {
        send_error(req, id, -32603, "Canvas full — call clear_canvas"); return;
    }
    drawing_push_path(pts, pt_cnt, closed ? 1 : 0, filled ? 1 : 0, r, g, b, width);
    send_result(req, id, text_result("Path drawn"));
}

static void handle_get_canvas_info(httpd_req_t *req, int id)
{
    char info[256];
    snprintf(info, sizeof(info),
             "Display: %dx%d px AMOLED | Objects: %d/%d | Queue: %lu/%d | "
             "Fonts: 14, 20 | Primitives: clear_canvas draw_rect draw_line "
             "draw_arc draw_text draw_path",
             SCREEN_W, SCREEN_H,
             ui_display_get_obj_count(), MAX_OBJECTS,
             (unsigned long)uxQueueMessagesWaiting(g_draw_queue),
             DRAW_QUEUE_LEN);
    send_result(req, id, text_result(info));
}

static void handle_get_battery_info(httpd_req_t *req, int id)
{
    amoled_battery_info_t bat;
    if (amoled_get_battery_info(&bat) != ESP_OK) {
        send_error(req, id, -32603, "Battery read failed (AXP2101 not available)");
        return;
    }
    char info[256];
    snprintf(info, sizeof(info),
             "Battery: %dmV (%d%%) | Charging: %s | USB: %s | Battery: %s",
             bat.voltage_mv, bat.percentage,
             bat.charging ? "yes" : "no",
             bat.vbus_present ? "connected" : "disconnected",
             bat.battery_present ? "present" : "not detected");
    send_result(req, id, text_result(info));
}

static void handle_set_brightness(httpd_req_t *req, int id, cJSON *args)
{
    char errbuf[128]; int level;
    if (!get_int(args, "level", 0, 255, 208, true, &level, errbuf)) {
        send_error(req, id, -32602, errbuf); return;
    }
    amoled_set_brightness((uint8_t)level);
    char msg[64];
    snprintf(msg, sizeof(msg), "Brightness set to %d/255", level);
    send_result(req, id, text_result(msg));
}

static void handle_get_system_info(httpd_req_t *req, int id)
{
    wifi_ap_record_t ap_info;
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    esp_netif_ip_info_t ip_info;
    char ip_str[16] = "N/A";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }

    char info[256];
    snprintf(info, sizeof(info),
             "Free heap: %lu B | Min free: %lu B | Uptime: %llu s | "
             "WiFi RSSI: %d dBm | IP: %s",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             (unsigned long long)(esp_timer_get_time() / 1000000),
             rssi, ip_str);
    send_result(req, id, text_result(info));
}

static void handle_power_off(httpd_req_t *req, int id)
{
    send_result(req, id, text_result("Shutting down..."));
    vTaskDelay(pdMS_TO_TICKS(200)); /* Let HTTP response flush */
    amoled_power_off();
    /* Should not reach here — AXP2101 cuts power */
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void handle_get_canvas_snapshot(httpd_req_t *req, int id)
{
    unsigned char *b64     = NULL;
    size_t         b64_len = 0;
    if (!snapshot_encode(&b64, &b64_len)) {
        send_error(req, id, -32603, "Snapshot encode failed"); return;
    }
    cJSON *res     = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *img     = cJSON_CreateObject();
    cJSON_AddStringToObject(img, "type",     "image");
    cJSON_AddStringToObject(img, "data",     (char *)b64);
    cJSON_AddStringToObject(img, "mimeType", "image/jpeg");
    cJSON_AddItemToArray(content, img);
    cJSON_AddItemToObject(res, "content", content);
    free(b64);
    send_result(req, id, res);
}

/* ── MCP method routers ────────────────────────────────────── */

static void handle_initialize(httpd_req_t *req, int id)
{
    cJSON *res  = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "protocolVersion", "2024-11-05");
    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name",    "esp32-canvas");
    cJSON_AddStringToObject(info, "version", "1.0.0");
    cJSON_AddItemToObject(res, "serverInfo", info);
    cJSON *caps  = cJSON_CreateObject();
    cJSON *tools_cap = cJSON_CreateObject();
    cJSON_AddItemToObject(caps, "tools", tools_cap);
    cJSON_AddItemToObject(res, "capabilities", caps);
    send_result(req, id, res);
}

static void handle_tools_list(httpd_req_t *req, int id)
{
    char *resp = NULL;
    asprintf(&resp, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, s_tools_json);
    if (resp) { send_json_str(req, resp); free(resp); }
    else      { send_error(req, id, -32603, "OOM"); }
}

static void handle_tools_call(httpd_req_t *req, int id, cJSON *params)
{
    cJSON *name_item = params ? cJSON_GetObjectItem(params, "name")      : NULL;
    cJSON *args      = params ? cJSON_GetObjectItem(params, "arguments") : NULL;

    if (!name_item || !cJSON_IsString(name_item)) {
        send_error(req, id, -32602, "tools/call: missing 'name'"); return;
    }
    const char *tool = name_item->valuestring;

    ESP_LOGD(TAG, "tools/call %s", tool);

    if      (strcmp(tool, "clear_canvas")       == 0) handle_clear_canvas(req, id, args);
    else if (strcmp(tool, "draw_rect")           == 0) handle_draw_rect(req, id, args);
    else if (strcmp(tool, "draw_line")           == 0) handle_draw_line(req, id, args);
    else if (strcmp(tool, "draw_arc")            == 0) handle_draw_arc(req, id, args);
    else if (strcmp(tool, "draw_text")           == 0) handle_draw_text(req, id, args);
    else if (strcmp(tool, "draw_path")           == 0) handle_draw_path(req, id, args);
    else if (strcmp(tool, "get_canvas_info")     == 0) handle_get_canvas_info(req, id);
    else if (strcmp(tool, "get_canvas_snapshot") == 0) handle_get_canvas_snapshot(req, id);
    else if (strcmp(tool, "get_battery_info")    == 0) handle_get_battery_info(req, id);
    else if (strcmp(tool, "set_brightness")      == 0) handle_set_brightness(req, id, args);
    else if (strcmp(tool, "get_system_info")     == 0) handle_get_system_info(req, id);
    else if (strcmp(tool, "power_off")           == 0) handle_power_off(req, id);
    else send_error(req, id, -32601, "Unknown tool");
}

/* ── HTTP handlers ─────────────────────────────────────────── */

#define RECV_BUF_SIZE 2048

static esp_err_t mcp_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len >= RECV_BUF_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request size");
        return ESP_OK;
    }
    char buf[RECV_BUF_SIZE];
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed");
        return ESP_OK;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        send_json_str(req,
            "{\"jsonrpc\":\"2.0\",\"id\":null,"
            "\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
        return ESP_OK;
    }

    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    cJSON *method  = cJSON_GetObjectItem(root, "method");
    int    id      = (id_item && cJSON_IsNumber(id_item)) ? (int)id_item->valuedouble : 0;

    if (!method || !cJSON_IsString(method)) {
        send_error(req, id, -32600, "Invalid Request: missing method");
        cJSON_Delete(root); return ESP_OK;
    }

    const char *m = method->valuestring;
    if (strcmp(m, "initialize") == 0) {
        handle_initialize(req, id);
    } else if (strcmp(m, "tools/list") == 0) {
        handle_tools_list(req, id);
    } else if (strcmp(m, "tools/call") == 0) {
        handle_tools_call(req, id, cJSON_GetObjectItem(root, "params"));
    } else {
        send_error(req, id, -32601, "Method not found");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t ping_get_handler(httpd_req_t *req)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"healthy\",\"objects\":%d,\"objects_max\":%d,"
             "\"queue_depth\":%lu,\"queue_max\":%d}",
             ui_display_get_obj_count(), MAX_OBJECTS,
             (unsigned long)uxQueueMessagesWaiting(g_draw_queue),
             DRAW_QUEUE_LEN);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t snapshot_get_handler(httpd_req_t *req)
{
    static uint8_t s_jpeg_http_buf[20480];
    int jpeg_len = 0;
    if (!snapshot_encode_raw(s_jpeg_http_buf, sizeof(s_jpeg_http_buf), &jpeg_len)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Snapshot failed");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=canvas.jpg");
    httpd_resp_send(req, (char *)s_jpeg_http_buf, jpeg_len);
    return ESP_OK;
}

/* ── Server start ──────────────────────────────────────────── */

esp_err_t mcp_server_start(void)
{
    build_tools_json();

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp32-canvas"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-C6 AMOLED Canvas MCP"));
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: esp32-canvas.local");

    httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.stack_size        = 8192;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    static const httpd_uri_t mcp_uri = {
        .uri = "/mcp", .method = HTTP_POST, .handler = mcp_post_handler,
    };
    static const httpd_uri_t ping_uri = {
        .uri = "/ping", .method = HTTP_GET, .handler = ping_get_handler,
    };
    static const httpd_uri_t snap_uri = {
        .uri = "/snapshot.jpg", .method = HTTP_GET, .handler = snapshot_get_handler,
    };

    httpd_register_uri_handler(server, &mcp_uri);
    httpd_register_uri_handler(server, &ping_uri);
    httpd_register_uri_handler(server, &snap_uri);

    ESP_LOGI(TAG, "HTTP server: POST /mcp  GET /ping  GET /snapshot.jpg");
    return ESP_OK;
}
