# M7b: projector batching, three-layer terrain, shadow blur (the Xbox paths M7 left): notebook

Branch `m7b-projectors`, worktree `../engine-uc2-m7b` (from main e4ce0a5), run trees `<WorkUC2>/run/m7b*`, logs and
pictures `<WorkUC2>/logs/m7b`. Scope: the three items of `postprocess.md` "Deferred" (lines ~220, 479-495):

1. projector batching with the 12 `PS_Projector_*` shaders (`UnRenderBatch.cpp` 340-905, `UnRenderStaticMesh.cpp`,
   `UnRenderBSP.cpp`, `SetProjectorMultiMaterial`, `ProjectorAtten.vsh`, `ProjectorDistanceAtten.vsh`,
   `TexSourceTexKill3.vsh`);
2. `PS_Terrain3Layer` (`FRenderCaps::MaxSimultaneousTerrainLayers`, `UnTerrain.cpp`, `SetTerrainMaterial`);
3. the shadow blur `PS_ShadowBlur` (the `_XBOX` branch of `UnShadowProjector.cpp`).

The rule for each: first establish whether the Xbox path draws something visibly different from our PC path (find
real content where it shows, render both ways, compare); implement only then, behind a named capability (never
`_XBOX`), both branches kept, an ini switch to compare. Every run goes through `<WorkUC2>/bin/capped` (game 2G,
build 6G).

## Step 0. Reading Epic's code: what differs between the two paths (2026-10-07)

### Shadows (item 3): the Xbox shadow texture is lighter and sharper

`UShadowBitmapMaterial::Get` (`UnShadowProjector.cpp`) renders the shadow caster into a temporary target and then
filters it into the shadow texture. `NEW_SHADOW_BLEND` is defined at the top of the file, so the two branches are
selected by `_XBOX` alone:

| | Xbox (`_XBOX && NEW_SHADOW_BLEND`) | PC (what we draw today) |
|---|---|---|
| texture size | `UScaledSize` (ShadowDetail: 128/256/512/1024; players: SD_Normal = 256) | always 256 (`#ifndef _XBOX`: "must be powers of 2 on PC, no L8") |
| format | `TEXF_L8` (the class default) | the same format field (we store RGBA8) |
| temp target clear | drawn tile, grey 127 | `RI->Clear` grey 127, alpha 255 |
| caster colour | `(D,D,D,255)`, D = `ShadowDarkness` = 200: the caster is *light* grey | `(127,127,127,255-D)`: the caster is in alpha (55) |
| filter | shadow texture cleared white, one full-size tile through `PS_ShadowBlur`: 4 horizontal taps at -1, -1/3, +1/3, +1 texel, weights 1/6, 2/6, 2/6, 1/6, then **1 - result** | cleared black, 9 tiles (3x3 box, offsets -1.5/0/+1.5 pixels) with `FB_ShadowBlend` (src*srcalpha + dst), vertex alpha 1/9: result = 127 x mean(alpha)/255 |
| value where no caster | 1 - 127/255 = 0.502 (neutral) | 127/255 x 0.988 = 0.492 (1/9 rounds to 28/255) |
| value in the core of the shadow | 1 - 200/255 = **0.216** | 127/255 x 55/255 x 0.988 = **0.106** |

