Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# Script `SaveConfig()` / `StaticSaveConfig()` on PC (task "saveconfig")

> 2026-10-11 (T4): the script sources this notebook refers to (`Source/<Pkg>/Classes`, `.ucproject`) left the
> repository; Epic's text is `<WorkUC2>/epic-sources/<Pkg>/Classes` (dev machine only), the build's base is the
> decompiled `build-*/Scripts/base`, our changes are `patches/` (`scripts-removed.md`).

Running notebook of the `saveconfig` branch (worktree `../engine-uc2-sc`), started 2026-10-07 from main's tip
`8161ead`. Brief (owner: "this is a PC game now"): enable UnrealScript's `SaveConfig()` / `StaticSaveConfig()`, which
Epic compiled out on PC *and* Xbox (`Source/Core/Src/UnCorSc.cpp` `execSaveConfig` / `execStaticSaveConfig`, each body
in `#if 0 // rjp - calling SaveConfig() has mucho side-effects, so behavior should be identical on both PC and XBOX`;
the line above it, `//#ifndef _XBOX//jg -- removed as it violates TCRs`, says what the Xbox reason was: the
certification requirements forbid unprompted disk writes; on the Xbox settings persist through the PROFILE instead).
Appended after every step so a successor can continue. Run trees `<WorkUC2>/run/sc*`, logs `<WorkUC2>/logs/sc/`.

## Step 1 (2026-10-07): the call sites, and who relied on the no-op

### What the native does when it is on (read: `UObject::SaveConfig`, Core/Src/UnObj.cpp:1450)

`SaveConfig(Flags=CPF_Config, Filename=NULL)` writes EVERY config property of the object's class (the whole
`PropertyLink`, inherited ones included) into the ini: file = the class's `ClassConfigName` (`config(User)` ->
User.ini, bare `config` -> `Avaris.ini` ("System"), `config(Gametypes)` -> Gametypes.ini); section = the class path
(`Engine.PlayerController`), or for a `globalconfig` property the path of the class that DECLARES it
(`Engine.Controller` for `Handedness`); values via `ExportText`; then `GConfig->Flush(0)` (all dirty files). It is
`#ifndef FINAL_RELEASE` (not defined in any of our builds, so it works). `execStaticSaveConfig` does it on the class
default object, `execSaveConfig` on the instance. Consequence for the question "what is a side-effect": a save writes
whatever the default object / the instance holds at that moment for ALL its config properties, so it is only as
clean as the object: anything script or native code changed in a class default at run time (a profile applying a
slider, a URL option overriding a game rule) goes into the ini together with the value the caller meant.

### Script callers (grep -a -i "saveconfig" Source/*/Classes, all of them; the 8 other hits are comments or the declaration, `Core/Classes/Object.uc:629-630`)

