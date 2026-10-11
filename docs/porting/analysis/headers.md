# T2 notebook: the generated C++ headers regenerate from scratch

> 2026-10-11 (T4): the script sources this notebook refers to (`Source/<Pkg>/Classes`, `.ucproject`) left the
> repository; Epic's text is `<WorkUC2>/epic-sources/<Pkg>/Classes` (dev machine only), the build's base is the
> decompiled `build-*/Scripts/base`, our changes are `patches/` (`scripts-removed.md`).

Task T2 (2026-10-10). The owner: "Why can't they be generated from scratch? Can't we patch the generator so that
it generates 64-bit headers by default?" Goal: `Source/<Pkg>/Inc/<Pkg>Classes.h` are pure output of the
script compiler's header exporter (`ucc-bin make -ForceHeads`, Editor's `UClassExporterH`), 64-bit-correct
whatever the host's pointer size, and a check fails the build workflow when they are stale.

Worktree `engine-uc2-t2-headers`, branch `t2-headers` (from main 36267c6). Run trees `<WorkUC2>/run/t2*`, logs
`<WorkUC2>/logs/t2/`. Every run through `<WorkUC2>/bin/capped`.

## Step 1 (2026-10-10): baseline, today's exporter against the committed headers

```
tools/dev/script-build.sh --tree t2-hdr --headers assemble make     # t<N> tree prefix added to script-build.sh
    # ucc-bin make -nobind -CONFORMNOCONFIG -ForceHeads: exit 1 in 16 s, Success - 0 error(s), 85 warning(s)
    # diff: <WorkUC2>/logs/t2/baseline.diff (< committed Source/, > exported)
```

Identical: GameSpyClasses.h, UCElementsClasses.h, UCGameplayClasses.h, UnrealGameClasses.h. No CoreClasses.h or
UWebClasses.h is written or committed (every native class of Core and UWeb is `noexport`, hand-written).
Differences (diff lines, `<` and `>` counted):