The projection itself is the same formula on both (Xbox `Proj_Modulate_1.psh`: `lerp(0.5, t, atten)` then
DESTCOLOR x SRCCOLOR = 2x modulate; PC `SetProjectorMaterial`: `BLENDDIFFUSEALPHA(texture, TFACTOR 127)` or the
projector cube map's alpha, the same 2x modulate). So the floor under the player keeps **43% of its colour on Xbox
and 21% on PC**, and the Xbox shadow is blurred horizontally only over 1 texel instead of a 3x3 box over 1.5. A
visible difference by arithmetic; to be shown with pictures.

### Terrain (item 2): one pass of three layers instead of three passes

`UTerrainSector::GenerateTriangles` (`UnTerrain.cpp` 419-494) takes `MaxSimultaneousTerrainLayers` from the
device caps on PC (1 here, as at M5); on Xbox it is hard-coded 3. With 3, runs of three opaque layers become one
`RM_CombinedWeightMap` pass drawn by `PS_Terrain3Layer` (`(w1 t1 + w2 t2 + w3 t3) x 2 v0`, one clamp at the end);
with 1, each layer is a pass `min(1, 2 v0 t_i) x w_i` added to the framebuffer (one clamp per layer, one 8-bit
rounding per pass). Equal unless a layer saturates under bright vertex light (2 v0 t > 1): then the Xbox's sum is
brighter in blend zones (concavity of the clamp). Small by arithmetic; the census and pictures decide.

### Projectors (item 1): the batching mimics the PC formulas

Epic's own comment on `SetProjectorMultiMaterial`: "trying to mimic the single pass (SetProjectorMaterial)
functionality as closely as possible". The per-pixel formulas are the same (above). What the Xbox path moves from the
CPU to the vertex shader (`ProjectorAtten.vsh`) is the attenuation; where it can differ from the PC path:

- gradient projectors (`bGradient`): PC multiplies the `GRADIENT_Fade` texture along the projection axis
  (`GradientMatrix`), the Xbox a linear `1 - d / (MaxTraceDistance x DistanceAttenScale)` per vertex. For projectors
  without `bGradient` the script sets `DistanceAttenScale = 1000` (`Projector.uc` PostBeginPlay), i.e. no fade, as PC.
- lifespan fade (decals): Xbox on every surface; PC on BSP and terrain, not on static meshes and actors.
- several projectors on one surface: one pass with `lrp_x2`/`mul` (clamped per projector) instead of one pass each.
- clipping: per pixel (`texkill`, `bClipBSP`) or none (static meshes, whole kDOP leaves) instead of CPU clipping.
- static projectors (`bLevelStatic`): on Xbox `UGameEngine::LoadMap` re-packs each into a `DT_StaticProjectorBatch`
  actor (`AProjector::RebuildStaticPrimsBatch`, `_XBOX` only), drawn with `UProjectorMultiMaterial`.

None of these is a difference in the common case (single, non-gradient projector on BSP). The census below finds
what the maps actually contain.

## Step 1. Census of the maps (`GLSCENECENSUS`, `tools/dev/m7b-census.sh`; 2026-10-07)

`UC2GLSceneCensus.cpp` lists every projector actor of the loaded level (class, place, texture, blending, flags,
the surfaces its geometry lies on) and every terrain (layers, render combinations, passes per method).
`tools/dev/m7b-census.sh` runs it on every map with a projector or a terrain (47 maps; the export tables of all
59 `.unr` say which, `upkg151.py`), three runs at a time, no match started. All census lines:
`<WorkUC2>/logs/m7b/census-pc.txt` (PC paths, the binary before any M7b capability).

- **236 projector actors in 36 maps**, 222 of them level-static (map projectors: wire-mesh and grate light
  patterns, light pools, tracks, vehicle tracks...), 14 shadow projectors (ambient creatures of the ACT maps; the
  players' shadows appear once a match runs), 0 decals at load (they are spawned by weapon hits in play).
  113 have `bGradient`; every one of these has `DistanceAttenScale` 1.0, every other one 1000 (Projector.uc
  PostBeginPlay), so the Xbox's distance attenuation is PC's on every map projector. 88 clip (`bClipBSP` or
  `bClipStaticMesh`). 145 also project on actors (`bProjectActor`). **No map contains Xbox pre-batched
  primitives** (0 `StaticProjectorClippedPrim` exports in the PC packages): the Xbox build batched at load
  (`UGameEngine::LoadMap`, `_XBOX` only).
- Blending: 195 modulate with no material blending (the common case), 16 additive, 2 alpha, 4 "none"; **19
  projectors in 7 maps blend with the surface's own material** (`MaterialBlendingOp` modulate/add/alpha):
  CTF-Recoil 8 (wire mesh, modulate), DM-Meridian 4 (pagoda pattern on static meshes and terrain), OD-Cancer 2,
  OD-Prism 1 (`FX_Shadow_PrismTop`, additive, on 439 BSP nodes), DM-Legacy 2, ACT-SelketCheats 2.
- **43 terrains in 32 maps**; with one layer per pass (PC) every pass is a single-layer weight map pass; 38 of the
  43 terrains have three or more opaque layers.

## Step 2. Shadows (item 3): implemented, numerically checked + seen

Engine: `FRenderCaps::XboxShadows`, `RenDevHasXboxShadows()` (`UnRenDev.h`, `UnRenderResource.cpp`); in
`UnShadowProjector.cpp` the two `_XBOX && NEW_SHADOW_BLEND` sites (caster colour in
`FShadowBitmapRenderInterface::SetMaterial`, the filter in `UShadowBitmapMaterial::Get`) take the Xbox branch at
run time for a device with the capability, the PC branch otherwise; both bodies unchanged (`RI->InsertFence()`
stays `_XBOX`). The PC-only 256x256 size override is left alone (every player shadow is `SD_Normal` = 256 on both).
Driver: `UseXboxShadows` (default True; `-noxboxshadows`/`-xboxshadows`, console `XBOXSHADOWS [0|1]`); the
capability is reported while the `PS_Blur` family is drawn (post-processing on: `PS_ShadowBlur` shares its
handler), so `-nopostprocess` also gives PC shadows.

**Numeric check, end to end** (`GLSHADOWCHECK`, `UC2GLShadowCheck.cpp`): both render targets of every shadow
material are read back after a frame and the shadow texture is recomputed on the CPU from the caster target
with the formula of the branch the engine took. Player shadow (Anubis) in DM-DeepCore, DM-Praxis, DM-Azure, same
numbers each time:

```
branch=pc   caster values (red/alpha:texels): 127/a255:55579 127/a55:9957;  shadow min=27 neutral=126; recomputed: largest error 0.00 of 255
branch=xbox caster values (red/alpha:texels): 127/a255:55586 200/a255:9950; shadow min=55 neutral=128; recomputed: largest error 0.56 of 255
```

So the engine's Xbox branch runs as specified (caster drawn in ShadowDarkness grey 200 on 127; four horizontal
taps at -1, -1/3, +1/3, +1 texel, weights 1/6, 2/6, 2/6, 1/6, negated) and the PC branch too (exactly). The shadow
texture's core is 55 instead of 27 and its neutral 128 instead of 126: after the 2x modulate the floor under the
shadow keeps twice the light it keeps on PC (43% instead of 21% at full attenuation), and the PC's whole projection
square was 1.6% darker than its surroundings, the Xbox's is not.

**Pictures** (`<WorkUC2>/logs/m7b/`, 1280x720, time frozen, the same frame drawn with `XBOXSHADOWS 0` then `1`;
controls: two frames of the same branch 0.5 s apart): `fx-shadow-deepcore-pc.png` / `-xbox.png` (DM-DeepCore, lit
metal floor under the wire-mesh projector, camera above and in front of the pawn), 2x crop
`m7b-shadow-deepcore-pc-xbox-2x.png`, difference x8 `m7b-shadow-deepcore-diff-x8.png`. Mean difference PC vs Xbox
0.305 (5.9% of pixels above 2 levels) against 0.023 / 0.024 between two frames of one branch. What I see: the
shadow of the head, horns and gun on the floor in front of the pawn is a dark, soft silhouette on PC and a lighter,
slightly crisper one with the Xbox filter; nothing else in the frame changes. Per pixel (pawn excluded) the PC
floor is more than 10% darker than the Xbox one in 8,047 pixels and more than 20% darker in 355; the darkest
part of the shadow is under the pawn's body, out of view. The same pairs in DM-Praxis (`fx-shadow-praxis-*`) and
DM-Azure (`fx-shadow-azure2-*`) show the shadow on dark floors, where the difference is hard to see.
**No Xbox reference**: the owner's xemu pictures (`temp/`) show the player but no shadow I can make out on those
dark floors.

## Step 3. Projectors (item 1): the static projector batching, implemented; seen + checked against the PC path

### What the PC path does that the Xbox does not, found on content

Reading on (step 0 listed the candidates), the census and pixel traces (`GLPIXTRACE`) settle it:

- **Nothing on PC draws a `UProjectorMultiMaterial`.** `UnSkeletalMesh.cpp` uses it without a gate
  (`USE_NEW_PROJECTOR_RENDERING_METHOD`), but only in its GPU-skinning branch, which needs `XBOX_ASSET_FORMAT`
  data; PC's software-skinned branch draws `FProjectorRenderInfo::GetMaterial` (checked with a pixel trace on a
  pawn under a DM-DeepCore projector: `ProjectorMaterial`, 3 stages). So the empty PC body of
  `SetProjectorMultiMaterial` never showed.
