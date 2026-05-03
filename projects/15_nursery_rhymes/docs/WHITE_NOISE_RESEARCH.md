# White Noise for Baby Sleep — Research Notes

Research collected for the proposed white-noise feature in
`projects/15_nursery_rhymes`. Goal: pick the right *kinds* of noise and the
right *limits* so the feature is safe, calming, and reproducible on the
ESP32-C6 + ES8311 + NS4150B hardware.

---

## 1. The "colors" of noise

Continuous broadband noise is classified by how its power spectral density
(PSD) varies with frequency `f`:

| Color    | PSD slope | Subjective character             | Real-world analog          |
|----------|-----------|----------------------------------|----------------------------|
| White    | flat (0 dB/oct) | bright, hissy, "TV static"  | untuned FM radio           |
| Pink     | −3 dB/oct (1/f) | balanced, "rainfall"        | steady rain, distant surf  |
| Brown    | −6 dB/oct (1/f²) | deep, rumbly                | low waterfall, jet cabin   |
| "Womb"   | −6 to −9 dB/oct + heartbeat amplitude modulation | thumping, muffled rumble | in-utero recordings |

Equal-loudness curves (Fletcher-Munson) make white noise sound dominated by
the upper-mid range to human ears, even though its energy is flat. Pink and
brown noise feel more "natural" because the −3 dB/oct rolloff matches the
ear's frequency sensitivity better.

### Why noise color matters for babies

- **Newborn / 4th-trimester (0–4 months)**: prefer **brown / womb sounds**.
  In utero, high frequencies are filtered by amniotic fluid and tissue;
  babies hear a low-frequency rumble (~85 dB SPL background, peaks at the
  maternal heartbeat) for nine months. Recreating that sound triggers a
  calming reflex (Karp, *Happiest Baby on the Block*). The Baby Shusher and
  similar devices are essentially low-pass-filtered noise modulated at the
  cadence of "shhh".
- **4 months and up**: **pink noise** is most studied. The 1/f spectrum
  reduces the contrast between baseline noise and sudden disturbances
  (door, sibling, dog), so the baby is less likely to wake. EEG research
  has shown pink noise reduces brain-wave complexity and can deepen slow-wave
  sleep.
- **Older infants / toddlers**: any of white, pink, or brown can work; this
  is age where preference becomes individual. White noise is brighter and
  good for masking sharp household sounds; brown noise is good for
  light-sleeper toddlers.

### Practical takeaway for our feature

Ship at minimum these four "voices":

1. **White** — flat spectrum, broad masking
2. **Pink** — softer, recommended default for >4 months
3. **Brown** — deep, mask low rumbles, good for lullaby pairing
4. **Womb / Shusher** — pink/brown base + 4 Hz amplitude modulation, plus
   optional heartbeat thump at ~70 BPM. Aimed at newborns.

Optional "flavors" (pre-filtered colored noise) that cost very little extra
code and make the menu feel rich:

5. **Fan** — white passed through a narrow-Q resonance around 200–400 Hz
6. **Rain** — pink with high-frequency emphasis (+3 dB above 4 kHz) plus
   sparse impulse drops
7. **Ocean / surf** — brown noise amplitude-modulated at ~0.1 Hz (10 s wave
   cycle)

Stretch goal — none of the rest are essential for v1.

---

## 2. Safety: how loud, how long, how far

The American Academy of Pediatrics (AAP) and the 2014 Hugh et al. paper in
*Pediatrics* drove the modern guidance:

| Parameter          | Recommendation                                            |
|--------------------|-----------------------------------------------------------|
| Maximum SPL at crib | **≤ 50 dB(A)** — about a quiet refrigerator              |
| Distance from crib  | **≥ 200 cm (7 ft)** — never directly in the bassinet     |
| Continuous duration | Limit to nap/sleep onset; consider auto-off              |
| Spectrum            | Avoid concentrating energy >4 kHz at high SPL            |

