# Audio F-6 (2026-10-09): the 19 waves from the `.xwb`, `A_Vehicle`, cue tables inside the Ogg (spike), HRTF

Branch `f6-audio` (worktree `engine-uc2-f6-audio`, base `1372dec`). Run trees `run/f6*`, logs `logs/f6/`, cook
output `logs/f6/cooked` (the coordinator installs it into `assets/uc2-cooked` after review). Brief: PLAN.md "Owner
review of 2026-10-09", F-6. Owner's words: "19 wave entries have no source WAV. We can convert them from the xwb
file" / "A_Vehicle.xsb/.xwb lets cook them in case we can use them in mods." / cue tables inside the Ogg "is a
exploration" / "No HRTF. OpenAL soft comes with this, can't we implement something similar based on how UT2004 or
UT99 work?"

Steps (each a commit and an entry here): 1 xwb decode + cook takes them, 2 A_Vehicle, 3 cue tables in the Ogg
(spike), 4 HRTF ini key, 5 regression.

## Step 0 - reading (2026-10-09)

- Base `1372dec` lacks F-4's `WaveFile=`/`UseEFX=` ini keys (branch `f4-platform`); not redone here.

## Step 1 - the 19 waves decoded from the shipped `.xwb` (2026-10-09)

### The reader: `tools/uc2xwb.py` (standard library only)

Format from the XDK (`reference/xdk/unpacked/2005-03-5849.17/XDK/xbox/include/xactwb.h`, version 3): header
`WBND` + version + 4 segment regions (bank data, entry meta-data, entry names, wave data); bank data = flags,
count, 16-byte name, meta-data element size, name element size, alignment, compact format; entries either full
(24 bytes: flags, mini format, play region, loop region; offsets relative to the wave-data segment) or compact
(4 bytes: offset:21 in alignment units, length deviation:11; length = next offset - this - deviation; the bank's
one format). Mini format: tag:2 (0 PCM, 1 Xbox ADPCM, 2 WMA), channels:3, rate:26, bits:1.

UC2's 183 shipped `.xwb` (measured, `tools/uc2xwb.py list`): all version 3, all flagged streaming (0x1); 178 compact
(124 with 2048-byte alignment, 54 with 512), 5 full (`A_ChemicalDawn`, `A_Gauntlet`, `A_Praxis`, `A_Tempest`, `A_Weapon`: 24-byte entries,
512 alignment, loop regions in ADPCM bytes, entry flags 0x10002 = ADPCM filter + loop cache); no entry names in any
bank. The 19 entries are all Xbox ADPCM (the 16 effects mono 22,050 Hz; the 3 Spanish announcer lines stereo
22,000 Hz). The localised banks carry their base bank's name (`A_Announcer_spa.xwb` says `A_Announcer`).

**Xbox ADPCM block layout, decided by measurement, not by documentation.** 36 bytes per channel per block (4-byte
header: s16 sample, u8 step index, u8 0; 32 bytes of codes, low nibble first; channels interleaved in 4-byte words).
Three readings tried on `A_Combo` entry 22 (`Combo_Pounce_Travel`, which has its source WAV), SNR against the source
in the first and the last quarter (`scratchpad/f6/variant.py`):

