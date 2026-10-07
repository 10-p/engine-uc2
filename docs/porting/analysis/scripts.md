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

## Step 2b (2026-10-07): the reproduction, and the proof

Commands:

```
tools/dev/script-build.sh            # assemble, make, strip, compare (tree <WorkUC2>/run/m14-make)
python3 tools/dev/script-diff.py <WorkUC2>/assets/uc2/System <WorkUC2>/run/m14-make/System \
    --patches Source/Engine/Inc/ScriptPatches.h --details <file> --patch-table <file>
```

What the reproduction needed beyond step 2a (each found by a difference, each now matching):

| Difference seen | Cause | Change |
|---|---|---|
| `Dir_PlayerEnableControls.DirectionStart`: `IntConst -1` shipped, `2147483647` ours | `const INPUT_All = 4294967295;` goes through `appAtoi` = `atoi`; MSVC's wraps modulo 2^32, glibc's saturates (out of range is undefined in C) | `Core/Src/UnAnsi.cpp` `appAtoi`: MSVC's algorithm off MSVC. Engine-wide (ini values out of range now wrap as on Windows/Xbox) |
| `GameEngine.MainMenuClass` = `UCMenu.StartMenu` ours, `UCMenu.UC2MainMenu` (the class's own) shipped | `UMakeCommandlet` loads the ini into the defaults of a package it conforms; Epic's retail make had nothing to conform to (no DefaultXbox.ini value in any shipped default) | `-CONFORMNOCONFIG` (ours): conform for order/GUID only. **Runtime-relevant**: our Default.ini has no MainMenuClass, so without this the recompiled game would open StartMenu instead of UC2MainMenu |
| `UCScreenBlur` Alpha/Enabled/Reset 0, `CharacterMemoryUsageCommandlet` 6 flags False, `ProfilePreview` 46 values | a native class without its own defaultproperties gets its defaults exported from its in-memory defaults (`MakeScripts`); ours were start-up zeros, Epic's came from the superclass when the package's DLL loaded | `UC2RefreshNativeDefaultsLikeDllLoad` (UnScrCom.cpp), called by UMakeCommandlet when it creates IpDrv/UWeb/UnrealGame/UCElements/UCGameplay/GUI: `InitClassDefaultObject` again, superclasses first. **Runtime-relevant** (UCScreenBlur.Alpha would have been 0) |
| `UCDeathmatch.LevelData`, character speeds | the strip loads config again; Epic's retail-era Gametypes.ini/Characters.ini (no Overdrive maps) | strip stage copies those (and Maplists.ini) from `<WorkUC2>/reference/iso-extract/System` (the retail disc) |

Result (`tools/dev/script-build.sh`, 2026-10-07):

```
Core             identical           exports  1011, byte-identical  1011, neutral    0, findings  0, problems  0
Engine           different-explained exports 12640, byte-identical 12125, neutral  933, findings  1, problems  0
UCAnnouncements  identical           exports   215, byte-identical   215, neutral    0, findings  0, problems  0
Editor           different-explained exports   446, byte-identical   438, neutral   15, findings  2, problems  0
UnrealEd         identical           exports   127, byte-identical   127, neutral    0, findings  0, problems  0
IpDrv            equivalent          exports   526, byte-identical   525, neutral    1, findings  0, problems  0
UWeb             identical           exports   153, byte-identical   153, neutral    0, findings  0, problems  0
GamePlay         equivalent          exports   974, byte-identical   962, neutral   16, findings  0, problems  0
UnrealGame       equivalent          exports   882, byte-identical   841, neutral   73, findings  0, problems  0
UCElements       equivalent          exports  2956, byte-identical  2955, neutral    1, findings  0, problems  0
UCGameplay       different-explained exports 10842, byte-identical 10559, neutral  458, findings  5, problems  0
UCEffects        equivalent          exports  3535, byte-identical  3498, neutral   37, findings  0, problems  0
UCCharacters     equivalent          exports  2704, byte-identical  2646, neutral   58, findings  0, problems  0
UCInventory      equivalent          exports  1924, byte-identical  1863, neutral   88, findings  0, problems  0
UCMutators       equivalent          exports   579, byte-identical   557, neutral   29, findings  0, problems  0
GUI              different-explained exports  3526, byte-identical  3361, neutral  235, findings  2, problems  0
UCMenu           different-explained exports 10100, byte-identical  9940, neutral  210, findings  1, problems  0
```

0 unexplained differences. "neutral" = explained, no effect on what runs, each recognised by a rule (the rule list
is at the end of the details file): stripsource marks, Line/TextPos and dependency CRCs/lists of the 17 classes
whose script text changed after retail (the 16 classes the patches touch, plus `Engine.PSDistortionBaseMaterial`,
whose `cpptext` block M7 lengthened by two lines: cpptext lines stay in ScriptText as `// (cpptext)` markers) (own-text CRC differs; the shipped CRC of every other class equals ours,
i.e. Epic compiled exactly our text), import time stamps and lazy-array file offsets of #exec textures (pixel
data compared mip by mip: identical), MipZero (unused colour average) off by one in 6 Editor textures, FName case,
patch-appended objects in another order, names the patch appends that only Epic's later ini-loaded defaults
used (`CacheSizeMegs`, `bPauseable`; no patch bytecode refers to them).

