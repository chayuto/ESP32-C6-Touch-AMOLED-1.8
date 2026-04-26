# Virtual pet animation — design references

> What makes a Tamagotchi-style character feel alive and "chilling" — the
> low-stakes ambient animation you can stare at while doing nothing.
> Compiled from pixel-art tutorials, virtual-pet design breakdowns, and
> open-source projects to inform the PixelPet animation rework.
>
> Date: 2026-04-26.

---

## 1. Idle animation taxonomy — timings & frequencies

The trick: layer multiple loops, **each on its own clock**, so no two
ever sync. Concrete numbers from pixel-art tutorials and Tamagotchi
animation breakdowns:

| Loop | Frames | Per-frame | Period / cadence |
|---|---|---|---|
| **Breathing** | 2–4 | 300–500 ms | ~1–2 s loop, runs forever; 1 px vertical squash on exhale |
| **Blink** | 2 (open / closed) | closed held 80–150 ms | jittered gap 2.5–8 s; ~10% chance of double-blink |
| **Look-around / head-turn** | 2–3 | dart frames fast, then hold 600–1200 ms | every 8–20 s |
| **Yawn / stretch / "big" idle** | 4+ | varies | every 30–90 s |
| **Anticipation frame** | 1 | 60–80 ms | preceding any deliberate action |

### Mood-conditioned tempo

