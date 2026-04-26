# 17_pixelpet — Animation Rework Plan (v2)

> Replaces the procedural-LVGL-primitive renderer from v1 with a layered
> sprite-composition system backed by a flash-resident asset bundle, so the
> project can scale from one species + 30 animations to ~30 species and
> hundreds of animations without rebuilding firmware.
>
> Read alongside:
> - `IMPLEMENTATION_PLAN.md` — the v1 plan (still authoritative for save
>   format, RTC/persistence, stat engine, power management)
> - `research/lvgl_sprite_animation.md` — LVGL 9 indexed-format gotchas,
>   `lv_animimg`, asset pipeline, per-frame budget
> - `research/virtual_pet_animation_references.md` — idle taxonomy with
>   timings, "fishbowl chilling" checklist, reference projects
>
> Date: 2026-04-26

---

## Why a rework

The v1 renderer composes the pet from LVGL primitives — a circle body,
two eye objects, a mouth bar. It works, it ships, but:

- No frame-based animation; "alive" feel is limited to a sine bob and an
  occasional blink.
- Adding a new species means writing more procedural code, not dropping
  in art.
- Mood is reflected only in primitive shape changes (eye height, mouth
  curve) — no breathing, no secondary motion, no reaction frames.

v2 replaces the renderer wholesale with a sprite-driven system, while
**keeping the v1 stat engine, save format core, RTC, and audio** intact.

---

## Constraints (re-stated)

