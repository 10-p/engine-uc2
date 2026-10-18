Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# T3 notebook: the build rules of the script patch pipeline

> 2026-10-11 (T4): the script sources this notebook refers to (`Source/<Pkg>/Classes`, `.ucproject`) left the
> repository; Epic's text is `<WorkUC2>/epic-sources/<Pkg>/Classes` (dev machine only), the build's base is the
> decompiled `build-*/Scripts/base`, our changes are `patches/` (`scripts-removed.md`).

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

### Cross presets, the web artifact, the proofs target

- `bin/capped 4G 900 -- cmake --preset windows` (cross from Linux; `logs/t3/configure-windows.log`): `-- Script
  patches: ON, 3 packages from the host build .../build-native/Scripts/System
  (sets=epic-fixes,epic-source,mouse-look,player-name,keys-menu)`; build rc 0 (`build-windows.log`): `[  2%] Script
  packages from the host build`, `-- scripts: 3 script packages in .../build-windows/Scripts/System (updated from
  .../build-native/Scripts/System)`; the three files' sha256 equal to build-native's (c796d108 GUI, 7b8ade86
  UCGameplay, 1166328f UCMenu).
- `cmake --preset wasm-perf` (same STATUS line), build rc 0 (`build-wasm-perf.log`): `[meta] wrote
  .../build-wasm-perf/Source/SDLLaunch/meta.json (abi 1, wasm-integrated, global-module; 3 script packages:
  System/GUI.u System/UCGameplay.u System/UCMenu.u)`; `Source/SDLLaunch/scripts/{GUI,UCGameplay,UCMenu}.u`, meta.json:
  ```
  {"path": "scripts/GUI.u", "assetPath": "System/GUI.u", "size": 370666, "sha256": "sha256-c796d1085a7e989b5855ce29bcc6f04b04ffa8ce7cd5e373084bd54954c04dd9"}
  {"path": "scripts/UCGameplay.u", "assetPath": "System/UCGameplay.u", "size": 1589219, "sha256": "sha256-7b8ade8629e7ece0ece35044d88fed413c1a786c746763e85771378501791cda"}
  {"path": "scripts/UCMenu.u", "assetPath": "System/UCMenu.u", "size": 2409720, "sha256": "sha256-1166328fbdb4758089c4ec07d70d84ce969df81cecd10c8dbb57583e88b92e2d"}
  ```
  (`sha256sum scripts/*.u`: the same). A second `cmake --build build-wasm-perf`: 0.9 s, `scripts: ... (unchanged)`, no
  relink (`build-wasm-perf-noop.log`).
- `cmake --build build-native --target scripts-proof` (2 min 5 s, `logs/t3/scripts-proof.log`): `PASS P1: problems 0
  in 7 packages`, `PASS P2: 3 packages equal to the reference build`, `PASS P3: decompile, apply and build are
  deterministic`.
- ucc saves its packages 0600: the delivered ones are now 0644 (`uc-build.sh`; the mirror copy chmods too), so a
  web server running as another user can serve them. Rebuilt after the change: same bytes, the whole chain re-ran
  from the shipped check (its script changed), `logs/t3/build-native-3.log`. (The wasm-perf artifact's `scripts/` of
  this session keeps 0600 until its next relink: the mirror moves the stamp only on content changes.)

### The game on the built packages (native)

- `tools/dev/mkrun-uc2.sh build-native t3-run`: `script packages from the build
  (.../build-native/Scripts/System): 3: GUI.u UCGameplay.u UCMenu.u`; `UC2_SCRIPTS=<WorkUC2>/assets/uc2-shipped/System
  ... t3-ship` (Epic's three; the asset tree's are rebuilt ones today). `cmp`: t3-run's three = the build's,
  t3-ship's = epic-sources'.
- `ucc-bin verifyclasses` in t3-run (2G): rc 0, `VERIFY: Editor class layouts match`, `VERIFY: all native class
  layouts match` (`logs/t3/verify-t3-run.log`).
