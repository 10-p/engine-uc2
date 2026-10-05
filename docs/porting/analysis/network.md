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
