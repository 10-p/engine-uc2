# First runtime: from "links" to "every map loads on a headless server" (M3)

What it took to get from a linked `ucc-bin` to a dedicated server that loads any of the 58 maps and ticks.
Each item was found by running, not by reading. Tools added on the way are at the end.

## Gate evidence (2026-10-04)

- `ucc-bin verifyclasses` → `VERIFY: all native class layouts match` (0 mismatches; it was ~120). It loads
  Engine, IpDrv, UnrealGame, UCElements, UCGameplay and GUI and runs the size/offset check lists the generated
  headers carry (713 checks).
- `ucc-bin Engine.ServerCommandlet CTF-Octane -log`, 60 s: `Game class is 'CTFGame'`,
  `Bringing Level CTF-Octane.MyLevel up for play (0)...`, still ticking at the timeout, clean shutdown
  (`Success - 0 error(s), 1 warning(s)`).
- `tools/dev/map-census.sh build-native 15 4` → `maps: 58  PASS: 58  FAIL: 0`, twice in a row
  (`<WorkUC2>/logs/census/summary.tsv`, one log per map). PASS = level brought up for play, nothing Critical,
  still ticking after 15 s.
- The only warning anywhere: `Missing Class Class Editor.TransBuffer` (56 maps reference the editor's
  transaction buffer; Editor is not built; harmless for play).

Not checked by this gate: anything a client does (rendering, input, audio), bots actually playing, script
behaviour beyond `InitGame`/`BeginPlay`, and the web build at runtime. `FScriptPatcher` runs at begin-play for
non-entry levels by code inspection (`UnGame.cpp`); it does not log, so there is no line proving it.

## What was wrong, in the order it surfaced

1. **Objects came out of construction with no class.** `DECLARE_CLASS` built objects with
   `new(...) TClass()`. That is value-initialisation: when the class has no user-provided constructor, ISO C++
   zero-fills the object first, wiping the `Class`/`Name`/`Outer`/`Index` that `StaticAllocateObject` had
   just stored. MSVC 7.1 did not zero-fill. Now `new(...) TClass` (as in the UT2004 tree).
   `Core/Inc/UnObjBas.h`.
2. **UWeb natives not found.** Its hand-written static header registers classes but not native lookups; UCC
   adds `FindUWebUWebResponseNative`/`FindUWebUWebRequestNative` (as engine-ut2004's UCC does).
3. **Class names corrupted in transit** (`UCDamageType` → `UCDaocgeType`). `UObject::ResolveName` stripped the
   package prefix with `strcpy` on overlapping buffers; glibc's optimised copy corrupts that, MSVC's byte copy
   did not. Now `appMemmove`. `Core/Src/UnObj.cpp`.
4. **C++ and script disagreed on class layout** (crash in `AActor::PostLoad`: `Brush` held the float 1.0).
   Three causes, all in how headers are tagged for GCC:
   - The header exporter tagged a bool with `GCC_PACK(4)` when its `CPF_Transient` differed from the previous
     bool's. `UBoolProperty::Link` packs those together and MSVC ignores the tag, so only GCC split the run
     and shifted everything after it (`AActor` was 580 instead of 576). 17 tags removed.
   - The exporter did not tag the first member of a class, so GCC placed a small first member in the parent's
     tail padding (Itanium ABI), which MSVC and the script linker never do. 183 first members tagged; the
     exporter rule is restored (engine-ut2004's has it).
   - Hand-written class headers needed the same rules: `UnTerrain.h`, `UnCameraEffects.h`,
     `UCCameraEffects.h`.
   The exporter (`Editor/Src/UnEdExp.cpp`) is fixed too, so a regeneration at M14 agrees.
5. **A header had fallen behind its script.** `UCCameraEffects.h` (`UUCScreenDOF`) lacked `FirstPersonAlpha`
   and `ThirdPersonAlpha`, which `UCScreenDOF.uc` declares before `AbsoluteBlurAlpha`. Under MSVC the class
   size still matched, so Epic's size check passed while native code read `FirstPersonAlpha`'s byte as
   `AbsoluteBlurAlpha`. The members were added. **This is a deliberate deviation from the shipped binary's
   behaviour** (see below).
6. **The map argument was dropped.** `appInit` skipped the first command-line token (the executable name in
   the Windows convention) on every non-Xbox platform, because its `_MSC_VER` condition had been commented
   out. MSVC-only again. `Core/Src/UnMisc.cpp`.
7. **Port 1000.** The Xbox system-link port is privileged on Linux. `Engine/Config/Default.ini` now has 7777
   (`Engine/Config/README.md` lists config deviations).
8. **"Negative delta time!" in about a quarter of runs.** `appSecondsSlow()` accumulated `gettimeofday()`
   differences in two statics; UC2's file streaming thread and the game thread raced on them and the counter
   stepped backwards. Now `clock_gettime(CLOCK_MONOTONIC)` minus a once-initialised origin: monotonic, no
   shared mutable state. `Core/Src/UnUnix.cpp`. (engine-ut2004 has the old function but no second thread
   calling it.)

## Deviation for the owner to review (visible from M7)

`UUCScreenDOF::AbsoluteBlurAlpha`: script sets it to `ThirdPersonAlpha` (100) or `FirstPersonAlpha` (65)
depending on the camera, and native code copies it into the blur colour's alpha. With Epic's stale header the
shipped binary always read `FirstPersonAlpha`'s byte there (65). We now read what script intends. If the
always-on depth-of-field looks stronger in third person than on Xbox, this is why; restoring the shipped
behaviour is a one-line change (read `FirstPersonAlpha` at that site).

## Still open

- The commandlet short names (`ucc server`) are not resolved: the registry built from localisation files is
  empty. `GetRegistryObjects` looks for `.int` files where UT2003 kept them; UC2 keeps them under
  `System/Localization/<lang>/`. Use full class names (`Engine.ServerCommandlet`) until that is looked at
  (it also affects anything else that enumerates registry objects, e.g. game types or mutators in menus: M4).
- Struct layout on wasm32/x86_64 for hand-written structs with 8-byte members: `verifyclasses` must be run on
  the web build (M4w) and on `native64` (M13) before trusting either.
- `ComboMatinees/*.unr` (9 maps) are not on the package search path and were not in the census.

## Tools

| Tool | What it does |
|---|---|
| `ucc-bin verifyclasses` | compares script and C++ layout of every native class before any map loads |
| `tools/dev/mkrun-uc2.sh <build-dir> [name]` | assembles `<WorkUC2>/run/<name>` (content hard-linked, own `System/`) |
| `tools/dev/map-census.sh <build-dir> [secs] [workers]` | loads every map on a headless server; `logs/census/summary.tsv` |
| `tools/dev/fix-bitfield-pack.py [--check] <headers>` | applies the two GCC tagging rules to class headers |
