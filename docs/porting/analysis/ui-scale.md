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

## Step 2 (2026-10-07): menus clipped to the box, sharper text, the verification script

- **The menu level is 3D.** The teal background of every menu page is the menu map rendered behind the GUI
  (with the level off it is black); the page's own 2D layer is `T_UI_2.XX_Background`, dark and translucent.
  So the world behind the menus fills the window whatever the setting; `UIMenuBackground` decides only whether
  that dark layer covers the window (Fill: aspect kept, cropped top/bottom on wide windows) or the box (Box).
  Both are taken in every run (`menu-<page>.png` Fill, `menu-<page>-box.png` Box). Fill is the default: with
  Box the box's edge shows as a brightness step over the moving background.
- **Clipping.** At 4K the arena page's "ARENA" header bar ran past x=640 into the pillar area (on the Xbox
  the 640x480 screen cut it). What the GUI's pages draw is now clipped to the design area in
  `UCanvas::DrawTile` (UVs adjusted, flips handled by normalising negative sizes); the controller switches the
  context back to game before the cursor and the designer frames, so the cursor still goes over the window.
  The HUD is not clipped (its markers on actors may be outside the box).
- **Sharper text** (`UISharpText`, default True): text keeps the 640x480 font's layout and takes each whole
  glyph from the smallest bigger size of the same family in the font's package that gives >= 1 texel per
  screen pixel (`T_Fonts2.SuiGeneris_<N>pt`: 12 18 20 24 28 40), or the biggest. Families are found from the
  package's export table once per font (logged: `UIScale: sharper text for ...`), the fonts kept in the root
  set. Clipped glyphs and the underline keep the small font. Not built/tested yet at this entry.
