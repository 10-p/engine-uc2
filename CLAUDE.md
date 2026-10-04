# engine-uc2: agent guide

Unreal Championship 2 (Unreal Engine 2, build 2226, package version 151, codename Avaris), being ported from
Epic's Xbox/Win32 tree to Linux, Windows (MinGW) and the Web (Emscripten). Private: proprietary source.

Read in this order: `docs/porting/PLAN.md` (milestones and gates), `docs/porting/LOG.md` (where we are),
`docs/porting/BLOCKERS.md`, then the `docs/porting/analysis/` note for the area you touch.

## The workspace (everything is inside `/home/ion/WorkUC2`, machine `ion-kubuai`)

| Path | What | Rule |
|---|---|---|
| `epic-sources/` | the pristine Epic tree | never edit; `reference/epic-sources.sha256` proves it |
| `engine-uc2/` | this repo | local git only; no remote, no push without the owner's go-ahead |
| `ufrontend/` | frontend clone, branch `uc2` | no push without the owner's go-ahead |
| `emsdk/` | Emscripten 5.0.7 | `source ../env.sh` |
| `assets/uc2/` | canonical asset tree | never committed anywhere |
| `reference/` | ISOs, community packs, Xbox SDKs, the owner's brief | read-only originals; delete nothing |
| `/home/ion/WorkUnreal` | the sibling ports (engine-ut2004 is the template) | read-only, never modify |

## Rules

- Small stepping stones. A milestone is done when its gate in PLAN.md is observed, not when the code is written.
- At most 4 agents at a time. Every agent appends what it found and did to
  `docs/porting/analysis/<topic>.md` as it goes, so a successor can continue. The coordinator adds one dated
  line per green gate to `docs/porting/LOG.md`.
- Be gentle with Epic's code. Never define `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`, `LIN_SAVE` or
  `USE_LINEAR_LOADING`. When an Xbox-only path is what we need (post-processing, movies), lift the gate
  through a named capability, do not delete the other branch. Real bugs in shared code get real fixes.
- Removing a broken effect is not a fix. The community "fixed maps" are reference only.
- TCHAR is ANSI on every target. Never add `-fshort-wchar` or `UNICODE`.
- `grep` here is ugrep and skips Latin-1 files silently: always `grep -a`. `find` is bfs.
- Sources are Latin-1 with LF endings; do not re-encode. Windows project files keep CRLF.
- Vendored libraries live in `Thirdparty/`, never under `Source/`. Fetched ones (SDL2, GL4ES, OpenAL Soft)
  stay fetched.
- Never commit assets, `build-*/`, or anything from `reference/`.
- Commits: one coherent step each, conventional style (`Core: …`, `build: …`, `docs: …`). Docs change in the
  same commit as the code they describe.
- Deleting or overwriting anything outside `build-*/` and your own scratch files needs the owner.
- If you cannot access something or a tool is missing, say so in `BLOCKERS.md` and tell the owner; do not
  work around it silently.

## Build

`BUILD.md`. Short version: `cmake --preset native && cmake --build build-native -j12`.
