Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

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

## Step 2 (2026-10-11): deleted

`git rm -r` of the 18 `Source/<Pkg>/Classes` folders and the 7 `.ucproject`: 2240 paths. Left: `git ls-files | grep
-a -c '\.uc$'` = 2 (the two new classes of `patches/keys-menu`); no `.huc`, `.upkg`, `.ucproject` tracked.
`.gitattributes` (`* -text`) referred to nothing script-specific and stays. `.gitignore` gains `/Source/*/Classes/` and
`/Source/*/*.ucproject` (`git check-ignore` matches `Source/UCMenu/Classes/X.uc`, not
`patches/keys-menu/new/UCMenu/Classes/KeyBindingPanel.uc`). Packages that held only scripts keep their Epic `.vcproj`
(`Source/UCMenu/UCMenu.vcproj` etc.).

## Step 3 (2026-10-11): the tools without `Source/` scripts

- `tools/dev/script-build.sh` (M14's builder of all 17 packages): header says DEV-ONLY PROOF TOOL, never a build input;
  `--classes-root` defaults to `$UC2_EPIC_SOURCES` (`<WorkUC2>/epic-sources`) when it has `Core/Classes`, else the
  tool stops ("no script sources: ... Pass --classes-root <dir>"). Everything else unchanged (resource folders already
  came from epic-sources; `<Pkg>/Inc` is still seeded from `Source/<Pkg>/Inc`, the headers' home).
- `tools/scripts/uc-patches-proof.sh` P2: our edits exist only as patches now, so P2's reference (a build of the
  repo's sources) cannot be rebuilt. P2 is a **regression reference**: `patches/reference.sha256` (comment header, then
  per package a `# <Pkg>.u <size> bytes` line and a `sha256sum` line, so `sha256sum -c` reads it: GNU sha256sum skips
  `#` lines silently, checked); P2 builds base + default sets and checks each package's sha256 and size and that the
  file lists exactly the default build's packages. `--write-reference` writes it from P2's build (the documented way
  to update it with a deliberate set change); `--reference <file>` checks another; `--ref <dir>` runs the old bytecode
  comparison as well (dev machine: `run/t1-ref/System`). `--ref` has no default any more: T1's reference matches the
  sets as they were at 4cbe12f, and running it by default would fail every deliberate set change.
- `tools/dev/uc-patches-regen.sh` rewritten: `edit <set>` makes `<build>/Scripts/edit-<set>/before` (base + every set
  before it in sets.txt) and `.../edit` (the same + the set) from the build's `Scripts/base`; `make <set>` runs
  `uc-patches.py make <set> --base before --changed edit --packages <the base's packages>` (so a set's diffs of
  packages the build does not decompile, e.g. epic-fixes' Engine.diff, are kept), then `uc-patches.py check --default`.
  It prints each command it runs; `patches/README.md` lists them. Why not edit a copy of `Scripts/tree` directly: it
  is base + ALL sets, and `make` against the tree before the set would fold the later sets into it.
- `tools/dev/check-headers.sh`: `--classes-root` defaults to `<build>/Scripts/tree` (an error naming the scripts target
  when absent); `Source/` appears only as where the headers are. `--write` (= `regen-headers.sh`) now works on a
  patched tree (before T4 it exited without writing there): `check-headers-cpptext.py --merge` writes the committed
  header with the export's changes and keeps its cpptext bodies and native struct blocks (which the shipped packages
  do not carry), and re-checks that the merged file compares equal. Synthetic test on `UCGameplayClasses.h` (export
  without its 34 native structs and 25 cpptext bodies, plus one `INT T4MergeTest` member): the merge differs from the
  committed header by exactly that line.
- `CMakeLists.txt` `check-headers`: no `Source/` fallback; with the patches OFF and no `UC2_HEADERS_CLASSES_ROOT` the
  target prints why and fails. A cached `<repo>/Source` (T2's old default) still counts as unset.
- `tools/dev/m14-scripts-check.sh`: `M14_SCRIPTS` defaults to `<repo>/<M14_BUILD>/Scripts/System`; the probe package
  that ties the trees together is `UCMenu.u` or the first `.u` there; the `patcher` check now accepts a partial
  rebuild (rebuilt packages "not applied", the tree's shipped ones patched; before it demanded no "patched" line at all,
  which only held for a full 17-package rebuild).
- `tools/dev/import-epic-sources.py`: skips `<Pkg>/Classes` and `.ucproject` (a record of how the rewritten first
  commit is made).

### Step 3 proofs (2026-10-11; build-native configured and built after the deletion: `logs/t4/build-native-1.log`)

- The scripts target, built in a fresh build directory of the tree without any `Source/*/Classes`: `Scripts/System`
  `GUI.u 370666 c796d108...`, `UCGameplay.u 1589219 7b8ade86...`, `UCMenu.u 2409720 1166328f...`, `cmp`-identical to T1's
  P2 build (`run/t1-proof/p2/System`) and T3's sha256.
- `patches/reference.sha256` written by `uc-patches-proof.sh p2 --write-reference --ref run/t1-ref/System`
  (`logs/t4/proof-p2-write.log`, 40 s): the three packages "as the reference", and the old bytecode comparison against
  T1's reference build still `differences 0` (UCGameplay 2474/2474, GUI 720/720, UCMenu 2360/2360 functions+states)
  -> `PASS P2: 3 packages equal to the reference (reference.sha256 and the build .../run/t1-ref/System)`.
  `sha256sum -c ../../../patches/reference.sha256` in `build-native/Scripts/System`: 3 OK.
  Negative (`logs/t4/proof-p2-negative.log`): a copy with one hex digit of UCGameplay's sha256 changed -> `UCGameplay.u:
  ... DIFFERENT (reference: 1589219 bytes, 7b8ade87...)`, `FAIL P2 (a set changed on purpose? regenerate: ...)`, rc 1.
- `tools/dev/script-build.sh --tree t4-epic` with no `--classes-root` (`logs/t4/script-build-epic.out`, 18 s): `Core/Classes
  -> /home/ion/WorkUC2/epic-sources/Core/Classes`; make `Success - 0 error(s), 85 warning(s)`; compare: all 17
  packages `problems 0` (Core, UCAnnouncements, UnrealEd, UWeb identical; Engine, Editor, UCGameplay, GUI, UCMenu
  different-explained; the rest equivalent), as M14. `UC2_EPIC_SOURCES=/nonexistent`: the refusal, rc 2.
- `tools/dev/check-headers.sh` with no `--classes-root` (`logs/t4/check-headers-default.log`): rc 0, `GUIClasses.h`,
  `UCGameplayClasses.h` current, "(scripts: .../build-native/Scripts/tree)". `regen-headers.sh` with no options: both
  "unchanged", `git status Source` clean.
- Patched-tree regen (`logs/t4/{check-headers-stale,regen-headers-stale,check-headers-after-regen}.log`): a scratch copy of
  `Scripts/tree` with `var int T4StaleCheck;` in UCPawn: check rc 1 `UCGameplay/Inc/UCGameplayClasses.h: STALE`, `+
  INT T4StaleCheck GCC_PACK(4);`; `regen-headers.sh --classes-root <scratch>`: `written (cpptext and native structs kept
  from the committed header)`, `git diff` = that one added line; check again rc 0 current; `git checkout -- Source`.
- **The new way to change a script, end to end** (`logs/t4/e2e-*`): `uc-patches-regen.sh edit mouse-look` (before =
  epic-fixes,epic-source; edit = + mouse-look); in `edit/UCGameplay/Classes/UCPlayerInput.uc` line 232
  `(Outer.aMouseX / 6.0)` -> `/ 6.5`; `uc-patches-regen.sh make mouse-look`: `diff UCPlayerController.uc (9 hunks, +56
  -4); UCPlayerInput.uc (7 hunks, +200 -8); UCTrainingController.uc (2 hunks, +24 -2)`, `check: 5 sets apply
  cleanly`; `git diff patches/mouse-look` = exactly that one line (`-+ ... / 6.0` `++ ... / 6.5`); `cmake --build
  build-native -j2`: apply + compile only (no decompile), UCGameplay.u sha256 3022926f..., UCMenu.u 093a6c7b..., GUI.u
  unchanged; `uc-patches.py compare` (with `--system-a/--system-b` = the work trees, which link the content folders;
  without them the comparison stops, "all 17 script packages must be in ...", and with a bare copy of the packages it
  cannot print defaults that name textures): UCGameplay `functions+states 2474 (2473 bytecode-identical) ...
  differences 1`, `ucplayerinput.playerinput: bytecode differs ... floatconst 6.5 / floatconst 6.0`; GUI and UCMenu
  `differences 0` (UCMenu's bytes moved only because its classes record the CRC of UCPlayerInput's text). Reverted
  with `git checkout -- patches/mouse-look` (not `patches/`: that would have dropped this task's README and
  reference), rebuilt: `sha256sum -c` of the reference 3 OK. README step 6 now gives the comparison with the
  `--system-*` options (found by this run).

## Step 4 (2026-10-11): the C++ side and the macro files

- No C++ file includes a file that left (`git grep -a -n -E '#\s*include\s*[<"][^>"]*(\.(uc|huc|upkg)|Classes[/\\])'
  -- Source Thirdparty`: nothing). The C++ that names `Classes/` reads script text at run time of `ucc make` or the
  editor, relative to a run tree (`FTextFilter.cpp:294`, `UMakeCommandlet.cpp:76` reading an optional
  `<Pkg>/Classes/<Pkg>.upkg`, `UnrealEd/Src/Main.cpp:4090`, `ParticleEditor.cpp:830`, the last two not built): code,
  unchanged. No `Source/*/CMakeLists.txt` names a script file.
- `.huc`: the build's trees have none and need none: `find build-native/Scripts/{base,tree} -name '*.huc' -o -name
  '*.upkg'` nothing; no `` `include ``/`` `define `` in `Scripts/tree` (`grep -a -r -l`: nothing; the decompiled base has
  the macros expanded, UCMenu's shipped text too, and T1 removed the one `` `include(GUI.huc) `` from our new classes);
  the `make-*.log` of the build mention no `.huc`. The scripts target built in a fresh build directory of the tree
  without `Source/*/Classes` (step 3) and again from scratch in step 6.

## Step 5 (2026-10-11): documents

- `BUILD.md` "Scripts (M14)": the repo holds no script sources; the commands are the build, check-headers,
  regen-headers, m14-scripts-check; headers default to `Scripts/tree` and `regen-headers` keeps cpptext/native structs;
  `script-build.sh` is a dev-machine-only proof tool reading `epic-sources`. "Script patches": `scripts-proof`'s P2 is
  `reference.sha256` (with how to regenerate it, and `sha256sum -c` by hand); changing a script = changing its set.
- `CLAUDE.md`: `epic-sources/` row (the leaked script sources, dev-only tools, never a build input); the rules: no
  UnrealScript in the repo, how to change a script, `grep -a` and the 0x02 bytes of `patches/`, `Scripts/{base,tree}`,
  `epic-sources/<Pkg>/Classes`; Latin-1/LF applies to `patches/`.
- `README.md` "What?": the script is content, the repo holds the patch sets. `patches/README.md`: "Changing a script:
  change its set" (the 5 steps with exact commands, what the helper runs, compare, headers, reference, commit) and
  "The proofs". `docs/SOURCE-LAYOUT.md`: a paragraph on the removal and the importer; the table row; the script-only
  packages keep their `.vcproj`. `docs/porting/PLAN.md`: one line under "Owner review of 2026-10-11".
- `docs/porting/analysis/scripts.md`: a banner (historical; where everything is now). One quoted line at the top of
  the 23 other notebooks that name the sources (audio-f6, controls, decompile-spike, engine-port, exploration-findings,
  headers, jitter, mouse, platform-f4, postprocess, pp-scale, profiles, rebind, saveconfig, script-patches,
  script-build-rules, splitscreen, ucc-windows, ui-scale, vanilla, video, web-ini-saves, webrtc); their text is left
  as written.

### Paragraph for REPORT.md section 60 (for the coordinator; REPORT.md not edited)

Under "30. Input, UI, scripts, profiles and saves" (30.17 and every "Script, `Source/<Pkg>/Classes/...`" line of
sections 30 and 50):

- **The script sources left the repository (T4, 2026-10-11).** Your rule: UnrealScript is game content. The 2233
  files of `Source/<Pkg>/Classes` (Epic's leaked `.uc`, `.huc`, `.upkg` with our edits on top) and Epic's 7
  `.ucproject` are deleted, and the history is rewritten without them; `.gitignore` keeps them out. A path
  `Source/<Pkg>/Classes/<Class>.uc` in this report now means that class as the build decompiles it
  (`build-*/Scripts/base`, then `Scripts/tree` with the sets applied); our changes to it are in `patches/<set>/`
  (UCPlayerInput, UCPlayerController, UCTrainingController: `mouse-look`; GUIController: `player-name`;
  UC2Settings_Main and the two new classes: `keys-menu`; PSDistortionBaseMaterial, GUIComponent and Epic's other 28
  fixes: `epic-fixes`). Changing a script is changing its set (`patches/README.md`, "Changing a script":
  `uc-patches-regen.sh edit <set>`, edit, `make <set>`, rebuild). The proof that the sets give our packages (T1's P2,
  against a build of the sources) cannot be rebuilt any more: it passed one last time at the removal (UCGameplay 2474,
  GUI 720, UCMenu 2360 functions bytecode-identical, 0 differences) and is now a regression reference,
  `patches/reference.sha256` (GUI.u 370666 bytes c796d108..., UCGameplay.u 1589219 7b8ade86..., UCMenu.u 2409720
  1166328f...), checked by `scripts-proof` and regenerated with a deliberate set change. Generated headers are checked
  and regenerated from the patched tree (a class's cpptext and native structs, which the shipped packages lack, stay as
  committed). `tools/dev/script-build.sh` (M14) remains a dev-machine tool over Epic's text in `epic-sources`, never a
  build input. Truth: `scripts-removed.md`, `patches/README.md`, BUILD.md "Scripts".

## Step 6 (2026-10-11): proof

- **Clean native build**: `rm -rf build-native`, `bin/capped 4G 900 -- cmake --preset native` (rc 0, `-- Script
  patches: ON, sets epic-fixes,epic-source,mouse-look,player-name,keys-menu; rebuilds UCGameplay;GUI;UCMenu ...;
  shipped .u from /home/ion/WorkUC2/assets/uc2/System` = the restored asset tree, no extra argument),
  `bin/capped 6G 5400 -- cmake --build build-native -j2` rc 0 (`logs/t4/{configure,build}-native-clean.log`): decompile
  UCAnnouncements, UCGameplay, GUI, UCMenu; apply 5 sets; compile; in `build-native/Scripts/System`, `sha256sum -c
  ../../../patches/reference.sha256`: `UCGameplay.u: OK`, `GUI.u: OK`, `UCMenu.u: OK` (`logs/t4/clean-sha256-check.txt`).
  No `.huc`, `.upkg` or script source anywhere in the inputs.
- `cmake --build build-native --target check-headers` rc 0 (`logs/t4/check-headers-cmake.log`): GUIClasses.h,
  UCGameplayClasses.h current, "(scripts: .../build-native/Scripts/tree)".
- `cmake --build build-native --target scripts-proof` rc 0, 2 min 6 s (`logs/t4/scripts-proof.log`): `PASS P1: problems 0
  in 7 packages` (patch table 30 identical; Engine with the dev aid from epic-sources), `PASS P2: 3 packages equal to the
  reference (reference.sha256)` (each "as the reference"), `PASS P3: decompile, apply and build are deterministic`.
- `cmake --preset wasm-perf` (`-- Script patches: ON, 3 packages from the host build .../build-native/Scripts/System`) and
  build rc 0 (`logs/t4/{configure,build}-wasm-perf.log`): `[meta] wrote .../meta.json (...; 3 script packages:
  System/GUI.u System/UCGameplay.u System/UCMenu.u)`; `build-wasm-perf/Source/SDLLaunch/scripts/` holds the three,
  `sha256sum -c` of the reference there: 3 OK; meta.json `scripts` = the same sizes and sha256
  (`logs/t4/wasm-meta-scripts.txt`).
- Game checks on a run tree of the build (`M14_PREFIX=t4-m14 tools/dev/m14-scripts-check.sh verify menu patcher deadbolt
  leftovers`, `logs/t4/m14/m14-summary.txt`; trees `run/t4-m14-run` = the build's 3 packages, `run/t4-m14-ship` = the
  asset tree's shipped ones; load 15 with the wasm build running): `PASS verify` (exit 0, all native class layouts
  match, Editor layouts match), `PASS menu` (both trees: menu up, 0 Critical, 0 GL errors), `PASS patcher` ("rebuilt
  tree: not applied to GUI UCGameplay UCMenu (rebuilt), patched Editor Engine UCInventory UnrealGame (shipped there);
  shipped tree: patched Editor Engine GUI UCGameplay UCInventory UCMenu UnrealGame"), `PASS deadbolt` (both trees),
  `PASS leftovers`. Then, at load 3-6, `D6_TREE=t4-m14-run D6_SHIP_TREE=t4-m14-ship tools/dev/d6-mouse.sh look dodge`
  (`logs/t4/d6-look-dodge.txt`): 55 PASS, 0 FAIL (look 38, dodge with dodge-off 17).
- `git grep -a -c -E 'Source/[A-Za-z0-9]+/Classes'` (`logs/t4/final-grep.txt`): only REPORT.md/REPORT.html (the
  coordinator's, 10 each) and historical notebooks (controls 6, decompile-spike 2, headers 1, jitter 1, mouse 2,
  platform-f4 1, pp-scale 1, script-patches 1, vanilla 4) and this notebook (2). No tool, build rule or current doc.
- `git ls-files | grep -a -c '\.uc$'` = 2 = `git ls-files patches | grep -a -c '\.uc$'`.
- Memory: configure 4G, builds 6G (-j2: load 11-17 during this task), Python 2G, ucc 3G (inside uc-build.sh), games 2G
  (one at a time); no ceiling hit (no exit 137).

## Open questions (coordinator / owner)

1. P2 is now a regression reference, not a proof of meaning: a deliberate set change is accepted by regenerating
   `reference.sha256`. What the sets mean is checked by P1 (Epic's fixes, function by function) and by the game checks;
   our own sets (mouse-look, player-name, keys-menu) have no independent semantic reference any more. Acceptable?
2. The reference depends on the inputs the default build documents (shipped `.u`, the retail per-class inis, this
   repo's decompiler/compiler). A build without the retail inis (`UC2_SCRIPT_RETAIL_INIS_DIR` none and a game folder
   with the leak's inis) was not measured here; the T3 notes say a few config defaults then differ, so its sha256
   would not match. Should `scripts` warn when the default build's output differs from the reference?
3. `--ref` (the old bytecode comparison) has no default: T1's `run/t1-ref` matches the sets as of 4cbe12f only.
4. The 18 Epic `.vcproj` still list `..\Classes\*.uc` items (kept byte for byte as Epic's). Leave, or strip?
5. `run/t4-sources/source-classes-4cbe12f.tar.gz` keeps the removed files (Epic's text + our edits) on the dev
   machine; delete it after the history rewrite if the owner prefers no copy outside epic-sources.
6. `patches/README.md`'s workflow edits a scratch tree inside `build-native/Scripts/edit-<set>`: a re-configure that
   cleans `Scripts/` would remove an unfinished edit (T3's configure deletes `Scripts/` only when the patches are
   turned OFF; not otherwise).