| reading | frames/block | first quarter | last quarter |
|---|---|---|---|
| A: header sample + the first 63 codes (the 64th predicts the next block) | 64 | 17.73 dB @ shift 0 | 16.67 dB @ 0 |
| B: 64 codes, header not output | 64 | 14.93 dB @ -1 | 13.87 dB @ -1 |
| C: header + 64 codes (ffmpeg's `adpcm_ima_wav` reading of tag 0x69) | 65 | -3.81 dB | -2.62 dB |

A is the format (64 frames per block is also the XDK's and the cook's unit). The standard IMA shift-and-add step
gives 16.67 dB in the last quarter against 16.59 for the exact `(2c+1)*step/8` product: kept the standard one.
(audio.md says the Xbox SDR figures were measured through ffmpeg wrapping the data as tag 0x69; reading C drifts by
a frame per block, so those figures must have come from another path; not re-examined here.)

**Verification over every entry that has a source** (`tools/dev/audio-xwb-verify.py`, `logs/f6/xwb-verify-1.out`),
measured, the 11 banks that hold the 19:
```
A_Combo: 102 entries compared; SNR min 14.0 median 24.9 max 52.9 dB; decode - source 0..63 frames; samples at full scale: decode 5002, source 6742
A_Gameplay: 43 entries compared; SNR min 14.1 median 24.0 max 49.0 dB; decode - source 0..63 frames; samples at full scale: decode 68, source 22
A_Weapon: 165 entries compared; SNR min 12.1 median 20.0 max 99.0 dB; decode - source 0..63 frames; samples at full scale: decode 908, source 1132
A_WeaponRace: 78 entries compared; SNR min 14.2 median 22.8 max 99.0 dB; decode - source 0..63 frames; samples at full scale: decode 611, source 862
A_Emperor: 18 entries compared; SNR min 22.5 median 44.4 max 57.8 dB; decode - source 0..62 frames; samples at full scale: decode 1, source 0
A_Gauntlet: 15 entries compared; SNR min 18.4 median 36.5 max 51.5 dB; decode - source 0..56 frames; samples at full scale: decode 17, source 27
A_SkyReach: 10 entries compared; SNR min 10.6 median 22.3 max 57.8 dB; decode - source 0..59 frames; samples at full scale: decode 1, source 0
A_Sever: 15 entries compared; SNR min 16.8 median 39.2 max 50.5 dB; decode - source 0..58 frames; samples at full scale: decode 1, source 2
A_SolarFlare: 10 entries compared; SNR min 15.2 median 38.5 max 47.2 dB; decode - source 0..60 frames; samples at full scale: decode 0, source 0
A_Tempest: 11 entries compared; SNR min 16.8 median 36.9 max 53.1 dB; decode - source 0..46 frames; samples at full scale: decode 1, source 0
A_Announcer_spa: 176 entries compared; SNR min 21.1 median 32.0 max 41.0 dB; decode - source 0..63 frames; samples at full scale: decode 6801, source 8727
compared 643 entries, 0 outside (length 0..63 frames longer, SNR >= 8 dB)
```
So: every decode is its source rounded up to a whole block, in phase (shift 0), at the SNR an IMA ADPCM coder gets
(weapons ~20 dB, speech ~32, ambience ~40). "Clipping": the decoder clamps to the s16 range like any IMA decoder;
over the 643 entries it produced *fewer* full-scale samples than the sources hold (the effects and the announcer
are mastered to full scale), so the full-scale samples in the 19 (below) are the material's, not the decoder's.

### What the 19 are (measured, from the cook's report `logs/f6/cooked/Sounds/cook-report.txt`)

| bank entry | name | format | decoded / kept frames | peak, full-scale samples, RMS |
|---|---|---|---|---|
| A_Combo 90 | Combo_Pounce_Travel2 | ADPCM 1ch 22050 | 47296 / 47280 | 25898, 0, -18.5 dBFS |
| A_Combo 91 | Combo_Pounce_Travel3 | ADPCM 1ch 22050 | 47296 / 47280 | 23630, 0, -20.0 |
| A_Combo 94 | Combo_BladeRush_Travel2 | ADPCM 1ch 22050 | 35200 / 35200 | 31728, 0, -11.0 |
| A_Combo 102 | Combo_SentinelFire | ADPCM 1ch 22050 | 15232 / 15190 | 32768, 55, -7.5 |
| A_Gameplay 40 | Gib_Liandri01 | ADPCM 1ch 22050 | 60736 / 60728 | 32768, 44, -11.9 |
| A_Gameplay 41 | Gib_Liandri02 | ADPCM 1ch 22050 | 49280 / 49224 | 32768, 31, -12.3 |
| A_Gameplay 42 | NewGib4 | ADPCM 1ch 22050 | 36608 / 36567 | 32768, 3, -15.6 |
| A_Weapon 163 | ST_Primary | ADPCM 1ch 22050 | 31296 / 31296 | 32768, 260, -7.4 |
| A_WeaponRace 78 | LI_RivetAlt | ADPCM 1ch 22050 | 29248 / 29248 | 32768, 209, -10.1 |
| A_Emperor 13, A_Sever 15, A_SolarFlare 4, A_Tempest 7 | Amb_Texture_13(mono) | ADPCM 1ch 22050 | 533440 / 533411 | 8411, 0, -26.8 |
| A_Gauntlet 0, A_SkyReach 3 | Amb_space_ext_loop11(mono) | ADPCM 1ch 22050 | 160768 / 160768 | 10582, 0, -22.9 |
| A_Tempest 8 | amb_texture_22b | ADPCM 1ch 22050 | 209728 / 209673 | 32690, 0, -9.0 |
| A_Announcer_spa 72 | Congratulations | ADPCM 2ch 22000 | 31040 / 31004 | 32768, 77, -10.0 |
| A_Announcer_spa 94, 96 | FinishHer, FinishIt | ADPCM 2ch 22000 | 36864 / 36857 | 32768, 48, -11.9 |

"Kept" = the project's play region (`Play Region Length` / 2 / channels: the source's own length, which ADPCM rounds up
to a block); every kept length is within one block of the bank entry's length (the gate's "length matches the bank
entry"). Finding: **the shipped Spanish `FinishHer` and `FinishIt` are byte-identical to `FinishHim` (entry 95, which
has a source)**: the Xbox said the same line for all three in Spanish; kept as shipped. The four `Amb_Texture_13` and
the two `loop11` entries are byte-identical to each other in their banks too. Spectrogram (seen only):
`logs/f6/spectrogram-Combo_Pounce_Travel2-vs-Travel.png` (top: the decoded `Travel2`; bottom: its sibling `Travel`
from the source WAV): the same whoosh shape, the ADPCM noise floor visible above ~8 kHz.

