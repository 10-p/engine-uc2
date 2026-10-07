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

## Step 2 (2026-10-07): D-3 test green except one name check; a Core bug; D-4 written

`tools/dev/d3-profiles.sh` runs 3 and 4 (`logs/d3/d3-profiles-run3.txt`, `-run4.txt`): r1, r2 (start in "Saver"
without any menu action, invert and character back, the menu's PlayerReplicationInfo named Saver), r2m (a direct
map start: `UCPlayerInput bInvertMouse = True`, i.e. the profile's setting is APPLIED to the player), r3 (Second, the
last used, is active; selecting Saver is remembered) PASS.

**Core bug found by r4 (Delete on the Profiles page did nothing):** `FFileManagerGeneric::DeleteFiles` cut the
directory off its pattern at the last `\` only; with `/` the pattern stayed, every file was deleted as
`<dir>/*/<file>`, the directory stayed non-empty, `DeleteDirectory(Tree)` failed, `DeleteGameObject` returned false
and the page's "unable to delete" box was itself refused ("Cannot open new menu until previous menu initialization has
completed!"). Fixed (`Core/Inc/FFileManagerGeneric.h`, last separator of either kind; commit 10b802f). Run 4 after the
fix: r4 deletes Saver, the engine remembers "" (no profile), r5 starts without a profile. (The web keeps profiles as
flat files deleted with `GFileManager->Delete`: not affected.)

**There is no "no profile" choice in UC2's UI once a profile exists**: the Profiles page always checks the
highlighted profile (`ManageProfilePanel.SelectCurrentProfile`), `UC2Profile.ClearProfile` is never called; only
deleting the active profile (or `ForceProfileLoad`'s "continue without" with several profiles and none loaded)
leaves a player without one. Same on the Xbox: with exactly one profile, `ForceProfileLoad` always loads it.

### D-4 (written in this step; tests running)

- GUI.u, `GUIController.SetProfilePlayerName` (GUI/Classes/GUIController.uc:2757-2781): a transient profile's name is
  `PC.Level.GetDefaultURL("Name")` (User.ini [DefaultPlayer] Name = the launcher's `-ini:`/`?Name=`, kept by the engine
  from the last map loaded without a profile); only if that is empty, the character's name (Epic). The only GUI.u
  function that changes: `script-diff.py` per-function list vs M14's = exactly `GUIController.SetProfilePlayerName`
  (684 -> 752 bytes; it is one of Epic's load-time patches, so the compare reports it as a finding against the patch's
  bytecode). Also restored `config` on `bUnlockCharacters`/`bShowAllMuts` (M14's two GUI findings are gone: the
  rebuilt properties now carry the shipped flags). Build: `tools/dev/script-build.sh --tree d3-make`
  (`logs/d3/script-build-1.txt`: every package problems 0; GUI findings 1 = SetProfilePlayerName).
- Engine, `UGameEngine::LoadMap` (UnGame.cpp, `#ifndef _XBOX` block): `SaveURLConfig("Name")` only while no saved
  profile names the player (`UC2ProfileNamesThePlayer`), so [DefaultPlayer] Name stays the player's own name.
- Engine, `MatchViewportsToActors` (UnGame.cpp): the primary viewport's PlayerController is spawned with a copy of the
  URL whose `Name` is the active profile's (`UC2ProfileNameInURL`). Needed because the profile's name had reached a
  directly started map only by leaking into [DefaultPlayer] (r2m failed once the leak was closed: `?Name=Player`,
  PlayerReplicationInfo "Player-1"): the GUI binds `SetPlayerName` only when a menu is first shown.
- Frontend (ufrontend 52e993d, 9a1c80a, d23794f): `network.playerName.ownedBy` (generic, data-driven), the uc2 target
  names `User.ini [Profiles] LastProfile`; the field is disabled while it is set. Unit tests: glue 726 pass (4 new),
  frontend 371 (5 new), root 28, build 185; gates no-engine-specifics, lint, abi-parity clean.
- Test: `tools/dev/d4-names.sh` (n1 no profile through the character page into a match, n2 profile created, n3 direct
  map start with `?Name=`, n4 profile deleted), on tree d3s = `UC2_SCRIPTS=<WorkUC2>/run/d3-make/System
  tools/dev/mkrun-uc2.sh build-native d3s`.

## Step 3 (2026-10-07): D-3 and D-4 native green

- `tools/dev/d3-profiles.sh` run 5 (`logs/d3/d3-profiles-run5.txt`, tree d3 = shipped packages, binary with every
  engine change): **d3-profiles: ALL PASS** (r1 19 checks, r2m 1+3, r2 9+8, r3 5+2, r4 2+3, r5 1+2 file checks).
  r2m now `PlayerName = Saver` through the login URL (MatchViewportsToActors), with `?Name=Player` in the URL.
- `tools/dev/d4-names.sh` run 1 (`logs/d3/d4-names-run1.txt`, tree d3s = rebuilt packages): **d4-names: ALL PASS**
  (n1 11+2: the menu, the character page after two character changes, the match URL `...?Name=FrontName` and the
  match's PlayerReplicationInfo are all FrontName, User.ini keeps FrontName; n2 7+2: the new profile names the
  player at once, User.ini [DefaultPlayer] Name stays FrontName, LastProfile=Saver; n3 3+2: a direct start of
  `DM-Deadbolt?Name=FrontName` with the profile: PlayerReplicationInfo `Saver`; n4 4+2: deleting the profile gives
  the player FrontName back, LastProfile empty).
- Frontend e2e spec written (ufrontend b9fb10e, `packages/e2e/uc2-wasm-saves.spec.js`, opt-in), not yet run.

Next: the same D-4 script on the shipped packages (evidence of Epic's behaviour), D-5 native scenarios, then the
browser runs (wasm-perf rebuilt with every engine change).

## Step 4 (2026-10-07): Epic's behaviour on the shipped packages, recorded; D-5 script; first browser run

`D4_TREE=d3 D4_OUT=<WorkUC2>/logs/d3/names-shipped tools/dev/d4-names.sh` (shipped packages, every engine change;
`logs/d3/d4-names-shipped.txt`): 4 FAILED, all in GUI.u's part: without a profile the player is `Anubis` already in
the menu level, `Sapphire` after the character page, the match URL carries `Name=Sapphire` and User.ini
[DefaultPlayer] Name becomes `Sapphire`; n2's start and n4's "the launcher's name is back" show `Anubis` too. The
engine-only parts PASS on the shipped packages: a profile names the player (n2, n3 with `?Name=FrontName` in the URL),
[DefaultPlayer] Name is not overwritten by the profile's name. So until the web gets the rebuilt GUI.u, a browser
player WITHOUT a profile is named after the character, and WITH one after the profile (the owner's rule holds there).

`tools/dev/d5-text.sh` (k1 new profile: exact text, Backspace, Escape cancels, Enter makes it; k2 rename: Escape keeps,
Enter renames and the remembered profile follows; k3 copy; k4 the host's server name on UC2Keyboard; k5 chat in a
match: text, no movement while typing, Enter sends, one Escape cancels). k4's path through the host pages is a first
guess (an exploration run starved at load 30: another agent's software-GL game used 4 cores; game runs are done one
at a time from here).

Browser: wasm-perf rebuilt with every engine change (`logs/d3/build-wasm-perf-2.log`, 0 errors); `yarn build --target
uc2 --engine-artifact <this worktree>/build-wasm-perf/Source/SDLLaunch` (`logs/d3/fe-build-1.txt`); null sink `uc2d3`;
`PULSE_SINK=uc2d3 bin/capped 6G 1800 -- node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=14 -- --target uc2
uc2-wasm-saves.spec.js` (`logs/d3/e2e-saves-1.txt`).

## Step 5 (2026-10-07): the browser's own file layer works; the spec's engine questions needed ueExecResult

First browser run (`logs/d3/e2e-saves-1.txt`; serve-web from this worktree's wasm-perf, shipped packages): what C-2
could not show under Node is now seen in the browser:
- the engine CREATES `/System/Profiles/` and writes `/System/Profiles/WebSaWdX.PRO` (56,951 bytes) under WasmFS (the
  feared EACCES of `mkdir` below the preloaded `/System` did not happen);
- the persistence daemon puts the profile into IndexedDB on its own clock, and `User.ini` with `[Profiles]
  LastProfile=WebSaWdX` (the `flushconfig` round trip);
- after the tab is closed and another opened: the profile is back in the engine file system byte for byte; the
  launcher's name field is DISABLED with "Your game profile “WebSaWdX” names you in the game ..." (ownedBy); the
  engine logs `Profiles: started with the last profile "WebSaWdX"`; User.ini keeps `[DefaultPlayer]
  Name=Player-c82644` = the launcher's name (D-4 engine part);
- D-5 in the browser: Playwright's real keyboard typed `WebSaWdXq`, Backspace, Enter: the engine made the profile
  `WebSaWdX` (exactly the typed text minus the deleted letter: the remembered name proves it), 0 GUI navigation lines
  while typing; Escape on a second keyboard page closed it and made nothing.
- The spec's `getall` questions all came back empty: `ueExec` is fire-and-forget and `getall` writes to the
  command's output device, not the log, so its lines never reach the console. Fixed in the spec: `ueExecResult`
  (the engine's own answer). Also: without a profile the SHIPPED GUI.u names the player after the character, so that
  check runs only with `UC2_E2E_REBUILT_GUI=1` (a served tree with the rebuilt GUI.u), else it is recorded.

Prepared for a browser run with the rebuilt GUI.u (the D-4 GUI part): `<WorkUC2>/run/d3-webassets` = hard links into
the asset tree with only `System/GUI.u` replaced by the rebuilt one (never written in place: the link was removed,
the asset tree's file is untouched); its manifests `logs/d3/web/file-manifest.json` / `dependency-manifest.json`
(generated with the glue's own tools; against the committed manifest only `system/gui.u` differs).

## Step 6 (2026-10-07): D-5 natively: two keyboard defects found and fixed in the engine (GUI C++)

`tools/dev/d5-text.sh` run 1 (`logs/d3/d5-text-run1.txt`, tree d5 = shipped packages). The exploration of the host
pages first failed twice for a silly reason worth knowing: the scenario was called `explore-server`, the test
channel's file name `-inputtest=.../explore-server.channel` contains `-server`, and `ParseParam(cmdline, "SERVER")`
matched it: the game started as a dedicated server (no client, no menu). Never put `-server` in a test file name.

Results of run 1:
- k1 new profile: PASS 13 (exact text `WaSdXq`, Backspace deletes one, no GUI navigation while typing, Escape closes
  the page and makes no profile, the Profiles page closes too when none exists (Epic: UC2Profile.InternalOnReopened),
  Enter makes "Typer").
- k2 rename / k3 copy: the flows work (rename to the typed name, the engine's remembered profile FOLLOWS the rename:
  `Profiles: remembered profile "TypeRenamed" (was "Typer")`; Escape keeps the old name; copy makes the copy), but
  **Backspace on the preselected name deleted ONE letter** ("Typer" -> "Type", then typing appended: "TypeRenamed").
  The page opens with the whole text selected (typing replaces it: "Nope" replaced "Typer"), and GUIEditBox's Backspace
  ignores a selection (Epic: the on-screen keyboard's own Backspace key, one letter for the pad). Copy names are cut
  at 15 characters (the profile keyboard's limit).
- k4 server name (Network -> Host -> DeathMatch -> CUSTOMIZE on -> SESSION NAME -> X): Escape keeps the old name, Enter
  sets the new one (the page reopens on `(Mine)`), but **the X that opened the keyboard was typed into it**: the field
  showed "x" instead of "Liandri Corporation". SDL delivers a key's character after its press; the press opened
  UC2Keyboard synchronously (`Arena_PanelServerMain.XClicked` -> OpenMenu), so the character reached the new page.
  (Pages opened through an animation, New/Copy on the Profiles page, were not affected: the character came first.)
- k5 chat (T in a match): PASS 9 of 10: exactly `Say wasd Hi`, W/A/S/D do not move the player while typing, Enter runs
  the line and closes, ONE Escape closes the chat and discards the text (Epic's Console clears on the press and closes
  on the release, as D-7's notebook explains; the leak of that release into the page below is D-7's fix 868f41d, not
  in this branch), keys move the player again. The one FAIL was my check ("the message was said": UC2 logs no chat);
  now the console history is read instead.

Fixes (both in `GUI/Src/UnGUICore.cpp`, the M10 text-entry routing, engine C++; no script, so the web gets them with
the next wasm build):
1. A real Backspace on a field whose whole text is selected is given to the field as Delete (GUIEditBox's Delete clears
   a selected text): Backspace on the preselected name clears it, as in any PC text field. The on-screen keyboard's own
   Backspace key (pad) is unchanged.
2. `GPadKeyHeld`: a character key whose press acted as a pad button while no text field was up is remembered until its
   release (or any other press); its character is dropped (logged `Dropped 'x': its key opened ...`) if it arrives at a
   keyboard page. File-static: no class layout changes.
Native build `logs/d3/build-native-5.log` (0 errors). Run 2 of d5-text.sh started (`logs/d3/d5-text-run2.txt`).

## The switch to rebuilt packages: what this work needs, and a proposal (2026-10-07)

What needs a rebuilt package: ONLY the D-4 part "without a profile the player keeps the launcher's name" (GUI.u,
`GUIController.SetProfilePlayerName`). Everything else in D-3/D-4/D-5 is engine C++ and reaches the web with the next
wasm build: starting in the last profile, remembering it, the profile naming the player in a directly started map,
[DefaultPlayer] Name not overwritten by a profile, deleting a profile on Linux, the two keyboard fixes.

What the rebuilt GUI.u changes (`tools/dev/script-build.sh --tree d3-make`, `script-diff.py` per function against
the shipped packages and against M14's list): one function, `GUIController.SetProfilePlayerName` (684 -> 778 bytes);
the properties `bUnlockCharacters`/`bShowAllMuts` carry the shipped `config` flag again (M14's two GUI findings
are gone); every package problems 0; make 0 errors, 85 warnings (as M14).

Proposal, smallest first:
1. **Overlay GUI.u only** (recommended now). `tools/dev/mkassets-uc2.sh` (or a small sibling) takes
   `UC2_SCRIPTS=<script-build tree>/System` and a package list (`UC2_SCRIPT_PACKAGES="GUI"`), moves the shipped
   `System/GUI.u` to `assets/uc2-shipped/System/GUI.u` (kept, for the M14 comparison and as the fallback) and copies
   the rebuilt one in. Why it is enough and safe:
   - GUI is not a server package (`Server Package:` lines: GamePlay, UnrealGame, UCAnnouncements, UCGameplay,
     UCEffects, UCCharacters, UCInventory, UCMutators), so a peer with the shipped GUI.u and one with the rebuilt
     one still connect (the menus are client-side);
   - `System/Core.u` is untouched, so the frontend's `assetMarkers.fingerprint` (sha256 of Core.u) still holds;
   - the load-time patcher skips the rebuilt GUI.u (its two GUI patches are in the source: M14) and still patches
     every other shipped package;
   - the frontend manifest changes in one entry: generated with the glue's own tool for a tree that differs only in
     GUI.u (`<WorkUC2>/run/d3-webassets`, hard links + the rebuilt GUI.u), only `system/gui.u` differs from the
     committed `targets/uc2/file-manifest.json`; regenerate it and `dependency-manifest.json`
     (`targets/uc2/README.md` has the two commands) and commit them in ufrontend with the asset-tree version.
   The browser run with exactly that tree is in this notebook (`UC2_E2E_REBUILT_GUI=1`).
2. **All 17 rebuilt packages** (M14's goal, later): Core.u's bytes change (a generation is added), so the target's
   fingerprint must be updated, every peer must run the same packages, and the full `tools/dev/m14-scripts-check.sh`
   must be green on that tree (run here on all 17 rebuilt packages with the D-4 change: results below).

## Step 7 (2026-10-07): D-5 native green

`tools/dev/d5-text.sh` run 3 (`logs/d3/d5-text-run3.txt`, tree d5, binary with both keyboard fixes): **d5-text: ALL
PASS** — k1 13 (+2 file checks), k2 14 (+2: renamed on disk, User.ini LastProfile=Renamed), k3 4 (+1), k4 14 (`Dropped
'x': its key opened UC2Keyboard->UC2KeyboardPanel->GUIEditBox`, the field shows "Liandri Corporation", Backspace
clears it, Escape keeps it, Enter sets "Mine": the page reopens on `(Mine)`), k5 10 (chat: exact text, no movement
while typing, Enter runs `Say wasd Hi` = the console history's first entry, one Escape leaves and discards).
Run 2 is void: I edited the running bash script (a k1 timing fix) and bash read the changed file mid-run ("syntax
error near unexpected token" at k2's end), the M14 lesson repeated. Never edit a running script.
The k1 fix: a GUIMessageBox ignores keys while it animates in; under load (another agent's software-GL game on 6
cores) the Return came before the box was ready. The scenario now waits for `GUIMessageBox Unblocking animation`.

## Step 8 (2026-10-07): browser runs: an intermittent "not remembered", and the diagnostics for it

Browser saves runs 3-8 (`logs/d3/e2e-saves-3..8.txt`; serve-web from this worktree; the load average was 25-42 from
other agents' software-GL games):
- runs 3 and 8: every save/restore/name/rename step passed; run 8 also shows in the browser "Backspace clears the
  selected name" (`TextStr =`), "the new name is typed: Other", Escape keeps the name. Their remaining failures were
  in the test (the trimmed empty-field entry; NETWORK is one Up from PROFILES; Enter on the host's game type taken
  by the list while the page loads its maps' localisation, fixed with a retry and CUSTOMIZE's Right as the witness).
- runs 4-7: the profile was made (`getall`: name WebSaWdX, DirtyMask 0 = not transient) but the engine did NOT
  write `[Profiles] LastProfile`: no "Profiles: remembered" line in the engine's own log file (read from the
  page's file system, so not a dropped console line), and the key absent from the engine's User.ini. Runs 3-7 ran
  the SAME wasm, so it is intermittent; never seen natively (d3-profiles 5 runs, d4-names, d5-text).
- Diagnostics added (engine): console command `PROFILESTATE` (remembered name, how often the poll ran, every
  viewport's profile, its transient bit and DirtyMask; UC2Profiles.cpp, chained in UGameEngine::Exec), and the poll
  logs every change it sees ("Profiles: viewport 0 holds ..."). Run 8 with them: `ProfileState: remembered
  "WebSaWdX" (read 1), ticks 759, viewports 1 ... viewport 0 SDL2Viewport profile PlayerProfile name "WebSaWdX"
  transient 0 dirtymask 0`. Runs 9 and 10 with the change log are running.

## Step 9 (2026-10-07): the browser gate runs

All with serve-web assembled from this worktree's final wasm-perf (`logs/d3/build-wasm-perf-5.log`, every engine change
of this branch), Chromium through the frontend's harness under `bin/capped 6G 1800`, sound to the null sink `uc2d3`:
- `uc2-wasm-saves.spec.js` runs 9 and 10 (`logs/d3/e2e-saves-9.txt`, `-10.txt`): **1 passed (2.5m)**, **1 passed
  (2.4m)**: every step above plus "the engine remembers" with its log lines from the engine's own file (`Profiles:
  viewport 0 holds "WebSaWdX" (was ""; remembered ""; 1 viewport(s))`, `Profiles: remembered profile "WebSaWdX"`),
  rename (Backspace clears the selected name, Escape keeps it), Network -> Host -> CUSTOMIZE -> SESSION NAME: X opens
  UC2Keyboard on "Liandri Corporation" and the X is not typed, Escape cancels. The intermittent failure of runs 4-7
  did not recur in runs 8-10 (cause not found; the diagnostics stay in).
- With the REBUILT GUI.u (`yarn build --target uc2 --assets <WorkUC2>/run/d3-webassets --manifest
  logs/d3/web/file-manifest.json --deps logs/d3/web/dependency-manifest.json ...`, `UC2_E2E_REBUILT_GUI=1`,
  `logs/d3/e2e-saves-rebuiltgui-1.txt`): **1 passed (2.4m)**, including "without a profile the player has the
  launcher's name: PlayerName = Player-dd0b49" (with the shipped GUI.u: `Anubis`).
- Regressions: `uc2-wasm-boot.spec.js` **3 passed (2.9m)** (`logs/d3/e2e-boot-1.txt`), `uc2-wasm-audio.spec.js`
  **3 passed (1.6m)** (`logs/d3/e2e-audio-1.txt`); serve-web assembled back to the standard asset tree before them.

## Step 10 (2026-10-07): correction — runs 4-7 ran ANOTHER branch's engine

The coordinator found that the D-7/D-8 agent assembled the shared `ufrontend/serve-web/` with its own engine at
13:23:46; my runs 4-7 (13:23-13:38) loaded that engine, which has no UC2Profiles.cpp: hence "nothing remembered".
It was not intermittent and not a defect of this branch. Which of my browser results ran my engine (proof = log
lines only this branch emits: `ProfileState:`, `Profiles: viewport 0 holds`, `Profiles: remembered`):
- runs 1, 2, 3 (`Profiles: remembered` present), run 8 (3 lines), runs 9 and 10 (7 lines each), the rebuilt-GUI.u
  run (7 lines): MINE. Runs 4-7: VOID.
- `uc2-wasm-boot.spec.js` / `uc2-wasm-audio.spec.js` of step 9: serve-web was assembled from my artifact immediately
  before, but those specs print nothing branch-specific and serve-web was reassembled by someone else at 14:17
  (`serve-web/meta.json` gitSha 143e193, SDLLaunch.wasm sha256 e38346... vs mine e6e82e..., meta gitSha 4d2e399):
  treated as unproven; rerun with a sha256 check of `serve-web/SDLLaunch.wasm` against this worktree's build right
  before AND after the run.
The diagnostics of step 8 stay (PROFILESTATE is what proves the engine's identity in the saves spec).

## Step 11 (2026-10-07): M14 regressions on ALL rebuilt packages; browser runs proven

`M14_PREFIX=d3-m14 M14_SCRIPTS=<WorkUC2>/run/d3-make/System M14_OUT=<WorkUC2>/logs/d3/m14-check
tools/dev/m14-scripts-check.sh` (all 17 packages rebuilt from this branch's sources, GUI.u with D-4; final native
binary; `logs/d3/m14-check/m14-summary.txt`): 11 PASS, 0 FAIL — verify, menu, deadbolt, patcher (rebuilt: none
patched; shipped: 7 patched), controls (keyboard 16, mouse 18, text 7, pad 33, both 14), audio (self-test 39/0, menu
music -19.02 dB), effects (4 pairs each tree, self-test 11/0), census (58 maps, 58 PASS both trees, 0 hung), movies
19/0, net 16/0 (dedicated2 + travel), leftovers. The suites' own logs went to `<WorkUC2>/logs/d3-m14-*` (they write to
`logs/<tree>` by design).

Builds at the final engine code (no engine change after c2e7b83): native `logs/d3/build-native-7.log`, wasm-perf
`logs/d3/build-wasm-perf-5.log`, windows `logs/d3/build-windows-2.log` (incremental after `build-windows.log`), all
rc 0, 0 errors.

Browser, engine identity PROVEN (sha256 of `serve-web/SDLLaunch.wasm` = this worktree's `e6e82ef55aab...` before and
after each run, `logs/d3/e2e-engine-proof.txt`): `uc2-wasm-boot.spec.js` 3 passed (2.9m), `uc2-wasm-audio.spec.js`
3 passed (1.2m), `uc2-wasm-saves.spec.js` run 11 1 passed (2.3m). Saves runs 9 and 10 and the rebuilt-GUI.u run are
proven by this branch's own log lines (step 10). Null sink removed.

## Gate (2026-10-07, branch head after this entry; engine code at c2e7b83, nothing in Source/ changed after it)

(a) Builds: `bin/capped 6G 3600 -- cmake --build build-native -j6` rc 0 (`logs/d3/build-native-7.log`);
`source env.sh && bin/capped 6G 3600 -- cmake --build build-wasm-perf -j6` rc 0 (`build-wasm-perf-5.log`);
`bin/capped 6G 3600 -- cmake --build build-windows -j2` rc 0 (`build-windows-2.log`, after the full
`cmake --preset windows` build `build-windows.log`); 0 `error:` lines in each.
(b) Native, final binary: `tools/dev/d3-profiles.sh` **ALL PASS** (27 PASS lines; `logs/d3/final-d3-profiles.txt`),
`tools/dev/c1-saves.sh` (C1_TREE=d3c1) **ALL PASS** (11). Browser saves spec: runs 9, 10, 11 **1 passed** each
(11 with the sha256 proof), and with the rebuilt GUI.u **1 passed**.
(c) D-4: `tools/dev/d4-names.sh` (tree d3s, rebuilt packages) **ALL PASS** (12 PASS lines; n1 11, n2 7, n3 3, n4 4
scenario checks); on the shipped packages 4 FAILED exactly where GUI.u decides (`logs/d3/d4-names-shipped.txt`).
Browser: the field disabled + hint, the profile names the player, [DefaultPlayer] Name kept (shipped packages); the
launcher's name without a profile (rebuilt GUI.u run).
(d) D-5: `tools/dev/d5-text.sh` **ALL PASS** (k1 13, k2 14, k3 4, k4 14, k5 10; `final-d5-text.txt`); browser: typing,
Backspace, Enter, Escape on the new-profile page, rename, the host's session name (saves spec).
(e) Regressions: `ucc-bin verifyclasses` on shipped and rebuilt trees "All native class layouts match";
`tools/dev/m10-controls.sh` (M10_TREE=d3m10) keyboard 16, mouse 18, text 7, pad 33, both 14, all PASS;
`tools/dev/c1-ini-overrides.sh` (C1_TREE=d3c1) **ALL PASS** (30 PASS, 0 FAIL); browser `uc2-wasm-boot.spec.js` 3
passed, `uc2-wasm-audio.spec.js` 3 passed (engine proven); `tools/dev/m14-scripts-check.sh` on all rebuilt packages
11 PASS (step 11).
Memory: every run in `bin/capped`: builds 6G, script make/strip 3G, game and ucc runs 2G, unit tests 3-4G, browser
6G (the mem-guard's machine-wide peak 10.7-13.2 GB, other agents included); census 8G and net 6G inside the M14
suite. No ceiling was hit (no exit 137).
