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
wave index=<i> name=<entry name> offset=<byte> size=<bytes> rate=<Hz> channels=<n> frames=<sample frames> loop=<start>,<length> src=<source file name | xwb:<bank>.xwb:<index>>
wave index=<i> name=<entry name> missing=<no-file|unreadable|encode-failed> src=<as the project names it>
```
`data` is `<bank>.ogp` (sound effects, speech) or `../Music/<Track>.ogg` with `stream=1` (the 21 `M_*` music
banks). `offset/size` delimit one complete Ogg Vorbis stream inside `data`. `frames` and `loop` are in sample
frames of the source; `loop=0,0` = no loop region (a looping play event then loops the whole wave). A localised
bank has the same `name` as its base bank and its own `file`.
`src=xwb:...` (F-6, `audio-f6.md`): the project has no WAV for the entry; it was decoded from the shipped `.xwb`.

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

Banks that ship but are not in the project (`A_Vehicle`) are cooked from their `.xsb`/`.xwb` into the same
format (F-6, `audio-f6.md` step 2).

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

### More evidence from the shipped `.xsb` sound records (found while building the runtime)

The sound table of an `.xsb` follows its cue table (20-byte records). Bytes 4-5 hold the sound's volume and
byte 14 its priority (150 for every footstep, 180 for combos: equal to the project's values). Over all 2,428
sounds the stored volume is exactly `-512 - (Volume + group Volume)/16`: e.g. (0, group 0) -> -512,
(-200, 0) and (0, group -200) -> -500, (-6400, 0) -> -112, (-600, -400) -> -450. So **a sound group's volume is
baked into each of its sounds** (not only a mixing-window control), and the project's volumes are linear in a
single unit. UC2ALAudio adds the group volume to the sound volume.

## Step 2 - build: Ogg/Vorbis vendored, OpenAL Soft fetched (A2, part 1)

- `Thirdparty/Vorbis/{ogg,vorbis}`: engine-ut2004's copy (libVorbis 1.0), see `Thirdparty/Vorbis/README.uc2`.
- Root `CMakeLists.txt`, one delimited block ("M8") after SDL2: the two vendored libraries (all targets), and
  on native non-Windows builds openal-soft from `10-p/openal-soft-web` branch `ufront/web` (the sibling's fork:
  openal-soft 1.25.2 + Web Audio backend for M11), SHARED, EAX off (we use EFX), **wave-file backend on**, built
  as C++20 with the engine's directory-wide definitions and options removed for its scope. Configure on this
  machine: backends PulseAudio, ALSA, OSS, WaveFile, Null (PipeWire headers are not installed for i386; PulseAudio
  covers PipeWire through its pulse server).
- Problem found: openal-soft decides "32-bit: compile with `-msse2 -mfpmath=sse`, realign the stack at entry
  points" from `CMAKE_SIZEOF_VOID_P`, which is 8 here (our `-m32` demotion happens after `project()`), so it
  compiled SSE intrinsics for plain i686 and failed (`inlining failed in call to always_inline _mm_set_ss:
  target specific option mismatch`). Fixed by setting `CMAKE_SIZEOF_VOID_P` to 4 for its configure only.
  Result: `build-native/_deps/openalsoft-build/libopenal.so.1.25.2: ELF 32-bit LSB shared object, Intel i386`.
- `UC2ALAudio` added to `UC2_NATIVE_ONLY_PACKAGES` (web: M11).

## Step 3 - `Source/UC2ALAudio` (A2, part 2): written, builds; not yet run

Files: `Inc/UC2ALAudio.h` (classes `UC2ALAudio.UC2ALAudioSubsystem` and the check commandlet
`UC2ALAudio.UC2AudioCheck`, nothing of OpenAL visible), `Src/UC2AudioData.cpp` (cue table and wave index
readers, Vorbis from memory through vorbisfile callbacks), `Src/UC2ALDevice.cpp` (dlopen binding of
`libopenal.so.1`, beside the binary first then the system's; the 26 I3DL2 presets of the XDK's `DSound.h` and
their translation to EFX), `Src/UC2ALAudioSubsystem.cpp` (the XACT behaviour), `Src/UC2ALAudio.cpp` (package +
commandlet). Root `CMakeLists.txt`: `UC2ALAudio` moved before `UCC` in `UC2_PACKAGES` (UCC links it for the
commandlet and tests `if(TARGET UC2ALAudio)`).

## Step 4 - A4 (1): every cooked wave decodes, every cue resolves (`UC2ALAudio.UC2AudioCheck`)

```sh
tools/dev/mkrun-uc2.sh build-native m8          # run tree with libopenal.so.1, Sounds/ and Music/ hard-linked
cd /home/ion/WorkUC2/run/m8/System && /home/ion/WorkUC2/bin/capped 2G 600 -- ./ucc-bin UC2ALAudio.UC2AudioCheck
```
First run (`logs/m8/audiocheck-1.out`): 687 failures, all "decoded N frames, index says M" with N > M and N a
multiple of 128: libvorbis 1.0's vorbisfile (the vendored decoder) does not cut the last packet at the stream's
end-of-stream granule position for those streams, although the granule is right (checked on `L1_Pain2`: last
page granule 16,637 = source frames; ffmpeg's decoder over-reads the same files). Fix in the runtime: a wave is
cut to its source length from the index (`frames=`), a music stream stops at its `frames`. Second run
(`logs/m8/audiocheck-2.out`), literally:
```
wave banks: 182 index files; waves: 7141 decoded to their exact length of 7141 cooked (19 entries have no cooked wave); music streams: 21 of 21
decoder end trim: 687 waves decoded past their end-of-stream granule position (at most 662 frames) and were cut to the source length
cues [int]: 2438 in 78 cue tables: 2417 resolve fully, 6 partly, 15 to no decodable wave (0 use a localised bank)
cues [deu]: 2438 in 78 cue tables: 2417 resolve fully, 6 partly, 15 to no decodable wave (1107 use a localised bank)
cues [fra]: 2438 in 78 cue tables: 2417 resolve fully, 6 partly, 15 to no decodable wave (1107 use a localised bank)
cues [ita]: 2438 in 78 cue tables: 2417 resolve fully, 6 partly, 15 to no decodable wave (1107 use a localised bank)
cues [spa]: 2438 in 78 cue tables: 2414 resolve fully, 6 partly, 18 to no decodable wave (1107 use a localised bank)
UC2AudioCheck: 78 cue tables, 2438 cues, 0 failures
```
The 15 (spa: 18, the three Spanish announcer lines without a source) are exactly the cook's list (Step 1).

## Step 5 - first game runs with sound rendered to a file (A4 (2), in progress; written after a usage-limit cut)

### How a capture is made

`uc2.bin -audiowave=<file.wav>` (or `WaveFile=` in `[UC2ALAudio.UC2ALAudioSubsystem]`): before libopenal is
loaded, the device writes `System/UC2ALAudio-wave.conf` = `[general] drivers=wave` + `[wave] file=<file>` and
sets `ALSOFT_CONF` to it and `ALSOFT_DRIVERS=wave`. OpenAL Soft's wave writer then renders the mix in real time
(48 kHz, stereo, 32-bit float WAV) from the moment the device opens. The cue log (`DevSound`, suppressed by
Epic's `Default.ini`) is unsuppressed **in the run tree only** by the test scripts. Since this step every cue
log line carries `[seconds since the device opened]` = the time in the capture.

- `tools/dev/run-uc2-audio.sh <tree> <name> <seconds> <fire-at> <url>`: capped 2G, Xvfb, `-audiowave=`, at
  `<fire-at>` the same three Fire clicks as `run-uc2-shot.sh` (start the match), then a fixed input loop
  (random generator seeded 7): hold Up (walk) and Fire 1.2 s, sometimes Space, turn the mouse, AltFire. Writes
  `logs/<tree>/<name>.{wav,out,png,report.txt,cues.txt}`.
- `tools/dev/audio-capture-report.sh <wav>`: duration, ffmpeg `astats` peak/RMS, RMS per second, silent seconds,
  RMS per octave band, spectrogram PNG.

### What the runs found and what was fixed

1. Crash at the first level change (`realloc(): invalid pointer`, found with gdb + glibc's
   `libc_malloc_debug.so.0`): `TArray<FString>::AddUniqueItem` -> UE2's `AddItem` assigns into unconstructed
   memory (it is only for types without destructors). Fixed (construct with `new(Array) FString`). Every other
   `AddItem` in the package is on ints/pointers/PODs (audited).
2. `logs/m8/menu-2.wav`: 58.7 s, peak -13.0 dBFS, RMS -28.5 dBFS, 0 silent seconds; log: `music NuBattle:
   playing ../Sounds/../Music/NuBattle.ogg (44100 Hz, 2 ch, looping, volume -900 mB, fade in 3.0 s)` and
   `A_Gameplay.MenuPageOpened ... played 2D`. RMS per second rises -40, -39, -34, -31, -28 over the first 4 s
   (the 3 s fade-in of `ClientSetMusic`).
3. `DM-Deadbolt` without bots (`deadbolt-1`): only countdown, `A_Ann_Female.Play`, spawn and 6 ambients: nobody
   fires. Runs now use `DM-Deadbolt?NumBots=3` and the input loop.
4. `deadbolt-2.wav`: music dropped out for 0.3-0.8 s several times (high band -72 dBFS in 100 ms windows at
   32.4-33.2, 35.3-35.7, 38.2-38.9, 41.1-41.6 s ...): the music stream is refilled from the game thread and its
   4 x 8192-frame buffers (0.74 s) ran dry during long first-load frames under software GL. Fixed: 4 x 32768
   frames (3 s). `deadbolt-3.wav`: no such gaps in the spectrogram.
5. Uncommitted change committed with this step (`UC2ALAudioSubsystem.cpp`): the static-ambient list is built
   once per level (it was rebuilt, and logged, every second when a level has none: menu spam); the music buffer
   size above; the timestamped cue log (`CueLogf`); `-NOEFX` (dry comparison runs) and a correct "no EFX" test
   (the device counts as having EFX only if an EAX or standard reverb effect can actually be created); the
   level's zone presets listed once per level, e.g. DM-Praxis: `zone reverb presets in this level: Default x2
   Auditorium x2 Hangar x1 Hallway x9 Underwater x1`.

### Open doubts at this point

- No zone change has been logged yet: DM-Deadbolt's zones are all Default (the string `ZoneAudioEffect` is not
  in its name table at all); in DM-Praxis the player stayed in a Default zone. A4 (4) needs a spawn or walk into
  a Hallway/Auditorium zone, or a deliberate zone change.
- The listener orientation mapping (Unreal X fwd, Y right, Z up -> OpenAL (Y, Z, -X)) has not been checked by
  measurement (left/right).
- Priority direction, the default of an unset variation weight (one event mixes them) and "Recurring = fade"
  are inferences (Step 1 table).

## Step 6 - numeric self-test without ears: `UC2ALAudio.UC2AudioSelfTest` (owner's request after the cut-off)

The device can be initialised in **loopback mode** (`LoopbackTest`, set only by this commandlet): OpenAL Soft's
`ALC_SOFT_loopback` device mixes into memory (48 kHz stereo float), time is simulated in 10 ms steps (the device
updates its cue instances between steps, as it does once a frame), and the listener sits at the origin looking
down +X with no rotation. Cues are started through the device's own code from `StartInst` on (variation choice,
voice allocation, source set-up, `UpdateVoice` gain/pitch/position, queueing for loops); only the actor is
replaced by a location (`UC2TestPlay`, `UC2TestRender`, `UC2TestStopAll` in `UC2ALAudioSubsystem.cpp`).
The expected values are computed **independently** in `UC2AudioSelfTest.cpp` from the documented rules.

What is checked, and how:
- gain: total output energy / energy of the decoded wave (mono downmix for a 3D voice) against
  `Pan * 10^(mB/1000) / PitchRatio * 48000/SourceRate`, with `mB = Volume(+group) + variation volume - 600
  (headroom) + 1000*log10(clamp(category,0.1,1)) + Volume-parameter curve - 2000*log10(1+Rolloff*(clamp(d,Min,
  Max)-Min)/Min)`, clamped at 0. `Pan` (OpenAL Soft's own gain for a centred source, which XACT's mixer need not
  share) is measured on one reference case per kind (3D, 2D) and reported; every other case is an independent
  check of the arithmetic. A unit mistake (linear vs mB, dB vs mB, the category formula) shows up as dB of error.
- pan direction: a source at +Y / -Y must be louder in the right / left channel.
- pitch: the playback-rate ratio is fitted from the output's 2 ms power envelope against the source's (least
  variance of the log ratio over time scales 0.5..2 in steps of 0.0005), compared with `2^(cents/1200)`. Two
  simpler measures were tried and dropped: first-to-last audible sample (the resampler rings for a few ms after
  a wave that ends on a non-zero sample: -16 cents on `Lift_Tech_Start02`), 10-90% energy span (sensitive to
  onset detail: +232 cents on the same cue). The fit has a constant +4.3 cents bias (the output's onset sits
  about 2 ms after the source's), measured on the 0-cent control and taken off; raw errors are printed too.
  Cents vs semitones vs the runtime's 4096/12 unit would be errors of 100x or 0.29x.
- loops: active duration of a Loop Count 1 cue = 2 x the wave (pitch included); an infinite loop still sounds
  after three wave lengths.
- variation picker: 60 plays of a playlist-4 cue (no immediate repeat), the only playlist-1 cue (order), and a
  14-wave playlist-0 cue (all drawn, none above 3x its share).
- `GetSoundCueRadius` / `GetSoundCueDuration` against the formulas.

Command and result (`logs/m8/selftest.out`; `DUMP=<dir>` also writes every rendered case as a .wav):
```sh
cd /home/ion/WorkUC2/run/m8/System && /home/ion/WorkUC2/bin/capped 2G 600 -- ./ucc-bin UC2ALAudio.UC2AudioSelfTest
```
```
--- gain: measured energy against Pan * 10^(mB/1000) / pitch ratio (Pan measured on the first case of each kind)
REF  gain A_Weapon.RL_Fire                   3D d=   400 cat=1.00 Volume=0.00: expected  -900.0 mB, measured  -900.0 mB, error -0.00 dB (defines Pan3D = -1.10 dB)
REF  gain A_Weapon.RL_Fire                   2D d=  5000 cat=1.00 Volume=0.00: expected  -900.0 mB, measured  -900.0 mB, error +0.00 dB (defines Pan2D = -1.52 dB)
PASS gain A_Weapon.RL_Fire                   3D d=   800 cat=1.00 Volume=0.00: expected  -900.0 mB, measured  -900.0 mB, error -0.00 dB
PASS gain A_Weapon.RL_Fire                   3D d=  1600 cat=1.00 Volume=0.00: expected -1192.3 mB, measured -1192.2 mB, error +0.00 dB
PASS gain A_Weapon.RL_Fire                   3D d=  3200 cat=1.00 Volume=0.00: expected -1584.8 mB, measured -1584.8 mB, error +0.00 dB
PASS gain A_Weapon.RL_Fire                   3D d=  8000 cat=1.00 Volume=0.00: expected -2225.5 mB, measured -2225.4 mB, error +0.00 dB
PASS gain A_Weapon.RL_Fire                   3D d= 32000 cat=1.00 Volume=0.00: expected -3340.2 mB, measured -3340.1 mB, error +0.00 dB
PASS gain A_Weapon.RL_Fire                   3D d= 99000 cat=1.00 Volume=0.00: expected -4299.7 mB, measured -4299.6 mB, error +0.00 dB
PASS gain A_Weapon.RL_Fire                   3D d=200000 cat=1.00 Volume=0.00: expected -4308.3 mB, measured -4308.3 mB, error +0.00 dB
PASS gain A_Gameplay.CampFireBombExplosion   3D d=   275 cat=1.00 Volume=0.00: expected -1200.0 mB, measured -1204.2 mB, error -0.04 dB
PASS gain A_Gameplay.CampFireBombExplosion   3D d=   550 cat=1.00 Volume=0.00: expected -1200.0 mB, measured -1204.2 mB, error -0.04 dB
PASS gain A_Gameplay.CampFireBombExplosion   3D d=  1100 cat=1.00 Volume=0.00: expected -2404.1 mB, measured -2408.1 mB, error -0.04 dB
PASS gain A_Gameplay.CampFireBombExplosion   3D d=  5500 cat=1.00 Volume=0.00: expected -4094.3 mB, measured -4098.2 mB, error -0.04 dB
PASS gain A_Weapon.RL_Fire                   3D d=   400 cat=0.50 Volume=0.00: expected -1201.0 mB, measured -1201.0 mB, error +0.00 dB
PASS gain A_Weapon.RL_Fire                   3D d=   400 cat=0.20 Volume=0.00: expected -1599.0 mB, measured -1598.9 mB, error +0.00 dB
PASS gain A_Weapon.RL_Fire                   3D d=   400 cat=0.05 Volume=0.00: expected -1900.0 mB, measured -1899.9 mB, error +0.00 dB
PASS gain A_Character.NK_Sobek_Death         3D d=  3000 cat=1.00 Volume=0.00: expected -1602.1 mB, measured -1590.5 mB, error +0.12 dB
PASS gain A_Character.NK_Sobek_Death         3D d=  3000 cat=1.00 Volume=0.25: expected -1602.1 mB, measured -1609.8 mB, error -0.08 dB
PASS gain A_Character.NK_Sobek_Death         3D d= 60000 cat=1.00 Volume=1.00: expected  -204.1 mB, measured  -211.1 mB, error -0.07 dB
PASS gain A_Character.NK_Sobek_Death         3D d=  3000 cat=1.00 Volume=1.00: expected     0.0 mB, measured     4.7 mB, error +0.05 dB
--- pan: a source 2000 units to the right / left of a listener looking down +X
PASS pan left : right minus left channel energy -16.2 dB
PASS pan right: right minus left channel energy +15.8 dB
--- pitch: playback rate ratio fitted from the output's envelope against the source's, against 2^(cents/1200)
PASS pitch A_Weapon.RL_Fire               expected    +0 cents (x1.0000), measured    +4.3 cents (x1.0025), error +4.3 cents, +0.0 after the method bias of the 0-cent control
PASS pitch A_Pickup.Adren_Pickup          expected  -300 cents (x0.8409), measured  -295.7 cents (x0.8430), error +4.3 cents, +0.0 after the method bias of the 0-cent control
PASS pitch A_Pickup.Adren_SuperClose      expected  -500 cents (x0.7492), measured  -495.7 cents (x0.7510), error +4.3 cents, -0.1 after the method bias of the 0-cent control
PASS pitch A_Octane.Lift_Tech_Start02     expected  +200 cents (x1.1225), measured  +203.9 cents (x1.1250), error +3.9 cents, -0.4 after the method bias of the 0-cent control
--- loops
PASS loop count 1 (STF_JumpChargeEthereal): plays 6.231 s, expected 6.224 s active (6.224 s total, 68617 frames x 2 at 22050 Hz)
PASS infinite loop (Powerup_Invis_Loop, wave 2.032 s, loop region 0+44800 of 44800 frames): still sounding at 6.100 s (last sample 0.000 s from the end)
--- variations
PASS variations A_Character.Footstep_Metal (playlist 4, 6 waves): random, no immediate repeat: 0 repeats; sequence 305314104041324025130154210230524215120301425040531512141342; counts 11,13,10,8,10,8
PASS variations A_Combo.Combo_EMP (playlist 1, 3 waves): ordered (all weights unset: index order): 0 out of order; sequence 012012012012012012012012012012012012012012012012012012012012; counts 20,20,20
PASS variations A_Weapon.Generic_BulletWhip (playlist 0, 14 waves): random: counts min 1 max 7 (fair share 4.3), 5 immediate repeats; sequence 11486851212944121113231121829100128106469101182697051135411249102112228121199; counts 2,5,7,2,6,3,4,1,6,7,4,5,7,1
--- radius and duration
PASS radius A_Weapon.RL_Fire = 5600.0 (2*800 + 800*2/0.4 = 5600)
PASS radius of a non-3D cue (A_Combo.Combo_Generic_Activate) = 4.00 (DirectSound defaults: 4)
PASS duration A_Weapon.RL_Fire = 1.828 s (cue table: 1828 ms)
PASS duration of an infinite loop = 4294968 s (XACT: 0xFFFFFFFF ms)
PASS duration of an unknown cue = 0.0
UC2AudioSelfTest: 34 checks, 0 failed; Pan3D -1.10 dB, Pan2D -1.52 dB
```
Reading of the numbers: distance attenuation, category volume, headroom, group volume and the Volume-parameter
curve and its 0 dB clamp agree with the documented arithmetic to 0.00-0.16 dB (the largest, -0.16 dB and +0.11 dB,
are on the cue with a random -40..+40 cent pitch variation, whose resampled energy is not exactly 1/ratio).
OpenAL Soft's centred-source gain is -1.10 dB (3D, a mono voice straight ahead) and -1.52 dB (2D); XACT's own
voice routing levels are not known here, so the absolute level of the whole mix differs from the Xbox by an
unknown constant of this size. Left/right are the right way round (+/-16 dB for a source 2000 units to the
side; HRTF is not used, plain stereo panning). Pitch after removing the method bias: -0.4 to +0.0 cents.

### Cooked lengths against the Xbox banks

`UC2AudioCheck` proved every cooked wave decodes to exactly its index's `frames` (the source's length). Against
the shipped `.xwb` entry tables (`read_xwb` in the cook: Xbox ADPCM is 36 bytes per 64 frames per channel, PCM
from the byte length), over all 7,161 entries with a cooked wave and a non-WMA Xbox entry (the 7,141 effects and
speech waves of all languages plus 20 of the 21 music tracks; one Xbox music entry is WMA):
cooked minus Xbox = 0 frames for 680 entries, -1..-63 frames for 6,479 (the Xbox rounds up to whole 64-frame
ADPCM blocks: at most 2.9 ms at 22 kHz), +4 for one, and -137,664 frames (-6.2 s) for the one known different
source (`A_ChemicalDawn` `Machine_Generator03`, see Defects). So every cooked wave has the Xbox wave's length to
within one ADPCM block, except that one.

### Reverb checked numerically (self-test section "reverb"), and zone logging

The self-test switches the zone preset (`UC2AUDIO ZONE <n>`, the same code `NotifyZoneChange` uses), plays
`A_Weapon.RL_Fire` 400 units ahead, lets it finish, then renders the room alone for 2.5 x the preset's decay
time and fits the tail's decay (dB/s between 5 and 35 dB below its start) as an RT60 (the I3DL2/EFX decay time
is an RT60). Literal result:
```
PASS reverb Hangar     3D, send -300 mB (dry wave 1.83 s): tail energy -18.4 dB of the sound's; decay time measured 10.07 s, preset 10.05 s (+0%)
PASS reverb Default    3D, send -300 mB (dry wave 1.83 s): tail energy -58.6 dB of the sound's; decay time measured 1.68 s, preset 1.49 s (+13%)
PASS reverb StoneRoom  3D, send -300 mB (dry wave 1.83 s): tail energy -47.3 dB of the sound's; decay time measured 2.48 s, preset 2.31 s (+8%)
PASS reverb NoReverb   3D, send -300 mB (dry wave 1.83 s): tail energy -233.6 dB of the sound's; no tail expected
PASS reverb Hangar     2D (dry wave 1.83 s): tail energy -328.0 dB of the sound's; no tail expected
UC2AudioSelfTest: 39 checks, 0 failed; Pan3D -1.10 dB, Pan2D -1.52 dB
```
(First attempt measured nothing: the test renderer stopped 50 ms after the last cue, so the "tail" was 50 ms
long; it now renders a fixed time for tails.) Whether these EFX levels equal the Xbox's I3DL2 hardware output
level for the same preset is not known: the parameters are carried over 1:1 (mB -> gain, % -> 0..1), the
decay times are measured to match.

`NotifyZoneChange` now logs every call in the cue log, including "preset unchanged" and "no EFX, sound stays
dry", so a run shows whether the player's zone changed at all.

## Runtime mapping: XACT behaviour -> UC2ALAudio on OpenAL Soft, with the known differences

| XACT / Xbox (`XACTAudioSubsystem.cpp`) | UC2ALAudio | Difference, measured where possible |
|---|---|---|
| banks `..\Sounds\<Bank>[_<lang>].xwb` + `.xsb`, 23 persistent (:215-237) | `../Sounds/<Bank>.uc2cues`; wave index `<Bank>_<lang>.uc2wb` then `<Bank>.uc2wb`; the same 23 persistent; wave data (`.ogp`) read on first play; decoded per wave on first play and kept while the bank is | the Xbox could only play waves of banks registered under the sound bank's own name; we load any wave bank a cue names (more permissive; no cue in the data depends on the restriction failing) |
| cue name `Bank.Group.Cue` -> `Bank\Cue` (:800-856) | same (`ResolveCue`), cached per FName, cache cleared at level change | - |
| 2D if viewer's controller or owned by its view target, else 3D for the closest viewer (:2176-2203); cull if `radius < dist*(1+cues/15)` unless ambient or Volume>0 (:2205-2214) | same | `cues` counts our live cue instances (XACT counted its Cues list: the same thing) |
| radius `2*Min + Min*2/Rolloff` (:2609-2636) | same; a non-3D sound reports DirectSound's defaults (Min 1, Rolloff 1) = 4 | for non-3D sounds the Xbox value is not known (XACT may have returned 0, giving 0*inf); with 4 such cues on a non-viewer actor are culled beyond 4 units, as they would be inaudible anyway (next row). Self-test: 5600 for RL_Fire, 4 for a non-3D cue |
| volume: sound + group (baked into the .xsb) + variation + events + RPC, 6 dB voice headroom unless Headroom Boost, category master `1000*log10(v)` (:864), distance rolloff computed by XACT, sum clamped at 0 dB | same sum in mB, clamped at 0 dB, then `AL_GAIN`; OpenAL's distance model off (`AL_NONE`) | **numerically checked**: 0.00-0.16 dB from the formula over 18 cases. OpenAL Soft's own centred-source gain is -1.10 dB (3D) / -1.52 dB (2D), a constant the Xbox mixer need not share |
| DirectSound rolloff `Min/(Min+Rolloff*(d-Min))`, d clamped to [Min,Max] | same formula in software | exact (checked to 0.00 dB at 6 distances incl. beyond Max) |
| non-3D sound on a 3D source: DirectSound default 3D properties | Min 1, Max 1e9, Rolloff 1 | follows the XACT white paper; such sounds are near-silent unless the listener is on top of them |
| 3D voice = mono submix (stereo waves downmixed) | stereo waves decoded once more as a mono downmix for 3D voices | - |
| positional panning with HRTF on 3D voices ("full HRTF", :196) | OpenAL Soft stereo panning (no HRTF on the wave writer / loopback; on a real device OpenAL Soft decides) | the Xbox's HRTF colouring is not reproduced. Left/right checked: +/-16 dB for a source 2000 units to the side |
| listener at view target, oriented by the controller (:1691-1696); split screen by transforming sources (:366) | each source is placed in its viewer's frame (Unreal X fwd, Y right, Z up -> OpenAL (Y, Z, -X)), listener fixed at the origin | split screen: implemented, never exercised in a run (no split-screen run) |
| doppler: off for every source (:500) | `alDopplerFactor(0)` | - |
| pitch: sound + variation + Set Pitch events, cents | `AL_PITCH = 2^(cents/1200)` | **numerically checked**: -0.4..+0.0 cents on 3 cues (+200, -300, -500) after the measuring method's bias |
| playlist types | Random (weighted), Ordered (heaviest first), Shuffle, Parameter-controlled (first), Random no repeat, Ordered from random; state kept per play event across plays | **numerically checked** for types 4, 1, 0 (the only ones the data uses); 2, 3, 5 implemented, never exercised (no data) |
| play event loops: count n = n+1 plays, 65535 = forever; new variation per loop; loop region | infinite on one wave: `AL_LOOPING` with `AL_SOFT_loop_points` for a partial region; finite or new-variation: one buffer queued per pass | finite loop **numerically checked** (6.231 s for 6.224 s expected); infinite loop checked still sounding after 3 wave lengths; a partial loop region: implemented, not measured (the checked loop is a whole-wave region) |
| Set Volume / Set Pitch events (set, random, stepped fade) | same, evaluated each update | implemented; exercised in game runs (combo loops, weapons) but not measured |
| "Volume" parameter control (:2302-2318) | the cue's RPC curve on its track, X = Volume*65535 | **numerically checked** incl. the step and the 0 dB clamp |
| low-pass/LFO/EG/mix-bin/loop-start events, parametric EQ, LFE send, cones | carried in the cue table, not played (counted per sound) | 4 LPF, 1 multi LFO, 1 pitch LFO, 1 EG, 5 mix-bin, 1 loop-start events and 15 EQ sounds in the whole project are not reproduced. Cones have no effect in the data (outside volume 0) |
| I3DL2 send level on 3D sounds; zone presets (`#if 0` on the Xbox) | EFX aux send gain = `10^(I3DL2/2000)` on 3D voices, none on 2D; one EAX-reverb slot loaded with the zone's preset (or Default with `ZoneReverb=False`) | decay times **numerically checked** (Hangar +0%, StoneRoom +8%, Default +13%); absolute reverb level vs the Xbox hardware unknown |
| voice stealing: lower priority stolen, oldest at equal priority | 64 voices, priority value higher = less important | implemented; "no voice"/"takes the voice of" never logged in the runs so far (never exercised) |
| music: streamed bank, A_Music cue, fade times ignored | streamed Ogg (4 x 32768 frames), A_Music cue's volume and loop, fades honoured (`MusicFades=True`; False = Xbox) | deliberate difference (brief): fades |
| pause: all cue instances pause, music plays on | same, schedules shifted by the pause | implemented, never exercised in a run |
| level change: stop all incl. music, drop non-persistent banks | same, wave banks dropped when no persistent sound bank uses them | exercised at every map load (logged) |
| category volumes from the default profile | same | Music/FX/Voice; VoiceChat stored only |
| ambient: 5 nearest static AmbientSounds within their radius, every second | same | exercised: e.g. 6 ambient loops on DM-Deadbolt |

