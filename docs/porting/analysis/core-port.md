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
