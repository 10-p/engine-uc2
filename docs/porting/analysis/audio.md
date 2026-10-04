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