- **Static (map) projectors are where the two builds differ.** The Xbox build re-packs every level-static
  projector at load into one `DT_StaticProjectorBatch` actor (`UGameEngine::LoadMap`, `_XBOX`;
  `AProjector::RebuildStaticPrimsBatch`), with `BaseMaterial = NULL` (Epic commented the base-material and UV
  copy out of `AddStaticPrimToBatch`), drawn with `UProjectorMultiMaterial`. Consequences, all on screen:
  1. `MaterialBlendingOp` is ignored: the projected texture is drawn alone. PC multiplies it with the surface's
     own texture (`HandleCombinedMaterial(BaseDiffuse)`, then `MODULATE2X`). 19 projectors in 7 maps.
  2. A projector that does not clip (`bClipBSP` off) covers whole BSP nodes on both builds; outside its frustum
     the clamped border texel and the attenuation give the neutral 0.5 on Xbox, but on PC the
     `base x projected x 2` term darkens the whole node (CTF-Recoil: a far floor pixel 51,63,67 -> 46,56,60 by a
     wire-mesh projector's pass, pixel trace).
  3. Base opacity of masked surfaces is ignored by the Xbox (PC keeps the projector off their holes).
- Where none of that applies (no material blending, `bClipBSP`, gradient: the DM-DeepCore wire-mesh lights) the
  formulas are the same and so are the pictures (below).
- Dynamic projectors (player shadows, decals, projectors that follow actors) are not touched: on Xbox they go
  through push-buffer renderers (`FProjectorRenderBatchBSPNodes`, `FProjectorRenderBatchStaticMesh` with kDOP
  node indices, `UnTerrain.cpp`'s `_XBOX` projector branch) whose per-pixel result is PC's
  (`lerp(0.5, t, attenuation)` then 2x modulate, the same attenuation: step 0); their base material is NULL on
  Xbox too, which changes masked surfaces only. Not ported: no visible difference to show for ~400 lines of
  push-buffer code.

### Implementation

- Engine: `FRenderCaps::XboxProjectors`, `RenDevHasXboxProjectors()`; `UnGame.cpp`'s `_XBOX` block in
  `UGameEngine::LoadMap` (the `RebuildStaticPrimsBatch` loop) runs for a device with the capability; the
  emptying of static mesh index buffers that follows it stays `_XBOX` (PC draws static meshes from them).
  `RebuildStaticPrimsBatch`, `RenderStaticPrimsBatch`, the `DT_StaticProjectorBatch` draw list and
  `UStaticProjectorClippedPrim` were never gated: they work as Epic wrote them.
- Driver: `SetProjectorMultiMaterial` is Epic's Xbox body (D3DX vector helpers added to
  `UC2GLD3DXCompat.h`; `TS_WORLD`/`TS_WORLDINV` are the interface's `LocalToWorld`; the 13 `VSC_PROJ_*` registers
  go to a `u_proj[13]` uniform; one `debugf` with no arguments for its two `%s` fixed). `ProjectorAtten.vsh` and
  `TexSourceTexKill3.vsh` in the vertex-shader composer, register for register; the 12 `PS_Projector_*` compiled
  (no longer engine-gated). `UseXboxProjectors` (ini, default True; `-noxboxprojectors`; console
  `XBOXPROJECTORS`, from the next map load: switching a loaded level over was tried and dropped, the static
  actor leaf lists of `ULevel::BuildRenderData` do not follow a projector re-attached at run time).
- Two driver defects found on the way, fixed:
  1. **Deferred occlusion queries dropped the real draw.** M11 made `DrawPrimitive` skip everything between
     `BeginOcclusionQuery` and `EndOcclusionQuery` (meant for the engine's query boxes, drawn with every write
     off); `FOcclusionQueryManager::BeginDeferredQuery` wraps an actor's real draw instead, and projector
     actors use deferred queries (`Projector.uc`), so most batches vanished. Now only passes that write nothing
     are skipped. No other class uses deferred queries (`Pawn.uc` sets it off), so nothing else changes.
  2. **The command-line map loads before the GL context exists**, so caps that depend on compiled shaders read
     "no" at load: the terrain passes and the projector batches were built the PC way for the first map. Until
     `InitPixelShaders` has run the caps now report what the switches ask for (`ShaderStateKnown`); both
     shaders compile on every target (`psh2glsl --gputest`, GLSL ES included).
- Debug: `GLSCENECENSUS` also prints each projector's frustum-plane signs at its middle (positive = inside, what
  `texkill` keeps) and how the renderer sees the actor (filter state, leaf lists, bounds); the projector material
  logs once per projector (`UC2GLDrv: projector material <name>: ... clipped per pixel / not clipped, pixel
  shader N`); `-glprojnoclip` draws clipped projectors with the unclipped shaders; `GLSTATS [RESET]` prints the
  `-glstats` line on demand.