### The cook takes them (`tools/cook-audio.py`)

When an entry's WAV is not in the tree and the bank's `.xwb` ships (not for music), the cook decodes the entry to
`<out>/.cache/xwb/<bank>.<index>.wav`, cut to the project's play region, and encodes that like any source (the
encode cache keys on its SHA-1). The report says `XWB-SOURCE <bank> entry <i> <name>: no WAV in the project, decoded
from the shipped bank: ...`, and the index line says `src=xwb:<bank>.xwb:<index>` (the runtime ignores `src=`).
`--no-xwb-sources` gives the old cook. The cook's own `.xwb` cross-check reader now uses `uc2xwb.read_xwb`.
Loop regions come from the project (`Loop Region Offset/Length`, 16-bit source bytes), as for every other wave.

Run (`logs/f6/cook-1.out`; the installed `.cache` copied first, so only the 19 were encoded):
```
cp -r assets/uc2-cooked/.cache logs/f6/cooked/
bin/capped 4G 3600 -- python3 tools/cook-audio.py --jobs 2 --out /home/ion/WorkUC2/logs/f6/cooked     # exit 0
wave entries: 7181 in 182 banks, 0 without a usable source file, 19 decoded from the shipped .xwb
cues resolved (base language): 2434 fully, 0 partly, 4 with no playable wave
xwb cross-check: 181 of 182 project wave banks agree with the shipped .xwb ... (the Machine_Generator03 one, as before)
encoded: 6796 sources (16 now, 6780 from the cache), 0 failures
cooked: 182 wave banks (123.0 MB of sound packs), 23 music tracks (65.4 MB), 78 cue tables
```
The 4 silent cues left are the ones with no wave at all in the project (`ST_Primary`, `JG_JumpChargeEthereal`,
`LI_Impact`, `Water_Dripping01`: 0 variations). 16 encodes, not 19: three pairs of the 19 are identical decodes.

### Diff against the installed cook (measured; `logs/f6/cook-diff-*.txt`)

- File list identical (161 `.ogp`, 182 `.uc2wb`, 78 `.uc2cues`, music untouched). 26 files differ: 13 `.ogp` and
  13 `.uc2wb` of the banks that hold the 19 (packs +841,182 bytes in all), and 4 `.uc2cues`.
- `.uc2wb`, ignoring `offset=` (later waves move in the pack): exactly 19 lines differ, each `missing=no-file` ->
  `offset=... frames=... loop=... src=xwb:...`.
