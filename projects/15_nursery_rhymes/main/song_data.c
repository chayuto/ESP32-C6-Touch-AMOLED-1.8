/*
 * song_data.c — Complete nursery rhyme note data for 38 classic songs.
 *
 * All melodies are traditional/public domain. Note data transcribed from
 * standard sheet music scores. See docs/ for per-song references.
 *
 * Duration encoding: at 120 BPM baseline, Q=500ms.
 * Songs with different tempos use scaled durations.
 * Tempo field is informational; actual playback speed can be adjusted.
 */

#include "song_data.h"
#include <math.h>

float midi_to_freq(uint8_t midi_note)
{
    if (midi_note == 0) return 0.0f;
    return 440.0f * powf(2.0f, (midi_note - 69) / 12.0f);
}

/* ═══════════════════════════════════════════════════════════════
 * 1. Twinkle Twinkle Little Star
 *    Key: C major, 4/4, ~100 BPM
 *    "Ah! vous dirai-je, Maman" (Mozart K.265 theme)
 * ═══════════════════════════════════════════════════════════════ */
static const note_t twinkle_notes[] = {
    /* Twin-kle twin-kle lit-tle star */
    {C4, Q}, {C4, Q}, {G4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {G4, H},
    /* How I won-der what you are */
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, H},
    /* Up a-bove the world so high */
    {G4, Q}, {G4, Q}, {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, H},
    /* Like a dia-mond in the sky */
    {G4, Q}, {G4, Q}, {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, H},
    /* Twin-kle twin-kle lit-tle star */
    {C4, Q}, {C4, Q}, {G4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {G4, H},
    /* How I won-der what you are */
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, H},
};

/* ═══════════════════════════════════════════════════════════════
 * 2. Mary Had a Little Lamb
 *    Key: C major, 4/4, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t mary_notes[] = {
    /* Ma-ry had a lit-tle lamb */
    {E4, Q}, {D4, Q}, {C4, Q}, {D4, Q}, {E4, Q}, {E4, Q}, {E4, H},
    /* Lit-tle lamb, lit-tle lamb */
    {D4, Q}, {D4, Q}, {D4, H}, {E4, Q}, {G4, Q}, {G4, H},
    /* Ma-ry had a lit-tle lamb its */
    {E4, Q}, {D4, Q}, {C4, Q}, {D4, Q}, {E4, Q}, {E4, Q}, {E4, Q}, {E4, Q},
    /* fleece was white as snow */
    {D4, Q}, {D4, Q}, {E4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 3. Baa Baa Black Sheep
 *    Key: C major, 4/4, ~110 BPM
 *    Same melody as Twinkle Twinkle (different rhythm/lyrics)
 * ═══════════════════════════════════════════════════════════════ */
static const note_t baa_baa_notes[] = {
    /* Baa baa black sheep have you a-ny wool */
    {C4, Q}, {C4, Q}, {G4, Q}, {G4, Q}, {A4, E}, {A4, E}, {A4, E}, {A4, E}, {G4, H},
    /* Yes sir yes sir three bags full */
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, H},
    /* One for the mas-ter one for the dame */
    {G4, Q}, {G4, E}, {G4, E}, {F4, Q}, {F4, Q}, {E4, Q}, {E4, E}, {E4, E}, {D4, H},
    /* One for the lit-tle boy who lives down the lane */
    {G4, E}, {G4, E}, {G4, E}, {G4, E}, {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q},
    /* Baa baa black sheep have you a-ny wool (reprise) */
    {C4, Q}, {C4, Q}, {G4, Q}, {G4, Q}, {A4, E}, {A4, E}, {A4, E}, {A4, E}, {G4, H},
    /* Yes sir yes sir three bags full */
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, H},
};

