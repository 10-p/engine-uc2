# Engine package port notebook (M2)

Baseline measurements are in `engine-baseline.md`. This file records what was changed to make
`Source/Engine` compile with GCC 15 (`-m32`) and emcc 5.0.7, and why.

## Gate evidence (2026-10-04)

- `cmake --preset native && cmake --build build-native --target Engine -j12` → `[100%] Built target Engine`,
  `build-native/Source/Engine/libEngine.a` (57.7 MB), 0 errors.
- `source ../env.sh && cmake --preset wasm && cmake --build build-wasm --target Engine` → `Built target Engine`,
  `build-wasm/Source/Engine/libEngine.a` (25.4 MB), 0 errors.
- `nm`: `GEnginePackage` defined once; no undefined `GPackage`, `Core` or `Engine` symbols.

Nothing has been linked or run yet. Runtime behaviour starts being checked at M3.

## Build file

`Source/Engine/CMakeLists.txt`: every `Src/*.cpp` except `UnSVehicle.cpp` (in no Visual Studio project; its
vehicle classes do not exist in UC2 script) and `palette.cpp` (leftover tool source). 105 sources, the same
set as `Engine.vcproj`.

**Package macro.** Under `__STATIC_LINK`, `IMPLEMENT_PACKAGE(pkg)` defines `G<pkg>Package`
(`Core/Inc/UnObjBas.h`), so each library is compiled with `GPackage=G<pkg>Package`. Core's CMakeLists had
`GPackage=Core`, which named a symbol nothing defines; corrected to `GCorePackage`. Every later package
follows the same rule. (The Xbox build defined one global `GPackage` = "Avaris" in its launcher instead, so
there `Localize()` defaulted to the Avaris package everywhere; per-library is what the PC DLL build did.)

## Changes to Epic's files (20 files, +117/−39)

