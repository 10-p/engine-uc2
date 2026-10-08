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