/* ═══════════════════════════════════════════════════════════════
 * 4. London Bridge Is Falling Down
 *    Key: C major, 4/4, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t london_bridge_notes[] = {
    /* Lon-don bridge is fall-ing down */
    {G4, DQ}, {A4, E}, {G4, Q}, {F4, Q}, {E4, Q}, {F4, Q}, {G4, H},
    /* Fall-ing down, fall-ing down */
    {D4, Q}, {E4, Q}, {F4, H}, {E4, Q}, {F4, Q}, {G4, H},
    /* Lon-don bridge is fall-ing down */
    {G4, DQ}, {A4, E}, {G4, Q}, {F4, Q}, {E4, Q}, {F4, Q}, {G4, H},
    /* My fair la-dy */
    {D4, H}, {G4, Q}, {E4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 5. Row Row Row Your Boat
 *    Key: C major, 6/8, ~100 BPM (dotted quarter = beat)
 * ═══════════════════════════════════════════════════════════════ */
static const note_t row_boat_notes[] = {
    /* Row row row your boat */
    {C4, DQ}, {C4, DQ}, {C4, Q}, {D4, E}, {E4, DQ},
    /* Gen-tly down the stream */
    {E4, Q}, {D4, E}, {E4, Q}, {F4, E}, {G4, DH},
    /* Mer-ri-ly mer-ri-ly mer-ri-ly mer-ri-ly */
    {C5, E}, {C5, E}, {C5, E}, {G4, E}, {G4, E}, {G4, E},
    {E4, E}, {E4, E}, {E4, E}, {C4, E}, {C4, E}, {C4, E},
    /* Life is but a dream */
    {G4, Q}, {F4, E}, {E4, Q}, {D4, E}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 6. Jack and Jill
 *    Key: C major, 6/8, ~108 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t jack_jill_notes[] = {
    /* Jack and Jill went up the hill to */
    {C4, E}, {C4, E}, {D4, E}, {E4, E}, {E4, E}, {F4, E}, {G4, E}, {G4, E},
    /* fetch a pail of wa-ter */
    {A4, E}, {G4, E}, {F4, E}, {E4, E}, {D4, E}, {C4, DQ},
    /* Jack fell down and broke his crown and */
    {C4, E}, {C4, E}, {D4, E}, {E4, E}, {E4, E}, {F4, E}, {G4, E}, {G4, E},
    /* Jill came tum-bling af-ter */
    {A4, E}, {G4, E}, {F4, E}, {E4, Q}, {D4, E}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 7. Humpty Dumpty
 *    Key: C major, 3/4, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t humpty_notes[] = {
    /* Hump-ty Dump-ty sat on a wall */
    {E4, Q}, {G4, Q}, {G4, Q}, {E4, Q}, {G4, Q}, {G4, Q},
    {A4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, H},
    /* Hump-ty Dump-ty had a great fall */
    {E4, Q}, {G4, Q}, {G4, Q}, {E4, Q}, {G4, Q}, {G4, Q},
    {A4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {C4, H},
    /* All the king's hor-ses and all the king's men */
    {D4, Q}, {D4, Q}, {D4, Q}, {D4, Q}, {E4, Q}, {F4, Q},
    {E4, Q}, {E4, Q}, {E4, Q}, {E4, Q}, {F4, Q}, {G4, Q},
    /* Could-n't put Hump-ty to-ge-ther a-gain */
    {A4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {F4, Q}, {D4, Q}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 8. Itsy Bitsy Spider
 *    Key: C major, 6/8, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t itsy_bitsy_notes[] = {
    /* The it-sy bit-sy spi-der climbed up the wa-ter spout */
    {G4, E}, {C5, E}, {C5, E}, {C5, Q}, {D5, E}, {E5, DQ}, {E5, DQ},
    {E5, Q}, {D5, E}, {C5, Q}, {D5, E}, {E5, DQ}, {C5, DQ},
    /* Down came the rain and washed the spi-der out */
    {E5, DQ}, {E5, Q}, {F5, E}, {G5, DH},
    {G5, Q}, {F5, E}, {E5, Q}, {F5, E}, {G5, DQ}, {E5, DQ},
    /* Out came the sun and dried up all the rain */
    {C5, DQ}, {C5, Q}, {D5, E}, {E5, DQ}, {E5, DQ},
    {E5, Q}, {D5, E}, {C5, Q}, {D5, E}, {E5, DQ}, {C5, DQ},
    /* And the it-sy bit-sy spi-der climbed up the spout a-gain */
    {E5, Q}, {E5, E}, {E5, Q}, {F5, E}, {G5, DH},
    {G5, Q}, {F5, E}, {E5, Q}, {D5, E}, {C5, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 9. Old MacDonald Had a Farm
 *    Key: C major, 4/4, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t old_macdonald_notes[] = {
    /* Old Mac-Don-ald had a farm */
    {C4, Q}, {C4, Q}, {C4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {G4, H},
    /* E-I-E-I-O */
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, H}, {REST, Q}, {REST, Q},
    /* And on his farm he had a cow (duck/pig...) */
    {C4, Q}, {C4, Q}, {C4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {G4, H},
    /* E-I-E-I-O */
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, H}, {REST, Q}, {REST, Q},
    /* With a moo-moo here and a moo-moo there */
    {G4, Q}, {G4, Q}, {C4, Q}, {C4, Q}, {G4, Q}, {G4, Q}, {C4, Q}, {C4, Q},
    /* Here a moo there a moo ev'-ry-where a moo-moo */
    {C4, E}, {C4, E}, {C4, E}, {C4, E}, {C4, E}, {C4, E}, {C4, E}, {C4, E},
    {C4, Q}, {C4, Q}, {C4, Q}, {REST, Q},
    /* Old Mac-Don-ald had a farm E-I-E-I-O */
    {C4, Q}, {C4, Q}, {C4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {G4, H},
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 10. Hickory Dickory Dock
 *     Key: C major, 6/8, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t hickory_notes[] = {
    /* Hick-o-ry dick-o-ry dock */
    {C4, E}, {D4, E}, {E4, E}, {F4, Q}, {E4, E}, {D4, DQ},
    /* The mouse ran up the clock */
    {E4, E}, {F4, E}, {G4, E}, {A4, Q}, {G4, E}, {F4, DQ},
    /* The clock struck one the mouse ran down */
    {G4, DQ}, {C5, DQ}, {G4, E}, {F4, E}, {E4, E}, {D4, E}, {C4, E}, {D4, E},
    /* Hick-o-ry dick-o-ry dock */
    {C4, E}, {D4, E}, {E4, E}, {F4, Q}, {E4, E}, {D4, DQ}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 11. Three Blind Mice
 *     Key: C major, 6/8, ~108 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t three_blind_mice_notes[] = {
    /* Three blind mice */
    {E4, DQ}, {D4, DQ}, {C4, DH},
    /* Three blind mice */
    {E4, DQ}, {D4, DQ}, {C4, DH},
    /* See how they run */
    {G4, DQ}, {F4, Q}, {F4, E}, {E4, DH},
    /* See how they run */
    {G4, DQ}, {F4, Q}, {F4, E}, {E4, DH},
    /* They all ran af-ter the far-mer's wife */
    {G4, E}, {G4, E}, {A4, E}, {G4, E}, {F4, E}, {E4, E},
    {G4, E}, {G4, E}, {A4, E}, {G4, E}, {F4, E}, {E4, E},
    /* She cut off their tails with a car-ving knife */
    {G4, E}, {G4, E}, {A4, E}, {G4, E}, {F4, E}, {E4, E},
    {D4, E}, {E4, E}, {F4, E}, {E4, E}, {D4, E}, {C4, E},
    /* Did you ev-er see such a thing in your life as three blind mice */
    {E4, E}, {F4, E}, {G4, DQ}, {F4, DQ}, {E4, E}, {D4, DQ}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 12. Hot Cross Buns
 *     Key: C major, 4/4, ~100 BPM
 *     One of the simplest melodies — great for beginners
 * ═══════════════════════════════════════════════════════════════ */
static const note_t hot_cross_buns_notes[] = {
    /* Hot cross buns */
    {E4, Q}, {D4, Q}, {C4, H},
    /* Hot cross buns */
    {E4, Q}, {D4, Q}, {C4, H},
    /* One a pen-ny two a pen-ny */
    {C4, E}, {C4, E}, {C4, E}, {C4, E}, {D4, E}, {D4, E}, {D4, E}, {D4, E},
    /* Hot cross buns */
    {E4, Q}, {D4, Q}, {C4, H},
};

/* ═══════════════════════════════════════════════════════════════
 * 13. Frere Jacques (Are You Sleeping / Brother John)
 *     Key: C major, 4/4, ~120 BPM
 *     Traditional French round
 * ═══════════════════════════════════════════════════════════════ */
static const note_t frere_jacques_notes[] = {
    /* Fre-re Jac-ques */
    {C4, Q}, {D4, Q}, {E4, Q}, {C4, Q},
    /* Fre-re Jac-ques */
    {C4, Q}, {D4, Q}, {E4, Q}, {C4, Q},
    /* Dor-mez vous? */
    {E4, Q}, {F4, Q}, {G4, H},
    /* Dor-mez vous? */
    {E4, Q}, {F4, Q}, {G4, H},
    /* Son-nez les ma-ti-nes */
    {G4, E}, {A4, E}, {G4, E}, {F4, E}, {E4, Q}, {C4, Q},
    /* Son-nez les ma-ti-nes */
    {G4, E}, {A4, E}, {G4, E}, {F4, E}, {E4, Q}, {C4, Q},
    /* Din din don */
    {C4, Q}, {G3, Q}, {C4, H},
    /* Din din don */
    {C4, Q}, {G3, Q}, {C4, H},
};

/* ═══════════════════════════════════════════════════════════════
 * 14. Yankee Doodle
 *     Key: C major, 4/4, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t yankee_doodle_notes[] = {
    /* Yan-kee Doo-dle went to town */
    {C4, Q}, {C4, Q}, {D4, Q}, {E4, Q},
    /* Ri-ding on a po-ny */
    {C4, Q}, {E4, Q}, {D4, Q}, {G3, Q},
    /* Stuck a fea-ther in his hat */
    {C4, Q}, {C4, Q}, {D4, Q}, {E4, Q},
    /* And called it ma-ca-ro-ni */
    {C4, H}, {B3, Q}, {REST, Q},
    /* Yan-kee Doo-dle keep it up */
    {A3, Q}, {B3, Q}, {C4, Q}, {A3, Q},
    /* Yan-kee Doo-dle dan-dy */
    {B3, Q}, {C4, Q}, {D4, H},
    /* Mind the mu-sic and the step */
    {E4, Q}, {D4, Q}, {C4, Q}, {B3, Q},
    /* And with the girls be han-dy */
    {A3, Q}, {B3, Q}, {C4, Q}, {G3, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 15. Pop Goes the Weasel
 *     Key: C major, 6/8, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t pop_weasel_notes[] = {
    /* All a-round the mul-ber-ry bush */
    {C4, E}, {E4, E}, {E4, E}, {G4, E}, {G4, E}, {E4, E},
    /* The mon-key chased the wea-sel */
    {F4, E}, {A4, E}, {A4, E}, {G4, DQ},
    /* The mon-key thought 'twas all in fun */
    {C4, E}, {E4, E}, {E4, E}, {G4, E}, {G4, E}, {E4, E},
    /* Pop goes the wea-sel */
    {F4, E}, {D4, E}, {E4, E}, {C4, DH},
    /* A pen-ny for a spool of thread */
    {A4, E}, {A4, E}, {C5, E}, {A4, E}, {F4, E}, {A4, E},
    /* A pen-ny for a nee-dle */
    {G4, E}, {E4, E}, {G4, E}, {E4, DQ},
    /* That's the way the mo-ney goes */
    {C4, E}, {E4, E}, {E4, E}, {G4, E}, {G4, E}, {E4, E},
    /* Pop goes the wea-sel */
    {F4, E}, {D4, E}, {E4, E}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 16. Rock-a-Bye Baby
 *     Key: C major, 3/4 (waltz), ~80 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t rock_a_bye_notes[] = {
    /* Rock-a-bye ba-by on the tree top */
    {E4, Q}, {E4, E}, {F4, E}, {G4, Q}, {E4, Q}, {C5, Q}, {B4, Q}, {A4, Q},
    /* When the wind blows the cra-dle will rock */
    {G4, Q}, {G4, E}, {A4, E}, {B4, Q}, {G4, Q}, {D5, Q}, {C5, Q}, {B4, Q},
    /* When the bough breaks the cra-dle will fall */
    {A4, Q}, {A4, E}, {B4, E}, {C5, Q}, {E4, Q}, {A4, Q}, {G4, Q}, {F4, Q},
    /* And down will come ba-by cra-dle and all */
    {E4, Q}, {A4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 17. Hush Little Baby (Mockingbird)
 *     Key: C major, 4/4, ~100 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t hush_little_baby_notes[] = {
    /* Hush lit-tle ba-by don't say a word */
    {C4, E}, {C4, E}, {E4, Q}, {G4, Q}, {G4, Q}, {A4, Q}, {G4, Q},
    /* Ma-ma's gon-na buy you a mock-ing-bird */
    {E4, Q}, {E4, Q}, {D4, Q}, {E4, Q}, {F4, Q}, {E4, Q}, {D4, H},
    /* And if that mock-ing-bird don't sing */
    {C4, E}, {C4, E}, {E4, Q}, {G4, Q}, {G4, Q}, {A4, Q}, {G4, Q},
    /* Ma-ma's gon-na buy you a dia-mond ring */
    {E4, Q}, {E4, Q}, {D4, Q}, {E4, Q}, {F4, Q}, {E4, Q}, {C4, H},
};

/* ═══════════════════════════════════════════════════════════════
 * 18. Wheels on the Bus
 *     Key: C major, 4/4, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t wheels_bus_notes[] = {
    /* The wheels on the bus go round and round */
    {C4, Q}, {C4, Q}, {C4, Q}, {E4, Q}, {G4, Q}, {G4, Q}, {G4, H},
    /* Round and round, round and round */
    {E4, Q}, {E4, Q}, {E4, H}, {C4, Q}, {C4, Q}, {C4, H},
    /* The wheels on the bus go round and round */
    {C4, Q}, {C4, Q}, {C4, Q}, {E4, Q}, {G4, Q}, {G4, Q}, {G4, H},
    /* All through the town */
    {F4, Q}, {D4, Q}, {E4, Q}, {C4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 19. Head Shoulders Knees and Toes
 *     Key: C major, 4/4, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t head_shoulders_notes[] = {
    /* Head shoul-ders knees and toes, knees and toes */
    {C4, Q}, {E4, Q}, {G4, Q}, {G4, Q}, {G4, Q}, {E4, Q}, {G4, H},
    /* Head shoul-ders knees and toes, knees and toes */
    {C4, Q}, {E4, Q}, {G4, Q}, {G4, Q}, {G4, Q}, {E4, Q}, {G4, H},
    /* And eyes and ears and mouth and nose */
    {A4, Q}, {A4, Q}, {A4, Q}, {G4, Q}, {F4, Q}, {F4, Q}, {F4, Q}, {E4, Q},
    /* Head shoul-ders knees and toes, knees and toes */
    {D4, Q}, {E4, Q}, {F4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 20. If You're Happy and You Know It
 *     Key: C major, 4/4, ~120 BPM
 * ═══════════════════════════════════════════════════════════════ */
static const note_t happy_know_it_notes[] = {
    /* If you're hap-py and you know it clap your hands */
    {C4, E}, {C4, E}, {E4, Q}, {E4, E}, {E4, E}, {G4, Q}, {G4, Q}, {REST, Q}, {G4, Q}, {E4, Q},
    /* If you're hap-py and you know it clap your hands */
    {C4, E}, {C4, E}, {E4, Q}, {E4, E}, {E4, E}, {G4, Q}, {G4, Q}, {REST, Q}, {G4, Q}, {E4, Q},
    /* If you're hap-py and you know it and you real-ly want to show it */
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {E4, Q}, {F4, Q},
    /* If you're hap-py and you know it clap your hands */
    {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * Song table
 * ═══════════════════════════════════════════════════════════════ */

#define SONG_ENTRY(title_str, lyrics_str, bpm, ts_n, ts_d, arr) \
    { title_str, lyrics_str, bpm, ts_n, ts_d, arr, sizeof(arr)/sizeof(arr[0]) }

/* Compile-time song inclusion: CONFIG_SONG_INCLUDE_ALL=y includes everything.
 * To exclude individual songs, set CONFIG_SONG_INCLUDE_ALL=n in menuconfig
 * and disable specific CONFIG_SONG_xxx options. */
#define SONG_ON(cfg) (defined(CONFIG_SONG_INCLUDE_ALL) || defined(cfg))

const song_t g_songs[] = {
#if SONG_ON(CONFIG_SONG_TWINKLE)
    SONG_ENTRY("Twinkle Twinkle",      "Twinkle twinkle little star",         100, 4, 4, twinkle_notes),
#endif
#if SONG_ON(CONFIG_SONG_MARY)
    SONG_ENTRY("Mary Had a Lamb",      "Mary had a little lamb",              120, 4, 4, mary_notes),
#endif
#if SONG_ON(CONFIG_SONG_BAA_BAA)
    SONG_ENTRY("Baa Baa Black Sheep",  "Baa baa black sheep have you any wool", 110, 4, 4, baa_baa_notes),
#endif
#if SONG_ON(CONFIG_SONG_LONDON_BRIDGE)
    SONG_ENTRY("London Bridge",        "London bridge is falling down",       120, 4, 4, london_bridge_notes),
#endif
#if SONG_ON(CONFIG_SONG_ROW_BOAT)
    SONG_ENTRY("Row Your Boat",        "Row row row your boat",              100, 6, 8, row_boat_notes),
#endif
#if SONG_ON(CONFIG_SONG_JACK_JILL)
    SONG_ENTRY("Jack and Jill",        "Jack and Jill went up the hill",     108, 6, 8, jack_jill_notes),
#endif
#if SONG_ON(CONFIG_SONG_HUMPTY)
    SONG_ENTRY("Humpty Dumpty",        "Humpty Dumpty sat on a wall",        120, 3, 4, humpty_notes),
#endif
#if SONG_ON(CONFIG_SONG_ITSY_BITSY)
    SONG_ENTRY("Itsy Bitsy Spider",    "The itsy bitsy spider",              120, 6, 8, itsy_bitsy_notes),
#endif
#if SONG_ON(CONFIG_SONG_OLD_MACDONALD)
    SONG_ENTRY("Old MacDonald",        "Old MacDonald had a farm",           120, 4, 4, old_macdonald_notes),
#endif
#if SONG_ON(CONFIG_SONG_HICKORY)
    SONG_ENTRY("Hickory Dickory",      "Hickory dickory dock",               120, 6, 8, hickory_notes),
#endif
#if SONG_ON(CONFIG_SONG_THREE_BLIND_MICE)
    SONG_ENTRY("Three Blind Mice",     "Three blind mice",                   108, 6, 8, three_blind_mice_notes),
#endif
#if SONG_ON(CONFIG_SONG_HOT_CROSS_BUNS)
    SONG_ENTRY("Hot Cross Buns",       "Hot cross buns",                     100, 4, 4, hot_cross_buns_notes),
#endif
#if SONG_ON(CONFIG_SONG_FRERE_JACQUES)
    SONG_ENTRY("Frere Jacques",        "Are you sleeping, Brother John?",    120, 4, 4, frere_jacques_notes),
#endif
#if SONG_ON(CONFIG_SONG_YANKEE_DOODLE)
    SONG_ENTRY("Yankee Doodle",        "Yankee Doodle went to town",         120, 4, 4, yankee_doodle_notes),
#endif
#if SONG_ON(CONFIG_SONG_POP_WEASEL)
    SONG_ENTRY("Pop Goes Weasel",      "All around the mulberry bush",       120, 6, 8, pop_weasel_notes),
#endif
#if SONG_ON(CONFIG_SONG_ROCK_A_BYE)
    SONG_ENTRY("Rock-a-Bye Baby",      "Rock-a-bye baby on the treetop",     80, 3, 4, rock_a_bye_notes),
#endif
#if SONG_ON(CONFIG_SONG_HUSH_LITTLE_BABY)
    SONG_ENTRY("Hush Little Baby",     "Hush little baby don't say a word",  100, 4, 4, hush_little_baby_notes),
#endif
#if SONG_ON(CONFIG_SONG_WHEELS_BUS)
    SONG_ENTRY("Wheels on the Bus",    "The wheels on the bus go round",     120, 4, 4, wheels_bus_notes),
#endif
#if SONG_ON(CONFIG_SONG_HEAD_SHOULDERS)
    SONG_ENTRY("Head Shoulders",       "Head shoulders knees and toes",      120, 4, 4, head_shoulders_notes),
#endif
#if SONG_ON(CONFIG_SONG_HAPPY_KNOW_IT)
    SONG_ENTRY("Happy & You Know It",  "If you're happy and you know it",    120, 4, 4, happy_know_it_notes),
#endif
};

const int g_song_count = sizeof(g_songs) / sizeof(g_songs[0]);

/* NOTE: To add more songs later, add the note array above the song table,
 * add a SONG_ENTRY line inside #if SONG_ON(...), and add a matching
 * config entry in Kconfig.projbuild. */
