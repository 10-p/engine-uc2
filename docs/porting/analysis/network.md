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

### Step 5 (checkpoint after the usage-limit cut-off): harness, first scenario results

Cause chain so far (all Epic code, latent in the shipped FINAL_RELEASE Xbox build, which compiled `check()` out
-- `XboxLaunch.vcproj` release configurations define FINAL_RELEASE; Epic's non-final PC build would have hit
both):
1. `FLinkerPatchData::Apply` -> `AppendImport` -> `VerifyImport` -> `GetPackageLinker` outside BeginLoad on a
   network client (USES placeholder packages get patched) -> `check(GObjBeginLoadCount)`. Fix: BeginLoad/EndLoad
   around the AppendImport loop only (step 2; the wider scope of step 1 broke the patcher's reader swap).
2. `UPackageMap::Compute` counted the patcher's appended exports/names; client and server patch at different
   moments, so the tables disagreed (UCMutators 765 vs 766 names). Fix: clamp to the file's last generation
   (step 3). Not a crash on its own here (UCMutators is last) but a desync with any other order.
3. Not a defect of ours: one `Received invalid actor class` per join = the server opening a channel for the
   run-time-spawned `DefaultPhysicsVolume` (bNoDelete, no linker, DM-Deadbolt has none in its file). Epic's
   behaviour (UT2004 identical); harmless; the harness tolerates exactly this many.

Harness (tools/dev/net-test.sh -> net-test.py, ONE `capped 6G` scope, port 7850, fresh inis per process,
each uc2.bin on its own Xvfb, virtual pad per client, all decisions from NetProbe/SDL2 probe lines):
- dedicated1 (server + NetA, null renderer): join, level, spawn, URL options on the server PRI
  (pawnclass/energy/explosive), server sees NetA move 624.9 units while the pad holds forward, client and
  server agree to 0.0 units after stopping, client state menu=None paused=0 pawn=1. All PASS; `.clean` failed
  only on my own diagnostic line matching the "bunch" regex (fixed: the regex skips `DevNet:   channel`).
- dedicated2 (server + NetA + NetB): join, place (NETPLACENEAR, a server-side test teleport in our code),
  seeother (0.4/0.5 units), othermoves (515.8 units, 0.5 from the server), hit (140 -> 124 on the first shot),
  kill after 9 shots (NetA score 1, NetB deaths 1 on the server and both clients), healthseen (124 on server,
  attacker and victim), clean: all PASS. `.spawn` FAILED in that run: NetA pressed Fire 10 times alone and
  stayed a spectator until NetB also pressed -- before the match starts a DM waits for every player
  ("UCDeathmatch0: START MATCH" only after both). Harness fixed: `spawn_all` presses Fire on all players.
- listen (uc2.bin DM-Deadbolt?Listen + NetB): join, spawn, place, seeother (0.0/0.4), clientmoves (500.4),
  hit (140 -> 124), kill (9 shots, score 1 on both), clean: all PASS.
- travel (servertravel DM-Sever): the server brings DM-Sever up, the client gets `Welcomed by server:
  LEVEL=DM-Sever` and its probe shows level DM-Sever with a local pawn, BUT the server hits a Critical
  (`appError called`) and has no "Join succeeded: NetA" afterwards -> next step, logs `logs/net/travel-*`.

Not yet run: disconnect, lag, glshot, soak. Open doubts: the DefaultPhysicsVolume channel (candidate fix in
ULevel::ServerTickClients, not applied); whether the patch-name clamp could ever hide a replicated FName
(the patched names all exist in other packages' tables, so NameIndices still finds them).

### Step 6: listen, travel, disconnect, lag runs; a shutdown use-after-free ("failed to route Destroy")

- listen: all items PASS (step 5).
- travel (`exec servertravel DM-Sever` on the dedicated server): the server brings DM-Sever up, the client
  follows (`Welcomed by server: LEVEL=DM-Sever`, its probe shows DM-Sever), rejoins and spawns again; the
  server's probe shows its pawn. Observation: after the travel the client logs in as `Name=Anubis` (its
  travel URL is rebuilt from its own settings, the command-line `?Name=NetA` is not kept), so the check
  accepts any name. Then, on Ctrl+C of the server, `Critical: ActorChannel Package.ActorChannel failed to
  route Destroy` from `UObject::StaticExit` -> `PurgeGarbage` -> `ConditionalDestroy` (gdb, scratch
  travelgdb.sh, breakpoint on `FTraceLog::operator()` with `pDev==GError` armed only after the SIGINT --
  armed from the start it slows the server too much). Needs >= ~2 min of play after the travel; a quick exit
  is clean. gdb on the object: the vtable pointer is not a vtable (gdb prints a raw value, no `<vtable for
  ...>`), `Index` is garbage, `Connection == NULL`, `bClosing=1`, RelevantTime = the last seconds of the run:
  GObjObjects still holds a pointer to a FREED actor channel of the live connection (glibc reused its first
  words). Same Critical on both clients at exit in the lag run. Being chased with an AddressSanitizer build of
  ucc-bin (build-asan, `cmake --preset native -B build-asan` + `-fsanitize=address`).
- disconnect: PASS: `exec disconnect` on NetA -> server conns 2 -> 1, NetA's PRI gone; NetB frozen with
  SIGSTOP (socket open, no ICMP) dropped by the server after 31 s (ConnectionTimeout 30); a client killed
  outright is dropped after 3 s (the closed socket's ICMP port-unreachable, TcpNetDriver); NetC joins and
  spawns afterwards. `Received invalid actor class` lines are now required to be explained one-for-one by
  server-side `persistent actor DefaultPhysicsVolume ... has no package-map index` lines.
- lag (Epic's DO_ENABLE_NET_TEST switches `PktLag=50 PktLoss=5` on server and both clients, i.e. ~100 ms
  round trip, 5 % loss each way): every gameplay item PASS (join, spawn, place, seeother 0.1/0.5, othermoves
  500.8, hit 140->124, kill after 9 shots with scores agreeing everywhere, health seen 124 on all three);
  `.clean` FAILS only on the exit-time Critical above, on both clients.

### Step 7: glshot PASS (looked at); AddressSanitizer build; two findings on the way

- glshot (NetA with the GL renderer under Xvfb/llvmpipe, NetB null; NETPLACENEAR NetB 350 units from NetA,
  NETAIM on NetA, `shot`): PASS; `logs/net/glshot-NetA-sees-NetB.png` LOOKED AT: third-person NetA in a
  DM-Deadbolt corridor, NetB's character standing ahead with its name label "NetB" and the lock-on reticle on
  it, scoreboard "NetA 0 / NetB 0", HUD health/adrenaline bars and the three weapon slots.
- ASan ucc-bin (`cmake --preset native -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address ..."`, target UCC,
  run as `System/ucc-bin-asan`, `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:...`), first findings, both before
  the network part:
  1. global-buffer-overflow at startup: `appPlatformInit` (Core/Src/UnUnix.cpp, from the UT2004 base)
     `appSprintf(GMachineCPU, "%s %s", Brand, Model)` + `appStrcat(" @ N MHz")` into UC2's
     `GMachineCPU[32]` (Core.cpp:232; UT2004's is 1024). Port defect (UT2004 code over UC2's smaller
     buffer), overwrote the next global. Fixed: bounded `appStrncpy`/`appStrncat` (5 sites).
  2. heap-buffer-overflow READ of 1 byte in `UObject::ProcessInternal` (Core/Src/UnCorSc.cpp:5382, Epic's
     interpreter): after the return expression it peeks `*Stack.Code == EX_DebugInfo` one byte past the end
     of a function's Script array. Epic's (non-Xbox block), benign (heap slack), left alone and recorded;
     the ASan build is being rebuilt with `-fsanitize-recover=address` so it keeps going past it.

### Step 8: soak PASS (10 min, 2 clients + 2 bots)

`NET_SOAK_MIN=10 tools/dev/net-test.sh soak`: server `DM-Deadbolt?NumBots=2` (bots Brock and Jackhammer),
NetA and NetB wander (pad forward + right stick turning + Fire every 3 s; Fire also respawns). After 10.0 min all
three processes alive; server PRIs (score, deaths) Brock (7,8), Jackhammer (9,3), NetB (-1,5), NetA (0,5) -- the
bots fight and kill the wandering clients, so damage/death/respawn ran for 10 minutes. RSS sampled every ~35 s
(`logs/net/soak-rss.txt`): server 37.3 -> 38.0 MB; NetA 86.6 -> 97.5, NetB 85.6 -> 99.0 MB, rising fast in the
first ~2 min (new effects/sounds) then +1-3 MB per 5 min, flattening. `.clean`: 0 Critical/assert/overflow/bunch
lines in all three logs (the exit-time Critical of step 6 did not occur here), 2 invalid-class lines both
explained. Not a leak test at hour scale.

### Step 9: the exit-time "failed to route Destroy" -- cause and fix (ASan with -fsanitize-recover)

ASan (`-fsanitize=address -fsanitize-recover=address`, `halt_on_error=0`), server + client, servertravel,
~2 min of play, Ctrl+C (scratch travelasan.sh). Findings, in order:
1. heap-use-after-free in `main` (UCC.cpp after `Commandlet->Main`): the commandlet object is referenced by
   nothing, so the garbage collection inside the server's LoadMap (UnGame.cpp:2133) freed it while its Main()
   ran (freed by `~UServerCommandlet` from `RemoveObjects`). Epic's UCC.cpp and UT2004's have the same lines.
   Fix: `Commandlet->AddToRoot()` after ConstructObject (UCC/Src/UCC.cpp); StaticExit tags everything
   unreachable, so it is still purged at exit.
2. heap-use-after-free in `UObject::ConditionalDestroy` (UnObj.cpp:408) during StaticExit's purge, freed by
   `~UActorChannel` from `UNetConnection::Destroy` (UnConn.cpp:125), called from `UChannel::RouteDestroy`
   (UnChan.cpp:178), called from `UActorChannel::Destroy`, called from the purge's ConditionalDestroy of THAT
   SAME channel. When the collector reaches a channel before its connection (after a travel or under packet
   loss the new connection's channels reuse lower object indices), RouteDestroy destroys the connection, whose
   Destroy `delete`s all its channels -- including the one whose Destroy is still on the stack. Epic's
   allocator left freed memory readable, glibc's does not -> the outer ConditionalDestroy read garbage flags
   -> `Critical: ActorChannel ... failed to route Destroy` (and in general, undefined behaviour).
   Fix (Engine/Src/UnConn.cpp `UNetConnection::Destroy`): while `GIsGarbageCollecting`, ConditionalDestroy the
   channels instead of deleting them; the collector deletes them in its second pass (they are unreachable
   with their connection). Outside garbage collection unchanged (`delete`).
3. With 2 fixed, no more use-after-free, but still `failed to route Destroy`: the nested ConditionalDestroy
   (connection -> channel) restores the flags it found, which the outer call had cleared of RF_DebugDestroy,
   so the outer check fails although UObject::Destroy did run. Epic's routed-destroy design and its
   non-final debug check (`#ifndef FINAL_RELEASE`, UnObj.cpp:403) do not fit together; the Xbox build skipped
   the check. Fix (Engine/Src/UnChan.cpp `UChannel::RouteDestroy`): after the connection's ConditionalDestroy
   destroyed this channel, set RF_DebugDestroy before returning 1 (the object is still allocated thanks to 2).
   Result: ASan run ends with `Log file closed`, 0 Critical, 0 use-after-free.
4. Remaining ASan reports, all 1-byte heap over-READS in Epic's script interpreter, left alone (benign: they
   read a byte of FArray slack; noted for the script milestone): `ProcessInternal` peeking `EX_DebugInfo`
   after a function's last byte (UnCorSc.cpp:5382), and `execLess_IntInt`/`execEqualEqual_IntInt`
   `P_FINISH` reading past the end of a class's Script when evaluating a replication condition from
   `AActor::ProcessRemoteFunction` (UnScript.cpp:3582: `FFrame(..., RepOffset).Step`).
Whose: 1 Epic (UCC, also UT2004); 2 and 3 Epic's net code, latent on the shipped Xbox build (FINAL_RELEASE,
Epic's allocator); the GMachineCPU overflow of step 7 the port's (UT2004 base). Removed the temporary
RouteDestroy diagnostic.

## The join sequence as this build does it (file:line, this branch)

1. Client `UGameEngine::Browse` (UnGame.cpp ~1391, network URL) -> `new UNetPendingLevel` (UnPenLev.cpp:34):
   loads `ini:Engine.Engine.NetworkDevice` (= IpDrv.TcpNetDriver), `InitConnect`, sends `HELLO REVISION=0
   MINVER VER`; `TryPreloadingLevel` loads the map early only when reconnecting to the same host (UC2/Epic
   "moelfke" addition; used by the travel path).
2. Server `ULevel::NotifyReceivedText` (UnLevel.cpp:1059): `HELLO` (1120) -> `CHALLENGE`; client
   `NotifyReceivedText` (UnPenLev.cpp) answers `NETSPEED` + `LOGIN RESPONSE URL=<options>` (`?Name ?Class
   ?Team ?EnergyWeapon ?ExplosiveWeapon ?ThirdPerson ?NumClients`); server `LOGIN` (1197) -> GameInfo
   PreLogin -> `WelcomePlayer` (1017): one `USES GUID PKG FLAGS SIZE GEN FNAME` per package of its map
   (67 for DM-Deadbolt, incl. Editor and UCMutators), `DLMGR`, UC2's `LOAD CLASS=` (assets the client must
   DLO before joining), `WELCOME LEVEL GAME LONE`.
3. Client on `USES`: `CreatePackage` placeholder + FPackageInfo; on `WELCOME`: checks each package is
   present (`appFindPackageFile` with GUID), else download (ChannelDownload; `IpDrv.HTTPDownload` does not
   exist in UC2's IpDrv.u: warning on the server, harmless).
4. Client `UGameEngine::Tick` AttemptLoadPending (UnGame.cpp ~3632) -> `LoadMap(URL, Pending)`: level load,
   `FScriptPatcher::ApplyPatches` (step 1/2 fix), BeginPlay, client-side actor cull, then VerifyPackages
   (`GetPackageLinker` per USES entry with the GUID, `VerifyLinker`), `PackageMap->Compute()` (step 3 fix),
   `HAVE` for generation mismatches; then `SendJoin` (UnPenLev.cpp:665) once UC2's dynamic loads finished.
5. Server `JOIN` (UnLevel.cpp:1232) -> GameInfo Login -> PlayerController spawned (`UCPlayerControllerN got
   player TcpipConnection`, `Join succeeded: <name>`); the client is a spectator until Fire; before the
   match starts the DM waits for every connected player to press Fire (`UCDeathmatch0: START MATCH`).
6. Actor replication: `ULevel::ServerTickClients` (UnLevTic.cpp ~1455) opens actor channels (class must be in
   the package map); `UActorChannel::ReplicateActor` (UnChan.cpp ~1420) sends persistent actors by package-map
   index and transient ones by class + compressed location.

What the Xbox build did differently (from the source, not observed): FINAL_RELEASE (no check(), no
"route Destroy" check), Epic's allocator (freed memory readable), XNet/system-link addressing
(`TcpNetDriverXbox.cpp`, `RETRY_XNET`/`TRY_LAST_MATCH`, voice): all `_XBOX`-only and compiled out here.

## Notes for the WebRTC net driver (M11)

- The interface to implement is UNetDriver (`InitConnect`, `InitListen`, `TickDispatch`, `LowLevelDestroy`,
  `LowLevelGetNetworkNumber`) + UNetConnection (`LowLevelSend`, `LowLevelGetRemoteAddress`, `LowLevelDescribe`),
  as UTcpNetDriver/UTcpipConnection do (IpDrv/Src/TcpNetDriver.cpp). Packets are unreliable datagrams up to
  `MaxPacket` (WINSOCK_MAX_PACKET) with the engine's own reliability on top: an unordered, unreliable WebRTC
  data channel (`ordered:false, maxRetransmits:0`) is the matching transport.
- Address assumptions in the join path: `UNetPendingLevel` compares `LastURL.Host == URL.Host` to decide on
  preloading (UnPenLev.cpp:63); TcpNetDriver identifies connections by `sockaddr_in` and rate-limits logins by IP
  (`MaxConnPerIPPerMinute`, default 5 -- all test clients come from 127.0.0.1, and every peer of a WebRTC
  relay/signalling setup may share one pseudo-address: give each peer a distinct id string); a closed client
  socket is dropped at once through ICMP port-unreachable (TcpNetDriver), a WebRTC driver must turn a data
  channel close into the same; otherwise the 30 s ConnectionTimeout applies.
- `UdpBeacon` (LAN discovery, ServerActors line commented out in Default.ini) and master-server code are IP
  only and not on the join path.
- Both ends must run this branch's fixes: the package-map count (step 3) changes what a client and a server
  must agree on; a fixed client against an unfixed server (e.g. the old wine `ucc.exe`) can desync.
- The test harness carries over: `-nettest=`/NETPROBE work over any driver; only the URL and the server start
  differ.

### Step 10: full run from a fresh tree; a harness weakness found (placement did not stick)

`NET_SOAK_MIN=10 tools/dev/net-test.sh` (fresh `run/net`, commit 1b2b... see `logs/net/full-run.txt`):
dedicated1, dedicated2, listen, travel, disconnect, glshot all PASS, 0 Critical anywhere; lag `.hit/.kill/
.healthseen` FAILED this time (no damage in 40 shots), `.clean` PASS. Cause (from the logs, not a net defect):
NETPLACENEAR's spot was not where NetB stayed -- server probe 1.5 s after `placenear NetB ok
loc=(255.7,3574.3,-386.1)` shows NetB at (-638,1645) under lag and at (605,2967) even in the passing
dedicated2 run (DM-Deadbolt has jump pads/slopes near there; server-authoritative ServerMove,
UnController.cpp:369, keeps the server's position, so the pawn itself moved). In the passing runs NetA still
hit NetB at the new distance; under lag NetB ended 811 units away and the race weapon did not reach. Fix
(harness + test command): NETPLACENEAR takes an optional first heading index and reports the index it used;
`place_near()` in net-test.py re-probes 1.5 s later and moves on to the next heading until the pawn stays
within 30 units. `.place` now means "placed and still there".

### Step 11: why placements slid back -- UC2's ServerMove rewind; NETPLACE fixed; full suite green

With the verifying `place_near` (step 10) the lag and listen runs still failed `.place`: after EVERY placement
the server pawn was back at the same spot 1.5 s later (`logs/net/lag-srv.log`: six headings tried, NetB always
back at (-1281.2,3483.7,-34.2)). Cause: UC2 (Epic "moelfke") returns the pawn to `LastServerMoveLocation` at the
start of every ServerMove (UnController.cpp:413) and in the server's pawn tick (UnLevTic.cpp:401), to undo
physics run between client moves; a server-side FarMoveActor that does not also update that field is undone by
the next move. NETPLACE/NETPLACENEAR now record the new spot there (`KeepServerMoveSpot`, UC2NetTest.cpp), as a
client move would. Rerun (`tools/dev/net-test.sh dedicated2 listen lag`): all items PASS, placements stick
(`... (and still there 1.5 s later)`).

Open question for gameplay (not tested, not changed): any other server-side teleport of a live player pawn
(script `SetLocation` from a Teleporter or a kismet-style event) would be undone the same way unless that
code also resets `LastServerMoveLocation` (PlayerController.Restart does, UnrealScript line 3323). Respawns
are fine (Restart). Worth a look when maps with teleporters are played online.

Full suite before this fix (`logs/net/full-run2.txt`, fresh tree): dedicated1, dedicated2, travel, disconnect,
glshot PASS; listen and lag failed only `.place` (the cause above), their hit/kill/health items PASS.

### Step 12: N4 -- Linux client against this branch's Windows ucc.exe under wine: PASS

`cmake --preset windows && cmake --build build-windows -j4` (capped 5G), `tools/dev/mkrun-uc2.sh build-windows
netwin`, `tools/dev/net-test.sh wine` (prefix `<WorkUC2>/.cache/wine-net`, created by m12-wine.sh's wineboot;
the wine-m12 prefix untouched): `wine.join` (server `Join succeeded: NetA`), `wine.spawn` (server pawn at
(1229.6,-672.9,-130.1)), `wine.move` (the wine server sees NetA move 630.0 units, client vs server 0.1),
`wine.packagemap` (the 67-line PackageMap tables of the MinGW server and the Linux client identical),
`wine.clean`; nothing left listening, no wineserver left.
The M12 smoke check `netjoin` (tools/dev/m12-wine-smoke.sh) upgraded from "handshake seen" to "joined and
spawned" (pad Fire through `-inputtest=`, SDL2 probe pawn=1, server `Join succeeded`, 0 assertions); trees,
output folder and port made overridable (M12_TREE, M12_REF, M12_OUT, M12_NETPORT; defaults unchanged). Run on my
trees: `M12_TREE=run/netwin M12_REF=run/net M12_OUT=logs/net/m12smoke M12_NETPORT=7851
UC2_WINEPREFIX=.cache/wine-net tools/dev/m12-wine-smoke.sh netjoin` -> `PASS netjoin: ... server 'Join
succeeded: Player-1', client pawn=1 loc=(1977.8,583.7,-34.1); 0 assertions; server shut down cleanly,
Critical 0`. With the default trees it needs both run/m12 and run/m12-native rebuilt from a branch with these
fixes.

### Step 13 (checkpoint after the second cut-off): gate state

Done before the cut-off (logs under `<WorkUC2>/logs/net/`):
- Final fresh-tree run of every check except soak, at b78b396 (`final-run.txt`, finished 09:30, rc=0):
  dedicated1 (8 items), dedicated2 (9), listen (8), travel (6), disconnect (6), lag (9), glshot (3), wine (5),
  leftovers: ALL PASS. glshot picture looked at again (NetB with its label and lock-on reticle in a
  DM-Deadbolt corridor, scoreboard NetA 0 / NetB 0).
- Soak (10 min, 2 clients + 2 bots): PASS twice (`full-run.txt` 09:03 and step 8), on code before 7bb4b53 /
  3047b6e, which changed only the test placement command (soak does not place). Rerun at the tip below.
- Gate (a) native: `capped 5G -- cmake --preset native && cmake --build build-native -j6` rc 0
  (`gate-native-*.log`, 09:17, tip). wasm: `source env.sh && cmake --preset wasm && cmake --build build-wasm
  -j6` rc 0 at 09:01 (before 3047b6e, a test-command change in Engine/Src/UC2NetTest.cpp): rebuilt below.
- Gate (d) on a fresh tree `run/netreg`: `ucc-bin verifyclasses` exit 0, `VERIFY: all native class layouts
  match`; `ucc-bin UC2ALAudio.UC2AudioSelfTest` -> `39 checks, 0 failed`; `M10_TREE=netreg
  M10_OUT=logs/net/m10 tools/dev/m10-controls.sh keyboard`: first run FAIL 14/16 (`kb-q-switches-back-to-ranged`,
  `kb-alt-fires`), second run PASS 16/16, and PASS 16/16 with the pre-branch m12 uc2.bin in the same tree
  (M10_LAUNCH) -> recorded as a flaky first run under machine load, not a regression (not proven);
  `tools/dev/run-uc2-shot.sh net gate-deadbolt 150 0 DM-Deadbolt -glerrors` -> `UC2GLDrv: frame 1200, 0 GL
  errors so far`, 0 Critical (a 60 s run reaches only ~400 frames under llvmpipe, before the first count line).
- Gate (c): every `.clean` item PASS in the final runs = 0 Critical/assert/overflow/bunch lines in every
  process's log.

### Step 14: gate completed at the tip (15374da)

- wasm rebuilt at the tip: `source env.sh && cmake --preset wasm && cmake --build build-wasm -j6` (capped 5G)
  rc 0, `[100%] Built target SDLLaunch` (`wasm-build2.log`; UC2NetTest.cpp recompiled).
- Soak at the tip, fresh `run/net` (`final-soak.txt`): bots, alive (10.0 min, all 3 processes; server PRIs
  Sobek 6/2, Jackhammer 6/2, NetA -2/9, NetB 0/1), rss (server 38->38 MB, NetA 83->99, NetB 82->96, flat after
  ~2 min), clean, leftovers: PASS. Memory ceilings used: tests 6G (one scope per run), builds 5G, single runs
  2G; none hit (no exit 137 in any run).
- After the run nothing of mine listens on 78xx; the four ucc-bin servers on 7880-7883 belong to another agent
  (`run/m13-census-*`), not touched.

## Behaviour list (labels as the brief asks)

Asserted by the script (net-test.sh, final runs): join to a dedicated server (1 and 2 clients), level up on the
client, spawn on Fire (all players must press before the match starts), UC2 URL options reaching the server
PRI, server sees client movement, client/server positions agree (<1 unit at rest), each client sees the other
at the server's position, movement of one changes what the other sees, a hit lowers health identically on
server/attacker/victim, a kill changes score/deaths everywhere, listen server + client (same items), servertravel
to DM-Sever with the client following, rejoining and respawning, clean disconnect, frozen client dropped after
ConnectionTimeout (31 s), killed client dropped (0-3 s via ICMP), rejoin afterwards, all of that under 100 ms RTT
+ 5 % loss each way (Epic's PktLag/PktLoss), 10-min soak with 2 bots (alive, RSS flat, clean), Linux client vs
this branch's MinGW ucc.exe under wine (join, spawn, move, identical package maps), 0 Critical in every log.
Seen in a screenshot only: the GL client rendering the other player (label, reticle, scoreboard).
Implemented/unchanged, never exercised: file download of a missing package (ChannelDownload), HTTP redirect
download (class missing in UC2's IpDrv.u), team games/CTF online, splitscreen child connections, voice, LAN
beacon/server browser, admin login, match end by frag/time limit (travel was tested via servertravel only),
more than 3 clients, teleporters with players online (see step 11).
Not working / observations: one `Received invalid actor class` per connection (run-time DefaultPhysicsVolume
has no package-map index; Epic's behaviour, harmless); after a travel the client logs in with its character
name ("Anubis"), not the command line's `?Name=`.

## Packet loss during the handshake (branch `net-loss`, from main 0f44cbf)

### Step L1: the coordinator's lag hang reproduced deterministically; two causes found

Coordinator's evidence (`logs/coord/net-lag-hang/`): in a `lag` run (PktLag=50 PktLoss=5) NetA sent HELLO,
the server answered CHALLENGE, and then neither side logged anything for that connection for 3.5 minutes; no
timeout on either side.

Test tools (our code, compiled under Epic's `DO_ENABLE_NET_TEST` like PktLoss/PktLag):
- `NETDROP=<spec>[,<spec>]` (UC2NetTest.cpp; hooks in `UNetConnection::Serialize` for sent control text,
  `UControlChannel::ReceivedBunch` for received text, `UNetConnection::FlushNet` for the drop): `TEXT[#n][+k]`
  loses the packet that carries the n-th control message starting with TEXT on a connection (plus k more);
  `<TEXT[#n][+k]` loses the next k packets after receiving it. The packet is lost as on the wire: id, lag
  bookkeeping and LastSendTime advance as for a sent packet. (First version crashed: UE2's `TArray::AddItem`
  assigns into raw memory, wrong for elements holding an FString; placement-new copy instead.)
- `NETPKTID=<n>` (UnConn.cpp constructor): this side's packet ids start at n and everything before counts as
  acked, i.e. a connection that has been running; lets a short test cross the packet-id wrap. (Without also
  setting OutAckPacketId the first ack produced 16k virtual NAKs and no actor channel opened -- an artefact of
  the knob, not of the engine.)
- `tools/dev/net-test.py` check `handshake-loss`: one server + one client per case, the drop on one side;
  item PASS = joined, the drop really happened, spawned, server sees the pawn.

Results BEFORE any fix (NET_DROP_LIMIT=40 s):

| lost packet | side | joins? |
|---|---|---|
| HELLO (client's first packet) | client | yes, 3.2 s (channel 0 not yet open-acked -> UChannel::Tick resends after 1 s) |
| CHALLENGE (server's first packet, id 0) | server | NO, hangs (client stays at HELLO) |
| NETSPEED / LOGIN | client | yes, 2.4 s |
| USES #10 | server | yes, 2.4 s |
| WELCOME | server | yes, 2.4 s |
| JOIN | client | yes, 2.2 s |
| server's 3 packets after JOIN | server | yes, 2.2 s |
| WELCOME just after the packet-id wrap (NETPKTID=16375) | server | NO, hangs (client stuck in the USES list) |
| (wrap alone, nothing lost) | - | yes, spawns |

Mechanism (UnConn.cpp, all Epic's UC2 code; the port changed nothing in UnConn.cpp, epic-sources diff empty):
- Lost reliable bunches are resent only on a NAK, and a NAK is deduced in `ReceivedPacket` from an ack of a
  LATER packet: `if( AckPacketId>OutAckPacketId ) for( Nak=OutAckPacketId+1; Nak<AckPacketId; ... )`
  (UnConn.cpp ~513). Acks ride on every packet (UC2 acks every received packet, ack-only ones too, at the end of
  ReceivedPacket ~691), so the ack stream itself carries later packet ids: a lost packet is normally noticed
  within a round trip. The only timer-based resend is `UChannel::Tick` for channel 0 while it is not
  open-acked (UnChan.cpp:309), i.e. on the side that opened it (the client).
- Cause 1, first packet: UC2 initialises `OutAckPacketId(0)` (UnConn.cpp:40; UT2004 has `-1`,
  engine-ut2004 UnConn.cpp:34) and the first packet a connection sends has id 0 (OutPacketId is never set, the
  "Zero reserved for invalid id" comment in UnConn.h is not implemented). Packet 0 is therefore never in a NAK
  range: if it is lost, only the channel-0 timer could resend it. On the client that timer exists (HELLO
  recovers); on the server channel 0 was opened by the client and is open-acked at once, so nothing ever resends
  the server's packet 0 = CHALLENGE. The server sends no keep-alives before the challenge is passed
  (`HasPendingData`, UnConn.cpp:296, same idea as UT2004's `PassedChallenge`), the client keeps sending
  keep-alives and the server keeps acking them, so neither side times out: a silent hang, exactly the
  coordinator's log.
- Cause 2, packet-id wrap: UC2 ("superville") made packet ids `_WORD`s that wrap `% (MAX_PACKETID-1)` (UnConn.cpp
  ~388) but kept plain `>` comparisons (UT2004 used INTs with `MakeRelative`). After ~16k packets (~4.5 min at
  60/s, every real game) an ack of a small post-wrap id is never `> OutAckPacketId`, so no NAK is ever deduced
  again: any reliable bunch lost after the wrap is never resent (here: WELCOME). The soak did not show it because
  it has no packet loss.
- Not a property of the simulation: PktLag/PktLoss drop or delay at FlushNet, NETDROP drops at the same point;
  a packet lost on the wire is equally invisible to the sender. The `Delayed` queue is flushed in
  `UNetConnection::Tick` for every connection (UNetDriver::TickFlush), so it is not starved.
- Whose: Epic's UC2 protocol code (both causes), not the port. On a LAN/system link (the Xbox's main case) loss
  is rare; over the internet 5 % loss on the first server packet hangs 1 join in 20.

### Step L2: the fix (wrap-aware acks, packet 0 NAK-able) and a handshake give-up; compatibility

Fix (Engine/Src/UnConn.cpp):
- `PacketIdDelta(A,B)`: signed distance modulo `MAX_PACKETID-1` (the range FlushNet wraps ids in), used for the
  incoming packet order (`ReceivedPacket`, loss statistics only) and for the ack/NAK decision; NAK ids are
  computed modulo the same range. UT2004 reaches the same with INT ids + `MakeRelative`.
- `OutAckPacketId` and `InPacketId` start at `MAX_PACKETID-2`, "the id before 0" (UT2004: -1), so a lost packet
  0 lies in the first NAK range.
- Wire format unchanged; the change is local to the side that SENDS a lost packet (it decides from incoming acks
  what to resend).
Defence (Engine/Src/UnPenLev.cpp `UNetPendingLevel::Tick`): a pending connection that has not got WELCOME after
`InitialConnectTimeout` (20 s, [IpDrv.TcpNetDriver]) fails through the engine's normal path (`Error` =
ConnectionFailed -> `Pending connect ... failed` -> UCMenu.ConnectionFailureMenu). It matters only against an
unfixed server; downloads start after WELCOME and are not limited by it.
Test switch for the matrix: `-NETOLDACKS` (UnConn.cpp, DO_ENABLE_NET_TEST): this process keeps Epic's UC2 ack
logic (OutAckPacketId 0, plain comparisons).

`tools/dev/net-test.sh handshake-loss handshake-compat` after the fix: every handshake-loss case joins (2.0-2.6 s
after the client started, i.e. no slower than without loss): hello, challenge, netspeed, login, uses, welcome,
join, afterjoin, wrap (server's WELCOME as packet 10 after its wrap), wraponly, wrapclient (client's JOIN as its
packet 0 after its wrap). Compatibility (`handshake-compat`):

| case | result |
|---|---|
| fixed server, unfixed client, server loses CHALLENGE | joins 2.2 s |
| unfixed server, fixed client, server loses CHALLENGE | no join; client gives up after 20 s, ConnectionFailureMenu |
| fixed server, unfixed client, client loses LOGIN | joins |
| unfixed server, fixed client, client loses LOGIN | joins |
| unfixed client, its JOIN = its packet 0 after its wrap | no join, client stays in the level without a player |
| unfixed server, server loses WELCOME after its wrap | no join; client gives up after 20 s, ConnectionFailureMenu |

So: each end's fix covers the packets that end sends. A fixed client against an unfixed server can still lose
the server's packet 0 or post-wrap packets (now ending in the failure menu instead of a silent hang), and an
unfixed client can still lose its own; both ends of a game should run the fix (they are the same build here).

### Notes for the WebRTC driver: what packet loss means on an unreliable/unordered data channel

- The engine's reliability (UnConn.cpp/UnChan.cpp) assumes datagrams that can be lost, duplicated or reordered,
  and detects loss ONLY from acks of later packets (virtual NAK); acks ride on every packet both ways, and
  each side sends keep-alives every KeepAliveTime (0.2 s) once the handshake allows it (client always, server
  after the challenge). An unordered, `maxRetransmits:0` data channel delivers exactly that model, so the engine
  needs nothing extra from it -- but it needs the two fixes of step L2 on both ends: without them the first
  packet a side sends (server: CHALLENGE) and every packet after ~16k packets (~4.5 min) are never resent once
  lost, which an unreliable channel will do as surely as UDP.
- Do not "fix" it by making the channel reliable/ordered: the engine would then see head-of-line blocking and
  its own resends on top; if a reliable channel is ever used, set `bInternalAck`-style semantics instead
  (UNetConnection already has `bInternalAck` for 100 % reliable transports such as demo recording).
- Use the same NETDROP=/NETPKTID= switches (they act in UNetConnection::FlushNet, above any driver) and
  `net-test.py handshake-loss` against the WebRTC driver; they test the engine's side regardless of transport.
- A data channel that closes must close the UNetConnection (State=USOCK_Closed) at once; otherwise only the 30 s
  ConnectionTimeout ends it, and a pending client now gives up after 20 s without WELCOME.