### Pictures and numbers (`<WorkUC2>/logs/m7b/`, separate runs for the two paths: the batching happens at load)

| Place | Projector | PC path vs Xbox batch | Control (two frames of one run) | What I see |
|---|---|---|---|---|
| DM-DeepCore, wire-mesh lights over the metal floor (`fx-m7b-proj-deepcore-*`) | Projector4 (`FX_Proj_WireMesh01_Scale`, modulate, no material blending, `bClipBSP`, gradient) and neighbours | floor, pawn/HUD masked: mean 0.136, largest 4 levels, 0.165% of pixels above 2 | 0.002 | the same warm grid of light on both; nothing to tell apart |
| CTF-Recoil, corridor floor under Projector5 (`fx-m7b-proj-recoil-*`, crop 2x `m7b-proj-recoil-grid-pc-xbox-2x.png`) | `FX_Proj_WireMesh01_Scale`, modulate, **`MaterialBlendingOp` modulate**, not clipped | grid area: mean 9.0 levels, 64.6% of pixels above 8; whole frame mean 5.3 | 0.043 (whole frame) | PC: a dim, dirty grid, the tiles under it darker (base texture x projection x 2); Xbox: a clear grid of light on the tiles, the floor around a few levels brighter (no darkening of whole nodes) |

The DM-DeepCore row is the numeric check of the port against Epic's PC math: the same projections computed two
ways (CPU attenuation per vertex + fixed-function stages, against `ProjectorAtten.vsh` + `Proj_Modulate_Clip`
with `texkill`) agree to 4 levels; a wrong texture coordinate, attenuation or clip plane would move or remove
the grid (it did: the deferred-query defect above removed it, and the clipped shader alone with
`-glprojnoclip` versus without was how that was found). **No Xbox reference:** none of the owner's xemu pictures
shows one of these projectors.

