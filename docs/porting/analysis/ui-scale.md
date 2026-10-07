# D-2 notebook: UI scaling (menus and HUD in a centred 4:3 box)

Task (PLAN.md Phase D, D-2; the owner's addendum of 2026-10-06): "Most urgent is the UI scaling, it is a disaster
beyond 480p, where the menu is very distorted, but worse, the in game ui stays in the corner, including the
reticle! We need to make the UI to scale to the 4:3 of fullscreen and be centered." Menus and the in-game HUD,
reticle included, drawn in a centred 4:3 area scaled to the window height (or width, when the window is
narrower than 4:3); the 3D world keeps filling the window. Worktree `engine-uc2-d2`, branch `d2-ui-scale`
(from main 24c80dc). Every run through `bin/capped`; run trees `run/d2*`, logs `logs/d2/`.

## Step 0 (2026-10-07): how the 2D drawing reaches the screen today

Read: `Source/Engine/Src/UnCanvas.cpp`, `Engine/Classes/Canvas.uc`, `UnRenderUtil.cpp` (FCanvasUtil),
`UnRender.cpp` (FPlayerSceneNode::Render, FInteractionRenderSceneNode), `UnController.cpp`
(WorldToScreen/ScreenToWorld), `Source/GUI/Src/UnGUICore.cpp` (controller, mouse, relative positions),
`UnGUI.cpp` (page background), `UCGameplay/Classes/UCHUD.uc`, `UCWeapon.uc`, `UCElements/Classes/*`,
`UnMovie.cpp` (the port's full-screen movie frame), `UnSplitScreenMgr.cpp`.

One pipe for every 2D draw of the game: script (`Canvas.DrawTile/DrawText/...`) and the GUI's C++ go through
`UCanvas::DrawTile` / `DrawString` into the viewport canvas's `FCanvasUtil`, whose `CanvasToScreen` matrix
(built in `UCanvas::Update()` from `Viewport->SizeX/SizeY`, once per frame before the HUD and the interactions)
maps canvas pixels to clip space. `Canvas.ClipX/ClipY/SizeX/SizeY` are set there too, from the viewport.
Camera effects, post-processing, shadows, scripted textures and the editor build their own `FCanvasUtil`
(never the viewport canvas's), so they are not touched by anything done to the canvas's matrix.

What the scripts and the GUI do with that:
- **HUD** (`UCHUD`, `UCHUD.cpp`, the `CanvasElement` tree of `UCElements`): positions are 640x480 design
  pixels (`pos_*` structs, `DrawX/DrawY`), `UCHUD.uc:2` "tile images are not currently scaling, but are
  designed for 640x480 only"; `ScreenWidth/ScreenHeight = Canvas.SizeX/SizeY` (`UCHUD.uc:916`). Hence the
  HUD in the top-left corner at higher resolutions.
- **Reticle**: the scripted crosshairs (`Crosshair_*` elements) sit at `DrawX=320 DrawY=240` (design pixels:
  top-left quarter at 1920x1080, the owner's "reticle in the corner"); the plain material crosshair
  (`UCWeapon.DoDrawCrossHair`) uses `0.503*(ClipX-w), 0.504*(ClipY-h)` (follows the resolution, but is
  1.9 px right/down of the centre by design at 640x480). The combo menu (`UCHUD.cpp:879`) uses
  `SizeX*0.503`.
- **3D-anchored items**: `PlayerController.WorldToScreen` (native, real pixels) is used by the lock-on
  reticle (`Element_TargetReticle`), the reflect-icon on projectiles (`UCHUD.DrawExtra`), and the Sonar
  combo's blips (`UCCombo_Sonar`). No script text is drawn at projected positions.
- **Full-screen 2D effects in script**: `UCPlayerController` blood flash (`DrawTile(ScreenBloodMat, ClipX,
  ClipY)` at 0,0) and `ColorOverlay` (`DrawTile(WhiteTexture, SizeX, SizeY)`). The directional hit indicators
  (`HitIndicationPosition`) are 640x480 design positions at the screen edges (2D, not projected).
- **Menus** (GUI): a component's position/size below 2.0 is relative and multiplied by
  `GUIController.ResX/ResY` = `Viewport->SizeX/SizeY` (`UnGUICore.cpp:1530`, `:3275-3460`): relative layouts
  stretch to the window (the owner's "very distorted" at 16:9), absolute ones (most UC2 pages: `WinLeft=38
  WinTop=28 ...`) stay 1:1 in the top-left. Page background: `UC2PageBase` draws `T_UI_2.XX_Background`
  with `ISTY_Scaled` over the page bounds (`UnGUI.cpp:294-312`). Fonts: every `GUIFont` of UC2 lists one font
  for all resolutions (`fntSui*`), `GetFont(XRes)` picks by `XRes` only among identical entries; the HUD uses
  `StaticGetAvarisFont` (fixed SuiGeneris 12/18/24/28/40 pt). Nothing in UC2 picks fonts by resolution.
- **Mouse in the menus**: windowed, `bShowWindowsMouse` (= `bWindowsMouseAvailable`, not fullscreen) and the
  GUI reads the OS cursor (`WindowsMouseX/Y`, window pixels, `UnGUICore.cpp:1735`, `GetCursorPos`); full
  screen, a software cursor accumulates the mouse deltas (`MouseX += dX*MenuMouseSens`), is clamped to
  `Canvas->SizeX/SizeY` and drawn by the controller (`UnGUICore.cpp:1690-1730`). Hit-testing compares
  `MouseX/MouseY` with the components' `Bounds` (canvas pixels).
- Movies: `UnMovie.cpp DrawMovieFrame` (ours) draws in real pixels through the viewport canvas, keeping the
  movie's aspect.
- Split screen (E-1, later): split viewports have `OffsetX/OffsetY`, `ScaleX = SizeX/InitSizeX`; their HUDs
  are `UCHUD_*_Split` with `SplitScaleX/Y`.

Plan (native, one place): in `UCanvas::Update()`, when the new client setting `UIScaleMode=Fit43`, the canvas
presents the 640x480 design space to everyone (`ClipX/ClipY/SizeX/SizeY` = 640x480) and its `CanvasToScreen`
maps it into the centred box (scale `s = min(W/640, H/480)`, offset `((W-640s)/2, (H-480s)/2)`). At 640x480
that is the identity, so the regression anchor holds by construction. Then the few things that must not be
boxed or that speak real pixels are converted at their one native entry point:
`WorldToScreen/ScreenToWorld` (design units in/out), `DrawActorTile`/`DrawPortal` (rect through the box), a
tile that covers the whole design area (full-screen fades, blood, overlays) goes to the whole window, the
page background of the menus is a setting (box or fill, both shown), the GUI's `ResX/ResY` and mouse are in
design units (cursor over the whole window), the movie frame keeps real pixels. Scripts unchanged unless a
case appears that native code cannot express.

Next: reference pictures from the unchanged binary (640x480 menu, arena page, pinned DM-Deadbolt; and the
same at 1920x1080 for "before"), then the canvas change.