- **No SD card.** Vendor-confirmed (ticket #235269) that SD and the QSPI
  display cannot be active at the same time on this C6 board. All
  assets must live in internal flash.
- **No designer.** Asset authoring is fully on me (Claude). Three tiers:
  procedural via Python, palette-swap of curated CC0 base art, and
  optional AI-assisted concept work that gets quantized into the
  procedural pipeline.
- **16 MB flash hard ceiling.** After bootloader, partition table, NVS,
  and a 2 MB app, ~12 MB is realistically available for assets.
- **No PSRAM, ~290 KB free heap, single-core 160 MHz RISC-V.** Per-frame
  budget cap from the research doc: keep total animated pixels under
  ~25,000 px/frame; no I4 sprite above ~96×96.

---

## Architecture overview

```
┌─────────────────────────────────────────────────────────────┐
│                       app code                              │
│  pet_state ── species_id ── stat_engine                     │
│       │                                                     │
│       ▼                                                     │
│  pet_renderer (layered compositor)                          │
│       │                                                     │
│       ▼                                                     │
│  asset_loader.asset_get(name)                               │
│       │                                                     │
│       ▼                                                     │
│  esp_partition_mmap("assets")                               │
│       │                                                     │
│       ▼                                                     │
│  flash @ 0x10_0000 — 12 MB asset bundle                     │
│       │                                                     │
│       ▼                                                     │
│  TOC + palette pool + sprite frames + duration tables       │
└─────────────────────────────────────────────────────────────┘

build-time:
  tools/sprite_factory/sprites.yml
       │ build_assets.py (Python)
       ▼
  build/assets.bin
       │ esptool write_partition assets
       ▼
  flash partition "assets"
```

### Layered composition

The pet is rendered as **independently animated layers**, not pre-baked
full-pet frames. Pou / Tamagotchi / Neko Atsume all do this — it's how
"many species" stays cheap.

```
+ background scene             (368×448, day/night tint LUT)
+ accessory_back layer         (poop, food bowl, scenery)
+ pet body                     (procedural body or curated CC0, palette per species)
+ pet face overlay             (eyes + mouth from expression library)
+ accessory_front layer        (hat, hearts, "?", sweat drop)
+ particle layer               (bubbles, sparkles, Z's)
```

Each layer is its own `lv_animimg`. The renderer's job is to:
1. Resolve `(species_id, mood, stage)` → `(body_anim, face_anim, accessory_set)`
2. Swap the source array on the relevant `lv_animimg`s when state changes
3. Drive the multi-clock idle scheduler (breath / blink / glance / bigIdle)

---

## Asset bundle format

A single binary blob. Built offline by `build_assets.py`, written to the
`assets` flash partition, mmap'd at runtime.

```
struct asset_bundle_header {
    uint32_t magic;           // 'PXPA' = 0x50585041
    uint16_t version;         // bump when layout changes
    uint16_t flags;           // reserved (compression, signing, etc.)
    uint32_t toc_offset;      // bytes from header start
    uint32_t toc_count;       // number of TOC entries
    uint32_t palette_offset;  // start of palette pool
    uint32_t palette_count;
    uint32_t total_size;
    uint32_t crc32;           // of everything after this field
};

struct asset_toc_entry {
    char     name[32];        // null-terminated, '/' for namespacing
    uint16_t format;          // 0 = I4, 1 = RGB565+A8
    uint16_t flags;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint16_t palette_id;      // 0xFFFF = inline / RGB565
    uint32_t data_offset;     // bytes from bundle start
    uint32_t data_size;
    uint32_t durations_offset; // 0 = uniform fps in flags
};

struct palette_entry {
    uint8_t  argb[16][4];     // 16 ARGB8888 entries (LVGL 9 I4 format)
};
```

### Why this format

- **Single mmap:** `esp_partition_mmap` returns a CPU-addressable pointer.
  All sprite data is read directly from flash like RAM, no decompression,
  no copy. Frame data hands directly to LVGL via `lv_img_dsc_t.data`.
- **Versioned** so the build can break the format and the runtime detects
  it.
- **Palette pool deduplicated** so 30 species × 1 palette each is just
  ~2 KB of palette data total.
- **Lookup by name** via linear scan over TOC at boot (build a hash table
  if it ever gets slow — at ~500 entries, linear is fine).

### Sizing target

```
12 MB asset partition:
  ~5 base body rigs × 8 anims × 4–8 frames @ ~2 KB I4   ≈ 1 MB
  ~30 species palettes × 64 B                            ≈ 2 KB
  particles + food + medicine + toys + expressions      ≈ 200 KB
  7 backgrounds × 10 KB                                  ≈ 70 KB
  egg + lifecycle + accessories                          ≈ 50 KB
  growth headroom                                        ≈ 10 MB
```

---

## Sprite factory pipeline

`tools/sprite_factory/` — Python (Pillow + PyYAML) that turns declarative
sprite specs into a binary asset bundle.

### Inputs

```
tools/sprite_factory/
├── build_assets.py           ← entry point
├── primitives.py             ← shape primitives: circle, rect, oval, pixel, line
├── palettes/
│   ├── classic.yml           ← named 16-color palettes
│   ├── species.yml           ← per-species body palettes
│   └── food.yml
├── sprites/
│   ├── particles/
│   │   ├── bubble.yml
│   │   ├── heart.yml
│   │   └── sparkle.yml
│   ├── food/
│   │   ├── apple.yml
│   │   ├── bread.yml
│   │   └── ...
│   ├── medicine/
│   ├── expressions/          ← eye + mouth library
│   ├── bodies/               ← 5 base rigs
│   ├── backgrounds/
│   └── accessories/
└── curated/                  ← CC0 PNGs, processed but not generated
```

### YAML spec for a procedural sprite

```yaml
name: bubble_rise
palette: aqua
size: 8x8
frames: 6
durations_ms: [120, 120, 140, 160, 200, 100]   # uneven holds
animate:
  base:
    - circle: {center: [4, 4], radius: 2, color: cyan}
    - pixel:  {pos: [3, 3], color: white_alpha}
  per_frame:
    0: {dy:  0, scale: 1.00}
    1: {dy: -2, scale: 1.05}
    2: {dy: -4, scale: 1.00, wobble_x:  1}
    3: {dy: -6, scale: 1.00, wobble_x: -1}
    4: {dy: -8, scale: 0.95, alpha: 0.7}
    5: {dy: -10, alpha: 0.0}
```

### Outputs

- `build/assets.bin` — the final bundle, ready for `esptool write_partition`
- `build/sprites/<name>/<frame>.png` — debugging / visual review
- `build/asset_index.h` — small C header with name → ID enum so app code
  can use compile-time constants instead of string lookups for hot paths

### Build hook

```cmake
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/assets.bin
            ${CMAKE_BINARY_DIR}/asset_index.h
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/sprite_factory/build_assets.py
            --src ${CMAKE_SOURCE_DIR}/tools/sprite_factory
            --out-bin ${CMAKE_BINARY_DIR}/assets.bin
            --out-header ${CMAKE_BINARY_DIR}/asset_index.h
    DEPENDS ... # globs of yml files
)
```

A custom flash target (`idf.py flash-assets` or just an esptool call from
a script) flashes only the assets partition — sprite iteration loop is
**save PNG → make → flash-assets**, no firmware rebuild.

---

## Reusable asset library — what gets authored

| Bucket | Items | Total bytes (I4) |
|---|---|---|
| **Particles** | heart, sparkle, star, bubble, dust, Z, ?, !, sweat, tear, music, crumb | ~24 KB |
| **Food** | bread, apple, fish, cake, milk, carrot, candy, water, broccoli, bone, ice cream, soup | ~24 KB |
| **Medicine + clean** | pill, syringe, bandage, soap, brush, towel, shampoo | ~14 KB |
| **Toys** | ball, falling apple, bell, drum pads, fishing rod, kite, yarn | ~28 KB |
| **Expressions** | 8 eyes × 6 mouths = 48 expressions from 14 sprites | ~28 KB |
| **Lifecycle** | egg, gravestone, halo, angel wings, level-up burst | ~20 KB |
| **Backgrounds** | day fishbowl, night fishbowl, bedroom, park, bath, sky, forest | ~70 KB |
| **Body rigs** | 5 base rigs × 8 anims × ~6 frames | ~1 MB |

Reusable library total (excluding bodies): **~200 KB**. Used by every
species, every stage. One-time cost.

---

## Rig + species data model

```c
// One rig defines a complete animation set; multiple species can point at
// the same rig with different palettes.
typedef struct {
    const char *name;          // "blob", "puppy", "dragon", "fish", "slime"
    asset_id_t  body_idle;
    asset_id_t  body_walk;
    asset_id_t  body_eat;
    asset_id_t  body_sleep;
    asset_id_t  body_happy;
    asset_id_t  body_sad;
    asset_id_t  body_sick;
    asset_id_t  body_dance;
    uint8_t     face_anchor_x; // where to put the face overlay
    uint8_t     face_anchor_y;
    uint8_t     base_size;     // base sprite size (32, 48, 64)
} rig_def_t;

typedef struct {
    const char *name;          // "Pinkie", "Mochi", "Spike", ...
    uint8_t     rig_id;
    uint8_t     palette_id;    // points into the bundle's palette pool
    uint16_t    accessory_mask; // bitmask of allowed accessories
    int8_t      growth_curve;  // affects size at each stage
} species_def_t;
```

### Save format v2 migration

```c
// v1 layout already on disk:
//   { uint8_t version=1, pet_state_v1_t state }
// v2 layout:
//   { uint8_t version=2, pet_state_v2_t state }
// Migration:
//   if (version == 1) {
//       pet_state_v2_t v2 = migrate_v1_to_v2(v1);
//       v2.species_id = SPECIES_BLOB;  // default
//       commit;
//   }
```

`pet_state_t` gains:
```c
uint8_t species_id;
```

(That's it — single byte. Rigs and species defs themselves live in flash
read-only as `static const`.)

---

## Multi-clock idle scheduler

Four independent clocks, each driving its own `lv_animimg` source swap or
overlay trigger:

```
breath_clock     — always running  — 1.5–2.5 s, body squash 1 px
blink_clock      — gaussian gap    — 2.5–8 s avg, closed eye held 80–150 ms
glance_clock     — uniform gap     — 8–20 s, eye dart + 600–1200 ms hold
bigIdle_clock    — weighted bag    — 30–90 s, picks from {yawn, stretch, sniff, sit, scratch}
```

Mood-conditioned tempo — same frames, different ms-per-frame:

| Mood | breath period | bigIdle gap |
|---|---|---|
| HAPPY | 1.0 s | 30 s, picks bouncy actions |
| NEUTRAL | 1.5 s | 60 s |
| TIRED / SAD | 2.5 s | 90 s, picks droopy actions |
| SICK | irregular | rarely (only sway) |
| SLEEPING | 4 s, deep | suppressed |

The bigIdle "weighted bag" never repeats consecutively — every pick is
removed from the bag until the bag is empty, then refill.

---

## Fishbowl backdrop

- **Static background sprite** per scene (day/night fishbowl, bedroom, etc.).
  Selected by `pet_state.is_sleeping` and time-of-day from RTC.
- **Bubble emitter** — 5–10 simultaneous bubbles, sinusoidal x-wobble,
  3–8 s rise time, respawn at bottom. Each bubble is its own small
  `lv_obj` driven by an `lv_anim_t` (cheap because particles are tiny).
- **Day/night tint** — overlay layer with `LV_OPA_*` based on RTC hour;
  warm tint by day, cool tint by night.
- **Optional zone wandering** — pet teleports between fixed "spots"
  every 30–120 s and plays a spot-specific micro-loop. Defer to
  follow-up.

---

## Phase plan

Each phase = one commit, ends with a clean build and on-hardware sanity
check. Estimated commit counts in parentheses.

### R1 — plan doc *(this file, 1 commit)*

### R2 — asset bundle infrastructure (2 commits)
- Repartition: `partitions.csv` adds 12 MB `assets` data partition. App
  shrinks from 3 MB → 2 MB (current image is 645 KB, fits comfortably).
- `tools/sprite_factory/` skeleton — Python entry point, primitives.py
  with circle/rect/oval/pixel, single placeholder sprite (a bubble).
- `main/asset_loader.c/h` — partition mmap, TOC parse, `asset_get(name)`
  returning a runtime-built `lv_img_dsc_t*` cached per-name.
- `main.c` boot adds `asset_loader_init()` after `amoled_init`.
- Test: load + render the bubble sprite from the partition.

### R3 — sprite factory + reusable asset library (2 commits)
- Author all the procedural items in the table above (~200 KB).
- Add palette system (16-color named palettes).
- Author 5 base body rigs as procedural sprites with idle / happy / sad /
  eat / sleep / walk / dance / sick anims.
- Bundle build target wired into CMake.

### R4 — rig + species data model (1 commit)
- Add `rig_def_t`, `species_def_t`, hardcode 5 species (Blob in 5
  palettes for now).
- `pet_state_t.species_id` field, save format v2 migration.
- Default new pets to species 0.
- Per-species selection UI deferred to follow-up.

### R5 — layered composition renderer (2 commits)
- Replace `pet_renderer.c` with new layered version: body / face / accessory_back /
  accessory_front / particle.
- Each layer is `lv_animimg` (or static `lv_image` for non-anim layers).
- `pet_renderer_set_state` resolves species → rig → asset names → swaps
  source arrays.
- Smoke test: pet appears, idle anim plays.

### R6 — multi-clock idle scheduler (1 commit)
- Add `idle_scheduler.c/h` with breath / blink / glance / bigIdle clocks.
- Mood-conditioned tempo table.
- Weighted-bag picker.

### R7 — fishbowl backdrop (1 commit)
- Background scene asset, day/night tint LUT, bubble emitter.
- Optional zone-wander deferred.

### R8 — touch reactions + eat sequence + stage-up (1 commit)
- Touch hook: when an unhandled touch lands, pet looks at it (eye
  position → glance toward x,y) and shows a brief surprise frame.
- Eat sequence: when `CARE_FEED_*` fires, run `eat` body anim + spawn
  food sprite at mouth, then despawn.
- Stage-up: sparkle burst overlay + brief grow animation when stat_engine
  transitions a stage.

### R9 — polish (deferred to follow-up; not blocking the rework)

Total: ~10 commits across R1–R8.

---

## Out of scope for v2

- **OTA-able assets.** Possible (separate partition, custom OTA path) but
  not required to ship the rework. Defer.
- **Hand-drawn art.** When/if a designer joins, replace the procedural /
  CC0 base bodies in `tools/sprite_factory/sprites/bodies/` with their
  PNGs. Pipeline change: zero.
- **AI-generated species.** Tooling could land later as a separate
  `tools/ai_concept/` folder; out of the build pipeline.
- **Pack store / downloadable species.** Tier 3 from the scaling
  discussion. Far future.

---

## Decisions locked

| Decision | Choice |
|---|---|
| Asset format | I4 indexed everywhere (palette-swap-friendly, flash-dense) |
| Asset partition size | 12 MB |
| OTA-able assets | No (out of scope for v2) |
| Sprite spec format | YAML (Python build tool) |
| Composition | Layered: bg / accessory_back / body / face / accessory_front / particles |
| Rig + species split | Yes — `species_id` is a save-state field, rigs are static const |
| Idle scheduler | Multi-clock with mood-conditioned tempo + weighted bag |
| Fishbowl scene | Static bg + bubbles + day/night tint; zone-wander deferred |

---

## Open questions

- **Where to source 5 base body rigs.** Options: pure procedural shapes
  (works, looks geometric), curated CC0 from OpenGameArt (works, license
  needs vetting per pack), or a mix. Recommendation: procedural for
  Phase R3 to unblock the rest of the pipeline; swap to CC0 in a
  follow-up commit once the asset hot-swap workflow is proven.
- **CRC validation at boot.** Bundle has a CRC32 field. If it fails, do
  we panic, fall back to a built-in placeholder bundle, or run with no
  pet? Recommendation: log a loud warning + render a "asset bundle
  invalid, please reflash" message screen until reflashed.