| Header | Diff lines | What | Against scripts.md step 2c |
|---|---|---|---|
| EngineClasses.h | 26 | 13 members `PTRINT` ours, `INT` exported: StateInfo.Node/StateNode/Code, Actor.OctreeNodes (`TArrayNoInit<PTRINT>`), FileLog.LogAr, MovieMaterial x5, ScriptedTexture.RenderTarget, ShadowBitmapMaterial.TextureInterfaces[3], ParticleEmitter.Particles (`TArrayNoInit<PTRINT>`) | as listed (the step 2c table says 16 lines for Engine; today it is 26 = 13 members, all in the placeholder table) |
| GUIClasses.h | 2 | GUIController.Designer | as listed |
| IpDrvClasses.h | 6 | InternetLink.PrivateResolveInfo, MasterServerLink.LinkPtr, MasterServerClient.MSLinkPtr | the table's "8" included the 2 HTTPDownload registrant lines, now gone (below) |
| EngineClasses.h | 6 | 3 comment lines in `UPSDistortionBaseMaterial`'s cpptext (`// UC2 port (M7): ...`) exported as 3 blank lines | as listed |
| EditorClasses.h | 4 | `UmakeCommandlet` (ours, = Epic's leak) vs `UMakeCommandlet` (exported), so also at another sorted position | as listed |
| UnrealEdClasses.h | 16 | 8 `GCC_PACK(4)` on the first member of a class (exporter rule restored at M3, header predates it) | as listed |

**Resolved since step 2c, nothing new**: the `UHTTPDownload`/`UPrefab` registrant placement no longer differs.
e0bce15 (2026-10-10, "IpDrv: HTTPDownload registered as IpDrv.HTTPDownload (Prefab as Editor.Prefab)") changed the
C++ `DECLARE_CLASS(...,Engine)` of both to `IpDrv`/`Editor` (Epic's leak says `Engine`: `epic-sources/IpDrv/Inc/
HTTPDownload.h:24`, `Editor/Inc/UnPrefab.h:21`), so our static build registers them in the package that declares
them and the exporter lists them where the committed headers (and Epic's leaked headers) have them.

## Step 2 (2026-10-10): the exporter reads the placeholder table

The knowledge "this script `int` is a C++ pointer" lives in one table, `GNativePlaceholders[]`
(`Source/Core/Src/UnClass.cpp`), used by `UStruct::Link` at 64 bits. Changes:

- The table, its accessors and `FindNativePlaceholder` are compiled at every pointer width (they were inside
  `#if PLATFORM_64BITS`; `UStruct::Link`'s use of them still is): the exporter runs in the 32-bit `ucc-bin`.
- A sixth column, `CppType`: the C++ spelling of one element of the stand-in. `PTRINT` for the 44 pointer rows
  (one pointer per script element: `int X` -> `PTRINT X`, `int X[3]` -> `PTRINT X[3]`, `array<int> X` ->
  `TArrayNoInit<PTRINT> X`); `NULL` for the 6 rows whose C++ data no exported member can spell (`ObjectInternal`:
  the object header; `ExecVtbl`, `vfOut`, `vfExec`: vtable pointers of secondary C++ bases; `ReplacementMap`,
  `VariableMap`: a `TMap`/`TMultiMap` behind `int[5]`). Those classes are `noexport` (Core's `Object`, `Subsystem`,
  Engine's `Player`, UWeb's `WebResponse`/`WebRequest`: hand-written headers), so the exporter never meets them;
  if one ever became exported, `GetNativePlaceholderCppType` logs an error naming the row and counts it
  (`make` ends `Failure - 1 error(s)`) instead of writing a header that is wrong at 64 bits. The spelling is
  chosen from the row, never from the host's `sizeof(void*)` (4 on the exporting host, so sizes cannot tell a
  pointer from an int).
- `GetNativePlaceholderCppType(UProperty*)` (UnClass.cpp, next to `IsNativePlaceholder`): the lookup Link does, by
  string (package, struct, property); an `array<int>`'s inner property is outered to the array property, so the
  inner looks up the array (kind `PH_ArrayElement`). `UIntProperty::ExportCppItem` (`Source/Core/Src/UnProp.cpp`)
  writes what it returns instead of the literal `INT`: the one place a property's C++ type is chosen, so
  class members, struct members and event parameters all go through it (no placeholder is a parameter today).
- Hardening on the way: `FindNativePlaceholder` cast an `int` property named like a `PH_ArrayElement` row to
  `UArrayProperty*` before checking it was one (no such name today); it checks the class first now.
- `ucc-bin verifyclasses` (64-bit mode, `Source/UCC/Src/UCC.cpp`): each `PTRINT` row must be linked at the size
  that spelling has (`ArrayDim * sizeof(PTRINT)` for a member, `sizeof(PTRINT)` for an array element), so the
  table's Size and its CppType cannot disagree.

Result (`tools/dev/script-build.sh --tree t2-hdr --headers assemble make`, `logs/t2/step2.diff`): `Success - 0
error(s), 85 warning(s)` (as before); GUIClasses.h and IpDrvClasses.h now identical to the committed ones,
EngineClasses.h differs only by the 3 cpptext comment lines: all 17 `PTRINT` members (13 Engine, 1 GUI, 3 IpDrv)
are generated. Left: EditorClasses.h (`UmakeCommandlet`), UnrealEdClasses.h (`GCC_PACK(4)`), the comment lines.

Not in the table on purpose: UnrealEd's `WBrowserLIPSincPtr`, `WBrowserAnimationPtr` (script `int`s holding
editor-window pointers, exported as `INT`): UnrealEd's C++ is the editor application, not built anywhere, and the
Editor package is 32-bit only (root CMakeLists, M14 block); a 64-bit editor would need rows for them.

## Step 3 (2026-10-10): the other differences, decided

