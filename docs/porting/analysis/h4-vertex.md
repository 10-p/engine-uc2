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
