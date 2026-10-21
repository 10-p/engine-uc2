Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# X-2: sound banks loaded lazily on the web (every first use stalled the match)

Branch `x2-sounds` (from main `a16ad39`). No engine code change: the fix is frontend data and a frontend generator
(ufrontend branch `uc2-x2-sounds`, commits `b53e40c` and `3db4481`; its notebook
`docs/migration/notes/uc2-sound-precache.md` has the bank table and every number). Logs: `<WorkUC2>/logs/x2`.

The owner (2026-10-11): sound banks load on first use, once per character's sounds and again per taunt, "at least
12 significant hitches while in the first map". His choice: option C (precache), always-needed banks appended by
hand to `targets/uc2/precache.json`, per-map banks and music as map dependencies in the generated dependency manifest.

## The engine's path (UC2ALAudioSubsystem.cpp, unchanged)

- Cue `<Bank>.<Group>.<Cue>` -> `ResolveCue` (:517) -> `GetSoundBank` (:249): `Sounds/<Bank>.uc2cues`, once per bank.
  The 23 `GPersistentBanks` (:41) are read at device open.
- First play -> `ResolveVariation` -> `GetWaveBank` (:300): `<Bank>_<lang>.uc2wb`, else `<Bank>.uc2wb`; then
  `EnsureBuffer` (:373): the whole `<Bank>.ogp` (`read wave pack` line, NAME_Log).
- `PlayMusic(Song)` (:1724): the A_Music cue `Song` -> `M_<Song>.uc2wb` -> `../Music/<Song>.ogg`, read whole. A map's
  song is its LevelInfo's `Song` string (DM-Deadbolt: `Recoil`).
- Web: each read is a FetchFS read; an uncached file is downloaded while the engine thread waits.

## Measured (frontend opt-in spec `uc2-wasm-sounds.spec.js`, DM-Deadbolt, 5 bots, 120 s, localhost)

- Before: 29 Sounds/Music files (20.84 MiB) fetched after the level was up: the music and the map's ambience at
  +0.2/+2.3 s, then A_Weapon, A_Ann_Female, A_WeaponRace, A_Character, A_WeaponMelee, A_Combo, A_Pickup, four taunt
  banks and A_Announcer at their first use (+6 to +115 s): 15 stalls; the engine's `read wave pack` 16-52 ms each on
  localhost (371 ms for 14 packs).
- After: 0 Sounds/Music fetches after the level is up, on DM-Deadbolt and DM-Decay; the map's 5 sound files (song,
  its M_ index, the ambience bank's three files) arrive with the map's prefetch 1.3-2.2 s before the level is up;
  pack reads 3-14 ms (from the cache).
- Still fetched in the match (not sound): `Animations/K_Female.ukx`, `K_Male.ukx`, `K_Weapons.ukx` (26.3 MiB) 2-5 s
  in, loaded by class at spawn. Open question for the owner.

## Option B (skip a sound whose bank is not cached, fetch it in the background): not one line

The reads are synchronous `appLoadFileToArray`/`appLoadFileToString` (UC2AudioData.cpp `UC2LoadSoundBank` :121,
`UC2LoadWaveBank`; EnsureBuffer :377). Skipping needs an engine-callable "is it cached?" and "fetch it async" in the
glue ABI (none exists: the engine thread has `Module.UE.pkgguid` and the FS hooks; prefetch is main -> cache worker
only), plus retry state at three sites: `ResolveCue` caches a failure as -1 in `CueMap` for the session,
`GetWaveBank` logs once and does not retry, `EnsureBuffer` sets `bDataTried`. An ambient loop started without its bank
would stay silent for the level. Not implemented.
