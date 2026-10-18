Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# Y-1 notebook: the launcher's player name and server name for a profile-less player and host

Branch `y1-names` (worktree `engine-uc2-y1-names`, from main c97a75a); frontend `ufrontend-y1` (branch `uc2-y1-names`).
Logs `<WorkUC2>/logs/y1/`, run trees `<WorkUC2>/run/y1*`. Newest step at the bottom.

The finding (ufrontend `docs/migration/notes/uc2-player-net-e2e.md`, full-1..5): a host without a profile plays as its
CHARACTER (`Anubis`) and its System Link session is `Liandri Corporation`, while the launcher's player name / server
name reach only the guest's join URL and the broker peer id. The owner: "Can we influence profile-less name and server
with those names. Once you create a profile name, we use those names."

## Step 0 (2026-10-12): where each name comes from today

### The player name

- The launcher seeds `-ini:User:[DefaultPlayer]:Name=<name>` (target.json `network.playerName`); the engine's default
  URL takes it (`menu.unr?Name=NetHost` in the E2E host log), `Level.GetDefaultURL("Name")` answers it.
- D-4 (patch set `player-name`, GUI.u): `GUIController.SetProfilePlayerName` (the transient profile's `SetPlayerName`
  delegate) names a profile-less PRIMARY player with `Level.GetDefaultURL("Name")` instead of the character's name, and
  stores it in the GameStartInfo (`GSI.SetURLKeyValue(i, "Name", ...)`), from which every menu URL takes `?Name=`
  (`GameStartInfoBase.GetSpawnURL`/`GetName`: a real profile's name wins over `PlayerName[i]`,
  `epic-sources/Engine/Classes/GameStartInfoBase.uc:156-185`).
- But that delegate runs only when the profile changes: `PlayerProfile.InitializeProfileValues` (:614, bound only once a
  menu has bound it: `GUIController.ControllerActivated`/`AddChildViewport` set `Profile.SetPlayerName`, AFTER the
  profile's start-up Initialize), `CreateProfile` (:449) and `SetCharacterClassName` when the CHARACTER CHANGES (:730).
- `UC2NetworkMain.Opened` / `InternalOnReopened` clear the GameStartInfo (`gameStart.Clear()`: `PlayerName[]` = "")
  (`UCMenu/Classes/UC2NetworkMain.uc` in the decompiled tree: Opened, InternalOnReopened).
- `UC2Arena_Character.SaveSettings` (UCMenu; tree `UCMenu/Classes/UC2Arena_Character.uc:1591-1612`): for a player with
  no `Name` in the GameStartInfo, `GameStart.SetURLKeyValue(PlayerIndex, "Name", Profile.GetCharacterName())`: Epic's
  second "profile-less = the character's name" place, which D-4 did not cover.
- So in the E2E: the GUEST pressed Left (Arclite): character change -> delegate -> D-4 -> `Name=NetGuest`. The HOST took
  the default character with Enter: no change, no delegate, `SaveSettings` -> `Name=Anubis` (host log: `LoadMap:
  CTF-Aqueduct?...?Name=Anubis?...?listen`). D-4's own test (`tools/dev/d4-names.sh` n1) changes character twice
  (Right, Right), so it never took the no-change path.

### The server name

- `GameReplicationInfo.ServerName` is `globalconfig` (`epic-sources/Engine/Classes/GameReplicationInfo.uc:44`): Avaris.ini
  `[Engine.GameReplicationInfo] ServerName` (shipped ini and ours: `Another Unreal Server`; the launcher seeds its
  Network panel's server name with `-ini:System:[Engine.GameReplicationInfo]:ServerName=`).
- `PlayerProfile.InitializeProfileValues` (:520) -> `SetServerName(Source.GetServerName(bDefault))` -> for the first
  viewport `class'GameReplicationInfo'.default.ServerName = ServerName` (:748-768). The profile's `ServerName` default
  is `Liandri Corporation` (:3220): every transient profile, i.e. every profile-less player, overwrites the ini value in
  memory with it at start-up (it never writes the ini). So the launcher's seed DID reach the ini (the web driver reads
  `[WebRTC] InitListen: ... server name 'E2E Netplay Host'` from it, `WebRTCNetDriver.cpp:141-150`), but the GAME's
  session name is the profile's.
- The beacon: `GameInfo.GetBeaconText()` (`epic-sources/Engine/Classes/GameInfo.uc:994-1040`) = `BeaconName |
  GameReplicationInfo.ServerName | map | maplist | players | max | purity | mutators | player names (PRI.PlayerName) |
  handicap`; natively `IpDrv.UdpBeacon.BroadcastBeacon` sends it, on the web `UWebRTCNetDriver::PushListenStatus`
  (`WebRTCNetDriver.cpp:287-332`) pushes it in the status row (the row's own name field is the driver's ini value).
- The host page's SESSION NAME rule (`UCMenu/Classes/Arena_PanelServerMain.uc` InitializeGameRules /
  RestoreProfileValues / SaveSettings) shows `CurrentProfile.GetServerName()` and writes it back with `SetServerName`.