| # | Site | Runs when | Saves (file, section) | Wanted or side-effect |
|---|---|---|---|---|
| 1 | `Engine/Classes/PlayerController.uc:1691` `SetHand` | the console commands `SetWeaponHand Left/Right/Center/Hidden` (:1700) and `ShowGun` (:1709 toggles hidden/right); no menu, no profile, no native caller | static, class `PlayerController`: User.ini `[Engine.PlayerController]` and `[Engine.Controller]` (`Handedness` is `globalconfig` in `Controller`) | WANTED: the player's weapon hand / hidden gun. The rest of the class is written as it was loaded (see "what else changes a class default" below: nothing else does) |
| 2 | `Engine/Classes/PlayerInput.uc:242,249,256` `UpdateSensitivity`, `UpdateAccel`, `UpdateSmoothing` | the console commands `SetSensitivity`, `SetMouseAccel`, `SetMouseSmoothing` (`PlayerController.uc:1386-1398`); no UI | static, class `PlayerInput`: User.ini `[Engine.PlayerInput]` | A player setting in UT2004, but in UC2 neither effective nor harmful: the live input object is a `UCPlayerInput`, whose `default.` the three functions write, while the save is of `Engine.PlayerInput`'s defaults (unchanged: the file gets the loaded values back). UC2's mouse does not use `MouseSensitivity` / `MouseSmoothingMode` / `MouseAccelThreshold` at all (D-6: `UCPlayerInput.MouseLookSensitivity`). Left as it is |
| 3 | `IpDrv/Classes/UdpGamespyQuery.uc:54` `PreBeginPlay` | a SERVER, when `ServerActors=...UdpGameSpyQuery` is configured and `bRestartServerOnPortSwap=True`: remembers the bound query port | instance: Avaris.ini `[IpDrv.UdpGameSpyQuery]` | server infrastructure, deliberately persisted (the class asserts on the old port at the next start). Not a player setting; not reachable in UC2 as shipped (every `ServerActors` line in Default.ini is commented out, the class is `IpDrv.UdpGameSpyQuery`, not `IpServer.UdpServerQuery`) |
| 4 | `UnrealGame/Classes/DeathMatch.uc:1791` `AdjustSkill` | during a match, `bAdjustSkill` on (config `False`; only the URL option `?AutoAdjust=True` sets it, no UC2 menu does): after a kill by/of a human when the adapted difficulty moved by 1 | instance (a LIVE game rules object): Gametypes.ini `[UnrealGame.DeathMatch]`, `[UCGameplay.UCDeathMatch]`, ... | SIDE-EFFECT. The intended value is `GameDifficulty`; the call writes every config property of the match's rules object, which at that time holds the match's URL options (GoalScore, TimeLimit, MaxPlayers, NumBots, team balance, ...) |
| 5 | `UCGameplay/Classes/UCPlayerController.uc:4909` `exec charconfig` | only when somebody types `charconfig` (marked `//debug`) | static, class of the pawn: Characters.ini `[UCGameplay.UCPawn_...]` (`config(Characters)`) | a developer's tool (dump a character's tuning), writes class defaults the pawn's static init has filled with loaded object references (`default.ComboList`, `HeadClass`, ...) into a CONTENT ini. Not a player setting; explicit; left as it is |

(`DeathMatch.uc:1791` is the only call on an instance of a gametype; `UdpGamespyQuery.uc:54` the only other instance call.)

No other script saves anything: the menus (UCMenu, GUI) never call it; the profile (`PlayerProfile.uc`) never does (it
sets values; the profile file is saved by `SaveProfile`, natively).

### What else changes a class default at run time (the thing a whole-class save would carry along)

`default.<config property> =` over all of `Source/*/Classes` (scratch script `defwrites.py` in the session; classes
resolved up the inheritance chain, `class'X'.default.` honoured): 

- `PlayerController.SetHand` -> `Handedness` (site 1, the intended one);
- `PlayerInput.UpdateSensitivity/Accel/Smoothing` -> on the LIVE subclass's defaults (site 2);
- `UCPlayerInput.Update*` (:1058-1297) -> `bInvertMouse` (declared in `Engine.PlayerInput`), `MouseSensitivity_Y`
  (`MouseSensitivity_X` is NOT written to `default.` by `UpdateSensitivityX` - Epic's source assigns the instance twice),
  turn/look limits, fall-offs, accel times and limits, `bTargetFriction`, `bTargetAdhesion`, `bNonlinearAccel`,
  `bAccelMultipliers`. Who calls them: the PROFILE (`PlayerProfile.SetMouseInvert` -> `PlayerController.SetMouseInvert` ->
  `UpdateInvert`; `SetSensitivityX/Y` -> `UpdateSensitivityX/Y`, applied at every `InitializeProfileValues`, also for the
  transient "no profile"), and the developer console commands `SetAccel*`, `SetTurnLimit`... So a whole-class save of
  `UCGameplay.UCPlayerInput` writes the active profile's pad invert and Y sensitivity (and a developer's tuning) into
  User.ini next to the mouse settings;
