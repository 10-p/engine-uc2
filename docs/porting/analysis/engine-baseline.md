Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# Engine package: compile baseline before any porting (2026-10-04)

Measured for planning M2, before Core's port landed. Method: every `Source/Engine/Src/*.cpp` (107 files) through
`g++ -m32 -std=gnu++14 -fsyntax-only -fpermissive -w` with the global defines from the root CMakeLists, against a
scratch copy of Core's headers carrying only two stop-gap fixes (`checkSlow` and `appInvSqrt`). The numbers
will move once Core's real platform header is in; the *classes* of error are what matter.

Total: 1,193 errors in 107 files; no file is clean, but most carry 5-8 errors that come from shared headers.

| Class | Count | Where it comes from |
|---|---|---|
| `appTrunc`, `appTan` undeclared | 225 each | UC2 additions to the Windows platform header, missing from the GCC one (Core, M1) |
| `__noop` | 161 | Engine headers use the MSVC intrinsic for compiled-out macros, as Core's `checkSlow` did |
| `strtof`, `sqrtf` undeclared | 105 each | artefacts of the scratch headers (missing `<math.h>`/`<stdlib.h>`); expected to vanish with M1 |
| `cannot bind packed field` | 212 | `Inc/UnForceFeedbackWaveform.h:25,52`, included everywhere: references to members of a packed struct |
| token pasting (`##` producing `.STATS_x`, `=TEXT`) | ~70 | stats macros in `UnStats.cpp` and friends: MSVC accepts invalid pastes |
| SSE intrinsics (`_mm_*`) undeclared | ~20 | `UnRenderUtil.cpp`: needs `<xmmintrin.h>` on x86 and a portable branch elsewhere |
| `ASVehicle`, `USVehicleWheel`, `ASVehicleFactory` | 50 | `UnSVehicle.cpp`: in no Visual Studio project and its classes do not exist; do not build it |
| two-phase lookup, `template<>` syntax, const binding, ambiguous overloads | ~35 | ordinary GCC strictness, one-line fixes |

Heaviest files: `UnStats` 297, `UnActor` 161, `UnRenderUtil` 82, `UnSVehicle` 81 (excluded), `UnTerrain` 19,
`UnParticleEmitter` 15, `UnController` 10. Missing includes: one `windows.h`, one `D3D8.h`.

Visual Studio project facts (PC `Release|Win32`, nothing excluded per file): Core 25 sources, Engine 105,
IpDrv 11, UWeb 2, UnrealGame 1, UCElements 1, UCGameplay 6, GUI 6, Editor 56, UCC 1, UnrealUtil 5, D3DDrv 8.
`Engine/Src` has 107 `.cpp`; the two not in the project are `UnSVehicle.cpp` and `palette.cpp`.
