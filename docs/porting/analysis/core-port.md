Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# M1 — Core port notebook (running)

Goal (PLAN.md row M1): `Source/Core` compiles with `g++ -m32` (GCC 15) and Emscripten 5.0.7 under CMake,
producing `libCore.a` with 0 errors. This file is appended after every step so a successor can continue.

Merge source: `/home/ion/WorkUnreal/engine-ut2004/Source/Core` (UT2004 build 3369, GCC/Emscripten port,
read-only). UC2 differs: ANSI TCHAR everywhere (no UNICODE, no `-fshort-wchar`), package v151 (never copy
serialisation from UT2004), UT2003-era base with "scion" (Xbox) additions.

## Commands

```sh
cd /home/ion/WorkUC2/engine-uc2
cmake --preset native && cmake --build build-native --target Core -j12
source /home/ion/WorkUC2/env.sh && cmake --preset wasm && cmake --build build-wasm --target Core -j12
# quick per-file syntax check (from Source/Core):
g++ -m32 -std=gnu++14 -fsyntax-only -fpermissive -w -fsigned-char -fno-strict-aliasing -D__STATIC_LINK=1 \
    -DPLATFORM_32BIT -DLINUX=1 -D__LINUX_X86__ -DNO_UNICODE_OS_SUPPORT=1 -D__FILE_NOCASE=1 -DZ_PREFIX \
    -DGPackage=Core -IInc -ISrc -I../../Thirdparty/zlib Src/UnObj.cpp
```

## Log

### Step 1 — CMakeLists.txt
- `Source/Core/CMakeLists.txt`: static `Core`, `GPackage=Core`, links `zlib` PUBLIC (the launchers compile
  `FFileManagerGeneric.h`, which uses zlib) and `Threads::Threads` on native. `-Og` on `UnCorSc.cpp` and
  `UnProp.cpp` under GCC, as in engine-ut2004 (FFrame::Step reference juggling miscompiles at -O2).
- Note: no Core `.cpp` uses zlib itself; only `Inc/FFileManagerGeneric.h` and `Inc/FFileManagerXbox.h`.

### Step 2 — shared headers compile (GCC 15 -m32 and em++ 5.0.7)
Measured after this step with the syntax-check commands above (`-w`, so warnings hidden): all 18 shared
TUs (everything except the platform layer) give 0 errors on both compilers. The "~100 errors per file"
counted before were almost all header cascades.

Per file (all minimal; MSVC/Xbox branches untouched):
- `Inc/UnGnuG.h` — **rebased** on engine-ut2004's UnGnuG.h, UC2 contract re-added (see "Decisions").
- `Inc/UnFile.h` (Latin-1, edited by bytes) — `check`/`checkSlow` were `__noop` (MSVC intrinsic): GCC
  branch `#define check(...) ((void)0)`. `NEW T[n]` needs `operator new[](size_t,const ANSICHAR*,INT)`
  on GCC/clang (MSVC 7.1 falls back to the scalar placement form). Sized `operator delete(void*,size_t)`
  and `delete[](void*,size_t)` added for non-MSVC (C++14 sized deallocation would otherwise free GMalloc
  memory with the runtime's delete). `UT_REPLACEMENT_ALLOC` (always_inline on the replaceable global
  new/delete) ported from the engine-ut2004 delta.
- `Inc/Core.h` — `#if __UNIX__` -> `#if __UNIX__ || defined(__MINGW32__)` for UnUnix.h (ut2004 delta).
- `Inc/UnUnix.h` — `appGetLocalIP` returns `DWORD` (ut2004; 32 bits on LP64).
- `Inc/UnMath.h` — `FVector::SafeNormal` extra qualification removed; GCC x87 asm
  (`ASMTransformPoint/Vector`) guarded `ASMLINUX && !UT_NO_X86_INTRIN`; `TransformPointBy/VectorBy` take
  the C path on GCC (`#elif 0 //ASMLINUX`), exactly as UT2004's shipped Linux code.
