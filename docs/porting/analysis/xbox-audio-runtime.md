# M16 audio: the shipped Xbox banks (`.xsb` + `.xwb`) played at run time

Running notebook for the audio part of milestone M16 ("original Xbox assets track", PLAN.md row M16, owner review of
2026-10-10). Branch `m16-audio`, worktree `../engine-uc2-m16-audio`, logs `<WorkUC2>/logs/m16`, run trees
`<WorkUC2>/run/m16*`. Appended after every step so a successor can continue.

Goal (owner): "being able to provide an XBOX disc and load everything from there... get the exe, drop the iso and
truck along." UC2ALAudio plays cooked data (`.uc2cues` + `.uc2wb` + `.ogp`, `Music/*.ogg`, made by
`tools/cook-audio.py`). M16 adds a second source: the retail disc's `Sounds/<Bank>.xsb` + `Sounds/<Bank>[_<lang>].xwb`
and `Sounds/M_<Song>.xwb`, read and decoded by the device itself. The cooked source stays the default where present.

## Step 0 - reading, and what the disc has (2026-10-10)

- Read: CLAUDE.md, PLAN.md (M16 row, owner review 2026-10-10), audio.md, audio-f6.md, `tools/uc2xwb.py`,
  `tools/uc2xsb.py`, the two verify tools, the device (`Source/UC2ALAudio`), `mkrun-uc2.sh`, `audio-regression.sh`,
  `run-uc2-audio.sh`.
