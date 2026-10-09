# F-5 renderer items (owner review of 2026-10-09): notebook

Branch `f5-render`, worktree `<WorkUC2>/engine-uc2-f5-render` (from main `1372dec`), run trees `<WorkUC2>/run/f5*`,
logs and pictures `<WorkUC2>/logs/f5`. Brief: PLAN.md "Owner review of 2026-10-09", task F-5. Every run through
`<WorkUC2>/bin/capped` (game 2G, builds 6G `-j4`).

Steps, in the agreed order, each a commit + an entry here:
1. small fixes: `DOFShippedBlurAlpha` compiled default 1; dynamic gamma stays on; `DistortionCullDistance` one source of truth;
2. occlusion queries: what the Xbox used them for, whether engine-ut2004 has them; implement only if visible and cheap;
3. shader inventory: every shader a material / post-process layer can select, ported or a reason;
4. CTF-Aqueduct shadow flicker under the canyon trees: reproduce, measure, cause, fix, before/after strip;
5. shadow strength vs the Xbox: what Epic's code implies, a spot for the owner's xemu capture, our picture;
6. DM-Azure sky haze: desktop vs GLES twin A/B with numbers and an explanation.

## Log

### 2026-10-09 01:55 start

Read: PLAN.md owner review, REPORT.md section 20 (R-1..R-22, open issues), the notebooks named in the brief.
Native build started in the worktree (`capped 6G 3600 -- cmake --build build-native -j4`, log `logs/f5/build-native.log`).

### Step 1 (02:10): small fixes. Done, measured.

**`DOFShippedBlurAlpha` compiled default 1** (`Source/UC2GLDrv/Src/UC2GLRenderDevice.cpp`, the defaults block after
`StaticConstructor`'s property list). The value the DOF uses stays the shipped 65 (it is an alpha byte, the owner's
"radius" reading notwithstanding); only an ini without the key changes (it now also gets 65, not script's 100 in
third person). `Engine/Config/README.md` row 18 says so.

**Dynamic gamma: on, stays on.** Who sets it, read in the code:
- the device, at the first frame drawn with post-processing on (`UC2GLRenderDevice.cpp` `Lock`, `XboxClientDefaultsApplied`):
  `UClient::DynamicGamma = 1`, `LuminosityBias = 1` (what `UClient::Init` does under `_XBOX`);
- Epic's own `USplitScreenMgr::UpdateDetailLevels` (`Engine/Src/UnSplitScreenMgr.cpp:1040-1055`) executes `DYNAMICGAMMA 1`
  whenever no split view is active and the menu game is not running, and `DYNAMICGAMMA 0` with split views (the ramp
  is full screen). So the Xbox logic itself turns it back on; nothing in our code turns it off.
- `DYNAMICGAMMA 0|1` (Epic's console command, `UnCamMgr.cpp:253`) stays as the per-session debugging aid; note that
  the split-screen manager may turn it on again at its next detail update. No change made.

**`DistortionCullDistance`: one source of truth.** Before: the GL4ES device (web, twin) read the key with a hard-coded
`GConfig->GetFloat("UC2GLDrv.UC2GLRenderDevice", ...)` in `UC2GLMaterialState.cpp` (`HandlePSVertexShader`), once, into a
global; its own section's copy (`[UC2GL4ESDrv.UC2GL4ESRenderDevice] DistortionCullDistance=15000`, in both
`Default.ini` and `web/Default.ini`) was dead text; the compiled default (no line) was 5000. Implication today: none
visible (both inis carry 15000 in both sections), but a user who edits the GL4ES section (or `-ini:` on it) got
nothing, and a native ini without the desktop section's key gave the web 5000.
After: `DistortionCullDistance` is a `CPF_Config` `UFloatProperty` of `UUC2GLRenderDevice` like the device's other keys
(`Inc/UC2GLRenderDevice.h`, the property list in `StaticConstructor`), so UE's per-class config reads it from the
section of the device's own class: `UC2GLDrv.UC2GLRenderDevice` for the desktop, `UC2GL4ESDrv.UC2GL4ESRenderDevice` for
GL4ES; the material code reads `RenDev->DistortionCullDistance` (not positive = the Xbox's 5000); the console command
`DISTORTIONCULLDISTANCE` sets the property; the global `GUC2GLDistortionCullDistance` is gone. Compiled default 15000,
as the ini (the owner's O-7 value; the owner's "sensible defaults in the cpp" for DOF applied here too). Why not one
shared section for all render keys: every other device key is already per class (UE's convention, and what the web ini
generator `sync-web-ini.py` and the README assume); one exception read from a foreign section was the inconsistency.

Evidence (tree `run/f5v` with both keys deleted from both device sections of its `Default.ini`; `logs/f5v/`):
- desktop, DM-Praxis: `get UC2GLRenderDevice DOFShippedBlurAlpha` -> `True`, `DistortionCullDistance` -> `15000.000000`,
  log `distortion fades out over 15000 units (DistortionCullDistance of UC2GLDrv.UC2GLRenderDevice ...)`; 0 GL errors.
- twin (`-gl4es`, `-ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:DistortionCullDistance=7000`): the GL4ES device reads
  7000 (`get UC2GL4ESRenderDevice DistortionCullDistance` -> 7000, the desktop class still 15000), log `distortion fades
  out over 7000 units (DistortionCullDistance of UC2GL4ESDrv.UC2GL4ESRenderDevice ...)`, console
  `DISTORTIONCULLDISTANCE` shows 7000; 0 GL errors.
- `tools/dev/sync-web-ini.py --check`: both web files up to date (no ini change was needed).
