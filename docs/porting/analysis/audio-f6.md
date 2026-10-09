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
