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

## Step 1 (2026-10-07): the native change, first pictures

Reference binary: this worktree built unchanged (= main 24c80dc), `bin/capped 6G 3600 -- cmake --build
build-native -j6` rc 0, copied to `run/d2-bin/uc2-main-24c80dc.bin`, run tree `run/d2-ref`
(`tools/dev/mkrun-uc2.sh build-native d2-ref`). Changed binary: run tree `run/d2`.

Code (commit 14d9738; every hunk carries a `D-2` comment):
- `UnCamera.h`/`UnCamMgr.cpp`: `UClient` config `UIScaleMode` (enum `Native640`, `Fit43`) and
  `UIMenuBackground` (`Box`, `Fill`); console `UISCALE [Native640|Fit43] [Box|Fill]` (session only).
  `SDL2Client.cpp`: compiled defaults Fit43/Fill; `TOGGLERENDERLEVEL` (Epic's `UXboxClient` command) for the
  measurements; `UnRender.cpp`: with the level off the frame is cleared to black (nothing else clears it).
- `UnRenderUtil.h/.cpp`: `FCanvasUtil` carries the box (`bUIBoxed`, `UIScale`, `UIOrigin*`, `UITarget*`) and
  `UIContext` (game or menu). C++ only: the script layout of `Canvas` is untouched (no new `UCanvas` data).
- `UnCanvas.h/.cpp`: `Update()` -> `ApplyUIBox()`: with Fit43, `ClipX/ClipY/SizeX/SizeY` = 640x480 (a split
  viewport: 640*ScaleX x 480*ScaleY) and `CanvasToScreen` = scale s, translate to the box's corner, then the
  old target mapping (Epic's half-pixel offset stays one target pixel). Identity (640x480, Native640) takes
  the exact old code. `DrawTile`: a tile covering the whole design space without a transform is a
  full-screen layer: game context -> the whole viewport, stretched; menu context -> `UIMenuBackground` (Fill
  = scaled about its centre to cover the viewport, aspect kept, cropped; Box = stays). Logged once per
  material (`UIScale: full-screen tile ...`). `DrawActorTile`, `DrawPortal`: rect through the box.
  `SetCanvasMatrix`: composed with the box.
- `UnController.cpp`: `WorldToScreen` returns design pixels (may be <0 or >640 outside the box),
  `ScreenToWorld` takes them.
- `UnGUICore.cpp`: `ResX/ResY` = canvas size; menus draw in menu context; the windowed cursor
  (`WindowsMouseX/Y`) and `GetCursorPos` converted to design pixels; the software cursor's deltas divided by
  s and clamped to the whole window in design pixels; `ValidMousePos` accepts the whole window.
- `UnMovie.cpp`: the full-screen movie frame calls `ApplyUIBox(0)` (it lays out in viewport pixels).
- tools: `m10-controls.sh` `M10_RES=<W>x<H>` (ini, Xvfb screen, `M10_UIMAP`), `m10-inject.py` maps the
  scenarios' 640x480 positions into the box when `M10_UIMAP` is set. `tools/dev/d2/menu.scen`.

First runs (`M10_TREE=d2 M10_OUT=logs/d2/t1 tools/dev/m10-controls.sh --file tools/dev/d2/menu.scen <name>`,
with `M10_RES=1920x1080` for the big one, the same on `d2-ref` for "before"):
- 640x480: main menu and arena page drawn as before (to be proved pixel-identical by the script).
- 1920x1080, before (`logs/d2/t1/ref-menu1080-main.png`): the menu 1:1 in the top-left 640x480, its relative
  parts (bottom bars) stretched; after (`menu1080-main.png`): the menu scaled x2.25 in the box 240..1680,
  the teal background (it is the 3D menu level, not a picture: with the level off it is black) fills the
  window, the page's own 2D overlay `T_UI_2.XX_Background` (dark, translucent) covers the window (Fill:
  `UIScale: full-screen tile Texture t_ui_2.Main.XX_Background (menu) covers the viewport, aspect kept`).
  Text at x2.25 is soft but legible.
- The arena step at 1920x1080 failed once: Escape reached the "no profile" box before its opening animation
  ended (`GUIMessageBox handled XC_B` logged before `Unblocking animation`), so it was ignored. Frames are
  slower at 1920x1080 in llvmpipe; the reference run made it by a frame. The d2 scenario now waits for the
  box's `Unblocking animation` line before pressing.

Next: the d2 verification script (all resolutions, menu + arena + DM-Deadbolt, level-off shots, measurements).
