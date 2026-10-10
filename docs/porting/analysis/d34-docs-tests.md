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