- `PlayerProfile.uc:764` `class'GameReplicationInfo'.default.ServerName` and `:1358` `class'GameInfo'.default.GoreLevel`
  (the profile's server name and gore level into class defaults; no site saves those classes);
- the `UCPawn_*` static init filling `default.ComboList`, `HeadClass`... (site 5);
- `UCCombo.uc:206` `class'Ann_Combo'.default.AnnounceRadius` (a class no site saves).
Native code: nothing writes the defaults of `PlayerController`, `Controller` or `PlayerInput` (`grep -a
"GetDefault<A\?PlayerController>\|GetDefault<UPlayerInput>\|GetDefault<AController>" Source/*/Src` finds no use at all;
`Handedness` appears only as the member in `EngineClasses.h:1579`).

### Native C++ callers of `SaveConfig()` (grep -a "SaveConfig(" Source/*/Src): none relied on the script side being a no-op

They call `UObject::SaveConfig()` directly, never through `execSaveConfig`, and `UObject::SaveConfig` is live already
(not `FINAL_RELEASE`): `UClient::PostEditChange` and the console `BRIGHTNESS`/`CONTRAST`/`GAMMA` (UnCamMgr.cpp:46,178,206,
236), `UPlayer` `NETSPEED`/`LANSPEED` (UnCamera.cpp:443,464), the vertex-shader enum list (UnVertexShaderConfigs.cpp:99,157),
the GUI controller's design-mode keys (UnGUICore.cpp:361,411,432,437, modders only: `bModAuthor`), the master-server
uplink/client (IpDrv: `Actor->SaveConfig()`), the viewport's `SETRES`/window size (SDL2Viewport.cpp:272,664; WinDrv,
SDLDrv likewise), and the editor (Core/UnObj.cpp:1951 under `GIsEditor`, Window/Properties.cpp, UnrealEd). Enabling
the script natives changes none of them. `Source/Engine/Inc/ULoadSaveManager.h:12` is a comment saying `LoadConfig()/
SaveConfig()` could one day go through the load/save manager "so that SaveConfig() doesn't have to be disabled".

### Decisions that follow (revised in step 2 after the first experiment)

1. Both natives call the real thing when `[Engine.Engine] ScriptSaveConfig` is True (default; read once at start;
   `False` = Epic's no-op, as on the Xbox).
2. Site 4 (a live gametype instance): the native does not save an instance of a `GameInfo` (logged once). Match
   rules come from the URL; Gametypes.ini holds the gametype's defaults and must not take a match's values.
3. A whole-class save of a BASE class is NOT harmless (step 2: it pinned the FOV): the natives leave out a
   globalconfig property that another class deliberately sets differently.
4. The D-6 mouse settings need a save of exactly two keys, and a whole-class save of `UCPlayerInput` would carry the
   profile's pad values along: a new native, `SAVECONFIGKEYS <Class> <Prop>...` (console command, written like
   `SaveConfig` writes a property, honouring `ScriptSaveConfig`), called by `MouseLookSens` / `MouseLookInvert`.

## Step 2 (2026-10-08): the natives, and the thing "mucho side-effects" hides: a base-class save pins every subclass

Native build of the unchanged tree first (`logs/sc/build-native-0.log`, 0 errors; load average 30-70 from other
agents, `-j4`, about 25 minutes from scratch), then the edits below, each rebuilt incrementally.

### First experiment: Epic's whole-class `StaticSaveConfig` through `SetWeaponHand`, as it would have been enabled

`ucc-bin saveconfigtest static Engine.PlayerController Handedness -1` (new, see below) on a fresh User.ini, then
`ucc-bin saveconfigtest get UCGameplay.UCPlayerController DesiredFOV` in a new process:

- the save wrote `[Engine.Controller] Handedness=-1.000000` (the intended key) and the WHOLE `[Engine.PlayerController]`
  section, 37 keys at `Engine.PlayerController`'s compiled defaults (its globalconfig properties, and the `config` ones the
  class inherits from Actor/Controller: `DrawScale`, `Mass`, `TargetDistWeight`...), among them `DesiredFOV=85`,
  `DefaultFOV=85` (evidence: `logs/sc/polluted-User.ini`);
