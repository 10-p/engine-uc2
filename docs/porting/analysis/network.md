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

Fix (Engine/Src/UnScriptPatcher.cpp ApplyPatches): BeginLoad moved up to before `Update->Apply()`, EndLoad
added on the "could not apply, retry later" branch. The success branch keeps its single EndLoad after the
bytecode swap, so the exports AppendExports created (queued in GObjLoaded, UnLinker.cpp:846) are still
serialized from the patch reader exactly as before (previously they waited in GObjLoaded with count 0 until
that same EndLoad).

Open doubt noted for later: `UPackageMap::Compute` (Core/Src/UnCoreNet.cpp:276) takes ObjectCount/NameCount
from `ExportMap.Num()`/`NameMap.Num()`, which include exports/names the patcher appended. Client and server
must therefore have patched the same packages before Compute, or every later package's ObjectBase shifts.
The client patches every USES package (placeholders included) before its Compute; the server patches only what
is loaded at its first ApplyPatches (packages loaded later, e.g. during InitGame, get patched by the second
pass after Listen/Compute). To check with a per-package map dump on both sides.
