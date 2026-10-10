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

## Step 2: the "exit-time crash" is a second SIGTERM taking Epic's abort path (2026-10-10)

Reading the stdout logs (not only ucc.log) of every old census with a `Developer Backtrace` (21 logs in `logs/*/`:
m13-census, m13-census32, m13-census-m13*, f4-census, d3/d6/m14-census-*): **every one** has, on stdout,

    Signal: SIGTERM [terminate]
    Requesting Exit.
    appRequestExit(0)
    Signal: SIGTERM [terminate]
    Aborting.

No SIGSEGV anywhere. The backtraces are not crash sites: they are where the main thread happened to be when a
**second** SIGTERM arrived (in `DetachExport`, in `realloc`, in `GetPathName`, in `clock_nanosleep` of the
file-stream wait, even in `ULevel::Tick` -> `AMover::performPhysics` before the main loop had seen the first one:
f4-census OD-Cancer). `HandleSignal` (UnUnix.cpp, Epic's/UT2004's code, identical in engine-ut2004) counts exit
signals; at the second one (`SignalExit > 1`) it takes the abort path **inside the signal handler**: `printf`,
`backtrace_symbols` (malloc), `debugf` (log device), `appExit()` (`GFileStream->Destroy`, `GConfig->Exit` writes the
inis, `FName::StaticExit`, `GMalloc->DumpAllocs`), then `exit(1)` -- all on top of whatever the main thread was doing,
usually its own `appPreExit`/`StaticExit`. That gives the two symptoms:
- "crash": the handler's `appExit` and the interrupted `StaticExit` run over the same objects/heap; the log shows the
  abort backtrace; `exit(1)` then ends the process mid-purge (log closed, so the old census called it PASS).
- "hang": the main thread was interrupted while holding a non-reentrant lock (glibc's malloc arena lock: DM-Nexus was
  interrupted inside `realloc`; or the log device), and the handler then needs the same lock (`backtrace_symbols`,
  `debugf`, `GConfig->Exit`): a self-deadlock. 64bit.md step 8 saw exactly that: "main thread in a futex wait" (a lock,
  not the file-stream poll, which is `nanosleep`). OD-Prism's stdout ends at "Crash information will be saved to your
  logfile." with nothing in the log: deadlocked in `backtrace_symbols`/`debugf`. DM-Nexus's log ends at "Waiting for
  file streaming thread to finish..." because that is the last line the handler could log before `GConfig->Exit`
  (malloc) blocked; the file-stream wait itself was not the hang.

Where the second SIGTERM comes from: `timeout` (here uutils coreutils 0.10.0) signals the child **and then its own
process group**, which contains the child. `strace -f -e trace=kill,setpgid timeout -s TERM 0.3 ./twoterm`
(`logs/d12/twoterm.c`, `logs/d12/tt.trace`):

    setpgid(0, 0) = 0
    kill(3257539, SIGTERM) = 0
    kill(0, SIGTERM) = 0

Normally the two merge (a standard signal still pending is not queued twice); when the first has already been
delivered before the second `kill` (a loaded machine; strace makes it certain: "handled 2 times"), the engine sees two.
Any supervisor that signals both a process and its group (or an operator who sends TERM twice) does the same: this is
an engine bug, not only a harness artifact. Width-independent, map-independent, timing-only: about 2 in 58 under load.

Not involved: the linker/teardown order (nothing was wrong with `ULinkerLoad::DetachExport`; the interrupted
frames merely show where StaticExit was), and the net branch's teardown fixes.

Reproduced on main's code with the new census (64-bit, 2 workers, load ~30, `logs/d12-c64-base/`): DM-Ascension and
DM-ColdFusion FAIL `crash handler`, stdout again "Signal: SIGTERM ... Requesting Exit. / Signal: SIGTERM ... Aborting.".
DM-Ascension shows the other half of the hang: its abort backtrace is `usleep` <- `FileStreamingThread`: the second
SIGTERM was handled **on the file-stream thread**, whose `appExit()` -> `FFileStream::Destroy()` then waits for the
file-stream thread, i.e. for itself, for ever (the process only ended because the main thread finished its own clean
exit meanwhile and returned from `main`).

## Step 3: the fix (Core, shared code: UnUnix.cpp, UnMisc.cpp)

`Source/Core/Src/UnUnix.cpp` `HandleSignal` / `HandleInterrupt`:
1. A repeated exit signal (SIGTERM/SIGHUP/SIGQUIT after the first, no critical signal) is ignored, with an
   async-signal-safe `write` ("Signal: exit already requested; this one is ignored (SIGKILL ends the process now).").
   Epic's abort-on-second-signal ran `appExit()` in the handler concurrently with the main thread's own exit; there is
   no safe way to do that, and the duplicate is usually the same request delivered twice. Forcing stays possible:
   SIGKILL, or Ctrl-C twice (`HandleInterrupt` -> `HandleSignal(SIGINT)`, a critical signal: unchanged).
2. The first exit signal (and the first Ctrl-C) no longer calls `printf`/`debugf` (`appRequestExit`) inside the
   handler: `write()` of the same stdout lines and `GIsRequestingExit = 1`, which is all appRequestExit(0) does natively
   besides logging. The log loses the line `appRequestExit(0)` for a signal-driven exit (no tool reads it; `Exit:
   Preparing to exit.` still follows). Reason: the handler may interrupt any thread inside malloc or the log device.
3. The abort path (a real crash, or Ctrl-C twice) arms `alarm(10)` before its report and `appExit()`: when those block
   on a lock the crashed thread held, SIGALRM's default action ends the process instead of a hang (POSIX only; MinGW
   has no alarm).

`Source/Core/Src/UnMisc.cpp` `FFileStream::Destroy`: the wait for the file-stream thread is bounded (5 s) and polled
every 0.1 s (engine-ut2004's `FALAudioStreamManager::Destroy` polls at 0.1 s, but without a bound; Epic's `appSleep(1)`
polled once per second, so every exit took >= 1 s). After 5 s it logs a warning and returns without freeing the
streams (the thread may still use them). This covers appExit() running on the file-stream thread itself (above).

engine-ut2004 has the same `HandleSignal`, the same `HandleInterrupt`, and an unbounded wait: the same latent bugs; it
does not show them because nothing there signals a server twice in its tests (not checked by running).
