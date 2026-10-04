# Audio (M8): cooked XACT data + UC2ALAudio on OpenAL Soft

Running notebook for milestone M8 (PLAN.md row M8). Branch `m8-audio`, worktree `../engine-uc2-m8`.
Appended after every step so a successor can continue.

Goal: the PC build had no audio driver (`Engine.NullAudioDevice`); the engine's audio interface
(`Engine/Inc/UnAudio.h`) is shaped around named XACT sound cues and was only implemented for Xbox
(`XboxAudio/Src/XACTAudioSubsystem.cpp`). M8 = an offline cook (`tools/cook-audio.py`: XACT project + source
WAVs -> Ogg Vorbis + cue tables) and a portable subsystem (`Source/UC2ALAudio`, OpenAL Soft) that plays the
cooked data the way the Xbox code did. Runtime loading of the shipped `.xsb/.xwb` is M16; the web build of the
package is M11.

## Step 0 - reading (2026-10-04)

### The spec, `XACTAudioSubsystem.cpp` (file:line in `Source/XboxAudio/Src`)

- `Init` (:150-255): XACT engine with 188 2D + 64 3D hardware voices; DSP image `..\xbox.bin` (never needed by
  us); distance factor 0.0254/2 m per unit (doppler/HRTF only); category volumes from the default
  `UPlayerProfile` (`MusicVolume`, `SoundFXVolume`, `VoiceVolume`); 23 persistent banks registered up front
  (:215-237: A_Character, A_Combo, A_Gameplay, A_Pickup, A_Weapon, A_WeaponMelee, A_WeaponRace, A_Announcer,
  A_Announcer_MK, A_Ann_Female and the 13 A_Taunt* banks).
- Bank lookup `GetBankIdx` (:526-619): wave bank `..\Sounds\<Bank>_<lang>.xwb`, else `..\Sounds\<Bank>.xwb`;
  sound bank is always `..\Sounds\<Bank>.xsb` (no localised .xsb exists). Persistent banks survive level
  change, the others are unloaded in `NotifyLevelChange` -> `RemoveSoundBanks` (:671-728).
- Cue names: script passes `Bank.Cue` or `Bank.Group.Cue`; `GetNamedCueIdx` (:800-848) / `RegisterSoundCue`
  (:2391-2465) keep the first and the last dot-separated part and look up the friendly name `Bank\Cue`.
- `PlaySoundCue` (:2133-2345): no actor -> fire-and-forget 2D. Otherwise 2D when the actor is a viewport's
  player controller or is owned by its view target; else 3D, heard by the closest viewport; distance cull for
  non-ambient 3D cues without a Volume parameter: skipped when `radius < dist * (1 + cues/15)` with `cues` =
  number of live cue instances; ambient (`bAmbient`) replaces the actor's previous ambient cue; one source per
  (actor, 2D/3D); the optional `Volume` (0..1) is set as the cue instance's parameter control "Volume".
- `GetSoundCueRadius` (:2609-2636): `2*MinDist + MinDist*(2/Rolloff)` from the cue's 3D properties;
  `GetSoundCueDuration` (:2573-2599): the cue's length in ms / 1000.
- `Update` (:1584-1719): listener at the view target's location, oriented by the player controller's
  rotation (front = X axis, top = Z axis); 3D source positions refreshed when moved more than
  `MIN_UPDATE_DELTA` (32, squared distance, so about 5.7 units); split screen: one real listener, other
  viewports' sources transformed into it (`MakeRelativeToViewport`, :366-382); static `AmbientSound` actors
  managed every second: the 5 nearest within their cue radius play, others stop (`UpdateAmbientCues`,
  :2010-2120); doppler is off for every source (:500 "don't allow anything to doppler").
- Music `PlayMusic` (:1777-1872): one streamed wave bank `..\Sounds\M_<Song>.xwb` per track plus the cue
  `<Song>` of sound bank `A_Music`; fade times ignored ("change the actual song cue"); one song at a time.
- Categories (:2686-2718): Music/FX/Voice master volumes, `CalcVolume` = `1000*log10(clamp(v,0.1,1))`
  hundredths of dB (so 0.1 -> -10 dB, below 0.1 = -100 dB silence) (:864-873). VoiceChat goes to the voice
  subsystem.
