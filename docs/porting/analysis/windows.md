# M12 — Windows (MinGW-w64 i686, verified under wine) — notebook (running)

Goal (PLAN.md row M12): `uc2.exe` and `ucc.exe` cross-built on Linux with MinGW-w64 13 (posix threads), CMake
only, statically linked; verified under wine 10.0 (headless, Xvfb). 64-bit Windows is M13 (the `windows64`
preset is not chased here). Branch `m12-windows`, worktree `<WorkUC2>/engine-uc2-m12`.

Every run goes through `<WorkUC2>/bin/capped` (build 6G `-j8`; wine runs 3G), with
`WINEPREFIX=<WorkUC2>/.cache/wine-m12 WINEDEBUG=-all WINEDLLOVERRIDES="winemenubuilder.exe=d;mscoree=d;mshtml=d"`
and `wineserver -k` after every run. Logs: `<WorkUC2>/logs/m12/`. Run trees: `<WorkUC2>/run/m12*`.

## Decisions (with evidence) — filled in as they are made

1. **Platform layer for MinGW: the GCC/Unix-style Core layer with `_WIN32` branches, as engine-ut2004.**
   `Core.h` picks `UnGnuG.h` by compiler (`__GNUG__`) and `UnUnix.h` for `__UNIX__ || __MINGW32__`; `UnGnuG.h`
   has a MinGW block (no `__UNIX__`/`__LINUX__`, `<minwindef.h>`/`<basetsd.h>` for DWORD/SIZE_T, real
   `__stdcall`, `\r\n` and `\` separators, `.dll`). Epic's `UnVcWin32.h/.cpp` stay MSVC-only (inline
   `__asm`, `#pragma`s, SEH, the UNICODE-era helpers). Piece by piece:
   - file manager: `FFileManagerUnix.h` (ucc and uc2), with its `_WIN32` branches (`_mkdir`, `O_BINARY`,
     `_fullpath`, `USERPROFILE`); `__FILE_NOCASE` case folding is harmless on a case-insensitive file system
     (it only adds a directory scan when the exact name is missing); `\` and `/` both accepted by Windows.
   - time: `appSeconds` = `clock_gettime(CLOCK_MONOTONIC)` (winpthreads provides it: QueryPerformanceCounter);
     `appSystemTime64` = UnVcWin32.cpp's GetLocalTime/SystemTimeToFileTime (W1).
   - threads: `UnThreadingPosix.cpp` on winpthreads (MinGW posix thread model, linked statically), not
     `UnThreadingWindows.cpp` (not built; the template's reasoning: the recursive-mutex/event semantics are
     already proven on Linux and web).
   - dynamic libraries: `LoadLibraryA`/`GetProcAddress` (`appGetSystemDllHandle`, `appGetDllExport`) for
     OpenAL32.dll; engine packages answer the static "linked in" sentinel as Epic's static builds did.
   - console/log: stdout (unbuffered in ucc) + `Avaris.log`/`ucc.log` through `FOutputDeviceFile`; uc2.exe is a
     Windows-subsystem program, `-log` still echoes to an attached console (wine passes its stdout).
   - crash handling: the template's Win32 reporter (`SetUnhandledExceptionFilter`, `std::set_terminate`,
     SIGABRT) prints image base + rva for `addr2line`; plus Epic's Ctrl+C console handler (W2).
   - clipboard: UnUnix.cpp's process-local string (`appClipboardCopy/Paste`), the same as on Linux; the
     Windows clipboard is not used (Epic's used it; only the GUI designer and `CopyToClipboard` script need it).
   - `appBaseDir`: UnUnix.cpp's: the **working directory** + `/`, as on Linux (Epic's Windows one is the exe's
     directory from GetModuleFileName). Left so on purpose: every content path in the inis (`../Maps/*.unr`) is
     relative to the working directory anyway, so the game must be started with `System\` as working
     directory on Windows too (Explorer double-click does that; a shortcut needs "Start in"). OpenAL32.dll is
     still found when it is not (LoadLibrary searches the exe's directory first).
   - sockets: WinSock (`__WINSOCK__` chosen by `_WIN32`).
2. **Struct layout: MinGW's default `-mms-bitfields` stays** (no flag). Evidence, both by running:
   - `i686-w64-mingw32-gcc -Q --help=target` → `-mms-bitfields [enabled]`; a probe struct
     `{unsigned long a:1; unsigned char b;}` is 8 bytes with it, 4 without, so the switch is real here.
   - `ucc.exe verifyclasses` under wine → `all native class layouts match` with the default (run/m12) AND
     with a second build configured `-DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -mno-ms-bitfields"`
     (`build-windows-nomsbf`, run/m12-nomsbf, `logs/m12/verifyclasses-nomsbf.out`). The 713 checks cannot tell
     the two apart: every script-visible bitfield run is already fenced by the `GCC_PACK(4)` tags M3 added
     for GCC (a member after a run, the first member of a class), which force the same offsets in both rules.
   - Whole-engine comparison: `tools/dev/dwarf-layouts.py` over every package object of both builds (11,781
     size/offset facts each) → the only differences are `UChannel`, `UControlChannel`, `UActorChannel`,
     `UFileChannel` (UnChan.h: a `BITFIELD` run followed by `_WORD`/`BYTE` members; MS rules start a new 4-byte
     unit, SysV packs into the bitfield's unit; 4 bytes larger with MS) and `FDynamicActor` (render-side). All
     five are C++-only (built with `sizeof`, never laid out by script, never serialised as a block), so either
     rule works; the MS one is the layout MSVC 7.1 gave the shipped Windows binary and the one the Windows
     headers (SDL, WinSock, OpenAL's) are written for. `-mno-ms-bitfields` would only buy "same as Linux" for
     those five classes, at the price of a non-Windows ABI for every system struct with bitfields.
3. Calling conventions / DLL_EXPORT under static link: _pending_
4. `long` / `wchar_t` / `time_t` widths: _pending_
5. Runtime DLL policy: _pending_

## Log

### Step 0 — reading (2026-10-05)
- Template `<WorkUnreal>/engine-ut2004`: MinGW goes through `UnGnuG.h` + `UnUnix.cpp` (+ `FFileManagerUnix.h`,
  pthreads via winpthreads) with `_WIN32` branches inside them (LoadLibrary/GetProcAddress, `_fullpath`,
  `GetUserNameA`, no POSIX signals, Win32 crash reporter). `NOT WIN32` drops `LINUX`/`__LINUX_X86__`;
  `-static-libgcc -static-libstdc++`; SDL2 static + SDL2main; ucc is NOT built on Windows there (its UCC.cpp
  wanted the UNICODE-era helpers); OpenAL on Windows = a fetched PREBUILT `soft_oal.dll` renamed
  `OpenAL32.dll` (cmake/OpenALMingw.cmake). Our brief forbids downloading binaries, so OpenAL Soft is built
  from source by the same FetchContent as Linux, for Windows.
- UC2's Core (M1) already took engine-ut2004's UnUnix.cpp with those `_WIN32` branches, and `Core.h` already
  includes `UnUnix.h` for `__MINGW32__`. So the starting point is the template's path.

### Step 1 (W1) — configure and compile (2026-10-05)
Commands (all through `capped`):
```
bin/capped 4G 600 -- cmake --preset windows
bin/capped 6G 1800 -- cmake --build build-windows -j8 -- -k      # logs/m12/build-all-<n>.log
```
The preset configured on the first try (GNU 13 `i686-w64-mingw32-g++-posix`, pointer width 4, SDL2
`release-2.32.x` with its Windows backends: WASAPI/DirectSound/WinMM audio, XInput/DirectInput, WGL video).
Core then had 3 errors, Engine 10 (one file), IpDrv 11 (one header), UC2ALAudio 5 (one header), the launchers
a handful. Each fix, guarded so that only MinGW sees a difference:

| File | Problem | Fix | Guard |
|---|---|---|---|
| `Core/Inc/wordwrap.h` | `<winnt.h>` already typedefs `WCHAR` as `wchar_t` (distinct from `UNICHAR`) | file-local `WW_WCHAR`/`WW_LPCWSTR`/`WW_LPWSTR` = UNICHAR, `#define`d over the Win32 names for the two TUs that include the header (wordwrap.cpp, UnCanvas.cpp). MSVC 7.1 (no `/Zc:wchar_t`) had WCHAR == unsigned short == UNICHAR | `!_XBOX && _WIN32` branch before the portable one (MSVC takes the first branch, unchanged) |
| `Core/Src/UnUnix.cpp` `appSystemTime64` | no `localtime_r`/`tm_gmtoff` in the MinGW CRT | UnVcWin32.cpp's exact body (GetLocalTime + SystemTimeToFileTime) | `#ifdef _WIN32` |
| `Engine/Src/UnModelLight.cpp` `CompressLightmaps` | uses nvDXT, whose declarations are `#if _MSC_VER && !_XBOX`, under `#if !CONSOLE && !__LINUX__` | `&& _MSC_VER` added to the use (Epic's guards disagreed; only MinGW is neither MSVC nor Linux). Editor-only feature | `_MSC_VER` |
| `IpDrv/Inc/UnIpDrv.h` | BSD sockets chosen by compiler (`_MSC_VER`), MinGW has no `<sys/socket.h>` | WinSock by platform `_WIN32` (engine-ut2004's change); `<ws2tcpip.h>` for socklen_t; resolver thread `CreateThread` under `_WIN32` (its declaration already switched on `__UNIX__`) | `_WIN32` / `!_MSC_VER` |
| `IpDrv/Src/UnSocket.cpp` | WinSock `setsockopt` optval is `const char*` | `#if _MSC_VER` → `#ifdef _WIN32` | `_WIN32` |
| `UC2ALAudio/Src/UC2ALDevice.cpp` | library names | `OpenAL32.dll`, `soft_oal.dll` on Windows; message names the first | `_WIN32` |
| `UC2ALAudio/Src/UC2ALAudioSubsystem.cpp` | no `setenv` | `_putenv` (copies, updates the process environment that OpenAL32.dll's `_wgetenv` reads) | `_WIN32` |
| `UCC/Src/UCC.cpp` | `#if WIN32` picked FFileManagerWindows, UnThreadingWindows (not built), GetCommandLineA | those three plus `<windows.h>` become `_MSC_VER`; file manager `__LINUX_X86__ \|\| __MINGW32__`; unbuffered stdout also on MinGW | `_MSC_VER` / `__MINGW32__` |
| `IpDrv/Inc/UnIpDrv.h` | in the launchers `<windows.h>` (through UnIpDrv.h) renames `MM_MAX` (a script enum value in UCGameplayClasses.h) and `CreateEvent/CreateSemaphore/CreateMutex` (virtuals of FSynchronizeFactory, declared before in Core.h → UnThreadingPosix.h no longer overrides them) | `#undef` the four after the WinSock includes | `!_MSC_VER` inside `__WINSOCK__` |
| `Engine/Src/xbstrip.cpp` | its own `typedef unsigned int DWORD` made `Stripify(unsigned int,…)`; the engine's caller sees `DWORD` = `unsigned long` on Win32 → undefined reference | Win32 headers (`NOMINMAX`, `<windows.h>`) on MinGW | `#elif defined(_WIN32)` |

Build files:
- Root `CMakeLists.txt`: MinGW links `-static -static-libgcc -static-libstdc++` (winpthread static too: without
  `-static` the OpenAL DLL imported `libwinpthread-1.dll`); OpenAL Soft is fetched and built on Windows as
  well (was `NOT WIN32`): `OpenAL32.dll`, backends WASAPI, DirectSound, WinMM, WaveFile, Null, SSE2 codegen.
- `Source/UCC/CMakeLists.txt`: MinGW → `ucc.exe` (console), `ws2_32 winmm`, no `-export-dynamic`.
- `Source/SDLLaunch/CMakeLists.txt`: MinGW → `uc2.exe`, `WIN32_EXECUTABLE` (Windows subsystem, as Avaris.exe),
  `ws2_32 winmm`; SDL2main not used (MinGW's CRT calls `main()` for either subsystem).

Result (`logs/m12/build-all-8.log`): rc 0, `ucc.exe` 67 MB, `uc2.exe` 81 MB (RelWithDebInfo, symbols in).
`objdump -p`: ucc.exe imports ADVAPI32, KERNEL32, msvcrt, WS2_32; uc2.exe additionally GDI32, IMM32, ole32,
OLEAUT32, SETUPAPI, SHELL32, USER32, VERSION, WINMM. OpenAL32.dll: AVRT, KERNEL32, msvcrt, ole32, SHELL32,
USER32, WINMM. All Windows system DLLs.

Decision taken on the way (3, partly): **packages and exports.** On `_WIN32`, `appGetDllHandle` returns a
non-NULL sentinel for every package (engine-ut2004's code), and under `__STATIC_LINK` `UPackage::GetDllExport`
uses `FindNative`, never the OS. That is what Epic's own static builds did (UnVcWin32.cpp: `return (void*)1;`
under `__STATIC_LINK`, PC and Xbox), so no `--export-all-symbols` (engine-ut2004 has it for a lookup UC2 does
not make). The Linux build answers honestly per package (dlsym `GLoaded<Pkg>`); the difference is that
Windows "binds" package names that have no native code, exactly like Avaris.exe/the XBE.

Next: run tree with the .exe/.dll (mkrun-uc2.sh), wine prefix, `ucc.exe verifyclasses`.

### Step 2 (W2) — first runs under wine (2026-10-05)
- Run tree: `tools/dev/mkrun-uc2.sh build-windows m12` now also copies `ucc.exe`, `uc2.exe` and the build's
  `OpenAL32.dll` into `System/` (→ `<WorkUC2>/run/m12`).
- Wrapper `tools/dev/m12-wine.sh <dir> <prog.exe> [args]` (always inside `capped`): own prefix
  `.cache/wine-m12`, `WINEDEBUG=-all`, `WINEDLLOVERRIDES=winemenubuilder.exe=d;mscoree=d;mshtml=d`, the owner's
  DISPLAY/WAYLAND_DISPLAY removed (a display only through `UC2_WINE_DISPLAY`, i.e. our own Xvfb), `wineserver
  -k` + `-w` on exit, a TERM from capped stops the program first. **wine 10 on this machine is a WoW64 build:**
  `WINEARCH=win32` is refused ("not supported in wow64 mode"); the default 64-bit prefix runs the i686 exe.
- `capped 3G 300 -- tools/dev/m12-wine.sh run/m12/System ucc.exe` → Epic's banner (exit 1 = UCC's default
  ErrorLevel with no commandlet; Linux does the same).
- **`capped 3G 600 -- tools/dev/m12-wine.sh run/m12/System ucc.exe verifyclasses` → exit 0, console
  `All native class layouts match`, ucc.log `VERIFY: all native class layouts match`** — first try, with
  MinGW's default `-mms-bitfields` (the same 713 size/offset checks over Engine, IpDrv, UnrealGame, UCElements,
  UCGameplay, GUI as on Linux). See decision 2 for the A/B with `-mno-ms-bitfields`.
- `capped 3G 60 -- tools/dev/m12-wine.sh run/m12/System ucc.exe server DM-Deadbolt -log`
  (`logs/m12/server-1.out`): `Game class is 'UCDeathmatch'`, `Bringing Level DM-Deadbolt.MyLevel up for
  play (0)...`, `WinSock: version 1.1 (2.2)`, `TcpNetDriver on port 7777`, `Loading of DM-Deadbolt took 0.40 s`,
  then silent until capped's 60 s limit (exit 124). The only warning is the Linux one (`Missing Class
  Editor.TransBuffer`). Not yet proven: that it keeps ticking, and a clean shutdown — there was no way to stop
  it cleanly: the POSIX SIGTERM/SIGINT handlers are `#ifndef _WIN32`, and the Windows build had no console
  handler at all.
- Fix: Epic's `CtrlCHandler` (UnVcWin32.cpp: `SetConsoleCtrlHandler`, first Ctrl+C sets `GIsRequestingExit`)
  ported into UnUnix.cpp's `_WIN32` block as `appWinCtrlHandler` (second Ctrl+C: Windows' default
  termination, where Epic threw `appErrorf("Aborted!")` on the handler thread). Also the crash reporter's
  "Symbolise with: addr2line -e ut2004.exe" (template text) now names the running exe (GetModuleFileNameA).
  `m12-wine.sh` gets `UC2_WINE_STOP_AFTER=<s>`: SIGINT to the wine process (wine turns it into CTRL_C_EVENT),
  then 20 s grace.

### Step 3 — layout A/B and a clean server shutdown (2026-10-05)
- Layout A/B: decision 2 above. Commands:
  `bin/capped 4G 600 -- cmake --preset windows -B build-windows-nomsbf -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer
  -mno-ms-bitfields" -DFETCHCONTENT_SOURCE_DIR_SDL2=… -DFETCHCONTENT_SOURCE_DIR_OPENALSOFT=…`,
  `bin/capped 6G 2400 -- cmake --build build-windows-nomsbf --target UCC -j8`, `tools/dev/mkrun-uc2.sh
  build-windows-nomsbf m12-nomsbf`, verifyclasses under wine; then `tools/dev/dwarf-layouts.py` (new, committed)
  over all package objects of both builds and `diff`.
- Server: `UC2_WINE_STOP_AFTER=30 bin/capped 3G 90 -- tools/dev/m12-wine.sh run/m12/System ucc.exe server
  DM-Deadbolt -log` → map up as before, then at 30 s the Ctrl+C: ucc.log `Exit: Preparing to exit.`,
  `29029.2ms Unloading: Package Engine` ... `Log file closed`; i.e. the server loop ran 29 s after the load and
  shut down cleanly through the new console handler. Exit code 1 = UCC's ErrorLevel for "1 warning"
  (the TransBuffer warning; Linux reports the same `1 warning(s)`).
- First client run (old exe, null renderer): `bin/capped 3G 100 -- xvfb-run -a -s "-screen 0 1280x720x24" bash -c
  'SDL_AUDIODRIVER=dummy UC2_WINE_DISPLAY=$DISPLAY UC2_WINE_STOP_AFTER=60 tools/dev/m12-wine.sh run/m12/System
  uc2.exe -log -nullrenderer'` (`logs/m12/client-null-1.out`): `SDL2 video driver: windows`, `UC2ALAudio: 1.1
  ALSOFT 1.25.2 on "OpenAL Soft on No Output"` (our OpenAL32.dll, null backend from capped's ALSOFT_DRIVERS),
  `Bringing Level Menu.MyLevel up for play`, `Opened viewport (SDL window 1, 640x480)`, then the Ctrl+C →
  `SDL_QUIT received`, `appRequestExit(0)`, clean shutdown, exit 0. stdout of the GUI-subsystem exe reaches the
  terminal under wine.

### Step 4 (W3/W5) — GL through wine, keyboard and pad (2026-10-05)
- **GL under wine + Xvfb** (`logs/m12/client-gl-1.out`, menu, 60 s, `-glerrors -glshot=20,35`):
  `GL_VENDOR : Mesa`, `GL_RENDERER : llvmpipe (LLVM 21.1.8, 256 bits)`, `GL_VERSION : 4.3 (Compatibility
  Profile) Mesa 26.0.8-1ubuntu0.3`, `GL_SHADING_LANGUAGE_VERSION : 4.50` (wine's WGL on the host's Mesa through
  Xvfb's GLX; the native Linux build on the same Xvfb gets 4.5 compatibility — wine 10 caps the reported
  version at 4.3; UC2GLDrv needs 2.1-era compatibility GL, so this is irrelevant), 8 texture units, S3TC yes;
  59 fragment shaders and 405 vertex shaders compiled, 0 failed; `0 GL errors in 1767 frames`. Two engine
  screenshots of the main menu (`logs/m12/client-gl-1-{0,1}.png`), looked at: the menu as on Linux.
- **Input test channel bug (Windows CRT)**: the first keyboard scenario under wine got no probe answers. A 6-line
  test program (`fopen` "r", `fgetc` to EOF, `clearerr`, append from outside) proved it: wine's msvcrt keeps
  returning EOF on a file that grows until the next seek (glibc does not). `SDL2InputTest.cpp` now does
  `clearerr` + `fseek(File, 0, SEEK_CUR)` (portable; the same test program then reads all lines). Whether
  Microsoft's own msvcrt behaves like wine's here is not known; the seek is right for both.
- `tools/dev/m10-controls.sh` runs unchanged scenarios against uc2.exe under wine with
  `M10_TREE=m12 M10_OUT=<WorkUC2>/logs/m12/controls M10_LAUNCH="tools/dev/m12-wine.sh <WorkUC2>/run/m12/System
  uc2.exe" M10_STOPSIG=INT M10_MEM=3G M10_PROC=uc2.exe M10_SETTLE=12` (new variables, defaults unchanged).
  The keyboard scenario first failed only its match start: the Fire (Alt) 6 s after "level up for play" was
  lost; a diagnostic run (`WINEDEBUG=+key`) showed wine delivering `vkey=00a4` press/release, and the same Alt
  6 s later spawned the player and every further Alt fired. So it is timing (the level's first frames come
  later under wine), not input: `M10_SETTLE` (default 6) = 12 for wine.
- **`keyboard`: PASS (16 checks, 0 Critical)** — the whole menu path by real X key events (Down, Return,
  Escape), travel to DM-Deadbolt, spawn with Alt, W forward, Right turn, E/Q weapons, Alt fire, Escape pause.
- **`pad`: PASS (33 checks, 0 Critical)** — SDL's virtual controller under wine (hot-plug, menus, sticks, dead
  zone, trigger threshold, rumble request reaching the device, unplug/replug).
