# Nursery Rhyme Score References

Reference sheet music sources for all 20 songs in the nursery rhyme player.
Use these to verify and correct note transcriptions in `projects/15_nursery_rhymes/main/song_data.c`.

## Verification Status

| # | Song | Rhythm Check | Verified Against Source |
|---|------|-------------|----------------------|
| 1 | Twinkle Twinkle | PASS (12 bars 4/4) | Standard ABC notation |
| 2 | Mary Had a Lamb | PASS (8 bars 4/4) | Standard ABC notation |
| 3 | Baa Baa Black Sheep | **FIXED** — had 8.5 bars | 8notes.com, musicnotes.com |
| 4 | London Bridge | PASS (8 bars 4/4) | Standard ABC notation |
| 5 | Row Your Boat | PASS (8 bars 6/8) | Standard ABC notation |
| 6 | Jack and Jill | PASS (6 bars 6/8) | Standard ABC notation |
| 7 | Humpty Dumpty | PASS (15 bars 3/4) | Standard ABC notation |
| 8 | Itsy Bitsy Spider | PASS (16 bars 6/8) | Standard ABC notation |
| 9 | Old MacDonald | **FIXED** — had 15.75 bars | 8notes.com, musicnotes.com |
| 10 | Hickory Dickory | **FIXED** — had 7.5 bars | musicnotes.com, makingmusicfun.net |
| 11 | Three Blind Mice | **FIXED** — had 14.83 bars | bethsnotesplus.com, 8notes.com |
| 12 | Hot Cross Buns | PASS (4 bars 4/4) | Standard ABC notation |
| 13 | Frere Jacques | PASS (8 bars 4/4) | Standard ABC notation |
| 14 | Yankee Doodle | PASS (9 bars 4/4) | Standard ABC notation |
| 15 | Pop Goes the Weasel | PASS (9 bars 6/8) | Standard ABC notation |
| 16 | Rock-a-Bye Baby | PASS (10 bars 3/4) | Standard ABC notation |
| 17 | Hush Little Baby | **FIXED** — had 6.5 bars | 8notes.com, makingmusicfun.net |
| 18 | Wheels on the Bus | PASS (8 bars 4/4) | Standard ABC notation |
| 19 | Head Shoulders | **FIXED** — had 8.5 bars | singing-bell.com, 8notes.com |
| 20 | Happy & You Know It | **FIXED** — had 8.25 bars | synthesizernotes.com, 8notes.com |

## Sheet Music Source URLs

### Free Lead Sheets & Easy Piano
- [8notes.com Piano](https://www.8notes.com/scores/piano/) — Free arrangements, many in C major
- [MakingMusicFun.net](https://makingmusicfun.net/htm/f_printit_free_printable_sheet_music.htm) — Free printable easy piano
- [Michael Kravchuk Lead Sheets](https://michaelkravchuk.com/) — Free lead sheets in multiple keys
- [Beth's Notes Plus](https://www.bethsnotesplus.com/) — Solfege + letter notes + analysis
- [Piano Song Download](https://pianosongdownload.com/) — Free lead sheets with chords

### MuseScore (Free Community Scores)
- [Twinkle Twinkle](https://musescore.com/song/twinkle_twinkle_little_star-307756)
- [Old MacDonald](https://musescore.com/song/old_macdonald_had_a_farm-2266340)
- [Three Blind Mice](https://musescore.com/user/498481/scores/6487012)
- [Hush Little Baby](https://musescore.com/user/498481/scores/6418651)
- [Head Shoulders](https://musescore.com/user/29277646/scores/8757849)
- [Happy & You Know It](https://musescore.com/user/29610820/scores/5246331)

### MusicNotes (Paid, Definitive)
- [Baa Baa Black Sheep C Major](https://www.musicnotes.com/sheetmusic/mtd.asp?ppn=MN0127891)
- [Old MacDonald C Major](https://www.musicnotes.com/sheetmusic/traditional/old-macdonald-had-a-farm/MN0127906)
- [Hickory Dickory C Major](https://www.musicnotes.com/sheetmusic/mtd.asp?ppn=MN0127897)
- [Three Blind Mice C Major](https://www.musicnotes.com/sheetmusic/mtd.asp?ppn=MN0082804)

### Letter Note Sites (For Quick Reference)
- [NoobnNotes.net](https://noobnotes.net/) — Letter-based notation for beginners
- [The Piano Notes](https://www.thepianonotes.com/) — Letter notes with octave markers
- [Synthesizer Notes](https://www.synthesizernotes.com/) — Letter notes for various instruments

## Music Theory Notes

### Duration Encoding (at 120 BPM)
| Symbol | Name | Beats | Ms |
|--------|------|-------|-----|
| W | Whole | 4 | 2000 |
| DH | Dotted Half | 3 | 1500 |
| H | Half | 2 | 1000 |
| DQ | Dotted Quarter | 1.5 | 750 |
| Q | Quarter | 1 | 500 |
| DE | Dotted Eighth | 0.75 | 375 |
| E | Eighth | 0.5 | 250 |
| S | Sixteenth | 0.25 | 125 |

### Measure Duration Checks
- **4/4 time**: Each measure = 4 beats = 2000ms
- **3/4 time**: Each measure = 3 beats = 1500ms
- **6/8 time**: Each measure = 2 dotted quarters = 1500ms
- **Pickup notes** (anacrusis): First measure may be incomplete — the missing beats appear at the end of the last measure

### Common Transcription Pitfalls
1. **Pickup beats**: Many songs start with an anacrusis (e.g., "If YOU'RE happy" — "If" is a pickup eighth note before beat 1)
2. **Tied notes across barlines**: A half note at the end of one bar may tie into the next
3. **6/8 vs 3/4**: Both have 1500ms per measure but different groupings. 6/8 = two groups of 3 eighths. 3/4 = three quarter beats
4. **Phrase length != bar length**: A lyric phrase may span 1.5 or 2.5 bars — that's fine as long as the total song duration is a whole number of measures
5. **Rests at phrase boundaries**: Missing rests between phrases cause measure count errors

### Key of C Major — MIDI Note Reference
| Note | MIDI | Hz (approx) |
|------|------|-------------|
| G3 | 55 | 196 |
| A3 | 57 | 220 |
| B3 | 59 | 247 |
| C4 | 60 | 262 |
| D4 | 62 | 294 |
| E4 | 64 | 330 |
| F4 | 65 | 349 |
| G4 | 67 | 392 |
| A4 | 69 | 440 |
| B4 | 71 | 494 |
| C5 | 72 | 523 |
| D5 | 74 | 587 |
| E5 | 76 | 659 |
| F5 | 77 | 698 |
| G5 | 79 | 784 |

*Note: All melodies are transposed +12 semitones at playback (TRANSPOSE_SEMITONES in audio_player.c)*