- the next start: `UCGameplay.UCPlayerController` `DesiredFOV` = **85, not 90**, `DefaultFOV` 85. Cause
  (`UObject::LoadConfig`, Core/Src/UnObj.cpp:903): a `globalconfig` property is read from the section of the class that
  DECLARES it (`Engine.PlayerController`) into the default of EVERY subclass, after that subclass's own
  `defaultproperties`. UCPlayerController sets `DesiredFOV=90.000000` (UCPlayerController.uc:8061); the ini key replaces it.
  Clean User.ini: 90. So enabling the natives naively would have changed the player's field of view (and with it the
  mouse sensitivity, `DesiredFOV/90`, and the AutoFOV baseline) the first time anybody typed `ShowGun`. This is the real
  content of Epic's "mucho side-effects": a save of a class writes that class's DEFAULTS over its subclasses'.

### What was done

`Source/Core/Src/UnCorSc.cpp`: `execSaveConfig` / `execStaticSaveConfig` call `UC2SaveConfigFromScript` when
`UC2ScriptSaveConfigEnabled()` (`[Engine.Engine] ScriptSaveConfig`, default True, read once by `appInit` after the `-ini:`
overrides, `UnMisc.cpp`; `_XBOX` keeps the no-op); an instance of a `GameInfo` is not saved (site 4), logged once.
`Source/Core/Src/UnObj.cpp`:
- `UC2NoteDefaultOverrides(UClass*)`, called by `UClass::Link` / `UClass::Serialize` (`UnClass.cpp`) right after a class
  default has loaded its config: records every inherited globalconfig property whose default differs from the
  superclass's (the class DELIBERATELY sets it: its defaultproperties, not an ini key, which both would carry). Recorded at
  load because afterwards they cannot be told from a subclass that merely did not see a change script made to its
  parent's default (Handedness after `SetHand`: UnrealPlayer and UCPlayerController still hold the old 1);
