# M12 — Windows (MinGW-w64 i686, verified under wine) — notebook (running)

Goal (PLAN.md row M12): `uc2.exe` and `ucc.exe` cross-built on Linux with MinGW-w64 13 (posix threads), CMake
only, statically linked; verified under wine 10.0 (headless, Xvfb). 64-bit Windows is M13 (the `windows64`
preset is not chased here). Branch `m12-windows`, worktree `<WorkUC2>/engine-uc2-m12`.

Every run goes through `<WorkUC2>/bin/capped` (build 6G `-j8`; wine runs 3G), with
`WINEPREFIX=<WorkUC2>/.cache/wine-m12 WINEDEBUG=-all WINEDLLOVERRIDES="winemenubuilder.exe=d;mscoree=d;mshtml=d"`
and `wineserver -k` after every run. Logs: `<WorkUC2>/logs/m12/`. Run trees: `<WorkUC2>/run/m12*`.

## Decisions (with evidence) — filled in as they are made

1. Platform layer for MinGW: _pending_
2. Struct layout (`-mms-bitfields` vs `-mno-ms-bitfields`): _pending (decided by `ucc.exe verifyclasses`)_
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
