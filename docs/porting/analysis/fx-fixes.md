# Owner review O-5, O-6, O-7 (DM-Azure water pool, DM-Azure waterfall, DM-Praxis pane range): notebook

Branch `fx-fixes`, worktree `../engine-uc2-fx`, run trees `<WorkUC2>/run/fx*`, pictures and logs
`<WorkUC2>/logs/fx`. Brief: PLAN.md, "Owner review of 2026-10-06", tasks O-5, O-6, O-7; the owner's words are in
`<WorkUC2>/answers.txt` (answer 2 "Distortion size" and answer 6). Every run goes through `<WorkUC2>/bin/capped`
(game 2G, builds 6G). The driver (`Source/UC2GLDrv`) is being edited by the M11 web agent at the same time:
every hunk there is itemised at the end of this file for a hand merge.

## The owner's pictures (`<WorkUC2>/temp/`), what each one is

| File | Whose | What it shows |
|---|---|---|
| `AzureWaterDistortion.png` | Xbox (xemu window title) | DM-Azure centre pool, third person: a water surface over the pool floor, reflection of the cobra statue / mega health on it, golden shimmer in the centre, floor seen through it softened |
| `AzureWaterFlat.png` | ours (native, 2428x1786) | same place: the pool floor is sharp and plain, no water layer is visible at all |
| `AzureWaterfallStraight.png` | ours, first person | the waterfall from the cobra's mouth: the stone behind it is not displaced (owner: only while standing still) |
| `AzureWaterfallDistorted.png` | ours, third person | same waterfall, the stone behind it is displaced |
| `PraxisWindowPanesClose.png` | Xbox | DM-Praxis panes from close: the reference for "spot on" |
| `PraxisWindowPanesCloseWeb.png` | ours (web) | same, matches |
| `PraxisWindowPanesDistance.png` | **Xbox** | the panes across the room: no visible distortion. So the Xbox itself loses it at a distance; the owner asks for the limit to be found and raised ("increase the lod of this effect so that it stays farther") |

## Log

### Step 0 (2026-10-06 23:15): reading, first leads

- DM-Azure (`tools/dev/ps-census.py` + an export/import listing of the map): one `FluidSurfaceInfo`, one
  `FluidSurfaceOscillator`, one `PhysicsVolume`, two `FinalBlend`s, one `TexScaler`; imports from `T_Liquid`:
  `PSDistortionMaterial T_Liquid.Downfall.T_Liquid_Downfall_Water01_DIST04`, `Shader
  T_Liquid.Decay.T_Liquid_Decay_Water_C_SD`, `Shader T_Liquid.Water.Liquid_Water_AdvWater_Sd2`.
- The pool is (almost certainly) the `FluidSurfaceInfo`. `AFluidSurfaceInfo::Render` has an `_XBOX` body and a
  PC body (`Source/Engine/Src/UnFluidSurface.cpp` 454-773). The Xbox body writes every vertex colour as
  `FColor(255,255,255,VertAlpha)`; the PC body (`FillVertexBuffer`, 1306-1308) writes `FluidColor.RenderColor()`,
  and `FluidColor` defaults to (0,0,0,0) in `FluidSurfaceInfo.uc`. Lead for O-5: a material that uses the
  vertex colour gets black on PC, white on Xbox. To be confirmed with the map's actual properties and material.
