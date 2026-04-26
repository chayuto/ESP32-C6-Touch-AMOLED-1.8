#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPECIES_BLOB_PINK = 0,
    SPECIES_BLOB_MINT,
    SPECIES_BLOB_YELLOW,
    SPECIES_BLOB_BLUE,
    SPECIES_BLOB_PURPLE,
    SPECIES_COUNT,
} species_id_t;

typedef struct {
    const char *id_str;          /* "blob_pink" */
    const char *display_name;    /* "Pinkie", "Mochi", etc. */
    const char *rig;             /* "blob" — used in asset lookups */
    const char *palette_tag;     /* "pink", "mint", ... — appended to anim names */
} species_def_t;

const species_def_t *species_get(species_id_t id);
species_id_t         species_default(void);

/* Compose an asset name like "bodies/blob_idle_pink" for a given species
 * + animation. Writes into out_buf (size out_sz) and returns it, or NULL
 * on overflow. */
const char *species_anim_asset_name(species_id_t id, const char *anim_name,
                                    char *out_buf, size_t out_sz);

#ifdef __cplusplus
}
#endif
