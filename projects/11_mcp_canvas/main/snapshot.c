#include "snapshot.h"
#include "drawing_engine.h"
#include "amoled.h"
#include "amoled_lvgl.h"

#include "esp_jpeg_enc.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "snap";

#define SNAP_W          92      /* SCREEN_W / 4 */
#define SNAP_H          112     /* SCREEN_H / 4 */
#define JPEG_BUF_SIZE   20480   /* 20 KB ceiling */

/* Static BSS buffers — no heap fragmentation */
static uint8_t s_snap_rgb888[SNAP_W * SNAP_H * 3]; /* 30,912 B */
static uint8_t s_snap_jpeg[JPEG_BUF_SIZE];          /* 20,480 B */

/* Synchronization between HTTP task (requester) and LVGL task (executor) */
static SemaphoreHandle_t s_snap_request_sem = NULL;  /* HTTP → LVGL: "please capture" */
static SemaphoreHandle_t s_snap_done_sem    = NULL;  /* LVGL → HTTP: "capture done" */
static volatile bool     s_snap_active      = false;
static int               s_snap_jpeg_len    = 0;
static bool              s_snap_ok          = false;

/* ── Flush hook: downsample each strip into snapshot buffer ── */

static void snap_flush_hook(const lv_area_t *area, lv_color_t *color_map)
{
    if (!s_snap_active) return;

    int w = area->x2 - area->x1 + 1;

    for (int y = area->y1; y <= area->y2; y++) {
        if (y % 4 != 0) continue;
        int snap_y = y / 4;
        if (snap_y >= SNAP_H) continue;

        for (int x = area->x1; x <= area->x2; x++) {
            if (x % 4 != 0) continue;
            int snap_x = x / 4;
            if (snap_x >= SNAP_W) continue;

            lv_color_t px = color_map[(y - area->y1) * w + (x - area->x1)];
            uint8_t *dst = &s_snap_rgb888[(snap_y * SNAP_W + snap_x) * 3];
            /* RGB565 → RGB888, handling LV_COLOR_16_SWAP */
            uint16_t raw = px.full;
#if LV_COLOR_16_SWAP
            raw = (uint16_t)((raw >> 8) | (raw << 8));
#endif
            dst[0] = (uint8_t)(((raw >> 11) & 0x1F) << 3);  /* R: 5→8 bit */
            dst[1] = (uint8_t)(((raw >> 5)  & 0x3F) << 2);  /* G: 6→8 bit */
            dst[2] = (uint8_t)(((raw)       & 0x1F) << 3);  /* B: 5→8 bit */
        }
    }
}

/* ── JPEG encode the RGB888 buffer ───────────────────────── */

static bool jpeg_encode(int *out_len)
{
    jpeg_enc_config_t cfg = {
        .width       = SNAP_W,
        .height      = SNAP_H,
        .src_type    = JPEG_PIXEL_FORMAT_RGB888,
        .subsampling = JPEG_SUBSAMPLE_444,
        .quality     = 35,
        .rotate      = JPEG_ROTATE_0D,
        .task_enable = false,
    };

    jpeg_enc_handle_t handle;
    if (jpeg_enc_open(&cfg, &handle) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_enc_open failed");
        return false;
    }

    int jpeg_len = 0;
    jpeg_error_t ret = jpeg_enc_process(handle,
        s_snap_rgb888, sizeof(s_snap_rgb888),
        s_snap_jpeg, JPEG_BUF_SIZE, &jpeg_len);
    jpeg_enc_close(handle);

    if (ret != JPEG_ERR_OK || jpeg_len <= 0) {
        ESP_LOGE(TAG, "jpeg_enc_process failed: %d", ret);
        return false;
    }

    ESP_LOGI(TAG, "JPEG encoded: %d bytes (%dx%d)", jpeg_len, SNAP_W, SNAP_H);
    *out_len = jpeg_len;
    return true;
}

/* ── Public API ──────────────────────────────────────────── */

void snapshot_init(void)
{
    s_snap_request_sem = xSemaphoreCreateBinary();
    s_snap_done_sem    = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "Snapshot subsystem ready (output: %dx%d)", SNAP_W, SNAP_H);
}

void snapshot_process(void)
{
    /* Called from LVGL render timer — check if HTTP task requested a snapshot */
    if (xSemaphoreTake(s_snap_request_sem, 0) != pdTRUE) return;

    ESP_LOGI(TAG, "Capture started");

    /* Clear the snapshot buffer */
    memset(s_snap_rgb888, 0, sizeof(s_snap_rgb888));

    /* Install flush hook, force full-screen redraw */
    s_snap_active = true;
    amoled_lvgl_set_flush_hook(snap_flush_hook);

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);   /* Synchronous — all flush_cb calls happen here */

    amoled_lvgl_set_flush_hook(NULL);
    s_snap_active = false;

    /* JPEG encode */
    s_snap_ok = jpeg_encode(&s_snap_jpeg_len);

    ESP_LOGI(TAG, "Capture done (ok=%d, jpeg=%d bytes)", s_snap_ok, s_snap_jpeg_len);

    /* Signal the waiting HTTP task */
    xSemaphoreGive(s_snap_done_sem);
}

bool snapshot_encode_raw(uint8_t *out_buf, size_t out_cap, int *jpeg_len)
{
    /* Signal LVGL task to capture */
    xSemaphoreGive(s_snap_request_sem);

    /* Wait for capture to complete (timeout 2s) */
    if (xSemaphoreTake(s_snap_done_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Snapshot timeout");
        return false;
    }

    if (!s_snap_ok) return false;
    if ((size_t)s_snap_jpeg_len > out_cap) {
        ESP_LOGE(TAG, "Output buffer too small: need %d, have %zu", s_snap_jpeg_len, out_cap);
        return false;
    }
    memcpy(out_buf, s_snap_jpeg, s_snap_jpeg_len);
    *jpeg_len = s_snap_jpeg_len;
    return true;
}

bool snapshot_encode(unsigned char **b64_out, size_t *b64_len_out)
{
    /* Signal LVGL task to capture */
    xSemaphoreGive(s_snap_request_sem);

    /* Wait for capture to complete (timeout 2s) */
    if (xSemaphoreTake(s_snap_done_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Snapshot timeout");
        return false;
    }

    if (!s_snap_ok) return false;

    /* Base64 encode — output is heap-allocated, caller frees */
    size_t b64_cap = ((s_snap_jpeg_len + 2) / 3) * 4 + 4;
    unsigned char *b64 = malloc(b64_cap);
    if (!b64) {
        ESP_LOGE(TAG, "OOM for base64 (%zu bytes)", b64_cap);
        return false;
    }

    size_t b64_len = 0;
    int rc = mbedtls_base64_encode(b64, b64_cap, &b64_len,
                                   s_snap_jpeg, (size_t)s_snap_jpeg_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "base64 failed: %d", rc);
        free(b64);
        return false;
    }

    ESP_LOGI(TAG, "Base64: %zu bytes", b64_len);
    *b64_out     = b64;
    *b64_len_out = b64_len;
    return true;
}