"findings" = explained, but what runs differs from shipped + patches (11):
1. `UCGameplay.CTFFlag.home`: **Epic's patch is defective** (both the label name, index 100 = `WeaponAbility`
   instead of `Begin`, and the `LabelTableOffset` 16 the patcher cannot update): with the patch, a flag returning
   home never runs its `Begin:` code (`Sleep(0.1); CheckTouching()`), the label search walks the new script from
   offset 16 and ends at a `None` entry without finding `Begin`. Recompiled: runs as the source says. Gameplay
   difference: an enemy standing on the base when the flag returns gets it after 0.1 s (recompiled) or only on
   the next touch (shipped + patch, also the Xbox title update by inference).
2. `GUIController.bUnlockCharacters`, `bShowAllMuts`: `config` in the shipped GUI.u, not in the source (no patch
   can change flags). The community tweak `bUnlockCharacters=True` in an ini works only with the shipped scripts.
3. `ConnectionFailureMenu.ProbeMask` 0 -> 0x100 (the patch adds a `Timer` event but cannot set the probe bit).
   Recompiled: the menu's Timer can be probed as the source intends.
4. `UCWeap_Melee.BusyShieldOn.HitWall`: shipped `FUNC_Defined` with the patch's empty body; recompiled not defined
   (the newer source comments the body out; an empty body is not "defined"), and the state's ProbeMask loses
   the HitWall bit. Same behaviour: the VM skips undefined functions (`UnCorSc.cpp` 5272, 5444).
5. `OverflowBall` defaults: `bUpdateSimulatedPosition=True` (the source sets it in defaultproperties; the patch
   sets it in `PostBeginPlay` code instead, so both end up True after spawn).
6. `Engine.NetworkDevice` default `IpDrv.TcpNetDriver` (ours) vs none (shipped): a config property; the strip's
   config load resolves the bare class name with `StaticFindObject` only, which finds the class in our static
   build (registered at start-up) and did not in Epic's (IpDrv.dll not loaded yet). At run time the ini key
   (`[Engine.Engine] NetworkDevice=`) replaces it.
7. `Editor.TexPropSphere`/`TexPropCube`: 1133 and 1 bytes of float low bits differ (`#exec NEW StaticMesh` .ase
   import math); editor-only meshes, never loaded by the game.

The 30 patches (`<WorkUC2>/logs/m14/m14-make-patches.txt`): 21 bytecode patches + 9 appended objects. For
29 of 30 the recompiled package contains exactly what the patch carries (bytecode token for token with references
resolved through the patched index space; appended locals/functions field for field) - i.e. the source in our
tree already contains each change, and the patch existed because the shipped .u was older than the source. The
30th (`CTFFlag.home`) differs because the patch itself is wrong (finding 1). Table in step 2c below / the report.

Script bugs seen while proving (recorded, not touched): see the findings above (the CTFFlag patch is Epic's
tooling bug; the scripts are fine). `GUIController.uc` warns "'Empty' : local variable used before assigned a
value" (GUIController.uc 3365, 3387), `ClientBeaconReceiver.uc(19)` 'zeroBeacon' and `UCWeap_SniperRifle.uc(604)`
'OldDist' used before assigned, plus unused locals (StartMenu.uc 521-524, UCWeapAttch_Melee.uc 9-10): compiler
warnings, recorded for later.

## Step 2c (2026-10-07): the C++ header export, compared (never written over Source/)

```
tools/dev/script-build.sh --tree m14-hdr --headers assemble make
    # make with -ForceHeads instead of -noh; the exporter writes <WorkUC2>/run/m14-hdr/<Pkg>/Inc/<Pkg>Classes.h,
    # copies of Source/<Pkg>/Inc; the diff against Source/ goes to <WorkUC2>/logs/m14/m14-hdr-headers.diff
```

