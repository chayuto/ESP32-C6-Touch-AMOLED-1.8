/*
 * asset_loader.c — mmap the assets partition, parse the bundle TOC, and
 * hand out per-asset frame arrays suitable for `lv_animimg_set_src`.
 *
 * Bundle layout written by tools/sprite_factory/build_assets.py:
 *
 *   header (64 bytes)
 *   TOC   (64 bytes per entry, toc_count entries)
 *   data pool — for each sprite, frame_count blocks of:
 *       [ 64-byte palette || (W*H/2) bytes I4 packed pixels ]
 *
 * Per-frame palette duplication keeps each frame self-contained so LVGL
 * 8's I4 decoder can read it directly from flash via lv_img_dsc_t.data,
 * with no runtime copy.
 */

#include "asset_loader.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_check.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "asset_loader";

#define ASSET_MAGIC      0x50585041  /* 'PXPA' */
#define ASSET_VERSION    1
#define HEADER_SIZE      64
#define TOC_ENTRY_SIZE   64
#define PALETTE_SIZE     64

#define FORMAT_I4        0
#define FORMAT_RGB565A8  1

/* ── On-disk structs (matches build_assets.py exactly). All little-endian. ── */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t toc_offset;
    uint32_t toc_count;
    uint32_t data_pool_offset;
    uint32_t data_pool_size;
    uint32_t total_size;
    uint32_t reserved;
    uint32_t crc32;
    uint8_t  pad[24];
} asset_header_t;

typedef struct __attribute__((packed)) {
    char     name[32];
    uint16_t format;
    uint16_t flags;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint16_t palette_id;
    uint32_t data_offset;        /* bytes from bundle start */
    uint32_t data_size;
    uint32_t durations_offset;   /* 0 == none */
    uint32_t reserved;
} asset_toc_t;

/* ── Runtime per-asset state ────────────────────────────────────────────── */
typedef struct {
    const asset_toc_t *toc;
    asset_anim_t       anim;
    /* Backing storage for the lv_img_dsc_t array + the void* array we
     * give to lv_animimg_set_src. Allocated once at init. */
    lv_img_dsc_t      *dsc_arr;
    const void       **frames_arr;
} asset_runtime_t;

/* ── Module state ───────────────────────────────────────────────────────── */
static const uint8_t          *s_bundle;
static size_t                  s_bundle_size;
static esp_partition_mmap_handle_t s_mmap_handle;
static const asset_header_t   *s_hdr;
static const asset_toc_t      *s_toc;
static uint16_t                s_count;
static asset_runtime_t        *s_runtime;

/* ── lv_img_dsc_t / lv_img_header_t bit-field setup ─────────────────────── */
/*
 * LVGL 8.4 lv_img_header_t is a 4-byte bitfield:
 *   uint32_t cf:5; always_zero:3; reserved:2; w:11; h:11;
 * Building it via { .cf=..., .w=..., .h=... } designated init works
 * across compilers.
 */