## Step 4. Terrain (item 2): implemented as a switch, off by default: no visible difference

Implementation, driver only (the engine's PC branch already reads the caps; the `_XBOX` parts of `UnTerrain.cpp`
are the packed Xbox vertex format, not needed): `MaxSimultaneousTerrainLayers` = 3 when `UseTerrain3Layer` and
`PS_Terrain3Layer` is compiled; `SetTerrainMaterial`'s `_XBOX` mask constants (c0..c2 select red, green, blue of
the combined weight map) set and the pass drawn by its program; `TERRAIN3LAYER [0|1]` re-runs
`ATerrainInfo::Update` on every loaded terrain (what loading does), so one frame can be drawn both ways.

With the switch on, the census counts 4 + 16 sectors of DM-Azure drawn by one combined pass each (instead of
three weight-map passes), DM-DeepCore's single sector by one combined + one single pass (4 layers).

| Map, view (spawn, time frozen) | one vs three layers per pass, pawn masked | control (two frames) | What I see |
|---|---|---|---|
| DM-Ascension, mossy courtyard ground (`fx-m7b-terrain-DM-Ascension-*`) | terrain area: mean 0.039, largest 2 levels, 0 pixels above 2 | 0.002 | identical |
| CTF-Judgment, grass and rock hills (`fx-m7b-terrain-CTF-Judgment-*`) | mean 0.147, largest 21, 0.034% above 2 | 0.003 | identical to the eye; the difference picture x8 is black but for a few specks |
| DM-Apex, snow seen through a doorway | mean 0.050, 0.081% above 2 | 0.003 | identical |

