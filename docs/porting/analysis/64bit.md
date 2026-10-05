# M13 — 64-bit (LP64 x86_64; LLP64 windows64; arm64 build-only) notebook

Branch `m13-64bit`, worktree `/home/ion/WorkUC2/engine-uc2-m13`. Appended after every step so a successor can
continue (CLAUDE.md). Commands run through `<WorkUC2>/bin/capped`. Logs: `<WorkUC2>/logs/m13/`.

## What the templates did (read 2026-10-05)

- **engine-ut2004** (`/home/ion/WorkUnreal/engine-ut2004`, read-only): Ryan Gordon's amd64 port of UE2 is in
  Epic's UT2004 code. `SERIAL_POINTER_INDEX` (pointer slots in bytecode hold a 4-byte index into
  `GSerializedPointers[]`, filled by a *linear search* `SerialPointerIndex`), `PTRINT`, `UPointerProperty`
  (script type `pointer`, ElementSize `sizeof(PTRINT)`), object properties aligned to 4 in `Link`, Object.uc's
  `ObjectInternal[7]` declared as `pointer` and recompiled into the shipped `.u`. One hand fix at link time:
  `USound.Data` (`byte[44]` placeholder) widened to 48 on LP64. `FMallocAnsi` pads every allocation by 32 bytes
  on 64-bit ("some uscript classes have four bytes extra at the end"). `UObject::Index`/`_LinkerIndex` are
  `PTRINT` (they double as registration stashes). `BITFIELD` stays `unsigned int` (4 bytes), never `long`.
  CMake: `UT_ALLOW_64BIT`, `UT_PTR_BYTES` gate every `-m32`.
- **UC2's Epic tree already has `SERIAL_POINTER_INDEX`** (UnObjBas.h, UnScript.h `ReadObject`, UnCorSc.cpp
  `execBoolVariable`, UnClass.cpp `XFERPTR`, UnObj.cpp registration stash for `Name`), keyed on `__x86_64__`
  only, with the same linear search and a 64K table. UC2's shipped `.u` files are 32-bit: every native pointer
  slot in script is an `int` (no `pointer` type), so UT2004's "recompile Object.uc" is not open to us before M14.
- **ufrontend 2.83/2.84** (UT99, Unreal 1 at LP64; `ufrontend/docs/migration/steps/2.83-64bit-ue1.md`,
  `2.84-64bit-unreal1.md`): the playbook. Bytecode keeps 4-byte slots holding an object index; mirrors are
  pack(4) and object properties align to 4; **a placeholder table in `UStruct::Link` gives each named native
  stand-in (script `int` hiding a C++ pointer/vtable) its C++ size** — same as before at 32-bit; the
  null-context zero-fill (`execContext` zeroes the compiler's 4 bytes of an 8-byte pointer) fixed in `execLet`;
  registration stashes moved to pointer-sized fields; hand-described reflection (`UInput` alias struct by
  arithmetic) crashed the client; `long` as 32 bits (`appGetLocalIP`); a literal `+ 4` next to a pointer in a
  renderer; a layout guard (fatal) + struct pass. Found by running: P1 scratch buffers read at pointer width
  after a 4-byte fill, P3 None-context struct zeroed to its 32-bit size, ASan bugs (not width).

## Plan

1. CMake: `native64` must really produce x86_64 everywhere (check `file`), cast diagnostics visible.
2. Compile census → every pointer↔int site; fix at the declaration.
3. Bytecode: `SERIAL_POINTER_INDEX` on every 64-bit target (not just `__x86_64__`), O(1) lookup.
4. Layout: placeholder table at link time for script `int`s that are C++ pointers/vtables (hand-written
   mirrors), pack(4) mirrors, object properties aligned 4; `verifyclasses` 64-bit mode.
5. VM hazards from 2.83 (null-context fill, scratch buffers).
6. Run: verify, census, client, GL, audio, movies, controls; cross-ABI saved data; windows64; arm64 preset.

## Log

### Step 1 — baseline: `native64` already configures and links (2026-10-05)

```
bin/capped 4G 900 -- cmake --preset native64          # "Target arch: x86_64; pointer width: 8 bytes"
bin/capped 6G 3600 -- cmake --build build-native64 -j6 -- -k   # 6m10s, 0 errors
file build-native64/Source/UCC/ucc-bin                 # ELF 64-bit LSB pie executable, x86-64
```
Log: `logs/m13/build64-first.log`. Everything links (uc2.bin 66 MB, ucc-bin 54 MB) because `-fpermissive`
turns every narrowing pointer cast into a warning. Nothing has run yet.

**Cast census** (`tools/dev/m13-cast-census.sh <log>`, new): distinct source lines with "cast from <ptr> to
<int> loses precision" (C++ makes it an error; `-fpermissive` a warning; GCC has no switch to make only
that one an error again) or `-Wint-to-pointer-cast`, fetched libraries skipped. Before any fix:
**29 ptr-to-int + 90 int-to-ptr = 119 lines** (Engine 81, IpDrv 21, Core 10, GUI 7);
`logs/m13/cast-census-before.txt`. Not seen by the compiler (needs reading/running): type puns like
`*(FResolveInfo**)&PrivateResolveInfo`, literal `4`s, `sizeof(INT)` used for a pointer, layouts.

