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
