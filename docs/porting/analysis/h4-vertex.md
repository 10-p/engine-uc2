# H-4: character vertex corruption, window-width dependent (owner report 2026-10-11): notebook

Worktree `../engine-uc2-h4-vertex`, branch `h4-vertex` from main `7ceeee7`; run trees `run/h4*`, pictures and logs
`<WorkUC2>/logs/h4`. Every run through `<WorkUC2>/bin/capped` (game 3G, builds 6G).

The report: Lauren in DM-Decay, third person, web preview. At `TELEPORT 317.1 832.0 -99.3 19415 -3255` some
polygons connect wrong vertices (elbows linked, hair to feet, "wings"); at `322.0 815.6 -99.3 19415 -3255` (a step
back) it is fine. 2882x1212 fine, 2883x1212 corrupt. Paused (MenuPause teal effect): fine; unpaused: corrupt again.
Wireframe: fine. Pictures `<WorkUC2>/temp/char corruption*.png`.

## Log

### Step 0 (reading): hypothesis from the code, before any run

The width enters the skeletal mesh's **LOD choice**, not any buffer size: `USkeletalMeshInstance::Render`
(`Source/Engine/Src/UnSkeletalMesh.cpp`, LOD block after "Determine desired VertexSubset for LOD"):

    ResolutionX     = Max( 820, LodSceneNode->RenderTarget->GetWidth() )
    ResolutionBias  = 0.3 + 0.7 * ResolutionX / 640
    MeshVertLODFactor = 420 * ResolutionBias * DrawScale... / ( LODStrength * tan(FOV/2) * Z * CpxBias )
    LOD = first level whose DisplayFactor < MeshVertLODFactor (with hysteresis complex->simple only)

So the window width moves the LOD factor linearly, and a step back (bigger Z) moves it down: both owner
observations are a LOD threshold crossing, not a buffer limit.

Why two LODs in one frame: the pawn's **shadow** (`UShadowBitmapMaterial::Get`, `UnShadowProjector.cpp`) renders
the pawn again into a 256x256 target with `Viewport->SizeX = USize-4 = 252` while the frame is being drawn. Its
`FShadowSceneNode::GetLodSceneNode()` returns the frame's scene node, whose render target is the viewport's dummy
`FViewportRenderTarget` (width = `Viewport->SizeX`, now 252), so the shadow pass computes the LOD with
`ResolutionX = Max(820,252) = 820`, the main pass with the window width. Above some width (at the owner's spot,
2883) the two passes pick different LODs in the same frame.

Why that corrupts: the software-skinned stream goes through a `FVertexPool` (`UnRenderPool.cpp`), whose revision only
changes on `SetClient` (once per frame per mesh instance; `FSkinVertexStream::Revision` is constant 1 in UC2). The
second draw of the same instance in the frame finds its own pool (big enough: pools keep their creation size) with
an unchanged revision, so the driver does **not** re-upload: the second LOD's index buffer is drawn over the first
LOD's skinned vertices. Different LODs share the start of their wedge order only partly, hence "recognizable with
some polygons connecting other polygons", and wrong UVs too (hair texture on the dress in the owner's picture).
Pause: the shadow is not re-rendered while paused (no new pose), one LOD per frame, fine. Wireframe: projectors and
shadows are not drawn in wire mode (`!IsWire()`), one LOD per frame, fine.

Epic fixed exactly this in UT2004 (`engine-ut2004 Source/Engine/Src/UnSkeletalMesh.cpp:5288`):

    if( OldLODLevel != NewLODLevel ) // Fixes corruption when meshes are rendered multiple times in a single frame  (from Bob Berry.)
        SkinStream.Revision++;

UC2's build 2226 does not have that line. To be measured before it is believed (next steps): the LOD each pass
picks, whether the upload is skipped, the threshold width at the owner's spot from the numbers.

### Step 1: measured natively; the LOD hypothesis does not hold at the owner's spot

Trace added: `[Engine.SkeletalMeshInstance] LODTrace=True` (test key through `-ini:`, read once), in
`UnSkeletalMesh.cpp` (`UC2LodTraceChoose`/`UC2LodTraceDraw`): for the meshes of the viewed pawn, one line per
software-skinned draw with the pass (main/shadow), the width the LOD used, the factor, the level, the pool, its cache
id and revision, whether the driver uploaded, the LOD the pool's vertices hold, the index buffer's cache id, "STALE"
when they disagree. Scenario `tools/dev/h4-vertex/spots.scen` (spot, spot again, step back, spot again).

Native runs (`tools/dev/decay-flash.sh h4 <name> tools/dev/h4-vertex/spots.scen -gl4es ... Class=UCPawn_Necris_Lauren`,
`bin/capped 2G`):