- `Inc/UnMem.h` — default arguments moved from the `friend operator new` declarations to the
  definitions (C++ forbids defaults on a non-defining friend; same as UT2004).
- `Inc/UnTemplate.h` — `FArray::Realloc` and `TScopePointer::operator=`/`operator&` had implicit int
  (now `void`/`void`/`T**`); `TMultiMap::MultiFind`/`TMap::Num` need `this->` (two-phase lookup);
  `TPooledList` nested templates renamed their parameters (`PTYPE`, `BTYPE`: C++ forbids shadowing
  `TYPE`) and use `typename ...::template TBlockAllocationList<>`.
- `Inc/UnName.h` — `FNameEntry::GetName(FString&)` implicit int -> `void`.
- `Inc/UnObjBas.h` — `ConstructObject`: `T::GetTransientPackage()` (ut2004 delta hunk; UPackage is
  incomplete there).
- `Inc/UnType.h` — forward declaration of `FindFieldNamed<T>` ahead of `FindField<T>`.
- `Src/UnAnsi.cpp` — `(TCHAR*)` on `strstr/strchr/strrchr` results (C++ overloads return const, as in
  UT2004); `_copysign` -> `copysign`, `_atoi64` -> `atoll` on non-MSVC.
- `Src/UnName.cpp` — namespace-scope prototype for `AllocateNameEntry` (only a friend declaration
  existed; UT2004 does the same).
- `Src/UnGUID.cpp` — includes `Core.h` instead of bare `UnUnix.h` (needs DWORD; also fixes the
  `strupr` linkage clash with Emscripten's `<compat/string.h>`), `unsigned32` is `unsigned int` (ut2004).
- `Src/UnObj.cpp` — `const BUFF_SIZE` implicit int -> `const INT`; `debugf("%s", FString)` -> `*EventName`
  (real bug: non-POD through varargs; worked by accident on MSVC); `FObjList::PoolInstance` explicit
  specialisation needs `template<>` and an initialiser to be a definition.

## Decisions (platform contract for the GCC/Emscripten build) — Engine porters read this
- `UnGnuG.h` now provides, mirroring UC2's PC `UnVcWin32.h`:
  `TCHAR_TO_ANSI(s)`/`ANSI_TO_TCHAR(s)`/`TCHAR_TO_OEM(s)` = `(ANSICHAR*)(s)` (a cast, drops const, as on
  PC), `TCHAR_CALL_OS(W,A)` = `(A)`; defining UNICODE without NO_UNICODE_OS_SUPPORT is an `#error`.
  `GET_VARARGS`, `GET_VARARGS_ANSI`, `GET_VARARGS_RESULT` are va_list macros (MSVC uses appGetVarArgs).
  `appTrunc(x)` = `(INT)(x)`; `CORE_API FLOAT appSqrt(FLOAT)` (out of line, UnUnix.cpp) next to UnAnsi's
  `DOUBLE appSqrt(DOUBLE)`; `inline FLOAT appInvSqrt(FLOAT)` = `1/appSqrt`; `LARGEINT` union;
  `appCycles()` (rdtsc, or microseconds via appSecondsSlow when `UT_NO_X86_INTRIN`) and
  `SQWORD appCycles64()` both inline (`DEFINED_appCycles`, `DEFINED_appCycles64`); `appSeconds()` is out
  of line (UnUnix.cpp); `FMovieThread`, `FMoviePlayer`, `appGetLanguageID()`, `class AvarisPlayerItem;`.
- `HANDLE`/`HINSTANCE` are `void*` (UC2's PC definition) instead of the old Unix `DWORD`.
- `GCC_PACK(n)` = `__attribute__((aligned(n)))` (UT2004's shipped Linux choice; the old UC2 Unix header
  had `packed,aligned(n)`). Struct layout vs shipped .u is M3's layout guard — revisit there.
- `UT_NO_X86_INTRIN` is defined on every non-x86 target (wasm); `ASMLINUX` stays 1 on wasm.
- Not taken from UT2004's UnGnuG.h: its math intrinsics block (`DEFINED_appMathIntrinsics`, appSqrt
  macros, appRound/appFloor macros), `appResetTimer`/`GBaseCycles`, `GIsMMX`/`GIsAltivec`, the
  UNICODE_BY_HAND wide-char layer, `SHORT`/`UINT` typedefs, `BUGGYINLINE`, `UREG_*`. UC2's math is
  declared in UnFile.h and defined in UnAnsi.cpp; UC2 has only `GIsSSE`.

## Suspected real bugs (left alone unless they block compilation)
- `Inc/UnTemplate.h` `TMultiMap::operator=` and `TMultiMap::Add` name `TMapBase<TK,TI>` (MinHashSize
  defaults to 8) instead of `TMapBase<TK,TI,MinHashSize>`: wrong base for any TMultiMap with a non-8 hash
  size (scion jg added the parameter to TMap only). Harmless while every TMultiMap uses 8.
- `Inc/UnTemplate.h` `TScopePointer` frees with `delete` what `UObject::CacheDrivers` allocated with
  `NEW TCHAR[]` (array/scalar mismatch; harmless while both go to appFree).
- `Src/UnObj.cpp` SUPPRESS/UNSUPPRESS exec passed an `FString` through `debugf`'s varargs (fixed: needed
  for clang, and it is the real fix).

