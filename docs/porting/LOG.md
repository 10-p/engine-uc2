# engine-uc2 progress log

One dated entry per green gate (PLAN.md), newest last. Evidence goes in the entry.

- 2026-10-04 — **M0 gate green: workspace and scaffold.**
  - Workspace reorganised by rename only, nothing deleted: ISOs → `reference/iso`, community packs →
    `reference/community`, Xbox SDK dump → `reference/xdk`, owner's brief copied to `reference/`.
  - Pristine tree: `reference/epic-sources.sha256` (16,451 files); `sha256sum -c` after all M0 work: 0 mismatches.
  - Repo: first commit `7633696` = the Epic sources as imported (3,448 LF-normalised, 82 byte for byte, 570
    binaries left behind; `docs/SOURCE-LAYOUT.md`). `git ls-files` holds no package, media or binary file.
  - Presets: `native` (pointer width 4, `-m32`), `native64` (8), `wasm` (emcc 5.0.7 from the workspace's own
    `emsdk/`), `windows` all configure. Only `Thirdparty/zlib` is in the build so far.
  - Frontend: `ufrontend/` cloned at `7fb637e`, local branch `uc2` (no such branch on the remote).
  - Assets v0: `assets/uc2` = 595 files, 1.1 GB (58 maps + 9 ComboMatinees, 62 `.utx`, 45 `.usx`, 16 `.ukx`,
    17 `.u`, 29 top-level inis, 16 ladder inis, 5 languages), built by `tools/dev/mkassets-uc2.sh`; every file's
    sha256 is present in the pristine manifest.
  - Found on the way: `epic-sources/System` already holds `default.xbe`, `defaultLL.xbe`, `defaultR.xbe`,
    `xbox.bin` and `Script.lin` (the leak's own Xbox builds), so decompilation references exist without the ISO.
  - Still running at the time of this entry: unpacking of the Xbox SDKs and ISO-only files
    (`reference/README.md` is that agent's running notes).
- 2026-10-04 — **M0 follow-up: reference material unpacked** (notes and inventory: `<WorkUC2>/reference/README.md`).
  - `bin/extract-xiso` (v2.7.1, built from source). ISO-only files in `reference/iso-extract/`: `default.xbe`,
    `xbox.bin`, the update/dash/downloader XBEs, `media/`, `Startup/` (built splash `.xpr`), `System/` (404 files).
  - Retail `default.xbe`: title ID 4D570021, certificate 2005-03-24, every library 1.00.5849 with QFE up to 15
    ⇒ **built with XDK 5849.15** (`reference/iso-extract/XBE-LIBS.md`).
  - XDKs unpacked to `reference/xdk/unpacked/`: `2005-02-5849.15` (the exact match), `2005-03-5849.17` (differs in
    6 files), `2003-02-5344`. Layout `XDK/xbox/{include,lib,bin}`, docs `XDK/doc/XboxSDK.chm`. All previously
    missing headers are there (xtl, d3d8, xgraphics, xact, xmv, xhv, uix, xonline, xbdm, dsound, dsstdfx) plus
    `xsasm.exe`, `xmvtool.exe`, `xactbld.exe`, `bundler.exe`. Reference only: the portable build never includes them.
  - Retail `System/` vs the leak: 385 of 404 files identical. `DefUser.ini` == the leak's `DefUserXbox.ini`.
    `Default.ini` is a slightly later `DefaultXbox.ini` (net rates 7000→10000/20000, four log suppressions on).
    17 more differ with no counterpart: `Characters.ini`, `Gametypes.ini`, `Maplists.ini` and 14 localisation
    files (`reference/iso-extract/DIFF-vs-epic-System.md`). Which side is newer per file is an M15 question.
- 2026-10-04 — **M1 gate green: Core compiles** (commits `82c9fd3`, `9faf167`, `e7fc07d`, `c36e3d2`; notebook
  `analysis/core-port.md`). Verified by the coordinator from deleted build directories:
  `cmake --preset native && cmake --build build-native --target Core` → `libCore.a`, 0 errors (GCC 15, `-m32`);
  `cmake --preset wasm && cmake --build build-wasm --target Core` → `libCore.a`, 0 errors (emcc 5.0.7).
  24 files under `Source/Core` changed (20 of Epic's, 4 new); root CMakeLists and Thirdparty untouched.
  - Platform layer: `UnGnuG.h`, `UnUnix.cpp`, `FFileManagerUnix.h` rebased on engine-ut2004's with UC2's PC
    contract re-added (ANSI TCHAR macros, `va_list` varargs, `appTrunc`/`appInvSqrt`/`appSqrt(FLOAT)`,
    `appCycles64`, `appSystemTime64`, contiguous allocator, file streaming, `appGetLanguageID`). New pthread
    threading (`UnThreadingPosix.*`). `wordwrap.cpp` made portable and built.
  - Carried forward: launchers must set `GSynchronizeFactory`/`GThreadFactory` before `appInit` and link with
    `-rdynamic` + whole-archive; `UCC.cpp` still creates the Windows factories.
  - Risks noted for M3: the Visual Studio projects compile with 4-byte struct packing (`/Zp4`), which i386 GCC
    matches for 8-byte members and wasm32 does not; null-`this` checks in `UObject::IsValid`/`GetFullName`.
  - Remaining warnings: `-Wwrite-strings` (1,128) and "invalid use of incomplete type" in `UnObjBas.h`/`UnClass.h`
    (accepted under `-fpermissive`; to be looked at if M3 misbehaves).