## Step 7 - capture suite, zone change in game, EFX on/off pair (A4 (2)-(4))

`tools/dev/audio-regression.sh m8` (committed; check + self-test + four captures, one `SUMMARY` line each in
`logs/m8/audio-summary.txt`). The captures pin the spawn (`set UCDeathmatch NumPlayers 0` bound to F1 and
pressed before Fire: the first PlayerStart) and inject a seeded input loop; bots and frame timing still vary
(cue counts differ a little between runs). First suite run (before the Ghost change below), literally:
```
SUMMARY audio-check: 182 index files; waves: 7141 decoded to their exact length of 7141 cooked (19 entries have no cooked wave); music streams: 21 of 21; cues [int]: 2438 in 78 cue tables: 2417 resolve fully, 6 partly, 15 to no decodable wave (0 use a localised bank); UC2AudioCheck: 78 cue tables, 2438 cues, 0 failures
SUMMARY audio-selftest: UC2AudioSelfTest: 39 checks, 0 failed; Pan3D -1.10 dB, Pan2D -1.52 dB
SUMMARY audio-menu: 58.709333 s, peak -13.0 dBFS, RMS -28.5 dBFS, silent seconds 0; cue requests 1: played 1 (1 distinct), culled 0 (0 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-deadbolt: 83.797333 s, peak -3.0 dBFS, RMS -23.0 dBFS, silent seconds 0; cue requests 1105: played 497 (61 distinct), culled 608 (69 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-praxis-efx: 83.818667 s, peak -1.5 dBFS, RMS -20.4 dBFS, silent seconds 0; cue requests 1079: played 547 (56 distinct), culled 532 (62 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-praxis-dry: 83.797333 s, peak 0.3 dBFS, RMS -21.3 dBFS, silent seconds 0; cue requests 1067: played 606 (60 distinct), culled 461 (69 distinct), failed 0 (0 distinct); Critical 0
```
(The menu's "1 request" is `A_Gameplay.MenuPageOpened`; the menu music is not a cue request but a
`PlayMusic`, logged as `music NuBattle: playing ...`. `audio-praxis-dry` peaks at +0.3 dBFS: the float wave
writer does not clip; a 16-bit device would, see doubts.)

