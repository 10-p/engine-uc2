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
