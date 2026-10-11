# T4 notebook: every UnrealScript source leaves the repository

Task T4 of the UC2 port. The owner (2026-10-11): "Unreal Script is asset material, content, we should not push it to a
repo... Once all of this is done, we remove all the unreal script from this repo and stick to just the patches."
The pipeline that replaces the sources (T1 `script-patches.md`, T2 `headers.md`, T3 `script-build-rules.md`) is on
main (4cbe12f). Worktree `engine-uc2-t4-remove`, branch `t4-remove`. Logs `<WorkUC2>/logs/t4/`, run trees
`<WorkUC2>/run/t4*`. Every run through `<WorkUC2>/bin/capped` (builds 6G -j4, ucc 3G, Python 2G, games 2G).

After this task the coordinator rewrites the history (git-filter-repo removes the same paths from every commit);
this task is the working tree at the tip and everything that referred to the files.

## Step 1 (2026-10-11): inventory, before deleting

**What leaves** (`git ls-files` at 4cbe12f): 2233 files under `Source/<Pkg>/Classes/` (2223 `.uc`, 5 `.huc`, 4
`.upkg`, 1 `STY_*.txt`; 18 packages: Core 5, Editor 17, Engine 263, Gameplay 102, GUI 95, IpDrv 14, Setup 5,
UCAnnouncements 104, UCCharacters 271, UCEffects 672, UCElements 43, UCGameplay 233, UCInventory 137, UCMenu 174,
UCMutators 60, UnrealEd 10, UnrealGame 20, UWeb 8) and the 7 `Source/<Pkg>/<Pkg>.ucproject` (Core, Editor, Engine,
Gameplay, IpDrv, UWeb, UnrealEd). 2240 paths. **What stays**: `patches/**` (with our two new classes
`patches/keys-menu/new/UCMenu/Classes/{KeyBindingPanel,UC2Settings_Keys}.uc`), the generated
`Source/<Pkg>/Inc/<Pkg>Classes.h`, everything else.

A copy of the 2240 files as they were at 4cbe12f (Epic's leaked text with our edits on top, the input of the T1
reference build `run/t1-ref`) is kept on the dev machine only: `<WorkUC2>/run/t4-sources/source-classes-4cbe12f.tar.gz`
(1,875,675 bytes, sha256 c2e92a92...; `files.txt` lists the paths). Not a build input; it exists so the history
rewrite loses nothing that cannot be looked at again. Epic's own text stays at `<WorkUC2>/epic-sources/<Pkg>/Classes`.

**What refers to them** (`git grep -a -n -E 'Classes/|\.huc|\.upkg|ucproject'` outside the leaving files: 62 files;
plus `git grep -a 'Classes\|script-build'` over cmake/, CMakeLists.txt, tools/, the top-level docs):

