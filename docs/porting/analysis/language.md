# Language option (E-4): one launcher choice, every engine's language

Running notebook of the `e4-language` branch (worktree `../engine-uc2-e4`), started 2026-10-08 from main's tip
`4813dd9`. Brief (coordinator, verbatim from the owner): "add a localization option in the advanced section that
would change the languages of ALL OF THE ENGINES to the one selected. That last part ought to be data option that
says, what do we need to change (likely the ini command we need to launch) and the options with a key (language)
value (what to put in the ini such as "English": "int"). Lets port the ini loader suport to UC2 to make this
happen." UC2 already has `-ini:<File>:[<Section>]:<Key>=<value>` (C-1, `web-ini-saves.md`). For UC2: the leaked
tree's config and localisation (int, and whatever other languages `epic-sources/System` and the cooked assets
actually carry).

Rules for this notebook: appended after every step and committed, so a successor can continue. Frontend
(`/home/ion/WorkUC2/ufrontend`, branch `uc2`, shared) is committed with exact paths only; other engines
(`/home/ion/WorkUnreal`) are read-only, their engine-side needs go to `logs/e4/<engine>.patch`.

## Plan

1. Read the frontend rule (`iniSettings.ts`, frameRateCap/audioHrtf) and the UC2 language plumbing. (this entry)
2. Per target: where the engine reads its language, whether `-ini:` is applied BEFORE that read, which
   languages the shipped assets carry (evidence). Table below, filled in as it is established.
3. UC2 engine side: language reaches menu/HUD text, localised audio banks, movie tracks, loading screens; native
   runs with `-ini:System:[Engine.Engine]:Language=<x>` (log + GPU screenshot).
4. Frontend: target data `language`, pure seed function + unit tests, Advanced control, fill-from-ini.
5. Verification: `yarn test`, browser spec for UC2 (non-English and back), data + unit tests for the other
   targets, one browser boot per other target whose artifact and assets exist read-only.

## Step 1 - what the rule is, and what is already in the frontend (2026-10-08)

Read: `ufrontend/packages/frontend/src/config/iniSettings.ts` (THE RULE), `test/ini-settings.test.ts`,
`components/AdvancedPanel.vue`, `packages/e2e/lib/ini-settings-test.js`, `vite.config.ts` + `env.d.ts` (a target's
`frameRateCap` / `audioHrtf` is baked as `__UE_FRAME_RATE_CAP__` / `__UE_AUDIO_HRTF__`).

