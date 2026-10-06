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

Results of run 3 (`logs/c1/c1-ini-run3.txt`, 27 PASS, 0 FAIL): system key (`ShortName`) and user key
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