| run | window | result |
|---|---|---|
| twin, Mesa llvmpipe | asked 2883x1212, got **2884x1212** | Lauren fine at the spot (same view as the owner's pictures), 0 STALE, no shadow pass of the pawn at all |
| twin, Quadro P620 (`UC2_GPU=1`, GLES 3.2 NVIDIA) | 2884x1212 | fine |

Lauren's LODs: `lauren_body` LOD 0 DisplayFactor 0.5 (1446 wedges, 2026 faces), LOD 1 (670 wedges, 791 faces);
`lauren_head` LOD 0 0.5 (720/1005), LOD 1 (318/347). Her factor at the spot is 3.19 (body), 1.33 (head): LOD 0 by a
factor 6 and 2.7, one pixel of width cannot flip it. **Hypothesis of step 0 rejected for this report** (the UT2004
revision bump stays a real Epic fix, but it is not this bug).

Width rounding: the engine keeps the viewport width even (`SDL2Viewport.cpp` `Align(NewX,2)`, as WinDrv; the web
binding `UC2GL4ESDrv.cpp` `& ~1`). Natively 2883 becomes 2884; the owner's widths: fine 2248, 2882 (even), corrupt
2883, 3149 (odd). An odd CSS width can only exist in the browser, where SDL's resize handler sends the CSS size
(`SDL_emscriptenevents.c` `Emscripten_HandleResize`) and our viewport answers `SIZE_CHANGED` with a SetRes. Next:
the browser itself (harness worktree `<WorkUC2>/ufrontend-h4`, branch `uc2-h4-vertex`, spec
`packages/e2e/uc2-wasm-h4.spec.js`).

### Step 2: the browser reproduces it; the cause is step 0's after all (two LODs in one frame), not a size limit

Browser: wasm-perf of `d71754a` (trace, no fix) through the harness worktree `<WorkUC2>/ufrontend-h4` (branch
`uc2-h4-vertex` from `uc2`, node_modules copied from `ufrontend-pads`; `corepack yarn build --target uc2 --assets
<WorkUC2>/assets/uc2 --engine-artifact build-wasm-perf/Source/SDLLaunch`), spec `packages/e2e/uc2-wasm-h4.spec.js`
(listed in that worktree's playwright.config.js), one harness machine-wide (checked), null sink `uc2h4` loaded and
unloaded by its module index, `bin/capped 7G 2400 -- node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=23`:
2 passed (3.3 min), guard peak 10.9 GB machine-wide. Logs and pictures `<WorkUC2>/logs/h4/web1/`.

- The canvas: CSS 2882 -> drawing 2882; CSS **2883 -> drawing 2882** too (`UC2GL4ESDrv.cpp` keeps it even). At
  device pixel ratio 1 the engine cannot tell the owner's 2882 from his 2883.
- The pawn's shadow pass runs in the browser (it did not under Mesa llvmpipe; it does on the Quadro natively): every
  frame Lauren's head is drawn by the shadow pass at **width 252, factor 0.4625 -> LOD 1** and by the scene at width
  2882, factor 1.334 -> LOD 0. The body is LOD 0 in both (1.105 / 3.19).
- **2882 run: 99 scene draws of the head STALE** (LOD 0 index buffer, 1005 faces over 720 wedge slots, drawn over
  the 318 LOD 1 vertices the shadow pass had just skinned into the same pool; slots 318-719 hold whatever the
  driver's scratch buffer held, the body's vertices skinned just before): `crop-2882-spot3.png` is the owner's
  picture (hair-textured triangles from the head down the dress). 2883 run: the scene drew first at the spot in
  that session, 0 scene draws stale (the shadow pass's draws were stale instead, invisible).

Native, on the GPU (Quadro P620, the GL4ES twin, `tools/dev/h4-vertex.sh`, the unfixed binary kept as
`run/h4/System/uc2-before.bin`): 2884 wide (asked 2883): **723 of 723 scene draws of the head at the spot STALE**,
0 of 252 at the step back; pictures `logs/h4/compare-native-2883.png` (spot corrupt, back fine, then both fixed).
So it is not browser-only: it needs the shadow pass (GPU) and a session state, see below.

What decides it (measured, `df-h4-uc2-before-288{2,3}.out`):
1. Order: at the owner's spot the shadow pass draws the head before the scene does (815/817 and 722/723 frames); one
   step back the scene draws first (250/251, 251/252). Only shadow-then-scene can corrupt the visible pawn: the step
   back cures it.
2. Pool: the shadow draw takes the smallest free vertex pool that fits LOD 1 (10176 bytes). At 2882 a 14208-byte
   pool was free (some other skinned mesh's, from earlier in the session): the scene then needed its own 23040-byte
   pool, uploaded, fine. At 2884 none was: the shadow pass took the head's 23040-byte pool, the scene found its own
   pool already holding it this frame (revision unchanged) and skipped the upload. Which pools exist and are free
   depends on which skinned meshes were drawn this and the previous 500 frames: the owner's "move a lot around the
   map", and the window width only through what is in view (AutoFOV widens the horizontal FOV with the width).
3. Pause: the shadow is not re-rendered while the game is paused (no new pose to project): one LOD per frame.
   Wireframe: projectors and shadows are not drawn in wire mode: one LOD per frame. Both as the owner saw.

Why the shadow pass has another LOD: `UShadowBitmapMaterial::Get` sets `Viewport->SizeX = USize-4 = 252` while it
draws, and the LOD uses `LodSceneNode->RenderTarget->GetWidth()` = the viewport's dummy target = `Viewport->SizeX`,
so `ResolutionX = Max(820,252) = 820` against the window's 2882: a factor 0.347 smaller (0.3+0.7*820/640 = 1.197 vs
0.3+0.7*2882/640 = 3.452). On the Xbox (640 wide: 820 in both passes) the two passes always agree; on a PC window
wider than 820 they can differ. No width threshold: the 2882/2883 edge the owner found is where his session's pool
state and draw order happened to flip, not a buffer size (no frame-sized buffer shares anything with the stream; the
skinned stream is 46272 + 23040 bytes whatever the window).

### Step 3: the fix (UT2004's line), regression, browser

`USkeletalMeshInstance::Render` (`Source/Engine/Src/UnSkeletalMesh.cpp`, right after `CurrentLODLevel = NewLODLevel`):
`if( OldLODLevel != NewLODLevel ) SkinStream.Revision++;`, Epic's own fix from engine-ut2004 ("Fixes corruption
when meshes are rendered multiple times in a single frame (from Bob Berry.)"). A LOD change between two draws of the
same instance now changes the pool's revision (`FVertexPool::GetRevision` = base + client revision), so the driver
re-skins and re-uploads for the second draw. Cost: one more skinning of the head per frame while its two passes
disagree (UT2004 pays the same). Ours vs GL4ES: ours (Epic's engine code); nothing in GL4ES, Emscripten or the driver
is involved (`logs/h4/gl4es.patch` not needed). The shadow pass's LOD width (252 -> 820) is left as Epic wrote it
(UT2004 has it too); with the revision bump both passes draw correct geometry at their own LOD.

Regression `tools/dev/h4-vertex.sh <tree> [widths]` (GL4ES twin on the GPU, LODTrace; PASS = both passes traced, 0
STALE, 0 Critical, 4 pictures):

    FAIL h4 uc2-before.bin width 2882: scene draws 2872, shadow draws 2868, STALE 266 (main 0, shadow 266)
    FAIL h4 uc2-before.bin width 2883: scene draws 2310, shadow draws 2308, STALE 982 (main 723, shadow 259)
    PASS h4 uc2.bin width 2882: scene draws 3926, shadow draws 3924, STALE 0, critical 0, pictures 4
    PASS h4 uc2.bin width 2883: scene draws 3724, shadow draws 3720, STALE 0, critical 0, pictures 4

Browser after the fix (wasm-perf of the fix, same spec, `logs/h4/web2/`): 2 passed (2.9 min), guard peak 10.4 GB;
2882: 786 traced draws, 0 STALE (182 frames with two LODs of the head); 2883: 2108, 0 STALE (267). Pictures
`logs/h4/compare-browser.png`: before at the spot (corrupt), before a step back, after at 2882, after at 2883.
The spec's STALE/shadow assertion (UC2_H4_EXPECT=fixed) was added after this run; the run's numbers satisfy it.

### Step 4: m11-twin and the M7 effects with the fix

`M11_SERIAL=1 M11_DESKTOP_TREE=h4m11 M11_TWIN_TREE=h4m11t tools/dev/m11-twin.sh boot selftest pinned effects`
(load 15-29 during the run; `logs/h4/m11-twin.txt`): PASS boot (97 fragment + 405 vertex shaders, 0 GL errors),
PASS selftest (15/15 on both), PASS pinned (mean 0.384), effects 13 of 14 PASS; **FAIL effect-blur 6.175 > 6**, the
check known to be time-dependent (BlurMe decays; earlier measurements 3.17 to 7.39 in decay-flash.md,
render-f5.md, web-renderer.md, d34-docs-tests.md). A/B of that one pair (`logs/h4/blurab/`, both bindings, one run
at a time): fixed 6.156 then **3.939**; unfixed (`uc2-before.bin`) 4.442 then 4.777. Noise, not a regression.
PASS leftovers.

Memory ceilings: builds 6G (never hit), game runs 2G through decay-flash.sh / run-uc2-shot.sh (never hit), the
frontend build 4G, the harness 7G with the guard at 23 GB (peaks 10.9 and 10.4 GB machine-wide); no exit 137.

Open questions: (1) the shadow pass computing its LOD with the 252-pixel width (UT2004 does the same; on the Xbox
both passes agree because 640 < 820) is left as Epic wrote it; giving the shadow the scene's LOD would save the
second skinning but changes Epic's code beyond UT2004's fix. (2) Native Mesa llvmpipe never drew the pawn's shadow
in these runs (no `shadow` lines), so the regression runs on the GPU (`UC2_GPU=1`); why llvmpipe skips it was not
investigated. (3) The LODTrace test key stays in the engine (read once through -ini:, nothing in the ini); remove it
if the owner prefers.