| Difference | Decision | Why |
|---|---|---|
| `UHTTPDownload`/`UPrefab` registrants | nothing to do | already resolved by e0bce15 (step 1): the C++ `DECLARE_CLASS` names IpDrv/Editor, the exporter lists them there, as Epic's leaked IpDrvClasses.h/EditorClasses.h do. No tolerated deviation needed |
| 3 comment lines in `UPSDistortionBaseMaterial`'s cpptext | take the exporter's output (3 blank lines) | the exporter writes cpptext after the preprocessor has stripped comments. The comment is ours (M7) and its content is already in the .cpp, above the two definitions it explains (`Source/Engine/Src/UnPixelShaderMaterial.cpp:401-406`), and stays in the `.uc` cpptext (`Source/Engine/Classes/PSDistortionBaseMaterial.uc:40-42`), where comments are free; the `.uc` is not touched (script sources are T1's) |
| `UmakeCommandlet` (committed, = Epic's leak) vs `UMakeCommandlet` (exported) | take the exporter's | the C++ class is `UMakeCommandlet` (`Source/Editor/Src/UMakeCommandlet.cpp:21`, also Epic's); the exporter prints the class's FName, whose case is the first spelling the name table saw. Epic's DLL build loaded Editor.dll after UCC had looked up the typed `make` + `Commandlet`, so its table held `makeCommandlet`; our static build registers the class before the command line is read. `UmakeCommandlet::StaticClass()` would not compile; nothing compiles this list (`AUTO_INITIALIZE_REGISTRANTS_EDITOR` is mirrored by hand in `Source/Editor/Src/UC2EditorRegistrants.cpp`, which says `X(UMakeCommandlet)`). The line also moves to its case-sensitive sorted place |
| 8 `GCC_PACK(4)` on UnrealEd first members | take the exporter's | the rule (tag the first member of a class, as engine-ut2004's exporter does) was restored at M3; the committed UnrealEdClasses.h predates it. UnrealEd's C++ is not built: no `Source/UnrealEd/CMakeLists.txt`, not in `UC2_PACKAGES`, the only includer of UnrealEdClasses.h is `Source/UnrealEd/Inc/UnrealEd.h`, which no other package includes, and no `compiler_depend.make` of build-native names `UnrealEd/Inc` |

So every header can be the exporter's output byte for byte (LF line endings: the exporter writes CRLF, the repo
keeps LF; the copy strips CR, as `script-build.sh`'s diff always did).

## Step 4 (2026-10-10): every header regenerated, the committed ones replaced

```
tools/dev/regen-headers.sh        # export (script-build.sh assemble + make -ForceHeads in run/hdr-engine-uc2-t2-headers),
                                  # then each exported <Pkg>/Inc/<Pkg>Classes.h, CRs dropped, over Source/
```

`git diff --stat -- 'Source/*/Inc/*Classes.h'`: 3 files, 13 insertions, 13 deletions. Every changed line:

| File | Lines | Change | Step-1 row |
|---|---|---|---|
| EditorClasses.h | 219-220 (+), 300-301 (-) | `UmakeCommandlet::StaticClass(); \` / `VERIFY_CLASS_SIZE(UmakeCommandlet);\` -> `UMakeCommandlet`, at its sorted place | FName case (step 3) |
| EngineClasses.h | 7681-7683 | the 3 `// UC2 port (M7): ...` comment lines in `UPSDistortionBaseMaterial`'s cpptext -> 3 lines holding a tab | cpptext comments (step 3) |
| UnrealEdClasses.h | 34, 47, 56, 71, 127, 165, 179, 197 | `INT WBrowserLIPSincPtr;` (x3), `INT WBrowserAnimationPtr;` (x4), `INT OldArrayCount;` -> `... GCC_PACK(4);` (first member of each class) | GCC_PACK (step 3) |

Unchanged (already byte-identical to the export once the exporter writes `PTRINT`, step 2): GUIClasses.h,
IpDrvClasses.h, UCElementsClasses.h, UCGameplayClasses.h, UnrealGameClasses.h; EngineClasses.h's 13 `PTRINT`
members. So the list is exactly the step-1 table, resolved; nothing else moved. IpDrv's `GameSpyClasses.h` is
hand-written (Epic's GameSpy validation code, included by InternetLink.cpp) and not generated: the old
`script-build.sh --headers` diff called it "identical" only because the assemble stage had copied it into the tree.

Builds and the layout proof (logs in `<WorkUC2>/logs/t2/`):

| Build / check | Command | Result |
|---|---|---|
| native (32-bit, with Editor) | `bin/capped 6G 3600 -- cmake --build build-native -j4` | rc 0, 0 `error:` lines, `[100%] Built target UCC` (`build-native-2.log`) |
| native64 | `cmake --preset native64`; `bin/capped 6G 3600 -- cmake --build build-native64 -j4` | rc 0, 0 `error:` (`build-native64.log`) |
| MinGW i686 (with Editor) | `cmake --preset windows`; `bin/capped 6G 5400 -- cmake --build build-windows -j2` (load above 10) | rc 0, 0 `error:`, `[100%] Built target UCC` (`build-windows.log`) |
| wasm-perf (wasm32 layout) | `source env.sh`; `cmake --preset wasm-perf`; `bin/capped 6G 5400 -- cmake --build build-wasm-perf -j2` | rc 0, 0 `error:`, `[100%] Built target SDLLaunch` (`build-wasm-perf.log`) |
| verifyclasses 64 and 32 | `M13_TREE=t2-m13 M13_REF=t2-m13-32 tools/dev/m13-64bit.sh verify` | `PASS verify: exit 0, 'VERIFY: all native class layouts match'; 64-bit native size check: 401 classes, 0 mismatches while linking; 50 placeholders linked at their C++ size; bytecode scan: 6589 scripts read, 0 placeholder references; DWARF layout-check: 2 mismatches at 64-bit, layout-check: 3 mismatches at 32-bit, none new` (`logs/t2/t2-m13/`) |

The 32-bit run (`verify32.log`): `VERIFY: Editor class layouts match`, `VERIFY: all native class layouts match`,
`All native class layouts match`. No `exported as PTRINT ... but linked at` line at 64 bits (the new spelling/size
check of step 2). The DWARF mismatches are the known ones: `UTexture` `__LastUpdateTime`/`MemUsage` at both widths
and, at 32 bits, `Editor.EditorEngine.Pad` (Editor.h mirrors script's `int Pad[8]` with four pointers; the same 3
lines in `logs/m13/m13-f4b/verify32.check.txt` of 2026-10-09). 401 classes / 6589 scripts against 400 / 6578 in
64bit.md: main has grown since (e0bce15's IpDrv.HTTPDownload registration and the D-phase scripts).
Compiled code is unaffected by the regeneration anyway: EditorClasses.h's registrant macro is not compiled,
EngineClasses.h changes only comment text, UnrealEd is not built.

## Step 5 (2026-10-10): the staleness check

- `tools/dev/check-headers.sh [--build <dir>] [--classes-root <dir>] [--tree hdr...] [--logs <dir>]`: assembles a
  scratch run tree with `script-build.sh` (default `<WorkUC2>/run/hdr-<checkout dir name>`, so worktrees do not
  share one), deletes the tree's copies of the generated headers (a package that stopped exporting must not leave a
  current-looking copy), runs `ucc-bin make -ForceHeads` (inside `bin/capped 3G`), requires `Success - 0 error(s)`,
  then compares every exported `<Pkg>/Inc/<Pkg>Classes.h` (CRs dropped) with `Source/<Pkg>/Inc/<Pkg>Classes.h`
  with `cmp`, and every committed `<Pkg>Classes.h` must have been exported. Exit 0 current, 1 stale (a unified
  diff, also `<WorkUC2>/logs/hdr/<tree>-check.diff`, and the regenerate command), 2 could not run (no ucc-bin with
  Editor, no sources, make failed). About 30 s.
- `tools/dev/regen-headers.sh` = `check-headers.sh --write`: the same export, then the copy over `Source/`.
- `--classes-root <dir>` (both, and `script-build.sh`): the script sources are `<dir>/<Pkg>/Classes`, default
  `Source/`. For T1/T3: point it at the build-generated script tree (CMake: `-DUC2_HEADERS_CLASSES_ROOT=<dir>`).
  The headers are always compared with / written to this checkout's `Source/<Pkg>/Inc`.
- CMake target `check-headers` (root CMakeLists.txt, T2 block at the end): defined when the `Editor` and `UCC`
  targets exist on a non-Windows, non-Emscripten, non-cross build, i.e. the native 32-bit preset; depends on `UCC`;
  not in ALL (it compiles every package and needs the asset tree). `cmake --build build-native --target
  check-headers`.
- `script-build.sh`: `t<N>` and `hdr` tree prefixes (logs in `<WorkUC2>/logs/t<N>`, `<WorkUC2>/logs/hdr`);
  `m13-64bit.sh` accepts `t<N>` trees.
- Documented in BUILD.md ("Scripts (M14)") and scripts.md ("How to change a script", item 4; step 2c marked
  superseded).

Observed:
- On the regenerated headers: `bin/capped 6G 1200 -- cmake --build build-native --target check-headers` -> rc 0,
  8 lines `<Pkg>/Inc/<Pkg>Classes.h: current`, `check-headers: all 8 generated headers are current (scripts:
  .../Source)`, `[100%] Built target check-headers` (`logs/t2/check-headers-current.out`).
- Before the regeneration (the old committed headers, the fixed exporter): rc 1, Editor/Engine/UnrealEd `STALE`
  (`logs/t2/check-before-regen.out`; it also caught `GameSpyClasses.h` as "committed but not exported", which is
  how it was found to be hand-written; the check now considers only `<Pkg>/Inc/<Pkg>Classes.h`).
- A script change the headers do not follow: a scratch copy of the sources (`--classes-root <scratchpad>/stale-root
  --tree hdr-t2-stale`) with `var int T2StaleCheck;` added after `PrivateResolveInfo` in IpDrv's InternetLink.uc:
  rc 1, `IpDrv/Inc/IpDrvClasses.h: STALE`, diff `+    INT T2StaleCheck;` after `PTRINT PrivateResolveInfo;`
  (`logs/t2/check-headers-stale-script.out`).
- A hand edit the generator would not make (the old way of keeping a 64-bit header: `PTRINT LogAr` turned back into
  `INT LogAr` in the committed EngineClasses.h, restored afterwards with `git checkout`): rc 1,
  `Engine/Inc/EngineClasses.h: STALE`, diff `-    INT LogAr GCC_PACK(4);` / `+    PTRINT LogAr GCC_PACK(4);`
  (`logs/t2/check-headers-stale-header.out`).
- A non-pointer placeholder in an exported class (scratch sources, `noexport` commented out of UWeb's
  WebResponse.uc): rc 2, `check-headers: ucc-bin make did not succeed (Failure - 1 error(s), 86 warning(s))`, the
  log says `Error: Native placeholder UWeb.WebResponse.ReplacementMap has no C++ spelling: its class must be
  noexport (Core/Src/UnClass.cpp GNativePlaceholders)` (`logs/t2/check-headers-noexport-demo.out`).
- After all commits: `cmake --build build-native --target check-headers` rc 0, `check-headers: all 8 generated
  headers are current` (`logs/t2/check-headers-final.out`).

Memory: every ucc run inside `bin/capped 3G` (script-build.sh's default), builds 6G, verify 3G/2G + DWARF 4G (the
m13-64bit.sh ceilings); no ceiling was hit (no exit 137).

## Step 6: the paragraph for REPORT.md section 60 (for the coordinator; REPORT.md not edited)

> **T2 generated C++ headers regenerate from scratch** (2026-10-10, `headers.md`). The ten `<Pkg>Classes.h` the
> script compiler exports (eight exist: Core and UWeb export none) had been hand-edited for 64 bits, so they could
> only be compared with the exporter's output, never regenerated (section 30, M14, "The C++ headers are compared,
> never regenerated": superseded). The exporter now takes the C++ spelling of each script `int` that holds a C++
> pointer from the same table `UStruct::Link` uses at 64 bits (`GNativePlaceholders`, Core/Src/UnClass.cpp, one
> row per member, a new `CppType` column; compiled at every width because `ucc-bin make` is 32-bit): 17 members
> come out `PTRINT` (13 Engine, 1 GUI, 3 IpDrv), arrays as `TArrayNoInit<PTRINT>`; rows with no C++ spelling (object
> header, vtables, maps) make the export fail if their class is ever exported. The committed headers were replaced
> by the regenerated ones; the only changed lines were three earlier hand deviations, each taken from the
> generator: `UMakeCommandlet` for Epic's `UmakeCommandlet` in an uncompiled registrant list, a three-line M7 comment
> in a cpptext block (its content is in the .cpp), and `GCC_PACK(4)` on UnrealEd's first members (not built). Native,
> native64, MinGW and wasm-perf build; `ucc-bin verifyclasses` passes at 32 and 64 bits, nothing new.
> `tools/dev/check-headers.sh` (CMake target `check-headers`, native preset) exports and compares byte for byte and
> fails with the diff when a script change was not followed; `tools/dev/regen-headers.sh` regenerates. Both take
> `--classes-root` for the build-generated script tree of T1/T3. Deviations: none left in the generated headers;
> UnrealEd's two editor-window pointer ints stay `INT` (32-bit-only editor, not built).

## Open questions

- `check-headers` is a separate target, not part of ALL nor of a test suite: it needs the asset tree and compiles
  all 17 packages (about 30 s). Whether the coordinator wants it wired into a gate script (e.g. m14-scripts-check.sh)
  or CI is open.
- When T3 moves the script sources to a build-generated tree, the CMake cache variable `UC2_HEADERS_CLASSES_ROOT`
  (or a dependency on the generating target) has to point the check there.
- UnrealEd's pointer ints (`WBrowserLIPSincPtr`, `WBrowserAnimationPtr`) have no placeholder rows;
  they matter only if a 64-bit editor is ever built.