### Step 2 — Core: platform macro, bytecode object slots, registration stashes (WIP, 2026-10-05)

First run of the unmodified 64-bit `ucc-bin verifyclasses` (`logs/m13/verify64-0.out`): `Assertion failed:
sizeof(_LinkerIndex)>=sizeof(GAutoRegister)` (UnObj.cpp) — the native-registration stash.

- `Core.h`: `PLATFORM_64BITS` (x86_64, `_WIN64`, aarch64, any `__LP64__`) / `PLATFORM_32BITS`, as engine-ut2004.
  (CMake's `-DPLATFORM_32BIT` is unused by any source.)
- `UnObjBas.h`: `SERIAL_POINTER_INDEX` on `PLATFORM_64BITS` (was `__x86_64__` only: arm64 would have widened
  bytecode slots). Ryan's side table (`GSerializedPointers[64K]`, linear search per reference at every load and
  every GC pass) is replaced by the object table: a slot holds `Obj->Index`, `INDEX_NONE` for None
  (`UObject::SerialPointerIndex` / `SerialIndexPointer`, O(1); same scheme as ufront 2.83's UT99 decision 1).
  Bytecode keeps 4-byte slots, so every jump/skip offset of the 32-bit `.u` and every ScriptPatch (applied
  through `SerializeExpr`, UnScriptPatcher.cpp) stays valid. Readers: `FFrame::ReadObject` (UnScript.h),
  `execBoolVariable` (UnCorSc.cpp), `XFERPTR` (UnClass.cpp `SerializeExpr`). All other bytecode object reads in
  Core/Engine already go through `ReadObject` (grep: no raw `*(UObject**)Stack.Code` outside those).
- `UnObj.cpp` native/static constructors: on 64-bit the stashed `const TCHAR*` name goes to `StateFrame` and
  the autoregister chain to `_Linker` (both pointer-sized, NULL until `Register()`, which clears them again);
  `ProcessRegistrants` walks `_Linker`. 32-bit code path unchanged (`#else` keeps Epic's lines).

Built: `cmake --build build-native --target Core` (32-bit) and `build-native64 --target Core`: 0 errors.

**Plan for the layout proof (B2)** — decided, not yet written:
1. `UStruct::Link` gets a placeholder table (ut99 `NativePlaceholderSize`, by string): a script `int` that
   stands in for C++ data (UObject `ObjectInternal[7]`, `Subsystem.ExecVtbl`, `Player.vfOut/vfExec`, the
   `*Ptr` one-int structs of Actor.uc, `WebResponse.ReplacementMap[5]`, `WebRequest.VariableMap[5]`, IpDrv's
   `PrivateResolveInfo`/`LinkPtr`/`MSLinkPtr`, ...) advances the layout by the C++ size; the property itself
   stays a 4-byte int (script never reads these; to be confirmed per entry). Identical sizes at 32-bit.
2. Object properties align to 4 at 64-bit (they align to `sizeof(UObject*)` now; the generated headers are
   pack(4)); hand-written mirror headers not yet under pack(4) get it where verify says so.
3. Native C++ size recorded at static class construction; at link a mismatch with the script size is fatal
   on 64-bit (ut99 decision 5).
4. `verifyclasses` 64-bit mode: the generated lists (as at 32-bit) + every native class's C++ size vs script
   size + the placeholder report + a layout dump; `tools/dev/m13-layout-check.py` compares the dump with the
   binary's DWARF (`dwarf-layouts.py`) for every class AND script struct member. 32-bit mode unchanged.
5. VM: port ut99 `253aaf2` (None-context fill re-zeroes the whole destination; switch compares pointer width).

### Step 3 — the layout proof and every pointer-through-int site in the census (2026-10-05)

**What "C++ and script agree" means at 64 bits** (decided): the shipped 32-bit `.u` files, linked by the
property system with this target's rules — object references 8 bytes aligned to **4** (`UObjectProperty::Link`,
was `sizeof(UObject*)`; the mirrors are pack(4) like Epic's /Zp4; same value on every 32-bit target),
`FString`/`TArray` `sizeof(FArray)`=16, `TMap` 32, delegates 16, and the **native placeholders** — must give
every native class, script struct and event-parameter struct the size and member offsets GCC gives the C++
declaration. Checked three ways:
1. **Link-time guard** (UnClass.cpp, 64-bit only): the C++ `sizeof` of every class registered by
   `DECLARE_CLASS` (recorded in both UStruct native constructors; static link uses `EStaticConstructor`) is
   compared with the linked script size; mismatch = `appErrorf` (fatal), or a `VERIFY: [layout]` warning on
   the console while `ucc verifyclasses` runs (`GLayoutWarnOnly`, set by UCC from argv before appInit).
2. **`ucc-bin verifyclasses` 64-bit mode** (UCC.cpp `VerifyNativeLayouts64`; the 32-bit mode is unchanged):
   loads UWeb too; runs the generated member-offset lists exactly as at 32-bit; then (a) every class with a
   C++ size: C++ vs script (400 classes), (b) every placeholder table entry exists and is linked at its C++
   size, (c) **no bytecode reads or writes a placeholder** — every `UStruct`'s script is walked with
   `SerializeExpr` on a collecting archive (6,578 scripts) and every `UProperty` it references is tested.
   `DUMP=<file>` (any width; opt-in) writes the linked layout of every class, script struct and function.
3. **`tools/dev/m13-layout-check.py <dump> <dwarf>`** (new): compares that dump with
   `tools/dev/dwarf-layouts.py ucc-bin` member by member: classes (AX/UX), script structs (FX) and event
   parameter structs (`UX_eventF_Parms`) — covers the hand-written headers and structs the generated lists
   never see.

**Native placeholders** (`GNativePlaceholders`, UnClass.cpp; ut99 `NativePlaceholderSize`, by string): a
script `int` standing in for C++ data advances the layout by the C++ size (`PH_Member`), or gives an
`array<int>` the pointer stride (`PH_ArrayElement`); the property itself stays a 4-byte int. Identical sizes
at 32-bit (the code is `#if PLATFORM_64BITS` anyway). 48 entries at step 3, 50 since step 6 (see the site table):

| Script (package.struct.property) | C++ | kind |
|---|---|---|
| Core.Object.ObjectInternal[7] | vtable, Index, HashNext, HashOuterNext, StateFrame, _Linker, _LinkerIndex (56) | member |
| Core.Subsystem.ExecVtbl; Engine.Player.vfOut, vfExec | FExec / FOutputDevice base vtables | member |
| Core.Orientation.MA | `UMatSubAction*` (FOrientation, Engine.h) | member |
| Engine.ActorRenderDataPtr/LightRenderDataPtr/ProjectorRenderInfoPtr/StaticMeshProjectorRenderInfoPtr.Ptr | typedef'd pointers (Engine.h) | member |
| Engine.Actor.OctreeNodes | `FOctreeNode*` in `TArray<PTRINT>` | array element |
| Engine.StateInfo.Node/StateNode/Code | `UStruct*`/`UState*`/`BYTE*` (FStateInfo, Controller.StateStack) | member |
| Engine.FileLog.LogAr | `FArchive*` | member |
| Engine.MovieMaterial.MovieDecoder/RenderInterface/InMemoryDecodingBlock/ThreadSynch/Thread | player, texture, buffer, critical section, thread | member |
| Engine.ParticleEmitter.Particles | `FParticle*` in `TArray<PTRINT>` | array element |
| Engine.Texture.RenderInterface, Cubemap.CubemapRenderInterface | `FStaticTexture*`, `FStaticCubemap*` | member |
| Engine.Canvas.pCanvasUtil; MotionBlur.RenderTargets[2]; FluidSurfaceInfo.Indices | `FCanvasUtil*`; 2 `FAuxRenderTarget*`; `void*` | member |
| UCGameplay.UCScreen{Bloom,Emissive,DOF,MotionBlur,RadialBlur,HeatShimmer}.*Target(s) | `FRenderTarget*` (BlurTargets: array element) | member/array |
| GUI.GUIController.Designer | `UPropertyManagerBase*` | member |
| GUI.UIXInteraction.m_pFont/m_pLiveEngine/m_pUIPlugin/LiveInterface/FriendsInterface | Xbox Live UI pointers | member |
| IpDrv.InternetLink.PrivateResolveInfo, MasterServerLink.LinkPtr, MasterServerClient.MSLinkPtr | resolver / link objects | member |
| UWeb.WebResponse.ReplacementMap[5], WebRequest.VariableMap[5] | `TMap`/`TMultiMap<FString,FString>` (32) | member |

Where the C++ member was a generated `INT` that native code casts to and from a pointer it is now `PTRINT`
(4 bytes, `unsigned long`, at 32-bit: same layout; casts `(INT)p` -> `(PTRINT)p`): EngineClasses.h
(`OctreeNodes`, `FStateInfo`, `LogAr`, the five MovieMaterial fields, `Particles`), GUIClasses.h (`Designer`),
IpDrvClasses.h (`PrivateResolveInfo`, `LinkPtr`, `MSLinkPtr`). An `FArchive << PTRINT&` (not on Windows,
where PTRINT is DWORD/QWORD) serves the counting archives that walk `Particles`.

**pack(4) on hand-written mirrors** (Epic built everything /Zp4; GCC packed only the generated headers):
whole-file `#pragma pack(push,4)` in UnMaterial.h, UnForceFeedbackWaveform.h, UnCanvas.h, UnPlayer.h,
UnTerrain.h, UnFluidSurface.h, UnCameraEffects.h, UnGame.h, UnEngine.h, UnURL.h (FURL), UnCorObj.h
(UCommandlet's tail), UCCameraEffects.h, UWeb.h; around `FLiveUser` (UnObjBas.h), `FOrientation`
(Engine.h), `UUIXInteraction` (UnUIXInteraction.h). None of these headers has an 8-byte scalar member
(grep `QWORD|DOUBLE|SQWORD|double`), so no 32-bit target's layout can change (i386 never aligns above 4;
wasm32/MinGW only differ for 8-byte scalars).

**Other census fixes**: `Align()` and `FMemStack::PushBytes` through `PTRINT` (were DWORD/INT: truncated
heap addresses), `STRUCT_OFFSET` `(INT)(PTRINT)`, object outer-hash input `(DWORD)(PTRINT)` (a hash, kept),
`UnAudio.cpp` RIFF chunk bound, `UnStats.cpp` memset length, `UnIn.cpp` binding offsets, **UInput alias
struct** described by `sizeof(FAlias)`/`STRUCT_OFFSET` (the arithmetic `sizeof(FName)+sizeof(FString)` put
`Command` at 4 instead of 8: the ut99 client crash on the first alias key), UnMovie.cpp log map keyed by
object index, **native class config name** stashed in a side table (UClass is pack(1): the 8-byte
`const TCHAR*` written into the 4-byte `ClassConfigName` overwrote half of `ClassConstructor` -> SIGSEGV
`0x0000555500005555` on the first object of Engine).

**Result** (`logs/m13/verify64-6.out`, `ucc.log`): `VERIFY: 64-bit native size check: 400 classes, 0
mismatches while linking`, `bytecode scan: 6578 scripts read for placeholder references` (0 references),
all 48 placeholders found, `All native class layouts match`, exit 0. Cast census of a full rebuild:
**0 ptr-to-int, 0 int-to-ptr** (`logs/m13/build64-3.log`). m13-layout-check: 5,697 properties, 400 classes,
122 structs, 202 event parm structs checked; 2 mismatches left: `Texture.__LastUpdateTime`/`MemUsage` are
declared in the opposite order in C++ and script — to compare with the 32-bit dump (expected pre-existing).

### Step 4 — VM: None-context fill and switch width; both widths verified (2026-10-05)

- `UnCorSc.cpp` (64-bit only, `#if PLATFORM_64BITS`): engine-ut99 `253aaf2` ported. `execContext`/
  `execClassContext` record where a None-context zero-fill went and how many bytes (the compiler's 32-bit
  size); `execLet` zeroes the destination's full `ElementSize` when exactly it got a short fill (else the high
  half of an old pointer survives `X = None.Y`). `execSwitch` compares at least `sizeof(UObject*)` bytes.
  UE2's `execStructMember` already zeroes its whole (linked-size) buffer and `P_GET_OBJECT` starts at NULL, so
  Unreal-1's P1/P3 do not apply.
- **32-bit unchanged, measured**: `dwarf-layouts.py` of main's 32-bit `ucc-bin` (776ed23,
  `engine-uc2/build-native`) against this branch's: the only differences are 4 new template instantiations
  (`TArray<unsigned long>`, `TArrayNoInit<unsigned long>`, `TObjectIterator<UStruct>`, `TTypeInfoBase<unsigned
  long>`); every existing type has the same size and member offsets (`logs/m13/dwarf32-*.txt`).
