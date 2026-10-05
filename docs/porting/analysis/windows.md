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
3. **Calling conventions, DLL_EXPORT/DLL_IMPORT under static link.** UnGnuG.h: `DLL_IMPORT` empty,
   `DLL_EXPORT` = `extern "C"`, `DLL_EXPORT_CLASS` empty, so `CORE_API`/`ENGINE_API`/... carry no
   `__declspec(dllimport/dllexport)` (nothing is a DLL; `-static`, no export table, decision in Step 1). `VARARGS`,
   `CDECL` empty (GCC's default is cdecl), `STDCALL` = `__stdcall` on `_WIN32` (only Win32 callbacks use it:
   IpDrv's `ResolveThreadEntry` for `CreateThread`, the console handler as `WINAPI`). Member functions use GCC's
   i686-mingw `thiscall` default; everything engine-side is one compiler, so no cross-compiler ABI exists. The
   two foreign interfaces: OpenAL (function-pointer types from OpenAL Soft's own headers, `AL_APIENTRY` =
   `__cdecl` on `_WIN32`, same headers the DLL was built with; a cdecl/stdcall mix-up would unbalance the stack
   on the first call, and the 39-check self-test runs thousands of calls) and SDL2 (static, `SDLCALL` = cdecl).
4. **Type widths** (measured with `sizeof` in both compilers): `long` 4 / 4 (Win32 is LLP64-like at 32 bits:
   same as Linux i386), pointers 4 / 4, `time_t` 4 / 4 (this mingw-w64's i686 default), `wchar_t` **2** on
   Windows / 4 on Linux, `long double` 12 / 12. `wchar_t` is in no engine structure (TCHAR is ANSI, UNICHAR is
   `unsigned short`); its only effect was wordwrap.h's WCHAR (Step 1). `time_t`/`struct stat` only feed local
   time/file-date arithmetic, never a file or the network. `DWORD` is `unsigned long` on Windows (from
   `<minwindef.h>`) vs `unsigned int` on Linux: same size, a different C++ type (it surfaced once, xbstrip.cpp).
   **The one real ABI difference is alignment: MinGW (MS ABI) aligns `double`/`QWORD`/`SQWORD` members to 8, Linux
   i386 to 4** (MSVC 7.1 with Epic's `/Zp4` also 4). That is the wasm32 situation M4w already met: the generated
   class headers are `#pragma pack(push,4)` and the two hand-written script mirrors with 8-byte members
   (UnTex.h, UnParticleSystem.h) got 4-byte packing then; `verifyclasses` passes. The whole-engine DWARF comparison
   (`tools/dev/dwarf-layouts.py` over every package object of `build-native` and `build-windows`;
   `<WorkUC2>/logs/m12/layouts-{linux-native,windows}.txt`) lists 39 types of different size, all C++-only:
   Core's `UState`/`UClass` (+8/+4: `QWORD ProbeMask`, `IgnoreMask`; the engine only logs `sizeof(UClass)`,
   "Class size: 279" vs 275), `ULevel`, `UViewport`, `USkeletalMeshInstance`, `UDemoRecDriver`, the net channels
   (bitfields, decision 2), render caches (`FActorLightCache`, `FCoronaRender`, `FDynamicActor`,
   `FProjectorRenderInfo`, `FSortedStaticMeshBatchIndexBuffer`), UC2ALAudio's and libvorbis' private state, the
   platform's own structs (`stat`, `tm`, `hostent`, `linger`, pthread types) and our Posix threading objects.
   None is mirrored by script or written to disk/network as a block (the only bulk `Ar.Serialize` of an array in
   Core/Engine is of DWORDs, UnCorSc.cpp). `-mno-align-double` / `-fpack-struct=4` were not used: they would put
   every Windows/SDL header structure with 8-byte members out of the platform ABI.
5. **Runtime DLL policy**: libgcc/libstdc++/winpthread linked statically (`-static` at the root, which also
   reaches OpenAL32.dll); SDL2 static; OpenAL Soft built from the fetched source as `OpenAL32.dll`, shipped
   beside the exe, LoadLibrary'd when the audio device starts (absent → `UC2ALAudio: no OpenAL library
   (OpenAL32.dll) beside the game or on the system: sound is off`). Both exes import only Windows system
   DLLs (Step 7 has the list).

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

### Step 5 — the same pinned view on Linux and under wine (2026-10-05)
- `tools/dev/m12/deadbolt-pinned.scen` (run by `m10-controls.sh --file`, URL `DM-Deadbolt?NumBots=0` through the
  new `M10_URL`, `-glerrors`): wait for the level, `set UCDeathmatch NumPlayers 0` (first PlayerStart), Alt to
  spawn, God, probe, `shot pinned`. Linux tree `run/m12-native` (this worktree's `build-native`), wine tree
  `run/m12`. Both probes: `loc=(1531.9,3111.5,-34.1) rot=(0,-40880,0)`, the same view.
- Comparison (`logs/m12/linux/pinned-pinned.png` vs `logs/m12/wine/pinned-pinned.png`, diff ×4 in
  `logs/m12/pinned-diff.png`): 640×480; 90.6 % of pixels bit-identical; PSNR 25.5 dB overall; **outside the
  rectangle x 60–395, y ≥ 265 (the player model and two pickups) 234,959 of 234,960 pixels identical and the
  one other differs by 1 level**. Inside it the differences are the player's idle animation and the pickups'
  rotation at a slightly different time since the spawn (the two runs do not spawn at the same engine time).
  Same Mesa llvmpipe underneath, so the world, the HUD and every shader pass come out the same through wine's
  WGL as through native GLX.
- Wrapper fix: a background job started by a non-interactive shell inherits SIGINT as ignored, and bash cannot
  trap it, so the M10 runner's stop signal never reached the game (it was killed after the grace period;
  `X connection ... broken` at the end of the log). `m12-wine.sh` now also takes USR1 as "press Ctrl+C";
  `M10_STOPSIG=USR1` for wine runs: `Exiting.`, `0 GL errors in 467 frames`, `Object subsystem successfully
  closed.`

### Step 6 (W3 audio, W5) — audio under wine; the smoke script (2026-10-05)
- `ucc.exe UC2ALAudio.UC2AudioSelfTest` under wine: `39 checks, 0 failed; Pan3D -1.10 dB, Pan2D -1.52 dB` —
  the same line, to the hundredth, as every Linux run in `<WorkUC2>/logs/*/audio-selftest.out` (OpenAL Soft's
  loopback mixer, same source, now as OpenAL32.dll).
- `ucc.exe UC2ALAudio.UC2AudioCheck`: `78 cue tables, 2438 cues, 0 failures`, 7141 waves at exact length, 21 music
  streams — identical to Linux.
- Capture: `uc2.exe -audiowave=<WorkUC2>/logs/m12/smoke-capture.wav` on the menu, 60 s: the device is `"OpenAL
  Soft on Wave File Writer"` (the `_putenv` path works: OpenAL32.dll read ALSOFT_CONF/ALSOFT_DRIVERS set by the
  game at run time); `audio-music-check.py`: NuBattle at -19.01 dB (expected -19.00), correlation 0.827, control
  Recoil 0.124. A Unix absolute path works as a file name for a Windows program under wine (the working
  directory is on Z:, so `/home/...` resolves to `Z:\home\...`).
- What wine provides for sound: nothing is used. The game's OpenAL32.dll is ours (wine 10 ships no openal32);
  its backends would be WASAPI/DirectSound/WinMM on top of wine's mmdevapi → winepulse/winealsa, but every run
  here forces `ALSOFT_DRIVERS=null` (capped) or the wave writer, so no wine audio driver is exercised.
- `tools/dev/m12-wine-smoke.sh` (W5): checks verify, server, null, gl, pinned, audiocheck, selftest, capture,
  keyboard, pad, leftovers; PASS/FAIL from log lines and measurements only. First runs: all PASS (pinned:
  static part 100.000 % within 2 levels, 90.8 % of the frame bit-identical, PSNR 29.5 dB).
- `m12-wine.sh` also disables winedbg (`winedbg.exe=d`): a crash ends the process after the engine's own report
  instead of opening a dialog on the Xvfb display.

### Step 7 (W4) — packaging, DLL policy (2026-10-05)
- `tools/dev/package-windows.sh [build-dir [dest]]` → `<WorkUC2>/run/m12-package`: `System/` = the asset tree's
  System (script packages, localisation) + our inis + `uc2.exe`, `ucc.exe`, `OpenAL32.dll`, `README-Windows.txt`,
  `DLLS.txt`; the content folders hard-linked from `assets/uc2` and the cooked `Sounds`/`Music` (a copy to
  another disk copies the data). `UC2_PACKAGE_CONTENT=0` → System only (to put over an existing tree),
  `UC2_PACKAGE_STRIP=1` → debug info stripped from the copies. It fails unless every imported DLL of the three
  binaries is a Windows system DLL or OpenAL32.dll.
- **DLL list** (`objdump -p`, `DLL Name`):
  - `uc2.exe`: ADVAPI32, GDI32, IMM32, KERNEL32, msvcrt, ole32, OLEAUT32, SETUPAPI, SHELL32, USER32, VERSION,
    WINMM, WS2_32 (SDL2: window, input, IME, device enumeration; IpDrv: WinSock; Core: registry/user name).
  - `ucc.exe`: ADVAPI32, KERNEL32, msvcrt, WS2_32.
  - `OpenAL32.dll` (shipped): AVRT, KERNEL32, msvcrt, ole32, SHELL32, USER32, WINMM.
  - Loaded at run time, seen with `WINEDEBUG=+loaddll` on the menu from the package: `OpenAL32.dll` (from the
    package folder), `OPENGL32.DLL`, `XInput1_4.dll`, `hid.dll`, plus Windows' own dependencies of the above.
  - Not needed: libgcc_s_dw2-1, libstdc++-6, libwinpthread-1 (`-static`), SDL2.dll (static), zlib (static).
  - Decision 5: runtime libraries static; SDL2 static (as engine-ut2004); OpenAL Soft as a DLL loaded at run time
    (as on Linux: no sound library still boots; built from the same fetched source, not downloaded).
- The package's `uc2.exe` under wine (`logs/m12/package-run.out`): menu up, GL, `"OpenAL Soft on No Output"` from
  the package's OpenAL32.dll, Ctrl+C → exit 0.
- `BUILD.md`: Windows section (commands, prerequisites `g++-mingw-w64-i686-posix`, wine, xvfb).

## Every change to shared code, with its guard (vs main 520b8a2)

| File | Change | Guard / effect elsewhere |
|---|---|---|
| `CMakeLists.txt` | MinGW link options `-static` added; OpenAL Soft fetch `if(NOT EMSCRIPTEN AND NOT WIN32)` → `if(NOT EMSCRIPTEN)` | `if(MINGW)`; the OpenAL condition is unchanged for Linux/web |
| `Source/Core/Inc/wordwrap.h` | new `!_XBOX && _WIN32` branch (WW_WCHAR...) before the portable one | MSVC takes the first branch; Linux/web the old portable one |
| `Source/Core/Src/UnUnix.cpp` | `appSystemTime64` Win32 body; `appWinCtrlHandler` + `SetConsoleCtrlHandler`; crash text names the exe | all inside `#ifdef _WIN32` |
| `Source/Engine/Src/UnModelLight.cpp` | `CompressLightmaps` block also needs `_MSC_VER` | Linux already excluded by `!__LINUX__` |
| `Source/Engine/Src/xbstrip.cpp` | `#elif defined(_WIN32)` branch with `<windows.h>` | MinGW only |
| `Source/IpDrv/Inc/UnIpDrv.h` | WinSock by `_WIN32` (was `_MSC_VER`); `ws2tcpip.h` + `#undef MM_MAX/CreateEvent/CreateSemaphore/CreateMutex`; resolver `CreateThread` by `_WIN32` | `_WIN32`, `!_MSC_VER`; identical for MSVC and Linux |
| `Source/IpDrv/Src/UnSocket.cpp` | setsockopt cast by `_WIN32` | same |
| `Source/UC2ALAudio/Src/UC2ALDevice.cpp` | Windows library names; the error message names the first one | `_WIN32`; Linux message text unchanged (`libopenal.so.1`) |
| `Source/UC2ALAudio/Src/UC2ALAudioSubsystem.cpp` | `_putenv` | `_WIN32` |
| `Source/SDL2Drv/Src/SDL2InputTest.cpp` | `fseek(File,0,SEEK_CUR)` after `clearerr` at EOF | **all platforms** (test channel only; Linux keyboard/pad scenarios re-run: PASS) |
| `Source/UCC/Src/UCC.cpp` | `WIN32` → `_MSC_VER` at 4 places; FFileManagerLinux also for `__MINGW32__`; unbuffered stdout for MinGW | MSVC/Linux unchanged |
| `Source/UCC/CMakeLists.txt` | `if(MINGW)` link block (ucc.exe) | Linux/web unchanged |
| `Source/SDLLaunch/CMakeLists.txt` | `if(MINGW)` link block (uc2.exe, WIN32_EXECUTABLE) around the existing one | Linux/web unchanged |
| `tools/dev/m10-controls.sh` | M10_* variables (defaults = old behaviour); pad right-stick hold 1 s → 0.5 s; stop grace 5 → 10 s | Linux keyboard + pad re-run: PASS |
| `tools/dev/mkrun-uc2.sh` | also copies ucc.exe, uc2.exe, OpenAL32.dll | no effect on other trees |

New files: `tools/dev/m12-wine.sh`, `tools/dev/m12-wine-smoke.sh`, `tools/dev/m12/deadbolt-pinned.scen`,
`tools/dev/package-windows.sh`, `tools/dev/dwarf-layouts.py`, this notebook.

### Hunks in files the other running agents edit (for the coordinator's hand merge)
- m7 (UC2GLDrv, UnRender.cpp, UnPixelShaderMaterial.cpp, UCCameraEffects.cpp): **none**. `Source/UC2GLDrv`
  compiled for Windows unchanged.
- m9 (UnVcWin32.h, UnGnuG.h, UnAudio.h, movie package, Theora, the launcher's startup): **UnVcWin32.h, UnGnuG.h,
  UnAudio.h, SDLLaunch.cpp untouched.** Possible overlap only in build files:
  1. `Source/SDLLaunch/CMakeLists.txt`, one hunk: the `target_link_libraries(...)` + `-export-dynamic` lines are
     wrapped in `if(MINGW) … else() … endif()`; a package m9 adds to `LINK_PACKAGES` reaches both branches; a
     library m9 adds to the link line (Theora/Ogg) must be added to the MinGW branch too.
  2. Root `CMakeLists.txt`, two hunks: the `if(MINGW) add_link_options(...)` line, and the OpenAL fetch condition
     (`if(NOT EMSCRIPTEN)` + 3 comment lines). If m9 adds `add_subdirectory(Thirdparty/Theora)` or a package to
     `UC2_PACKAGES`, no textual overlap expected.
  3. If the m9 movie package uses POSIX-only calls, the Windows build will need the same kind of `_WIN32`
     guards (it was not available to compile here).

## Real-Windows checklist (what wine cannot confirm)
wine is not Windows. One session on a real Windows 10/11 PC with `tools/dev/package-windows.sh` copied over:
1. Double-click `System\uc2.exe`: the menu comes up, no console window, no missing-DLL dialog (any
   "VCRUNTIME/libstdc++/libwinpthread missing" message would contradict the static-link finding).
2. GL: the real GPU driver's OpenGL (Avaris.log `GL_VENDOR/GL_RENDERER/GL_VERSION`), then DM-Deadbolt with
   `-glerrors`: "0 GL errors"; does it look like the Linux build (shaders on the player)? Under wine only Mesa
   llvmpipe was exercised. Try one NVIDIA/AMD/Intel if possible.
3. Sound through a real device (WASAPI): menu music and in-match sounds audible; Avaris.log `UC2ALAudio: ... on
   "OpenAL Soft on <device name>"`. Wine runs used only the null and wave-file backends.
4. A real XInput pad (Xbox controller): menu with D-pad/A/B, a match with sticks/triggers, unplug/replug, rumble.
   Wine runs used SDL's virtual controller only.
5. Keyboard and mouse in a match (mouse capture in a window, Alt+Enter fullscreen toggle and back, Alt+Tab
   out and in), fullscreen at the desktop resolution, high-DPI scaling (125/150 %): is the window the right
   size, is the mouse mapped right in the menus?
6. A path with spaces and with non-ASCII characters (e.g. `C:\Jeux\Unreal Championship 2\` and `C:\Users\Zoë\…`):
   does it start, load maps, write Avaris.log/Avaris.ini? (TCHAR is ANSI: non-ASCII paths depend on the code page.)
7. Antivirus/SmartScreen: an unsigned 81 MB exe from a USB stick may be blocked or scanned slowly; note what
   Windows Defender says.
8. `cmd.exe` in System: `ucc.exe verifyclasses` (must print "All native class layouts match"), `ucc.exe server
   DM-Deadbolt`, Ctrl+C stops it cleanly; Windows Firewall prompt on first listen (UDP 7777).
9. A client on the Linux build joining the Windows server (or the reverse) on the LAN: never tried here.
10. Exit from the menu (Quit): process gone, no "has stopped working" dialog; Avaris.log ends with "Log file closed".

### Step 8 — no-sound path, network handshake, a defect that is not Windows' (2026-10-05)
- `nosound` smoke check: OpenAL32.dll moved aside for one run → `UC2ALAudio: no OpenAL library (OpenAL32.dll)
  beside the game or on the system: sound is off`, menu up, exit 0 (wine 10 has no openal32 of its own, so
  nothing else was found).
- `netjoin` smoke check: the Linux `uc2.bin` (run/m12-native) connects to `ucc.exe server DM-Deadbolt PORT=7797`
  under wine. Server: `NotifyAcceptingConnection`, `Open MyLevel ... 127.0.0.1:<port>`, HELLO/CHALLENGE/NETSPEED/
  LOGIN, `Login request: ...`, the `USES` package list; client: `Welcomed by server: LEVEL=DM-Deadbolt
  GAME=UCGameplay.UCDeathmatch`. That is the server's WinSock UDP and its tick loop answering a real client, the
  "ticks" part of W2 shown by traffic. The client then stops at `Assertion failed: GObjBeginLoadCount`
  (UnObj.cpp:3361, `UObject::GetPackageLinker`). **Reproduced Linux client → Linux `ucc-bin server`**
  (`logs/m12/linux-netjoin-*.out`): a pre-existing network-join defect, not Windows', recorded in BLOCKERS.md and
  left alone (outside M12). The check passes on the handshake and reports the assertion count.