- A profile HAS a server name of its own (the `ServerName` field, saved in the profile, edited on the host page): with a
  profile, the profile's server name is the session's name, as its player name is the player's.

## Step 1 (2026-10-12): the change (patch set `player-name`), native proof

Reproduced natively first, on main's rebuilt packages (tree `y1base`, `tools/dev/y1-names.sh y1base rebuilt h1`):
launched with `-ini:User:[DefaultPlayer]:Name=Coordinator -ini:System:[Engine.GameReplicationInfo]:ServerName=MyBox`,
hosted through the menus with the default character: beacon `CTF|Liandri Corporation|CTF-Aqueduct|CTF_Custom|1|4|0||
Anubis|0`, match URL `...Name=Anubis...?listen` (`logs/y1/native-rebuilt-run1/../native/y1base-h1.*`).

The change (all in `patches/player-name`, regenerated with `uc-patches-regen.sh edit|make player-name`):

- `UCMenu.diff` (new): `UC2Arena_Character.SaveSettings`: a player with no name in the GameStartInfo and without a
  profile is named by `Controller.SetProfilePlayerName(PlayerOwner(PlayerIndex), Profile)` (D-4's rule: the first player
  the launcher's name, a split-screen guest his character's); the character's name stays the fallback. Written inside
  the existing `if` (with a `continue`) so players34's hunks on that function still apply (at an offset of +6 lines).