- `NotifyPauseChange` (:338-351) pauses every live cue instance (music is not in that list, so it plays on).
- `NotifyLevelChange` (:265-326): stop everything, music too, drop sources, unload non-persistent banks.
- `NotifyZoneChange` (:1267-1289): the zone -> I3DL2 preset code is **inside `#if 0`** in the shipped source:
  the Xbox game never changed reverb per zone. The preset table (:1169-1256) maps `EZoneAudioEffect`
  (`ZoneInfo.uc:56-84`) to the 26 `DirectSoundI3DL2ListenerPreset_*`. See "Defects and divergences".
- `StopSoundCues` (:2529-2563) is commented out entirely (no-op) and `AActor::execLocalStopSoundCue`
  (`UnScript.cpp`) is a no-op too; `StopSoundCue` works.

### Engine call sites (all already compiled on PC; the null device ignores them)

`UEngine::InitAudio` (`UnEngine.cpp:72-110`: `ini:Engine.Engine.AudioDevice` when `UseSound`, `GIsClient` and
no `-nosound`; on `Init()` failure it deletes the device and falls back to `Engine.NullAudioDevice`), script
natives (`UnScript.cpp:2040-2420`), `NoteDestroy` (`UnLevAct.cpp:410`), `NotifyZoneChange` for the local
player's pawn (`UnLevAct.cpp:2308`), `NotifyLevelChange`/`CleanUp`/`SetViewport` (`UnGame.cpp`), `Update` from
`UGameEngine::Draw` (`UnGame.cpp:3191, 3223`), `NotifyPauseChange` (`UnScript.cpp:5135`). A dedicated server
never creates an audio device (`GIsClient` is 0).

## Defects and divergences in Epic's code and data (recorded; behaviour kept unless stated)

- Zone reverb disabled in the shipped Xbox code (`#if 0`, above). M8's brief asks for zone reverb, so
  UC2ALAudio implements it, switchable; the Xbox behaviour (one fixed listener reverb) is the switch's other
  setting. Owner decision recorded in BLOCKERS.md.
- **Localised wave banks out of order (Epic data).** A sound bank names a wave by its *index* in the wave bank,
  and on the Xbox a localised `.xwb` replaces the English one at the same indices. In 99 entries the localised
  bank holds a different line at that index: `A_TauntSapphire_{deu,fra,ita,spa}` entries 40-53 are shifted by
  one (base 40 = `Spammer`, 53 = `Sorry`; localised 40 = `Sorry`), and one entry of `A_Tutorial1_spa`. The
  shipped `.xwb` files have the project's order (checked by length: `A_TauntSapphire_deu.xwb` entry 40 is
  21,564 bytes of ADPCM = 38,336 frames = the 76,626-byte `Sorry` source), so the Xbox played the wrong taunt
  in those four languages. Kept (Xbox order); `cook-audio.py --relink-localised` re-orders by name instead.
  Owner decision (BLOCKERS.md).
- **19 wave entries have no source file** (list in "Step 1"); the shipped `.xwb` contains all of them (the
  `.xwb` cross-check found entry counts and lengths matching), i.e. the Xbox played them. Not invented, not
  decoded from the `.xwb` (brief); the variations are left out. Two of the three entries pointing into a
  `..\..\UC2Demo\SoundData\...` tree resolve to the same relative path in this tree; for the third
  (`A_Machines\Machine_Generator03`, used by `A_ChemicalDawn`) this tree's file is a different, shorter recording
  (55,808 frames; the shipped one has 193,472): the cook uses ours and its loop region is clamped.
- `A_Vehicle.xsb`/`.xwb` (31 vehicle cues) ship but are not in the project. Not cooked (no project data).
- Three cues have no sound at all (`A_Weapon\ST_Primary`, `A_WeaponMelee\JG_JumpChargeEthereal`,
  `A_WeaponRace\LI_Impact`) and one sound has a play event with no wave (`A_Octane\Water_Dripping01`). They
  were silent on the Xbox too; cooked as silent cues.

## Step 1 - `tools/cook-audio.py` (A1)

### The `.xap` grammar, as parsed

```
file   := item*
item   := assign | block
assign := KEY '=' VALUE ';'        KEY runs to the first '='; VALUE may span lines (Comment) up to a line ending in ';'
block  := NAME NEWLINE '{' item* '}'   name, '{' and '}' each on their own line
```
CRLF text, Latin-1. Keys repeat inside a block (`Deviation` three times in a `Mapping`), so a block keeps its
assignments in order. Values never carry their own quoting. The parser is strict: a `{` without a name, a name
without a `{`, a value that never ends, or unbalanced braces stop the cook with the line number. A schema
(`SCHEMA` in the tool) lists every block and key the cook knows; anything else is written to the report as
`UNKNOWN`. Result on `Avaris.xap` (248,057 lines): **0 things not understood**.

