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
