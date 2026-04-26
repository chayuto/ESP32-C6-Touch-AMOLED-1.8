#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t asset_id_t;

/** Animation descriptor: frame array + per-frame durations.
 *  `frames` points to an array of (lv_img_dsc_t *) suitable for
 *  `lv_animimg_set_src(obj, anim->frames, anim->frame_count)`. */
typedef struct {
    uint8_t          frame_count;
    uint16_t         width;
    uint16_t         height;
    const void     **frames;          /* lv_img_dsc_t * each */
    const uint16_t  *durations_ms;    /* per-frame, NULL = uniform */
    uint32_t         total_duration_ms;
} asset_anim_t;

/** Locate the assets partition, mmap it, validate the header, and build
 *  per-asset lv_img_dsc_t arrays. Subsequent asset_get() calls are O(1)
 *  index lookups. Must be called after amoled_init. */
esp_err_t asset_loader_init(void);

/** Look up an asset by compile-time ID (from generated asset_index.h). */
const asset_anim_t *asset_get(asset_id_t id);

/** Look up by name. O(N) — prefer the ID variant in hot paths. */
const asset_anim_t *asset_get_by_name(const char *name);

/** Total number of assets in the bundle. */
uint16_t asset_count(void);

#ifdef __cplusplus
}
#endif