The 2014 Hugh paper found 14 of 14 commercial infant noise machines could
exceed 50 dB at typical bedside placement, and 3 exceeded 85 dB at maximum
volume. Karp's defence of higher SPL during *active* crying (matching the
~85 dB level of the cry to break the cycle) is a short-term technique, not
sustained playback.

### Constraints this places on our firmware

- **Default volume must be conservative.** Boot the white-noise page with
  the codec output volume set to a low value (suggestion: 35/100, equivalent
  to roughly 50 dB at ~30 cm with the NS4150B; the parent verifies in their
  own room).
- **No "max" preset.** The volume slider should top out below the codec's
  absolute maximum. Cap at 80/100.
- **Auto-off timer.** Default 60 min, options 15/30/60/∞. Continuous-only is
  acceptable but discouraged.
- **Fade-out.** When the timer ends or the user stops, fade volume linearly
  over 10–15 s rather than cutting hard. Sudden silence is itself a sleep
  disruptor.
- **Volume label in dB-equivalent is risky** — without calibration the
  number lies. Use a percent slider and document the SPL behaviour in the
  README.

---

## 3. Hardware constraints from this board

Findings from the existing `audio_player.c` and project notes:

| Constraint                         | Implication for noise generator        |
|------------------------------------|-----------------------------------------|
| Sample rate **16 kHz**             | Nyquist 8 kHz — covers 99% of perceived noise content; high hiss above 8 kHz is gone, which is fine (and slightly pink-ish by default) |
| 16-bit stereo                      | int16 PCM, identical L/R is OK         |
| **NS4150B speaker poor < 400 Hz**  | Pure brown noise will sound thin; need to either accept it or add a "fundamental" tonal cue |
| No PSRAM                           | Cannot pre-render a 30-min PCM buffer; must synthesize on the fly |
| Single core (RISC-V 160 MHz)       | Algorithm must be cheap; avoid per-sample FFTs |
| 4 KB audio task stack              | No large local buffers; reuse the existing 256-sample stereo buffer |
| LFSR noise example already exists  | `projects/17_pixelpet/main/audio_output.c:155` — 16-bit Galois LFSR (taps 0,2,3,5) generates white noise at zero cost |

### Speaker reality check

The NS4150B + onboard speaker physically cannot reproduce 30 Hz "true brown
noise" content. What it *can* do is reproduce noise that has been **shaped**
toward the speaker's response (≈ 400 Hz – 6 kHz, peaking around 1–2 kHz).
This is also where masking-of-household-noise is most useful for sleep.
Don't fight the hardware: design noise voices that sit in this band.

---

## 4. Algorithms — chosen for cost vs. quality

### 4.1 White noise — direct LFSR

A 16-bit Galois LFSR with taps `{16, 14, 13, 11}` (or the existing
`{0,2,3,5}` Fibonacci form in pixelpet) produces a flat-spectrum sequence at
1 multiply-free op per sample. Period 65535 samples ≈ 4 s at 16 kHz, which
is audibly periodic. Use a 32-bit xorshift instead — period 2³²−1 (~74 h),
single XOR/shift sequence, fits in registers.

```c
static uint32_t s_xs = 0xC0FFEEu;
static inline int16_t white_sample(void) {
    s_xs ^= s_xs << 13;
    s_xs ^= s_xs >> 17;
    s_xs ^= s_xs << 5;
    return (int16_t)(s_xs >> 16);  // top 16 bits are well-mixed
}
```

Cost: ~3 ops per sample. Effectively free.

### 4.2 Pink noise — Voss-McCartney (additive)

Approximates 1/f by summing N white sources updated at rates
`{f, f/2, f/4, … f/2^(N-1)}`. With 7 octaves of generators we get a flat
1/f curve from ~30 Hz up to Nyquist with ±0.5 dB error — more than enough.

The clever optimisation: count trailing zeros of the sample index `n` to
decide which generator to update; only one generator changes per sample.

