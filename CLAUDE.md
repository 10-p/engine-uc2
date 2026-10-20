# engine-uc2: agent guide

Unreal Championship 2 (Unreal Engine 2 build 2226 "Avaris", package version 151) from Epic's Xbox/Win32 tree, for
Linux, Windows (MinGW-w64), the Web (Emscripten) and Android. Private: proprietary source. What the engine changes:
[`README.md`](./README.md); how to build it: [`BUILD.md`](./BUILD.md).

## The work folder: `/home/ion/WorkUnreal` (`<repo>/..`)

| Path | What | Rule |
|---|---|---|
| `engine-uc2/` | this repo, GitHub `10-p/engine-uc2` (private, `origin`), branch `main` | no push without the owner's go-ahead |
| `unreal-frontend-arch-private/` | the frontend (`main`), target `uc2`; the e2e specs in `packages/e2e` | read it, change it only in its own work |
| `epic-sources/uc2-avaris-2226/` | Epic's drop, the baseline (`epic-sources/README.md`; import commit `9fc7bcf`) | read-only; tools that read it take `UC2_EPIC_SOURCES=<dir>` |
| `assets/uc2-assets/` | the cooked game folder (`UE_ASSETS_DIR`), also what the frontend target serves | read-only overlay mount; never committed |
| `assets/uc2-retail-assets/` | the retail disc's files; only the pipeline proof's P1 reads its `System/` inis (`uc-patches-proof.sh --retail-inis`), the build and the publish read the game folder alone | read-only |
| `emsdk/` | Emscripten 5.0.7 | `source ../env.sh` |
| `bin/capped`, `bin/mem-watchdog` | the memory ceiling and its watchdog (below) | every heavy run |
| `run/`, `logs/` | run trees (`tools/dev/mkrun-uc2.sh`) and tool logs | scratch |

The repo: `Source/` (Epic's packages and ours), `Engine/Config/` (the inis, [`README.md`](./Engine/Config/README.md)),
`patches/` (the script patch sets), `cmake/`, `vendor/glue/` (the frontend's ABI files, vendored), `Thirdparty/`,
`Android/` (the Gradle/NDK project of the Android app, BUILD.md "Android"),
`tools/` (the cookers and format readers), `tools/scripts/` (the script patch pipeline), `tools/dev/` (checks and
measurements: each script's header says what it does and how to run it), `docs/` (the Epic import notes and the port's
notebooks: records, kept as history).

- The baseline is the Epic drop in `~/WorkUnreal/epic-sources/`, see its README. A change is ours when the repo differs
  from that drop.
- Features are listed in the frontend's `docs/14-change-inventory.md`; a new feature adds its line there.

## Rules

- Be gentle with Epic's code. Never define `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`, `LIN_SAVE` or
  `USE_LINEAR_LOADING`. When an Xbox-only path is what the PC needs (post-processing, movies), lift the gate through
  a named capability; do not delete the other branch. Real bugs in shared code get real fixes, at the cause.
- Removing a broken effect is not a fix. The community "fixed maps" are reference only.
- TCHAR is ANSI on every target. Never add `-fshort-wchar` or `UNICODE`.
- `grep` here is ugrep and skips Latin-1 files silently: use `grep -a` or `git grep`. Script text has 0x02 bytes
  (button glyphs): `patches/`, `build-*/Scripts/{base,tree}`.
- No UnrealScript in this repo: script is game content. The build decompiles the shipped `.u` into
  `build-*/Scripts/base`, applies `patches/` into `Scripts/tree` and compiles `Scripts/System` (BUILD.md, "Script
  patches"). A script changes through its patch set (`patches/README.md`, "Changing a script"); never commit a `.uc`
  outside `patches/`, nor Epic's script text beyond the context lines a diff needs.
- Sources (`.cpp .h`, the `.diff`/`.uc` of `patches/`) keep their encoding (four are Latin-1) and LF endings.
  `Engine/Config/{Default,DefUser}.ini` and `Engine/Config/web/*.ini` are LF (`tools/dev/sync-web-ini.py` generates
  the web ones and refuses a CR); Epic's reference copies `DefaultXbox.ini`, `DefUserXbox.ini`, `KeyBinds.ini` stay
  Epic's bytes. Windows project files keep CRLF.
- Vendored libraries live in `Thirdparty/`, never under `Source/`. Fetched ones (SDL2, GL4ES, OpenAL Soft) stay
  fetched.
- Never commit assets, `build-*/` or anything from the work folder's asset and reference trees.
- Commits: one coherent step each, Conventional Commits naming the feature (`feat:`, `fix:`, `refactor:`, `build:`,
  `docs:`), never a stage number or a date. Docs change in the same commit as the code they describe.
- Deleting or overwriting anything outside `build-*/` and your own scratch files needs the owner. A missing tool or an
  access you lack: say so and stop, do not work around it.

### Comments, docs and names

- Comments explain the code as it is, in one or two lines. No work ids or stage markers, no story of a change ("was",
  "used to", "no longer", "before"), no "measured" proof, hardware, owner quotes, dates, commit hashes, no pointers to
  other comments, no glyphs or ALL-CAPS emphasis; a platform statement lives inside its `#ifdef`. A comment that
  states a default or a behaviour matches the code beside it. Epic's and the UT2004 Linux port's own comments stay as
  they are. Tool headers say what the tool does and how to run it.
- Docs (README.md, BUILD.md, this file, `Engine/Config/README.md`, the patch-set READMEs) describe the current state,
  in present tense, written from the code. `docs/porting/**` and the other notebooks under `docs/` are records: they
  are not edited, only labelled.
- Names follow behaviour. Ini sections and keys players may have saved, the `WebRTCDrv` package and its classes,
  exported ABI symbols and log lines the frontend matches keep their names; change those only together with the
  frontend. CMake options of the shared build take the `UE_` prefix.

## Running anything: memory ceiling, always

- Every run of the game, a browser, a wasm build under Node, a test harness or wine goes through
  `$W/bin/capped <memory> <seconds> -- <command>` (`$W` = `<repo>/..`): one cgroup with a hard memory ceiling and a
  time limit, no core dumps, and whatever is left is killed when it returns. Builds too:
  `bin/capped 6G 7200 -- cmake --build <dir> -j3` when several agents build at once.
- Give each run the ceiling it needs: the native game or `ucc-bin` well under 2G, the wasm build under Node under 3G,
  one headless-browser boot about 3.2G, a parallel build under 6G. `bin/mem-watchdog` warns and kills capped runs at
  the machine profile's levels (the frontend's `packages/e2e/parallel.config.json`); start it if `pgrep -f mem-watchdog`
  finds nothing.
- `capped` sets `ALSOFT_DRIVERS=null` unless the caller sets it, so no test plays through the desktop; audio tests
  choose OpenAL Soft's wave writer (`tools/dev/run-uc2-audio.sh`).
- Exit 137 from `capped` means the ceiling was hit. Do not retry unchanged: find what uses the memory first. Three
  failed attempts at one step without a new diagnosis: stop and report what is known.
- Never load the game or a test page in the Claude desktop app's browser pane, in Claude in Chrome or in the owner's
  browser. Browser tests use a separate headless Chromium with a throwaway profile, started through `capped`, one at a
  time; stop the servers and browsers you started.
- The web build sets an explicit `-sMAXIMUM_MEMORY`, and a test harness keeps no whole game files in JavaScript memory
  per worker.

## Build

[`BUILD.md`](./BUILD.md). Short version: `cmake --preset native && cmake --build build-native -j8`.
