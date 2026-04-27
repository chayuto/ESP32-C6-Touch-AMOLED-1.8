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
#define SAVE_VERSION   3
#define DEBOUNCE_US    (5LL * 1000 * 1000)

typedef struct {
    uint8_t      version;
    pet_state_t  state;
} __attribute__((packed)) save_blob_t;

/* v1 layout — same fields as pet_state_t but without species_id at the
 * end. Used only for migration on load. Order/types must match the
 * historical struct exactly. */
typedef struct {
    int64_t  hatched_unix;
    int64_t  last_update_unix;
    pet_stage_t stage;
    pet_adult_form_t adult_form;
    uint8_t  hunger, happy, energy, clean, disc, health;
    uint32_t care_score;
    uint8_t  poop_count;
    bool     is_sleeping;
} pet_state_v1_t;

typedef struct {
    uint8_t        version;
    pet_state_v1_t state;
} __attribute__((packed)) save_blob_v1_t;

/* v2 layout — pet_state_t before name[8] + intro_done were appended. */
typedef struct {
    int64_t  hatched_unix;
    int64_t  last_update_unix;
    pet_stage_t stage;
    pet_adult_form_t adult_form;
    uint8_t  hunger, happy, energy, clean, disc, health;
    uint32_t care_score;
    uint8_t  poop_count;
    bool     is_sleeping;
    uint8_t  species_id;
} pet_state_v2_t;

typedef struct {
    uint8_t        version;
    pet_state_v2_t state;
} __attribute__((packed)) save_blob_v2_t;

static int64_t s_dirty_since_us = 0;
static int64_t s_last_commit_us = 0;

esp_err_t pet_save_load(pet_state_t *p)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    /* Read into a buffer big enough for either schema. */
    uint8_t buf[sizeof(save_blob_t) + 16];
    size_t sz = sizeof(buf);
    err = nvs_get_blob(h, NVS_KEY_BLOB, buf, &sz);
    nvs_close(h);
    if (err != ESP_OK) return err;
    if (sz < 1) return ESP_ERR_INVALID_SIZE;

    uint8_t version = buf[0];

    if (version == SAVE_VERSION && sz == sizeof(save_blob_t)) {
        memcpy(p, &buf[1], sizeof(pet_state_t));
    } else if (version == 2 && sz == sizeof(save_blob_v2_t)) {
        const pet_state_v2_t *v2 = (const pet_state_v2_t *)&buf[1];
        memset(p, 0, sizeof(*p));
        p->hatched_unix     = v2->hatched_unix;
        p->last_update_unix = v2->last_update_unix;
        p->stage            = v2->stage;
        p->adult_form       = v2->adult_form;
        p->hunger           = v2->hunger;
        p->happy            = v2->happy;
        p->energy           = v2->energy;
        p->clean            = v2->clean;
        p->disc             = v2->disc;
        p->health           = v2->health;
        p->care_score       = v2->care_score;
        p->poop_count       = v2->poop_count;
        p->is_sleeping      = v2->is_sleeping;
        p->species_id       = v2->species_id;
        p->name[0]          = '\0';
        p->intro_done       = true;     /* existing pet → skip onboarding */
        ESP_LOGI(TAG, "migrated v2 → v3 save (intro_done=1, name unset)");
        pet_save_commit(p);
    } else if (version == 1 && sz == sizeof(save_blob_v1_t)) {
        const pet_state_v1_t *v1 = (const pet_state_v1_t *)&buf[1];
        memset(p, 0, sizeof(*p));
        p->hatched_unix     = v1->hatched_unix;
        p->last_update_unix = v1->last_update_unix;
        p->stage            = v1->stage;
        p->adult_form       = v1->adult_form;
        p->hunger           = v1->hunger;
        p->happy            = v1->happy;
        p->energy           = v1->energy;
        p->clean            = v1->clean;
        p->disc             = v1->disc;
        p->health           = v1->health;
        p->care_score       = v1->care_score;
        p->poop_count       = v1->poop_count;
        p->is_sleeping      = v1->is_sleeping;
        p->species_id       = 0;
        p->name[0]          = '\0';
        p->intro_done       = true;
        ESP_LOGI(TAG, "migrated v1 → v3 save (species_id=0, intro_done=1)");
        pet_save_commit(p);
    } else {
        ESP_LOGW(TAG, "save schema mismatch (sz=%u v=%u) — discarding",
                 (unsigned)sz, version);
        return ESP_ERR_INVALID_VERSION;
    }

    ESP_LOGI(TAG, "loaded pet: species=%d stage=%d hunger=%d happy=%d energy=%d age=%llds",
             p->species_id, (int)p->stage, p->hunger, p->happy, p->energy,
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
