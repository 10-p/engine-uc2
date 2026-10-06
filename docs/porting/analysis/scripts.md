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