- `verifyclasses`: 32-bit `All native class layouts match` (exit 0, unchanged output), 64-bit the same plus
  the 64-bit checks (exit 0). m13-layout-check at 32-bit gives the same 2 `Texture.__LastUpdateTime`/`MemUsage`
  lines as at 64-bit: **pre-existing** (C++ declares MemUsage before __LastUpdateTime, script after; Epic's
  shipped PC/Xbox builds have the same order; script never reads either). Not changed.

### Step 5 — first runs: GC pointer copy; census parameter (2026-10-05)

- First 64-bit `ucc-bin Engine.ServerCommandlet DM-Deadbolt`: SIGSEGV in `FArchiveTagUsed::operator<<` at the
  first garbage collection (`LoadMap`): `appMemcpy(&Obj, &Object, sizeof(INT))` copied 4 bytes of every object
  reference the GC follows (rdx = 0x55e63b40, the low half). Both copies now `sizeof(UObject*)` (UnObj.cpp).
  The same grep (`appMemcpy.*sizeof(INT|DWORD)`) found no other pointer copy outside PSX2/GCN branches; the
  script call-graph profiler's `FLargeDWORDArray` is `#if PROFILE` (not built). After it: the server brings
  DM-Deadbolt up and ticks 60 s (`logs/m13/server64-smoke.out`).