Also fixed on the way (a crash at the end of every `make`, after all packages were saved): `Editor.h`'s
`UEditorEngine` put `BYTE AutosaveTimeMinutes` right after a bitfield run; GCC places it inside the bitfield's
word, MSVC and the script linker start a new DWORD, so `GameCommandLine` and `EditPackages` were 4 bytes off
and `ExitProperties` destroyed a garbage array. `GCC_PACK(4)` added (the M3 rule), the 3 untagged first members
of the generated `EditorClasses.h` tagged with `tools/dev/fix-bitfield-pack.py`, and `ucc-bin verifyclasses`
now also checks the Editor classes (generated sizes plus every UEditorEngine member by script and C++ name;
`Editor/Src/UC2EditorRegistrants.cpp`). Editor is built for 32-bit Linux only (root CMakeLists, M14 block):
its UEditorEngine mirrors script's `int Pad[8]` with four pointers.

Result: UCElements, UCGameplay, UnrealGame, GameSpy (IpDrv): identical. The differences, all explained:

| Header | Lines | What | Why |
|---|---|---|---|
| Engine, GUI, IpDrv | 16 + 2 + 8 | `PTRINT` ours, `INT` exported | our M13 64-bit placeholders (deliberate deviation, `analysis/64bit.md`): a regenerated header would lose them, so headers are compared, never regenerated |
| EngineClasses.h | 3 | comment lines in `UPSDistortionBaseMaterial` | the exporter writes cpptext with comments stripped (the preprocessor runs on cpptext too); M7 put the comment in the header and the .uc |
| EngineClasses.h, EditorClasses.h, IpDrvClasses.h | 4 | `UHTTPDownload` and `UPrefab` registrants listed under Engine instead of IpDrv/Editor | both say `DECLARE_CLASS(...,Engine)` but are implemented in the IpDrv/Editor DLLs: Epic's DLL build registered them in the DLL's package, our static build in `Engine`. **Runtime finding (pre-existing, not M14's)**: Default.ini names `DownloadManagers=IpDrv.HTTPDownload`, which our static build registers as `Engine.HTTPDownload` (package download has never been exercised) |
| EditorClasses.h | 2 | `UmakeCommandlet` vs `UMakeCommandlet` | FName case (step 0) |
| UnrealEdClasses.h | 8 | `GCC_PACK(4)` on first members | the exporter rule restored at M3; the checked-in UnrealEdClasses.h predates it (UnrealEd's C++ is not built) |

## Step 2 wrap-up for S1's other questions

- **UnrealEd**: the shipped UnrealEd.u is in the asset tree; `make` compiles UnrealEd's 10 classes from script
  only (their C++ is the editor application, not built: `UClass::Bind` tolerates a native class without C++
  under `GIsEditor`), and the result is byte-identical to the shipped UnrealEd.u (after the strip).
- **Case** of `#exec` paths and `` `include `` paths (`..\Core\Classes\avaris.huc`, `GamePlay` vs `Gameplay`):
  resolved by the file manager's case-insensitive lookup (`__FILE_NOCASE`, `appUnixPath`); backslashes needed the
  two fixes in step 1/2a (object names from `#exec` file names, package names from file names).
- **0x02 bytes** (button glyph escapes in 37 .uc strings, `GUI.uc`'s `const XboxButtonCode="\x02"`): pass
  through the comment stripper and the macro processor unchanged; the UCMenu/GUI defaults and constants that
  contain them are byte-identical to the shipped ones.
- **Preprocessor symbols**: neither `debug` nor `FINAL_RELEASE` (step 0); confirmed by the bytecode: `` `log ``
  calls compile to `log(...)` calls identical to the shipped ones, `` `logd `` to nothing.

## Step 3: the game on the rebuilt packages (S3)

`tools/dev/m14-scripts-check.sh` (one PASS/FAIL line per check, summary `<WorkUC2>/logs/m14/check/m14-summary.txt`).
Trees: `m14-run` (rebuilt packages via `UC2_SCRIPTS`), `m14-ship` (same binary, shipped packages) for the
side-by-side checks. The patcher switch (`Engine/Src/UnScriptPatcher.cpp`): a patch is applied only to the
package it was made for, the first-generation file with the patch's `NameCount`/`ExportCount`; a rebuilt
package (a later, conformed generation) is skipped with
`ScriptPatcher: <Pkg> is not the package the patch was made for (...): rebuilt from source, patch not applied`;
applied patches log `ScriptPatcher: <Pkg> patched (N bytecode patches, M new objects)`. The patch table is
untouched.

Results are recorded below as they come in (step 3 log).

Repeatability: two consecutive `script-build.sh` runs give byte-identical packages except the `InternalTime`
property of the 78 `#exec`-imported textures (Engine 68, Editor 6, GamePlay 4): the import time stamp
(`appSeconds()`), by design. Everything else is deterministic (file order, class order, conform).

## How to change a script (for the D-phase tasks)

1. Edit `Source/<Pkg>/Classes/<Class>.uc`. The files are Latin-1 with LF endings, 37 of them contain 0x02
   bytes (button glyph escapes inside strings): edit bytes, never re-encode (`python3` on bytes, or an editor
   set to Latin-1). Never edit `epic-sources/`.
2. Build: `tools/dev/script-build.sh` (assemble + make + strip + compare, ~1 min) or only
   `tools/dev/script-build.sh assemble make strip`. The make log is `<WorkUC2>/logs/m14/m14-make-make.log`
   (compiler errors as `<file>(<line>) : Error, ...`; the last line must be `Success - 0 error(s), ...`).
3. Compare: the summary lines show which packages changed. Before any change every package is
   identical/equivalent/different-explained with **problems 0**; after a change the edited package (and only
   what really changed) shows up as problems, with a bytecode listing diff per function in
   `m14-make-compare.txt`. A change to a class adds it to the "changed classes" list at the top, and other
   classes' dependency records follow (neutral). Packages compiled after it change only if they use what you
   changed (a constant, a default, a function signature).
4. Native classes: adding or moving a `var` in a class declared `native` changes its C++ layout. Run
   `tools/dev/script-build.sh --tree m14-hdr --headers assemble make` and port the relevant hunk of
   `<WorkUC2>/logs/m14/m14-hdr-headers.diff` into `Source/<Pkg>/Inc/<Pkg>Classes.h` by hand (keep our
   `PTRINT`, `GCC_PACK` and comment deviations; never copy the exported file over ours), rebuild the C++, and
   `ucc-bin verifyclasses` must pass (32- and 64-bit: `tools/dev/m13-64bit.sh verify`).
5. Run: `UC2_SCRIPTS=<WorkUC2>/run/m14-make/System tools/dev/mkrun-uc2.sh build-native <tree>`, then the
   suites with that tree (`tools/dev/m14-scripts-check.sh` runs them all). The rebuilt packages no longer get the
   load-time patches (they do not need them: step 2b). A tree mixing rebuilt and shipped packages is fine: each
   shipped package is still patched.
6. Network: a rebuilt package keeps the shipped GUID and adds a generation (conformed save), so peers with the
   same rebuilt files connect; mixing rebuilt and shipped peers is not a goal.
7. Web: the web build and the asset tree keep the shipped `.u` until the coordinator and the owner decide.

Things a D-phase change must know (from the proof): `GUIController.bUnlockCharacters`/`bShowAllMuts` are not
`config` in the source (the shipped files have them config); `CTFFlag.home` runs its `Begin:` code again with
rebuilt packages (Epic's patch for it was broken); `ConnectionFailureMenu` can probe `Timer` again;
`GameEngine.MainMenuClass` defaults to `UCMenu.UC2MainMenu` (an ini can still choose `StartMenu`).

## Checkpoint after the usage-limit cut-off (2026-10-07 07:55)

Cut off at about 01:50; nothing of this milestone kept running. State of each gate item at that moment:

Observed (logs kept):
- (a) native: `bin/capped 6G 3000 -- cmake --build build-native -j4` -> rc 0, `ucc-bin` linked with Editor
  (`logs/m14/build-native-14.log`). wasm-perf: `source env.sh && bin/capped 4G 1200 -- cmake --preset wasm-perf`
  rc 0, `bin/capped 6G 5400 -- cmake --build build-wasm-perf -j4` rc 0, 0 error lines, `[100%] Built target
  SDLLaunch` (`logs/m14/build-wasm-perf.log`, finished 01:55, the background job completed normally).
- (b) script-build summary and (c) the 30-patch table: as in step 2b (`logs/m14/m14-make-compare.txt`,
  `m14-make-patches.txt`, `m14-make-functions.txt`).
- (e) header-export diff: step 2c (`logs/m14/m14-hdr-headers.diff`).
- (d) partly: `verifyclasses` on m14-run and m14-ship (both pass), menu and DM-Deadbolt on both trees (0
  Critical, 0 GL errors, ScriptPatcher lines as designed), `m10-controls.sh` with `M10_TREE=m14-run`:
  keyboard 16, mouse 18, text 7, pad 33, both 14 checks, all PASS (`logs/m14/m10-run.txt`, run outside the
  check script).
- per-function file: `script-diff.py --functions` (committed now): 10456 functions/states, 9621 byte-identical,
  813 only Line/TextPos in the 17 changed classes, 19 patched (bytecode = ScriptPatches.h), 3 with findings
  (CTFFlag.home, UCWeap_Melee.BusyShieldOn and its HitWall).

Not run yet (so nothing is claimed for them): audio, effects, census, movies, net, leftovers on the rebuilt
packages, and the complete `tools/dev/m14-scripts-check.sh` run. Next: run the check script end to end.

## Step 3 results: `tools/dev/m14-scripts-check.sh` on the rebuilt packages (2026-10-07 07:52-08:51)

Full run (commit a5b874b's script, trees m14-run with the rebuilt packages from `run/m14-make/System`,
m14-ship with the shipped ones, same binary; summary kept as `logs/m14/check/m14-summary-full-run.txt`):

```
PASS verify: exit 0, 'VERIFY: all native class layouts match', 'VERIFY: Editor class layouts match' (rebuilt packages)
PASS menu: m14-run: menu up, 0 Critical, 0 GL errors; m14-ship: menu up, 0 Critical, 0 GL errors
PASS deadbolt: m14-run: DM-Deadbolt up, Fire pressed at 28 s, 0 Critical, 0 GL errors; m14-ship: the same
PASS patcher: rebuilt tree: not applied to Editor Engine GUI UCGameplay UCInventory UCMenu UnrealGame (none patched);
     shipped tree: patched Editor Engine GUI UCGameplay UCInventory UCMenu UnrealGame
PASS controls: keyboard 16, mouse 18, text 7, pad 33, both 14 checks, 0 Critical each
PASS audio: UC2AudioSelfTest 39 checks, 0 failed; menu music NuBattle at -19.02 dB (expected -19.00); captures
     menu/deadbolt/praxis-efx/praxis-dry: 0 silent s, 0 failed cues, 0 Critical (UC2AudioCheck 0 failures)
FAIL effects: (the check's own filter counted the 11 "fx selftest" lines as pairs; the data: on both trees
     selftest 10 passed 0 failed, bloom/blur/berserk/decay critical=0 glerrors=0 programs_failed=0+0)
PASS census: 58 maps, 58 PASS with the rebuilt packages, the same per-map result as the shipped ones (58 PASS);
     hung servers killed after exit: 0
PASS movies: 19 PASS, 0 FAIL (m9-movies.py on tree m14-movies with the rebuilt packages)
PASS net: 16 PASS, 0 FAIL (NET_TREE=m14-net, dedicated2 + travel, rebuilt packages), port 7860 free after
PASS leftovers: no uc2.bin/ucc-bin/Xvfb running in an m14 tree
```

My mistake during that run: I edited the running script to fix the effects filter, then restored it with
`git checkout`, which replaced the file while bash still read the old (edited) inode; after its last check bash
resumed at a shifted offset ("line 212: s: command not found") and started the checks again. I stopped that
accidental second pass by PID (it had redone verify, menu, deadbolt, patcher, controls, all PASS again) and
checked that no m14 process was left. The effects check, fixed (commit after ea59500), is being rerun on its own.
Lesson for whoever runs these suites: never edit a running bash script.

UCMutators is in no patcher line: nothing in S3 loads it (the census servers, 58 maps, report the other seven
packages: Engine/Editor/UnrealGame/UCGameplay/UCInventory/GUI on 57 maps each, UCMenu on 1; never UCMutators).
So the UCMutators patches (WeaponMaster `ScoreKill`, the two `Reset` functions) and their rebuilt equivalents
are proved equal only by the bytecode comparison, never exercised in a game here.

Effects rerun alone with the fixed check (`M14_NO_MKRUN=1 tools/dev/m14-scripts-check.sh effects leftovers`):

```
PASS effects: m14-run: 4 pairs critical=0 glerrors=0 programs_failed=0+0, selftest 10 passed, 0 failed;
     m14-ship: the same; B pictures rebuilt-vs-shipped mean difference: bloom 0.62, blur 4.43, berserk 1.33, decay 0.47
PASS leftovers: no uc2.bin/ucc-bin/Xvfb running in an m14 tree
```

The blur pair (the one that depends on UCScreenBlur's defaults, which the DLL-load emulation fixed) looked at
side by side: the same concussion blur with ghost copies in both trees; its displacement is time-driven and the
wall screen plays a movie, hence the larger mean (seen only, not numerically compared beyond the mean).

All S3 items observed: 11 PASS lines (verify, menu, deadbolt, patcher, controls, audio, census, movies, net,
leftovers from the full run; effects from the rerun).