- **The disc's `Sounds/`** (`<WorkUC2>/.cache/iso-list.txt`, the extract-xiso listing of the USA ISO): 263 files, the
  same names and byte sizes as `epic-sources/Sounds` (79 `.xsb`, 183 `.xwb`, and `Avaris.xap`, 6.6 MB, the XACT
  project itself, is on the disc too). Identity by name and size only (the ISO's `Sounds/` was never extracted).
- **The disc has no `Music/` folder.** Music is 21 streamed banks `Sounds/M_<Song>.xwb`, one entry each: 20 are Xbox
  ADPCM stereo 44.1 kHz; **`M_Adoratrice.xwb` is WMA** (the entry is a whole ASF file: GUID `3026b275...` at the data
  start). `epic-sources/Music/` holds the 23 source `.wav` + `.wma` (not on the disc). Adoratrice is the music of
  `CTF-SkyReach` and `DM-Meridian` (name in the map files).
- Formats of all 183 `.xwb` (`tools/uc2xwb.py list`): 7,181 Xbox ADPCM entries, 30 PCM16, 1 WMA.

## Step 1 - the rest of the `.xsb` format, decoded against the project (2026-10-10)

F-6's `tools/uc2xsb.py` read 2,064 "plain" sounds; the device needs all 2,459 (2,428 project + 31 `A_Vehicle`).
Method as F-6: read the shipped `.xsb` next to the parsed `Avaris.xap` (the cook's own parser) and the cooked cue
tables, find each field. Scratch scripts in the session scratchpad (`re/ex*.py`); the result is
`tools/uc2xsb.py read_xsb_full` (layout in its module comment) and `tools/dev/audio-xsb-verify.py --full`.

What was found (each confirmed over every instance in the data; counts are the project's):
- **Event header** u32 = type (low byte) | timestamp ms << 8 (`ST_FireLoop`'s play at 300 ms: `00 2c 01 00`).
  Events are stored in the project's order. Types: 0 play (12 bytes), 1 play with pitch/volume variation (24),
  4 set pitch / 5 set volume (16), 7 low-pass (20), 8 pitch LFO (12), 9 multi-function LFO (16), 11 envelope (24),
  12 loop start (8), 15 play without a wave (8; only `Water_Dripping01`), 16 mix bins (8 + its byte count).
- **Play flags** u16: 0x4 one wave (u32 = entry | bank << 16), 0x400 the u32 is a variation table offset, 0x10 the
  24-byte form, 0x4000 new variation on loop (5 events, all `newvar=1`). Type 1 adds s16 pitch low/high (4096/1200
  units, as the sound pitch), s16 volume low/high (mB, as typed), u16 variable delay (ms; 5 events with 100..20000);
  its last u16 is uninitialised (217, 32, 29814, 59136 seen).
- **Variation table**: u16 count | playlist type << 13 (types 0, 1, 4 in the data: 91, 1, 43 tables), u16 2*count,
  then 8 bytes per wave: u16 entry, u16 bank index, u16 low, u16 high = cumulative ranges over 0..10000. Equal
  weights give equal ranges, the last one taking the rounding remainder (6 waves: 1666 x 5 + 1670).
- **Set volume / set pitch**: u8 8, u8 mode (0x10 set: 75, |0x04 random: 20, |0x20 fade: 70), u16 steps, s16 low,
  s16 high, u8 relative, u16 duration at +13 (unaligned). Pitch values in 4096/1200 units, volume in mB.
- **Sound flag 0x10**: no track table; +0 is a variation table (one play event at time 0, 27 sounds).
- **Sound +4 is two fields**: bits 0-8 = attenuation / 16 mB (F-6's `floor((-8192 - v)/16)` is the same number
  sign-extended), bits 9-15 = LFE level / -50 mB, 0x7F when the sound has none. The 5 sounds F-6 listed as
  "overflowed volumes (garbage)" are exactly the 5 sounds with an LFE level (-3000 -> 60, -4000 -> 80); read this way
  their volume is the project's (-200 mB). Not an Epic bug.
- **I3DL2**: byte +15 is 1 on all 734 sounds with an I3DL2 level; the level is stored nowhere (searched: no -300 or
  -333 as s16 or float in the bank). The reader takes -300 mB (733 of 734); `A_Character\JG_Gorge_Death` has -333.
- **RPC area** (offset at 0x14, count = the u16 at 0x20, F-6's "?"): 24-byte records u16 sound, u8 track, u8 1,
  u32 curve, char name[16]; curve = 4 bytes 0, 4 points (u16 x, s16 y), 4 bytes 0. All 33 curves equal the project.
- **EQ** (sound flag 4, 15 sounds): +16 holds the EQ parameters (not read: the device does not play EQ).
- **Cue flag 1** = queue (the 3 `A_Turmoil` thunder cues). Every cue has one sound or none (3), no cue variation.

Measured (`logs/m16/xsb-full-verify-1.out`; `tools/dev/audio-xsb-verify.py --full` against `assets/uc2-cooked`):
```
banks 79, sounds 2459, cues 2469 compared (cooked / read from the .xsb)
DIFF play.weights: 4
DIFF sound.i3dl2: 1
DIFF sound.length (ms, xsb - cooked = +1): 448
DIFF sound.length (ms, xsb - cooked = +2): 520
DIFF sound.length (ms, xsb - cooked = +3): 231
DIFF sound.length (ms, xsb - cooked = +4): 1
DIFF sound.length (ms, xsb - cooked = +5): 2
DIFF sound.name: 9
DIFF sound.volume: 25
```
The differences, all explained:
- `play.weights` 4: two tables with equal explicit weights (`NCSW_HeavyAttack` 4000 x 3, `JG_Swing` 2000 x 3) read as
  "unset" (equal ranges; the device draws them the same), `NCSTF_JumpChargeAttack` (one wave, weight 6000: no
  choice), `NK_FreezeImpactPlayer` 9878:4146 read as 7043:2957 (the same ratio to 1/10000: the bank keeps only the
  normalised ranges).
- `sound.i3dl2` 1: `JG_Gorge_Death` -333 -> -300 (the level is not in the bank; 0.33 dB of reverb send).
- `sound.length` 1,202: the cue length (`GetSoundCueDuration`) from the shipped waves is 1-5 ms longer: Xbox ADPCM
  pads every wave to a whole 64-frame block (<= 2.9 ms at 22 kHz per pass; 4-5 ms on looped events).
- `sound.name` 9: sound names are not in the bank; a sound is named after its first cue (`Bank\SoundN` without one).
  Only logs show them.
- `sound.volume` 25: the 25 jump/dodge sounds of `A_Character` typed -666 mB (+ group -400): the bank keeps 16 mB
  steps, the reader's round-number guess gives -1060 for -1066 (0.06 dB).
Nothing else differs: every cue, track, event, wave reference, loop count, delay, playlist type, 3D distance,
rolloff, priority, pitch, category, headroom boost, RPC curve.
