# M14 notebook: script compiler (`ucc make`) and the proof against the shipped `.u`

Milestone D-1 of the owner review (PLAN.md, "Phase D"). Goal: a working UnrealScript compiler on Linux that rebuilds
the 17 EditPackages from `Source/<Pkg>/Classes`, a tool that proves the result equal to the shipped `.u` + the 30
`ScriptPatches.h` patches, and a run of the game on the recompiled packages. No gameplay or UI change.

Worktree `engine-uc2-m14`, branch `m14-scripts`. Run trees `<WorkUC2>/run/m14*`, logs `<WorkUC2>/logs/m14/`.
Every run through `<WorkUC2>/bin/capped`.

## Step 0 (2026-10-07): what the tree says before any change

Facts established by reading, each with its source:

- **The Editor package is not built yet.** `Source/Editor` has no `CMakeLists.txt`, so the root list
  (`UC2_PACKAGES` names `Editor`) skips it. `ucc-bin` links GUI, UCGameplay, UCElements, UnrealGame, UWeb, IpDrv,
  Engine, Core (+ UC2ALAudio, UC2Movie). Building Editor is part of this milestone.
- **engine-ut2004 has no working `ucc make` to copy.** Its root CMakeLists says "Editor and UTV2004 excluded per
  D-10", and UT2004's own `UMakeCommandlet::Main` starts with `#if !WIN32 "'ucc make' is broken on Unix/Mac right
  now."`. Only small GCC fixes can be borrowed from its Editor sources (e.g. `register INT i, j` in `Perlin.h`,
  `&FEditorVisibility::AddPortal` in `UnVisi.cpp`), as diffs, not as a template.
- **How Epic built the shipped scripts** (`Source/BuildProcess/sync_avaris.pl`):
  1. `del System\*.u` (every package rebuilt, in EditPackages order);
  2. `copy DefUserXbox.ini User.ini`, `copy DefaultXbox.ini Avaris.ini` (the Xbox inis are the compile inis);
  3. `ucc make -nobind -noh` (`$scriptcompileoptions`; `-noh` = no C++ header export; `-nobind` = do not fail when a
     native function has no C++ (`UPackage::GetDllExport`, Core/Src/UnObj.cpp));
  4. `ucc patchscript *.u -nosummary`: `UPatchScriptCommandlet` (UnrealUtil/Src/UnPatchCommandlets.cpp) loads the
     fresh `System/*.u` and the originals in `System/OriginalScript/*.u`, compares them and writes the differences
     as C++ (`ScriptPatches.h`);
  5. `revertscript()`: the fresh `.u` are reverted to the depot's.
  So the shipped `System/*.u` (= `OriginalScript/*.u` = `GUIRes/*.u`, byte-identical) are the **retail** scripts,
  and `ScriptPatches.h` is exactly "what the later source compiles to, minus retail". The Xbox title update could
  replace the XBE but not the `.u` on the disc: hence load-time patching. Expected end state: recompiled = shipped
  + patches.
- **Preprocessor symbols** (`Editor/Src/FTextFilter.cpp` `MacroProcessingFilter::initSymbolTable`): `debug` is
  defined only by `-debug` on the command line (not passed by the build); `FINAL_RELEASE` only if the Editor C++
  was compiled with it (PC `Release|Win32` of `Editor.vcproj`: `NDEBUG;_WINDOWS;EDITOR_API=...;_EDITOR_;
  _WIN32_IE=0x0200;WIN32`, no FINAL_RELEASE; only the XboxLaunch configs define it). So for the shipped PC `.u`:
  `` `logd(...) `` expands to nothing, `` `log(...) `` expands to a `log(...)` call (`Core/Classes/Avaris.huc`).
  The bytecode can confirm this (log calls present, logd absent).
- **Conform**: `UMakeCommandlet` (Editor/Src/UMakeCommandlet.cpp) conforms each new package against
  `../GUIRes/<Pkg>.u` unless `-NOCONFORM`, and then calls `LoadConfig(1)` on every class default of the package
  being conformed. Conforming orders names/exports like the old package (network compatibility), which is what
  makes an export-by-export comparison with the shipped file possible. `-LOADCONFIGS` (import ini values into
  defaults) exists but the build did not pass it.
- **Header exporter**: `-noh` in the build; with headers on, `UExporter::ExportToFile(..., ../<Pkg>/Inc/
  <Pkg>Classes.h, !ReplaceHeads, 1, ForceHeads)`. We must never let it write over `Source/*/Inc`.
- `Editor/Inc/EditorClasses.h` (generated) registers `UmakeCommandlet` (lower-case m: an FName case artifact;
  the class is `UMakeCommandlet`). That list is only compiled under `__STATIC_LINK`, which no Epic build used for
  Editor.

Next: `Source/Editor/CMakeLists.txt`, GCC fixes, link Editor into `ucc-bin`.

## Step 1 (2026-10-07): Editor builds, `ucc-bin make` compiles all 17 packages

Commands (all through `capped`):

