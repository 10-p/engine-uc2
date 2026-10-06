# Web: `-ini:` overrides (C-1) and what must persist (C-2)

Running notebook of the `web-ini-saves` branch (worktree `../engine-uc2-c1`), started 2026-10-06 from main's tip
`8cbea58`. Brief: PLAN.md "Owner review of 2026-10-06", tasks C-1 and C-2 (D-3/D-4 build on them later and are
NOT done here). Other agents work in `engine-uc2-m11` (web renderer) and `engine-uc2-fx` (effects) and in the
frontend (`ufrontend`, read only for me: the recommended `targets/uc2/target.json` changes are written below
and the coordinator applies them). Appended after every step so a successor can continue.

## Step 1 - C-1: appApplyIniOverrides ported (2026-10-06)

Template read (read only): `engine-ut2004/Source/Core/Src/UnMisc.cpp:1274` `appApplyIniOverrides`, called from
`appInit` right after `GConfig->Init` (UnMisc.cpp:2337 there), declared in `Core/Inc/UnFile.h:543`. It is the only
call site in the template: SDLLaunch, UCC, DebuggerLaunch and the editor all go through `appInit`, so one call
covers uc2.bin and ucc-bin here too (SDLLaunch.cpp:474, UCC.cpp:442 both call `appInit`).

UC2 differences checked:
- Config API: `FConfigCacheIni::SetString(Section, Key, Value, Filename)` is the same as the template's (Core.h:
  `FConfigCache::SetString` virtual). `FConfigSection` is `TMultiMap<FName,FString>` here (UT2004: `FString`);
  `SetString` does `Sec->Find(Key)` / `Sec->Add(Key, Value)` with a TCHAR* either way, so nothing to adapt.
- File names: `FConfigCacheIni::ValidateFileName` (FConfigCacheIni.h:309) maps the generic `User` -> `User.ini` ->
  `UserIni` (= "User.ini", appInit sets it) and `System` -> `System.ini` -> `SystemIni` (= GIni = "Avaris.ini":
  `appPackage()` is "Avaris", SDLLaunch.cpp:463). So `-ini:User:[DefaultPlayer]:Name=X` hits User.ini and
  `-ini:System:[Engine.Engine]:Key=V` hits Avaris.ini, exactly as in UT2004; no UC2-specific name mapping needed.
- `appIsGlobalOptionToken` (UnMisc.cpp:1966) already existed here and `UnGame.cpp:309` already treats a first
  token starting with `-` as "no map", so `-ini:...` as the first token still boots to the default map.
- Command line assembly: SDLLaunch.cpp:449 and UCC.cpp:~400 join argv with a plain space (no re-quoting), as the
  template does, so a value with spaces needs the whole token in double quotes (`"-ini:...=a b"`, the quote
  characters being part of the argv element) - exactly what the frontend's `iniOverrideArg` emits
  (`ufrontend/packages/glue/src/config/ini-identity.js`).

Change (Core only; 2 files, 62 lines added):
- `Source/Core/Inc/UnFile.h`: declaration `CORE_API void appApplyIniOverrides();` after `appIsGlobalOptionToken`.
- `Source/Core/Src/UnMisc.cpp`: the function (after `appIsGlobalOptionToken`) and one call in `appInit` after
  `GConfig->Init`, before the Language lookup (so even `Engine.Engine Language` can be overridden).
  One deliberate difference from the template: a token that starts with `-ini:` but cannot be parsed is LOGGED
  (`Warning: Ini override ignored (expected -ini:<File>:[<Section>]:<Key>=<Value>): <token>`); the template
  skips it silently ("an unrecognised switch is silence"). Empty file/section/key are also rejected.

