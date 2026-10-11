# engine-uc2: agent guide

Unreal Championship 2 (Unreal Engine 2, build 2226, package version 151, codename Avaris), being ported from
Epic's Xbox/Win32 tree to Linux, Windows (MinGW) and the Web (Emscripten). Private: proprietary source.

Read in this order: `docs/porting/PLAN.md` (milestones and gates), `docs/porting/LOG.md` (where we are),
`docs/porting/BLOCKERS.md`, then the `docs/porting/analysis/` note for the area you touch.

## The workspace: `/home/ion/WorkUnreal` (the canonical home since 2026-10-11)

This repo lives beside every sibling engine and the frontend; the workspace's own `README.md` and `SETUP-GAPS.md`
describe it (`bin/setup.sh` on a new machine). `$W` below is `/home/ion/WorkUnreal`, i.e. `<repo>/..`.

| Path | What | Rule |
|---|---|---|
| `engine-uc2/` | this repo | GitHub `10-p/engine-uc2` (private, `origin`); history rewritten 2026-10-11 (no UnrealScript sources) |
| `unreal-frontend-arch-private/` | the frontend (`main`), target `uc2` | no push without the owner's go-ahead |
| `emsdk/` | Emscripten 5.0.7 | `source ../env.sh` |
| `assets/uc2-assets/` | the cooked asset tree: the game folder (`UC2_ASSETS_DIR`) and what the frontend target serves | an overlay mount of the workspace's read-only image (`assets/mount-assets.sh`); never committed anywhere |
| `assets/uc2-retail-assets/` | the retail disc's files (its `System/` holds the 17 shipped `.u`, `Characters/Gametypes/Maplists.ini`; `Sounds/` the Xbox banks) | read-only; the script build reads only its three inis (`UC2_SCRIPT_RETAIL_INIS_DIR`) |
| `bin/capped`, `bin/mem-watchdog` | the memory ceiling and its watchdog (below) | every heavy run |
| `run/`, `logs/` | run trees (`tools/dev/mkrun-uc2.sh`, or the workspace's `bin/mkruntree.sh <name> uc2 <build>`) and tool logs | scratch |
| `/home/ion/WorkUC2` | the port's original workspace: `epic-sources/` (Epic's pristine leaked tree, `reference/epic-sources.sha256`) and `reference/` (ISOs, Xbox SDKs, community packs, the owner's brief) | read-only, never copied; a separate home may host long-term specialised work. Only the tools built on Epic's leaked text need it (`tools/dev/script-build.sh`, the Engine part of `uc-patches-proof.sh` P1, `mkassets-uc2.sh`, `import-epic-sources.py`, the report tools): point them at it explicitly; no build, test or publish reads it |

## Rules

- Small stepping stones. A milestone is done when its gate in PLAN.md is observed, not when the code is written.
- At most 8 agents at a time (the owner raised it from 4 on 2026-10-10). Every agent appends what it found and did to
  `docs/porting/analysis/<topic>.md` as it goes, so a successor can continue. The coordinator adds one dated
  line per green gate to `docs/porting/LOG.md`.
- Be gentle with Epic's code. Never define `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`, `LIN_SAVE` or
  `USE_LINEAR_LOADING`. When an Xbox-only path is what we need (post-processing, movies), lift the gate
  through a named capability, do not delete the other branch. Real bugs in shared code get real fixes.
- Removing a broken effect is not a fix. The community "fixed maps" are reference only.
- TCHAR is ANSI on every target. Never add `-fshort-wchar` or `UNICODE`.
- `grep` here is ugrep and skips Latin-1 files silently: always `grep -a`. `find` is bfs. Script text has 0x02 bytes
  (button glyphs) too: `patches/`, the build's `build-*/Scripts/{base,tree}`, `epic-sources/<Pkg>/Classes`.
- **No UnrealScript in this repo** (owner, 2026-10-11: script is game content). There is no `Source/<Pkg>/Classes`;
  the build decompiles the player's shipped `.u` into `build-*/Scripts/base`, applies `patches/` into `Scripts/tree`
  and compiles `Scripts/System` (BUILD.md "Script patches"). To change a script, change its patch set:
  `patches/README.md`, "Changing a script" (`tools/dev/uc-patches-regen.sh edit|make <set>`, rebuild,
  `uc-patches-proof.sh p2 --write-reference`, headers with `regen-headers.sh`). Never commit a `.uc` outside
  `patches/`, nor copy Epic's leaked text into the repo beyond the context lines a diff needs.
- Sources (`.cpp .h` ..., the `.diff`/`.uc` of `patches/`) are Latin-1 with LF endings; do not re-encode. Windows
  project files keep CRLF.
  `Engine/Config/{Default,DefUser}.ini` and `Engine/Config/web/*.ini` are LF too (`tools/dev/sync-web-ini.py` refuses a
  CR); Epic's reference copies `DefaultXbox.ini`, `DefUserXbox.ini`, `KeyBinds.ini` stay Epic's bytes (CRLF).
  Checked against the siblings (F-4d, 2026-10-09): engine-ut2004 keeps Latin-1 (35 ISO-8859 files) and LF inis,
  but kept Epic's CRLF in most of its sources (1,736 of 1,788 `.cpp/.h/.uc`); engine-ut99 and engine-unreal1 are LF
  throughout. UC2 converted its sources to LF once, at the import (`docs/SOURCE-LAYOUT.md`), and stays so.
- Vendored libraries live in `Thirdparty/`, never under `Source/`. Fetched ones (SDL2, GL4ES, OpenAL Soft)
  stay fetched.
- Never commit assets, `build-*/`, or anything from `reference/`.
- Commits: one coherent step each, conventional style (`Core: …`, `build: …`, `docs: …`). Docs change in the
  same commit as the code they describe.
- Deleting or overwriting anything outside `build-*/` and your own scratch files needs the owner.
- If you cannot access something or a tool is missing, say so in `BLOCKERS.md` and tell the owner; do not
  work around it silently.

## Running anything: memory ceiling, always

On 2026-10-04 an agent's headless-browser test of the WebAssembly build grew to 6-9 GB per browser process,
was retried in a loop, was also opened in the Claude desktop app's own browser pane, and the kernel's
out-of-memory killer took down the owner's desktop session. These rules exist because of that.

- Every run of the game, a browser, a wasm build under Node, a test harness or wine goes through
  `$W/bin/capped <memory> <seconds> -- <command>`. It puts the whole process tree in one cgroup with a
  hard memory ceiling and time limit, turns core dumps off, and kills whatever is left when it returns.
- The owner's memory policy for this 32 GB machine (2026-10-04): one piece of work (a compile, a test, a
  browser run) may use **up to 16 GB as long as that memory is doing something useful**; parallel work (several
  processes at once, for example multi-client network tests) may take the machine **up to 24 GB**. Above 16 GB,
  stop and ask why and whether we are doing a good job; above 24 GB, kill it before the kernel's reaper kills
  things senselessly.