```
bin/capped 4G 600 -- cmake --preset native                    # picks up Source/Editor/CMakeLists.txt
bin/capped 6G 3000 -- cmake --build build-native -j4
tools/dev/script-build.sh assemble make                       # tree <WorkUC2>/run/m14-make, log logs/m14/m14-make-make.log
```

Result: `Success - 0 error(s), 101 warning(s)`, all 17 `.u` written into `run/m14-make/System/` in 6-10 s, then a
crash at exit (see "open" below). Peak memory well under the 3G ceiling (not hit).

Changes, each for a reason found by compiling or running:

| File | Change | Why |
|---|---|---|
| `Source/Editor/CMakeLists.txt` (new) | static `Editor`, every `Src/*.cpp` (= Editor.vcproj), `GPackage=GEditorPackage`, `UC2_STATIC_CLASS_ACCESSORS=1` | Editor was not built |
| `Editor/Inc/Perlin.h` | `register i, j` -> `register INT i, j` (2x) | implicit int (same fix as UT2004's tree) |
| `Editor/Inc/Editor.h` | `struct FObjectExport;` forward declaration | MSVC made UObject's `friend struct FObjectExport` visible; ISO C++ does not |
| `Editor/Src/UnVisi.cpp` | `&FEditorVisibility::AddPortal` etc. (3x) | pointer-to-member syntax (UT2004's tree has the same fix) |
| `Editor/Src/UAnalyzeMeshesCommandlet.cpp` | `#undef MAXFLOAT` | glibc macro clashes with a local constant |
| `Editor/Src/TTFontImport.cpp` | GDI code under `UC2_TTF_GDI` (= `_MSC_VER`), `FW_NORMAL` 400 elsewhere | TrueType import rasterises through Windows GDI; elsewhere the factory reports an error |
| `Editor/Src/UnParams.cpp` | the 5 parsers that Engine/Src/UnParams.cpp also defines are `#if !__STATIC_LINK` | each DLL had a copy; one static program cannot (code identical but for explicit `(INT)` casts) |
| `Core/Inc/UnObjBas.h` | static `IMPLEMENT_CLASS` emits `extern "C" UC2StaticClass<T>()` when `UC2_STATIC_CLASS_ACCESSORS` (Editor only) | most Editor classes are declared inside .cpp files, so the generated `AUTO_INITIALIZE_REGISTRANTS_EDITOR` cannot be compiled outside them |
| `Editor/Src/UC2EditorRegistrants.cpp` (new) | `UC2InitializeEditorRegistrants(Lookup)`: the 82 classes and 2 native tables of that macro, through the accessors | registers Editor's classes in `ucc-bin` (`UmakeCommandlet` -> `UMakeCommandlet`, the generated name's case artifact) |
| `UCC/CMakeLists.txt`, `UCC/Src/UCC.cpp` | link Editor when `TARGET Editor`; call the registrants | `ucc-bin make` and the editor commandlets |
| `Core/Src/UnClass.cpp` | native/static `UStruct` constructors initialise `StructFlags` and `ScriptTextCRC` to 0 | **real bug of our static link**: DLL builds construct native classes in zero-initialised static storage; on the heap `ScriptTextCRC` was garbage, and `GetScriptTextCRC` takes any non-zero value as the cached CRC, so every dependency record on a native class carried garbage (found by comparing Core.u) |
| `Editor/Src/UnEdSrv.cpp` | object names deduced from `#exec` file names also split at `\` (3 sites) | `PATH_SEPARATOR` is `/` here; `Texture Import File=Textures\WhiteSquareTexture.dds` became an object called `Textures\WhiteSquareTexture` and `Canvas.uc` failed |
| `Editor/Src/UMakeCommandlet.cpp` | `UC2SortLikeNTFS(Files)`: `.uc` files sorted as NTFS lists them (upper-cased code units) | import order = class creation order = the compiler's class-walk order; readdir() order is arbitrary |
| `Editor/Src/UnScrCom.cpp` + `UC2EpicClassOrder.h` (generated by `tools/dev/epic-class-order.py`) | `MakeScripts` sorts its class list by (epoch, native-before-script, Epic's registration rank or object index); `-NOEPICORDER` turns it off | **the scripts only compile in Epic's class order**: `UCWeapon.uc` declares `var UCPawn.EWeaponAnimType`, so the Pawn subtree must be parsed before the Inventory subtree (Weapon). Epic's DLL build registered each DLL's classes in reverse static-init order (LIFO registrant list; link order = .vcproj order, UnActor.cpp before UnPawn.cpp), our static build registers them alphabetically. A plain reversal fails elsewhere (`PlayerInput.uc` needs `Actor.eDoubleClickDir`). With the emulated order all 17 packages compile. |

No script source was changed.

First look at Core.u (tools/dev/upkg151.py): same 512 names, same 17 imports, same 1011 exports in the same order
(conform), same package GUID (conform copies it); 1003/1011 exports byte-identical; the 8 others are the 4
`ScriptText` text buffers (shipped: a single space) and the 4 classes, differing only in (a) the class flag
`CLASS_FullDefaults` 0x08000000 (shipped set) and (b) before the UnClass.cpp fix, the dependency CRCs. The shipped
CRC of Object (0xCD10E213) equals appStrCrc of **our** preprocessed `Object` script text: Epic compiled exactly
this text.

**The shipped packages went through `ucc editor.stripsource` after `make`** (the line is commented out in
`sync_avaris.pl`, but the shipped files carry its marks): `UStripSourceCommandlet` loads every `*.u` except
Editor.u (loading sets `CLASS_FullDefaults` in `UClass::Serialize`), replaces each class's ScriptText and CppText
with `" "`, and saves without conforming (new GUID, one generation). Editor.u kept its text (skipped) and the
shipped Editor.u indeed still contains script text.

Open after step 1:
- crash at exit after the last package is saved: `UEditorEngine::Destroy -> UEngine::Destroy ->
  UObject::ExitProperties -> UArrayProperty/UStrProperty::DestroyValue -> ~FString` (teardown only; all files are
  written before it).
- 101 warnings, mostly `Failed to load 'UCEffects'` while UCGameplay's defaults name UCEffects classes (UCEffects
  is compiled later); to be checked against the shipped defaults in step 2.

Next: the comparison tool (step 2).

## Step 2a (2026-10-07): the comparison tool, first results, and what the shipped files went through

`tools/dev/script-diff.py <shipped System> <recompiled System> --patches Source/Engine/Inc/ScriptPatches.h
--details <file>`: parses every export with the v151 serialisers (UObject stack + tagged properties, UField,
UStruct with its script, UFunction, UState, UClass with dependencies and tagged defaults, every UProperty kind,
UConst, UEnum, UTextBuffer; bytecode token by token like `UStruct::SerializeExpr`), requires each parse to consume
the export exactly, and compares field by field (references resolved to object paths). One line per package.

More fixes found by comparing:
- `Core/Src/UnObj.cpp` `GetPackageLinker`: the package name deduced from a file name also splits at `\`. A
  script's `#exec obj load file=..\Textures\T_HUD.utx` (no `PACKAGE=`) named the package `""` (cut at the first
  `.` of `..`), i.e. a transient `PackageN`, and every T_HUD texture referenced from defaults was saved as an
  import from `Package15`; `stripsource` could then not load UCMutators.u. Real Linux bug (Windows splits at `\`
  because it is PATH_SEPARATOR there); it also affects the game wherever a package file name with backslashes
  is loaded.
- `Editor/Src/UStripSourceCommandlet.cpp`: files in NTFS order; `-CONFORM` (ours, opt-in) saves conforming to
  `../GUIRes/<Pkg>.u`.

What the shipped files went through (evidence in the comparison):
1. `make` with the Xbox inis (as sync_avaris.pl);
2. `editor.stripsource` with the **PC** inis (`Default.ini`/`DefUser.ini`): the shipped default of
   `UWeb.WebServer.DefaultApplication` is 0 (Default.ini) where the class says -1 and DefaultXbox.ini has no
   key; with the PC inis our strip reproduces it. (sync_avaris.pl deletes Avaris.ini/User.ini right after
   make, so a later strip run derives them from Default.ini.) Marks of the strip: text buffers `" "`,
   `CLASS_FullDefaults` set on load, `CPF_NeedCtorLink` on edit-inline object properties (set by
   `UObjectProperty::Link` on load, after `make`'s save had added `CPF_EditInline|CPF_ExportObject`).
3. **except UCMenu.u**, which carries none of these marks: all 171 text buffers hold the full text (so the
   shipped UCMenu.u contains Epic's retail UCMenu source!), and it matches our unstripped make output. So
   `script-build.sh strip` keeps UCMenu.u (and Editor.u, skipped by the commandlet) unstripped.

Result after make + strip (PC inis): Core, UCAnnouncements, UnrealEd: every export byte-identical. The rest:
explainable categories (positions and dependency CRCs in classes whose source changed after retail, import
time stamps and lazy-array file offsets in textures, FName case `l`/`L`, the patch's own appended objects in
another order) plus real findings, being worked through in step 2b:
- `Dir_PlayerEnableControls.DirectionStart`: shipped `IntConst -1`, ours `IntConst 2147483647` (twice): a literal
  the C runtime parses differently (to look at).
- `GUIController.bUnlockCharacters` and `bShowAllMuts` are `config` in the shipped GUI.u, not in the source
  (a post-retail source change no patch can carry: the runesofwar "bUnlockCharacters" ini tweak works with the
  shipped scripts and would not with recompiled ones).
- **Epic's patch for `UCGameplay.CTFFlag.home` is defective**: its label table names `WeaponAbility` (name index
  100 of the shipped package) where the source and the recompiled state say `Begin` (index 122), and the
  patcher cannot update the state's `LabelTableOffset` (16 in the shipped state, 48 in the patched script). The
  recompiled state is consistent.
- `ConnectionFailureMenu` gains a `Timer` event by patch; the patch cannot set the class's `ProbeMask`
  (0 shipped, 0x100 recompiled).