Block tree (counts in this project):
`Wave Bank`(182){`Entry`(7,181)} . `Sound Group`(9) . `Sound Category`(3) . `Sound Layer`(5) .
`Sound Bank`(78){`Sound Template`(3){`3D`} . `Sound`(2,428){`3D`(1,113) . `Parametric EQ`(15) .
`Parameter Control`(33){`Mapping`{`Coordinates`(4 each)}} . `Track`(2,559){`Play with Pitch and Volume
Variation`(2,559){`Registered Wave`(2,860)} . `Set Volume`(108) . `Set Pitch`(57) . `Low-pass Filter`(4) .
`Multi-function LFO`(1) . `Pitch LFO`(1) . `Pitch and Filter EG`(1) . `Set MixBins`(5){`MixBin`} .
`Loop Start`(1)}} . `Cue`(2,438){`Sound`(2,435)}}.
Only non-default values are written (e.g. `Volume` is absent on 1,528 sounds = 0 dB).

### Units and meanings, with the evidence

Sources: XDK 5849 `xact.h`, `DSound.h`, the SDK help (`XboxSDK.chm`, unpacked with `7z x` into the scratch
directory: `xbox_xact_authoring_dxaudio.htm` "Track Event Overview", `cd_aud_wp_xactsounds.htm`,
`cd_aud_wp_bp_chap10.htm`), the XACT tool binary `XDK/xbox/bin/xact.exe` (string table), and the data itself.