Zone change in game: on the walk through DM-Praxis the player stayed in its spawn zone (no zone notification
at all), on DM-Deadbolt one notification, `zone ZoneInfo8 (ZoneAudioEffect 0): reverb preset Default
unchanged` (Deadbolt has only Default zones). `tools/dev/run-uc2-audio.sh` got `UC2_POSTCMD` (a console command
bound to F2, pressed once after the spawn); with `Ghost` the same walk goes through walls. Literal log lines:
```
praxis-zone (EFX):      [  38.96] zone ZoneInfo2: reverb preset Underwater
                        [  55.99] zone LevelInfo0: reverb preset Default
praxis-zone-dry (-NOEFX): [  38.74] zone LevelInfo0 (ZoneAudioEffect 0): no EFX, sound stays dry
                        [  39.23] zone ZoneInfo2 (ZoneAudioEffect 24): no EFX, sound stays dry
                        [  56.16] zone LevelInfo0 (ZoneAudioEffect 0): no EFX, sound stays dry
```
The suite's Praxis pair now uses `Ghost`. The pair (`logs/m8/praxis-zone.wav` / `praxis-zone-dry.wav`, crops
`praxis-zone-30to65.png` / `praxis-zone-dry-30to65.png`; the two runs are close to identical in timing thanks to
the pinned spawn and seeded input): RMS with EFX minus without, by window:
- 10-25 s (before the spawn: music and 3D ambients, Default preset): +0.9 dB
- 39.5-55.5 s (in the Underwater zone): **+2.0 dB** (+2.3 dB below 1 kHz)
- 60-80 s (back in a Default zone, gameplay): +0.1 dB
This is log-correlated only (gameplay is not identical between the two runs); the reverb arithmetic itself is
numerically checked in the self-test (decay times). The spectrogram crops look alike to the eye.

