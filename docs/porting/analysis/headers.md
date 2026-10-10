# T2 notebook: the generated C++ headers regenerate from scratch

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