| Field | Unit / meaning | Evidence |
|---|---|---|
| `Volume`, `Volume Low/High`, `Set Volume Low/High`, `I3DL2 Level`, `LFE Level`, group `Volume`, RPC volume `Y` | hundredths of a dB (mB), 0 = unity | `xact.h:438` "Sound volume (dB * 100)"; guide: volume variation "as low as -64 dB ... as high as 64 dB", and the data's extremes are exactly -6400 and +6400 |
| `Pitch`, `Pitch Low/High`, `Set Pitch Low/High` | hundredths of a semitone (cents) | guide: pitch variation "as low as -24 semitones ... 24 semitones", data extremes exactly -2400/+2400; `Set Pitch` -1200 (an octave) on 10 events. NB the *runtime* API unit is different (`lPitch` "semitone * 4096 / 12", `xact.h:439`) |
| `Timestamp`, `Duration`, `Variable Delay`, `Delay` | milliseconds | integer values such as 875, 1350, 20000 where the tool's UI shows seconds; `dwLength` of the API is ms |
| `Loop Count` (play event, Loop Start) | additional plays; 65535 = infinite | guide "loop count is 1, the sound will play twice"; `XACT_LOOP_INFINITE 0xFFFF` (`xact.h:318`) |
| `Loop Count` (Set Volume/Pitch, LPF) | fade step count | guide "Fade step count"; values 10-40 only, always with `Duration` |
| `Set Volume/Pitch` `Variation = 1` | random value in [Low, High] | guide "Enable variation"; e.g. every `*_DoubleJump` pitch -400..-200 |
| `Set Volume/Pitch` `Recurring = 1` | fade (ramp) Low -> High over Duration in Loop Count steps | guide "Enable fade ... Fade step count"; every Recurring event has Low != High (e.g. combo loops -6400 -> 0 over 500 ms: a fade-in); every non-Recurring, non-Variation event has Low == High (a plain set). Name not documented in this SDK; inferred |
| `Relative Value = 1` | adds to the track's other values (else replaces) | guide "Use relative values"; all 165 events are relative |
| `Playlist Type` | 0 Random, 1 Ordered, 2 Shuffle, 3 Parameter-controlled, 4 Random (no immediate repeats), 5 Ordered from random | the guide lists the six alphabetically (no numbers). `xact.exe` STRINGTABLE (UTF-16): "Random", "Ordered", "Shuffle" consecutive (file offset 0xd351c), later "Parameter-controlled", "Random (no immediate repeats)", "Ordered from random" (0xd7aa2-0xd7b08). Data: 2,515 events omit it (default 0 = Random, the tool's natural default), 43 set 4 and they are all footsteps, melee swings, pain and chant sounds (exactly where an immediate repeat is audible), 1 sets 1 (`Combo_EMP`). Inferred, consistent |
| `Weight` (Registered Wave) | 0..10000, absent = equal | `XACT_VARIATION_TABLE_WEIGHT_MAX 10000` (`xact.h:501`); only 15 waves carry one |
| `Priority` | 0..255, **lower = more important**; absent = 0 | not stated in this SDK. Data by group: announcer 40/50, weapons/melee 100, character/footsteps 150, combo 180, taunts 225, ambience 240-254. Stealing rule from `cd_aud_wp_bp_chap10.htm:148`: a lower-priority sound is stopped for a higher one; at equal priority the oldest is stopped. Inferred |
| `3D` block | DirectSound 3D buffer parameters, distances in game units | `Minimum/Maximum Distance`, `Rolloff Factor`, cones; `Cone Outside Volume` is 0 everywhere (cones have no effect); doppler unused by the game (`XACTAudioSubsystem.cpp:500`) |
| `Headroom Boost = 1` | the 6 dB per-voice headroom removed (sound 6 dB louder than one without) | `cd_aud_wp_xactsounds.htm` "6 dB gain boost option" |
| non-3D sound played on a 3D source | DirectSound default 3D properties (min distance 1, rolloff 1) | `cd_aud_wp_xactsounds.htm`, table "Mixing and Matching 2D/3D Sounds" |
| RPC `Volume` mapping | X = parameter 0..65535 (script's 0..1 scaled), Y = volume offset in mB, piecewise linear | guide "The range of the RPC (the input value) is from 0 to 65535"; `XACT_PARAMETER_ENTRY.flStartValue` 0..1. All 33 curves are steps 0 -> +1500..+4144 mB at X = 32765 (pain/death sounds heard by the instigator: `UCPawn.uc:3145` passes 1.0 for the instigator, 0 otherwise) |
| Wave `Loop Region Offset/Length` | bytes of the source's PCM data, relative to the play region | equals `(end+1)*block_align` of the WAV `smpl` loop on all 378 looped entries checked |
| Wave `Format Tag`, `File Type` | 0 PCM / 2 WMA; 0 WAV, 1 AIFF, 2 WMA | `WAVEBANKMINIFORMAT_TAG_*` (`xactwb.h`); the 53 `File Type = 1` entries are AIFF data in `.wav` files (`A_TauntArclite_ita`, one `A_TauntLauren_deu`) |

### Cooked format (version 1; a file format we own)

All text files: ASCII/Latin-1, LF, one record per line, `keyword key=value key=value ...`. Values never contain
space, `=`, `%`, `#` or control characters: those are written `%XX` (hex). Indentation is cosmetic; a line
belongs to the nearest preceding parent record (`sound` > `track` > event > `wave`). Readers ignore unknown
keywords and keys (forward compatibility); the first line names the format and its version.

**`Sounds/<WaveBankFile>.uc2wb`**, one per wave bank file (`A_Announcer.uc2wb`, `A_Announcer_deu.uc2wb`, ...):
```
uc2wb 1
bank name=<wave bank name> file=<Xbox .xwb file> lang=<deu|fra|ita|spa|-> data=<data file, relative to Sounds/> stream=<0|1> entries=<n>
wave index=<i> name=<entry name> offset=<byte> size=<bytes> rate=<Hz> channels=<n> frames=<sample frames> loop=<start>,<length> src=<source file name>
wave index=<i> name=<entry name> missing=<no-file|unreadable|encode-failed> src=<as the project names it>
```
`data` is `<bank>.ogp` (sound effects, speech) or `../Music/<Track>.ogg` with `stream=1` (the 21 `M_*` music
banks). `offset/size` delimit one complete Ogg Vorbis stream inside `data`. `frames` and `loop` are in sample
frames of the source; `loop=0,0` = no loop region (a looping play event then loops the whole wave). A localised
bank has the same `name` as its base bank and its own `file`.

**`Sounds/<WaveBankFile>.ogp`**: the bank's waves, each a complete, independent Ogg Vorbis physical stream
(libvorbis, sample rate and channel count of the source, no tags), concatenated in index order with no padding.
A wave is decoded from its byte range alone.

**`Sounds/<SoundBank>.uc2cues`**, one per sound bank:
```
uc2cues 1
bank name=<sound bank> sounds=<n> cues=<n>
group name=<group> volume=<mB>                                    (the groups this bank's sounds use)
sound index=<i> name=<s> priority=<0..255> volume=<mB> pitch=<cents> category=<Music|FX|Voice|-> group=<g|->
      layer=<l|-> headroomboost=<0|1> i3dl2=<mB> lfe=<mB> 3d=<0|1> length=<ms, -1 infinite> loops=<highest loop count>
 3d mode=<n> cone_in=<deg> cone_out=<deg> cone_vol=<mB> min=<units> max=<units> distfactor=<f> rolloff=<f> doppler=<f>
 eq enabled=<0|1> gain=<n> q=<n> freq=<n>
 rpc name=<parameter> track=<i> type=<n> deviation=<a,b,c|-> points=<x:y,x:y,...>
 track index=<i> name=<n|->
  play time=<ms> delay=<ms> loops=<n, 65535 infinite> newvar=<0|1> playlist=<0..5> pitch=<lo,hi cents> volume=<lo,hi mB> waves=<n>
   wave bank=<wave bank name> entry=<index> weight=<0..10000, -1 = unset> name=<entry name>
  setvol|setpitch time=<ms> mode=<set|random|fade> relative=<0|1> type=<n> steps=<n> dur=<ms> lo=<n> hi=<n>
  lpf time=<ms> mode=<set|random|fade> relative=<0|1> steps=<n> dur=<ms> freq=<lo,hi> res=<lo,hi>
  lfo time=<ms> delay=<ms> delta=<n> pitchmod=<n> filtermod=<n> ampmod=<n>
  pitchlfo time=<ms> delay=<ms> delta=<n> pitchmod=<n>
  eg time=<ms> delay= attack= hold= decay= release= sustain= mode= pitchscale= filtercutoff=
  mixbins time=<ms> bins=<bin:mB,...>
  loopstart time=<ms> count=<n> outer=<n>
cue index=<i> name=<Bank\Cue> queue=<0|1> sounds=<sound index:weight,...|->
```
`length` and `loops` are what `GetSoundCueProperties` returned on the Xbox (`dwLength`, `dwLoopCount`):
`length` = the longest track (timestamp + delay + longest variation x (loops+1)), -1 if anything loops forever.
Everything the project carries is in the table, including what the runtime does not use yet (EQ, LFOs, EG,
mix bins, low-pass events, LFE); see "Runtime mapping" for what is played.

**`Music/<Track>.ogg`**: one Ogg Vorbis file per `Music/*.wav` (23; the project's 21 music banks name 21 of
them, `AnubisFightsSelket` and `Theme` are cooked too). **`Sounds/cook-report.txt`**: the full report.

### Encoder settings and packing, with the reasons

- `ffmpeg -i <src> -map_metadata -1 -fflags +bitexact -flags:a +bitexact -c:a libvorbis -q:a 4` for effects and
  speech, `-q:a 5` for music. Rate and channels kept (ffmpeg's default). Quality sweep on 24 sources (weapons,
  announcer, character, ambients, pickups; signal-to-distortion ratio of the decoded result against the source,
  ffmpeg `asdr`): q2 9.5% of the PCM size, SDR median 17.8 dB; q3 10.7%, 19.2; **q4 12.0%, 20.2**; q5 14.0%,
  21.4; q6 16.3%, 22.9. The Xbox's own data, decoded from the shipped `.xwb` (Xbox ADPCM wrapped as WAVE format
  0x69 for ffmpeg) and measured the same way: weapons 16.8-27.8 dB (median about 20), announcer 27.7-33.0 dB, at
  28% of the PCM size. SDR flatters ADPCM (its error is white; Vorbis puts its error under the signal), so q4 is
  at least the Xbox's quality at 43% of its size. Music q5 (about 160 kbit/s for 44.1 kHz stereo; the Xbox used
  ADPCM at about 390 kbit/s).