[TamaFi](https://github.com/cifertech/TamaFi) varies idle-animation
playback speed by mood (HAPPY / CALM / BORED / SICK / HUNGRY / EXCITED).
**Same frames, different ms-per-frame and different big-idle frequency.**
This is a low-cost way to multiply apparent character.

### Pou / Tamagotchi pattern

A small set of 2-frame loops (sit, sway, sniff, sleep) chosen by a
**weighted random picker every N seconds**, with state biases.

**Implementation takeaway:** build a multi-clock idle scheduler —
`breath_clock` always running, `blink_clock` with jittered interval,
`glance_clock` rarer, `bigIdle_clock` rarest, all fed by a
mood-weighted picker.

---

## 2. "Fishbowl" / aquarium aesthetic

Elements consistently cited in aquarium screensavers (Dream Aquarium,
SereneScreen Marine Aquarium, Aquarium Live Wallpapers 4K) and cozy
pixel scenes:

- **Parallax depth** — background drifts slower than foreground; even
  2-layer parallax reads as "3D".
- **Drifting bubbles** — spawned from 2–3 fixed emitters, sinusoidal
  x-wobble, 3–8 s rise.
- **Ambient particles** — dust motes / floating debris with very slow
  drift.
- **Day/night tint shift** — slow palette LUT swap tied to RTC. (The
  PCF85063 is on this board — perfect.)
- **Pet wandering between zones** — Neko Atsume's pattern: cats
  teleport to fixed "interaction spots" (mat, scratch post, food bowl)
  and play a spot-specific 2-frame loop. For PixelPet: define 4–6
  zones; on a 30–120 s timer, walk to a new one and play a
  zone-specific micro-loop.
- **Idle-only autonomy** ("ambient mode" in screensavers) — no input
  required; the scene runs itself.
- **Tearing-effect** — keep the SH8601 panel TE on so slow drifts
  don't tear.

---

## 3. Open-source virtual pet projects (last 3 yrs)

### [TamaFi / TamaFi V2](https://github.com/cifertech/TamaFi)
ESP32-S3, ST7789 240×240. `ui_anim.h` holds named sprite-frame tables
(`idle`, `egg`, `hunt`, `rest`). Uses `TFT_eSprite` framebuffer for
flicker-free transparent sprites. **Idle speed scales with mood**;
explicit enter/exit animations on rest. V2 (Dec 2025) adds environment
reactivity (Wi-Fi scan affects mood). Closest match to what PixelPet
should become.

### [ESP32-TamaPetchi](https://github.com/CyberXcyborg/ESP32-TamaPetchi)
"Sentient pixel spirit" framing. `updatePet()` tick handles state
transitions (sleeping/sick/normal); web UI for control; small pixel
sprite set.

### [TamagotchiESP32 (RBEGamer)](https://github.com/RBEGamer/TamagotchiESP32) / [Tamagotchi (anabolyc)](https://github.com/anabolyc/Tamagotchi)
Emulators of original Bandai ROMs. Useful as a reference for the
original's cadence (long holds, 2-frame swaps) rather than novel art.

**Common technique across all:** **named animation tables in
PROGMEM/flash, played by a state machine** — exactly what to copy.

---

## 4. Pixel-art "feeling alive" tricks

From MortMort, AdamCYounis, Pedro Medeiros (Miniboss), and
12-principles-for-pixel-art guides:

- **Squash & stretch in 1 px** — for a 14 px-tall sprite, squash to
  12–13 px tall + 1 px wider; stretch to 15–16 px tall + 1 px narrower.
  Landing squash held 80–120 ms.
- **Frame-hold variation > frame count** — shipped idle loops are
  usually 2–4 frames with **uneven hold times** (e.g. 400 / 300 /
  300 / 500 ms). Even spacing reads robotic.
- **Anticipation frame** — 1 frame, 60–80 ms, opposite direction of
  the action.
- **Sub-pixel illusion via dithering / shading shift** — move the
  highlight pixel one position without moving the silhouette; reads as
  motion under a px.
- **Secondary motion** — ears, tail, antenna, hair lag the body by 1
  frame.
- **Eye blink hold ~80–150 ms** — any longer looks sleepy on purpose.
- **Dust / hit-spark effects** — 3-frame, sub-200 ms total.
  Disproportionate "alive" payoff.
- **Don't loop the loop** — pick the next idle from a weighted bag so
  the viewer never sees the same 10 s twice.

---

## 5. Aseprite / Pixelorama workflows

### [Aseprite](https://www.aseprite.org/)
Paid binary, source on GitHub. Industry default for pixel-art
animation. PNG + JSON sprite-sheet export, **per-frame durations
baked into the JSON**, headless CLI for asset pipelines:

```bash
aseprite -b sprite.aseprite --sheet out.png --data out.json
```

Frame durations come through directly — feed straight into the
animation table.

### [Pixelorama](https://pixelorama.org/)
Free, open-source, Godot-based. Exports frames as separate PNGs or a
single sheet; imports `.aseprite`. Good free fallback if a contributor
doesn't own Aseprite.

### Pipeline for this board

```
sprite.aseprite (artist's source)
        │
        ▼   aseprite -b … --sheet out.png --data out.json
sheet.png + sheet.json
        │
        ▼   small Python script
generated_sprites/foo.c {
    uint8_t  w, h, frame_count;
    const lv_img_dsc_t *frames[];
    const uint16_t durations_ms[];
}
        │
        ▼
linked into the app build
```

The durations array is the load-bearing piece — that's how you get
**uneven frame holds** (the #1 trick from §4) for free.

---

## 6. Checklist: bake these into the animation system

Things that ship "alive feel" if implemented; skip or shortcut at your
peril.

- [ ] Multi-clock idle scheduler — breath / blink / glance / bigIdle on
      separate timers, never synced.
- [ ] Per-frame duration baked in (not a single fps for the whole
      loop).
- [ ] Mood-conditioned tempo (same frames, different ms-per-frame).
- [ ] Weighted-bag picker for "big idle" actions — never repeat
      consecutively.
- [ ] Anticipation frame before deliberate actions.
- [ ] Squash-and-stretch on jump / land / eat.
- [ ] Secondary-motion lag on appendages.
- [ ] Day/night tint via RTC.
- [ ] Drifting ambient particles (bubbles or dust, parallax).
- [ ] Zone-based idle wander (Neko Atsume style).
- [ ] Touch reaction — pet looks at finger, brief surprise frame.

---

## Sources

- [Sprite-AI: 12 animation principles for pixel art](https://www.sprite-ai.art/guides/animation-principles)
- [Lospec idle tutorials index](https://lospec.com/pixel-art-tutorials/tags/idle)
- [Pedro Medeiros / Miniboss tutorials (Lospec)](https://lospec.com/pixel-art-tutorials/author/pedro-medeiros)
- [MortMort tutorials (Lospec)](https://lospec.com/pixel-art-tutorials/author/mortmort)
- [Tamagotchi Close Up (Fandom)](https://tamagotchi.fandom.com/wiki/Close_Up)
- [Tamagotchi Plus idle animations (Tumblr)](https://sa311.tumblr.com/post/666773897457401856/tamagotchi-connection-plus-series-idle)
- [TamaFi GitHub](https://github.com/cifertech/TamaFi)
- [ESP32-TamaPetchi GitHub](https://github.com/CyberXcyborg/ESP32-TamaPetchi)
- [TamagotchiESP32 (RBEGamer)](https://github.com/RBEGamer/TamagotchiESP32)
- [Tamagotchi emulator (anabolyc)](https://github.com/anabolyc/Tamagotchi)
- [Dream Aquarium](https://www.quantumenterprises.co.uk/aquarium/)
- [SereneScreen Marine Aquarium](https://serenescreen-marine-aquarium.en.softonic.com/)
- [Neko Atsume — TV Tropes](https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/NekoAtsume)
- [Aseprite exporting docs](https://www.aseprite.org/docs/exporting/)
- [Pixelorama](https://pixelorama.org/)
- [Pedro Medeiros animation tutorial overview (80.lv)](https://80.lv/articles/pixel-animation-tutorial-by-pedro-medeiros)
