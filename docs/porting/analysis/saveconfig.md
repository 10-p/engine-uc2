# Script `SaveConfig()` / `StaticSaveConfig()` on PC (task "saveconfig")

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

### Decisions that follow (implemented in the next steps)

1. Both natives call the real thing when `[Engine.Engine] ScriptSaveConfig` is True (default; read once at start;
   `False` = Epic's no-op, as on the Xbox).
2. Site 4 (a live gametype instance): the native does not save an instance of a `GameInfo` (logged once). Match
   rules come from the URL; Gametypes.ini holds the gametype's defaults and must not take a match's values.
3. Sites 1, 2, 3, 5 write what their class holds, which is only the intended change (survey above): kept.
4. The D-6 mouse settings need a save of exactly two keys, and a whole-class save of `UCPlayerInput` would carry the
   profile's pad values along: a new native, `SAVECONFIGKEYS <Class> <Prop>...` (console command, written like
   `SaveConfig` writes a property, honouring `ScriptSaveConfig`), called by `MouseLookSens` / `MouseLookInvert`.