### Step 3 — platform layer: Src/UnUnix.cpp rebased, Src/UnThreadingPosix.cpp + Inc/UnThreadingPosix.h new
State after this step: `cmake --build build-native --target Core` and `cmake --build build-wasm --target Core`
both give `[100%] Built target Core`, 0 errors. Native warnings: only `-Wwrite-strings` (1095), 3
`-Wendif-labels` (UnObj.cpp), 1 `-Wunused-result` (getcwd, UT2004 code); **no -fpermissive diagnostics
left in Core** (the remaining two-phase-lookup spots in UnTemplate.h `TTransArray`, `TLazyArray::Unload`,
`TMultiMap::AddUnique/RemovePair/FindPair` and UnObjBas.h `GetDefault<T>` got `this->`/`template`).

`Src/UnUnix.cpp` = engine-ut2004 HEAD's UnUnix.cpp (UTF-8 punctuation in comments transliterated to ASCII:
the tree is Latin-1) with these UC2 changes (`diff` vs that base: ~740 changed lines, almost all additions):
- no `vorbis/codec.h` / `vorbisfile.h` (nothing in UC2's Core uses Ogg; UC2's PC FFileStream Ogg cases are
  `#if 0`), `<math.h>`, `<cpuid.h>`, `FOutputDeviceFile.h` (non-final, for DUMPNAMES) added.
- `USystem` block replaced by UC2's (UnCorObj.h has no CacheRecordPath/MusicPath/SpeechPath): UC2's
  constructor/StaticConstructor and Exec commands (CONFIGHASH, EXIT/QUIT with !GIsEditor, DEBUG
  CRASH/GPF/RECURSE/EATMEM, DIR, NAMEHASH, VIEWNAMES, DUMPNAMES) from UnVcWin32.cpp; MEMSTAT "not
  available" and RELAUNCH = UT2004's Unix GUnixSpawnOnExit relaunch (binary name still `./ucc-bin`).
  DIR passes `**Buffer[x]` (UnVcWin32.cpp passes an FString object through varargs).
- UT2004's `FCriticalSection`/`FScopeCriticalSection` implementation removed (UC2's are the abstract
  classes in UnThreadingBase.h; see UnThreadingPosix).