The mechanics a new ini-backed setting touches today (to be copied for `language`):
- `targets/<id>/target.json`: the data block (`iniPath`, `iniToken`, `section`, `key`, + the setting's own fields).
- `packages/frontend/vite.config.ts` (~70-74, 179-181): `_descriptor.<block> || null` -> `define: __UE_<NAME>__`.
- `packages/frontend/env.d.ts` (~111-129): the global's type.
- `packages/frontend/src/stores/play.ts` (57-66, 127-130, 195): the control's reactive value + its reset.
- `packages/frontend/src/config/iniSettings.ts`: `IniSetting<T>` factory, `resolveIniSettings` (fill from the ini),
  `iniSettingSeeds` (the seeds), `isIniSettingSwitch` (re-entry never stacks two).
- `packages/frontend/src/components/AdvancedPanel.vue`: the control. `controller.ts` appends `iniSettingSeeds()`.
- unit tests: `test/ini-settings.test.ts` (pure `iniSeed`), `config-controller.test.ts:68` mirrors the seeds,
  `config-playstore.test.ts` (nothing stored: no `frameRateCap`/`hrtf` in localStorage).
- e2e: `packages/e2e/lib/ini-settings-test.js`, registered by each target's boot spec.

UC2's own `targets/uc2/target.json` declares NEITHER `frameRateCap` NOR `audioHrtf` (only `audioBuffer`), so UC2 is
the first UC2 target block of this kind; the rule's code path is exercised by the other four targets today.

Next: the UC2 engine's language read and the localisation files in the tree.

## G-4 (2026-10-10): the task restarts as G-4 on branch `g4-language`

Owner review of 2026-10-10 (PLAN.md) restarts E-4 as G-4. Worktrees: engine `../engine-uc2-g4-language` (branch
`g4-language` from main `4a72bfb`, the E-4 step-1 commit cherry-picked), frontend `../ufrontend-language` (branch
`uc2-language`, notebook `docs/migration/notes/language-option.md` there). Logs: `<WorkUC2>/logs/g4`.

## Step 2 - where each engine reads its language, and which languages the shipped files carry (2026-10-10)

Every engine reads `[Engine.Engine] Language` of its main ini (the `System` token of `-ini:`) once, in `appInit`,
AFTER `appApplyIniOverrides()`, so a launcher seed `-ini:System:[Engine.Engine]:Language=<ext>` is in effect before
the read. No engine needs a patch for the read itself (no `logs/g4/<engine>.patch`).

| target | read (file:line) | ini (`iniPath`) | shipped default |
|---|---|---|---|
| uc2 | `Source/Core/Src/UnMisc.cpp:2209-2210` (non-Xbox branch; overrides at :2194) | `System/Avaris.ini` | `Language=int` (`Engine/Config/Default.ini:29`, web copy the same) |
| ut2004 | `engine-ut2004/Source/Core/Src/UnMisc.cpp:2340-2342` (overrides :2337) | `System/UT2004.ini` | `Language=int` (`Default.ini:32`; `:437 Language=ute` is UnrealEd's own section) |
| ut99 | `engine-ut99/Source/Core/Src/UnMisc.cpp:1841-1842` (overrides :1837) | `System/UnrealTournament.ini` | `Language=int` (`Default.ini:26`) |
| ut99-436-compat | `engine-ut99-436-compat/Source/Core/Src/UnMisc.cpp:1939-1940` (overrides :1935) | `System/UnrealTournament.ini` | `Language=int` (`Default.ini:26`) |
| unreal1 | `engine-unreal1/Source/Core/Src/UnPlat.cpp:427-428` (overrides :424) | `System/Unreal.ini` | `Language=int` (`Engine/Config/Default.ini:46`) |

Fallback when a string is missing in the chosen language: all five fall back to `.int` per key (UC2
`LocalizePropertyValue`, `Core/Src/UnMisc.cpp:1230-1245`, folder `Localization\<lang>\` then
`Localization\int\`; UT2004 `Localize`, `UnMisc.cpp:1474-1520`), so a partial language shows English for the rest.

Languages each target's shipped files carry (counted in the frontend's `targets/<id>/file-manifest.json`, `System/`
files by extension; "core" = the engine/game packages the menus come from):

| target | `.int` | other languages (files) | offered |
|---|---|---|---|
| uc2 | 77 (`System/Localization/int/`) | deu 66, fra 66, ita 66, spa 67 (`System/Localization/<lang>/`); the 11 without a copy are editor/tool packages (Window, IpDrv, Setup, ...); plus 21 voice banks `Sounds/<Bank>_<lang>.{uc2wb,ogp}` and 19 movie tracks `Movies/<Movie>_<lang>.oga` per language | English int, German deu, French fra, Italian ita, Spanish spa |
| ut2004 | 85 | det 36, frt 36, itt 35, est 37 (core present, except `Core.det`, `IpDrv.*`); kot 20 (partial: no Engine, XInterface, UnrealGame, XWeapons, XPickups, XPlayers `.kot`) | English int, German det, French frt, Italian itt, Spanish est. Not Korean: its menus would be mostly English and the fonts are not Korean |
| ut99 | 29 | none | English only |
| ut99-436-compat | 140 | det 13, est 13: only the MonsterHunt mod's files (no Core/Engine/Botpack/UnrealShare/UnrealI copy) | English only |
| unreal1 | 18 | none | English only |