```c
#define PINK_OCT 7
static int32_t s_rows[PINK_OCT];
static int32_t s_pink_run;
static uint32_t s_pink_n;

static inline int16_t pink_sample(void) {
    s_pink_n++;
    int k = __builtin_ctz(s_pink_n);   // 0..PINK_OCT-1
    if (k < PINK_OCT) {
        int32_t old = s_rows[k];
        int32_t new_ = (int32_t)white_sample();   // -32768..32767
        s_pink_run += new_ - old;
        s_rows[k] = new_;
    }
    // Sum is ~PINK_OCT × 16-bit; scale to int16
    return (int16_t)(s_pink_run / PINK_OCT);
}
```

Cost: a CTZ + one subtract/add + one divide per sample. RISC-V has a `ctz`
instruction; total ~5 cycles.

### 4.3 Brown noise — single-pole leaky integrator

A first-order IIR `y[n] = α·y[n−1] + β·white[n]` with α ≈ 0.997 produces a
−6 dB/oct slope. Add saturation to prevent integrator drift.

```c
static float s_brown = 0.0f;
static inline int16_t brown_sample(void) {
    s_brown = 0.997f * s_brown + 0.025f * (float)white_sample();
    if (s_brown >  20000.f) s_brown =  20000.f;
    if (s_brown < -20000.f) s_brown = -20000.f;
    return (int16_t)s_brown;
}
```

If we want to avoid float in the audio inner loop, swap for a Q15 fixed-point
form using two `int32_t` ops per sample. Either is < 10 cycles.

### 4.4 Womb / Shusher — modulated brown + heartbeat

Layer two effects on the brown noise generator:

1. **Shush envelope** — amplitude modulation at 4 Hz with 50 % depth,
   producing the "shhh-shhh-shhh" cadence (matches ~240 BPM, similar to a
   resting fetal heart rate). Implementation: multiply each output sample by
   `(0.5 + 0.5·sin(2π · 4Hz · t))^2` (squared sine = smoother).
2. **Heartbeat thump** — every ~860 ms (≈70 BPM resting heart rate) inject a
   short low-pass filtered impulse at -10 dB relative to the noise floor.
   Use a windowed sine burst: 60 Hz × 60 ms with a half-cosine envelope. Two
   beats per cycle (lub-dub) separated by 200 ms gives the characteristic
   pattern.

The heartbeat is at 60 Hz which is below NS4150B's response, but the burst
*envelope* shape will still be perceived through harmonic distortion
products in the speaker — this is good enough for the womb illusion.

### 4.5 Spectrum-shaped flavours (rain, fan, ocean)

Each is a 1- or 2-pole filter applied on top of an existing color:

- **Rain**: pink noise + small amount of high-shelf boost (+3 dB above
  4 kHz). Implementation: 1-pole HPF feedforward `y = pink + 0.3·(pink -
  prev_pink)`.
- **Fan**: white noise through a 1-pole LPF at 1.2 kHz, plus a low-Q peak
  at 200 Hz to suggest motor rumble.
- **Ocean**: brown noise multiplied by a slow LFO at 0.1 Hz, amplitude swing
  60–100 %. Result: ~10 s wave cycle.

All three can be derived from white/pink/brown by a single biquad — share
one biquad implementation.

### Computed cost summary

| Voice           | Per-sample cost (estimate) | Comments                       |
|-----------------|-----------------------------|--------------------------------|
| White           | ~3 cycles                   | xorshift                       |
| Pink            | ~12 cycles                  | Voss-McCartney + scale         |
| Brown           | ~15 cycles (Q15)            | single IIR pole                |
| Womb            | ~35 cycles                  | brown + sin LFO + heartbeat    |
| Rain/Fan/Ocean  | brown/pink + ~10 cycles     | extra biquad/LFO               |

At 16 kHz × 35 cycles worst-case = 560 k cycles/s ≈ 0.4 % of a 160 MHz core.
We have headroom for everything plus LVGL.

---

## 5. Volume / fade behaviour

