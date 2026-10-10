# T3 notebook: the build rules of the script patch pipeline

Task T3 of the UC2 port: "build the engine" also builds the patched script packages (T1's pipeline,
`script-patches.md`) on the host preset, the other presets consume them, the web artifact carries them. Worktree
`engine-uc2-t3-cmake`, branch `t3-cmake` (main a9743b4 = T1 + T2). Frontend half: `ufrontend-scripts`, branch
`uc2-scripts`, notebook `docs/migration/notes/uc2-script-overlay.md` there. Logs `<WorkUC2>/logs/t3/`. Every run
through `<WorkUC2>/bin/capped` (configure 4G, builds 6G -j4, ucc 3G inside uc-build.sh, Python 2G).

## Design (as built)

- `cmake/UC2Scripts.cmake` (included by the root CMakeLists.txt after the packages, so it knows whether `Editor` and
  `UCC` exist): the options, the configure checks, the targets. `cmake/UC2ScriptsCheck.cmake`: the shipped-file check
  and the mirror copy, as functions (configure) and as `cmake -P` modes (build time).
- Options: `UC2_SCRIPT_PATCHES` (ON), `UC2_SCRIPT_PATCH_SETS` (`default`), `UC2_ASSETS_DIR` (the game folder, named
  like engine-ut2004's `UT_ASSETS_DIR`: "the game folder holding System/"), `UC2_SCRIPT_PACKAGES_DIR`
  (`UC2_ASSETS_DIR/System`), `UC2_SCRIPT_RETAIL_INIS_DIR` (optional), `UC2_SCRIPTS_DIR` (non-host presets).
  - Game folder resolution: the cache variable; else the `UC2_ASSETS_DIR` environment variable (the dev tools,
    `mkrun-uc2.sh`, `script-build.sh`, already read it); else `<repo>/../assets/uc2` **only when it exists** (the
    workspace layout every dev tool assumes; a STATUS line says "found beside the repo"); else FATAL_ERROR with what to
    pass. engine-ut2004's `UT_ASSETS_DIR` defaults to an absolute path of the owner's machine; that was not copied.
  - Retail inis: the cache variable, else `<repo>/../reference/iso-extract/System` when it exists, else `none` (the
    game folder's per-class inis; `uc-build.sh` notes it). Why they matter: scripts.md, the strip row of the table.
- Host = `TARGET Editor AND TARGET UCC AND NOT EMSCRIPTEN` and the build runs where it targets (Linux on Linux, or
  Windows on a Windows host: `CMAKE_HOST_WIN32 AND WIN32`). So: `native` (Linux), `windows` on a Windows host.
  Not host: `wasm*`, `windows`/`windows64` from Linux, `native64*` and `arm64` (no Editor at 64 bits).
- Host configure: Python 3 (`find_package(Python3)`) and bash required; the 17 shipped `.u` checked against
  `patches/shipped.sha256` (all 17, not only the compile list: the decompiler's World, the compiler and the strip load
  every one of them; 9.2 MB to hash, instant); `uc-patches.py sets` resolves and checks the selection (dependencies:
  "set keys-menu needs mouse-look"); `uc-patches.py packages --sets ... --compile-list <shipped>` gives the compile list
  and the kept packages; `Scripts/build-info.txt` records sets/packages/shipped/assets; stale `.u` of an earlier
  selection are removed from `Scripts/System` and stale package folders from `Scripts/base`.
- Host build (`scripts`, in ALL), one stamp per stage under `Scripts/stamp/`:
  1. `shipped.stamp`: the shipped check again (`cmake -P`), DEPENDS the 17 files, the sums, the check script;
  2. `base-<Pkg>.stamp` per compiled package: `rm -rf base/<Pkg>`, `uc-decompile.py <S>/<Pkg>.u --out base/<Pkg>/Classes
     --content <A> [--shipped-text for UCMenu]`, DEPENDS shipped.stamp, uc-decompile.py, ucpkg.py;
  3. `tree.stamp`: `uc-patches.py apply --base base --sets <list> --out tree`, DEPENDS the base stamps, sets.txt, every
     `.diff` and `new/**/*.uc` of the chosen sets (globbed with CONFIGURE_DEPENDS), uc-patches.py;
  4. `build.stamp` + `Scripts/System/<Pkg>.u`: `bash uc-build.sh --classes tree --out work --packages <compile list>
     --keep <kept> --shipped <S> --assets <A> --retail-inis <R|none> --ucc $<TARGET_FILE:UCC> --capped <bin/capped|none>`,
     then `cmake -E copy` of the kept ones, touch `Scripts/stamp/System.stamp`; DEPENDS tree.stamp, shipped.stamp,
     uc-build.sh, the UCC target (file-level: a relinked ucc-bin rebuilds), Epic's four inis, the retail inis.
- `scripts-proof` (not ALL): `uc-patches-proof.sh --work Scripts/proof` with the same inputs.
- Not host: `scripts` (ALL) always runs `cmake -P UC2ScriptsCheck.cmake` in copy mode: mirror `<UC2_SCRIPTS_DIR>/*.u`
  into `Scripts/System` (each must start with the package magic C1 83 2A 9E; files not in the source removed), touch
  `Scripts/stamp/System.stamp` only when something changed. Configure: FATAL_ERROR when the folder has no `.u`.
- OFF: `Scripts/` is deleted at configure (nothing left for a run tree or the artifact), `scripts` is a no-op target.
- Web artifact (`Source/SDLLaunch/cmake/Web.cmake`): SDLLaunch's POST_BUILD mirrors `Scripts/System` into
  `<artifact>/scripts/` (empty when there is none), then `emit-artifact-meta.mjs --scripts=scripts
  --scripts-under=System` lists them: `"scripts": [{"path", "assetPath", "size", "sha256"}]` (sha256 in the
  `checksums` form). `UC2Scripts.cmake` adds `add_dependencies(SDLLaunch scripts)` and LINK_DEPENDS on the stamp and
  on `CMakeFiles/uc2-scripts-mode.txt` (rewritten only when the mode/sets change), so the artifact relinks, and the
  POST_BUILD re-runs, when the packages or the mode change. (A relink of wasm-perf for a script change costs the
  link; chosen over a second writer of meta.json.)
- `tools/scripts/uc-build.sh`: `--capped none` (default when `<repo>/../bin/capped` does not exist: a stranger's
  machine), the compiler keeps its own name in the work tree (`ucc.exe` on a Windows host), `--headers` (export the
  C++ headers, `-ForceHeads` instead of `-noh`, `<Pkg>/Inc` seeded from `Source/<Pkg>/Inc`, no strip), `--retail-inis
  none`. `uc-patches-proof.sh`: `--capped none` honoured by its own Python runs.
- `tools/dev/mkrun-uc2.sh`: installs `<build-dir>/Scripts/System/*.u` when present; `UC2_SCRIPTS=<dir>` still wins;
  `UC2_SCRIPTS=none` = the asset tree's `.u`. Logs `script packages from the build (<dir>): 3: GUI.u UCGameplay.u
  UCMenu.u`. `m14-scripts-check.sh`'s "shipped" trees (ship, census-ship) now pass `UC2_SCRIPTS=none` (they would
  otherwise get the build's packages).
- `check-headers`: classes root defaults to the build's `Scripts/tree` (`UC2_SCRIPTS_TREE`; T2's cached default
  `Source/` counts as unset); with the patches OFF, `Source/` (transitional, while it holds the `.uc`). On a tree
  without `Engine/Classes`, `check-headers.sh` compiles the tree's packages with `uc-build.sh --headers` against the
  shipped ones (inputs from `Scripts/build-info.txt`) into `Scripts/headers`, and compares the generated headers of
  those packages with `tools/dev/check-headers-cpptext.py`.

## Finding: what a decompiled tree cannot give the header check

The first `check-headers` against `Scripts/tree` (`logs/t3/check-headers-1.log`): GUIClasses.h and
UCGameplayClasses.h STALE, 682 lines only in the committed headers, 40 only in the exported ones (all blank). Two
causes, both in the shipped packages, neither in the compiled bytecode (P2 is equal):
1. **cpptext** is stripped from the shipped packages (`uc-decompile` writes `cpptext { // stripped from the shipped
   package }`); the exporter writes a class's cpptext after its `DECLARE_CLASS(...)` line, with comments removed, so
   the exported body is blank lines. 27 classes in GUI, 17 in UCGameplay.
2. **`struct native` / `struct export`** are not serialised (`UStruct::StructFlags` is set by the compiler,
   UnScrCom.cpp:1551, and read by the exporter, UnEdExp.cpp:271, but no Serialize writes it; the struct exports'
   object flags are 0x70004 in GUI.u, no RF_Native), so the decompiled struct is plain and the exporter leaves it out:
   9 structs in GUI (e.g. `FMutatorRecord`, `FColorRecord`), 34 in UCGameplay.
So on a patched tree the check takes those two from the committed header and compares everything else byte for byte
(`check-headers-cpptext.py`: stub cpptext bodies replaced by the committed body of the same class; committed struct
blocks the export lacks entirely are left out). A patch that changes a cpptext or a native struct is not checked by
this (only by a check on full sources). Making the decompiler recover them would need the C++ side as input (the
committed headers), which is what the check compares against: not done. Owner question 2.

## Proof (2026-10-11)

### Configure, build, equality, no-op, touch

- Refusal of a rebuilt input, `bin/capped 4G 600 -- cmake --preset native` with the asset tree as it is now (its
  GUI/UCGameplay/UCMenu are rebuilt, `assets/uc2/System/SCRIPTS.txt`), rc 1 (`logs/t3/configure-refuse.log`):
  ```
  CMake Error at cmake/UC2Scripts.cmake:174 (message):
    Script patches: /home/ion/WorkUC2/assets/uc2/System

    The script build starts from the game's SHIPPED script packages (the retail
    disc's System folder, the same files as Epic's System folder), and these
    are not:

      /home/ion/WorkUC2/assets/uc2/System/UCGameplay.u: sha256 45dd2558ac16c48f82a2c4ccb49ad09f0aa4cf6a070968ab0c6f07caa8e7881f, not the shipped file (c675a4429a44dbf77b68581ff3c579816d80da130a8b6191c4a4f50534688ef9)
      /home/ion/WorkUC2/assets/uc2/System/GUI.u: sha256 4e5b1af0add70b57d88893694498b84249a98ffc96484912847819a6e780f204, not the shipped file (2eb7e89933c0bea5cd5125b667b2346a792b5bc031e671b4fbe6d1f36e560d64)
      /home/ion/WorkUC2/assets/uc2/System/UCMenu.u: sha256 1600fdc1906d6caadbe18de20240f4c7ad91d9e07e5029513b1c07ff30db5489, not the shipped file (4538a4ba120f53cbc49a7e2398b5e3feb979cb9822954b971f728b8df4865581)

    A package rebuilt from source (for example one an earlier patched build
    installed into the game folder, see System/SCRIPTS.txt there) cannot be the
    base: decompiling it would apply the patches twice.

    Put the shipped files back (from the disc), or point
    -DUC2_SCRIPT_PACKAGES_DIR=<dir> at a folder holding the 17 shipped .u
    files, or build without the script patches: -DUC2_SCRIPT_PATCHES=OFF.
  ```
- `patches/shipped.sha256` = sha256 of `epic-sources/System/<Pkg>.u` for the 17; `sha256sum -c` in
  `reference/iso-extract/System` (the retail disc): 17 OK.
- `cmake --preset native -DUC2_SCRIPT_PACKAGES_DIR=/home/ion/WorkUC2/epic-sources/System -DUC2_HEADERS_CLASSES_ROOT=`
  (3.7 s, `logs/t3/configure-native-1.log`):
  `-- Script patches: ON, sets epic-fixes,epic-source,mouse-look,player-name,keys-menu; rebuilds UCGameplay;GUI;UCMenu
  (compiles UCAnnouncements;UCGameplay;GUI;UCMenu); shipped .u from /home/ion/WorkUC2/epic-sources/System; game folder
  /home/ion/WorkUC2/assets/uc2 (found beside the repo); retail inis: /home/ion/WorkUC2/reference/iso-extract/System`
  (the asset tree's `.u` are not used: the shipped ones are copied into the work tree).
- `cmake --build build-native -j4` on a built engine: 22 s, rc 0 (`logs/t3/build-native-1.log`): decompile
  UCAnnouncements 104 / GUI 94 / UCMenu 171 (shipped text) / UCGameplay 232 classes, 0 errors; `make UCAnnouncements`
  1 s, UCGameplay 3 s, GUI 3 s, UCMenu 4 s (each `Success - 0 error(s)`), strip 5 s; `Scripts/System/{GUI.u 370666,
  UCGameplay.u 1589219, UCMenu.u 2409720}`.
- Equality with the coordinator's reference (`logs/t3/compare-t1-ref.txt`, `uc-patches.py compare` against
  `run/t1-ref/System`):
  ```
  UCGameplay: exports 10899 / 10899, functions+states 2474 (2474 bytecode-identical), classes with defaults compared 232, differences 0
  GUI: exports 3526 / 3526, functions+states 720 (720 bytecode-identical), classes with defaults compared 94, differences 0
  UCMenu: exports 10466 / 10466, functions+states 2360 (2360 bytecode-identical), classes with defaults compared 173, differences 0
  ```
  and byte-identical (`cmp`) to T1's P2 build (`run/t1-proof/p2/System`), all three.
- No-op: the second `cmake --build build-native -j4`: 0.57 s, the only scripts line `[100%] Built target scripts`
  (`logs/t3/build-native-noop.log`).
- `touch patches/mouse-look/UCGameplay.diff`, build: 18.5 s, `Scripts: applying ...` then `Scripts: compiling ...`,
  no decompile (`logs/t3/build-native-touch.log`).
- Vanilla: `cmake --preset native -DUC2_SCRIPT_PATCHES=OFF` -> `-- Script patches: OFF (the game runs the shipped .u)`;
  build rc 0; `ls build-native/Scripts`: no such file; `--target scripts` prints `scripts: UC2_SCRIPT_PATCHES is OFF,
  nothing to build` (`logs/t3/configure-off.log`, `build-off.log`).
- Subset `-DUC2_SCRIPT_PATCH_SETS=epic-fixes;mouse-look` (`logs/t3/configure-subset.log`, `build-subset.log`):
  `rebuilds UCGameplay (compiles UCAnnouncements;UCGameplay)`; `set epic-fixes 7 hunks, 0 new classes (not in this
  tree, left out: Engine, GUI, UCInventory, UCMenu, UCMutators, UnrealGame)`, `set mouse-look 18 hunks`; make
  UCAnnouncements, UCGameplay; `Scripts/System` = `UCGameplay.u` only (no keys-menu, GUI, UCMenu).
- Errors: `-DUC2_ASSETS_DIR=/nonexistent/UC2` -> `Script patches: /nonexistent/UC2 (UC2_ASSETS_DIR) is not a game
  folder: no System/DefaultXbox.ini.` + what to pass; `-DUC2_SCRIPT_PATCH_SETS=epic-fixes;keys-menu` ->
  `UC2_SCRIPT_PATCH_SETS=epic-fixes;keys-menu: uc-patches: set keys-menu needs mouse-look`.
- Back to `default`: only GUI and UCMenu decompiled again (UCAnnouncements, UCGameplay bases kept), the three
  packages byte-identical to T1's P2 build again (`logs/t3/build-native-2.log`).

### check-headers against Scripts/tree

- `cmake --build build-native --target check-headers` (`logs/t3/check-headers-2.log`), rc 0:
  ```
    GUI/Inc/GUIClasses.h: current (taken from the committed header, not in the shipped packages: 27 cpptext, 9 native structs)
    UCGameplay/Inc/UCGameplayClasses.h: current (taken from the committed header, not in the shipped packages: 17 cpptext, 34 native structs)
  check-headers: all 2 generated headers of the patched packages (GUI,UCAnnouncements,UCGameplay,UCMenu) are current (scripts: /home/ion/WorkUC2/engine-uc2-t3-cmake/build-native/Scripts/tree)
  ```
- Negative: a scratch copy of the tree with `var int T3StaleCheck;` added to UCPawn (`check-headers.sh --classes-root
  <scratch> --tree hdr-t3-stale`, `logs/t3/check-headers-stale.out`): rc 1, `UCGameplay/Inc/UCGameplayClasses.h:
  STALE`, `+    INT T3StaleCheck GCC_PACK(4);`.
