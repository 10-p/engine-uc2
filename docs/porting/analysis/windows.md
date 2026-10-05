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