- In practice: give each run the ceiling it plausibly needs, not the maximum (measured so far: native game or
  `ucc-bin` well under 2G; wasm under Node under 3G; one headless-browser boot 3.2G; a full parallel build
  under 6G). A ceiling above 16G for a single run needs a written reason in the notebook. The sum of
  everything running at once stays under 24G. `$W/bin/mem-watchdog` (warn at 16 GB machine-wide,
  kill all capped runs at 24 GB) should be running; start it if `pgrep -f mem-watchdog` finds nothing.
- The game has sound since M8. `capped` sets `ALSOFT_DRIVERS=null` unless the caller sets it, so test runs never
  play through the owner's desktop; audio tests choose the wave writer (`tools/dev/run-uc2-audio.sh`).
- Exit 137 from `capped` means the ceiling was hit. **Do not retry unchanged.** Find out what is using the
  memory first (the engine's own stats, a smaller map, a heap profile under a lower ceiling).
- Three failed attempts at the same step without a new diagnosis: stop, write down what is known in the
  notebook and BLOCKERS.md, and report.
- Never load the game or any test page in the Claude desktop app's built-in browser pane, in Claude in Chrome,
  or in the owner's own browser. Browser tests use a separate headless Chromium with a throwaway profile,
  started through `capped`.
- One browser instance at a time. Stop servers and browsers you started before you finish (`capped` does it
  for what runs inside it; check with `pgrep` for anything started outside it).
- The web build sets an explicit `-sMAXIMUM_MEMORY` (no unbounded growth), and a test harness must not keep
  whole game files in JavaScript memory per worker.

## Build

`BUILD.md`. Short version: `cmake --preset native && cmake --build build-native -j12`.