- Loops: Vorbis has no loop points; the loop region is in the index (`loop=`). The encoder's end-of-stream
  granule position is exact (checked: the last page of `L1_Pain2` carries 16,637 = the source's frames), so a
  wave decodes to exactly its source length and loops without a gap. (ffmpeg's own decoder ignores that trim
  when reading a cut-out stream; libvorbisfile, which the runtime uses, honours it.)
- **Packing: one `.ogp` per wave bank.** The wave bank is the engine's unit of loading (the Xbox registered a
  bank when a cue of it was first played and kept or dropped it per level), so one file per bank fetches exactly
  what the first touch needs. Per-wave files would be 6,777 files of a few KB (most waves are short effects);
  one archive would be 122 MB fetched up front on the web. Result: 161 packs from 96 KB (`A_AnubisCheats_spa`)
  to 4.3 MB (`A_Announcer_MK`); 21 music files of 0.9-5 MB streamed by themselves.
- Byte-identical sources share one encode (`.cache/` is keyed by the source's SHA-1, the quality and the format
  version); a re-cook only encodes what changed (1m55s for everything from scratch with 6 encoders, a few
  seconds from the cache).

### Running it

```sh
cd /home/ion/WorkUC2/engine-uc2-m8
/home/ion/WorkUC2/bin/capped 4G 3600 -- python3 tools/cook-audio.py --jobs 6      # -> assets/uc2-cooked
/home/ion/WorkUC2/bin/capped 2G 600 -- python3 tools/cook-audio.py --no-encode --out <scratch>   # checks only
```