- `DoCPUID` uses GCC `<cpuid.h>` `__cpuid` (PIC-safe; UT2004's version needed `UREG_*` macros).
- `GIsMMX` / `GRunningCPU` assignments dropped (UC2 has neither; PC sets only GIsSSE).
- `EdLoadErrorf` gets UC2's PC body (records into GEdLoadErrors); UT2004's Unix one was empty.
- Added (UnVcWin32.cpp PC branches on POSIX): `appGetSystemErrorMessage` (strerror), `appDebugMessagef`
  (log + stderr instead of a MessageBox), `appGetLastError` (log), `appSystemTime64` (local-time FILETIME,
  100 ns since 1601), `appSystemCmd` (system()), `appContiguousMalloc/Free` (GMalloc, `check(dwAlign==4)`
  as on PC), `appMemCPUFastAccess` (no-op), `FLOAT appSqrt(FLOAT)`, `appLogva` + `FTraceLog::operator()`
  (DEBUG_LOGGING), `appGetLanguageID` (LC_INT), `ExternalContigMalloc/Free`, `FFileStream::Read/Create/
  Destroy` on stdio + the file-streaming pthread started at the end of `appPlatformInit` (PC does the
  same). If the thread cannot start, `FFileStream::Destroyed=2` so `appExit` does not wait forever.
- Kept from UT2004 although UC2 headers do not declare them (harmless, eases later 3-way merges):
  `ClipboardCopy/ClipboardPaste` (non-app), `appGetTime`, `appGetSystemDllHandle` (declared in UnUnix.h
  in a later step), the MinGW/Android/MacOSX branches.

`Inc/UnThreadingPosix.h`, `Src/UnThreadingPosix.cpp` (new, ~700 lines): FCriticalSectionPosix (recursive
mutex), FEventPosix (manual/auto reset + PulseEvent via a pulse counter), FMutexPosix (recursive, names
ignored), FSemaphorePosix (count 0..max, Win32 contract), FSynchronizeFactoryPosix, FQueuedThreadPosix,
FQueuedThreadPoolPosix, FRunnableThreadPosix, FThreadFactoryPosix, plus `GSynchronizeFactory` and
`GThreadFactory` (defined in UnThreadingWindows.cpp on Win32). Differences from Win32, by necessity:
no TerminateThread (a thread that does not stop in time is detached, its handle leaked), no
SuspendThread (logs a warning), priorities/affinity recorded only. A dying queued thread does not call
`ReturnToPool` (the Win32 version does, while `FQueuedThreadPoolBase::Destroy` holds the thread-queue
lock and waits INFINITE for it — looks like a deadlock; see suspected bugs). Compiles under em++ -pthread.

Symbol check (scratch `syms.py` = nm of libCore.a, undefined-but-not-defined): only libc/libstdc++/pthread
symbols remain. Every function UC2's UnVcWin32.cpp defines has a definition, except Windows-only helpers
(`win*` Unicode helpers, `CtrlCHandler`, `GetDllHandleHelper`) and `appDecodeDmHRESULT` (Xbox-only on
PC too).

### Step 4 — file manager, word wrap, launcher-side headers (commit e7fc07d)
- `Inc/FFileManagerUnix.h` (new) = engine-ut2004 HEAD's (ASCII), minus its `GFileManagerBytesRead/
  LoadingTime` statistics (UC2 has none). Every method of UC2's old FFileManagerLinux.h exists in it.
- `Inc/FFileManagerLinux.h` — the stale UT2003 stdio manager is replaced by a shim
  (`#include "FFileManagerUnix.h"`, `typedef FFileManagerUnix FFileManagerLinux;`) so UCC.cpp and
  SDLLaunch.cpp keep their include/class name. Original: git history (7633696) and epic-sources.
- `Inc/FFileManagerGeneric.h` — zlib by its vendored path `../../../Thirdparty/zlib/zlib.h` on non-MSVC
  (as engine-ut2004); `CompareFileTimes` uses `stat` instead of MSVC `_stat` on non-MSVC.
- `Inc/FOutputDeviceFile.h` — `#if WIN32` -> `#if WIN32 && defined(_MSC_VER)` (ut2004 delta; MinGW).
- `Inc/UnTemplate.h` — `FString::Mid`: `(DWORD)Start + (DWORD)Count` (ut2004 delta: signed overflow UB
  with the default Count=MAXINT, which clang -O2 exploited). Same bits as MSVC.
- `Inc/UnUnix.h` — declares `appGetSystemDllHandle(const ANSICHAR*)` (ut2004 put it in UnFile.h; here it
  stays in the platform header; ANSICHAR because TCHAR is not typedef'd yet at that point).
- `Inc/wordwrap.h`, `Src/wordwrap.cpp` — built portably: on non-MSVC, Core.h types instead of
  <windows.h>, `WCHAR` = `UNICHAR` (16-bit, as UCanvas passes appToUnicode() output), the Win32
  code-page parts (GetVersionEx, GetCPInfoEx, WideCharToMultiByte, FindNextLineA, TokenizeA) compiled out
  through a file-local `WW_WIN32_API` (as the Xbox build has them), 16-bit `wcslen/wcsncpy` helpers.
  Exports WordWrap_SetOption/SetCallback/FindNextLineW/TokenizeW/ClearTokensW/InitTokensW.
- Check used: a scratch TU including CorePrivate.h, FFileManagerLinux.h, FMallocAnsi.h, FConfigCacheIni.h,
  FOutputDeviceFile/Stdout/Null/AnsiError.h, FFeedbackContextAnsi.h, UnThreadingPosix.h and instantiating
  `FFileManagerLinux`, `FMallocAnsi`, both Posix factories: 0 errors with g++ -m32 and em++.

### Step 5 — gate (2026-10-04)
```
cd /home/ion/WorkUC2/engine-uc2 && cmake --preset native && cmake --build build-native --target Core -j12
  -> [ 38%] Built target zlib / [100%] Built target Core          (0 errors)
source /home/ion/WorkUC2/env.sh && cmake --preset wasm && cmake --build build-wasm --target Core -j12
  -> [ 38%] Built target zlib / [100%] Built target Core          (0 errors)
```
Also from scratch in fresh binary dirs (`cmake --preset native -B <scratch>`): same, 0 errors.
`nm -C --defined-only build-native/Source/Core/libCore.a`, strong (T/D/B/R) definitions: exactly 1 each of
appSqrt(float), appSqrt(double), appSeconds, appSecondsSlow, appGetLanguageID, appDebugMessagef,
appSystemTime64, appContiguousMalloc, appContiguousFree, appMemCPUFastAccess, appSystemCmd,
appGetSystemErrorMessage, appGetLastError, ExternalContigMalloc/Free, FFileStream::Read/Create/Destroy(int),
both FTraceLog::operator(), EdLoadErrorf, appArgv0, appGetDllHandle, appGetSystemDllHandle,
appPlatformInit, USystem::Exec, GSynchronizeFactory, GThreadFactory, GIsSSE, GLoadedCore, GCorePackage.
appCycles/appCycles64/appInvSqrt are inline (no strong symbol), appTrunc is a macro, LARGEINT/FMovieThread/
FMoviePlayer are types. 37 `::PrivateStaticClass` registrants, none duplicated; no strong symbol defined
twice (other than GCC's COMDAT `__x86.get_pc_thunk.*`). Same check on the wasm archive with llvm-nm: only
libc/libc++ symbols undefined, no duplicates.

## Excluded files (Core.vcproj PC Release list vs this build)

| File | In PC Release? | What it provides | Decision |
|---|---|---|---|
| `Src/UnVcWin32.cpp` | yes (`#if _MSC_VER`) | MSVC/Win32+Xbox platform layer | left out; its PC contract is reimplemented in `UnUnix.cpp` (list in Step 3) |
| `Src/UnThreadingWindows.cpp` | yes | Win32 threading classes, `GSynchronizeFactory`, `GThreadFactory` | left out; `UnThreadingPosix.cpp` is the portable equivalent |
| `Src/UnUnix.cpp` | yes (compiles to nothing on MSVC) | GCC platform layer | **built**, rebased on engine-ut2004 |
| `Src/wordwrap.cpp` | yes | Unicode line breaking for `UCanvas::WrapStringToArray` (Engine) | **built**, portable branch |
| `Src/FNameDebug.cpp` (+ `FNameDebugCOM_i.c`) | yes | COM client of an external FName debugging tool | left out: only referenced from `UnName.cpp` under `!_XBOX && _DEBUG`; we never define `_DEBUG` |
| `Src/FContiguousMalloc.cpp` | no | Xbox best-fit allocator over a physical heap, installed by XboxLaunch as `GContigMalloc` | left out: only used through the `USE_GCONTIG` macros, which are off (`#if 0//LIN_LOAD`); PC and portable `appContiguousMalloc` use GMalloc |
| `Src/FContiguousMallocDebug.cpp` | no | debug wrapper of the above | left out (same reason) |
| `Src/UnPSX2.cpp` | yes (all `#ifdef __PSX2_EE__`) | PS2 platform layer | left out (dead console) |
| `Src/UnGCN.cpp` | no | GameCube platform layer | left out (dead console) |
| `Src/makefile` | — | UT2003-era makefile | reference only |
Headers that stay Windows-only: `FMallocWindows.h`, `FMallocDebug.h`, `FMallocDL.h` (use FCriticalSectionWin),
`FFileManagerWindows.h`, `FFileManagerXbox.h`, `FFeedbackContextWindows.h`, `FOutputDeviceWindowsError.h`,
`UnThreadingWindows.h`, `UnVcWin32.h`. Launchers on Linux/web use FMallocAnsi + FFileManagerLinux(Unix).

## engine-ut2004 delta checklist (`git diff -w --ignore-cr-at-eol cfc7e09 HEAD -- Source/Core`, 11 files)

| File / hunk | Status |
|---|---|
| Core.h: UnUnix.h also for `__MINGW32__` | ported |
| Core.h: `UT_MSVC_NO_MMX` | n/a — UC2's Core.h uses no MMX/SSE intrinsics under GCC (`!defined __GNUC__`) |
| Core.h: MinGW `TCHARU` typedef | already right — UC2 typedefs TCHAR/TCHARU unconditionally after `#undef TCHAR` |
| Core.h: `FFileManager::GetUserDir()` virtual | deferred (M4/M4w): a vtable change for one log line; FFileManagerUnix has it as a plain member |
| FFileManagerUnix.h: UT_MKDIR, O_BINARY, `if(Error)` guards, localtime_s, -usehomedir opt-in, GetUserDir, USERPROFILE | ported (whole file taken) |
| FOutputDeviceFile.h: base-dir prefix only for MSVC | ported |
| UnFile.h: `appGetSystemDllHandle` | ported (declared in UnUnix.h) |
| UnFile.h + UnMisc.cpp: `appIsGlobalOptionToken`, `appApplyIniOverrides` (`-ini:` overrides) | deferred to M4w (launcher contract) |
| UnFile.h: `UT_REPLACEMENT_ALLOC` | ported (+ UC2 needed the sized deletes too) |
| UnGnuG.h: MinGW block, `_WIN32` minwindef/basetsd, VOID/HANDLE, STDCALL, DWORD/SIZE_T/BITFIELD | ported (HANDLE is `void*` for UC2) |
| UnGnuG.h: LLP64-safe PTRINT | ported |
| UnGnuG.h: `appCycles()` microseconds on UT_NO_X86_INTRIN | ported (and `appCycles64`) |
| UnObjBas.h: `GObjPreloading` / `IsLoadInProgress` + UExporter.cpp FindExporter guard + UnObj.cpp EndLoad | deferred (M3/M4): a load-time object-walk crash fix seen on Windows; verify UC2's UClass::Serialize path first |
| UnObjBas.h: `ConstructObject` uses `T::GetTransientPackage()` | ported |
| UnTemplate.h: `FString::Mid` unsigned End | ported |
| UnMisc.cpp: "User directory" log line | deferred with GetUserDir |
| UnMisc.cpp: `appUnwindf` no longer latches GIsCriticalError | deferred to M12: on `__UNIX__` (Linux and web) guard/unguard are plain braces, so appUnwindf is only reached by the try/catch guards of MSVC/MinGW builds |
| UnObj.cpp: GetPackageLinker `FreshLinker` (emscripten/android fail-soft) | deferred to M4w — UC2 has no fail-soft branch there yet (part of the Asyncify fail-soft loader patches) |
| UnUnix.cpp: all hunks (MinGW includes, execinfo, MAXPATHLEN, MAIN_THREAD_EM_ASM, Win32 dll sentinel, appGetSystemDllHandle, FreeLibrary/GetProcAddress, _spawnlp/_spawnv, signal guards, _fullpath, GetUserNameA, GetVersionExA, LARGE_INTEGER, Win32 crash reporter, GSecondsPerCycle pin, UT_NO_FAULT_HANDLER) | ported (file taken as base); the RELAUNCH `_spawnl` hunk sat in UT2004's dead `#else` of `#if 1` and went away with UC2's Exec |
Also deferred to M4w (not in the Core delta but in PLAN.md's reuse list): Asyncify fail-soft loader
patches, `FLUSHCONFIG`, and the web question of the file-streaming thread (pthread pool size).

## Every Epic file changed, one line each (vs import commit 7633696)
- `Inc/Core.h` — UnUnix.h for MinGW too.
- `Inc/FFileManagerGeneric.h` — vendored zlib path; POSIX `stat` in CompareFileTimes.
- `Inc/FFileManagerLinux.h` — now a shim to FFileManagerUnix.h (stale UT2003 body replaced).
- `Inc/FOutputDeviceFile.h` — base-dir prefix only for MSVC.
- `Inc/UnFile.h` — `__noop` checks; `new[]` placement form; sized deletes; UT_REPLACEMENT_ALLOC.
- `Inc/UnGnuG.h` — rebased on engine-ut2004 + UC2 PC contract.
- `Inc/UnMath.h` — SafeNormal qualification; x87 asm guarded; C transform path on GCC.
- `Inc/UnMem.h` — friend operator new default args moved to the definitions.
- `Inc/UnName.h` — implicit int.
- `Inc/UnObjBas.h` — `T::GetTransientPackage`; `->template GetDefaultObject<T>`.
- `Inc/UnTemplate.h` — implicit ints, `this->`/`typename`/`template`, TPooledList parameter shadowing, Mid UB.
- `Inc/UnType.h` — FindFieldNamed forward declaration.
- `Inc/UnUnix.h` — DWORD appGetLocalIP; appGetSystemDllHandle.
- `Inc/wordwrap.h` — portable Win32 type subset.
- `Src/UnAnsi.cpp` — const casts of strstr/strchr/strrchr; copysign; atoll.
- `Src/UnGUID.cpp` — include Core.h; 32-bit unsigned32.
- `Src/UnName.cpp` — AllocateNameEntry prototype.
- `Src/UnObj.cpp` — `const INT`; FString varargs; explicit specialisation syntax.
- `Src/UnUnix.cpp` — rebased on engine-ut2004 + UC2 contract.
- `Src/wordwrap.cpp` — portable branch.
New: `CMakeLists.txt`, `Inc/FFileManagerUnix.h`, `Inc/UnThreadingPosix.h`, `Src/UnThreadingPosix.cpp`.

## For whoever ports Engine / the launchers next (M2)
- Launchers must set `GSynchronizeFactory`/`GThreadFactory` to `FSynchronizeFactoryPosix`/
  `FThreadFactoryPosix` (UnThreadingPosix.h) before `appInit` — appInit creates critical sections, and
  `FFileStream::Init` needs one. UCC.cpp currently instantiates the `...Win` classes unconditionally.
- Static link: `appGetDllHandle` answers "package linked in?" with `dlopen(NULL)` + `dlsym("GLoaded<Pkg>")`,
  so the native launcher needs `-rdynamic` (export-dynamic) and whole-archive linking of the package libs
  (as engine-ut2004); Emscripten returns the sentinel.
- `TCHAR_TO_ANSI`/`ANSI_TO_TCHAR` are casts that drop const (PC semantics); `TCHAR_CALL_OS(W,A)` = A.
- `HANDLE`/`HINSTANCE` are `void*`. `PTRINT` exists (pointer-sized unsigned). `UT_NO_X86_INTRIN` marks wasm.
- `appContiguousMalloc` keeps the PC `check(dwAlign==4)`; a GL driver that wants other alignments must
  revisit it (it is GMalloc memory).
- `wordwrap.h` defines `WCHAR`=`UNICHAR`, `BOOL`, `UINT`, `LPCWSTR`... on non-MSVC; UnCanvas's width
  callbacks must have type `FLOAT(*)(WCHAR,BOOL)`. On MinGW this would clash with <windows.h> (M12).
- Engine/Src/UnMaterial.cpp:1265 stores a `FCriticalSection*` in an `INT` (fine at 32 bits; M13).
- Layout: Core.vcproj builds with `StructMemberAlignment=3` (/Zp4). i386 GCC aligns double/QWORD members
  to 4 like that; **wasm32 aligns them to 8** — a candidate source of native-vs-script layout mismatches
  for M3's layout guard (`GCC_PACK` is `aligned(n)` only, it cannot lower alignment).
- `UObject::IsValid()` / `GetFullName()` test `this` against NULL (UnObj.cpp:229, :378); GCC and clang may
  delete such checks. If null-`this` calls show up at runtime, consider `-fno-delete-null-pointer-checks`.

## Suspected real bugs, continued (left as Epic wrote them)
- `Src/UnMisc.cpp:1255` reads `GSys->LocPaths`, but no platform's `USystem::StaticConstructor` registers
  `LocPaths` as a config property, so it is always empty.
- UnVcWin32.cpp `USystem::Exec DIR`: fixed 512-entry `Buffer` with no bound (overflow beyond 512 files);
  kept in UnUnix.cpp. (Its FString-through-varargs was fixed: `**Buffer[x]`.)
- `appSystemCmd` tests `Code<=32` (ShellExecute's convention) on a `system()` result, so success (0)
  reports "CmdFailed"; kept identical in UnUnix.cpp.
- `UnThreadingBase.h`: `FRunnable` and `FRunnableThread` have no virtual destructor, yet
  `FThreadFactoryWin::Destroy` and the `bShouldDeleteRunnable` path delete through the base. The Posix
  factory deletes through `FRunnableThreadPosix*`; the FRunnable delete is unchanged.
- `FQueuedThreadWin::Run` calls `ReturnToPool` after being told to die, while
  `FQueuedThreadPoolBase::Destroy` holds `SynchThreadQueue` and waits INFINITE in `Kill` — deadlock risk.
  The Posix version skips ReturnToPool when dying.
- `FFileStream::Destroyed` is a plain `INT` polled across threads (`FFileStream::Destroy` spins on it).
- `UObject::IsValid` / `GetFullName` null-`this` tests (see above).

## Open questions
- Should the web build start the file-streaming thread at all (one pthread from the pool, idle-polling
  every 1 ms)? Nothing in UC2 creates a stream outside Core yet; decide at M4w.
- `GetUserDir`/home-dir policy for UC2 (engine-ut2004 defaults to no home dir, `-usehomedir` opts in;
  FFileManagerUnix.h already behaves that way).