static esp_err_t build_runtime_for(asset_runtime_t *rt)
{
    const asset_toc_t *t = rt->toc;
    uint8_t fc = t->frame_count;
    if (fc == 0) {
        ESP_LOGW(TAG, "asset '%s' has zero frames", t->name);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t per_frame_block = PALETTE_SIZE +
                             ((size_t)t->width * t->height + 1) / 2;
    if ((size_t)fc * per_frame_block != t->data_size) {
        ESP_LOGW(TAG, "asset '%s' size mismatch: expected %u, got %u",
                 t->name, (unsigned)((size_t)fc * per_frame_block),
                 (unsigned)t->data_size);
        return ESP_ERR_INVALID_SIZE;
    }

    rt->dsc_arr    = calloc(fc, sizeof(lv_img_dsc_t));
    rt->frames_arr = calloc(fc, sizeof(void *));
    if (!rt->dsc_arr || !rt->frames_arr) {
        free(rt->dsc_arr);
        free(rt->frames_arr);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < fc; i++) {
        const uint8_t *block = s_bundle + t->data_offset + i * per_frame_block;
        rt->dsc_arr[i].header.cf          = LV_IMG_CF_INDEXED_4BIT;
        rt->dsc_arr[i].header.always_zero = 0;
        rt->dsc_arr[i].header.w           = t->width;
        rt->dsc_arr[i].header.h           = t->height;
        rt->dsc_arr[i].data_size          = per_frame_block;
        rt->dsc_arr[i].data               = block;
        rt->frames_arr[i]                 = &rt->dsc_arr[i];
    }

    rt->anim.frame_count = fc;
    rt->anim.width       = t->width;
    rt->anim.height      = t->height;
    rt->anim.frames      = rt->frames_arr;

    if (t->durations_offset) {
        rt->anim.durations_ms = (const uint16_t *)(s_bundle + t->durations_offset);
        uint32_t sum = 0;
        for (int i = 0; i < fc; i++) sum += rt->anim.durations_ms[i];
        rt->anim.total_duration_ms = sum;
    } else {
        rt->anim.durations_ms      = NULL;
        rt->anim.total_duration_ms = fc * 100;  /* default 10 fps */
    }
    return ESP_OK;
}

esp_err_t asset_loader_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0xfa, "assets");
    if (!part) {
        ESP_LOGE(TAG, "'assets' partition not found");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "assets partition: addr=0x%08lx size=%lu",
             (unsigned long)part->address, (unsigned long)part->size);

    /* Read header first (the C6 flash MMU can't map the whole 12 MB
     * partition — it has a limited number of 64 KB mapping pages, and
     * mapping the entire region returns ESP_ERR_NOT_FOUND when we exceed
     * the budget). Mmap only the actual bundle bytes. */
    asset_header_t header_probe;
    esp_err_t err = esp_partition_read(part, 0, &header_probe,
                                       sizeof(header_probe));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "header read failed: %s", esp_err_to_name(err));
        return err;
    }
    if (header_probe.magic != ASSET_MAGIC) {
        ESP_LOGE(TAG, "bad magic 0x%08lx — partition not flashed yet?",
                 (unsigned long)header_probe.magic);
        return ESP_ERR_INVALID_VERSION;
    }
    if (header_probe.total_size == 0 ||
        header_probe.total_size > part->size) {
        ESP_LOGE(TAG, "header total_size %u out of range",
                 (unsigned)header_probe.total_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Round up to the next 64 KB page so mmap covers the whole bundle. */
    size_t mmap_size = (header_probe.total_size + 0xFFFF) & ~0xFFFFU;
    if (mmap_size > part->size) mmap_size = part->size;

    const void *base = NULL;
    err = esp_partition_mmap(part, 0, mmap_size,
                             ESP_PARTITION_MMAP_DATA, &base, &s_mmap_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mmap %u bytes failed: %s",
                 (unsigned)mmap_size, esp_err_to_name(err));
        return err;
    }

    s_bundle      = (const uint8_t *)base;
    s_bundle_size = mmap_size;
    s_hdr         = (const asset_header_t *)s_bundle;

    if (s_hdr->version != ASSET_VERSION) {
        ESP_LOGE(TAG, "bundle version %u, expected %u",
                 s_hdr->version, ASSET_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }

    s_toc   = (const asset_toc_t *)(s_bundle + s_hdr->toc_offset);
    s_count = s_hdr->toc_count;

    s_runtime = calloc(s_count, sizeof(asset_runtime_t));
    if (!s_runtime) return ESP_ERR_NO_MEM;

    int built = 0;
    for (uint16_t i = 0; i < s_count; i++) {
        s_runtime[i].toc = &s_toc[i];
        if (build_runtime_for(&s_runtime[i]) == ESP_OK) built++;
    }

    ESP_LOGI(TAG, "loaded %d/%u assets (%lu bytes mmap'd)",
             built, s_count, (unsigned long)s_hdr->total_size);
    return ESP_OK;
}

const asset_anim_t *asset_get(asset_id_t id)
{
    if (id >= s_count) return NULL;
    if (!s_runtime[id].dsc_arr) return NULL;
    return &s_runtime[id].anim;
}

const asset_anim_t *asset_get_by_name(const char *name)
{
    for (uint16_t i = 0; i < s_count; i++) {
        if (strncmp(s_toc[i].name, name, sizeof(s_toc[i].name)) == 0) {
            return asset_get(i);
        }
    }
    return NULL;
}

uint16_t asset_count(void) { return s_count; }