| Reference | Kind | What is done |
|---|---|---|
| `tools/dev/script-build.sh` `classes_root=$repo/Source` (line 52), header lines 22-31 | tool default | default becomes `$UC2_EPIC_SOURCES` (`<WorkUC2>/epic-sources`) when it has `Core/Classes`, else `--classes-root` is required; header: a dev-only proof tool (compiles Epic's leaked sources), never a build input |
| `tools/dev/check-headers.sh` `classes_root=$repo/Source` (line 34), header 15-28, the hint at line 178; `regen-headers.sh` header | tool default | default becomes the build's patched tree `<build>/Scripts/tree`; `Source/` stays only as the destination of the headers; `--write` (regen-headers) learns the patched tree (it exited before writing there) |
| `CMakeLists.txt` 454-479 (`UC2_HEADERS_CLASSES_ROOT`, the `Source/` fallback when the patches are OFF) | build rule | no `Source/` fallback: with the patches OFF and no root given, `check-headers` says what to pass and fails |
| `tools/dev/uc-patches-regen.sh` (T1's set regenerator: chained trees from `run/t1-ref`, the new classes copied from `Source/UCMenu/Classes`, lines 21, 79) | tool input gone | rewritten: the helper of the new way to change a set (trees before/after the set from the build's `Scripts/base`, edit, `uc-patches.py make`) |
| `tools/scripts/uc-patches-proof.sh` P2 (reference = `run/t1-ref/System`, a build of `Source/`) | proof input gone | P2 becomes the regression reference `patches/reference.sha256` (sha256 + size of each package the default build makes); the old bytecode compare stays as `--ref <dir>` |
| `tools/dev/m14-scripts-check.sh` `M14_SCRIPTS` default `run/m14-make/System` (a script-build.sh tree of `Source/`) | tool default | default becomes `<build>/Scripts/System` (the build's packages) |
| `tools/dev/import-epic-sources.py` (imports `<Pkg>/Classes` and `.ucproject`) | historical tool | skips `Classes/` folders and `.ucproject` files from now on, so it describes the rewritten first commit |
| `tools/scripts/uc-build.sh`, `uc-patches.py`, `uc-patches-proof.sh` `<Pkg>/Classes` | tree layout of the build (`Scripts/{base,tree}/<Pkg>/Classes`), not `Source/` | none |
| `cmake/UC2Scripts.cmake` 9-10, 269 | same (`Scripts/base/<Pkg>/Classes`) | none |
| `patches/**/*.diff`, `patches/keys-menu/new/UCMenu/Classes/*.uc` | the sets' own paths (`<Pkg>/Classes/<Class>.uc`) | none |
| `Source/Editor/Src/FTextFilter.cpp:294`, `UMakeCommandlet.cpp:76` (`../<Pkg>/Classes/<Pkg>.upkg`), `Source/UnrealEd/Src/Main.cpp:4090`, `ParticleEditor.cpp:830` | Epic's C++ reading script text at run time of `ucc make` / the editor, relative to a run tree (`uc-build.sh` links `<Pkg>/Classes` there) | none (code, not a build input; `.upkg` is optional to `make`: T1/T3 build without one) |
| `Source/Engine/Src/UnCanvas.cpp:1017` (`WeaponSelectorSplit.huc`), `Source/WebRTCDrv/Src/WebRTCNetDriver.cpp:16` (`Engine/Classes/PlayerProfile.uc:764`) | comments citing Epic's script | none (citations; the file is in `epic-sources`) |
| C++ `#include` of a leaving file | | none exists (`git grep -a -E '#\s*include\s*[<"][^>"]*(\.(uc|huc|upkg)|Classes[/\\])'`: nothing) |
| 18 `.vcproj` (`RelativePath="..\Classes\*.uc"`, Epic's Visual Studio projects) | Epic's project files, not built (CMake builds) | none: kept byte for byte; their script items now point at nothing, as the IDE projects of a tree without sources would |
| `BUILD.md` 168-213, 263 ("rebuilds all 17 from `Source/<Pkg>/Classes`", `--classes-root` default `Source/`, "while it still holds the `.uc`") | doc | rewritten (step 5) |
| `CLAUDE.md` 13, 32-33 | doc | sources not in the repo; how to change a script; `grep -a`/0x02 for `patches/` and `Scripts/tree` |
| `README.md` | doc | one line: no script in the repo |
| `patches/README.md` "Changing a set" (`uc-patches-regen.sh` "from a build of our sources", P2 "our reference build") | doc | the new workflow, step by step |
| `docs/SOURCE-LAYOUT.md` (import table, line 46 "compiled by `ucc make`") | doc | a note: the scripts left on 2026-10-11 |
| `docs/porting/analysis/scripts.md` | historical notebook (M14) | a banner at the top |
| `docs/porting/analysis/{audio-f6,controls,decompile-spike,engine-port,exploration-findings,headers,jitter,mouse,platform-f4,postprocess,pp-scale,profiles,rebind,saveconfig,script-patches,splitscreen,ucc-windows,ui-scale,vanilla,video,web-ini-saves,webrtc}.md` | historical notebooks | left as written, with one line at the top |
| `docs/porting/REPORT.md` / `REPORT.html` (48 / 46 lines) | the coordinator's | not edited; the paragraph for section 60 is below |
| `.gitignore`, `.gitattributes` | | nothing referred to the scripts (`* -text` stays); `.gitignore` gains `/Source/*/Classes/` and `/Source/*/*.ucproject` so they cannot come back by accident (`patches/` is not matched) |