### Music level checked in game

Found while checking the menu capture's level: the music player left out the 6 dB voice headroom every other
cue gets (XACT plays music as a cue of `A_Music`, `Headroom Boost` unset). Fixed. `tools/dev/audio-music-check.py`
(in the suite) correlates the capture's 50 ms level envelope with the source track and measures the mix gain:
```
CHECK music NuBattle.wav in audio-menu.wav: envelope correlation 0.867 at 0.35 s, mix gain -19.02 dB (expected -19.00 dB, error -0.02 dB), control track Recoil.wav correlates 0.115: PASS
```
(-19.0 dB = -900 mB of the A_Music sound - 600 mB headroom + 1000*log10(0.40), the default profile's
MusicVolume through XACT's category formula; the init line now logs the category volumes: `Music 0.40, FX 0.80,
Voice 0.80`.) The cue log puts the music start at 0.23-0.29 s; the correlation finds it at 0.35 s.

## Step 8 - gate runs (d), (e), (b)

- (d) no device: `SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=nonexistent ./uc2.bin -log` (capped 2G 40, Xvfb) ->
  `UC2ALAudio: OpenAL could not open an output device: sound is off` (the only audio line), `Bringing Level
  Menu.MyLevel up for play (0)...`, running until the 40 s limit (`logs/m8/gate-d-nodevice.out`). `-nosound`:
  no audio line, menu up (`gate-d-nosound.out`). `AudioDevice=Engine.NullAudioDevice` in the run tree's
  `Default.ini`: menu up, no audio line, `Avaris.ini` derived with `AudioDevice=Engine.NullAudioDevice`
  (`gate-d-nullaudio.out`; the run tree's ini restored afterwards). Note: this machine has a 32-bit system
  `libopenal.so.1` (1.25.1), so "no library" cannot be produced here without hiding files; that path
  (`UC2ALBind` fails) logs `...: sound is off` the same way, not exercised.
- (e) `./ucc-bin verifyclasses` -> `All native class layouts match`; `timeout 20 ./ucc-bin server DM-Deadbolt
  -log` -> `Game class is 'UCDeathmatch'`, `Bringing Level DM-Deadbolt.MyLevel up for play (0)...`, no audio
  line (a dedicated server has `GIsClient` 0, `UEngine::InitAudio` never creates a device); `run-uc2-shot.sh m8
  gate-e-deadbolt 60 0 DM-Deadbolt -glerrors` -> `UC2GLDrv: frame 2400, 0 GL errors so far`, 0 Critical, audio
  device `"No Output"` (OpenAL Soft's null backend: `run-uc2-shot.sh` now sets `ALSOFT_DRIVERS=null` unless the
  caller sets it, so headless runs never play through the desktop's sound server).
- (b) `cmake --preset native && cmake --build build-native -j12` (capped 6G) -> `Built target UC2ALAudio`,
  `Built target SDLLaunch` (uc2.bin contains `UUC2ALAudioSubsystem::PlaySoundCue`), `Built target UCC`.
  `source env.sh && cmake --preset wasm && cmake --build build-wasm -j6` (capped 6G) -> `[100%] Built target
  SDLLaunch`, rc 0; targets built: ogg, zlib, vorbis, Core, Engine, UnrealGame, UCElements, SDL2Drv, UWeb,
  UCGameplay, IpDrv, GUI, UCC, SDLLaunch (UC2ALAudio excluded there).

## Status at the end of M8 (2026-10-04, ~22:10)

Final suite run (`tools/dev/audio-regression.sh m8` on the final binary, `logs/m8/audio-summary.txt`):
```
SUMMARY audio-check: 182 index files; waves: 7141 decoded to their exact length of 7141 cooked (19 entries have no cooked wave); music streams: 21 of 21; cues [int]: 2438 in 78 cue tables: 2417 resolve fully, 6 partly, 15 to no decodable wave (0 use a localised bank); UC2AudioCheck: 78 cue tables, 2438 cues, 0 failures
SUMMARY audio-selftest: UC2AudioSelfTest: 39 checks, 0 failed; Pan3D -1.10 dB, Pan2D -1.52 dB
SUMMARY audio-menu-music: music NuBattle.wav in audio-menu.wav: envelope correlation 0.861 at 0.30 s, mix gain -19.02 dB (expected -19.00 dB, error -0.02 dB), control track Recoil.wav correlates 0.115: PASS
SUMMARY audio-menu: 58.816000 s, peak -19.0 dBFS, RMS -34.5 dBFS, silent seconds 0; cue requests 1: played 1 (1 distinct), culled 0 (0 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-deadbolt: 83.861333 s, peak -4.0 dBFS, RMS -24.1 dBFS, silent seconds 0; cue requests 998: played 510 (60 distinct), culled 488 (64 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-praxis-efx: 83.776000 s, peak -0.8 dBFS, RMS -23.0 dBFS, silent seconds 0; cue requests 1053: played 346 (45 distinct), culled 707 (87 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-praxis-dry: 83.264000 s, peak -3.1 dBFS, RMS -25.3 dBFS, silent seconds 0; cue requests 936: played 230 (39 distinct), culled 706 (81 distinct), failed 0 (0 distinct); Critical 0
```
Zone lines of that run: EFX `[41.46] zone ZoneInfo2: reverb preset Underwater`, `[57.14] zone LevelInfo0:
reverb preset Default`; dry `[41.48] zone ZoneInfo2 (ZoneAudioEffect 24): no EFX, sound stays dry`.

Deferred: runtime `.xsb/.xwb` loading (M16; the readers in the cook got far enough for the cross-checks: header,
cue names, sound records' volume/priority, wave bank entry formats and lengths); the web build of the package
(M11: openal-soft's Web Audio backend; the code reads files through `GFileManager`, binds OpenAL through one
table, uses no threads of its own); the Xbox voice-processor events (LPF, LFOs, EG, mix bins, loop start,
parametric EQ, LFE) and HRTF; split screen (implemented as on the Xbox, never run).

Doubts (also in BLOCKERS.md where the owner decides):
- Priority direction (lower = more important) and the default of an unset variation weight are inferred; voice
  stealing never happened in any run (64 voices were enough), so it is untested in practice.
- OpenAL Soft's centred gain (-1.10 dB 3D, -1.52 dB 2D) and XACT's 5-speaker/HRTF routing differ by an unknown
  constant; the absolute mix level is therefore only "right to within a dB or two" against the Xbox.
- One capture peaked at +0.3 dBFS (float wave writer, no clipping there); on a 16-bit device OpenAL Soft's output
  limiter applies. The Xbox would have had its own headroom behaviour; not compared.
- Non-3D cues on world actors are culled beyond 4 units (DirectSound defaults per the XACT white paper). If the
  Xbox reported a different radius for them, some sounds of other players (e.g. their combo activations) that
  are silent here were audible there.
- `Recurring = 1` read as "fade" rests on the data pattern, not on documentation.
