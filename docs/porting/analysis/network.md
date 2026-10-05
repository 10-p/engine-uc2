# Network play (IpDrv.TcpNetDriver): join, play, travel

Branch `net-join`, worktree `<WorkUC2>/engine-uc2-net`. Notebook appended after every step (newest at the
bottom of "Steps"). Logs under `<WorkUC2>/logs/net/`, run trees `<WorkUC2>/run/net*`.

## Starting point (2026-10-05)

- BLOCKERS.md: a client joining `ucc-bin server DM-Deadbolt` gets `Welcomed by server: LEVEL=DM-Deadbolt` and
  then fails `check(GObjBeginLoadCount)` in `UObject::GetPackageLinker` (Core/Src/UnObj.cpp:3361), Linux->Linux
  and Linux->wine (logs `<WorkUC2>/logs/m12/linux-netjoin-*.out`).
- In that client log the last lines before the assertion are `LevelInfo0::PrecacheMap`, `InitAudio() took`,
  `Level is Level DM-Deadbolt.MyLevel DM-Deadbolt` (= `UGameEngine::FixUpLevel`, UnGame.cpp:1512), i.e. the
  failure is in `LoadMap` after the level loaded, at the point where `FScriptPatcher::ApplyPatches()` runs
  (UnGame.cpp, "Apply any script fixups"), not in the package-map verification (which holds BeginLoad).
- Port delta in the net path vs epic-sources: UnPenLev.cpp, UnLevel.cpp, UnConn.cpp, UnChan.cpp, UnNetDrv.cpp,
  UnLinker.cpp, UnCoreNet.cpp, UnScriptPatcher.cpp, IpDrv TcpNetDriver.cpp: 0 changed lines; UnGame.cpp 5
  lines, UnObj.cpp 14 lines (none in GetPackageLinker/BeginLoad).

## Steps

### Step 1 (2026-10-05): the assertion, with its stack - Epic's latent bug in FScriptPatcher

Repro (scratch script, one capped scope): `bin/capped 6G 240 -- bash repro1.sh`: in `run/net/System` (made by
`tools/dev/mkrun-uc2.sh <m12 build-native> net`, same net code as main) `./ucc-bin server DM-Deadbolt PORT=7850
INI=NetSrv.ini USERINI=NetSrvUser.ini LOG=NetSrv.log &`, then under `xvfb-run` `gdb -batch -ex 'break
appFailAssert' -ex run -ex bt --args ./uc2.bin 127.0.0.1:7850 -log -nullrenderer INI=NetC1.ini
USERINI=NetC1User.ini LOG=NetC1.log`. (`INI=`, `USERINI=` are Epic's appInit options, UnMisc.cpp:2069/2083;
`LOG=` FOutputDeviceFile.h:60 -- so two processes can share one System/ without sharing ini/log files.)
Evidence `logs/net/repro1-client-gdb.out`:

```
#1 UObject::GetPackageLinker (InOuter=..., InFilename=0x0, LoadFlags=8 ...)   UnObj.cpp:3361
#2 ULinkerLoad::VerifyImport (i=0)      UnLinker.cpp:527
#3 ULinkerLoad::VerifyImport (i=3)      UnLinker.cpp:537
#4 ULinkerLoad::VerifyImport (i=377)    UnLinker.cpp:537
#5 ULinkerLoad::AppendImport            UnLinker.cpp:998
#6 FLinkerPatchData::Apply (bCreateDependencies=0)
#7 FScriptPatcher::ApplyPatches         UnScriptPatcher.cpp:285
#8 UGameEngine::LoadMap (Pending=...)   UnGame.cpp:2464  ("Apply any script fixups")
#9 UGameEngine::Tick                    UnGame.cpp:3632  (AttemptLoadPending)
```

