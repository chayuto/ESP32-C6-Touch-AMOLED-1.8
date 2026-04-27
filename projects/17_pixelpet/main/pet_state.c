#include "pet_state.h"
#include <string.h>

static const char *STAGE_NAMES[STAGE_COUNT] = {
    "EGG", "BABY", "CHILD", "TEEN", "ADULT", "SENIOR", "RIP",
};

void pet_state_init_new(pet_state_t *p)
{
    memset(p, 0, sizeof(*p));
    p->stage      = STAGE_EGG;
    p->hunger     = 100;
    p->happy      = 100;
    p->energy     = 100;
    p->clean      = 100;
    p->disc       = 50;
    p->health     = 100;
    p->species_id = 0;        /* SPECIES_BLOB_PINK */
    p->name[0]    = '\0';     /* unset until intro completes */
    p->intro_done = false;
}

const char *pet_stage_name(pet_stage_t s)
{
    if (s >= STAGE_COUNT) return "?";
    return STAGE_NAMES[s];
}
