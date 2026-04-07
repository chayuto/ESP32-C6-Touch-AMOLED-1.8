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
 * 21. This Old Man (Knick-Knack Paddy Whack)
 *     Key: C major, 4/4, ~120 BPM
 *     Traditional English counting song
 *     Src: singing-bell.com, zebrakeys.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t this_old_man_notes[] = {
    /* This old man, he played one */
    {G4, Q}, {E4, Q}, {G4, Q}, {G4, Q},
    /* He played knick-knack on my thumb */
    {A4, Q}, {G4, Q}, {E4, Q}, {REST, Q},
    /* With a knick-knack pad-dy whack give the dog a bone */
    {C4, E}, {D4, E}, {E4, E}, {F4, E}, {E4, E}, {D4, E}, {C4, E}, {E4, E}, {D4, Q}, {G3, Q},
    /* This old man came roll-ing home */
    {C4, Q}, {D4, Q}, {E4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {C4, H},
};

/* ═══════════════════════════════════════════════════════════════
 * 22. The Farmer in the Dell
 *     Key: C major, 6/8, ~120 BPM
 *     Traditional singing game
 *     Src: singing-bell.com, musicnotes.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t farmer_dell_notes[] = {
    /* The far-mer in the dell */
    {G4, E}, {G4, DQ}, {G4, E}, {A4, E}, {B4, DQ},
    /* The far-mer in the dell */
    {B4, E}, {B4, DQ}, {B4, E}, {A4, E}, {B4, DQ},
    /* Hi-ho the der-ry-o */
    {C5, DQ}, {B4, E}, {A4, E}, {G4, E},
    /* The far-mer in the dell */
    {A4, DQ}, {G4, DQ}, {G4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 23. BINGO
 *     Key: G major (transposed to C), 4/4, ~120 BPM
 *     Traditional spelling song
 *     Src: bethsnotesplus.com, zebrakeys.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t bingo_notes[] = {
    /* There was a far-mer had a dog and Bin-go was his name-o */
    {G4, Q}, {C5, Q}, {C5, Q}, {D5, Q}, {D5, Q}, {E5, Q}, {D5, Q}, {C5, Q},
    {B4, Q}, {A4, Q}, {B4, Q}, {C5, Q}, {A4, H}, {REST, H},
    /* B - I - N - G - O */
    {G4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {B4, Q}, {B4, Q}, {A4, H},
    /* B - I - N - G - O */
    {G4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {B4, Q}, {B4, Q}, {A4, H},
    /* And Bin-go was his name-o */
    {G4, Q}, {C5, Q}, {B4, Q}, {A4, Q}, {G4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 24. I'm a Little Teapot
 *     Key: C major, 4/4, ~110 BPM
 *     George Harold Sanders (1939), public domain
 *     Src: noobnotes.net (verified letter notes)
 * ═══════════════════════════════════════════════════════════════ */
static const note_t little_teapot_notes[] = {
    /* I'm a lit-tle tea-pot short and stout */
    {C4, Q}, {D4, Q}, {E4, Q}, {F4, Q}, {G4, Q}, {C5, H}, {A4, Q},
    {C5, Q}, {G4, H}, {REST, Q},
    /* Here is my han-dle here is my spout */
    {F4, Q}, {F4, Q}, {F4, Q}, {E4, Q}, {E4, H},
    {D4, Q}, {D4, Q}, {D4, Q}, {C4, Q},
    /* When I get all steamed up hear me shout */
    {C4, Q}, {D4, Q}, {E4, Q}, {F4, Q}, {G4, Q}, {C5, H}, {A4, Q},
    {C5, Q}, {G4, H}, {REST, Q},
    /* Tip me o-ver and pour me out */
    {C5, Q}, {A4, Q}, {G4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {C4, Q},
};

/* ═══════════════════════════════════════════════════════════════
 * 25. Alouette
 *     Key: C major, 4/4, ~120 BPM
 *     Traditional French-Canadian folk song
 *     Src: carvedculture.com PD list, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t alouette_notes[] = {
    /* A-lou-et-te, gen-tille a-lou-et-te */
    {G4, Q}, {E4, Q}, {F4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {A4, H},
    /* A-lou-et-te, je te plu-me-rai */
    {A4, Q}, {G4, Q}, {F4, Q}, {G4, Q}, {A4, Q}, {G4, Q}, {F4, H},
    /* Je te plu-me-rai la tete */
    {F4, E}, {F4, E}, {F4, E}, {F4, E}, {E4, Q}, {F4, Q},
    /* Je te plu-me-rai la tete */
    {F4, E}, {F4, E}, {F4, E}, {F4, E}, {E4, Q}, {F4, Q},
    /* Et la tete! A-lou-ette! Oh — */
    {G4, Q}, {E4, Q}, {G4, Q}, {E4, Q}, {F4, H},
    /* A-lou-et-te, gen-tille a-lou-et-te */
    {G4, Q}, {E4, Q}, {F4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {A4, H},
    /* A-lou-et-te, je te plu-me-rai */
    {A4, Q}, {G4, Q}, {F4, Q}, {G4, Q}, {A4, Q}, {G4, Q}, {F4, H},
};

/* ═══════════════════════════════════════════════════════════════
 * 26. Oh! Susanna
 *     Key: C major, 4/4, ~120 BPM
 *     Stephen Foster (1848), public domain
 *     Src: noobnotes.net (verified letter notes)
 * ═══════════════════════════════════════════════════════════════ */
static const note_t oh_susanna_notes[] = {
    /* Oh I come from Al-a-ba-ma with a ban-jo on my knee */
    {C4, E}, {D4, E}, {E4, Q}, {G4, Q}, {G4, E}, {A4, E}, {G4, Q}, {E4, Q},
    {C4, E}, {D4, E}, {E4, Q}, {E4, Q}, {D4, Q}, {C4, Q}, {D4, H},
    /* I'm goin' to Lou'-si-a-na my true love for to see */
    {C4, E}, {D4, E}, {E4, Q}, {G4, Q}, {G4, E}, {A4, E}, {G4, Q}, {E4, Q},
    {C4, E}, {D4, E}, {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, H},
    /* Oh! Su-san-na, oh don't you cry for me */
    {F4, Q}, {A4, Q}, {A4, Q}, {B4, Q},
    {C5, Q}, {A4, Q}, {G4, Q}, {E4, H},
    /* I come from Al-a-ba-ma with a ban-jo on my knee */
    {C4, E}, {D4, E}, {E4, Q}, {G4, Q}, {G4, E}, {A4, E}, {G4, Q}, {E4, Q},
    {C4, E}, {D4, E}, {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 27. She'll Be Coming Round the Mountain
 *     Key: C major, 4/4, ~120 BPM
 *     Traditional American folk song
 *     Src: noobnotes.net (verified letter notes)
 * ═══════════════════════════════════════════════════════════════ */
static const note_t coming_round_notes[] = {
    /* She'll be com-ing round the moun-tain when she comes */
    {G4, E}, {A4, E}, {C5, Q}, {C5, Q}, {C5, Q}, {C5, Q}, {A4, Q}, {G4, Q},
    {E4, Q}, {G4, Q}, {C5, W},
    /* She'll be com-ing round the moun-tain when she comes */
    {C5, E}, {D5, E}, {E5, Q}, {E5, Q}, {E5, Q}, {E5, Q}, {G5, Q}, {E5, Q},
    {D5, Q}, {C5, Q}, {D5, W},
    /* She'll be com-ing round the moun-tain, com-ing round the moun-tain */
    {G4, E}, {G4, E}, {E5, Q}, {E5, Q}, {E5, Q}, {E5, Q}, {D5, Q}, {C5, Q},
    {G4, Q}, {C5, Q}, {C5, Q}, {G4, Q}, {REST, Q},
    /* She'll be com-ing round the moun-tain when she comes */
    {E5, Q}, {E5, Q}, {E5, Q}, {D5, Q}, {C5, Q}, {B4, Q}, {C5, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 28. The Muffin Man
 *     Key: C major, 4/4, ~112 BPM
 *     Traditional English (1820)
 *     Src: musicnotes.com, 8notes.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t muffin_man_notes[] = {
    /* Oh do you know the muf-fin man, the */
    {E4, DQ}, {E4, E}, {F4, Q}, {G4, Q}, {G4, Q}, {A4, Q}, {G4, Q},
    /* muf-fin man, the muf-fin man? */
    {F4, Q}, {E4, Q}, {D4, Q}, {E4, Q}, {F4, H},
    /* Oh do you know the muf-fin man who */
    {E4, DQ}, {E4, E}, {F4, Q}, {G4, Q}, {G4, Q}, {A4, Q}, {G4, Q},
    /* lives on Dru-ry Lane? */
    {F4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 29. Sing a Song of Sixpence
 *     Key: C major, 4/4, ~110 BPM
 *     Traditional English (1744)
 *     Src: musicnotes.com, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t sixpence_notes[] = {
    /* Sing a song of six-pence, a pock-et full of rye */
    {G4, E}, {E4, E}, {E4, Q}, {F4, E}, {D4, E}, {D4, Q},
    {E4, E}, {F4, E}, {G4, Q}, {A4, H},
    /* Four and twen-ty black-birds baked in a pie */
    {G4, E}, {E4, E}, {E4, Q}, {F4, E}, {D4, E}, {D4, Q},
    {E4, Q}, {D4, Q}, {C4, H},
    /* When the pie was o-pened the birds be-gan to sing */
    {G4, E}, {E4, E}, {E4, Q}, {F4, E}, {D4, E}, {D4, Q},
    {E4, E}, {F4, E}, {G4, Q}, {A4, H},
    /* Was-n't that a dain-ty dish to set be-fore the king? */
    {A4, E}, {B4, E}, {C5, Q}, {A4, E}, {B4, E}, {C5, Q},
    {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 30. Oh My Darling Clementine
 *     Key: C major, 3/4, ~100 BPM
 *     Percy Montrose (1884), public domain
 *     Src: pianoletternotes.blogspot.com (verified)
 * ═══════════════════════════════════════════════════════════════ */
static const note_t clementine_notes[] = {
    /* Oh my dar-ling, oh my dar-ling */
    {E4, Q}, {E4, Q}, {E4, Q}, {G4, DH}, {G4, Q}, {G4, Q}, {E4, Q},
    /* Oh my dar-ling Cle-men-tine */
    {G4, Q}, {G4, Q}, {A4, Q}, {G4, Q}, {F4, Q}, {E4, Q},
    /* You are lost and gone for-ev-er */
    {F4, DH}, {F4, Q}, {F4, Q}, {E4, Q}, {D4, DH},
    {D4, Q}, {E4, Q}, {F4, Q},
    /* Dread-ful sor-ry, Cle-men-tine */
    {F4, Q}, {E4, Q}, {D4, Q}, {E4, DH},
    {C4, Q}, {C4, Q}, {C4, Q}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 31. Down by the Station
 *     Key: C major, 4/4, ~120 BPM
 *     Traditional American (early 1900s)
 *     Src: musicnotes.com, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t down_station_notes[] = {
    /* Down by the sta-tion ear-ly in the morn-ing */
    {C4, Q}, {C4, Q}, {E4, Q}, {E4, Q}, {G4, Q}, {G4, Q}, {E4, H},
    {F4, Q}, {F4, Q}, {E4, Q}, {E4, Q}, {D4, W},
    /* See the lit-tle puf-fer-bil-lies all in a row */
    {C4, Q}, {C4, Q}, {E4, Q}, {E4, Q}, {G4, Q}, {G4, Q}, {E4, H},
    {D4, Q}, {D4, Q}, {D4, Q}, {E4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 32. Five Little Ducks
 *     Key: C major, 4/4, ~110 BPM
 *     Traditional counting song
 *     Src: musicnotes.com, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t five_ducks_notes[] = {
    /* Five lit-tle ducks went out one day */
    {C4, Q}, {D4, Q}, {E4, Q}, {F4, Q}, {G4, Q}, {G4, Q}, {G4, H},
    /* O-ver the hills and far a-way */
    {A4, Q}, {A4, Q}, {G4, H}, {A4, Q}, {A4, Q}, {G4, H},
    /* Mo-ther duck said quack quack quack quack */
    {E4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, Q}, {C4, Q}, {D4, H},
    /* But on-ly four lit-tle ducks came back */
    {E4, Q}, {E4, Q}, {F4, Q}, {F4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 33. The Bear Went Over the Mountain
 *     Key: C major, 3/4, ~120 BPM
 *     Traditional (same melody as "For He's a Jolly Good Fellow")
 *     Src: carvedculture.com PD list, 8notes.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t bear_mountain_notes[] = {
    /* The bear went o-ver the moun-tain */
    {C4, Q}, {E4, Q}, {E4, Q}, {E4, Q}, {D4, Q}, {E4, Q},
    {F4, Q}, {F4, Q}, {F4, Q}, {E4, Q}, {F4, Q}, {G4, Q},
    /* The bear went o-ver the moun-tain */
    {A4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {E4, Q},
    {F4, DH}, {D4, DH},
    /* To see what he could see */
    {D4, Q}, {E4, Q}, {F4, Q}, {G4, DH},
    {C4, Q}, {C4, Q}, {C4, Q}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 34. Lavender's Blue
 *     Key: C major, 3/4 (waltz), ~100 BPM
 *     Traditional English folk song (1672)
 *     Src: musicnotes.com, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t lavenders_blue_notes[] = {
    /* Lav-en-der's blue, dil-ly dil-ly */
    {C4, Q}, {E4, Q}, {G4, Q}, {G4, H}, {A4, Q},
    /* Lav-en-der's green */
    {G4, Q}, {F4, Q}, {E4, Q}, {D4, DH},
    /* When I am king, dil-ly dil-ly */
    {D4, Q}, {F4, Q}, {A4, Q}, {A4, H}, {B4, Q},
    /* You shall be queen */
    {A4, Q}, {G4, Q}, {F4, Q}, {E4, DH},
    /* Who told you so, dil-ly dil-ly */
    {C4, Q}, {E4, Q}, {G4, Q}, {G4, H}, {A4, Q},
    /* Who told you so? */
    {G4, Q}, {F4, Q}, {E4, Q}, {D4, DH},
    /* 'Twas my own heart, dil-ly dil-ly */
    {D4, Q}, {F4, Q}, {A4, Q}, {A4, H}, {G4, Q},
    /* That told me so */
    {F4, Q}, {E4, Q}, {D4, Q}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 35. My Bonnie Lies Over the Ocean
 *     Key: C major, 3/4, ~108 BPM
 *     Traditional Scottish folk song (1881)
 *     Src: musicnotes.com, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t my_bonnie_notes[] = {
    /* My Bon-nie lies o-ver the o-cean */
    {G4, Q}, {E4, Q}, {G4, Q}, {A4, Q}, {G4, Q}, {E4, Q}, {D4, DH},
    /* My Bon-nie lies o-ver the sea */
    {E4, Q}, {C4, Q}, {E4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, DH},
    /* My Bon-nie lies o-ver the o-cean */
    {G4, Q}, {E4, Q}, {G4, Q}, {A4, Q}, {G4, Q}, {E4, Q}, {D4, DH},
    /* Oh bring back my Bon-nie to me */
    {E4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {F4, Q}, {E4, DH},
    /* Bring back, bring back, oh bring back my Bon-nie to me, to me */
    {C5, Q}, {C5, Q}, {A4, Q}, {G4, Q}, {E4, Q}, {C4, Q},
    {D4, Q}, {D4, Q}, {E4, Q}, {D4, Q}, {F4, Q}, {E4, Q},
    /* Bring back, bring back, oh bring back my Bon-nie to me */
    {C5, Q}, {C5, Q}, {A4, Q}, {G4, Q}, {E4, Q}, {C4, Q},
    {E4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {D4, Q}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 36. Polly Put the Kettle On
 *     Key: C major, 4/4, ~112 BPM
 *     Traditional English (1797)
 *     Src: musicnotes.com, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t polly_kettle_notes[] = {
    /* Pol-ly put the ket-tle on */
    {E4, Q}, {E4, E}, {F4, E}, {G4, Q}, {E4, Q},
    /* Pol-ly put the ket-tle on */
    {E4, Q}, {E4, E}, {F4, E}, {G4, Q}, {E4, Q},
    /* Pol-ly put the ket-tle on, we'll all have tea */
    {E4, Q}, {E4, E}, {F4, E}, {G4, Q}, {E4, Q},
    {D4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * 37. Little Bo Peep
 *     Key: C major, 6/8, ~100 BPM
 *     Traditional English
 *     Src: musicnotes.com, 8notes.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t bo_peep_notes[] = {
    /* Lit-tle Bo Peep has lost her sheep and */
    {E4, E}, {G4, DQ}, {G4, E}, {A4, E}, {G4, DQ}, {E4, DQ},
    /* does-n't know where to find them */
    {D4, E}, {E4, E}, {D4, E}, {C4, E}, {E4, E}, {G4, DQ},
    /* Leave them a-lone and they'll come home */
    {E4, E}, {G4, DQ}, {G4, E}, {A4, E}, {G4, DQ}, {E4, DQ},
    /* Wag-ging their tails be-hind them */
    {D4, E}, {E4, E}, {G4, E}, {F4, DQ}, {D4, E}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 38. Hey Diddle Diddle
 *     Key: C major, 6/8, ~108 BPM
 *     Traditional English
 *     Src: musicnotes.com, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t hey_diddle_notes[] = {
    /* Hey did-dle did-dle the cat and the fid-dle */
    {G4, E}, {E4, E}, {E4, E}, {E4, E}, {G4, E}, {G4, E},
    {A4, E}, {G4, E}, {F4, E}, {E4, E}, {D4, E}, {C4, E},
    /* The cow jumped o-ver the moon */
    {D4, E}, {E4, E}, {F4, E}, {G4, DQ}, {G4, DQ},
    /* The lit-tle dog laughed to see such sport */
    {G4, E}, {E4, E}, {E4, E}, {E4, E}, {G4, E}, {G4, E},
    {A4, E}, {G4, E}, {F4, E}, {E4, E}, {D4, E}, {C4, E},
    /* And the dish ran a-way with the spoon */
    {D4, E}, {E4, E}, {F4, E}, {E4, Q}, {D4, E}, {C4, DH},
};

/* ═══════════════════════════════════════════════════════════════
 * 39. Skip to My Lou
 *     Key: C major, 4/4, ~120 BPM
 *     Traditional American folk
 *     Src: musicnotes.com, bethsnotesplus.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t skip_to_lou_notes[] = {
    /* Skip skip skip to my Lou */
    {E4, Q}, {E4, Q}, {E4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {G4, H},
    /* Skip skip skip to my Lou */
    {E4, Q}, {E4, Q}, {E4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {G4, H},
    /* Skip skip skip to my Lou */
    {E4, Q}, {E4, Q}, {E4, Q}, {G4, Q}, {A4, Q}, {A4, Q}, {G4, H},
    /* Skip to my Lou, my dar-ling */
    {E4, Q}, {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {E4, Q}, {C4, H},
};

/* ═══════════════════════════════════════════════════════════════
 * 40. I've Been Working on the Railroad
 *     Key: C major, 4/4, ~120 BPM
 *     Traditional American folk (1894)
 *     Src: 8notes.com, musicnotes.com
 * ═══════════════════════════════════════════════════════════════ */
static const note_t railroad_notes[] = {
    /* I've been work-ing on the rail-road all the live-long day */
    {C4, Q}, {E4, Q}, {E4, Q}, {E4, Q}, {E4, DQ}, {F4, E}, {G4, H},
    {G4, Q}, {F4, Q}, {E4, Q}, {F4, Q}, {G4, W},
    /* I've been work-ing on the rail-road just to pass the time a-way */
    {C4, Q}, {E4, Q}, {E4, Q}, {E4, Q}, {E4, DQ}, {F4, E}, {G4, H},
    {A4, H}, {G4, H}, {E4, W},
    /* Can't you hear the whis-tle blow-in'? Rise up so ear-ly in the morn */
    {A4, Q}, {A4, Q}, {A4, Q}, {A4, Q}, {G4, Q}, {E4, Q}, {G4, H},
    {E4, Q}, {G4, Q}, {A4, Q}, {G4, Q}, {E4, Q}, {D4, Q}, {C4, H},
    /* Can't you hear the cap-tain shout-in'? Di-nah blow your horn! */
    {A4, Q}, {A4, Q}, {A4, Q}, {A4, Q}, {G4, Q}, {E4, Q}, {G4, H},
    {G4, Q}, {F4, Q}, {E4, Q}, {D4, Q}, {C4, W},
};

/* ═══════════════════════════════════════════════════════════════
 * Song table
 * ═══════════════════════════════════════════════════════════════ */

#define SONG_ENTRY(title_str, lyrics_str, bpm, ts_n, ts_d, arr) \
    { title_str, lyrics_str, bpm, ts_n, ts_d, arr, sizeof(arr)/sizeof(arr[0]) }

/* Per-phrase lyrics for songs that have them.
 * Songs with phrase_count=0 just show lyrics_short. */

/* ── Per-phrase lyrics for all 40 songs (auto-generated from C comments) ── */

static const song_lyrics_t s_lyrics[] = {
    /* 0: Twinkle Twinkle */
    { .phrases = {"Twinkle twinkle little star", "How I wonder what you are", "Up above the world so high", "Like a diamond in the sky", "Twinkle twinkle little star", "How I wonder what you are"},
      .starts = {0, 7, 14, 21, 28, 35}, .count = 6 },
    /* 1: Mary Had a Lamb */
    { .phrases = {"Mary had a little lamb", "Little lamb, little lamb", "Mary had a little lamb, its", "Fleece was white as snow"},
      .starts = {0, 7, 13, 21}, .count = 4 },
    /* 2: Baa Baa Black Sheep */
    { .phrases = {"Baa baa black sheep, have you any wool?", "Yes sir, yes sir, three bags full", "One for the master, one for the dame", "One for the little boy down the lane", "Baa baa black sheep, have you any wool?", "Yes sir, yes sir, three bags full"},
      .starts = {0, 9, 16, 25, 35, 44}, .count = 6 },
    /* 3: London Bridge */
    { .phrases = {"London bridge is falling down", "Falling down, falling down", "London bridge is falling down", "My fair lady"},
      .starts = {0, 7, 13, 20}, .count = 4 },
    /* 4: Row Your Boat */
    { .phrases = {"Row, row, row your boat", "Gently down the stream", "Merrily merrily merrily merrily", "Life is but a dream"},
      .starts = {0, 5, 10, 22}, .count = 4 },
    /* 5: Jack and Jill */
    { .phrases = {"Jack and Jill went up the hill", "To fetch a pail of water", "Jack fell down and broke his crown", "And Jill came tumbling after"},
      .starts = {0, 8, 14, 22}, .count = 4 },
    /* 6: Humpty Dumpty */
    { .phrases = {"Humpty Dumpty sat on a wall", "Humpty Dumpty had a great fall", "All the king's horses and all the king's men", "Couldn't put Humpty together again"},
      .starts = {0, 11, 22, 34}, .count = 4 },
    /* 7: Itsy Bitsy Spider */
    { .phrases = {"The itsy bitsy spider climbed up the water spout", "Down came the rain and washed the spider out", "Out came the sun and dried up all the rain", "The itsy bitsy spider climbed up the spout again"},
      .starts = {0, 13, 23, 34}, .count = 4 },
    /* 8: Old MacDonald */
    { .phrases = {"Old MacDonald had a farm", "E-I-E-I-O", "And on his farm he had a cow", "E-I-E-I-O", "With a moo-moo here, moo-moo there", "Here a moo, there a moo, everywhere a moo-moo", "Old MacDonald had a farm, E-I-E-I-O"},
      .starts = {0, 7, 14, 21, 28, 36, 48}, .count = 7 },
    /* 9: Hickory Dickory */
    { .phrases = {"Hickory dickory dock", "The mouse ran up the clock", "The clock struck one, the mouse ran down", "Hickory dickory dock"},
      .starts = {0, 6, 12, 20}, .count = 4 },
    /* 10: Three Blind Mice */
    { .phrases = {"Three blind mice", "Three blind mice", "See how they run", "See how they run", "They all ran after the farmer's wife", "She cut off their tails with a carving knife", "Did you ever see such a thing in your life?"},
      .starts = {0, 3, 6, 10, 14, 26, 38}, .count = 7 },
    /* 11: Hot Cross Buns */
    { .phrases = {"Hot cross buns", "Hot cross buns", "One a penny, two a penny", "Hot cross buns"},
      .starts = {0, 3, 6, 14}, .count = 4 },
    /* 12: Frere Jacques */
    { .phrases = {"Frere Jacques", "Frere Jacques", "Dormez-vous?", "Dormez-vous?", "Sonnez les matines", "Sonnez les matines", "Din din don", "Din din don"},
      .starts = {0, 4, 8, 11, 14, 20, 26, 29}, .count = 8 },
    /* 13: Yankee Doodle */
    { .phrases = {"Yankee Doodle went to town", "Riding on a pony", "Stuck a feather in his hat", "And called it macaroni", "Yankee Doodle keep it up", "Yankee Doodle dandy", "Mind the music and the step", "And with the girls be handy"},
      .starts = {0, 4, 8, 12, 15, 19, 22, 26}, .count = 8 },
    /* 14: Pop Goes the Weasel */
    { .phrases = {"All around the mulberry bush", "The monkey chased the weasel", "The monkey thought 'twas all in fun", "Pop goes the weasel!", "A penny for a spool of thread", "A penny for a needle", "That's the way the money goes", "Pop goes the weasel!"},
      .starts = {0, 6, 10, 16, 20, 26, 30, 36}, .count = 8 },
    /* 15: Rock-a-Bye Baby */
    { .phrases = {"Rock-a-bye baby on the tree top", "When the wind blows the cradle will rock", "When the bough breaks the cradle will fall", "And down will come baby, cradle and all"},
      .starts = {0, 8, 16, 24}, .count = 4 },
    /* 16: Hush Little Baby */
    { .phrases = {"Hush little baby, don't say a word", "Mama's gonna buy you a mockingbird", "And if that mockingbird don't sing", "Mama's gonna buy you a diamond ring"},
      .starts = {0, 7, 14, 21}, .count = 4 },
    /* 17: Wheels on the Bus */
    { .phrases = {"The wheels on the bus go round and round", "Round and round, round and round", "The wheels on the bus go round and round", "All through the town"},
      .starts = {0, 7, 13, 20}, .count = 4 },
    /* 18: Head Shoulders */
    { .phrases = {"Head, shoulders, knees and toes", "Head, shoulders, knees and toes", "And eyes and ears and mouth and nose", "Head, shoulders, knees and toes"},
      .starts = {0, 7, 14, 22}, .count = 4 },
    /* 19: Happy & You Know It */
    { .phrases = {"If you're happy and you know it, clap your hands!", "If you're happy and you know it, clap your hands!", "If you're happy and you know it, and you really want to show it", "If you're happy and you know it, clap your hands!"},
      .starts = {0, 10, 20, 28}, .count = 4 },
    /* 20: This Old Man */
    { .phrases = {"This old man, he played one", "He played knick-knack on my thumb", "With a knick-knack paddy whack, give the dog a bone", "This old man came rolling home"},
      .starts = {0, 4, 8, 18}, .count = 4 },
    /* 21: Farmer in the Dell */
    { .phrases = {"The farmer in the dell", "The farmer in the dell", "Hi-ho the derry-o", "The farmer in the dell"},
      .starts = {0, 5, 10, 14}, .count = 4 },
    /* 22: BINGO */
    { .phrases = {"There was a farmer had a dog and Bingo was his name-o", "B-I-N-G-O!", "B-I-N-G-O!", "And Bingo was his name-o!"},
      .starts = {0, 14, 21, 28}, .count = 4 },
    /* 23: Little Teapot */
    { .phrases = {"I'm a little teapot, short and stout", "Here is my handle, here is my spout", "When I get all steamed up, hear me shout:", "Tip me over and pour me out!"},
      .starts = {0, 10, 19, 29}, .count = 4 },
    /* 24: Alouette */
    { .phrases = {"Alouette, gentille alouette", "Alouette, je te plumerai", "Je te plumerai la tete", "Je te plumerai la tete", "Et la tete! Alouette!", "Alouette, gentille alouette", "Alouette, je te plumerai"},
      .starts = {0, 7, 14, 20, 26, 31, 38}, .count = 7 },
    /* 25: Oh! Susanna */
    { .phrases = {"Oh I come from Alabama with a banjo on my knee", "I'm goin' to Louisiana, my true love for to see", "Oh! Susanna, oh don't you cry for me", "I come from Alabama with a banjo on my knee"},
      .starts = {0, 15, 30, 38}, .count = 4 },
    /* 26: Coming Round the Mountain */
    { .phrases = {"She'll be coming round the mountain when she comes", "She'll be coming round the mountain when she comes", "She'll be coming round the mountain, coming round the mountain", "She'll be coming round the mountain when she comes"},
      .starts = {0, 11, 22, 35}, .count = 4 },
    /* 27: The Muffin Man */
    { .phrases = {"Oh, do you know the muffin man?", "The muffin man, the muffin man?", "Oh, do you know the muffin man", "Who lives on Drury Lane?"},
      .starts = {0, 7, 12, 19}, .count = 4 },
    /* 28: Sixpence */
    { .phrases = {"Sing a song of sixpence, a pocket full of rye", "Four and twenty blackbirds baked in a pie", "When the pie was opened, the birds began to sing", "Wasn't that a dainty dish to set before the king?"},
      .starts = {0, 10, 19, 29}, .count = 4 },
    /* 29: Clementine */
    { .phrases = {"Oh my darling, oh my darling", "Oh my darling Clementine", "You are lost and gone forever", "Dreadful sorry, Clementine"},
      .starts = {0, 7, 13, 21}, .count = 4 },
    /* 30: Down by the Station */
    { .phrases = {"Down by the station, early in the morning", "See the little pufferbillies all in a row"},
      .starts = {0, 12}, .count = 2 },
    /* 31: Five Little Ducks */
    { .phrases = {"Five little ducks went out one day", "Over the hills and far away", "Mother duck said quack quack quack quack", "But only four little ducks came back"},
      .starts = {0, 7, 13, 20}, .count = 4 },
    /* 32: Bear Over Mountain */
    { .phrases = {"The bear went over the mountain", "The bear went over the mountain", "To see what he could see"},
      .starts = {0, 12, 20}, .count = 3 },
    /* 33: Lavender's Blue */
    { .phrases = {"Lavender's blue, dilly dilly", "Lavender's green", "When I am king, dilly dilly", "You shall be queen", "Who told you so, dilly dilly?", "Who told you so?", "'Twas my own heart, dilly dilly", "That told me so"},
      .starts = {0, 5, 9, 14, 18, 23, 27, 32}, .count = 8 },
    /* 34: My Bonnie */
    { .phrases = {"My Bonnie lies over the ocean", "My Bonnie lies over the sea", "My Bonnie lies over the ocean", "Oh bring back my Bonnie to me", "Bring back, bring back", "Oh bring back my Bonnie to me"},
      .starts = {0, 7, 14, 21, 28, 40}, .count = 6 },
    /* 35: Polly Kettle On */
    { .phrases = {"Polly put the kettle on", "Polly put the kettle on", "Polly put the kettle on, we'll all have tea"},
      .starts = {0, 5, 10}, .count = 3 },
    /* 36: Little Bo Peep */
    { .phrases = {"Little Bo Peep has lost her sheep", "And doesn't know where to find them", "Leave them alone and they'll come home", "Wagging their tails behind them"},
      .starts = {0, 6, 12, 18}, .count = 4 },
    /* 37: Hey Diddle Diddle */
    { .phrases = {"Hey diddle diddle, the cat and the fiddle", "The cow jumped over the moon", "The little dog laughed to see such sport", "And the dish ran away with the spoon"},
      .starts = {0, 12, 17, 29}, .count = 4 },
    /* 38: Skip to My Lou */
    { .phrases = {"Skip, skip, skip to my Lou", "Skip, skip, skip to my Lou", "Skip, skip, skip to my Lou", "Skip to my Lou, my darling"},
      .starts = {0, 7, 14, 21}, .count = 4 },
    /* 39: Working on the Railroad */
    { .phrases = {"I've been working on the railroad, all the livelong day", "I've been working on the railroad, just to pass the time away", "Can't you hear the whistle blowin'? Rise up so early in the morn", "Can't you hear the captain shoutin'? Dinah, blow your horn!"},
      .starts = {0, 12, 22, 36}, .count = 4 },
};

/* All 40 songs have lyrics — 1:1 mapping */
static const int8_t s_lyrics_map[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
};

const char *song_get_phrase(int song_index, int note_index)
{
    if (song_index < 0 || song_index >= g_song_count) return "";
    if (song_index < (int)(sizeof(s_lyrics_map)/sizeof(s_lyrics_map[0])) &&
        s_lyrics_map[song_index] >= 0) {
        const song_lyrics_t *lyr = &s_lyrics[s_lyrics_map[song_index]];
        /* Find which phrase contains this note index */
        for (int i = lyr->count - 1; i >= 0; i--) {
            if (note_index >= lyr->starts[i]) {
                return lyr->phrases[i];
            }
        }
    }
    return g_songs[song_index].lyrics_short;
}

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
#if SONG_ON(CONFIG_SONG_THIS_OLD_MAN)
    SONG_ENTRY("This Old Man",         "This old man, he played one",         120, 4, 4, this_old_man_notes),
#endif
#if SONG_ON(CONFIG_SONG_FARMER_DELL)
    SONG_ENTRY("Farmer in the Dell",   "The farmer in the dell",             120, 6, 8, farmer_dell_notes),
#endif
#if SONG_ON(CONFIG_SONG_BINGO)
    SONG_ENTRY("BINGO",                "There was a farmer had a dog",       120, 4, 4, bingo_notes),
#endif
#if SONG_ON(CONFIG_SONG_LITTLE_TEAPOT)
    SONG_ENTRY("Little Teapot",        "I'm a little teapot short and stout",110, 4, 4, little_teapot_notes),
#endif
#if SONG_ON(CONFIG_SONG_ALOUETTE)
    SONG_ENTRY("Alouette",             "Alouette, gentille alouette",        120, 4, 4, alouette_notes),
#endif
#if SONG_ON(CONFIG_SONG_OH_SUSANNA)
    SONG_ENTRY("Oh! Susanna",          "Oh I come from Alabama",             120, 4, 4, oh_susanna_notes),
#endif
#if SONG_ON(CONFIG_SONG_COMING_ROUND)
    SONG_ENTRY("Coming Round Mountain","She'll be coming round the mountain",120, 4, 4, coming_round_notes),
#endif
#if SONG_ON(CONFIG_SONG_MUFFIN_MAN)
    SONG_ENTRY("The Muffin Man",       "Do you know the muffin man?",        112, 4, 4, muffin_man_notes),
#endif
#if SONG_ON(CONFIG_SONG_SIXPENCE)
    SONG_ENTRY("Sixpence",             "Sing a song of sixpence",            110, 4, 4, sixpence_notes),
#endif
#if SONG_ON(CONFIG_SONG_CLEMENTINE)
    SONG_ENTRY("Clementine",           "Oh my darling Clementine",           100, 3, 4, clementine_notes),
#endif
#if SONG_ON(CONFIG_SONG_DOWN_STATION)
    SONG_ENTRY("Down by the Station",  "Down by the station early morning",  120, 4, 4, down_station_notes),
#endif
#if SONG_ON(CONFIG_SONG_FIVE_DUCKS)
    SONG_ENTRY("Five Little Ducks",    "Five little ducks went out one day", 110, 4, 4, five_ducks_notes),
#endif
#if SONG_ON(CONFIG_SONG_BEAR_MOUNTAIN)
    SONG_ENTRY("Bear Over Mountain",   "The bear went over the mountain",    120, 3, 4, bear_mountain_notes),
#endif
#if SONG_ON(CONFIG_SONG_LAVENDERS_BLUE)
    SONG_ENTRY("Lavender's Blue",      "Lavender's blue, dilly dilly",       100, 3, 4, lavenders_blue_notes),
#endif
#if SONG_ON(CONFIG_SONG_MY_BONNIE)
    SONG_ENTRY("My Bonnie",            "My Bonnie lies over the ocean",      108, 3, 4, my_bonnie_notes),
#endif
#if SONG_ON(CONFIG_SONG_POLLY_KETTLE)
    SONG_ENTRY("Polly Kettle On",      "Polly put the kettle on",            112, 4, 4, polly_kettle_notes),
#endif
#if SONG_ON(CONFIG_SONG_BO_PEEP)
    SONG_ENTRY("Little Bo Peep",       "Little Bo Peep has lost her sheep",  100, 6, 8, bo_peep_notes),
#endif
#if SONG_ON(CONFIG_SONG_HEY_DIDDLE)
    SONG_ENTRY("Hey Diddle Diddle",    "Hey diddle diddle the cat and fiddle",108, 6, 8, hey_diddle_notes),
#endif
#if SONG_ON(CONFIG_SONG_SKIP_TO_LOU)
    SONG_ENTRY("Skip to My Lou",       "Skip skip skip to my Lou",           120, 4, 4, skip_to_lou_notes),
#endif
#if SONG_ON(CONFIG_SONG_RAILROAD)
    SONG_ENTRY("Working on Railroad",  "I've been working on the railroad",  120, 4, 4, railroad_notes),
#endif
};

const int g_song_count = sizeof(g_songs) / sizeof(g_songs[0]);

/* NOTE: To add more songs later, add the note array above the song table,
 * add a SONG_ENTRY line inside #if SONG_ON(...), and add a matching
 * config entry in Kconfig.projbuild. */
