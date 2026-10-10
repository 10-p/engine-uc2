# D-3 stale text and D-4 test hygiene (2026-10-10)

Tasks D-3 and D-4 of PLAN.md "Owner review of 2026-10-10". Worktree `engine-uc2-d34`, branch `d34-docs-tests`
from main 2ed330f. Logs `/home/ion/WorkUC2/logs/d34`, run trees `run/d34*`.

## Step 1: `tools/dev/report-appendices.py` (D-3 1)

The coordinator's generator of 2026-10-08 was a scratch script (`inidiff.py` for Appendix C, an inline git loop
for A and B). Rewritten as `tools/dev/report-appendices.py [TIP]`: A and B from `git diff --name-status/--numstat
7633696 TIP -- Source` and `git log --reverse 7633696..TIP -- <file>` (commit lines cut at 150 characters, as the
original); C from `git show TIP:Engine/Config/*.ini` against `epic-sources/System/*.ini` (the coordinator's parser
unchanged: multi-valued keys as +/- values, Epic's D3DDrv/WinDrv sections as the base of ours).

Proof: `report-appendices.py 4813dd9` is byte-identical to the appendices in REPORT.md (sha256 a932129c...,
`logs/d34/appendices-4813dd9.md` vs `appendices-in-report.md`).

## Step 2: stale statements (D-3 2), commit 09daa62 + this one

- `Source/Engine/Inc/UnRenDev.h:887-889`: the gate users are UnRender.cpp, UnPixelShaderMaterial.cpp (which also
  answers for PSDistortionBaseMaterial) and UCCameraEffects.cpp (`grep -a RenDevHasXboxPostProcess`);
  `USubActionDepthOfField.cpp` has no gate and no change since the import (removed from the list).
- `Source/UC2GLDrv/Src/UC2GLRenderDevice.cpp:224-226`: `UseXboxProjectors`'s default comment no longer says skeletal
  meshes "were drawn with nothing" (projectors.md step 3: PC's software-skinned branch draws them); it says what the
  key does (the Xbox's static projector batch; dynamic projectors keep PC's path).
- `Source/WebRTCDrv/Inc/WebRTCDrv.h:6-9`: the channel is reliable and ordered (measured in E-2); webrtc.md already
  had the measurement in its steps: a correction note under its title; network.md's M11 plan line gets a pointer.
- `docs/porting/analysis/client-boot.md:175`: already marked superseded by F-4d; nothing to do.
- `docs/porting/analysis/audio.md:42-45`: "below 0.1 = silence" corrected (F-6 had not touched it): 0..0.1 is
  clamped to -10 dB, only < KINDA_SMALL_NUMBER is -100 dB (Epic's `CalcVolume`, XACT :864-873; ours `CategoryMB`).
- README.md: WebGL 2 (WebGL 1 fallback), F-1..F-6 in the status line; `Engine/Config/README.md:86` "WebGL 1" ->
  WebGL 2 since F-3; `docs/SOURCE-LAYOUT.md:15`: Epic's three reference inis are Epic's CRLF bytes again (9d5683a),
  only Default/DefUser are LF. BUILD.md's status (2026-10-09) is current.
- Frontend (patch only, `logs/d34/frontend-docs.patch`, against ufrontend `uc2` 1fdc55d, `git apply --check` OK):
  root README row, `targets/uc2/README.md` (title, manifest 1,227 files / 1,992.8 MB, "What the engine needed",
  Rendering on WebGL 2 and no -nopostprocess option, StartupMovies key, the mouse paragraph without the removed
  migration), `branding/help.md`, `branding/portal-notes.json`, `target.json`'s two comments (WebGL 1,
  -nopostprocess); `docs/migration/STATE.md` gets a new top entry (written at the end of D-4).
- O-3 entry for the coordinator: `logs/d34/log-o3.md`.

## Step 3: d6-mouse.sh pinned (D-4 1), commits b9904f0, 3d343c1

`place <probe> <check> [spot]` in `tools/dev/d6-mouse.sh`: TELEPORT twice (as jit-trace.sh places), then a check that
the pawn is there. Used before the arrow-strafe keys of `look` and `vanilla`, at the start of `lazy` (its camera
measure depends on walls) and at the start and before the adhesion part of `lock`. `pad` (NETPLACE) and `walk`
(TELEPORT) already placed. The open spot is jit-trace.sh's `1977.8 583.7 -34.1 33000 -1400` (D6_SPOT).

Found in run 1: at that spot the lock's pull-back never happens (`lock-offset-after-mouse-and-1s-later` 1799 ->
1799). The table of every earlier lock run (spawn -> offset after the mouse stops and 1 s later):

| spawn (PlayerStart) | runs | offsets | pull-back |
|---|---|---|---|
| 1977.8 583.7 -34.1 yaw 33000 | f1/s1-van0, jit/d6, d9/reg/d6-lock2, d34/d6-1 | 7->7, 1753->1753 (x2), 1799->1799 | never |
| -2367.6 3733.8 -482.1 yaw 48624 | f1/s2-d6, d9/reg/d6, d6/mouse | 1773->1757 (x3) | 16 < 50: FAIL |
| 1025.2 1698.1 -528.2 yaw -43424 | jit/d6-lock-jit-main, d9/reg/d6-lock-base1 | 2021->1742 (x2) | 279, all 12 checks PASS both |
| 1229.6 -672.9 -130.1 yaw 40448 | jit/d6-lock-jit-fix, d9/reg/d6-lock-base2 | 1891->1813 (x2) | 78 (one run failed adhesion) |
| others (5 spawns) | | | 230-808 |

So the "spawn-dependent lock check" was two PlayerStarts where the pull-back cannot be seen. `lock` now starts at
`LOCK_SPOT` = `1025.2 1698.1 -528.2 22112 0` (D6_LOCK_SPOT). Runs: `logs/d34/d6-3x.sh` (look vanilla lock lazy pad
walk, three times; run 1's lock still at the open spot), results below.

## Step 4: m7-effects.sh self-test (D-4 3), commit b9904f0

- `mkdir -p "$out"` before the self-test's report redirect (the shell opened `fx-selftest.report` before
  run-uc2-shot.sh made the folder: "FAILED to run" on a brand-new tree).
- The 60 s window: `Exec=44:POSTSELFTEST` is 44 s after the FIRST FRAME, the 60 s limit is wall-clock from the launch,
  so any start-up slower than 16 s lost the self-test. Now `Exec=44:POSTSELFTEST@54:exit` with a 220 s limit (as
  m11-twin.sh's own self-test): the game ends itself 10 s after the self-test; the limit only stops a hang.

## Step 5: the twin's two timing FAILs (D-4 4), commit b9904f0

- Liandri: the "random" noise is three TexPanners (`UCScreenLiandri` Noise1-3 = `T_HUD.HUD_LI_Static_Pan01/02`,
  `HUD_LI_Lines_Pan`): no random numbers, the pan offset is `Level->TimeSeconds x PanRate` of the shot's frame,
  different in every run (desktop vs twin mean 4.4-9.3 of 255 against a threshold of 6; the diff picture is the noise
  bands). A seed cannot fix it. New GL test key `MaterialTime=<s>` (`UC2GLRenderInterface.h` `UC2GLMaterialTime`,
  read in Init, -1 = off): texture modifiers, constant colours and PreSetMaterial see that time instead of the level's.
  `m7-effects.sh` liandri passes `MaterialTime=100`.
- Menu movie: the a19 FAIL had the title and list changing between the two shots (26 s and 28 s after the first frame:
  PSNR 28.7 / 26.6 dB): the page was still changing at those wall-clock times on a loaded machine. New test key
  `[UC2Movie] MaterialShotTimes=<s>,...` (`Engine/Src/UnMovie.cpp`): SHOT when the first MovieMaterial with a picture
  passes those movie times; `m9-movies.py --menu-args` default `MaterialShotTimes=3,5` (2 s apart, 3 s after the
  page opened, on any machine).

## Step 6: the browser strafe slope (D-4 2): measurement, found

The a19b browser trace survived in the frontend's test-results (`logs/d34/jit/a19b-browser-trace.log`, 306 lines).
`jit-analyze.py` on it: b = -0.000 / 0.001 / -0.000 / -0.000 (fwd/back/right/left), residual 0.00-0.01. The spec on
the same frames: 0.000 / 0.001 / **0.217** / -0.039. The difference: the spec fits the pawn alone
(`ys.push(pawn - cam ...)`, its parse does not even keep `bob=`), jit-analyze.py fits pawn + WalkBob, the point the
camera's target follows. Along the velocity the bob is 0 on forward/back and +-3.4 units on strafes (measured on these
frames), about 1.7 cycles in a 1.3 s segment, so its phase correlates with the frame times by chance: any sign, any
size (0.217, 0.247, -0.039). Not a per-frame dependency on the web: the drawn pawn + bob stands within 0.02-0.03 px of
one spot with the bob removed (nobob judder), as natively. The browser frame-time spikes are not the cause either
(the 0.247 run had no frame above 29 ms).
- `tools/dev/jit-analyze.py`: the reason written in its docstring; `ahead_nobob` (the fit without the bob) printed
  for comparison only.
- The spec's fix (frontend; patch only): `logs/d34/frontend-jitter-spec.patch` (parse `bob`, fit pawn + bob). To be
  run twice from a scratch copy of the frontend with this worktree's wasm-perf (no write in the frontend checkout).

## Machine-load rule (coordinator, 2026-10-10 02:18; load 42)

Builds at `-j2` while the 1-minute load is above 24; timing-sensitive game, twin and browser runs only below 16. The
first d6 three-times run (started at load 32) was stopped (killed by PID, my processes only: the loop, d6-mouse.sh,
m10-controls.sh, the uc2.bin in `run/d34-ship`); its run 1 (`logs/d34/d6-1`) is kept as evidence only (it showed the
lock spot problem of step 3). The wasm-perf build (started -j4) was stopped and resumed at `-j2`
(`logs/d34/build-wasm-perf-2.log`). Every timing run now goes through `logs/d34/quiet-runs.sh`, which waits for a
1-minute load below 16 and 10 GB available before EACH game/twin run and logs the load at its start
(`logs/d34/quiet-runs.txt`).

## Step 7: REPORT.md section 60, the superseded notes, the appendices (D-3 1)

- New top-level section `# 60. Changes since this report (2026-10-09/10)` before the appendices: the truth table
  (task -> LOG.md entry -> notebook), then per section/feature: 00 (start here, decisions, found-while-writing,
  milestones), 10 features 8, 9, 13 (+ arm64, new Engine files), 20 R-10, R-11, R-18, R-20, open issue 12, 30.5, 30.7,
  30.8, 30.16, 30.18 and the open issues, 40.1, 40.2, 40.8, 40.15, the frontend, 50, the appendices.
- 57 one-line notes "Superseded (2026-10-09/10): see section 60, <key>." after the paragraphs/bullets/tables that no
  longer hold; the reviewed text itself is unchanged (`git diff -U0`: insertions only above the appendices).
- Appendices regenerated for main `2ed330f` with `report-appendices.py` (a note under the heading says how to get the
  reviewed `4813dd9` version): 140 modified Epic files, 189 new, Default.ini 118 / DefUser.ini 54 keys.
- `REPORT.html` rebuilt with `tools/dev/report-html.cjs` (the committed HTML was in step: regenerating HEAD's md gives
  the committed html except the build stamp). The file was written through a temp file + `os.replace`.

## Step 8: builds, the frontend copy, the queue

- `build-wasm-perf` at -j2 rc 0 (`logs/d34/build-wasm-perf-2.log`): `SDLLaunch.wasm` sha256 `488c57bc...13c61`.
- The frontend is NOT written: an on-disk copy `run/d34-ufrontend` (tar of the checkout at `1fdc55d` without `.git`
  and test-results; on disk because /tmp is tmpfs) with `logs/d34/frontend-jitter-spec.patch` applied; `yarn build
  --target uc2 --engine-artifact <this worktree>/build-wasm-perf/Source/SDLLaunch` in it under `capped 4G` rc 0;
  `serve-web/SDLLaunch.wasm` has the same sha256 (`logs/d34/wasm-sha256.txt`).
- `logs/d34/quiet-runs.sh`: the browser jitter spec twice (one harness machine-wide, null sink by module index,
  `capped 6G`, mem-guard 17), then the native jitter traces, d6 x3, the m7 self-test on a brand-new tree x2, the twin
  in full and its effects + movies again; each start waits for load < 16 and 10 GB available.
- D-4 (5) `map-census.sh` `timeout -k`: left to the D-1 agent as told; it is done on their branch
  (`engine-uc2-d12` `3fdbbf6`: `timeout --preserve-status -k "$grace"`), not touched here.

## Step 9: the native reproduction (D-4 2), quiet machine (1-minute load 15.7 / 13.4 at the starts)

`JIT_OUT=logs/d34/jit tools/dev/jit-trace.sh` on `run/d34-run` (this branch's build, the asset tree's rebuilt
UCGameplay.u 4ed72f0b), keyboard + mouse, 1920x1080:

| run | frame times (steady frames) | judder RMS fwd/back/right/left px | b (pawn + bob) | b without the bob |
|---|---|---|---|---|
| `uneven` (16.7:33.3:16.7:16.7:25:33.3) | 16.8-35.0 ms | 0.53 / 0.38 / 0.53 / 0.54: PASS | -0.000 / -0.000 / -0.000 / 0.000 | -0.000 / -0.000 / **-0.035 / -0.023** |
| browser-like `FramePattern=25:23:24:50:26:24:29:23:21:30` | 21.1-51.0 ms | 0.71 / 0.50 / 0.71 / 0.80: PASS | 0.000 / 0.000 / -0.000 / -0.000 | 0.000 / 0.000 / **0.048 / 0.023** |
| browser, a19b trace (WebGL 2) | 19.4-55.0 ms | 0.66 / 0.31 / 0.45 / 0.50 | -0.000 / 0.001 / -0.000 / -0.000 | -0.000 / 0.001 / **0.217 / -0.039** |

Read: natively too, the fit without the bob is non-zero on the strafes only, with any sign, and exactly zero forward
and back; with the bob (what the camera follows) it is 0.000 everywhere, residual 0.00-0.01, on the web as natively.
The size of the chance correlation depends on how a segment's frame times fall on the bob's phase (a periodic
native pattern averages out more than the browser's irregular times). Decision: measurement, not a web-only
dependency; nothing to fix in the engine or UCGameplay.u; the spec fits pawn + bob as jit-analyze.py always did
(patch), the limit stays 0.2. The browser runs of the patched spec follow (step 10).

## Step 10: d6-mouse three times on a quiet machine (`logs/d34/d6q-{1,2,3}`, 02:44-03:00, load at the starts 11.6-15.6)

| scenario | run 1 | run 2 | run 3 |
|---|---|---|---|
| look (placed for the keys) | 38/0 | 38/0 | 38/0 |
| vanilla (placed for the keys) | 7/0 | 7/0 | 7/0 |
| lock (at LOCK_SPOT) | 14/0, pull-back 2021 -> 1829 | 14/0, 2021 -> 1829 | 14/0, 2021 -> 1829 |
| lazy (placed) | 6/0 | 6/0 | 6/0 |
| pad (NETPLACE, as before) | 9/1 (*) | 10/0 | 10/0 |
| walk (TELEPORT, as before) | 6/0 | 6/0 | 6/0 |

(*) `pad-matches-shipped-left-stick-walk`: half-stick segment 353.0 (ours) vs 340.9 (shipped), 3.55% against 3%: the
difference is ONE frame of running (the stick is held 1.0 s of wall clock = 30 or 31 frames at 30 fps; 12.1 units at
365/s backwards). The measure cannot resolve 3% of that segment, so the check now allows 3% or one frame (14.8
units), whichever is larger (commit e935d29); run 1's numbers pass under it; runs 2 and 3 ran with it (3.0 and 0.1
units). Every other number is identical in all three runs (the lock offsets exactly). The arrow-strafe checks
(`look-` and `vanilla-right-arrow-strafes-and-never-turns`) passed 6 of 6.