Codec volume is already clamped 0..100 and applied via
`esp_codec_dev_set_out_vol`. For sleep playback we need:

- **Linear fade-in** on play start: 0 → user-set volume over 3 s. Avoids
  the click that today's implementation already partially mitigates with
  the 2 ms anti-click ramp.
- **Linear fade-out** on stop or timer expiry: 100 % → 0 over 12 s, then
  silence, then disable amp via `audio_player_amp_enable(false)` to
  eliminate residual hiss.
- **Volume slider behaviour**: smoothing — when the user drags, ramp the
  active gain over 200 ms to the target. Prevents zipper noise.

The codec hardware has a finite number of volume steps; ramping the gain
*inside* our synthesis (multiplying the output sample) gives finer control
than ramping codec volume directly.

---

## 6. Recommended defaults for v1

| Parameter         | Default        | Min  | Max  | Notes                          |
|-------------------|----------------|------|------|--------------------------------|
| Voice             | Pink           | —    | —    | Best general default           |
| Volume            | 35 %           | 5 %  | 80 % | Hard cap below codec maximum   |
| Timer             | 60 min         | 15   | ∞    | "∞" = continuous               |
| Fade-in           | 3 s            | —    | —    | Always on                      |
| Fade-out          | 12 s           | —    | —    | Always on                      |
| Heartbeat in womb | ON             | —    | —    | Toggleable in v2               |

---

## 7. Open questions

- Should noise pause when a song is selected from the main list, or should
  the song *replace* it? Current `audio_player_play()` aborts whatever is
  playing — that semantics extends naturally to "playing noise is just
  another track that gets cancelled". Recommend keeping that.
- Should we expose a separate volume for noise vs. song? Noise calibrated to
  ~50 dB SPL is much quieter than a sung melody at the same codec setting
  (because of crest factor). For v1, single shared codec volume is OK; v2
  could store a per-voice gain.
- Do we want NVS-persisted last-used voice and timer? Strong yes — sleep
  routines are habitual; reopening the page should not require re-picking.

---

## 8. Sources

- [American Academy of Pediatrics — Preventing Excessive Noise Exposure in Infants, Children, and Adolescents (2023)](https://publications.aap.org/pediatrics/article/152/5/e2023063752/194468/Preventing-Excessive-Noise-Exposure-in-Infants)
- [Nationwide Children's — Understanding White, Brown and Pink Noise for Children's Sleep (2025)](https://www.nationwidechildrens.org/family-resources-education/700childrens/2025/07/colorful-noise-and-sleep)
- [Hugh et al. — Hazardous sound outputs of white noise devices intended for infants (PubMed)](https://pubmed.ncbi.nlm.nih.gov/33992973/)
- [Sleep Baby Consulting — Understanding the Latest AAP Guidelines on Noise Machines](https://www.sleepbabyconsulting.com/sleep-tips-for-tired-parents/2023/11/1/understanding-the-latest-aap-guidelines-on-noise-machines-for-children)
- [Happiest Baby — How White Noise Can Help Your Baby Sleep](https://www.happiestbaby.com/blogs/baby/white-noise-for-baby-sleep)
- [Baby Shusher — What does the Baby Shusher sound like?](https://babyshusher.com/pages/what-does-baby-shusher-sound-like)
- [Hearing Health & Technology Matters — What can fetuses and babies hear?](https://hearinghealthmatters.org/hearing-international/2023/baby-sound-generators/)
- [firstpr.com.au — DSP Generation of Pink Noise (Voss-McCartney summary)](https://www.firstpr.com.au/dsp/pink-noise/)
- [Allen Downey — Generating pink noise (dsprelated.com)](https://www.dsprelated.com/showarticle/908.php)
- [RidgeRat — Improved Pink Noise Generator Algorithm](https://www.ridgerat-tech.us/pink/newpink.htm)
- [Wikipedia — Pink noise](https://en.wikipedia.org/wiki/Pink_noise)