- `D6_TREE=t3-run D6_SHIP_TREE=t3-ship tools/dev/d6-mouse.sh look dodge` (each game inside bin/capped 2G, Xvfb;
  `logs/t3/d6-look-dodge.txt`): `look` 38/38 PASS (linear 10.012 units per count, same turn at every speed and in both
  axes, sensitivity/invert saved to User.ini, the right arrow strafes, ...); `dodge` 9 of 11 with
  `dodge-lock-and-one-press-of-d-switches-target` and `dodge-lock-held-double-tap-switches-and-does-not-dodge` FAIL;
  `dodge-off` 4/4. That run overlapped a `yarn build` (load 7-10). Control on the reference build's 17 packages
  (`run/t1-ref/System`, tree t3-ref): `dodge` 17/17 PASS (`logs/t3/d6-ref-dodge.txt`, load 4.5). Rerun on t3-run
  at load 3.8: **17 PASS, 0 FAIL** (`logs/t3/d6-dodge-2.txt`). So the two lock-switch checks failed under load
  once and passed on a quieter machine (the owner's rule: a failure under load is rerun before it counts).

### OFF in the artifact, and the frontend

- The emitter with an empty mirror (`UC2ScriptsCheck.cmake` copy mode from a missing folder, then
  `emit-artifact-meta.mjs --scripts=scripts`, scratch folder): `0 script packages`, `"scripts": []`. A wasm build
  configured OFF was not built.
- Frontend (`ufrontend-scripts`, branch `uc2-scripts`, its notebook `docs/migration/notes/uc2-script-overlay.md`): the
  artifact's `scripts` become an asset overlay (`engine-overlay/<assetPath>`, named in the manifest). `yarn build
  --target uc2 --engine-artifact build-wasm-perf/Source/SDLLaunch` assembles it; the uc2 boot spec passed 5/5 with
  the engine logging `ScriptPatcher: UCGameplay|GUI|UCMenu is not the package the patch was made for (generations 2,
  ...): rebuilt from source, patch not applied` and `[SHELL] Pre-cached: engine-overlay/System/UCGameplay.u (1589219
  bytes)`.

## What changed for other presets and tools

- Every preset configures as before when the game folder is found (this workspace: `<repo>/../assets/uc2`) and its
  17 `.u` are the shipped ones. **Today the asset tree's GUI/UCGameplay/UCMenu are rebuilt ones**, so a plain
  `cmake --preset native` stops with the refusal above until the coordinator restores them (or a build passes
  `-DUC2_SCRIPT_PACKAGES_DIR=<WorkUC2>/epic-sources/System`, or `-DUC2_SCRIPT_PATCHES=OFF`).
- Non-host presets need `build-native/Scripts/System` (or `UC2_SCRIPTS_DIR`, or OFF): a fresh `cmake --preset
  wasm-perf` before the native build stops with that message.
- Run trees: `mkrun-uc2.sh` without `UC2_SCRIPTS` now installs the build's patched packages (it used to leave the
  asset tree's). A tree meant to hold the shipped files needs `UC2_SCRIPTS=none` (the asset tree's) or
  `UC2_SCRIPTS=<dir of shipped files>`; `m14-scripts-check.sh` was changed accordingly; `d6-mouse.sh`'s
  D6_SHIP_TREE and similar trees are made by the caller and need the same.
- The web artifact relinks when the script packages change (LINK_DEPENDS), and carries `scripts/`.

## Open questions (coordinator / owner)

1. The game-folder fallback `<repo>/../assets/uc2` (and `<repo>/../reference/iso-extract/System` for the retail
   inis) is taken only when it exists and is announced, so the presets keep working in this workspace with no new
   argument; a stranger passes `-DUC2_ASSETS_DIR`. Keep it, or require the variable (e.g. `export
   UC2_ASSETS_DIR=...` in `<WorkUC2>/env.sh`, which the tools already read)?
2. `check-headers` on the patched tree checks everything the shipped packages carry, but not cpptext nor `struct
   native` blocks (not stored in a `.u`; taken from the committed header). A patch changing either is only caught by
   a check on full sources, which step 4 removes. Acceptable, or should those two live somewhere checkable (e.g.
   a C++-side list the decompiler reads)?
3. After the asset tree is restored to the shipped files, `targets/uc2/file-manifest.json` in the frontend names
   the rebuilt sizes; it should be regenerated (the overlay then replaces the shipped entries).
4. A non-host preset takes whatever packages the host build has; it prints the host's sets but does not refuse a
   different `UC2_SCRIPT_PATCH_SETS` of its own (it has no use for one). Fine?
5. Windows host (MSYS2, `ucc.exe`): written, not run (no Windows machine); `uc-build.sh` links content folders with
   `ln -s` (needs `MSYS=winsymlinks:nativestrict` + Developer Mode, else MSYS2 copies the folders: not measured).