- `GUI.diff`: new `GUIController.SetProfileServerName(PC, Profile)`: for the PRIMARY viewport's TRANSIENT profile whose
  server name is still the profile default, `GetConfigValue("System", "Engine.GameReplicationInfo", "ServerName", S)`
  (Core's native, reads the config cache = the ini with the launcher's `-ini:` applied, which the profile never
  writes) and `Profile.SetServerName(S)` (which copies it to `GameReplicationInfo`'s default: the session's name, the
  host page's SESSION NAME, the beacon); one log line `SetProfileServerName: no profile: the session is named <S>
  ([Engine.GameReplicationInfo] ServerName)`. Called from `ControllerActivated` where the profile delegates are bound
  (the first menu of a run: the start-up profile's Initialize runs before any delegate is bound) and from
  `SetProfilePlayerName`'s transient branch (a profile cleared to "no profile" in the menus). Two of D-4's padding
  blank lines went so the `assert` in `SetProfilePlayerName` stays on line 2791 of the edit tree.
- Compiled result (`uc-patches.py compare` against the previous build): GUI.u: `ControllerActivated` and
  `SetProfilePlayerName` differ, `SetProfileServerName` new, 723 of 725 functions bytecode-identical; UCMenu.u: only
  `UC2Arena_Character.SaveSettings` differs (2414 of 2415 identical). Compiler warnings unchanged (GUI 5, UCMenu 81).
- check-headers: current (no native declaration changed). P2: old reference FAIL on GUI.u/UCMenu.u only (UCGameplay.u
  as the reference); `--write-reference`, then P2 PASS (3 packages; `logs/y1/p2-*.log`).

A profile and the server name: a profile HAS a server name (`PlayerProfile.ServerName`, saved in it, the host page's
SESSION NAME). With a profile the profile's server name is the session's (unchanged from Epic). One consequence of the
hook: Epic's `NewProfile` resets the profile through `ClearProfile` -> `InitializeProfileValues` -> the same delegate
while it is still transient, so a profile CREATED while profile-less keeps the session name it had (the launcher's)
instead of "Liandri Corporation"; existing profiles keep their own. Telling the two apart would need state in
GUIController (a native class: a new variable changes its layout and header), so this is left as it is and said here.

`tools/dev/y1-names.sh <tree> rebuilt|vanilla [h1..h4]` (new): a System Link host through the menus (main menu ->
NETWORKED COMBAT -> HOST SYSTEM LINK GAME -> the nag (no profile) -> Enter x3 -> character page, default character ->
armory -> Begin Match), then `tools/dev/y1/y1-beacon.py` sends `REPORT?4242` to the UdpBeacon as UC2's System Link page
does and reads `GetBeaconText()` back; the ports moved off the defaults with `-ini:` (UDP 18777, game 17777). Checks: the
match URL's `Name=`, `getall PlayerReplicationInfo PlayerName` (test channel, native only), the beacon's server field
and player list, no Critical/signal/Accessed None (the Entry level's known `UCMinimalPlayerController0 ... Accessed None
[PlayerReplicationInfo]` excepted: on every start, baseline too). h1 fresh, no profile; h2 fresh, profile "Proffy"
created first; h3 restart with launcher server name `OtherBox` (starts in Proffy); h4 restart, Proffy deleted, host
without profile.

Results (tree `y1` = this build's packages, `y1van` = `UC2_SCRIPTS=none`, Epic's shipped GUI.u/UCMenu.u):

| part | rebuilt (y1) | vanilla (y1van) |
|---|---|---|
| h1 no profile | `CTF\|MyBox\|...\|Coordinator\|0`, URL `Name=Coordinator`, PRI Coordinator | `CTF\|Liandri Corporation\|...\|Anubis\|0` |
| h2 profile created | `CTF\|MyBox\|...\|Proffy\|0` (inherits the session name) | `CTF\|Liandri Corporation\|...\|Proffy\|0` |
| h3 restart in profile, launcher says OtherBox | `CTF\|MyBox\|...\|Proffy\|0` (the profile's own) | `CTF\|Liandri Corporation\|...\|Proffy\|0` |
| h4 profile deleted | `CTF\|OtherBox\|...\|Coordinator\|0` | `CTF\|Liandri Corporation\|...\|Anubis\|0` |

Rebuilt run 1 (`logs/y1/native-rebuilt-run1/`): every name check PASS in h1-h4; the first version of the script
counted the Entry level's known Accessed None as a fault (recounted from the same logs without it: 0 in each).
Vanilla run 1 (`logs/y1/native-vanilla-run1/`): **ALL PASS** (42 PASS lines, 0 Critical/signal/Accessed None but the
Entry one): Epic's behaviour unchanged with the shipped packages (the engine is not touched by Y-1).

## Step 2 (2026-10-12): new profiles start with the profile default again (the hook moves to the host page)

Why: in step 1 a profile created while profile-less inherited the launcher's server name (Epic's `NewProfile` ->
`ClearProfile` -> `InitializeProfileValues` -> the `SetPlayerName` delegate while the profile is still transient,
indistinguishable at the hook from a profile cleared to "no profile"). That changes a PROFILE's behaviour, which Y-1
must not: ufrontend's `uc2-wasm-saves.spec.js` (D-5 step, a profile created in session 1, then Host -> SESSION NAME)
expects `OpenMenu [UCMenu.UC2Keyboard] (Liandri Corporation)` and would have failed on it.

Change (still `patches/player-name`): `SetProfileServerName` is no longer called from `SetProfilePlayerName` (that
function is bytecode-identical to main's again; the padding blank lines adjusted so its `assert` stays on line 2791); it
is called from `ControllerActivated` (start-up) and from UCMenu `Arena_PanelServerMain.RestoreProfileValues` (the host
page, right before it reads `CurrentProfile.GetServerName()` for SESSION NAME), which covers a profile deleted or
cleared in the menus. `NewProfile` then resets the profile to the default as Epic's does.

- Compare against main's packages: GUI.u: `ControllerActivated` differs, `SetProfileServerName` new, 724 of 725
  identical; UCMenu.u: `UC2Arena_Character.SaveSettings` and `Arena_PanelServerMain.RestoreProfileValues` differ, 2413 of
  2415 identical. Warnings unchanged (GUI 5, UCMenu 81).
- P2: `--write-reference` then PASS (GUI.u 39538dde..., UCMenu.u ce8775ff..., UCGameplay.u unchanged 1e795f93...);
  check-headers current (`logs/y1/p2-*-2.log`, `check-headers-2.log`).
- `tools/dev/y1-names.sh y1 rebuilt` (expectations: h2/h3 the profile default in both variants): **ALL PASS**
  (`logs/y1/native-rebuilt-run2/`): h1 `CTF|MyBox|...|Coordinator|0`; h2 `CTF|Liandri Corporation|...|Proffy|0`; h3
  (launcher says OtherBox) `CTF|Liandri Corporation|...|Proffy|0`; h4 (profile deleted) `CTF|OtherBox|...|Coordinator|0`
  with the host page's `SetProfileServerName: no profile: the session is named OtherBox` line.

The web (step 1's packages, artifact sha256 5884f482..., the profile-less path identical to step 2's): ufrontend-y1's
`uc2-wasm-netplay.spec.js` extended (see the frontend notes `docs/migration/notes/uc2-y1-names.md`), `y1-full-1`:
**1 passed (7.2 min)**: host beacon `CTF|E2E Netplay Host|CTF-Aqueduct|CTF_Custom|1|4|0||NetHost|0`, host match URL
`...Name=NetHost...`, the guest's System Link row "E2E Netplay Host", both scoreboards NetHost and NetGuest
(`logs/y1/e2e-y1-full-1-results/*/15-host-scoreboard.png`). Memory: guard peak 9.8 GB machine-wide, 7G ceiling not hit.

## Step 3 (2026-10-12): final proofs on 999b6fc's packages

- Native vanilla (`y1van`, Epic's shipped GUI.u/UCMenu.u), the final script: **ALL PASS**, 42 checks
  (`logs/y1/native-vanilla-run2/`): h1 `CTF|Liandri Corporation|...|Anubis|0`, h2/h3 `...|Liandri Corporation|...|Proffy|0`,
  h4 `...|Liandri Corporation|...|Anubis|0`; no Critical/signal/Accessed None but the Entry one.
- `tools/dev/d4-names.sh` (D4_TREE=y1): ALL PASS (`logs/y1/d4-final`); `m14-scripts-check.sh verify menu patcher`
  (M14_PREFIX=y1-m14): PASS x3 (`logs/y1/m14-check-1.txt`).
- Web (ufrontend-y1 aec5944, serve-web from this build: wasm 5884f482..., overlays GUI.u 39538dde..., UCMenu.u
  ce8775ff..., proved before/after by the runner `logs/y1/run-e2e*.sh`):
  - `uc2-wasm-netplay.spec.js` `y1-full-2`: **1 passed (7.0 min)**; host beacon `CTF|E2E Netplay Host|CTF-Aqueduct|
    CTF_Custom|1|4|0||NetHost|0`, URL `Name=NetHost`, the guest's row "E2E Netplay Host", both scoreboards NetHost and
    NetGuest. Guard peak 9.5 GB machine-wide; `capped 7G` not hit.
  - `uc2-wasm-saves.spec.js` (UC2_E2E_REBUILT_GUI=1) `y1-saves-1`: **1 passed (2.5 min)**: without a profile the player
    is the launcher's name; with the profile made in session 1 the host page's SESSION NAME is `Liandri Corporation`
    (the profile's), the step 1 design would have failed here. Guard peak 7.9 GB.
- Not run: the web with Epic's shipped packages (an artifact without overlays): the engine C++ is untouched by Y-1, the
  vanilla behaviour is proven natively.
