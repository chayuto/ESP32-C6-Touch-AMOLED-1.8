#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JINGLE_HATCH = 0,
    JINGLE_EAT,
    JINGLE_HAPPY,
    JINGLE_SAD,
    JINGLE_POOP,
    JINGLE_SICK,
    JINGLE_DEATH,
    JINGLE_LEVELUP,
    JINGLE_DISCIPLINE,
    JINGLE_COUNT,
} jingle_t;

void audio_jingles_play(jingle_t j);

#ifdef __cplusplus
}
#endif