- `.uc2cues`: exactly 7 lines differ, all `length=` of a sound that references one of the 19 (`Combo_Pounce_Travel`
  0 -> 2144 ms, `Combo_BladeRush_Travel` 0 -> 1596, `Combo_SelfDestruct_Explode` 2123 -> 3254, `Gib_Explosion_LI` 0 ->
  2754, `CDG_Anubis`/`CDG_Apophis` 5092 -> 5144, `LI_Fire` 1056 -> 1326). 20 sounds reference the 19; the other 13 loop
  forever (length -1) or take their length from the base-language bank. No cue, event or wave reference changed.

### Found on the way (not changed; for the owner)

- **Priority of the 674 sounds without a `Priority` key.** The shipped `.xsb` stores 255 for every one of them (and the
  project's value for the other 1,754: 40, 50, 100, 150, 180, 200, 225, 240-254, all equal). The cook writes 0, and
  UC2ALAudio treats lower as more important: those 674 sounds (FX 284 with no group, Ambience 243, Voice 82, Machines 52, other 13) win every
  voice-stealing contest instead of losing it. Matters only when all 64 voices are busy. A one-line cook fix
  (`priority` default 255) that changes 674 lines of the cue tables; not done here (the step's diff must stay at the
  19). Evidence: `scratchpad/f6/xsbstat.py`, byte 14 of the 20-byte `.xsb` sound record.
- `A_ChemicalDawn` entry 7 (`Machine_Generator03`, 55,808 frames in the tree, 193,472 in the shipped bank) could now be
  taken from the `.xwb` as well; not done (it has a source, and the owner's decision named the 19).

## Step 2 - `A_Vehicle` cooked from the shipped `.xsb`/`.xwb` (2026-10-09)

**What references it (measured, `grep -arli` over `epic-sources`):** nothing that plays it. `A_Vehicle` appears only in
`XboxDrv/Inc/XboxFileSizes.h:1527,1627` (the Xbox loader's file-size table, compiled into `System/default.xbe` and
`defaultLL.xbe`), the design spreadsheets `Help/Design/OLDSoundList.xls` and `SoundData/sound list.xls`, and the two
bank files. No `.uc`, `.u`, `.ini` or map names a cue of it. The vehicle itself is cut content: the "Shenu cycle"
mesh ships (`Animations/K_Vehicles.ukx`, `XboxAnimations/K_Vehicles.ukx`, material `Engine/Classes/PSShenuSkin.uc`),
and `UCPawn.uc:4393` mentions "teleport in an object (Shenu)". The project (`Avaris.xap`) has no `A_Vehicle` block,
but `SoundData/A_Vehicle/` has all 31 source WAVs.

**The `.xsb` had to be read.** The XDK documents no sound-bank layout (only the runtime API in `xact.h`). New
`tools/uc2xsb.py` reads the plain part of format 11 (layout in its module comment), decoded by comparing the 78
project banks' `.xsb` with `Avaris.xap` (`scratchpad/f6/xsbcorr.py`, `xsbstat.py`): 20-byte cue records, 20-byte sound
records (volume `floor((-8192 - (sound+group mB))/16)`, pitch `trunc(cents*4096/1200)`, track count, layer, category,
flags 1 3D / 2 headroom boost / 4 EQ / 8 one wave / 0x10 variation table, 3D set index, priority, I3DL2 flag, RPC),
track tables (count | offset << 8), the 12-byte plain play event (header 0, flags 4, loop count, wave, bank) and the
40-byte 3D records. Anything else (variation tables, timed events, set-volume/pitch events, EQ, RPC) raises
`XsbUnsupported`, so a bank is cooked faithfully or not at all.

**Checked against the project** (`tools/dev/audio-xsb-verify.py`, `logs/f6/xsb-verify-1.out`), measured:
```
sounds read: 2064, equal to the project in every field read: 2059; volume guess = the project's sound + group volume: 2059 of 2064
fields that differ: {'volume-interval': 5}
sounds not read (outside the reader's shape): 364: {...variation tables, timed events, set volume/pitch, EQ...}
plain cues read: 2432, same name and sound as the project: 2432
```
The 5 are `LowThrobLoop`/`FusionReactorLoop(2)` in `A_Axon`, `A_ColdFusion`, `A_Fury`, whose stored volume is garbage
(30732, -24564: an overflow in Epic's tool), not a reader error. The volume is stored at 1/16 of its precision; the
reader returns the round value inside the stored interval (a multiple of 100, else 50, else 10), which equals the
project's value on 2,059 of 2,059 readable sounds. Priority: stored as is; the project's "no Priority" is 255 here
(see step 1, "found on the way").

**Cooking.** The cook (`shipped_only_banks`, default on, `--no-shipped-only` to skip) reads every shipped `.xsb` that
the project lacks, turns it into the project's own blocks (`Wave Bank` with entry names from the first cue that plays
each entry and `File = ..\SoundData\<bank>\<leaf>.wav`; `Sound Bank` with sounds, 3D blocks, tracks, play events,
cues) and appends them to the parsed project, so everything after (source lookup, xwb fallback, encode, cue length,
the `.xsb`/`.xwb` cross-checks, writers) is the common path. For such a bank, each WAV found by name is compared with
the shipped wave (decoded): length and SNR; a mismatch would make the cook use the shipped wave instead. Group volume
is baked into the sound's volume (`group=-`), as the binary stores the sum.

Result (`logs/f6/cook-2.out`), measured:
```
SHIPPED-ONLY A_Vehicle.xsb: not in the project; read from the binary: 31 sounds, 31 cues, wave bank A_Vehicle with 31 entries
SHIPPED-ONLY A_Vehicle.xwb entry 0 A_Vehicle\Shenu_Crash: source SoundData/A_Vehicle/Shenu_Crash.wav, SNR 20.4 dB against the shipped wave, shipped - source = 51 frames
... all 31 match: SNR 19.0..51.2 dB, shipped - source 0..63 frames (one ADPCM block)
sound banks: 79, sounds: 2459, cues: 2469
cues resolved (base language): 2465 fully, 0 partly, 4 with no playable wave
xsb cross-check: 79 of 79 project sound banks agree ...      xwb cross-check: 182 of 183 ... (Machine_Generator03 as before)
encoded: 6827 sources (29 now, 6798 from the cache), 0 failures
```
**A_Vehicle numbers:** 31 cues, 31 sounds (one cue -> one sound -> one wave each), 31 waves (29 distinct sources:
`Shenu_ShenuDying` = `Shenu_ShotDying`, `Shenu_ShenuHurt` = `Shenu_ShotHurt`), all 3D (min 250, max 100,000,
rolloff 0.25, cones off), category FX, priority 255, volume -500 mB (the 16 `Shenu_*` except `ShotDestroyed` -600)
and -800 mB (the 15 `Vehicle_Traffic_Pass*`), two infinite loops (`Shenu_EngineIdle`, `Shenu_EngineMove`, whole
wave). Files: `A_Vehicle.ogp` 483,003 bytes, `A_Vehicle.uc2cues` 15,926, `A_Vehicle.uc2wb` 4,555 (shipped: `.xwb`
919,552 + `.xsb` 2,296). Diff against the installed cook: the 26 files of step 1 plus these 3 new files, nothing else.

Cue names: `A_Vehicle.Shenu_<Crash|CrashHard|CrashShenu|EngineIdle|EngineMove|EngineStart|EngineStop|Ground|GroundHard|
ShenuDying|ShenuHurt|Shot|ShotDestroyed|ShotDying|ShotHurt|TurboBoost>` and `A_Vehicle.Vehicle_Traffic_Pass<01-08,11-17>`:
a mod plays them with `PlaySoundCue(Actor, 'A_Vehicle.Shenu_EngineIdle', ...)` like any other cue. Not in the
frontend's `targets/uc2/file-manifest.json` (frontend work is parked), so the web build does not fetch them yet.

## Step 3 - cue tables inside the Ogg packs: spike and recommendation (2026-10-09)

**Today's layout (measured on `logs/f6/cooked`, which includes the 19 and `A_Vehicle`):** `Sounds/` holds 425 files:
162 packs `.ogp` (123.4 MB; 95,632 B to 4.34 MB), 183 wave indexes `.uc2wb` (1.10 MB; 194 B to 29.5 KB), 79 cue
tables `.uc2cues` (1.22 MB; 1.6 KB to 75 KB), `cook-report.txt`; `Music/` 23 `.ogg` (65.4 MB). The installed tree has 3
fewer (no `A_Vehicle`); the frontend manifest lists Sounds 421 / Music 23. Each wave in a pack is a complete Ogg Vorbis
stream with its own (empty) comment header.

**How the runtime reads them** (`UC2ALAudioSubsystem.cpp`): the 23 persistent cue tables at device start
(`GPersistentBanks :36`, `GetSoundBank :238`), a level's cue table when the level registers it; a wave index when a
variation of it is first resolved (`GetWaveBank :266`); the pack, whole, at the first decode (`EnsureBuffer :339`,
`appLoadFileToArray`). On the web every file is fetched whole on first open; web-audio.md measured "23 persistent cue
tables read: 1,345 ms" (about 58 ms per small file, latency-bound) and 27 Sounds files (1 MiB) fetched for the menu.
The reader decodes each wave with vorbisfile over its byte range; it never looks at the comment header (no
`ov_comment` anywhere), so it would need new code to read one.

**Why the cue table does not fit in a pack (measured):**
- A cue table belongs to a *sound* bank, a pack to a *wave* bank, and they are not 1:1: 7 of the 79 sound banks play
  waves of other wave banks, 4 packs (`A_Combo` 2, `A_Gameplay` 4, `A_Weapon` 3, `A_Character` 2 sound banks) serve
  several sound banks, and 84 packs are localised copies (`_deu/_fra/_ita/_spa`): in German the game reads
  `A_Announcer_deu.ogp`, so the cue table would have to be duplicated into 5 packs or the English pack fetched for
  its header only.
- The cue tables are needed early, the packs late. The 23 persistent cue tables are 837 KB; their 23 packs are
  30.8 MB. With the tables inside the packs, start-up (native: read; web: fetch) grows from 0.84 MB to 30.8 MB (x37),
  and every one of those packs stays resident from the start instead of at first use. The file count saved (79) does
  not pay for that.

**The variant that does fit: the wave index inside its own pack.** The index (`.uc2wb`) is needed exactly when its
pack is (first resolve, then first decode, normally the same cue), and it is per wave bank including the language.
Spike (scratchpad `f6/spike/`, measured): `A_Announcer` (the largest index, 181 lines, 23,677 B), one Vorbis comment
field per index line (`UC2WB00000=uc2wb 1`, ...) written into the first wave's comment header by
`ffmpeg -i first.ogg -i meta.txt -map 0 -map_metadata:s:a:0 1 -c copy -fflags +bitexact first-meta.ogg`:
the first stream grows from 27,940 to 54,227 B (+26,287 B: the index plus field names); its decoded PCM is unchanged
(same MD5 through ffmpeg); a page reader of 25 lines (`parse.py`: Ogg pages from byte 0, packets 0 and 1 of the first
stream, the comment fields) gets the index back identical, reading 29,932 B before the first audio page, in 0.32 ms
in Python. Ogg allows it (a comment packet may span any number of pages; fields are `NAME=value` UTF-8).
What it would take: the cook writes the comment (ffmpeg remux as above, or our own Ogg page writer with the CRC) and
must make the offsets self-consistent (they move by the comment's own size: offsets relative to the end of the first
stream, or a fixed point); the runtime gets a page/comment parser (libogg is vendored; ~60 lines of C) and reads the
pack at the index's moment; both formats stay readable (the `.uc2wb` wins when present: the installed packs keep
working); the frontend manifest loses 162 entries. Gain: 162 fewer files (Sounds 425 -> 263) and one fewer fetch per
wave bank at first use on the web (~58 ms each by the figure above, e.g. ~2 of the 27 menu fetches).

**Recommendation:** do not embed the cue tables (the numbers above: x37 start-up bytes, many-to-many banks, five
languages). Do not implement the index-in-pack variant now either: it works (the spike proves it) but its gain is a
file count and a few small fetches, against a second format in the reader, the cook's offset fixed point and a
manifest change in the parked frontend. The real web cost in this area is the 23 persistent cue tables fetched one by
one at start (1.3 s); the parked precache list (PLAN, "Parked for the frontend merge") fetches them up front, in
parallel, without any format change. **Not implemented** (stopped at the recommendation, as the brief allows).