| File | Change | Why |
|---|---|---|
| `Inc/Engine.h` | `CheckNotBool` → `(...) ((void)0)` on non-MSVC | `__noop` is an MSVC intrinsic (same pattern as Core's `check`) |
| `Inc/UnRenDev.h` | `BEGIN_D3D_EVENT(x)` → `{ UC2_D3D_EVENT_NOOP` + variadic no-op, both compiled-out branches | used as `BEGIN_D3D_EVENT(n)(colour,fmt,…)`; `__noop` swallowed the second list |
| `Inc/UnPrim.h` | namespace-scope declaration of `CompareHits` | in-class friend is visible only to ADL; `UnLevAct.cpp` takes its address (same fix as engine-ut2004) |
| `Inc/UnRenderResource.h`, `Src/UnRender.cpp` | `void` return type on `SetCoord`, `AddToList` | implicit int; neither returns a value |
| `Inc/APawn.h`, `Inc/UnPlayerStats.h`, `Inc/UnChan.h` | dropped `Class::` on in-class member declarations | extra qualification (clang error) |
| `Src/UnStats.cpp` | removed `##` before/after `.`, `(`, `[`, `=` in 9 macros | invalid token pastes; MSVC ignores them, the expansion is unchanged |
| `Src/UnInteraction.cpp` | literal function name where `__FUNC_NAME__` is not declared | `guard()` omits it on `__LINUX__` (`Core/Inc/UnFile.h`); same preprocessor test used |
| `Src/UnRenderUtil.cpp` | the two `F32vec4` branches under `#if __HAS_SSE__` | the classes come from MSVC's `<fvec.h>`; `__HAS_SSE__` is Epic's own switch (MSVC only). Other compilers always run the scalar loop that was the `else` |
| `Src/UnRenderStaticMesh.cpp`, `Src/UnRenderBatch.cpp` | `FloatToInt`: `lrintf` branch for everything that is not 32-bit x86; `FColor`→`FPlane` conversion named explicitly | x87 inline asm; `FPlane(FColor)` is ambiguous (see below) |
| `Src/UnParticleEmitter.cpp`, `Src/UnRender.cpp`, `Src/UnRenderVisibility.cpp` | `template<> … PoolInstance{};` on non-MSVC | explicit specialisation of a static member needs `template<>` and an initialiser to be a definition |
| `Src/UnParticleEmitter.cpp` | `static const INT MIN_ATTEN_DIFF` | implicit int (see suspected bugs) |
| `Src/UnCamMgr.cpp` | `Compare(UViewport*&, UViewport*&)` | first parameter was `const UViewport*&`, which cannot bind to a `UViewport*` element |
| `Src/UnTex.cpp` | `*GetFormatDesc()` in two `debugf` calls | an `FString` object was passed through `...` for `%s` |
| `Src/FContentManager.cpp` | `&FContentManager::Handle…` in the handler table | pointer-to-member syntax |
| `Src/xbstrip.cpp` | basic scalar typedefs instead of `<D3D8.h>` on non-MSVC; include name case; three loop variables declared before their first loop; `operator<` made `const` | standalone XDK sample; pre-standard for-scope; libc++ `std::sort` compares const references |

## Decisions someone else needs to know

- **`FPlane(FColor)`**: `FColor` converts to both `FPlane` and `FVector`, and `FPlane` is constructible from
  both, so the expression is ambiguous in ISO C++. The three sites now call `.operator FPlane()`, i.e. the
  conversion that keeps alpha (W = A/255). Assumption: this is what MSVC 7.1 chose. If lighting alpha looks
  wrong at M5, this is the place to look (`UnRenderStaticMesh.cpp:535`, `UnRenderBatch.cpp:1057,1060`).
- **Sort templates and file-static `Compare`**: `Sort<INT>` with a `static INT Compare(INT&,INT&)` declared
  after the template (e.g. `UnTerrain.cpp`) relies on `-fpermissive`, exactly as in engine-ut2004. GCC makes
  that a hard error once any *other* error exists in the same file, which inflates error counts while porting.
- **SSE**: no SSE path is compiled outside MSVC. A portable SSE/NEON version of the convex-volume checks is a
  possible later optimisation, not a correctness issue.
- **Transient compiler crash**: at `-j12`, emscripten's clang segfaulted once on `UnDemoPenLev.cpp` and
  `UnRenderBSP.cpp` while parsing `EngineClasses.h`; a re-run at `-j4` compiled both. If it recurs, re-run
  before investigating.

## Suspected real bugs in Epic's code (left as shipped unless noted)

- `UnParticleEmitter.cpp:262`: `static const MIN_ATTEN_DIFF = 0.2f;` has no type, so it is `int` 0 and the
  "skip if almost equal" test never skips. Kept as shipped (typed `INT`, value still 0); making it `FLOAT`
  would change behaviour.
- `UnTex.cpp:857,988`: `FString` passed by value to `%s` (fixed: harmless on MSVC by layout accident, undefined
  elsewhere).
- `D3DRenderInterface.cpp:3713-3777` (not built): PC occlusion-query stubs never write `dwPixels`.

## Remaining warnings worth a look later

- clang `-Wstring-plus-int` on `IMPLEMENT_CLASS` (`TEXT(#TClass)+1` skips the class prefix letter: intended).
- GCC "invalid use of incomplete type" in `UnObjBas.h`/`UnClass.h` (accepted under `-fpermissive`).

## The smaller native packages (same day)

`IpDrv`, `UWeb`, `UnrealGame`, `UCElements`, `UCGameplay`, `GUI`: one generated-pattern `CMakeLists.txt` each (all
`Src/*.cpp`, `GPackage=G<pkg>Package`, `ThisPackage=<pkg>`). Link dependencies follow the Visual Studio
projects: GUI → UCGameplay, UnrealGame, IpDrv; UCGameplay → UnrealGame; everything → Engine → Core.
Gate: `cmake --build build-native` and `cmake --build build-wasm` produce all eight `lib*.a` with 0 errors.

They were nearly clean; the whole list of changes:

| File | Change | Why |
|---|---|---|
| `IpDrv/Inc/UnSocket.h` | `WSAEHOSTUNREACH` → `EHOSTUNREACH` alias | used by `TcpNetDriverXbox.cpp` outside its `_XBOX` blocks |
| `IpDrv/Src/UnSocket.cpp` | `TRY_AGAIN`/`NO_RECOVERY` cases not on Emscripten | their values (2, 3) equal errno constants there (same guard as engine-ut2004) |
| `IpDrv/Src/MasterServerUplink.cpp` | dropped `Class::` on an in-class declaration | extra qualification |
| `UCElements/Src/UCElements.cpp` | `C->Color.operator FPlane()` | same `FColor`→`FPlane` ambiguity as in Engine |
| `GUI/Src/GUI.cpp` | `<wtypes.h>` only on Windows | Windows header |
| `GUI/Inc/UnUIXInteraction.h` | `ULONGLONG`, `HRESULT`, `BOOL`, `S_OK` for non-Windows | the PC (non-Xbox) branch of the Xbox Live UI declarations borrowed them from `<wtypes.h>` |

`TcpNetDriverXbox.cpp` is built (it is in `IpDrv.vcproj` for PC and its Xbox-only parts are `_XBOX`-gated), so
the native class the shipped `IpDrv.u` declares exists.
