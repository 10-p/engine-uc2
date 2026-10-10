# D-1 / D-2 (owner review 2026-10-10): the dedicated-server exit-time crash/hang, and the two misregistered classes

Branch `d12-fixes` (worktree `engine-uc2-d12`), from main `2ed330f`. Logs: `<WorkUC2>/logs/d12/`. Run trees: `run/d12-*`
(`run/d12-main` is older and not this task's: not touched).

## Step 0: reading (2026-10-10)

D-1, what was known (BLOCKERS.md "Open", 64bit.md step 8, network.md steps 6-9):
- M13's censuses (`map-census.sh`, `ucc-bin Engine.ServerCommandlet <map>`, SIGTERM at 15 s) saw about 2 maps in 58 crash
  in `UObject::StaticExit -> PurgeGarbage -> RemoveObjects -> ConditionalDestroy -> ULinkerLoad::Destroy ->
  DetachExport` (64-bit OD-Overflow: SIGSEGV in DetachExport; 32-bit ACT-HallwayAmbush: in `UObject::IsValid` on the
  export) or in glibc `realloc` from `UClass::Destroy` (32-bit DM-Nexus). The crash handler (`HandleSignal`,
  UnUnix.cpp) then prints the backtrace and calls `appExit`, whose `GFileStream->Destroy()` (UnMisc.cpp) sets
  `Destroyed=1` and polls `while (Destroyed == 1) appSleep(1)` for the detached file-stream thread (UnUnix.cpp
  `FileStreamingThread`) to set 2. DM-Nexus's log ends at `Waiting for file streaming thread to finish...` and its
  stdout at a second `Signal: SIGTERM` (the kill by PID); OD-Overflow and ACT-HallwayAmbush did reach `Log file closed`.
- A separate hang (64-bit OD-Prism, 64bit.md step 8): `appRequestExit(0)`, `Preparing to exit.`, then main thread in a
  futex wait, file-stream thread sleeping; no crash logged.
- `map-census.sh` runs `timeout -s TERM <secs>` without `-k`: a server that does not end blocks the worker forever
  and `timeout` still returns 124 (= PASS) once it is killed by hand.
- Later fixes on the net branch (network.md step 9): `Commandlet->AddToRoot()` (UCC.cpp), `UNetConnection::Destroy`
  ConditionalDestroys its channels while `GIsGarbageCollecting`, `UChannel::RouteDestroy` sets RF_DebugDestroy. None
  of them touches `ULinkerLoad`.
- engine-ut2004: `HandleSignal` identical (calls `appExit` from the crash path); its file-stream wait
  (`FALAudioStreamManager::Destroy`, ALAudioStream.cpp:571) also polls forever with no bound. So UT2004 has the same
  hang shape, not a fix to copy.
- The SIGTERM path is not async-signal-safe: `HandleSignal` -> `appRequestExit(0)` -> `debugf` (formatting, the log
  device, possibly malloc) runs inside the signal handler, on whatever the main thread was doing (64bit.md step 8
  already named this as a candidate).
- Preset `native64-asan` exists (`UC2_SANITIZE=address,undefined`).

D-2: `Source/IpDrv/Inc/HTTPDownload.h:24` and `Source/Editor/Inc/UnPrefab.h:21` say `DECLARE_CLASS(...,Engine)`.
Under `__STATIC_LINK` (UnObjBas.h:420-437) the package of a class is DECLARE_CLASS's TPackage; in Epic's DLL build
(`IMPLEMENT_CLASS`, `_MSC_VER` branch) it is the DLL's `GPackage` (IpDrv, Editor). engine-ut2004 has
`DECLARE_CLASS(UHTTPDownload,UDownload,CLASS_Transient|CLASS_Config,IpDrv)` (its HTTPDownload.h:30, from its initial
import) and keeps `UPrefab` under Engine. Neither class has script (`grep -a` finds no `Prefab`/`HTTPDownload` in any
shipped `.u`); no asset references either. `UC2EpicClassOrder.h` (generated from DECLARE_CLASS by
`tools/dev/epic-class-order.py`) lists them as `Engine` too.

## Step 1: map-census.sh sees a hang and a crash on the way out (2026-10-10)

`tools/dev/map-census.sh` (native mode; the Node mode is unchanged):
- `timeout --preserve-status -k $CENSUS_GRACE -s TERM <secs>` (grace default 30 s): a server still alive 30 s after its
  TERM is KILLed (status 137) and the map FAILs as `hang`; the census no longer blocks on it.
- `--preserve-status`: the server's own exit status is recorded (`exit=`); it is 0 or 1 for a clean exit (UCC's
  ErrorLevel is 1 when any warning was logged), so it is reported, not judged.
- New column `shutdown=`: seconds from the TERM to the end of the process (wall clock minus the time limit).
- A map now also FAILs when it ended before the TERM, when the engine's crash handler ran (`Aborting.` on stdout or
  `Developer Backtrace` in the log), when the log was not closed (`Log file closed` missing), or when a sanitizer
  build printed `ERROR: AddressSanitizer`. Columns 1-2 (status, map) are unchanged, so `m13-64bit.sh` and
  `m14-scripts-check.sh` (which `cut -f1,2`) read it as before; their own hung-server watchers become redundant
  (left alone).
- First rows (64-bit, main's code, 2 workers, load ~28): `PASS ACT-AnubisCheats exit=1 shutdown=1.1 ...`. The 1.1 s is
  `FFileStream::Destroy` polling with `appSleep(1)` (one second per poll).