As step 0 predicted: the two are the same sum, rounded once instead of three times; the Xbox's single clamp
would only brighten blend zones where a layer saturates under strong vertex light, and none of these views has
that. **Decision: `UseTerrain3Layer=False` by default** (the owner's rule: implement where the Xbox looks
different; here it does not), kept as a switch because it saves draw calls (`m7b.sh terrain` prints them) and
because it is the exact Xbox structure.

## Checkpoint, 2026-10-07 22:55 (after a usage-limit stop; everything known so far)

State per item (branch `m7b-projectors`, 7b07076 and the docs commit after it):

- **Shadows (item 3): implemented, default on, numerically checked + seen.** `tools/dev/m7b.sh m7b shadow`
  (desktop, rerun after the criterion change; `logs/m7b/m7b-desktop-shadow2.txt`): `shadow-filter` PASS (PC branch
  recomputed exactly, 0.00/255, core 27; Xbox branch 0.56/255, core 55); `shadow-picture` PASS with the pixel
  measure `tools/dev/m7b-darker.py` (the earlier "mean difference with the pawn masked" criterion failed on a
  rerun because the mask hides most of the shadow): PC frame more than 10% darker than the Xbox frame in 10,744
  pixels (more than 20%: 3,094), the Xbox frame darker than PC in 5,115 (the sharper edge), two PC frames 0.
  Same on the GL4ES twin (`logs/m7b/m7b-twin2.txt`): filter PASS with identical numbers, 10,769 / 1,337 pixels.
  **Shared code with the post-process work:** the Xbox branch draws through `UPSBlurMaterial` with
  `BLUR_SHADOW` (the driver's `SetMatState_PS_Blur`, which selects `PS_ShadowBlur`), four `UTexMatrix` offsets
  and one `FScreenPrimUtil` tile, into the shadow material's own two 256x256 `FAuxRenderTarget`s (not the
  render-target pool, not `FilterCopyRenderTarget`). The capability is reported only while the `PS_Blur` family
  is drawn, i.e. with post-processing on. A change to `FScreenPrimUtil` or the `PS_Blur` handler would reach it.
- **Projectors (item 1): level-static projectors implemented (Xbox batching at load), default on, numerically
  checked against the PC path + seen.** Desktop `m7b.sh` (`logs/m7b/m7b-desktop.txt`, run 1, 11 PASS 0 FAIL):
  census 24/24 and 8/8 batched; projector-agree (DM-DeepCore floor) mean 0.173, largest 4; projector-differ
  (CTF-Recoil grid) mean 9.0; draw calls per frame PC/Xbox 85/81 (DM-DeepCore camera), 81/75 (CTF-Recoil).
  Twin (`m7b-twin2.txt`): agree mean 0.014 largest 3, differ mean 8.7, twin vs desktop pictures mean 1.1-1.9.
  Dynamic projectors stay on the PC path (reason in step 3). Not done: OD-Prism (no stable camera found: the
  pawn falls through the prism roof; its pictures are not comparable), DM-Meridian (static meshes).
- **Terrain (item 2): implemented as a switch, default off (no visible difference).** Desktop and twin
  `m7b.sh terrain` PASS (one vs three layers per pass: largest 2 levels on the DM-Ascension ground; draw calls
  60 -> 56 CTF-Judgment, 53 -> 51 DM-Ascension on the desktop).

What remained at this checkpoint (the gate): Windows and wasm-perf builds; `tools/dev/m7-effects.sh` on a run
tree of mine; `tools/dev/o9-nvidia.sh` (`O9_TREE=m7b-o9`, main's binary as `uc2-before.bin`: main's commit
e4ce0a5 is built from `git archive` in `<WorkUC2>/run/m7b-mainsrc/build-native`); `tools/dev/m11-twin.sh`
(`M11_DESKTOP_TREE=m7b-m11 M11_TWIN_TREE=m7b-m11t`; it now expects 97 fragment shaders and 485 programs); the
pinned DM-Deadbolt `-nopostprocess` picture against main's binary (`pinned-compare.py`); `verifyclasses`.
Claims still from reading only: that the Xbox ignores the base opacity of masked surfaces under a projector
(step 3, point 3: not seen on screen).

## Step 5. Gate runs (2026-10-07, from 22:55)

- `./ucc-bin verifyclasses` (run tree m7b, binary of this branch, capped 2G): exit 0, `All native class layouts
  match` (no script-mirrored class changed: `FRenderCaps` is a plain C++ struct; the device's new members are in a
  native-only class).