- `tools/dev/map-census.sh`: `CENSUS_NAME` (run trees `<name>-N`, `logs/<name>/`) and `CENSUS_PORT`; defaults
  unchanged (`census`, 7780). M13 runs `CENSUS_NAME=m13-census CENSUS_PORT=7880` (64-bit) and
  `m13-census32`/7890 (this branch's 32-bit build) so it never touches the shared `census-*` trees.

### Step 6 — two punned pointer ints; presets arm64 and native64-uchar; script parameters (2026-10-05)

- A grep for `*(T**)&member` (type puns the compiler cannot see) found two script ints written as pointers
  through a pun, i.e. 8-byte stores into 4-byte fields that verify could not see (the C++ member was `INT`
  like the script): `ScriptedTexture.RenderTarget` (`*(FRenderTarget**)&RenderTarget`, UnScriptedTexture.cpp:
  would overwrite `RenderViewport`'s low half) and `ShadowBitmapMaterial.TextureInterfaces[3]`
  (`*(FAuxRenderTarget**)&TextureInterfaces[0]/[1]`, UnShadowProjector.cpp: [1] overlapped [0]'s high half).
  Both `PTRINT` in EngineClasses.h + placeholders (8 and 24). The other puns are on real pointer members
  (`FCheckResult::Next`, `*(BYTE**)&` stream pointers), PS2-only, or Core's (stashes, handled).
- CMake: `UC2_UNSIGNED_CHAR_AUDIT` (`-funsigned-char`, UnGnuG.h's `#error` lifted only for it), preset
  `native64-uchar`; preset `arm64` + `cmake/toolchain-aarch64-linux-gnu.cmake` (build-only, compiler missing).
  arm64 path read: every x86 asm/intrinsic site is gated `__i386__` (FloatToInt in UnRenderBatch/
  UnRenderStaticMesh -> `lrintf`), `__x86_64__||__i386__` (cpuid, rdtsc in UnUnix.cpp/UnGnuG.h; else
  `UT_NO_X86_INTRIN`, the wasm path), `_XBOX`, `__PSX2_EE__` or `__HAS_SSE__` (Intel compiler only); so
  arm64 takes the same C paths as x86_64 and wasm. Platform block: `__LINUX_X86__` is set for every non-Windows
  build (as for wasm); little-endian.
- `tools/dev/m12-wine-smoke.sh`: `M12_TREE`, `M12_REF`, `M12_OUT` (defaults m12, m12-native, logs/m12), for
  windows64. Its picture comparison moved to `tools/dev/pinned-compare.py` (unchanged code) so M13 reuses it.
- `tools/dev/m10-controls.sh`/`m12-wine-smoke.sh`: main's `b367ae0` hunks (`-nostartupmovies`, the 1 s click)
  applied verbatim, so the merge sees one version.

### Step 7 — saved data is width-independent; the verification script (2026-10-05)

- **Save games** (`FArchiveSaveGame`, Engine/Inc/ULoadSaveManager.h; profiles in `System/Profiles/<name>/`):
  object references are always saved as None, but in `sizeof(UObject*)` bytes — 4 in every shipped build, 8 at
  64-bit, so a 64-bit profile would not load in a 32-bit game (and vice versa: every later byte misplaced). Now
  always 4 bytes (an INT 0) on save; load reads 4 bytes and sets None. Byte-for-byte the old format at 32-bit.
  The rest of the format is fixed-width (`<<` of INT/FLOAT/FString/FName index; name table at the end).
- `tools/dev/m13-64bit.sh` (new): the gate's checks as one script (see its header): verify, census, controls,
  audio, movies, glselftest, effects, pinned, psh2glsl, soak, leftovers; `M13_TREE`/`M13_BUILD` select the
  `-funsigned-char` build. Logs under `logs/m13` (other trees: `logs/m13/<tree>`, with `logs/<tree>` a link,
  because the suites write to `logs/<tree>`).

### Step 8 — first census, an exit hang seen once, test hooks (2026-10-05)

- 64-bit census (`CENSUS_NAME=m13-census CENSUS_PORT=7880 bin/capped 8G 1800 -- tools/dev/map-census.sh
  build-native64 15 4`): **maps 58, PASS 58, FAIL 0** (`logs/m13-census/summary.tsv`).
- Seen once in that census: the OD-Prism server got its SIGTERM at 15 s, logged `appRequestExit(0)` and
  `Exit: Preparing to exit.`, then hung (main thread in a futex wait, the file-stream thread sleeping) until I
  killed it by PID minutes later; the census still says PASS because `timeout` without `-k` reports 124 whatever
  happens after the TERM. Six more runs of the same map exited in 16-17 s. Not reproduced, not diagnosed (gdb
  cannot attach here: ptrace_scope 1). Candidates, both width-independent: the SIGTERM handler is not
  async-signal-safe (`HandleSignal` -> `appRequestExit` -> `debugf`, formatting and the log lock from inside a
  signal handler), or a lock taken in `UObject::StaticExit`. Left for the owner's list (BLOCKERS.md).
- `CMakeLists.txt`: `-Werror=int-to-pointer-cast` (C++) on 64-bit targets; `UC2_SANITIZE` + preset
  `native64-asan` (`address,undefined`, minus `vptr`/`alignment`).
- `ucc-bin profiletest save|load <profile> <export.txt>` (UCC.cpp): writes a profile with a deterministic value
  in every plain property through `ULoadSaveManager` (the game's own save path) or loads one, and exports every
  property as text: the cross-width save test.

## Site table (every pointer-width site fixed in M13)

Kinds: **P** = script int/array standing in for C++ data (placeholder, UnClass.cpp), **C** = pointer through an
integer in C++ (cast census), **U** = type pun / literal size the compiler cannot see, **L** = layout (pack),
**F** = file format, **V** = script VM. "Exercised" = a check of tools/dev/m13-64bit.sh reaches the code at run
time (verify reaches every layout; listed here is what *runs* the C++ that uses the field).

| Site | What was stored where | Fix | Kind | Exercised by |
|---|---|---|---|---|
| UnObjBas.h/UnScript.h/UnClass.cpp/UnCorSc.cpp bytecode object slots | `UObject*` in 4-byte script slots | object index (`SerialPointerIndex`) | V | every run |
| UnObj.cpp native registration | `TCHAR*` name in `FName`, chain in `_LinkerIndex` | `StateFrame`/`_Linker` | U | every run (start-up) |
| UnClass.cpp UClass config name | `TCHAR*` in `FName ClassConfigName` (pack 1) | side table | U | every run |
| UnObj.cpp `FArchiveTagUsed` | GC copied 4 bytes of each reference | `sizeof(UObject*)` | U | every run (GC) |
| UnTemplate.h `Align`, UnMem.h `FMemStack` | heap addresses through DWORD/INT | PTRINT | C | every run |
| UnTemplate.h `STRUCT_OFFSET`, UnIn.cpp bindings | offsets via pointer->INT | `(INT)(PTRINT)` | C | every run |
| UnObj.cpp outer hash | pointer as hash input (DWORD) | `(DWORD)(PTRINT)` (hash) | C | every run |
| UnIn.cpp `UInput` alias struct | `sizeof(FName)+sizeof(FString)`, Command at 4 | `sizeof(FAlias)`, `STRUCT_OFFSET` | U | client (controls: key aliases) |
| UnProp.cpp object property alignment | aligned to 8 | 4 (pack 4) | L | every run |
| Object.ObjectInternal, Subsystem.ExecVtbl, Player.vfOut/vfExec | header, vtables | P | P | every run |
| Orientation.MA (FOrientation) | `UMatSubAction*` | P | P | **none** (matinee/cinematics: SceneManager not in any check) |
| Actor *RenderDataPtr / *ProjectorRenderInfoPtr structs | render caches | P | P | client GL runs |
| Actor.OctreeNodes | `FOctreeNode*` in array<int> | PTRINT + P(array) | C,P | every map run |
| StateInfo Node/StateNode/Code | Controller state stack | PTRINT + P | C,P | **none known** (PushState/PopState: AI scripts; not asserted) |
| FileLog.LogAr | `FArchive*` | PTRINT + P | C,P | **none** (stat/file logging) |
| MovieMaterial 5 fields | player, texture, buffer, lock, thread | PTRINT + P | C,P | movies (menu movie materials); in-memory decode thread: **none** |
| ParticleEmitter.Particles | `FParticle*` in array<int> | PTRINT + P(array) | C,P | maps with emitters (census, client) |
| Texture.RenderInterface, Cubemap.CubemapRenderInterface | render resources | P | P | client GL |
| Canvas.pCanvasUtil, MotionBlur.RenderTargets[2], FluidSurfaceInfo.Indices | renderer pointers | P | P | client GL (MotionBlur: effects only if used) |
| ScriptedTexture.RenderTarget, ShadowBitmapMaterial.TextureInterfaces[3] | punned pointers | PTRINT + P | U,P | shadows/scripted textures: effects runs only if in view; **not asserted** |
| UCScreen* render targets (6 classes) | `FRenderTarget*` | P | P | effects (post-processing) |
| GUIController.Designer | `UPropertyManagerBase*` | PTRINT + P | C,P | **none** (GUI designer, editor-only) |
| UIXInteraction 5 pointers | Xbox Live UI | P + pack | P,L | **none** (Xbox Live) |
| InternetLink.PrivateResolveInfo, MasterServerLink.LinkPtr, MasterServerClient.MSLinkPtr | resolver/links | PTRINT + P (IpDrv) | C,P | **none** (no DNS resolve / master server in tests) |
| WebResponse.ReplacementMap, WebRequest.VariableMap | TMap/TMultiMap | P + pack | P,L | **none** (web admin) |
| Hand-written mirrors (13 headers + FLiveUser, FOrientation, UUIXInteraction) | natural alignment | pack(4) | L | every run (verify), per class as above |
| UnAudio.cpp RIFF bound, UnStats.cpp memset length | pointer arithmetic via INT/DWORD | PTRINT | C | wave import: **none**; stats: every run |
| UnMovie.cpp log map key | `(INT)M` | object index | C | movies |
| UnOctree.cpp CompareArray | debug check | PTRINT | C | **none** (debug path) |
| UnCorSc.cpp None-context fill, switch | 4 of 8 bytes zeroed/compared | full width | V | scripts that hit None (any run; not asserted) |
| ULoadSaveManager.h save games | object ref as `sizeof(UObject*)` | 4 bytes | F | `saves` check (profiletest) |
| UnArc.h `<< PTRINT` | (new) counting archives over PTRINT arrays | operator | C | GC/memory stats |

Counts: P 50 placeholder entries (46 members — 8 of them script-struct members — and 4 array strides; one per
table entry), C 22 casts (cast census 119 diagnostic lines -> 0), U 6, L 16 headers/structs, V 2, F 1.

### Step 9 — first verification results (2026-10-05)

- Both censuses after the step-5 fixes: 64-bit (`logs/m13-census/summary.tsv`) **58/58 PASS**, this branch's 32-bit
  build (`logs/m13-census32/summary.tsv`) **58/58 PASS**, per-map result identical (`diff` of status+map empty).
  Each had about 2 servers crash during exit after the TERM (BLOCKERS.md, pre-existing, both widths).
- Rebuilt with `-Werror=int-to-pointer-cast`: 64-bit full build 0 errors, cast census **0 + 0**
  (`logs/m13/build64-final1.log`); 32-bit build 0 errors.
- 64-bit GL client, DM-Deadbolt with 4 bots, 70 s (`/usr/bin/time -v` inside `capped 3G`): **max RSS 227 MB**,
  0 GL errors in 1,124 frames — the suites' 2G ceilings stay right for 64-bit (`logs/m13/client64-mem.out`).
- `m13-64bit.sh verify psh2glsl glselftest pinned`: all PASS — verify (400 classes, 0 link mismatches, 50
  placeholders, 6,578 scripts scanned, DWARF 2 = 2 pre-existing); psh2glsl 100 shaders 0 failed; glselftest
  10/10; **pinned: 64-bit and 32-bit at the same loc=(1531.9,3111.5,-34.1) rot=(0,-40880,0), static part
  100.000% within 2 levels**, whole frame 89.7% bit-identical, 0 GL errors.
- New check `saves` (profiletest on both trees, files compared byte for byte, cross loads).