Not yet built or tested (a native build is running; the machine's load average is ~35). Next: build, then the
test script `tools/dev/c1-ini-overrides.sh`.

## Step 2 - C-1 built and tested; a config-cache bug found on the way (2026-10-06/07)

Build: `bin/capped 6G 3000 -- cmake --preset native && cmake --build build-native -j4` (load average ~35 from other
agents: about 12 minutes) links `uc2.bin` and `ucc-bin`; native run tree `tools/dev/mkrun-uc2.sh build-native c1`
(`<WorkUC2>/run/c1`; remember to re-run it after a rebuild: it holds COPIES of the binaries).

Test: `tools/dev/c1-ini-overrides.sh` (new). Client runs through `m10-controls.sh --file` under Xvfb in `capped 2G`,
ucc through `capped 2G`. "Takes effect" is proven by the engine itself: the test channel runs `get <Class> <Prop>`,
which prints the class default the config loader produced from the (overridden) ini, and the harness waits for it.
Lines logged inside appInit (the "Ini override:" lines) go to the FILE log (`System/Avaris.log`, `ucc.log`) only: the
`-log` console echo starts after appInit, so the script reads the file logs for them.

Results of run 3 (`logs/c1/c1-ini-run3.txt`, 26 PASS, 0 FAIL): system key (`ShortName`) and user key
(`MouseSensitivity_X`) reach the class defaults; a quoted token with spaces, `?` and `=` in the value
(`"-ini:System:[Engine.GameReplicationInfo]:AdminEmail=My C1 Server?mode=a=b"`) arrives whole; an `-ini:` token as
the FIRST command-line token still boots to the menu; seven different malformed tokens are logged as
`Warning: Ini override ignored (expected ...): <token>`, wrote nothing, did not stop the good token after them, no
Critical; ucc-bin applies them too. WRITE-BACK: the overridden values are in `Avaris.ini` / `User.ini` after a normal
(SIGTERM) exit, i.e. exactly as engine-ut2004 documents it: an override is not "for this run only", it is written
into the ini. That is the template's deliberate trade (one mechanism; on the web the persistence daemon then keeps it).

`ServerName` cannot be the test key (and will not work as `serverName` for the frontend later): the profile code
copies the profile's own `ServerName` ("Liandri Corporation") over `class'GameReplicationInfo'.default.ServerName` at
start-up (`PlayerProfile.uc:764`), so `get` shows that, not the ini. `AdminEmail` is used for the spaced value.

### Bug found by the test, fixed: the config cache is dropped without being written (Core/Src/UnObj.cpp)

Run 2 (same script, before the fix; `logs/c1/c1-ini-run2.txt`): everything passed except the ucc write-back, 2 FAIL
(`[C1Test] Key is not in c-Avaris.ini`): the override was applied (log line, class default) and never reached the ini.
Cause: `UObject::RemoveNames` (called by every `PurgeGarbage`: each level change, and `StaticExit`, which runs BEFORE
`appExit` flushes) ends with `GConfig->UnloadAll()` (Epic moelfke: "the names it contains may no longer be valid":
`FConfigSection` is keyed by FName here) and the `GConfig->Flush(0)` above it is `#if 0`. `SetString` only marks the
ini dirty and only `SaveConfig` flushes, so every value set but not yet written was DROPPED: the `-ini:` overrides,
`FURL::SaveURLConfig` ([DefaultPlayer] Name/Class/EnergyWeapon..., called after every map load, UnGame.cpp:2892), the
launcher's resolution switches. The client run (A) wrote its overrides only because some `SaveConfig` happened to
flush first; ucc has none. This is the same hole as "saving doesn't seem to work" for anything held in the inis.
Fix: `GConfig->Flush(0)` (writes dirty files only) at the top of `RemoveNames`, before the names go, `#ifndef _XBOX`.
Same script after the fix: the two ucc write-back checks PASS (run 3).

Commit: `Core: ...`, `tools: c1-ini-overrides.sh`, this notebook.

Next: C-2 - the web needs an explicit "write the config now" (engine-ut2004's `FLUSHCONFIG`, which the frontend's
UserData daemon calls through `ue_exec_command`); then the file inventory, the profile restart check, Node, globs.

## Step 3 - C-2 native: what the game writes, and the restart check (2026-10-07)

Correction to step 2: the C-1 script has 26 PASS lines before the FLUSHCONFIG checks (not 27); with them (checks D, E)
it has 30 PASS, 0 FAIL (`logs/c1/c1-ini-run4.txt`).

### FLUSHCONFIG (`Source/Engine/Src/UnEngine.cpp`, `UEngine::Exec`, first in the chain)

engine-ut2004's command (its UnEngine.cpp:321), which the frontend's UserData daemon calls as
`ueExecResult('flushconfig', 4000)` (`ufrontend/packages/frontend/src/shell/boot.ts:752`) through the frozen
`ue_exec_command` ABI (UC2: `SDLLaunchWeb.cpp:50`, already there) before it reads the inis to keep them: a browser tab
is closed, not shut down, so nothing may wait for the exit flush. `GConfig->Flush(0)` and the reply
`ufront: flushed config to disk (ok)`. Without it nothing on the web writes a value that was only SET (the `-ini:`
overrides, the player name that `SaveURLConfig` copies to `[DefaultPlayer]`, the resolution switches) until some
`SaveConfig` happens to flush. Proof (C-1 script, checks D/E): a client started with `-ini:System:[C1Flush]:Key=flushed`
and `-ini:User:[C1Flush]:UserKey=flushed-user`, given `exec flushconfig` and then SIGKILLed (no exit code runs) has both
values in `Avaris.ini` / `User.ini`. (E, the same without the command: the KILLed client also had the value, so
something in the native menu boot flushes by itself; the web cannot rely on that.)

### The file inventory (native, `tools/dev/c1-saves.sh`, evidence `logs/c1/saves/`)

Method: a fresh configuration (our tracked inis, no Avaris.ini/User.ini, no Profiles, no logs), a listing of the whole
run tree (1231 files: path, size, mtime, links) -> run 1 -> listing -> diff. Run 1 (client under Xvfb, `capped 2G`):
main menu -> Profiles -> the on-screen keyboard page, name `Saver` typed on the real keyboard (the M10 `text`
scenario) -> Enter -> back -> Instant Action -> DeathMatch -> DM-Deadbolt, one character change (Right on the
character page) -> match, pawn spawned, a second of forward movement, a screenshot -> SIGTERM. What it ADDED
(`logs/c1/saves/written1.txt`; nothing else in the 1231 files changed, nothing outside `System/`):

```
new  ./System/Avaris.ini                           10110 bytes   the engine ini, derived from Default.ini at first start
new  ./System/User.ini                              9189 bytes   the user ini, derived from DefUser.ini at first start
new  ./System/Avaris.log                           34559 bytes   the log (overwritten at every start, no backups)
new  ./System/Profiles/Saver/AVARIS_PROFILE.PRO    57681 bytes   THE PROFILE
```
(+ `System/ShotNNNNN.bmp` for each `SHOT`: the harness converts and deletes them; see below.)

The profile: `ULoadSaveManager::GetProfileRoot()` = `<GFileManager default directory>/Profiles/` (the default
directory is `appBaseDir()` = the working directory = `System/` natively and `/System` on the web, boot.ts:471
`bootFS.chdir('/System')`); one DIRECTORY per profile named as the profile (`GetProfilePath` = root + name + "/"), and
in it one file, `SAVE_FILE_NAME` = `AVARIS_PROFILE.PRO` (ULoadSaveManager.h:33): the whole player state (settings,
button layout, ladder progression, unlocks, per-character stats: `UPlayerProfile::Serialize`). 56951 bytes when
created, 57681 after the match (rewritten by the game's own saves; profile names with spaces keep them in the
directory name, `PlayerName` is the underscore form). Several profiles = several sibling directories
(`ListGameObjects` lists the directories of `Profiles/`, `DeleteGameObject` removes one recursively).

Writers found by reading (`CreateFileWriter` / `appSaveStringToFile` over `Source/`, minus the editor, Xbox, Windows
setup): the inis (`FConfigFile::Write`), the log (`FOutputDeviceFile`), profiles (`ULoadSaveManager`), screenshots
(`appCreateBitmap("Shot")` -> `System/Shot%05d.bmp`, UnMisc.cpp:1567), `System/Detected.ini` / `Running.ini` (SDLLaunch.cpp
:381/388: only with `-testrendev` / `-debugging`), `AutoPatch.txt` (MasterServerClient.cpp:352: the master-server
browser), `uc2.uprof` (the script profiler's `profile stop`), demo recording and `ucc` exports. The shipped
`System/PlayerProfile.ini` (class `PlayerProfile` is `config(PlayerProfile)`) was NOT written. There are no save
games other than the profile (the `Save/` and `Cache/` folders in the tree stay empty).

Persist or not (web): YES `System/Avaris.ini`, `System/User.ini` (settings, keys, `[DefaultPlayer] Name`),
`System/Profiles/<name>/AVARIS_PROFILE.PRO`. NO: `Avaris.log`, `Shot*.bmp` (1.2 MB each, nobody can fetch them out of
a browser), `Detected.ini`/`Running.ini`, `AutoPatch.txt`, `uc2.uprof`.

### The restart check (native), PASS

Run 2 starts a NEW process on the SAME tree (`M10_KEEP=1`, new in `m10-controls.sh`: skips the tree reset): the
Profiles page lists `Saver` (no "no profiles" dialog; screenshot `logs/c1/saves/run2-list.png`: title "No Profile",
list "Saver"), A selects it (title "Saver", Edit enabled; `run2-selected.png`), leaving the page loads it: the
engine's `getall PlayerProfile ProfileName` answers `Saver` and `CharacterClassName` answers
`UCCharacters.UCPawn_Human_Sapphire`, the character run 1 chose (the default is `UCPawn_Nakhti_Anubis`), so the
value came from the file, not from class defaults. No `Unable to load profile` in either log; the profile's bytes
are identical after run 2 (md5 `b2f33d84...`, not rewritten by a run that changed nothing). Output of run 4 of the
script: `logs/c1/c1-saves-run4.txt` (11 PASS, 0 FAIL).

So natively saving works since M13's `FFileManagerUnix::Exists` fix (a profile that `SaveGameObject` wrote was
reported as non-existent by `ExistsGameObject`, so every load failed: the "Unable to load profile" lines in M10's
logs). What can still look like "saving does not work" to a player: nothing is saved while the profile is the
transient default ("No Profile" in the title): every setting lives IN the profile (`SensitivityX`, `bInvertY`, ...)
and `SaveProfile` skips a transient one; and the profile is not auto-selected at the next start (the viewport always
begins with the transient profile; the page lists the saved ones, the player picks one).

## Step 4 - C-1.3: the player name and the profile name (facts for D-4; nothing implemented) (2026-10-07)

How a name reaches the game (file:line in this tree):

1. **The default name is the user ini's `[DefaultPlayer]`.** `UGameEngine::Init` (Engine/Src/UnGame.cpp:210) does
   `DefaultURL.LoadURLConfig("DefaultPlayer","User")`: every option of that section (`Name`, `Class`, `team`,
   `EnergyWeapon`, `ExplosiveWeapon`, `ThirdPerson`) becomes a URL option of `DefaultURL`, and every URL the engine
   builds starts from it (`FURL URL(&DefaultURL, Parm, TRAVEL_Partial)`, UnGame.cpp:311, the command-line map). So
   `-ini:User:[DefaultPlayer]:Name=<n>` (C-1) is what makes `<n>` the name of the FIRST map, the menu level
   (`LoadMap: menu.unr?Name=FrontendName?Class=...` in `logs/c1/d4/noprofile.out:39`, `InitGame: ?Name=FrontendName`).
   The Login of a map takes the URL option `Name` (`GameInfo.Login`, GameInfo.uc:1317: `Left(ParseOption(Options,"Name"),20)`, `_` shown as a space; then `ChangeName`, 2285); a launcher
   travel URL `Map?Name=<n>` (what `ufrontend` `getNameTravel()` appends today, controller.ts:340) wins over the default.
2. **The engine writes the name BACK to `[DefaultPlayer]` at every map load**: `UGameEngine::LoadMap` ends with
   `URL.SaveURLConfig("DefaultPlayer","Name"|"Class"|"Team"|..., "User")` (UnGame.cpp:2896-2905; `SetString` only, so
   it reaches User.ini at the next flush: SaveConfig, `flushconfig`, exit; before the RemoveNames fix of step 2 it was
   lost at the next garbage purge when nothing else flushed). Consequence seen in the runs: after a session the
   persisted `User.ini [DefaultPlayer] Name` is whatever the LAST loaded URL carried: `Saver` after a profile session
   (`logs/c1/saves/run2.out:39`: `menu.unr?Name=Saver?Class=...Sapphire`), `Anubis` after a profile-less session
   (the character's name, below). That is also what the frontend's `playerName.iniPath` read-back would show next time.
3. **The profile.** Every viewport starts with a TRANSIENT default profile (Engine/Src/UnCamera.cpp:177-188:
   `ConstructObject<UPlayerProfile>` + `DirtyMask |= kTransientProfile`); a transient profile is never saved
   (UnPlayerProfile.cpp:203). The player picks or creates a real one on the Profiles page (UC2Profile.uc,
   `NewProfileClosed` 334 -> `PlayerProfile.NewProfile`; selecting: `APlayerController::execLoadProfile`,
   UnController.cpp:1070 -> `UPlayerProfile::LoadProfile`, then `eventSetProfileName`). Two names live in the profile
   (PlayerProfile.uc): `ProfileName` (true name, spaces allowed, = the directory name under `System/Profiles/`) and
   `PlayerName` = the same with spaces as underscores (`SetProfileName`, PlayerProfile.uc:382). Neither is stored in
   the `.PRO` file; the loader sets them (M13's finding).
4. **Profile -> name in the game.** Whenever the profile's name or character changes, its `SetPlayerName` delegate
   fires (PlayerProfile.uc:449 `NewProfile`, 614 `InitializeProfileValues`, 731 `SetCharacterClassName` when
   transient) into `GUIController.SetProfilePlayerName` (GUI/Classes/GUIController.uc:2746): 
   - NON-transient profile: the name is `Profile.GetPlayerName()` (the underscore form);
   - TRANSIENT profile (no profile picked): the name is the CHARACTER's name (`CharacterInformation.GetCharacterName`,
     GUIController.uc:2759-2768), "Anubis", "Sapphire", ... This is what replaces the frontend's name;
   - it then stores the name in the GameStartInfo (`GSI.SetURLKeyValue(i,"Name",...)`, GUIController.uc:2784; GSI keeps
     `PlayerName[i]` with spaces -> `_`, GameStartInfoBase.uc:236) and calls `PC.SetName(...)` in the menu level or for a
     real profile (2794-2802), else only `PC.UpdateURL("Name", ...)` (2806).
   - `GameStartInfo.GetName` (GameStartInfoBase.uc:156-190) decides the `?Name=` of the launch URL a menu builds:
     Xbox Live gamer tag (never on PC) > the non-transient profile's name > `GSI.PlayerName[i]` (whatever
     `SetURLKeyValue` last stored; "" at start: then no `?Name=` is added and `DefaultURL`'s applies).
5. **Evidence** (`logs/c1/d4/noprofile.out`; run: the client with `-ini:User:[DefaultPlayer]:Name=FrontendName`, NO
   profile, menu -> Instant Action -> DeathMatch -> DM-Deadbolt, default character): menu level `Name=FrontendName`
   (line 39); on the character page `getall PlayerReplicationInfo PlayerName` = `Anubis` (170, the transient
   profile's character name replaced it); the match URL `LoadMap: DM-Deadbolt?...?Name=Anubis?Game=...` (210) and the
   human's PRI in the match is `Anubis` (353); with a profile `Saver` the match URL carries `Name=Saver`
   (`logs/c1/saves/run1.out:242`). So today: profile wins (correct for D-4), but without a profile the FRONTEND's name
   is overwritten by the character name as soon as the character page is used, and then persisted that way.
6. **Where D-4 would change it** (not done): the transient branch of `SetProfilePlayerName` (GUIController.uc:2759-2768)
   should keep the name the frontend/ini gave (the `[DefaultPlayer] Name` the engine started with) instead of the
   character's; and `SaveURLConfig("Name")` (UnGame.cpp:2896) should not persist a profile's name into
   `[DefaultPlayer]` (or the frontend's read-back of `User.ini` should read it only before the first launch). Both are
   script/engine decisions for D-4; the C-1 port only supplies the switch.
7. Single player (`UC2SP_EntryPanel.uc:123`) sets the PRI name from the profile directly.

`network.playerName` for `targets/uc2` is therefore the same block as UT2004's: file `System/User.ini`, section
`DefaultPlayer`, key `Name` (DefUser.ini and the web `Engine/Config/web/DefUser.ini` both carry `Name=Player`).
The frontend restricts its own name to `[A-Za-z0-9._-]` (no spaces: the engine rewrites spaces to `_` anyway).

## Step 5 - C-2 web: can the persistence daemon see a profile? No, as laid out; the web build now lays it out flat (2026-10-07)

### The finding (read from the frontend, executed with its own code)

The daemon (`ufrontend/packages/glue/src/idb/user-data.js` `scanUserFiles`) lists ONE directory per `userFiles` entry and
the entry grammar (`glue/src/target/user-files.js` `compileUserFiles`, which THROWS on an invalid entry) allows `*`/`?`
in the LAST path segment only. UC2's profile is `System/Profiles/<name>/AVARIS_PROFILE.PRO`: a directory per profile.
I ran the frontend's own compiler (read-only import, `tools/dev/c1-web-saves.sh`): `System/Profiles/*/AVARIS_PROFILE.PRO`
is REJECTED ("a glob is allowed only in the file name"), and `System/Profiles/*` accepts but never matches the nested
file. So no `userFiles` entry can persist a native-layout profile, and pasting the nested entry into target.json would
break the frontend (the compiler throws). UT2004 never met this: its profiles are flat files (`Saves/*.uvx`).

### What was done (web build only; native unchanged; one commit, revertable)

`Source/Engine/Src/ULoadSaveManager.cpp`: under `__EMSCRIPTEN__` (or `-DUC2_FLAT_PROFILE_FILES=1`) a profile is ONE flat
file `Profiles/<name>.PRO` in the engine's default directory (`/System/` in the browser), same bytes inside; load,
save, exists, delete and list (`FindFiles("Profiles/*.PRO")`, extension stripped) follow. The five functions branch on
the macro; the header (and the Xbox subclass's virtual `GetProfilePath`) are untouched. Native keeps Epic's layout
(`Profiles/<name>/AVARIS_PROFILE.PRO`; the M13 `saves` check and `c1-saves.sh` still hold).
Alternative the coordinator may prefer: keep the layout, and teach the frontend's `compileUserFiles`/`scanUserFiles`
one directory-wildcard segment (`System/Profiles/*/AVARIS_PROFILE.PRO`); then revert this commit.

`ucc-bin profiletest` got `list|exists|delete` (UCC.cpp) so the file layer can be exercised without a game.

### Evidence under Node (`tools/dev/c1-web-saves.sh`, `capped 3G`; `logs/c1/c1-web-saves-run2.txt`: 16 PASS)

`node ucc-bin.js profiletest save WebProf out.txt` from `System/` writes `Profiles/WebProf.PRO` (845 bytes, no
`Profiles/WebProf/`), the engine's base directory is that `System/` (the browser's `/System/`, boot.ts:471), so the
daemon's path is `/System/Profiles/WebProf.PRO`; list answers `WebProf`; exists true/false; load gives back every stored
property; the file is byte-identical to the native build's `Profiles/WebProf/AVARIS_PROFILE.PRO`, and each build loads
the other's file to the same export; delete removes it and the list is empty. The frontend's compiler accepts
`System/Profiles/*.PRO`, matches `/System/Profiles/WebProf.PRO`, does not match the nested native path. The C-1 ucc
check on the web build: the three `-ini:` tokens (one quoted with a space, one malformed) are applied/logged and the
two values are in `Avaris.ini` / `User.ini` after the exit (the RemoveNames flush works under wasm too).

### What Node cannot show (nothing here may start a browser)

`-sNODERAWFS` is the HOST file system, not WasmFS. Not verified, to be shown by the C-3 browser e2e (create a profile,
reload, the profile is listed and selectable; set a setting, reload):
- `mkdir /System/Profiles` by the engine (`EnsureDirectory`, `FFileManagerGeneric::MakeDirectory`): the web build
  `--preload-file`s `Default.ini`/`DefUser.ini` into `/System`, which Emscripten's WasmFS creates read-only (0555), and
  WasmFS refuses `mkdir` below it (EACCES) while allowing file creation (the frontend's own note,
  `glue/src/fetchfs/wasmfs-backend.js:307`). The backend's `ensureDir` chmods `/System` writable when it registers a
  manifest file in a SUBdirectory (UC2's manifest has `System/Localization/<lang>/` and `System/Ladders/`), so it should
  be writable by then; if not, creating a profile fails with the page's own "create profile failed" state. A
  fallback that needs no directory at all would be to keep the profile files directly in `System/`.
- that a file the engine thread creates in a NEW directory is visible to the main-thread daemon's `FS.readdir`/`stat`
  (the daemon's own note: writes into a SERVED file land in the engine thread's heap; a new file is not served);
- the daemon's 5-second scan and the `flushconfig` round trip for the inis;
- `Profiles/` must not exist in the served tree (see manifestExclude below).

## Step 6 - the recommended `targets/uc2/target.json` changes, and the final gate (2026-10-07)

The frontend is NOT edited from here (another agent commits there). The coordinator applies:

```json
"network": {
  "_networkComment": "Namespaces and the player name. The engine applies `-ini:<File>:[<Section>]:<Key>=<Value>` switches (engine-uc2 C-1, Core appApplyIniOverrides) and writes the value into the ini; the web engine has no WebRTC net driver yet, so modes/serverName stay omitted (and ServerName could not be seeded anyway: the profile code overwrites GameReplicationInfo's default ServerName at start-up).",
  "publicRoom": "default-public-room",
  "peerPrefix": "uc2",
  "defaultServerName": "UC2 Server",
  "_playerNameComment": "Where the ENGINE persists the in-game player name: User.ini [DefaultPlayer] Name (DefUser.ini and the web Default seed carry Name=Player). The engine reads it into its default URL at start (UGameEngine::Init) and WRITES it back at every map load from the URL it loaded (the profile's name, or the character's when no profile is picked: see engine-uc2 analysis/web-ini-saves.md step 4, the D-4 question).",
  "playerName": { "iniPath": "System/User.ini", "iniToken": "User", "section": "DefaultPlayer", "key": "Name", "prefix": "Player" }
},
"_userFilesComment": "The files a PLAYER makes by playing, which a reload must give back (ufront 2.75). UC2's only save game is the PROFILE (settings, button layout, ladder progression, unlocks, stats: ~57 KB). The web engine keeps it as ONE FLAT FILE, System/Profiles/<name>.PRO (engine-uc2 ULoadSaveManager.cpp, web build only), because a glob is allowed only in the file name and Epic's layout, Profiles/<name>/AVARIS_PROFILE.PRO, is rejected by compileUserFiles. Avaris.ini and User.ini are the engine files already derived from manifestExclude; the engine writes them when the host sends `flushconfig` (engine-uc2 UEngine::Exec).",
"userFiles": [ "System/Profiles/*.PRO" ],
```
and these entries appended to `manifestExclude` (all GLOBS or non-`.ini` paths on purpose: a literal `*.ini` entry
becomes an "engine file" the daemon persists, files.ts:118):
```json
"System/Profiles/**",       a saved profile is the player's, never served from the asset tree (a play tree used as the asset source has them)
"*.PRO",                    the same for a profile that is anywhere else
"System/Shot*.bmp",         SHOT screenshots (1.2 MB BMPs): not served, not persisted
"System/AutoPatch.txt"      the master-server browser's download list (IpDrv), engine scratch
```
(`*.log` and `Logs/**` are already excluded: `Avaris.log` is overwritten at every start and must not persist.
`System/Detected.ini` / `Running.ini` exist only with `-testrendev` / `-debugging`; do NOT list them: a literal
`.ini` entry would make them persisted engine files.)

What is persisted, entry by entry: `System/Avaris.ini` (engine settings: video, audio, game defaults) and
`System/User.ini` (key bindings, mouse sensitivity, `[DefaultPlayer]` name/character/weapons) through the existing
manifestExclude engine-file derivation + `flushconfig`; `System/Profiles/*.PRO` through the new `userFiles` entry.
NOT persisted, on purpose: `Avaris.log`, `Shot*.bmp`, `Detected.ini`/`Running.ini`, `AutoPatch.txt`, `uc2.uprof`,
the `Save/` and `Cache/` folders (empty: nothing writes there).

### Final gate (2026-10-07)

(a) `capped 6G 3600 -- cmake --preset native && cmake --build build-native -j4` links (`uc2.bin`, `ucc-bin`);
`source env.sh && cmake --preset wasm-perf && cmake --build build-wasm-perf -j4` builds (`SDLLaunch.js/.wasm/.data`
+ `meta.json`, `ucc-bin.js/.wasm`). (b) `tools/dev/c1-ini-overrides.sh`: 30 PASS, 0 FAIL. (c) `tools/dev/c1-saves.sh`:
11 PASS (inventory + restart), `tools/dev/c1-web-saves.sh`: 16 PASS. (d) from a fresh tree: `./ucc-bin verifyclasses`
exit 0 "all native class layouts match"; `m10-controls.sh keyboard text`: PASS 16 checks, PASS 7 checks.
Memory: every run in `capped`: clients 2G, native ucc 2G, wasm under Node 3G, builds 6G; no ceiling was hit.