Cause: `FScriptPatcher::ApplyPatches` (Epic's file, unchanged by the port) holds BeginLoad only around
`GetPackageLinker(Package, ..., LOAD_NoVerify)` and around the bytecode swap, not around `Update->Apply()`.
Apply -> `AppendImport` -> `VerifyImport` needs the linker of the new import's package when the import chain is
not yet verified. In single player the patched packages (Engine, UnrealGame, UCGameplay, UCInventory,
UCMutators, GUI, UCMenu, Editor) are only patched when loaded, and a loaded package's linker verified all its
imports, so VerifyImport returns early. On a network client every package of the server's `USES` list exists as
an empty `UPackage` (`CreatePackage` in UNetPendingLevel::NotifyReceivedText, UnPenLev.cpp ~250) before
LoadMap; `FindObject<UPackage>` finds e.g. UCMutators (not loaded in single player), the patcher creates its
linker with `LOAD_NoVerify`, and the appended import's package chain is unverified -> GetPackageLinker outside
BeginLoad. The server sends `Editor` and `UCMutators` in USES (ucc-bin loads Editor; UCMutators is a server
package), so a client always meets it.

Whose bug: Epic's PC/Xbox code path. `check()` is compiled out under `FINAL_RELEASE` (Core/Inc/UnBuild.h:39),
which the shipped Xbox game used; GetPackageLinker itself works without the count (it only finds/creates a
linker), so the shipped game never saw it. Epic's non-final PC build (and ours: DO_CHECK=1) asserts.

Fix, first attempt (commit 0926219, WRONG, replaced in step 2): BeginLoad moved up in ApplyPatches to cover
`Update->Apply()`.

Open doubt noted for later: `UPackageMap::Compute` (Core/Src/UnCoreNet.cpp:276) takes ObjectCount/NameCount
from `ExportMap.Num()`/`NameMap.Num()`, which include exports/names the patcher appended. Client and server
must therefore have patched the same packages before Compute, or every later package's ObjectBase shifts.
The client patches every USES package (placeholders included) before its Compute; the server patches only what
is loaded at its first ApplyPatches (packages loaded later, e.g. during InitGame, get patched by the second
pass after Listen/Compute). To check with a per-package map dump on both sides.

### Step 2: the first fix was wrong; BeginLoad/EndLoad around the AppendImport loop only

With the step-1 change the client got past the assertion and died with `Bad export index 12550143/582`
(`logs/net/repro2-client-gdb.out`), and with a breakpoint on the error output (repro3, first run) in
`UClass::Serialize` `check(GetPropertiesSize()>=sizeof(UObject))` from `EndLoad` in the SECOND
`ApplyPatches(TRUE)` pass (UnGame.cpp:2931). Cause: holding the count across Apply also covered
`PreloadExports` (bCreateDependencies=TRUE), whose own BeginLoad/EndLoad used to flush the classes it creates
from the package file; under my outer count they stayed queued until the EndLoad after the reader swap
(`Loader->Loader = &Ar`) and were serialized from the patch bytes -> garbage. Lesson: in this function the
position of each EndLoad decides which reader an object is read from.

Fix (Engine/Src/UnScriptPatcher.cpp, `FLinkerPatchData::Apply`): BeginLoad/EndLoad around the
`AppendImport` loop only. VerifyImport creates linkers there, never objects, so the EndLoad has nothing of its
own to serialize; GObjLoaded is empty on entry (the previous patch's bytecode EndLoad flushed it).
ApplyPatches is back to Epic's text.

Result (repro3, `logs/net/repro3-*`): client `UCPlayerController0 got player SDL2Viewport` / `setplayer`,
server `Join request`, `UCPlayerController0 got player TcpipConnection`, `Join succeeded: Player-1`. One
`Received invalid actor class` on the client -> next step (package map agreement).

### Step 3: client and server package maps must agree -- count only the file's exports/names

Diagnostic: `UPackageMap::Compute` now prints Epic's own commented-out per-package line under DevNet
(`PackageMap <i> <pkg>: base, objs (file), names (file), gen local-remote`), Core/Src/UnCoreNet.cpp. Before the
fix (repro4) the 67-line tables of server and client differed in one line: `UCMutators ... 765 names` (server)
vs `766 names` (client). The patcher had appended its name `Reset` on the client (USES placeholder, patched
before the client's Compute) but not yet on the server (UCMutators is loaded during InitGame, after the
server's first ApplyPatches; its second pass runs after Listen/Compute). Patched packages: Engine 12641 objs
(file 12640, both sides patched before Compute), UCGameplay 8030 names (file 8029), Editor 360 (file 359),
UCMutators. With different counts every later ObjectBase/NameBase and the bit width of the name index
(`SerializeInt(Index, MaxNameIndex+1)`) differ between the ends. Here UCMutators was the last entry, so only the
name width could differ; with another package order objects would be misidentified.

Whose bug: Epic's (both ends run the same patcher; the shipped Xbox listen-server/client pair had the same
timing). Fix (Core/Src/UnCoreNet.cpp `UPackageMap::Compute`): ObjectCount/NameCount are clamped to the package
file's last generation (`Summary.Generations(LocalGeneration-1)`), which equals the counts for every unpatched
file (all 61 other lines of the table). Patched-in objects (function locals: Engine `...EndState.P`, UCMenu
locals) and names (`Reset`, `bPauseable`, `CacheSizeMegs`, ... all present in other packages' name tables) are
not addressable over the net afterwards, which nothing replicated needs. After the fix (repro5) the tables are
identical (`diff` empty).

Remaining: one `Received invalid actor class` per join. Diagnostics added (DevNet, UnChan.cpp: what the client
got, and on the server any persistent actor without package-map index; UnNetDrv.cpp: which export a static index
named when it resolves to nothing). Result (repro6): client `channel 17: got None (bunch error 0, bits 45,
reliable 1, seq 1)`, server `persistent DefaultPhysicsVolume DM-Deadbolt.DefaultPhysicsVolume0 ... index -1
linker 0`: the default physics volume is spawned at run time on both ends (`ALevelInfo::GetDefaultPhysicsVolume`,
UnLevAct.cpp:2447, sets bNoDelete), DM-Deadbolt's file has none, PhysicsVolume is bAlwaysRelevant, so the
server opens a channel for an actor it can only name as None. The client marks that channel broken and keeps its
own volume. Same code in UT2004 (UnLevAct.cpp:2139); Epic's behaviour, harmless here (default gravity etc. are
the same on both ends). Left as is for now; candidate fix noted: skip bNoDelete/bStatic actors with no
package-map index in `ULevel::ServerTickClients` (UnLevTic.cpp:1455) as it already skips unmapped classes.

### Step 4: test commands (our code) and the first look at a joined client

New file Engine/Src/UC2NetTest.cpp (ours, test-only, quiet unless used), hooked by two delimited lines in
UnGame.cpp (`UGameEngine::Exec` start, `UGameEngine::Tick` start):
- console commands `NETPROBE <tag>` (log name `NetProbe`: a summary line with net mode, level, time,
  connections, local player, GRI, pawn/PRI counts; one line per pawn with name, location, health, local, role,
  class, weapon, FlashCount; one per PRI with name, id, score, deaths, kills, bot, spectator, pawn class and the
  UC2 loadout strings), `NETPLACE <name> x y z yaw` (server: FarMoveActor of that player's pawn; the client
  follows by the engine's own ClientAdjustPosition), `NETAIM <name>` (a player's own process: turn the local
  controller to face that pawn). UC2's PlayerController has no ClientSetLocation/ClientSetRotation, so the
  client's view rotation is set on the client (NETAIM), which is also what a player does.
- `-nettest=<file>` command file for any process (the dedicated server has no other input): `exec <cmd>`
  (UGameEngine::Exec, one per tick), `log <text>`. Clients keep using SDL2Drv's `-inputtest=` channel (virtual
  pad, `probe`, `exec`).

First run (scratch try1.sh, `logs/net/try1-*`): dedicated server + 1 client (null renderer, own Xvfb), pad
attached, right trigger (Fire) pressed twice. Client and server agree:
`C3 pawn name=Player-1 loc=(-1908.7,-1097.8,-130.1) health=140 local=1 role=3 ...weapon=UCWeap_Race_Nakhti`
and `S3 pawn name=Player-1 loc=(-1908.7,-1097.8,-130.1) health=140 local=0 role=4`; PRI before Fire
`spectator=1`, after `spectator=0`; `pawnclass=UCPawn_Nakhti_Anubis energy=UCInventory.UCWeap_SniperRifle
explosive=UCInventory.UCWeap_RocketLauncher` on the server = the client's URL options arrived. The first Fire
(1 s after attaching the pad, ~4 s after the join) did not spawn; the second did. `-name=` is not an option;
the player name comes from the URL (`?Name=`).