- `UC2SaveConfigFromScript`: `UObject::SaveConfig()`, except that a globalconfig property some OTHER class (a descendant
  of the declaring class) deliberately sets is left out (`UC2SetByAnotherClass`), and the names are logged:
  `Script SaveConfig: PlayerController: not written, another class sets its own default and globalconfig is shared with it:
  bNeverSwitchOnPickup, DesiredFOV, DefaultFOV`. Native C++ callers of `SaveConfig()` are untouched (they never share
  a section with a subclass's default this way);
- `SAVECONFIGKEYS <Class> <Property>...` in `UObject::StaticExec`: `SaveConfig()` with a filter (`GSaveConfigOnly`) on the
  named properties, the same file/section/`ExportText`/flush code as a whole-class save; replies one line per key
  (`SaveConfigKeys: UCPlayerInput.MouseLookSensitivity=1.750000 written to User.ini`); `ScriptSaveConfig=False`: replies
  `off`, writes nothing.
`Source/UCC/Src/UCC.cpp`: `ucc-bin saveconfigtest static|instance|keys|get <Class> <Property> [<Value>] [load=<Pkg>]`: sets the
class default, then runs the native behind `StaticSaveConfig()` / `SaveConfig()` the way the VM does (a frame whose code is
the end-of-parameters token) or the `SAVECONFIGKEYS` / `GET` command; works unchanged in the wasm `ucc-bin.js`.
`Source/Core/Src/UC2ScriptSaveConfig.h`: the private declarations (no public header changed). `Engine/Config/Default.ini`
`[Engine.Engine] ScriptSaveConfig=True`, `web/Default.ini` regenerated by `tools/dev/sync-web-ini.py`, a row in
`Engine/Config/README.md`.

Bugs found by the compiler's warning on the first build and fixed before anything ran: `GetPathName()` / `GetFullName()`
return `const TCHAR*` here, I had dereferenced them (`*C->GetPathName()`).

### Results of the same experiment with the filter (`logs/sc/t4-User.ini`)

`ucc-bin saveconfigtest static Engine.PlayerController Handedness -1 load=UCGameplay` (a game has UCGameplay loaded; the
filter looks at the classes that are loaded): `[Engine.Controller] Handedness=-1.000000` written; `bNeverSwitchOnPickup`,
`DesiredFOV`, `DefaultFOV` not written (UCPlayerController sets them); in a new process `UCPlayerController` `DesiredFOV` 90
and `Handedness` -1. Without `load=` the same save writes DesiredFOV=85 (nothing else is loaded to protect): that is why
the filter must be a load-time record of the loaded classes, and why the hook has `load=`.
`keys UCGameplay.UCPlayerInput MouseLookSensitivity 1.75`: of the 36 keys of `[UCGameplay.UCPlayerInput]` only
`MouseLookSensitivity` changed (2.5 -> 1.750000). With `-ini:System:[Engine.Engine]:ScriptSaveConfig=False`: reply `off`,
`MouseLookSensitivity=2.5` stays, the log has `Script SaveConfig: off: they do nothing, as on the Xbox`.

Script (UCGameplay, `UCPlayerInput.uc`): `MouseLookSens` / `MouseLookInvert` also set the class default and send
`SAVECONFIGKEYS UCGameplay.UCPlayerInput <key>`. `tools/dev/script-build.sh --tree d10-sc --logs <WorkUC2>/logs/sc`: make
`Success - 0 error(s), 85 warning(s)` (M14's count); against the shipped packages UCGameplay "different", problems 18,
findings 6 (D-6's own numbers; every other package as before). Against a build of main's sources (`--tree d10-base`) the
exports differ in exactly two sizes by path: `UCPlayerInput.MouseLookSens` 68 -> 161 bytes, `UCPlayerInput.MouseLookInvert`
63 -> 154; 78 more exports differ in at most 29 bytes each (Line/TextPos and dependency CRCs of the classes after the
edit); no export added or removed. Package to switch: **UCGameplay.u only** (1,584,207 -> 1,584,391 bytes; GUI.u as the
asset tree has it).

## Step 3 (2026-10-08): a class save writes only what changed; the profile/ini rule; the test script

### Second finding: even without the shadowing, a whole-class dump bakes in defaults

After the filter, `StaticSaveConfig` of `Engine.PlayerController` still wrote 35 keys (`[Engine.PlayerController]`: every
globalconfig property no subclass touches, and the `config` ones the class inherits from Actor/Controller: `DrawScale`,
`Mass`, `bNetDebug`, `TargetDistWeight`...) at their compiled defaults. Value for value harmless today, but the ini then
holds a stale copy of a SCRIPT default: a rebuilt package that changes a default would not reach a player whose User.ini
has the copy (User.ini is kept for years natively and for ever on the web, C-2). So a script save of a CLASS DEFAULT now
writes a property only when it differs from a baseline (`UC2ConfigValueChanged`, UnObj.cpp):
- the ini's value, when the key is in the ini (text imported into a scratch value of the property's type and compared with
  `Identical`, so `2.5` equals `2.500000`);
- else the value the class default held when it loaded its config (`UC2NoteDefaultOverrides` records, per class, each
  property it declares and each inherited one its `defaultproperties` set differently from the superclass; ExportText);
- else (inherited and not overridden) the superclass default's value.
Arrays and maps are not written by this form. An INSTANCE save (`SaveConfig()` on an object: UdpGameSpyQuery) writes the
whole instance, as before: a server object's state has no baseline and is meant to be persisted. SAVECONFIGKEYS writes
exactly the keys named, without any comparison.
Results (`ucc-bin saveconfigtest`, keys new or changed against DefUser.ini): `static Engine.PlayerController Handedness -1`
-> exactly `[Engine.Controller] Handedness=-1.000000` (was 37 keys, then 35 keys, now 1); `instance Engine.PlayerInput
MouseSmoothingMode 2` -> exactly that key; `static UCGameplay.UCPlayerInput MouseLookSensitivity 1.5` -> exactly that
key (UCPlayerInput's own defaultproperties `MouseSensitivity=0`, `MouseSmoothingMode=0`, `MouseAccelThreshold=0`, which the
first version of the rule wrote into `[Engine.PlayerInput]`, are recorded as its overrides and not written).
Found on the way: `GIsEditor` is 1 inside ucc's commandlets, so the first version of the record (skipped under
`GIsEditor`) had no entry for classes loaded later; the record is not guarded.

### The rule between the profile and the ini (task 3)

What the profile applies, and where (script, `PlayerProfile.uc`): at every `PlayerController.SetPlayerOwner`/`Initialize`
-> `InitializeProfileValues` (also for the transient "no profile" profile) the pad's invert (`bInvertY` ->
`PlayerInput.UpdateInvert` -> `default.bInvertMouse` and the instance, ini key `[Engine.PlayerInput] bInvertMouse`),
sensitivities (`SensitivityX/Y` -> `UpdateSensitivityX/Y` -> `MouseSensitivity_X/Y`, ini `[UCGameplay.UCPlayerInput]`),
auto-centre, handicap, the weapon classes and the character (`PC.UpdateURL("Class"|"EnergyWeapon"|..., ...)` into the
viewport's player URL), the name (D-4). The decision:

1. **The profile owns everything it applies; its values are applied AFTER the ini was read, at every player start, so
   the ini can never override a loaded profile.** Measured (`sc-saveconfig.sh` g1/g2): with the ini saying
   `MouseSensitivity_Y=0.9` and the transient profile applying 0.8, `getall UCPlayerInput MouseSensitivity_Y` in the match is
   0.8, and the pad invert is the profile's after a restart. The ini copies of these keys are therefore never read in a
   game, and nothing writes the profile's values into them: a class save writes only what changed against the ini and the
   class default (so the profile's `default.MouseSensitivity_Y = 0.8` WOULD be written by `UCPlayerInput.StaticSaveConfig()`;
   nothing calls that, and the mouse commands use SAVECONFIGKEYS for exactly this reason).
2. **The ini owns what the profile has no field for: the mouse** (`MouseLookSensitivity`, `bInvertMouseLook`,
   `MouseLockBreakAngle`, `bKeysAsStick`): the profile file is binary with room for one invert flag, the pad's (D-6), and
   no profile code touches these properties, so a profile cannot change the mouse (measured: g2, a restart gives the
   mouse the ini's values while the pad invert comes from the profile). The mouse is per machine and per player, like
   its DPI; it persists in User.ini, and on the web with the other inis.
3. **The name**: D-4 (the profile names the player; the ini name is the launcher's and is kept).
4. **The character**: a menu-driven launch builds the URL from the active profile (GameStartInfo), so the profile wins;
   `[DefaultPlayer] Class` is only the URL a launch that names no class falls back to (the web launcher's Play form).
   Known gap, not closed here: after switching profile and before launching from the menus, such a direct launch uses the
   previous launch's character from `[DefaultPlayer] Class`. A fix (the profile's class into the player's login URL, like
   `UC2ProfileNameInURL`) needs the profile's class string natively (a script function) and a two-profile test; recorded
   in BLOCKERS.md.
5. Weapon hand (`Handedness`), FOV: not in the profile; User.ini owns them (SetWeaponHand, ShowGun, the FOV command).

### The test: `tools/dev/sc-saveconfig.sh`

Parts u (native ucc), w (wasm ucc under Node), g (game: set/kill/restart/off), m (menu round trip + full match with the
natives on and off, allow-list). Results are in the gate at the end of this notebook.
`tools/dev/d6-mouse.sh` look scenario: two checks added (`look-sensitivity-saved-to-user-ini`,
`look-mouse-invert-saved-to-user-ini`): its console commands were session-only before and are not now.

## Checkpoint (2026-10-08 03:55, after the usage-limit cut-off at about 01:15)

State found: branch `saveconfig` at 6d0798a (7 commits on 8161ead), worktree clean, no process of mine running, no temporary
instrumentation in `Source/` or `tools/` (`grep -a "DBGREC\|UC2STAT\|GUC2NoteSeconds"` finds nothing) and none in any built
binary (`grep -a -c UC2STAT` is 0 in the native, windows and wasm ucc/game binaries; the native, windows and wasm binaries were
all built from the committed sources: the measurement build below was native only and was rebuilt from the reverted source,
`cmp build-native/Source/UCC/ucc-bin run/sc/System/ucc-bin` equal).

What had finished before the cut-off, with its evidence: the full `tools/dev/sc-saveconfig.sh` (u, w, g, m) on the final
binaries, **91 PASS, 0 FAIL** (`logs/sc/sc-all-final2.txt`, 7m08s, load 17-25); the regression suites on the binary of f72face
(Core unchanged since; 6d0798a only adds the `gameinfo` mode to the ucc test hook): `d3-profiles` 27 PASS, `c1-saves` 11 PASS,
`c1-ini-overrides` 30 PASS, `d6-mouse look ini migrate lock` 60 PASS / 0 FAIL, `m10-controls` keyboard 16, mouse 18, text 7, pad 33,
both 14 (`logs/sc/reg/`); builds native/windows/wasm-perf 0 errors (`logs/sc/build-native-18.log`, `build-windows-3.log`,
`build-wasm-perf-3.log`).

The measurement that was running when the limit hit: the cost of the class records (`UC2NoteDefaultOverrides`, called once per
class when its default loads its config), instrumented in a scratch edit of UnObj.cpp and reverted: **995 records, 1.5 ms in
total** for a ucc start with Engine, UCGameplay and everything they pull in (`UC2STAT records=995 seconds=0.001511`); about
50 KB of memory (a `FString` per record). Not a start-up cost.

What remained: re-running the regression suites on the very binary of the final commit (the idle machine allows it: load 1.3),
the gate and the report. Done below.

## Gate (2026-10-08, branch `saveconfig`; engine code at f72face, nothing in `Source/` except the ucc test hook changed after it)

(a) Builds, 0 `error:` lines each: native `bin/capped 6G 3600 -- cmake --build build-native -j4` rc 0 (`logs/sc/build-native-18.log`;
the from-scratch build `build-native-0.log`); windows `bin/capped 4G 900 -- cmake --preset windows` + `bin/capped 6G 3600 --
cmake --build build-windows -j4` rc 0 (`logs/sc/build-windows-3.log`; full build `build-windows-1.log`); wasm-perf `source env.sh &&
bin/capped 4G 1200 -- cmake --preset wasm-perf` + `bin/capped 6G 9000 -- cmake --build build-wasm-perf -j4` rc 0
(`logs/sc/build-wasm-perf-3.log`; full build `-1.log`), SDLLaunch.wasm sha256 37123ee0...; `ucc-bin verifyclasses`: "All native
class layouts match". Script: `tools/dev/script-build.sh --tree d10-sc --logs <WorkUC2>/logs/sc`: `Success - 0 error(s),
85 warning(s)`.
(b) `tools/dev/sc-saveconfig.sh` (parts u w g m): **91 PASS, 0 FAIL** on the final binaries (`logs/sc/sc-all-final2.txt`).
(c) Regressions on the final binaries (`logs/sc/reg2/`): `d3-profiles.sh` ALL PASS (27), `c1-saves.sh` ALL PASS (11),
`c1-ini-overrides.sh` ALL PASS (30), `d6-mouse.sh look ini migrate lock` 60 PASS 0 FAIL (two checks added for the saved console
commands), `m10-controls.sh`: keyboard 16, mouse 18, text 7 PASS; pad and both each failed ONE timing check at load average 16-24
(`pd-stick-just-outside-dead-zone-moves`, `both-keyboard-fires-during-pad-move`) and passed on the rerun: pad 33, both 14, 0 Critical
(`logs/sc/reg2/m10b/`). The same suite passed 5/5 on the earlier run of the same engine code (`logs/sc/reg/`, load 18-24).
(d) The call-site table is "Step 1" at the top; what changed for each site is in "Step 2"/"Step 3".
Memory: builds 6G (-j4), configures 4G, script make/strip 3G, ucc 3G (wasm under Node 3G), games 2G through `m10-controls.sh`; no
ceiling was hit (no exit 137, no "STOPPED AT THE" in any log).
