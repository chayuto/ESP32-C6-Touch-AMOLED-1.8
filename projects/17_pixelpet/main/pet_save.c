/*
 * pet_save.c — NVS-backed pet save with debounced commits.
 *
 * The whole pet_state_t struct is written as one blob (preceded by a u8
 * schema version) so adding fields later just bumps the version and runs a
 * migration at load time. Commit is debounced 5s to coalesce rapid touch
 * events and reduce flash wear.
 */

#include "pet_save.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "pet_save";

#define NVS_NAMESPACE  "pixelpet"
#define NVS_KEY_BLOB   "pet"
#define SAVE_VERSION   1
#define DEBOUNCE_US    (5LL * 1000 * 1000)

typedef struct {
    uint8_t      version;
    pet_state_t  state;
} __attribute__((packed)) save_blob_t;

static int64_t s_dirty_since_us = 0;
static int64_t s_last_commit_us = 0;

esp_err_t pet_save_load(pet_state_t *p)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    save_blob_t blob;
    size_t sz = sizeof(blob);
    err = nvs_get_blob(h, NVS_KEY_BLOB, &blob, &sz);
    nvs_close(h);
    if (err != ESP_OK) return err;

    if (sz != sizeof(blob) || blob.version != SAVE_VERSION) {
        ESP_LOGW(TAG, "save schema mismatch (sz=%u v=%u) — discarding",
                 (unsigned)sz, blob.version);
        return ESP_ERR_INVALID_VERSION;
    }

    *p = blob.state;
    ESP_LOGI(TAG, "loaded pet: stage=%d hunger=%d happy=%d energy=%d age=%llds",
             (int)p->stage, p->hunger, p->happy, p->energy,
             (long long)((p->last_update_unix - p->hatched_unix)));
    return ESP_OK;
}

esp_err_t pet_save_commit(const pet_state_t *p)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    save_blob_t blob = {
        .version = SAVE_VERSION,
        .state   = *p,
    };
    err = nvs_set_blob(h, NVS_KEY_BLOB, &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        s_last_commit_us = esp_timer_get_time();
        s_dirty_since_us = 0;
        ESP_LOGD(TAG, "committed");
    } else {
        ESP_LOGW(TAG, "commit failed: %s", esp_err_to_name(err));
    }
    return err;
}

void pet_save_request(void)
{
    if (s_dirty_since_us == 0) s_dirty_since_us = esp_timer_get_time();
}

void pet_save_pump(const pet_state_t *p)
{
    if (s_dirty_since_us == 0) return;
    int64_t now = esp_timer_get_time();
    if (now - s_dirty_since_us < DEBOUNCE_US) return;
    pet_save_commit(p);
}

esp_err_t pet_save_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, NVS_KEY_BLOB);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