- Pinned DM-Deadbolt `-nopostprocess` (`tools/dev/m12/deadbolt-pinned.scen` through `m10-controls.sh --file`, run
  trees m7b-pin = this branch, m7b-pinmain = main's commit e4ce0a5 built from `git archive`): both PASS, same view
  `loc=(1531.9,3111.5,-34.1) rot=(0,-40880,0)`; `pinned-compare.py`: `OK 100.000 91.4 32.95` (static part 100.000%
  within 2 levels); `0 GL errors in 350 frames`. With post-processing off the shadow capability is off (it needs
  the `PS_Blur` family), DM-Deadbolt has no projector and terrain stays one layer per pass by default: nothing of
  M7b is on there, and nothing changed.
- Builds (capped 6G, -j6): native (`[100%] Built target SDLLaunch`, exit 0; `logs/m7b/build-native-final.log`),
  windows (exit 0, `build-windows/Source/SDLLaunch/uc2.exe`, `Source/UCC/ucc.exe`), wasm-perf (exit 0,
  `SDLLaunch.wasm` + `meta.json`).
- `tools/dev/m7-effects.sh m7b-fx` (all 14 effects; `logs/m7b/m7-effects.txt`): every line `critical=0 glerrors=0
  programs_failed=0+0 left_behind=0 tries=1`; praxis-panes A vs the wine reference 2.095 (below 8). The self-test
  did not run in that call (its log folder did not exist yet: a first-run quirk of the script); rerun alone
  (`m7-effects.sh m7b-fx selftest`, `logs/m7b/m7-selftest.txt`): `selftest summary: 11 passed, 0 failed`.
- `tools/dev/o9-nvidia.sh` (`O9_TREE=m7b-o9 O9_NO_MKRUN=1`, "before" = main's e4ce0a5 binary; `logs/m7b/o9-nvidia.txt`):
  combiner-mesa programs / texenv combine3 / combine4 35 passed 0 failed each, combiner-mesa-before reproduces the
  defect (22/11/2) as designed, combiner-twin 33/0/2 skipped, leftovers PASS. DM-Azure pillar and cobra on the
  NVIDIA GPU against Mesa: 0.739 and 0.287 (mean, of 255) with this branch and the same with main's binary;
  Mesa this branch vs main 0.000. The `pictures` line prints FAIL because its criterion expects "before" to be
  the pre-O-9 binary (with main as "before" the two are equal by construction): not a regression.
  combiner-gpu, caps and pictures-other did not start: another agent's headless GPU run was going on (the script
  allows one at a time); rerun below.
- `tools/dev/m11-twin.sh` (`M11_DESKTOP_TREE=m7b-m11 M11_TWIN_TREE=m7b-m11t`; `logs/m7b/m11-twin.txt`): shaders,
  gputest, boot (`97 fragment shaders compiled, 0 failed; 405 vertex shaders`), selftest 11/0 on both bindings,
  **progtest 485 programs on each side, largest difference 1 of 255** (the 12 projector shaders and
  `PS_Terrain3Layer` now among them, on GLES too), pinned PASS, m6 4/4 PASS, effects 13 of 14: liandri FAIL
  (mean 11.4: the overlay's random noise, the known failure of main noted in LOG.md for O-9b). Stopped during
  `fallbacks` at the coordinator's new rule (at most one game run at a time; the script ran desktop and twin side
  by side): `M11_SERIAL=1` added; fallbacks and movies rerun serially below.
- `tools/dev/o9-nvidia.sh` rerun (`O9_TREE=m7b-o9`, after the other agent's GPU run ended; `logs/m7b/o9-nvidia-2.txt`,
  summary `logs/m7b-o9/o9-nvidia-summary.txt`): `o9-nvidia: 10 PASS, 0 FAIL lines` (combiner-gpu-programs 35/0 on
  the NVIDIA Quadro P620, combiner-gpu-texenv-combine4 33/0/2 skipped, combiner-gpu-before reproduces the defect,
  caps PASS, pictures-other PASS: DM-AcidRain NVIDIA vs Mesa before 0.774 after 0.729, DM-Tempest 0.607 / 0.593).
  Main's binary against this branch on Mesa, same session (`fx-<map>-before-*.png` vs `fx-<map>-*.png` in
  `logs/m7b-o9`), cut into an 8 x 6 grid: the differing cells are the animated pawn (its pose at the shot time),
  the torches and the rain particles; outside them DM-AcidRain (0 static projectors) is unchanged (floor corners
  mean 0.00 / 0.12, largest 3) and DM-Tempest (6 static projectors batched) differs on the floor by mean 1.0-1.4,
  largest 5 levels of 255, nothing above 8: the batched projectors' rounding, the size the projector-agree check
  measured on DM-DeepCore (largest 4). Nothing visible.