- **Reference binary for the 640x480 identity.** The menu level is a camera flight on wall-clock time, so two
  runs of main's binary agree on only 41-55% of the pixels of a menu picture (measured: `ref-menu` vs
  `ref2-menu`, 640x480). The UI has to be compared on black, which main's binary cannot do. So the reference
  is main's source plus only the measurement switch: `git archive 24c80dc` into `run/d2-refsrc`,
  `logs/d2/refplus.diff` applied (the TOGGLERENDERLEVEL command and its black clear, nothing else), built with
  `cmake --preset native` (FetchContent sources pointed at this worktree's `build-native/_deps`, disconnected)
  and `bin/capped 6G 3600 -- cmake --build build-native -j6 --target SDLLaunch` (rc 0). It is `uc2.bin` of
  run tree `d2-ref`; main's own binary is `uc2-main.bin` there (pinned pictures).
- **Verification**: `tools/dev/d2-ui-scale.sh` (runs) + `tools/dev/d2-measure.py` (checks). Per resolution
  (640x480 1280x720 1920x1080 2560x1440 3840x2160 1280x1024 1920x1200 1024x1280): `tools/dev/d2/menu.scen`
  (main menu and arena page: Fill/Box, level on/off) and `tools/dev/d2/deadbolt.scen` (pinned DM-Deadbolt,
  HUD level on/off). Extras: the reference at 640x480 twice (menus, HUD), main's binary pinned with and
  without post-processing, before pictures at 1920x1080, `M10_INI="UIScaleMode=Native640"` at 1920x1080, the
  M10 mouse scenario at 640x480 and 1920x1080. Checks (PIL only, no numpy here): UI bounding box on black vs
  the computed box (Box mode); the box scaled back to 640x480 vs the 640x480 picture (mean difference and
  the best alignment within +-2 px); the title and a menu item located within +-6 px; Fill/Box outside the
  box; HUD bounding box = the 640x480 one scaled; reticle centre vs the screen centre; identities at 640x480;
  the switch; mouse; logs. m10-controls.sh gained `M10_INI`.
- First measurements, 640x480 HUD on black: bounding box (38,32)-(597,418); the reticle (Nakhti race
  weapon's scripted crosshair, `DrawX=320 DrawY=240`) centre (319.5, 240.0): half a design pixel left of the
  centre, which is 2.25 px at 4K (s=4.5) if the art scales as drawn.

## Step 3 (2026-10-07): sharper text built and looked at; the first full pass and what it taught

- Sharp text, first try: every glyph an empty square at 4K. `SuiGeneris_40pt` has only 12 of the 95
  printable characters (digits for the HUD's big numbers), so `RemapChar` gave the null character. Now each
  glyph tries the sizes going down from the preferred one and takes the first that has the character with a
  box of the same shape (within a third); the log lists what each size has: `UIScale: sharper text for
  T_Fonts2.SuiGeneris_12pt: 18pt(95/95) 20pt(95/95) 24pt(95/95) 28pt(95/95) 40pt(12/95)`. At 4K the menu text
  then comes from the 28pt glyphs: visibly crisper, same layout (`logs/d2/t5/menu2160-arena.png` against
  `logs/d2/t1/menu2160-arena.png`; crops compared side by side). Small kerning differences (the 'f' of
  "Profile" sits a little further right) are the only visible change. Xbox button glyphs
  (`Controller_Buttons_LG`) have no bigger size and stay magnified.
- First pass of all resolutions (before sharp text): every menu run passed; the HUD runs at 2560x1440 and
  3840x2160 failed to spawn: at under one frame per second (49 frames in the whole 2560x1440 run, software
  GL) the 0.2 s Fire press and its release fell between two frames. `exec Fire` through the test channel did
  not restart the player either (not investigated further); the scenarios now hold Fire for 2 s.
- My mistake: I ran a short 640x480 test in the `d2` tree while the 4K run of the full pass used that tree.
  `m10-controls.sh` resets the tree before a run (inis, `Shot*.bmp`), so the 4K menu pictures of that pass
  were lost. Nothing else is shared between trees; the gate regressions use their own tree `d2-reg`. Never
  two runs in one tree.
- `./ucc-bin verifyclasses` on `run/d2-reg` (this build): `All native class layouts match`, rc 0 (no
  script-visible class changed: the new state lives in `FCanvasUtil` and `UClient`, which are C++ only).
- Started: the full verification (`tools/dev/d2-ui-scale.sh`, output `logs/d2/d2-ui-scale-full-1.txt`) and the
  wasm-perf build (`logs/d2/build-wasm-perf.log`).

## Step 4 (2026-10-07): builds, the browser harness, the reticle's own asymmetry

- **wasm-perf**: `source env.sh && bin/capped 4G 1200 -- cmake --preset wasm-perf` rc 0, `bin/capped 6G 5400 --
  cmake --build build-wasm-perf -j6` rc 0 (`[100%] Built target SDLLaunch`, `logs/d2/build-wasm-perf.log`).
  **windows**: `bin/capped 4G 600 -- cmake --preset windows` rc 0, `bin/capped 6G 3600 -- cmake --build
  build-windows -j6` rc 0 (`uc2.exe`, `ucc.exe`; `logs/d2/build-windows.log`).
- **Browser spec without touching the frontend.** The frontend's Playwright config picks specs by a fixed
  list per target (`<id>-wasm-boot/net/benchmark/proxynet/audio.spec.js`), so a new spec there would mean
  editing it. The D-2 spec lives here instead: `tools/dev/d2/web/uc2-d2-uiscale.spec.js` (requires the
  frontend's `lib/` by path) with `tools/dev/d2/web/playwright.d2.config.js` (the frontend's config, this
  folder's spec, output in `logs/d2/web`), run as `node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=N --
  --target uc2 --config <engine-uc2>/tools/dev/d2/web/playwright.d2.config.js` from the frontend, after
  `yarn build --target uc2 --engine-artifact <engine-uc2-d2>/build-wasm-perf/Source/SDLLaunch` (rc 0; it
  assembles the frontend's `serve-web/` with this build: rebuild it before another engine's run).
  First try: "No tests found" (the fixed list); second: the memory guard stopped it at 12.1 GB machine-wide
  (9.6 GB in use before the browser started: the desktop, other agents' census, my Windows build). The
  WebGL renderer was the NVIDIA GPU (`ANGLE (NVIDIA, Vulkan 1.4.312 (NVIDIA Quadro P620))`). Next try with
  the machine quieter and `--max-gb=14` (machine-wide; under the owner's 16 GB for one piece of work).
- **Reticle**: measured centre at 640x480 (319.5, 239.5); the crosshair's parts (`Crosshair_NakhtiRW`) are
  laid out around x=62.5 of a 128-wide element anchored at its centre, so the art itself sits about 1.5
  design pixels left of its anchor (rounding at 640x480 shows half a pixel). Scaled up, the offset grows:
  -2.0 px at 1920x1080, -2.5 px at 2560x1440. The check now compares with where the 640x480 reticle maps
  (+-max(2, s/2+1) px) and states the literal distance to the screen centre; the literal "within 2 px" holds
  up to 1920x1080 and not above, by the art's design, not by the mapping (the box's centre is the window's
  centre exactly: origin floor((W-640s)/2) is a whole number at every listed size).
- 4K HUD: about 2.5 s per frame in software GL with the builds running; the 2 s Fire hold was still lost.
  The scenarios now repeat a 3 s Fire until a probe shows the pawn (`seek 8 P['_'].pawn==1 ;; key Alt_L 3`).
