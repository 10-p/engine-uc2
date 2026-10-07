# D-3 / D-4 / D-5 notebook: profiles that come back, the player name, text entry

Running notebook of the `d3-profiles` branch (worktree `../engine-uc2-d3`), started 2026-10-07 from main's tip
`421cb95`. Brief: PLAN.md "Owner review of 2026-10-06", Phase D, tasks D-3 (saving), D-4 (player name vs profile
name), D-5 (text entry). Builds on C-1/C-2 (`analysis/web-ini-saves.md`) and M10 (`analysis/controls.md`).
Frontend work happens in `<WorkUC2>/ufrontend` (branch `uc2`, local commits). Appended after every step.

## Step 0 (2026-10-07): what the code says before any change

### D-3: how a profile becomes active, and what Epic already does about it

- Every viewport starts with a TRANSIENT profile (`UViewport::Init`, Engine/Src/UnCamera.cpp:166-189:
  `ConstructObject<UPlayerProfile>` + `DirtyMask |= kTransientProfile`). The initial viewport is made in
  `UGameEngine::Init` (Engine/Src/UnGame.cpp:235, `Client->NewViewport`), before the GUIController and the
  Entry PlayerController exist.
- A profile is made active by `APlayerController::execLoadProfile` (UnController.cpp:1070: `UPlayerProfile::
  LoadProfile` + `eventSetProfileName` + `eventSetPlayerOwner`/`eventInitialize`), by `PlayerProfile.NewProfile`
  (script: `SetProfileName`, `DirtyMask = 1`, native `SaveProfile`), copy/rename (`NewProfile(bCopy|bRename)`:
  rename deletes the old save first), and it becomes transient again by `ClearProfile` (script, the
  "no profile" choice, UC2PageBase.LoadProfile("")).
- Epic's only automatic selection: `UC2PageBase.ForceProfileLoad` (UCMenu/Classes/UC2PageBase.uc:316-379), run
  when a page with `bRequiresProfile` opens (Instant Action, the character page...): if the profile is transient
  and exactly ONE saved profile exists it is loaded; with several, the "no profile" question box asks the player
  to pick one (once per session, `bWarnedPlayerOne`). Nothing at start-up; nothing remembers the last profile.
- Searched for an Xbox "last profile" notion (`LastProfile`, `RecentProfile`, `defaultProfile*`, the Xbox
  load/save manager `XboxDrv/Src/XboxLoadSaveManager.cpp`, StartMenu's sign-in): none. The Xbox persists only the
  profiles themselves and a "disconnect" flag file. The `[Engine.PlayerProfile] defaultProfileDirectoryPath` /
  `defaultProfileExtension` keys in Default.ini are dead (read nowhere in Source/: the PlayerProfile.uc header
  comment describes an older design that kept profiles as ini sections). `UC2PageBase.LoadProfile`'s
  `LastProfileName` is a local (reload the previous profile if the new one fails), not persisted.
- So "start in the last used profile" is new behaviour; it has to remember a name somewhere the web persists
  (User.ini, through `flushconfig`) and load it before the menus look at the profile.

### D-4: where the name comes from (facts in web-ini-saves.md step 4, re-read here)

- No profile: `GUIController.SetProfilePlayerName` (GUI/Classes/GUIController.uc:2746-2810) takes the
  CHARACTER's name for a transient profile (2759-2768) and stores it in the GameStartInfo (`?Name=` of every URL a
  menu builds) and, in the menu level, `PC.SetName`. Script (GUI.u): changing it needs a rebuilt GUI.u.
- `Level.GetDefaultURL("Name")` (native, UnScript.cpp:255) answers User.ini `[DefaultPlayer] Name` as the config
  cache holds it now (so with the frontend's `-ini:User:[DefaultPlayer]:Name=` override applied) - the value a
  transient profile should use instead of the character's name.
- `UGameEngine::LoadMap` (UnGame.cpp:2893-2906, `#ifndef _XBOX`: PC code) writes every loaded URL's `Name` back to
  `[DefaultPlayer]` - with a profile active that is the PROFILE's name, which the frontend then reads back as
  "the player's name" (`readPersistedPlayerName`, ufrontend glue/src/config/ini-identity.js). Engine code.
- Frontend: the name field is `#net-name` (packages/frontend/src/components/NetworkPanel.vue:84-92, also
  ProxyNetworkPanel.vue:205); the engine's ini is its only source of truth (stores/net.ts `resolvePlayerName`);
  nothing can disable the field today.

### D-5: the text-entry pages

- `UCMenu.UC2ProfileKeyboard` (a `UC2Keyboard`): new profile (UC2Profile.uc:695, UC2OverlayProfile.uc:96,
  UC2SP_EntryPanel.uc:102), copy (UC2Profile.uc:707), rename (UC2Settings_Main.uc:90).
- `UCMenu.UC2Keyboard`: the server name when hosting (Arena_PanelServerMain.uc:992, X on the server-name rule) and
  mid-game (UC2MidGame_Config.uc:107).
- In-game chat (`Talk`/`TeamTalk`, T) and the console (`Type`) are `Engine.Console`'s `Typing` state
  (Engine/Classes/Console.uc:80-): Escape clears, then leaves; Enter on release runs. Console code is D-7's area
  (another worktree): tested here, changed only if D-5 needs it and then coordinated.
- No IP-entry page exists (System Link / Live only; `open` is built by the menus, UC2PageBase.uc:928).
- B on a keyboard page = `BeginCancelAnimation` -> `CloseMenu(true)` (cancelled: the caller ignores the text);
  Start = `pnl_Keys.StartClicked` (done). M10's table maps Escape to B and Enter to Start on a text page.

### Plan (decided here, reasons above)

1. D-3 native, ENGINE code (works with the shipped packages, so the web gets it without the package switch):
   the primary viewport's profile is remembered in User.ini (the profile active when the player last played:
   a name, or empty for "no profile"), and `UGameEngine::Init` loads it into the initial viewport's profile
   right after `NewViewport`, exactly as `execLoadProfile` does (load + `SetProfileName`; the PlayerController
   binds it later through `SetGamepadID` -> `SetPlayerOwner` -> `Initialize`). Recording: the engine compares
   the primary viewport's profile with the remembered name once per tick and writes the key only when it
   changes, so every script path (load, new, copy, rename, delete, "no profile") is covered without hooks in
   script. Restart test: `tools/dev/c1-saves.sh` extended / a d3 script.
