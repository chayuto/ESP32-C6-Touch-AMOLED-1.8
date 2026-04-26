#include "species.h"
#include <stdio.h>

static const species_def_t SPECIES[] = {
    [SPECIES_BLOB_PINK]   = { "blob_pink",   "Pinkie",  "blob", "pink"   },
    [SPECIES_BLOB_MINT]   = { "blob_mint",   "Mochi",   "blob", "mint"   },
    [SPECIES_BLOB_YELLOW] = { "blob_yellow", "Sunny",   "blob", "yellow" },
    [SPECIES_BLOB_BLUE]   = { "blob_blue",   "Blueb",   "blob", "blue"   },
    [SPECIES_BLOB_PURPLE] = { "blob_purple", "Vio",     "blob", "purple" },
};

const species_def_t *species_get(species_id_t id)
{
    if (id >= SPECIES_COUNT) return &SPECIES[SPECIES_BLOB_PINK];
    return &SPECIES[id];
}

species_id_t species_default(void) { return SPECIES_BLOB_PINK; }

const char *species_anim_asset_name(species_id_t id, const char *anim_name,
                                    char *out_buf, size_t out_sz)
{
    const species_def_t *s = species_get(id);
    int n = snprintf(out_buf, out_sz, "bodies/%s_%s_%s",
                     s->rig, anim_name, s->palette_tag);
    if (n < 0 || (size_t)n >= out_sz) return NULL;
    return out_buf;
}