### Result (2026-10-04, `logs/m8/cook-2.out`, report `assets/uc2-cooked/Sounds/cook-report.txt`)

- Parsed: 182 wave banks (7,181 entries), 78 sound banks, 2,428 sounds, 2,438 cues, 9 groups (volumes:
  Ambience -200, Machines -100, Taunts -600, Weapons 0, all -300, announcer 0, character -400, footsteps 0,
  melee -700 mB), 3 categories (Music, FX, Voice), 5 layers; schema: 0 unknown keys or blocks; 0 unresolved
  variation references (every `Registered Wave` names an existing bank and the entry of that name).
- Encoded: 6,777 distinct sources, 0 failures. Cooked: 161 `.ogp` packs = 122.1 MB, 21+2 music tracks = 59.9 MB,
  182 `.uc2wb`, 78 `.uc2cues`: **182 MB against 375.9 MB of shipped `.xwb`+`.xsb`** (366 MiB).
- Missing sources: **19** entries (the brief's "about 23"):
  `A_Combo` 90/91/94/102 (`Combo_Pounce_Travel2/3`, `Combo_BladeRush_Travel2`, `Combo_SentinelFire`),
  `A_Gameplay` 40/41/42 (`Gib_Liandri01/02`, `NewGib4`), `A_Weapon` 163 (`ST_Primary`), `A_WeaponRace` 78
  (`LI_RivetAlt`), `A_Announcer_spa` 72/94/96 (`Congratulations`, `FinishHer`, `FinishIt`), and seven ambient
  loops in level banks that all point at two files absent from `SoundData/A_Ambients` (`Amb_Texture_13mono`,
  `amb_space_ext_loop11mono`) or at `amb_texture_22b`: `A_Emperor` 13, `A_Gauntlet` 0, `A_Sever` 15,
  `A_SkyReach` 3, `A_SolarFlare` 4, `A_Tempest` 7/8.
- Cue resolution (base language): 2,417 cues fully playable, 6 partly (a missing variation: e.g.
  `A_WeaponRace\LI_Fire` 2 of 3), **15 with no playable wave** (the 3 sound-less cues, `Water_Dripping01`, and 11
  whose only wave is missing: `Combo_BladeRush_Travel`, `Combo_Pounce_Travel`, `Combo_SentinelFire`,
  `Gib_Explosion_LI` and seven level ambients).
- `.xsb` cross-check (name table reader, format 11: header counts at 0x18, cue records of 20 bytes at 0x38 with
  a name offset at +4, wave bank names of 16 bytes at the offset in 0x08): **78 of 78 project sound banks agree**
  in bank name, cue names, cue count, sound count and referenced wave banks. One shipped `.xsb` is not in the
  project: `A_Vehicle.xsb` (31 cues, `A_Vehicle\Shenu_Crash` ...).
- `.xwb` cross-check (entry table per `xactwb.h`, compact and full layouts): **181 of 182 wave banks agree**
  with the shipped file in name, entry count and, entry by entry, channels, rate and length within 1%. The one
  difference is `A_ChemicalDawn` entry 7 (`Machine_Generator03`, see Defects). `A_Vehicle.xwb` ships without
  project data.
- Playlist types used: 0 (Random) 2,515, 4 (Random, no immediate repeats) 43, 1 (Ordered) 1.
