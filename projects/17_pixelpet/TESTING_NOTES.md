# PixelPet — pending hardware tests

The enrichment branch (`feat/17-pixelpet-enrichment`, PR #10) shipped
without hardware verification because the device wasn't available at
authoring time. Build is clean (`idf.py -C projects/17_pixelpet build`,
651 KB / 2 MB, 69% free). Below is what to actually check once the
board is back.

## R8.1 — no-op care reactions

- [ ] Tap **Medicine** with health > 70 → expect `?` particle above
      the pet, sad body anim one-shot, sad jingle. No health change.
- [ ] Tap **Meal** with hunger ≥ 95 → same `?` + sad reaction.
- [ ] Tap **Quick play** with energy < 10 → `zzz` particle + yawn anim.
- [ ] Tap **Wipe** with no poop → `sparkle` particle + happy anim, AND
      the screen should auto-pop back to STATUS so the FX is visible.
      (CLEAN-success keeps you on CLEAN; CLEAN-noop pops to STATUS.)
- [ ] Confirm `audio_jingles_play(JINGLE_SAD)` doesn't fight the eat
      jingle when meal is rejected (timing — both are short).

## R9 — onboarding + naming

- [ ] **Fresh save**: erase NVS (`idf.py erase-flash` then `flash`) →
      welcome card appears with "tap to begin".
- [ ] Tap welcome → species picker. Verify ◀/▶ cycles through all 5
      blob colours and the egg sprite + species display name update.
- [ ] Tap **PICK** → name picker. Verify all three ▲/▼ buttons cycle
      A→B→...→Z→space→A. Underscore shown for blank slots.
- [ ] Tap **HATCH** → JINGLE_HATCH plays, intro overlay disappears,
      egg sprite appears with chosen name on the status screen.
- [ ] Save persistence: power-cycle, confirm name + species survive
      and intro does NOT show again.
- [ ] Boot button BOOT (GPIO 9) is suppressed while intro is up
      (no screen nav while picking).
- [ ] **Touch hit-targets**: ▲/▼ buttons are 64×44 px. If they feel
      hard to hit, bump to 72×48 in `intro_screens.c::build_name`.

## R9 → R10 → R11 save migration chain

- [ ] **From v2 (current main)**: flash main, let pet age, switch to
      enrichment branch, flash again. Pet should load with
      `intro_done=true`, `name=""`, counters=0, quests unset. Check
      logs for `migrated v2 → v5 save`.
- [ ] **From v1**: harder to set up; trust the unit-of-code that's
      identical to the v2 path. Skip unless something breaks.
- [ ] First stat tick post-migration should fire `daily_quests_check_reset`
      and pop the **NEW DAY** card with three quest names.

## R10 — story cards + milestones

- [ ] **Stage transitions** at TIME_SCALE=360 (Kconfig default):
      - egg→BABY: ~5 s real time after intro (no card; intentional)
      - BABY→CHILD: ~60 s real → "<name> is now CHILD" card
      - CHILD→TEEN: ~3.6 min real → TEEN card
      - TEEN→ADULT: ~12 min real → form-reveal card
      - ADULT→SENIOR: ~28 min real → SENIOR card
- [ ] Tap dismisses; confirm BOOT button is suppressed while card is up.
- [ ] **Adult form** depends on care during teen stage. Quick checks:
      - High care + discipline > 60 → HAPPY
      - disc < 30 → NAUGHTY
      - energy < 40 OR care_score < 80 → LAZY
      - health < 50 → SICK
      Easiest verification: spam Scold to get NAUGHTY, neglect to get SICK.
- [ ] **Memorial**: kill the pet (let health hit 0 — neglect food + clean
      simultaneously). Memorial should show name, lifespan, and counter
      grid. "Hatch new pet" → goes back to ONBOARDING (because rebirth
      calls `pet_state_init_new` which clears `intro_done`). **VERIFY** —
      this is desirable IMO (each life starts with naming) but the user
      may prefer skipping intro on rebirth. If so, set `intro_done=true`
      in `rebirth_cb` after `pet_state_init_new`.

## R11 — discipline + daily quests

- [ ] Tap **Scold** on PLAY → JINGLE_DISCIPLINE plays, disc bar rises
      by 20, happy drops by 5. Visible on status bars.
- [ ] **Shake gesture**: physically shake the board. Expect one
      discipline event per shake with a 3 s cooldown. Watch serial for
      `shake → discipline (mag X.X)`. If too sensitive (every wiggle
      fires) raise `SHAKE_THRESHOLD` in `imu_manager.c` from 3.0 to 4.5.
      If too insensitive, lower to 2.0.
- [ ] Confirm shake is suppressed during: minigame, intro, story card,
      egg/dead stage.
- [ ] **Daily quest line** appears on status screen as yellow text
      ("today: feed 1/3"). Should rotate to the next unfinished quest
      after one completes.
- [ ] Complete a quest → JINGLE_HAPPY + green "QUEST DONE" card +
      happy bar jumps by 5.
- [ ] **Midnight reset** is the riskiest piece — uses `gmtime_r` so
      the boundary is UTC midnight. To force-test: set RTC to 23:59:50
      via `pcf85063_set_time`, wait, watch for NEW DAY card with three
      fresh quest names.

## Risks worth watching

1. **RTC unset on first boot** — `daily_quests_check_reset` uses
   `now_unix` for the day-bucket. If RTC reads 1970-01-01, the first
   roll happens immediately and subsequent days never roll until RTC
   gets set. Acceptable for now (the pet works without quests) but
   a future PR could gate on `rtc_manager_is_valid()`.

2. **Story card stacking** — `story_card_show` replaces the current
   card without queueing. If two transitions happen back-to-back in
   one stat tick (unlikely — stages are ≥5 minutes apart) the second
   wins. Not worth fixing unless seen.

3. **Quest reset card on rapid stage changes** — if the pet reaches
   ADULT and a midnight rollover happens in the same stat tick, two
   cards fire (adult-form, then NEW DAY). The second replaces the
   first. Same caveat as above.

4. **Name field UTF-8 / non-ASCII** — name picker only emits A-Z and
   space. Migrated v2 saves get `name=""`; the renderer treats both
   as "no name". Should be fine; flagged here for completeness.

5. **Heap headroom** — added ~3 lv_obj containers (intro root,
   story_card root, intro cards). At ~200 B per object, ~1 KB of
   permanent heap. Within the existing budget but watch for
   "free heap" log line dropping below ~150 KB.

## When the device is back

1. Flash, erase NVS, walk through onboarding end-to-end.
2. Run for ~30 minutes at TIME_SCALE=360 to see all stage cards.
3. Try each no-op care path on day 1.
4. Verify a real shake fires discipline.
5. Set RTC to 23:59:50 and watch the day rollover.
6. Kill the pet (`p->health = 0` via debug, or 30+ minutes of
   neglect) and check the memorial.

If everything passes, merge PR #10. Otherwise file findings here so
the follow-up branch can address them.