- `tools/dev/m11-twin.sh` serial rerun (`M11_SERIAL=1`, `logs/m7b/m11-twin-2.txt`): fallbacks 4 of 5 PASS
  (S3TC, texture max level, gl_FragDepth, 32-bit indices); `glnodepthtexture` FAIL "FALLBACK NOT IMPLEMENTED" (the
  known missing fallback of main), leftovers PASS. Movies (`logs/m7b/m11-twin-3.txt`, alone, load ~25): 8 of 9;
  `m9-menu-movie-shots` FAIL (the list region changes between the two shots, PSNR 25.5 dB). The same check fails
  on main's twin runs (`logs/m11t/m9-summary.txt` 2026-10-07 17:05, `logs/o9m11t/m9-summary.txt`) and M7b touches
  neither menus nor movies: not caused here; not rerun (load stayed above 16).

## Result (2026-10-08)

Per item:

- **Shadow blur (item 3): numerically checked + seen (no Xbox reference picture).** Capability `XboxShadows`
  (`RenDevHasXboxShadows()`), setting `UseXboxShadows=True`, `-noxboxshadows`, console `XBOXSHADOWS`. Active only
  while the `PS_ShadowBlur` family is drawn (post-processing on). What changes for every player: the blob of a
  player/bot shadow is lighter in its core (the Xbox caster grey 200 over 127 and four 1-D taps keep 43% of the
  light under the core where the PC path's alpha caster and nine-tile blur keep 21%) and its edge is sharper.
  Seen on DM-DeepCore and DM-Praxis under the pawn.
- **Projector batching (item 1): level-static projectors numerically checked against the PC path + seen (no
  Xbox reference).** Capability `XboxProjectors`, setting `UseXboxProjectors=True`, `-noxboxprojectors`,
  console `XBOXPROJECTORS` (from the next map load). Where it matches the PC path (113 gradient projectors and
  the rest without a material blending op) the pictures agree within 4 levels and the frame has fewer draw calls
  (85 -> 81 DM-DeepCore, 81 -> 75 CTF-Recoil). Where it differs on purpose: the 19 projectors with a
  `MaterialBlendingOp` (CTF-Recoil 8, DM-Meridian 4, OD-Cancer 2, DM-Legacy 2, ACT-SelketCheats 2, OD-Prism 1).
  On CTF-Recoil the corridor floors no longer turn dark under the wire-mesh grid projectors: the PC path
  multiplies the whole unclipped BSP surface, the Xbox path only the projected texture (mean 8.7-9.0 levels over
  the corridor view, 65% of its pixels above 8). Pictured only on CTF-Recoil; the other five maps not.
  Dynamic projectors (weapon effects, the 14 shadow projectors): not changed, the push-buffer renderers they use
  on the Xbox give the same per-pixel result as the PC path.
- **Three-layer terrain (item 2): implemented as a switch, off by default: no visible difference.** Setting
  `UseTerrain3Layer=False`, `-terrain3layer`, console `TERRAIN3LAYER`. One pass of three layers against three
  passes: largest 2 levels of 255 on the DM-Ascension ground, a few draw calls fewer. Left off because it gains
  nothing anyone sees.

Engine changes are behind the capabilities: a device that does not report them (UC2GL4ESDrv without the
shaders, the Windows/D3D paths, `-noxbox*`) runs Epic's PC branch unchanged (pinned DM-Deadbolt
`-nopostprocess` 100.000% against main). `_XBOX` is not defined; both branches stay in the sources.
Driver fix that came with it: `DrawPrimitive` inside a deferred occlusion query now draws (it skipped every draw
in a query, which also dropped the real draws of `bUseDeferredHWOcclusionTests` actors; only draws with colour
and depth writes off are skipped now).
