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

## Step 4: first proofs at 64-bit (2026-10-10, load 30-40 from seven other agents' builds)

- Baseline census (main's code + the new census, `CENSUS_NAME=d12-c64-base CENSUS_PORT=7960 capped 5G 2400 --
  map-census.sh build-native64 15 2`): `maps: 58  PASS: 56  FAIL: 2` (DM-Ascension, DM-ColdFusion: `crash handler`, a
  second SIGTERM each). Shutdown times of the 56: 1.0-1.6 s (the 1-second poll).
- `ucc-bin verifyclasses` (fixed 64-bit, `capped 2G 600`): exit 0, `All native class layouts match`.
- Deterministic test `logs/d12/doubleterm.sh` (a server on DM-Deadbolt, `kill -TERM`, 0.02 s, `kill -TERM`; or one TERM;
  or a SIGSEGV), `capped 2G 200` each:
  - main's binary (run tree d12-c64-base-0), 3 runs: 3/3 `Aborting.` + `Developer Backtrace` (main thread interrupted in
    `appSprintf` <- `GetFullName` <- `RemoveObjects`, i.e. inside StaticExit), shutdown 1.1-1.2 s.
  - fixed binary, 3 runs: 3/3 `Signal: exit already requested; this one is ignored (SIGKILL ends the process now).`, no
    backtrace, log closed, shutdown 0.3 s (0.02 s gap + exit).
  - fixed, one TERM: exit 1, 0.3 s, log closed. Fixed, SIGSEGV: crash report + `appExit`, log closed, ends in 0.1 s.
- Build note: the owner's load rule (coordinator, 02:17): `-j2` while load > 24; my two `-j4` builds were stopped (their
  scopes, by name) and restarted at `-j2`, one after the other.

## Step 5: D-2, the two class packages, and the first package download (2026-10-10)

Changes:
- `Source/IpDrv/Inc/HTTPDownload.h`: `DECLARE_CLASS(UHTTPDownload,UDownload,CLASS_Transient|CLASS_Config,IpDrv)` (was
  Engine), as engine-ut2004. `Source/Editor/Inc/UnPrefab.h`: `DECLARE_CLASS(UPrefab,UObject,CLASS_SafeReplace,Editor)`
  (was Engine): the package Epic's Editor DLL registered it under (IMPLEMENT_CLASS's `_MSC_VER` branch uses the DLL's
  `GPackage`). engine-ut2004 kept Prefab under Engine (it never builds the editor); Epic's DLL build is the reference
  here because a prefab saved by Epic's UnrealEd would import `Editor.Prefab`. Nothing shipped names either class.
- `Source/Editor/Src/UC2EpicClassOrder.h` regenerated (`python3 tools/dev/epic-class-order.py`: 481 classes): the two
  rows now say IpDrv / Editor (epochs unchanged: 5, -1). Neither class has script, so `make`'s visiting order of
  script classes is unchanged.
- Found by exercising the download: `UHTTPDownload::Tick` treated a non-blocking `connect()` under way as a failure
  unless the error was `WSAEWOULDBLOCK`; BSD sockets say `EINPROGRESS`. Every HTTP download failed at once ("HTTPDownload:
  connect() failed", python's http.server saw the reset) and fell back to ChannelDownload. Fix as engine-ut2004:
  `Source/IpDrv/Inc/UnSocket.h` defines `WSAEINPROGRESS` = `EINPROGRESS` for BSD sockets, `HTTPDownload.cpp` accepts
  both.

Checks:
- `ucc-bin verifyclasses`: 64-bit and 32-bit both exit 0, `All native class layouts match` (`logs/d12/verify{64,32}.out`;
  the 32-bit log also `VERIFY: Editor class layouts match`).
- `tools/dev/net-test.py`: `Proc(ini_add=...)` writes the process's own ini from Default.ini plus lines (repeated keys
  cannot be added with `-ini:`); new opt-in checks `download` and `download-http` (net-test.sh header). The package:
  `Textures/RL_Hud_Movies.utx` (547 bytes) copied to `<tree>/NetDownload/NetDownloadTest.utx` and resaved by
  `ucc-bin resavepackage ../NetDownload NetDownloadTest.utx` (a new GUID, 549 bytes, PKG_AllowDownload). Only the
  server's ini has `Paths=../NetDownload/*.utx` and `ServerPackages=NetDownloadTest`; the client gets its own empty
  `CachePath`. `download-http` adds `RedirectToURL=http://127.0.0.1:<NET_PORT+1>/` and `UseCompression=False` and runs
  `python3 -m http.server` inside the same capped scope.
- `NET_TREE=d12-net NET_PORT=7866 tools/dev/net-test.sh download download-http` (32-bit build, capped 6G): 12/12 PASS
  (`logs/d12-net/net-summary.txt`): the client logs `Receiving package 'NetDownloadTest'`, its cache holds
  `<GUID>.uxx` byte-identical to the server's file and cache.ini maps the GUID to `NetDownloadTest.utx`, the client
  joins DM-Deadbolt; over HTTP the server sends `DLMGR CLASS=IpDrv.HTTPDownload PARAMS=http://127.0.0.1:7867/
  COMPRESSION=0` and the HTTP server logs `"GET /NetDownloadTest%2eutx HTTP/1.1" 200` (UHTTPDownload escapes the dot),
  no fallback. Before D-2, main's server logs had `Warning: Failed to load 'Class IpDrv.IpDrv.HTTPDownload'` once per
  client (`logs/net/dedicated2-srv.log:202`) and never sent that DLMGR; now 0 such lines.
- Regression `net-test.sh --keep-tree dedicated1`: 8/8 PASS + leftovers PASS (`logs/d12/net-summary-dedicated1.txt`).
- Not exercised: compressed HTTP download (`.uz`, `UseCompression=True`), a proxy, a map download.