2. D-3 web: browser spec (create a profile with the keyboard, flush, reload, profile active).
3. D-4: GUI.u `SetProfilePlayerName` transient branch uses `[DefaultPlayer] Name`; engine: `SaveURLConfig("Name")`
   skipped while a real profile is active; frontend: the name field disabled while the remembered-profile key
   is non-empty (target data names the key).
4. D-5: every keyboard page natively + browser.

Baseline (before any change): native build of main's tip in this worktree, `bin/capped 6G 3600 -- cmake
--preset native && cmake --build build-native -j6` rc 0 (`logs/d3/build-native-1.log`); run tree
`tools/dev/mkrun-uc2.sh build-native d3`; `C1_TREE=d3 C1_OUT=<WorkUC2>/logs/d3/saves-baseline
tools/dev/c1-saves.sh` (result below in step 1).

## Step 1 (2026-10-07): D-3 native, engine code: start in the last used profile

Baseline before any change (`logs/d3/saves-baseline.txt`): `tools/dev/c1-saves.sh` on tree d3 ALL PASS (run 2 starts
on `$$Default$$` and needs the Profiles page to get "Saver" back), i.e. the behaviour the owner saw.

Change (`Engine/Src/UC2Profiles.cpp`, new; `Engine/Src/UnGame.cpp` +8 lines, two calls):
- `UC2StartWithLastProfile(Viewport)`, called in `UGameEngine::Init` right after `Client->NewViewport` (UnGame.cpp:241):
  reads User.ini `[Profiles] LastProfile`; if that profile exists (`ULoadSaveManager::ExistsGameObject`) it is loaded
  into the new viewport's (transient) profile object with `UPlayerProfile::LoadProfile` + `eventSetProfileName`, the
  same two calls as `APlayerController::execLoadProfile`; the PlayerController binds it at its first `SetGamepadID`
  (UnLevAct.cpp:920, every `SpawnPlayActor` on PC) -> `SetPlayerOwner` -> `Initialize` -> `InitializeProfileValues`,
  which applies every setting to the player. Logs `Profiles: started with the last profile "<n>"`, or why not.
- `UC2RememberProfile(Client)`, called first thing in `UGameEngine::Tick`: the primary viewport's profile name ("" while
  transient) vs. the remembered one, case-sensitive; on a difference `GConfig->SetString` + `GConfig->Flush(0,"User")`
  and `Profiles: remembered profile "<new>" (was "<old>")`. So the key follows every script path (select, create,
  copy, rename, delete, "no profile") with no script change, and works with the SHIPPED packages (the web too).
- Why User.ini, why `[Profiles]`: per-player state, persisted by the web host with the other engine inis (flushed on
  `flushconfig`); a section that is no class's name, so no class config reads or rewrites it. Only viewport 0: a
  split-screen second player keeps Epic's behaviour.

First run of the new test (`tools/dev/d3-profiles.sh`, `logs/d3/d3-profiles-run1.txt`): the mechanism works
(`r2-engine-started-with-saver`, `r2-saver-active-before-any-menu-action: first answer: Saver`, `r4-user-ini-follows-the-
active-profile`, `r5-starts-with-what-r4-ended-with`), the scenarios themselves needed fixes: the Controller page's
focus is the inner `GUICheckBoxButton` (seek 'CheckBox', the first one is Invert); the menu level's PlayerInput is
Engine.PlayerInput, whose `UpdateInvert` is empty, so "the setting is applied" is checked in a map: new run r2m
starts straight into DM-Deadbolt (as the launcher's Play form does) and reads `UCPlayerInput bInvertMouse`.

Next: rerun the fixed script; then D-4 (GUI.u transient name, `SaveURLConfig("Name")` skipped while a profile is
active: already written in the working tree, not committed in this step).
