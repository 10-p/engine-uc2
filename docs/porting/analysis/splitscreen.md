# Split-screen on PC (E-1)

> 2026-10-11 (T4): the script sources this notebook refers to (`Source/<Pkg>/Classes`, `.ucproject`) left the
> repository; Epic's text is `<WorkUC2>/epic-sources/<Pkg>/Classes` (dev machine only), the build's base is the
> decompiled `build-*/Scripts/base`, our changes are `patches/` (`scripts-removed.md`).

Running notebook of task E-1 (PLAN.md, "Owner review of 2026-10-06", Phase E), appended after every step so a
successor can continue. Worktree `/home/ion/WorkUC2/engine-uc2-e1`, branch `e1-splitscreen` (from main 4813dd9).
Run trees `/home/ion/WorkUC2/run/e1*`, logs and pictures `/home/ion/WorkUC2/logs/e1/`. Every run goes through
`/home/ion/WorkUC2/bin/capped` (game 2G, builds 6G -j4). Other agents at the same time: `engine-uc2-jit` (O-11,
`UCPlayerController` third-person camera `MouseLookCamera`/`CalcBehindView`, Engine `FRAMETRACE`) and
`engine-uc2-e4` (language option). I stay out of their functions.

File:line references are to this tree unless they say `epic-sources/`.

## Step 0 - the map: what exists, what is gated (2026-10-08)

### Engine (shared code, no `_XBOX` gate on the manager itself)

- `Engine/Src/UnSplitScreenMgr.cpp` (Scion's `USplitScreenMgr`, compiled on every target): `CreateSplitPlayer`
  (spawn a player and a viewport for a gamepad), `CreateSplitViewport`, `SetupFirstView` (the first split view IS
  `Client->Viewports(0)`), `SetupOverlayViewport` (a NEW full-window viewport, `OffsetZ=-100`, that draws only the
  menus: `bShowMenu=1`, `bShowScene` only in the menu level; its actor is `ini:Engine.Engine.OverlayCameraClass`,
  `UCGameplay.UCOverlayCamera` in `DefaultXbox.ini:47`, NOT in our `Default.ini`), `RemoveSplitViewport`,
  `ResetState`, `MatchSplitViewportsToActors` (after every map load: back to one viewport in the menu level,
  otherwise spawn a player per split view), `UpdateDisplayFlags`, `UpdateSizes` (layouts below), `UpdateDetailLevels`
  (`SSDetailLevels` per player count, `DYNAMICGAMMA 0` while split, `ScaleFOVX/Y = ReduceFOV`).
- Layouts (`UpdateSizes`, sizes from `Client->GetInitScreenSize`): 2 = top/bottom halves; 3 = top half + two bottom
  quarters; 4 = quadrants; `BorderThickness` (0) inset; `ScaleX/ScaleY = Size/InitSize`. `SS_SPlit_Vertical` "not
  used". Note an Epic typo: `OffsetX` is set twice and `OffsetY` never for the overlay and for the single view (case
  0/1): `OffsetY` keeps whatever it had (to check when going back to one view).
- `SplitScreenMgr.uc`: `MaxSplitScreens=4`, `BorderColor` black, `SSDetailLevels` 0..3 (2+ players: no coronas,
  blob shadows, lower DetailMode, particles x0.6/0.4/0.2; 3-4 players `ReduceFOV=0.5`); defaultproperties, not config.
- `UClient` (`Engine/Src/UnCamMgr.cpp`): `Init` creates the manager; `AllowControllerJoin` (GUIController's script
  event); static `BeginSplitScreen(Client, GamepadID)` = `AllowControllerJoin` + `CreateSplitPlayer(NULL, id)`
  ("moved from XBoxClient.cpp ... for debugging purposes on PC"); console `SPLIT ADD [ID=n]` / `SPLIT REMOVE`: the
  `_XBOX` branch calls `BeginSplitScreen`/`RemoveSplitViewport`, the PC branch only halves viewport 0's height (a
  leftover hack); `ZSortViewports` sorts all but `Viewports(0)` by `OffsetZ` (overlay last).
- `UGameEngine::Draw` (`UnGame.cpp:3149`): `bShowScene` -> the world (and audio/voice update when the viewport also
  has `bShowMenu`, i.e. it is the only one); `bShowMenu` only -> `FInteractionRenderSceneNode` (menus) and the audio
  update; "Present the last frame" at the start of a draw when `PendingFrame`; `PendingFrame = Blit`.
- `UnGame.cpp:1147-1176` (after a map load): players for every viewport, then `MatchSplitViewportsToActors`;
  `:1573` `NotifyLevelChange`.
- `UnLevAct.cpp:913-921` `SpawnPlayActor`, `#ifndef _XBOX`: "For development on the PC, fake the GamepadID" -> EVERY
  spawned viewport gets GamepadID 0. Breaks split screen after a travel (all split views would read pad 0).
- `UnController.cpp:686` `GetSplitscreenLayout` (index in `Client->Viewports`, count = all viewports incl. the
  overlay); `:726` `IsSplitScreen` (= in `ActiveSplitViews`). `PlayerController.uc:1085-1122`: with more than one
  viewport, `ClientSetHUD` spawns the `<HUD>_Split` class and calls `SetSplitPosition`.
- Rendering in the engine already reads the viewport rectangle: `FCameraSceneNode::UpdateMatrices`
  (`UnRender.cpp`) scales camera space by `ScaleX/ScaleY` and the aspect by `ScaleX/ScaleY` (Epic's Xbox maths; to
  verify the picture's proportions), `ScaleFOVX/Y`; `UnPixelShaderMaterial.cpp:822` screen-space matrix with
  `OffsetX/ScaleX`; `UnRenderUtil.cpp:2458` back-buffer copy UVs from the viewport offset; `UnCanvas.cpp:1315-1378`
  `DrawPortal` keeps offsets.
- D-2 already planned for E-1: `UCanvas::ApplyUIBox` (`UnCanvas.cpp:1009`) gives a split view the design space
  `640*ScaleX x 480*ScaleY` fitted into its own rectangle. D-8: `ufrontAutoFovSize` (`UnRender.cpp:1802`) uses the
  whole window's aspect for split views (`SizeX/ScaleX x SizeY/ScaleY`), because the Xbox layouts were designed on
  the 4:3 screen they divide.

### Client/viewport (XboxDrv = spec, SDL2Drv = ours)

- Xbox (`epic-sources/XboxDrv/Src/XboxClient.cpp`): `Tick` = `DetectDevices` (hot-plug), `UpdateGamepadUnboundInput`
  (Start on an unbound pad, `AllowControllerJoin()`: bind it to a viewport with no pad, else `BeginSplitScreen`;
  other buttons to the interaction master only), then repaint every viewport that is not `bDisableDraw`, with
  `Blit` only for the FIRST drawn one ("only do blit (present) on last viewport": that viewport presents the whole
  back buffer of the previous frame at the start of its next draw); `GetInitScreenSize` = the fullscreen size.
  `XboxViewport.cpp`: `OpenWindow(ParentWindow!=0)` shares `GRenDev` and creates nothing; `IsFullscreen` = size equals
  the init screen size; `UpdateInput` syncs `GamepadID` with `Actor->GamepadID` and reads only the viewport's pad.
- Ours (`Source/SDL2Drv`): one SDL window per viewport (`OpenWindow` always creates one), `Tick` repaints every
  viewport with Blit, `IsFullscreen` = `BLIT_Fullscreen`, `GetInitScreenSize` = Windowed/FullscreenViewportX/Y,
  `UpdateInput` returns early without a window, keyboard/mouse events go to the window's viewport (`FindViewport`),
  `SDL2Pads.cpp`: bound pads read per viewport (`UpdateBoundPad`), unbound pads -> interaction master; header says
  "Start on an unbound pad does not join a split-screen player" (left for the owner's decision, now taken).

### Renderer (UC2GLDrv)

- `UC2GLRenderDevice.cpp:2161` `Lock`: re-creates the main framebuffer when the VIEWPORT's size differs from it
  (would thrash with sub-viewports); `Present` draws the present pass with the viewport's size; `ReadPixels` (SHOT)
  reads the viewport's size from (0,0).
- `UC2GLRenderInterface.cpp:147` `Locked` sets the GL viewport to `OffsetX/OffsetY/SizeX/SizeY` (Y flipped for GL)
  with scissor; `Clear` honours the scissor (= D3D's viewport-rect clear).
- `UC2GLPostProcess.cpp`: the Xbox paths ported in M7 read `OffsetX/OffsetY/SizeX/SizeY/ScaleX` as Epic does
  (back-buffer copy :776-790, :881-896); "nothing was run with more than one viewport" (postprocess.md:509).

### Audio (UC2ALAudio)

- XACT spec `XACTAudioSubsystem.cpp:2133-2370`: a cue is 2D for the viewer whose controller/view target owns it,
  else 3D for the closest viewer; split screen through `MakeRelativeToViewport` (one real listener).
- Ours (`UC2ALAudioSubsystem.cpp:1366-1400`, `:933-990`): same choice over `Client->Viewports` (skips non-player
  actors, so the overlay camera is skipped if it is not `bIsPlayer`), each source placed in its viewer's frame.
  audio.md: "split screen: implemented, never exercised". `Audio->Update()` runs from the one viewport with
  `bShowMenu` (the overlay in split screen), so once per frame.

### Scripts: what UC2 itself offers

- `Core/Classes/Object.uc:219` `const MAX_VIEWPORTS = 2; // max number of viewports that can be visible at once`.
  `GUIController.AllowControllerJoin` refuses beyond `MAX_VIEWPORTS` child viewports; arrays sized by it in
  GUI (`GUIComponent.LastFocusedControl`, `GUIMultiComponent.FocusedControl`: native-mirrored classes) and UCMenu
  (`UC2Arena_Character` two players: preview, cursor, "press start to join" labels; `UC2Arena_DualArmory` two panels).
- `UCHUD.SetSplitPosition` (`UCHUD.uc:2839`): "if we are going to support more than 2 split-screen, this function
  needs to be adjusted" (top/bottom only: `SplitScaleY*0.5`, safe-zone offset per index 0/1).
- Pages that allow a join (`AllowsControllerJoin`): `StartMenu` (the title "press start" page) and
  `UC2Arena_Character` (the character page of Instant Action/Arena, unless in an Xbox Live area). Everything else
  inherits `GUIPage.AllowsControllerJoin() = false`.
- So **UC2 as shipped is a two-player split-screen game** (top/bottom). The engine's 3- and 4-player layouts are
  Scion's generic code, reachable on the Xbox only through the `SPLIT ADD` development command.

### Decisions taken here (to be confirmed by the owner, see the end)

1. Players 2..4: the engine and driver support four viewports (Epic's layouts); UC2's menu flow (Start on an unbound
   pad on the title page or the character page) gives two players exactly as on the Xbox. Players 3 and 4 need the
   development path (`SPLIT ADD ID=<port>`, now really creating a player on PC as on the Xbox, and allowed in a
   match). Extending the menus and the HUD to four players is script and UI design work: owner question.
2. Keyboard and mouse: see step 2 (decided there with the code in front of me).

Next: notebook committed; then the measurement harness (`tools/dev/e1-splitscreen.sh`), then the driver.

## G-6, step 1 - the map refreshed against main 4a72bfb (2026-10-10)

G-6 continues E-1 here: worktree `/home/ion/WorkUC2/engine-uc2-g6-splitscreen`, branch `g6-splitscreen` (from main
4a72bfb). Run trees `/home/ion/WorkUC2/run/g6*`, logs and pictures `/home/ion/WorkUC2/logs/g6/`. Everything runs
through `bin/capped` (game 2G, builds 6G `-j4`, link 6G). Other agents meanwhile: g5-gamepad (frontend and maybe
SDL2Drv pad code for the web), g4-language (UC2ALAudio bank selection, UC2Movie), two frontend agents. My SDL2Drv
edits stay in the viewport/split-screen parts and are listed at the end of this notebook.

What changed since step 0 and matters here:
- F-1: Epic's shipped `GUI.u`/`UCGameplay.u`/`UCMenu.u` must play as shipped, so the 2-player limit of the scripts
  (`Object.uc:219 MAX_VIEWPORTS=2`, `GUIController.AllowControllerJoin`) is a fact the engine adapts to.
- F-4b: no command-line switches; tests set ini keys with `-ini:` (`[SDL2Drv.SDL2Client] InputTestFile=`,
  `[UC2Movie] StartupMovies=False`, `[Engine.Engine] FrameRateCap=`). The harness uses the same.
- F-3: WebGL 2 through GL4ES, 4x MSAA everywhere (`ResolveMain` before any read of the main framebuffer).

Facts read this time (file:line in this tree unless `epic-sources/`):
- Presentation (`UnGame.cpp:3221` `UGameEngine::Draw`): a viewport drawn with `Blit=1` gets `PendingFrame=1`
  (`:3486`) and calls `Present()` at the start of its NEXT draw (`:3283`). The Xbox client
  (`epic-sources/XboxDrv/Src/XboxClient.cpp:490-511`) repaints every viewport that is not `bDisableDraw` and gives
  `Blit` to the first one only: that one presents the whole back buffer of the previous frame. Ours
  (`SDL2Client.cpp:303`) gives every viewport `Blit=1`.
- GL device: `Lock` re-creates the main framebuffer whenever the VIEWPORT's size differs from it
  (`UC2GLRenderDevice.cpp:2158`) and `SetRes` creates it with the viewport's size (`:803`): with sub-viewports both
  must use the window's size. `DrawPresentPass` (`:1586`) already draws the whole main texture into the whole
  drawable (only `GetWindow()` is taken from the viewport); `ReadPixels` (`:2308`) reads the viewport's size from
  (0,0). `FOpenGLRenderInterface::Locked` (`UC2GLRenderInterface.cpp:147`) already sets GL viewport + scissor to the
  viewport rectangle; `Clear` honours it.
- Dynamic gamma (`UC2GLPostProcess.cpp:1071-1400`, Epic's `FD3DDynamicGamma`): ONE luminance histogram of the WHOLE
  back buffer (UV 0..1) per render interface, and ONE ramp applied by the present pass to the whole window. Epic's
  split screen switches it off (`UnSplitScreenMgr.cpp:1043-1049` `DYNAMICGAMMA 0`: "a full screen effect"). The
  owner keeps dynamic gamma on (2026-10-09), so on PC it has to become per viewport: histogram of the viewport's
  rectangle, ramp applied per rectangle in the present pass (planned, step 6).
- Camera effects with `FullscreenOnly` are skipped on a viewport that is not "fullscreen" (`UnRender.cpp:2321`,
  `:3090`); no script class sets `FullscreenOnly=true` (only `CameraEffect.uc:46` default false), so nothing is lost.
- `GetSplitscreenLayout` (`UnController.cpp:686`) counts every client viewport, overlay included (3 with two
  players); `ClientSetHUD` (`PlayerController.uc:1083`) only needs `count > 1` to pick `<HUD>_Split`.
- `SPLIT ADD` on the Xbox goes through `BeginSplitScreen` -> `AllowControllerJoin` (GUI) like the pad's Start, so
  even the development command stops at two players there. Three and four players are Scion's engine layouts that
  UC2's scripts never reach.
- How a split player leaves in UC2: back to the main menu (`UC2MainMenu.uc:32,98` `ResetSplitScreen` ->
  `UnGUICore.cpp:2022` -> `ResetState`), or a map change to the menu level (`MatchSplitViewportsToActors`), or
  `SPLIT REMOVE` (removes the last one; `UCHUD.ToggleSplitscreen` uses it). An unplugged pad is `IK_JoySlider2` to
  its viewport (the HUD's "controller unplugged"), the player stays.
- Profiles: `CreateSplitPlayer` builds the URL from `Viewport->Profile` when the viewport has one
  (`UnSplitScreenMgr.cpp:523-531`, `GSI->eventGetSpawnURL`), else `[DefaultPlayer]`.
- The test channel (`SDL2InputTest.cpp`) has ONE virtual pad and probes `Viewports(0)` only: it needs pad indices
  and a per-viewport probe for this task.

Plan (each a commit + a notebook entry):
1. this map;
2. SDL2Drv: sub-viewports of the one window (Xbox `OpenWindow(Parent!=0)`: no window, the shared device), Xbox
   tick (`Blit` for the first drawn viewport only, `bDisableDraw` honoured), `GetInitScreenSize` = the window,
   `IsFullscreen` as the Xbox's (size == screen), children's `UpdateInput` reads their own pad (no keyboard), the
   Xbox's GamepadID sync, Start on an unbound pad -> bind or `BeginSplitScreen`, re-layout on window resize;
3. Engine: `SpawnPlayActor`'s PC GamepadID fake only for a viewport that has none; `SPLIT ADD/REMOVE` doing the
   Xbox's work on PC; GL device frame size = the window;
4. test channel: `PAD <n> ...` (four virtual pads) and a `VIEWPORTS` probe; `tools/dev/g6-splitscreen.sh`;
5. measure 2/3/4 players; fix what the numbers show (post-processing per viewport, HUD, audio);
6. dynamic gamma per viewport; 7. regressions.

## G-6, steps 2-4 - sub-viewports, joins, the test channel (commits 1c99a9a, c577479, 3b74423, e4ce997)

What was built (file:line at e4ce997):
- SDL2Drv (`SDL2Viewport.cpp`): `OpenWindow` with a parent = a split-screen viewport (`SubViewport`): no window,
  `RenDev` = viewport 0's, size from the window (Xbox `UXboxViewport::OpenWindow`). `GetWindow()` of a sub-viewport
  is viewport 0's window (the GL binding swaps and sizes from it). `WindowSizeX/Y` (set in `ResizeViewport`) is the
  frame; `IsFullscreen` = window fullscreen and the viewport covers it (Xbox rule). `UpdateInput`: Xbox GamepadID
  sync (`XboxViewport.cpp:404-418`), the bound pad for every viewport, keyboard/mouse only for the window's
  viewport. Window commands (SETRES, TOGGLEFULLSCREEN, ...) and `SHOT` from a sub-viewport go to the window /
  the overlay (whole picture). `Destroy`: a sub-viewport leaves the shared device (no `Flush` of the level's
  resources) and console alone.
- SDL2Drv (`SDL2Client.cpp`): `Tick` = `UXboxClient::Tick` (Blit for the first drawn viewport only, `bDisableDraw`
  skipped, ActiveControllerId restored); `GetInitScreenSize` = window; `PadJoin` (Start on an unbound pad,
  `XboxClient.cpp:878-917`, plus `SplitScreenJoinInMatch`); `UpdateSplitLayout` after a resize; `CursorWanted`
  (the overlay's menu cursor is the window's); `Destroy` destroys viewports last first (the old forward loop skipped
  every second one; with shared consoles that crashed at exit: SIGSEGV seen in the first run).
- SDL2Drv (`SDL2Pads.cpp` `SendButtonEdges`): Start (button 12) of an unbound pad -> `PadJoin`.
- Engine: `UClient::SplitScreenViewports()` (UnCamera.h) lifts SPLIT ADD/REMOVE's Xbox branches (`UnCamMgr.cpp`);
  `SpawnPlayActor` fake only for padless viewports (`UnLevAct.cpp:914`); `USplitScreenMgr::RefreshPlayerHUDs`
  (`UnSplitScreenMgr.cpp`, after a join/leave in a match: without it player one kept the full-screen `UCHUD`, its
  reticle on the bottom edge of its strip); `UCanvas::ApplyUIBox` design space of a split view = 640 x 480*ScaleY
  (with 640*ScaleX the split HUD's 640-wide layout put the reticle on the right edge of a quarter).
- UC2GLDrv: `GetFrameSize` (window size) for the main framebuffer in `SetRes`/`Lock`; `ReadPixels` reads the
  viewport's rectangle.
- Default.ini (+ web copy by `sync-web-ini.py`): `[Engine.Engine] OverlayCameraClass=UCGameplay.UCOverlayCamera`
  (Epic's Xbox value; without it the overlay is a plain Camera: "Accessed None [PlayerReplicationInfo]" and no
  pause effect on the menus while split), `[SDL2Drv.SDL2Client] SplitScreenJoinInMatch=True`.
- Test channel: `pad <n> ...` (four virtual pads), `viewports <tag>`; m10-inject.py `vprobe` + `vdist/vyaw/vsplit`.

First runs (Xvfb, llvmpipe, 1280x720, DM-Deadbolt, logs/g6/try/): pads 1..3 Start in the match -> 2, 3, 4 split
views with Scion's rectangles: 2 = (0,0,1280,360)+(0,360,1280,360); 3 = top half + (0,360,640,360) +
(640,360,640,360); 4 = quadrants; overlay (0,0,1280,720) menu=1 scene=0. Pad 2's left stick moved player 3 by
>100 units, the other three by <5 (check `pad2-moves-player3-only` PASS). Pictures: try2-two/three/four.png: four
distinct views, each with the split HUD fitted in its quarter, reticle centred.

Seen, to follow up: the HUD of the viewport at index 1 of `Client->Viewports` gets `SafeZoneOffsetY=-29` (script:
"index 1 = bottom half"), so the top-right quarter's HUD sits 29 design pixels higher than the others' (inside its
quarter). 3-4 players get Epic's `SSDetailLevels` (no coronas/decals/projectors, ReduceFOV 0.5). FovAngle 139 was
probed once right after a join (the spawn's teleport FOV effect?): to measure.

## G-6, steps 5-7 - measured, then fixed what the numbers showed (2026-10-10)

The test: `tools/dev/g6-splitscreen.sh [<tree>] [input|render|menu|audio]` (helpers `tools/dev/g6/g6-checks.py`,
pictures measured by `tools/dev/g6/g6-measure.py`); output in `<WorkUC2>/logs/g6/`.

Found by the numbers and fixed:
- Keyboard "not moving" player one in split screen after a pad had walked it: a wall (the pad walked it there). The
  movement checks now walk forward then back (`only_moved2`). Not a bug.
- Match start: a match with several local players starts once every one of them pressed Fire (UC2's rule); the
  harness presses all pads (`fire_all`), a joining player presses until it has a pawn (`spawn`), no stray shots.
- Telefrags in the render run (a pawn placed onto another dies, god mode or not): placement order fixed.
- Crosshair of player one on the bottom edge of its strip after a join in a match: UCWeapon.CreateScriptedCrosshair
  gives the cached crosshair ScaleY 0.5 only when it is made under a split HUD; `RefreshPlayerHUDs` now re-shapes
  the cached `ScriptedCrosshairs` (0.5 split, class default otherwise) (`UnSplitScreenMgr.cpp`).
- Dynamic gamma: Epic switches it off for split screen (`UpdateDetailLevels`, `DYNAMICGAMMA 0`: the Xbox ramp was
  the display's). The owner keeps it on, so it is per viewport now: `FRenderCaps::ViewportDynamicGamma` (UnRenDev.h)
  lifts the switch-off; UC2GLDrv keeps one `FOpenGLDynamicGamma` per split viewport (histogram of the viewport's
  rectangle of the back buffer, own adaptation), `SetViewportGammaRamp` stores its ramp, the present pass draws each
  split rectangle again through its own ramp (`DrawViewportRamps`, scissored), `ReadPixels` with ShotRamp applies
  the ramp of the rectangle a pixel is in. Single viewport: the old path, byte for byte (no entries).
- AutoFOV per viewport aspect: `ufrontAutoFovSize` is the viewport's own size now; a split viewport's ScaleFOVX/Y are
  set for the frame (and for WorldToScreen/ScreenToWorld and canvas-drawn actors) to atan(ScaleX*tan h)/h, which
  makes `FCameraSceneNode::UpdateMatrices` (camera space x ScaleX, half-angle x ScaleFOV) show exactly the Hor+ FOV
  of the viewport's shape with square pixels. Epic's SSDetailLevels ReduceFOV=0.5 for 3-4 players gave a 79 degree
  quarter (90 on 4:3) and, untested by Scion, a 45 degree top strip with three players; with AutoFOV off (`AUTOFOV
  0`) Epic's maths is untouched. Result at 1280x720: strips 1280x360 show 138.9 degrees horizontally and the same
  vertical extent as the single view; quarters 640x360 show the single view exactly (106.3).

Results at commit after these fixes (load 15-25 while measured; nothing here is timing-sensitive):
- input (Xvfb, llvmpipe): 31/31 PASS - layouts for 1..4 (rects equal Scion's, overlay full window menu-only), pads
  in join order, every pad's left stick moves only its player (186/158 units forward/back, others 0.0), every
  pad's fire fires only its player (3 shots, others 0), right sticks turn only their player (10483 units, others 0),
  keyboard W/S moves only player one, the mouse turns only player one (2002 units), SPLIT REMOVE x3 back to 3, 2, 1
  (full window, `UCHUD`), keyboard still player one, a pad joins again.
- render (GPU Quadro P620 through SDL offscreen, ShotRamp): every view rendered (mean luminance 21-33, 60-82% lit),
  views distinct (mean |diff| 15-42 between views), every HUD inside its rectangle (margins >= 3 px), the view at
  spot A against the single-player view scaled: |diff| 3.2-3.6 without post-processing, 5.0-6.4 with it;
  post-processing strength in the viewport 6.3-10.2 vs 8.5 single (ratio 0.75-1.2): bloom/glow/DOF and the
  per-viewport ramp land where they belong. Every split rectangle had a dynamic gamma ramp of its own.
- menu (Xvfb): UC2's own flow - pad 2's Start on the character page joins (two views, disabled while the overlay
  draws the menu), the match with two split players (pads 0 and 1 kept across the travel, split HUDs), pause menu
  over both, FORFEIT GAME back to the main menu with one viewport: 9/9 PASS.
- audio (Xvfb, wave writer, cue log): player one's fire cue 1.0 per shot alone and with four players, player three's
  1.0 per shot, all 2D (each shooter is its own viewer): not multiplied per viewport.

## G-6, step 8 - regressions, builds, what is open (2026-10-10)

Builds at the final source (all `capped 6G`, `-j2` while load > 24): native, native64, windows (MinGW, uc2.exe),
wasm-perf: 0 errors each (`logs/g6/build-*.log`). No script changed: no package to switch.

Regressions (run trees g6 = rebuilt packages of the asset tree, g6ship = Epic's shipped GUI.u/UCGameplay.u/UCMenu.u):
- m10-controls all five on g6: keyboard 16, mouse 18, text 7, pad 33, both 14 checks: PASS; keyboard on g6ship 16 PASS.
- d6-mouse look pad (D6_TREE=g6, D6_SHIP_TREE=g6ship): 47 PASS 0 FAIL (curves vs shipped worst 1.30%).
- g6-splitscreen input on g6ship: 35/35 PASS (second run; the first had 2 keyboard FAILs, see below); menu 9/9 PASS.
- net-test listen (NET_TREE=g6net): join, spawn, place, seeother, clientmoves, clean, leftovers PASS; listen.hit and
  listen.kill FAIL - and FAIL identically with the baseline binary (main 4a72bfb, tree g6base, `--keep-tree`), load
  17-26: not caused by G-6; to rerun on a quiet machine.
- verifyclasses (ucc-bin, g6 and g6ship): "All native class layouts match".
- audio regression items: audio-menu identical to base (1 cue played, peak -19.0 dBFS, 0 silent s); audio-deadbolt
  new 400/216 and 594/281 cue requests/played vs base 761/400 and 876/427 (bots decide most of it; the player's own
  2D footsteps 24 vs 38 in the last pair). 0 failed, 0 Critical. Not explained; see "unsure" below.
- fps-bench DM-Deadbolt 20 s 1280x720 GPU, alternating: base 84.6 / 89.6 fps, new 88.1 / 86.5 fps (load 15.6-18.6):
  no cost when not split, within the noise.
- single-player picture new vs base (Xvfb, same spot, time frozen): mean |diff| 0.29, differences only on the pawn's
  pose, the crosshair's animation and one animated sign (time-dependent), the world identical.
- browser boot spec: NOT RUN to the end - the frontend's mem guard (--max-gb=17) stopped it before the first test: the
  machine had 15.7 GB in use before the start (an owner's desktop app, missioncenter, held 8.5 GB). serve-web's wasm
  = this build's (sha256 ae8be733...b6e before and after). Null sink made and removed by module index.

Seen but not fixed / open:
- Keyboard walking in split screen failed twice (input run 1 on g6ship, and kb-ship) with the key pressed in the input
  system (KeyDown(W)=1, PlayerWalking) and 0.0 units moved; 6 other runs (both packages) walked 150-500 units.
  Not reproduced on demand; machine load 20-29 at the time. Cause unknown.
- HUD of the viewport at index 1 of the client gets the script's bottom-half safe-zone offset (SafeZoneOffsetY -29):
  the top-right quarter's HUD sits a little higher in its quarter (still inside: margins >= 3 px measured).
- Epic's scripts do not give a player who JOINS while split the always-on depth of field (UCPlayerController.uc:678
  `!IsSplitScreen()`); player one, who was alone, keeps his. Script behaviour, left as shipped.
- Split detail levels (SplitScreenMgr.uc SSDetailLevels, script defaults): 2 players: no coronas, blob shadows,
  DetailMode 1, particles x0.6; 3-4: also no decals/projectors/deco layers, particles x0.4. Epic's Xbox trade-offs,
  kept (they are not "an effect switched off to make it work", but the owner may want PC values).
- Profiles per player: UC2's character page asks player two to load a profile (picture
  owner-menu-character-page-two-players.png); loading one for player two was not exercised.
- Network split screen (a client with split players, UChildConnection/JOINSPLIT): not touched, not tested.

What the web needs later (G-5's area): the engine side is the same code (one window = one canvas, sub-viewports,
present-pass ramps with glScissor, all through GL4ES/WebGL 2); the web ini has `UseJoystick=False` (sync-web-ini.py
DELTAS) - pads through the browser's Gamepad API into SDL's game controllers are G-5's; then Start on a second pad
joins as natively. Keyboard and mouse stay player one (pointer lock is the frontend's). Untested in a browser; four
scene renders per frame will cost on the web (single-player browser frame was 22.5 ms in F-3).

SDL2Drv edits (for G-5's merge): SDL2Drv.h (USDL2Client: SplitScreenJoinInMatch, PadJoin, UpdateSplitLayout,
CursorWanted, SplitScreenViewports; USDL2Viewport: SubViewport, WindowSizeX/Y, GetWindowViewport); SDL2Client.cpp
(StaticConstructor key, Destroy order, Tick, GetInitScreenSize, the split-screen section); SDL2Viewport.cpp
(Destroy, IsWindowCommand + Exec forwarding + SHOT, OpenWindow sub-viewport, IsFullscreen, GetWindow,
UpdateWindowFrame/capture via CursorWanted, ResizeViewport, UpdateMousePosition, UpdateInput, key log lines);
SDL2Pads.cpp (header comment, SendButtonEdges: Start -> PadJoin); SDL2InputTest.* (pad index, viewports probe).

Final run of the whole test on the final binary (`G6_OUT=logs/g6/final tools/dev/g6-splitscreen.sh g6 input render
menu audio`, load 17-25): input 35/35, render 7/7 + g6-measure 59 PASS 0 FAIL, menu 9/9, audio 6/6 + cues 1.0 per
shot alone / player one with four / player three with four. One comparison made informational in g6-measure.py:
player two at spot A right after joining (n2b) showed its view from a camera at the eyes in 1 of 3 runs (|diff| 15.9
against the single-player third-person picture, same rotation, same spot); a camera-mode difference of a freshly
joined player, not a rendering error; player one (n2a, n3, n4) and player four (n4d) are counted. Owner pictures:
`logs/g6/owner-{1player,2players,3players,4players}-gpu.png`, `owner-menu-*.png`.

## G-6, step 9 - the audio-deadbolt cue counts settled (coordinator's request, 2026-10-10)

The difference in step 8 (audio-deadbolt 400/216 and 594/281 requests/played on this branch, 761/400 and 876/427 on
main's binary) came from captures WITH bots (`NumBots=3`): the bots' AI and their footsteps/weapons decide most of the
count. Deterministic comparison without bots, both binaries (run/g6base = main 4a72bfb with main's Default.ini,
run/g6 = this branch), alternating, load 15-22, per cue name and outcome
(`tools/dev/g6/g6-cuetab.py <run>.out ...`, the cue log of UC2ALAudio):

1. `audio-praxis-dry` item without bots: `UC2_PRECMD="set UCDeathmatch NumPlayers 0" UC2_POSTCMD="Ghost"
   tools/dev/run-uc2-audio.sh <tree> praxis-dry-<i> 85 28 'DM-Praxis?NumBots=0' -ini:...UseEFX=False`:

```
cue                                           outcome e/praxis-dry-1 e/praxis-dry-2 6/praxis-dry-1 6/praxis-dry-2
A_Ann_Female.Play                             played               1              1              1              1
A_Gameplay.Countdown                          played               3              3              3              3
A_Gameplay.Teleport                           played               1              1              1              1
A_Gameplay.Teleport.PlayerSpawn               played               1              1              1              1
A_Pickup.Item_Spawn                           culled               2              2              2              2
A_Praxis.Amb_Room_Loop06                      played               3              3              3              3
A_Praxis.Env_Underwater_02                    played               2              2              2              2
A_Praxis.PR_BlueGearAmbLoop                   played               1              2              1              2
A_Praxis.PR_GreenChamberBubbles               played               5              5              5              5
A_Praxis.PR_GreenChamberLoop                  played               2              2              2              2
A_Praxis.PR_OrangeMonitorLoop                 played               2              2              2              2
A_Praxis.PR_PlasmaFloorLoop                   played               1              1              1              1
A_Praxis.PR_PurpleAmbLoop                     played               2              2              2              2
A_WeaponRace.Nakhti.NK_Fire                   played              91            102             94             95
A_WeaponRace.Nakhti.NK_FireImpact             culled              27             29             27             26
A_WeaponRace.Nakhti.NK_FireImpact             played              19             22             21             24
A_WeaponRace.Nakhti.NK_FreezeCharging         played              14             14             15             15
A_WeaponRace.Nakhti.NK_FreezeStart            played              14             14             15             15
A_WeaponRace.Nakhti.NK_Raise                  played               1              1              1              1
A_WeaponRace.Nakhti.NK_Reload                 played               1              1              1              1
TOTAL                                                            193            210            200            204
```

2. The same walk on DM-Deadbolt without Ghost (footsteps on the floors), `'DM-Deadbolt?NumBots=0'`:

```
cue                                           outcome se/deadbolt0-1 se/deadbolt0-2 g6/deadbolt0-1 g6/deadbolt0-2
A_Ann_Female.Play                             played               1              1              1              1
A_Character.Footstep_Default                  played               0             15              0              0
A_Character.Footstep_Metal                    played               3              3              4              4
A_Character.Footstep_Stone                    played              18             10             18             18
A_Character.NK_Anubis_Dodge                   played               1              1              1              1
A_Character.NK_Anubis_Jump                    played               8              8              8              8
A_Deadbolt.Env_Wind_Cold3b                    played               1              1              1              1
A_Deadbolt.LightsHumLoop5                     played               3              3              3              3
A_Deadbolt.LowThrobLoop                       played               3              3              3              3
A_Deadbolt.Machine_Deadbolt                   played               1              1              1              1
A_Deadbolt.amb_room_texture01                 played               2              2              2              2
A_Gameplay.Countdown                          played               3              3              3              3
A_Gameplay.Teleport                           played               1              1              1              1
A_Gameplay.Teleport.PlayerSpawn               played               1              1              1              1
A_Pickup.Item_Spawn                           culled               3              3              3              3
A_WeaponRace.Nakhti.NK_Fire                   played              94             96            103            100
A_WeaponRace.Nakhti.NK_FireImpact             played              94             96            103            100
A_WeaponRace.Nakhti.NK_FreezeCharging         played              15             15             16             15
A_WeaponRace.Nakhti.NK_FreezeStart            played              15             15             16             15
A_WeaponRace.Nakhti.NK_Raise                  played               1              1              1              1
A_WeaponRace.Nakhti.NK_Reload                 played               1              1              1              1
TOTAL                                                            269            280            290            282
```

Result: every zone ambient, countdown, teleport, pickup and reload cue is identical; the player's footsteps (21/28
main, 22/22 branch; main's run 2 walked onto a Default-surface floor 15 times) and weapon cues (fire 91/102 vs 94/95
on Praxis, 94/96 vs 103/100 on Deadbolt: the fire rate follows the frame rate of a software-GL run) differ run to
run on BOTH binaries by about as much as between them, and the branch is not lower. No change in single-player audio:
nothing to fix. (Single player never takes the split path: one viewport, Blit on it as before, `Audio->Update()` from
the one viewport with bShowMenu+bShowScene as before; UC2ALAudio was not changed.)

## G-6, step 10 - the keyboard walk that failed (coordinator's request, 2026-10-10)

Five runs of `g6-splitscreen.sh g6 input` started at load < 16 (logs/g6/kb5.txt, kb5-1..5): the old check
`keyboard-ws-moves-player1-only` failed 4 of 5, W moved player one 0.0 in all five and S 0.0 in four. The key press
reached viewport 0 every time (`key W press (viewport 0)`), nothing released it early.

Diagnosis:
- A per-tick watch of player one (test channel, temporary) showed the input working: W held -> the pawn's
  acceleration (1417,1479) along its yaw, no hijack, PlayerWalking, GUI inactive; where the pawn did not move its
  velocity stayed 0 (a wall in front after the right stick's turn test), and S then walked 392 units.
- At the same spot and facing, placed with NETPLACE, the keyboard walked 400-600 units both ways, alone and with four
  players (logs/g6/diag/kbspot1-g6dbg, kbspot4-g6dbg).
- The harness's Xvfb parts render with the default 4x MSAA in llvmpipe, four scenes a frame: the FPS log during the
  old keyboard sequence (`[Engine.GameEngine] FpsLogSeconds=2`, logs/g6/diag/kbold.out) reads 0.7-1.0 fps, frame
  1010-1404 ms. A 1.2 s key hold is one frame: an X key press and its release can be pumped in the same frame, before
  any UInput::ReadInput, and the hold never walks. (The pads were never affected: the test channel stops after every
  virtual-pad change so each change is polled at least once, m10's rule; m10-controls also turns MSAA off for its
  Xvfb runs for this reason.) Not a defect of the keyboard path; a timing artifact of the test at < 1 fps.

Fix (the test): the keyboard part now probes every viewport DURING each hold (`vprobe kh<key>`, which waits for the
engine's answer, i.e. at least one tick with the key down) and checks the pawn's acceleration: player one's only, for W
S A D (`only_accel`), then that player one moved and nobody else over the four (`only_moved4`). The viewport probe
gained `acc=(x,y)` (SDL2InputTest.cpp ProbeViewports; m10-inject.py parses it).

Five runs at load < 16 at start (logs/g6/kb5b.txt, kb5b-1..5): **5/5 PASS** (39 checks each). Acceleration during the
holds: player one (+-1033,+-1769)/(+-1769,+-1033) by key, the other three (0,0) every time. Walks W/S/A/D: 186.0 / 158.1
/ 186.0 / 185.9 (runs 1-4), 357.9 / 304.3 / 185.9 / 357.9 (run 5); the others 0.0.

## G-9 - the owner's follow-up: FOV pictures, full detail, keyboard+mouse a player of its own (2026-10-10)

Worktree `/home/ion/WorkUC2/engine-uc2-g9-split2`, branch `g9-split2` (from main 486f17e, G-6 merged). Run trees
`run/g9` (the asset tree's rebuilt packages) and `run/g9ship` (`UC2_SCRIPTS=assets/uc2-shipped/System`: Epic's GUI.u,
UCGameplay.u, UCMenu.u); logs and pictures `/home/ion/WorkUC2/logs/g9/`. Everything through `bin/capped` (game 2G,
builds 6G `-j2`/`-j4` by load). The owner's decisions (verbatim): "For split screen fov, can you do a/b screenshot with
the options? I'll say keep full detail, and keyboard its own player, and yes even AutoFOV for here as well, but let's
see how aggressive it is."

### G-9 step 1 - the FOV A/B pictures

No change of behaviour, two additions so the pictures can be read and the third variant can be shown:
- `Source/Engine/Src/UnRender.cpp` `ufrontLogShownFov` (declared in `UnRender.h`), called from `FScopedAutoFov`
  (`UnGame.cpp`) for every viewport that draws the scene: one line per viewport whenever it changes,
  `[AutoFOV] shown viewport=<i> rect=X,Y WxH mode=M base=B effective=E scaleFOV=S horizontal=H vertical=V`: the FOV
  the picture shows after AutoFOV AND the split-screen scale, also with AutoFOV off (the D-8 line is for viewport 0
  only, and silent when off). Maths from `FCameraSceneNode::UpdateMatrices` (camera x ScaleX/ScaleY, half-angle h x
  ScaleFOVX/Y, aspect of the viewport's own render target `(W*ScaleX)/(H*ScaleY)`): across `2*atan(tan(h*ScaleFOVX)/
  ScaleX)`, up `2*atan(tan(h*ScaleFOVY)*H/(W*ScaleX))`.
- The proposal, NOT the default: `[ufront.AutoFOV] SplitClampToFullScreen` (compiled default and `Default.ini` False).
  `ufrontAutoFovSize` then corrects a split viewport that is wider than the window (the two-player strips, the
  three-player top strip) for the WINDOW's aspect instead of its own, so it shows the single player's horizontal FOV
  (106.26 at 16:9) and less vertically; quarters (the window's aspect) are unchanged. Every consumer of the AutoFOV
  size (the frame, the weapon, WorldToScreen) agrees because they all ask `ufrontAutoFovSize`.
- Test: `tools/dev/g6-splitscreen.sh g9 fov` (GPU, SDL offscreen, ShotRamp: G-6's render set-up, the pad as player one
  with `KeyboardIsPlayer=False`). The frame is frozen (`TimeDilation 0.0001`) at G-6's spot A (player one) for 1, 2,
  3, 4 players, and shot with `AUTOFOV 2` and `AUTOFOV 0` (the same frozen frame); a second run with
  `-ini:System:[ufront.AutoFOV]:SplitClampToFullScreen=True` gives the proposal. A `g9fov <tag>` log marker precedes
  each shot; `tools/dev/g6/g9-fov-sheet.py` takes the `[AutoFOV] shown` lines in force at the marker, writes them into
  each viewport of the picture, and compares every logged value with the value computed from the rectangle alone.

Result (`logs/g9/fov-run.txt`; load at start < 16, 25 at the end because of my own build: pictures, not timings):
`PASS g9-fov` 6 checks, `PASS g9-fovclamp` 6 checks, 0 Critical; 12/12 `logged-equals-computed` PASS, worst |logged -
computed| 0.0000 degrees. The sheets, four panels each (1 player reference with AutoFOV on | AutoFOV on, today's
default | Epic's maths, AutoFOV=0 | the PROPOSAL, clamped to the single player's FOV), with every viewport's
"across x up" written in it: **`logs/g9/fov-2players.png`, `logs/g9/fov-3players.png`, `logs/g9/fov-4players.png`**
(1288x808). 1280x720 window, base FOV 90:

| players | viewport (x,y w x h) | AutoFOV on (default) | Epic's maths (AutoFOV=0) | proposal: clamped |
|---|---|---|---|---|
| 1 | 0,0 1280x720 | 106.3 x 73.7 | 90.0 x 58.7 | 106.3 x 73.7 |
| 2 | each strip 1280x360 | **138.9** x 73.7 | 90.0 x **31.4** | 106.3 x 41.1 |
| 3 | top strip 1280x360 | **138.9** x 73.7 | **45.0 x 13.3** | 106.3 x 41.1 |
| 3 | bottom quarters 640x360 | 106.3 x 73.7 | 79.3 x 50.0 | 106.3 x 73.7 |
| 4 | quarters 640x360 | 106.3 x 73.7 | 79.3 x 50.0 | 106.3 x 73.7 |

(across x up, degrees; the single player's Epic value from the same run's n1-epic line, not in a sheet.) What the pictures say: AutoFOV
keeps every player's vertical view at the single player's 73.7 and pays for the strips' shape with width (139 across,
the edges stretched); Epic's maths keeps 90 across and crops the strips to 31 degrees up (three players' top strip:
45 x 13, a letterbox through a telescope: ReduceFOV 0.5 was made for quarters and never tried on that strip); the
proposal keeps the single player's 106 across and crops the strips to 41 up. Quarters: AutoFOV and the proposal are
the single player's view exactly; Epic's are zoomed (79 across). No decision taken here: the owner chooses.

### G-9 step 2 - full detail in split screen (`[SDL2Drv.SDL2Client] SplitScreenFullDetail=True`)

What Epic's code does (`USplitScreenMgr::UpdateDetailLevels`, `UnSplitScreenMgr.cpp`, after every join/leave/map load):
with N split views it applies `SSDetailLevels(N-1)` (`SplitScreenMgr.uc` defaultproperties) to the client (coronas,
dynamic lights, decals, deco layers, projectors), the level (`DetailChange(DetailMode)`, `PARTICLEATTEN`) and every
split viewport (blob shadows, camera effects, `ScaleFOVX/Y = ReduceFOV`); with no split view `SSDetailLevels(0)`. So a
single player is ALWAYS at entry 0 (DetailMode 2, coronas, decals, projectors, real shadows, particles x1), whatever
the ini says. `bUseDynamicDrawScale` (pawns drawn bigger with distance from THE player: `AUCPawn::ScalePawn`, only
with one viewport) is switched off while split: a gameplay aid made for one viewer, not a detail setting; kept.

Change:
- `UClient::SplitScreenKeepsFullDetail()` (`Source/Engine/Inc/UnCamera.h`, virtual, 0 = Epic's), overridden by
  `USDL2Client` (`SDL2Drv.h`) with the new config key `SplitScreenFullDetail` (`SDL2Client.cpp`: registered, compiled
  default 1; `Default.ini` and the web copy `True`).
- `UpdateDetailLevels`: when the client keeps full detail, every split view gets entry 0 (the single player's settings)
  except `ReduceFOV`, which stays the layout's (0.5 for three and four players): it is the FOV maths of Epic's view
  (AutoFOV=0), replaced per frame by AutoFOV anyway, and the FOV is the owner's separate choice (step 1). `False` =
  the Xbox's reductions, untouched. One log line per update: `Split screen: N views, detail of one player
  (SplitScreenFullDetail) (DetailMode 2, coronas 1, decals 1, projectors 1, deco layers 1, blob shadows 0, particles
  x1.0, ReduceFOV 0.50)` / `detail level 3 (Xbox) (DetailMode 0, coronas 0, decals 0, projectors 0, ...)`.

Measured (GPU, `logs/g9/render-full` = default, `logs/g9/render-xbox` = `G6_ARGS=-ini:...SplitScreenFullDetail=False`;
`tools/dev/g6-splitscreen.sh g9 render`, load < 16 at start):
- The log lines say what each run set: full = entry 0 for 2, 3, 4 views; xbox = levels 1, 2, 3 (coronas 0 from two
  players, decals/projectors/deco layers 0 from three, particles x0.6/0.4/0.2, DetailMode 1/0/0, blob shadows).
- render-full: g6-measure 0 failed (the same-view checks |diff| without post-processing 3.35 / 3.38 / 3.60 / 3.61 for
  n2a / n3 / n4 / n4d, G-6 had 3.2-3.6 with Epic's reductions in force). render-xbox: g6-measure 8 FAILED, all
  same-view: that run's single-player picture n1 was NOT at spot A (the pawn was placed there, NETPLACE "ok", but the
  frozen frame shows a first-person view elsewhere: the third-person camera did not follow before time was frozen;
  G-6 saw the same for a freshly joined player, n2b, 1 run in 3). Not a detail effect (single player is entry 0 in
  both runs). `tools/dev/g6/g9-detail.py` therefore compares both runs' spot-A views with the full run's single
  picture: |diff| without / with post-processing, full vs Xbox: n2a 3.35/5.43 vs 3.36/5.34, n3 3.38/5.11 vs 3.36/5.26,
  n4 3.60/5.93 vs 3.57/5.58, n4d 3.61/6.08 vs 3.63/6.22: `PASS g9-detail` (full not further from the single player).
  The difference does not grow; at spot A it does not shrink either: DM-Deadbolt has NO corona light (`ucc-bin
  batchexport` of the map: 0 of 176 lights with bCorona), no projector actor, and the frozen views show no decals, so
  what Epic's reductions remove is nearly absent from that picture (`logs/g9/detail-full-vs-xbox.png`: the |full -
  Xbox| x4 column is dark except the pawns' poses).
- 4-player frame rate (`g6-splitscreen.sh g9 bench4`, GPU Quadro P620, 1280x720, four players at spots A C D E, FPS
  log over 20 s, alternating, load 15-18.7 at the starts: NOT a quiet machine):

| run | fps | frame ms avg / p99 / max | 1% low |
|---|---|---|---|
| Xbox detail 1 | 19.2 | 52.0 / 64.7 / 72.3 | 14.5 |
| full detail 1 | 16.2 | 61.8 / 245.7 / 255.9 | 3.9 |
| Xbox detail 2 | 21.9 | 45.7 / 118.5 / 188.6 | 5.8 |
| full detail 2 | 16.6 | 60.2 / 226.7 / 264.4 | 3.9 |

  Full detail costs about 10-15 ms a frame with four players here (16.4 against 20.6 fps, -20%), and both full runs
  had frames of 230-260 ms (1% low 3.9 fps) that the Xbox runs had less of: to look at on a quiet machine (particles
  x1 against x0.2 and four views of the same emitters is the first suspect). Single-player is not affected (one view:
  entry 0 before and after; the fps-bench single-player comparison is in the regressions).

Where single player shows coronas, shadows and decals (`tools/dev/g6-splitscreen.sh g9 corona` / `detail`, DM-Praxis:
nine bCorona lights, a ring over the round room; GPU):
- `corona` part: six candidate views, each shot with the client's coronas on and off (`set SDL2Client Coronas
  False`): the ring shows from the round room (2,189 to 22,775 pixels lit by coronas; `logs/g9/corona/`).
- `detail` part: P1 looks up at the ring, P2 at the ceiling slab the ring is behind (single player: 44 px, i.e. no
  corona: Epic's line check from the view hides them), P3/P4 elsewhere; four players at P1..P4.
- **A real bug found** with full detail, before any fix (`logs/g9/detail0-full`, `logs/g9/corona-leak-before-fix.png`):
  player TWO's quadrant (P2) showed 3,329 corona pixels against the single player's 44 at the same spot: the ring that
  player ONE sees, drawn through the ceiling. Cause, in Epic's shared code: one global `FCoronaRender GCoronaRender`
  (`UnRenderVisibility.cpp`) holds the lights seen last, which fade out over a few frames and are drawn with
  `ZTest=0` (`UnRender.cpp` `RenderCoronas`), and the time of the last draw; every split view drew every light any
  view had just seen, with its own camera, through walls, and the fade ran on the milliseconds between two views. The
  Xbox never met it: its split screen had coronas off (`SSDetailLevels` from two players). Fix (`UnRender.h`
  `UC2CoronaRenderFor` / `UC2ResetCoronas`, `UnRenderVisibility.cpp`, `UnLevel.cpp`): each split view other than the
  first gets a corona state of its own (four at most; more falls back to the shared one); `Viewports(0)` (the single
  player, the first split view) keeps `GCoronaRender`, so one viewport is exactly as before; the three places where
  Epic reset the global reset them all.

### G-9 step 3 - keyboard+mouse a player of their own (`[SDL2Drv.SDL2Client] KeyboardIsPlayer=True`)

Epic's model, read before choosing (G-6 notes, controls.md "Routing", `UnGUICore.cpp:1114-1149`): a player is a
viewport; a pad is bound to the viewport whose `GamepadID` is its port (`GetPadViewport`), read only by it, and an
unbound pad reaches the menus only (`MasterProcessKeyEvent`), where Start joins (`PadJoin`); on PC the keyboard and
the mouse are events of the window's viewport, and `ULevel::SpawnPlayActor` fakes `GamepadID 0` for it ("for
development on the PC"), so the first pad (port 0) and the keyboard were one player. The GUI routes every key by
`Engine->ActiveControllerId`: `ControllerMask & (1 << id)`, `FindViewportIndex(id)` = the viewport with that
`GamepadID`, per-port arrays `RepeatKey[LAST_VIEWPORT]` (`Object.uc:220 LAST_VIEWPORT=4`: "highest supported
controller port"), and UCMenu's pages use `1 << PC.GamepadID` masks. The keyboard's events carry the resting
`ActiveControllerId` 0 (every pad read restores it: `UpdateBoundPad`, `UpdateUnboundPads`, `USDL2Client::Tick`).

Design: the keyboard and the mouse KEEP port 0 (the id the GUI already gives their events, Epic's PC fake), and the
pads move to ports 1-3. A "pad -1"/255 for the keyboard was rejected: `GamepadID` is a BYTE, 255 is
`INVALID_GAMEPAD_ID` ("no pad": PadJoin would hand that viewport to the first pad), and `1 << 255` / arrays of four in
the shipped GUI.u would break. With four ports that is keyboard + three pads = MaxSplitScreens 4; a fourth pad finds
no port ("SDL2 pads: all 3 ports in use (port 0 is the keyboard's: KeyboardIsPlayer), pad ignored").
- `SDL2Drv.h` / `SDL2Client.cpp`: config `KeyboardIsPlayer` (Input), compiled default 1, `Default.ini` and the web copy
  `True` with a comment; `False` = G-6's model (the first pad drives player one with the keyboard).
- `SDL2Pads.cpp` `PadAdded`: first free port from 1 when `KeyboardIsPlayer`; the connect line says so.
- `SDL2Client.cpp` `PadJoin`: the Xbox's "a pad takes a viewport that has no pad yet" never takes the window's viewport
  (the keyboard's player) when `KeyboardIsPlayer`, even before its controller got port 0.
- Nothing else changes: an unbound pad is Epic's unbound pad (menus only, Start joins: in a match through G-6's
  `SplitScreenJoinInMatch`, on the title and character pages through UC2's own `AllowControllerJoin`); the joined pad
  is read by its viewport only; keyboard and mouse events go to the window's viewport only (G-6), so the rebuilt
  package's PC mouse (F-1/D-6: `UCPlayerInput` separates the mouse from the stick by the Count bytes, per player) only
  ever sees the mouse on the keyboard's player and only ever sees a stick on a pad's: Epic's pad tuning is untouched
  on the pads' viewports. `UnLevAct.cpp` only gained a comment.
- Menus (D-2/D-5): the keyboard's player is the window's viewport and port 0, so the menus' ownership is Epic's: its
  keys reach the page the way a pad on port 0 would; a pause menu opened by a pad player belongs to that player
  (`ActiveChildViewport`, `UnGUICore.cpp:1120`), as on the Xbox. Text entry (D-5) is the window's keyboard as before.
- Profiles: player one (the keyboard's) has the profile D-3 loads at start (`UC2StartWithLastProfile`, G-7's name and
  class); a pad player made by `CreateSplitPlayer` without `Viewport->Profile` logs in with `[DefaultPlayer]` (User.ini:
  the launcher's/ini's name and class, i.e. player one's defaults: two players can carry the same name), exactly as in
  G-6 and on the Xbox for a player without a profile; UC2's character page asks player two to load a profile. Nothing
  per player was added.
- Consequence to know (owner question below): with `KeyboardIsPlayer=True` a person with only a pad never gets
  player one: the pad's Start joins as player two next to an idle keyboard player (in a match, and on the title page
  `StartMenu.AllowsControllerJoin` makes it a split player in the menus). Pad-only play needs `KeyboardIsPlayer=False`.
- Tests that use the virtual pad as player one (the G-6 model) now say so with
  `-ini:System:[SDL2Drv.SDL2Client]:KeyboardIsPlayer=False`: `g6-splitscreen.sh` input/render/menu/audio/fov/detail/
  corona/bench4 (`$KBOLD`), `m10-controls.sh` (and so d6-mouse, d9-rebind, jit-trace), `fx-owner.sh` (and m7b),
  `fps-bench.sh`, `g4-language.sh`, `m12-wine-smoke.sh`, `jit-strip.sh`, `f5-flicker.sh`, `net-test.py`. `m10-inject.py`'s
  `pyexec` namespace gained `click`.

Test: `tools/dev/g6-splitscreen.sh g9 kbplayer` (Xvfb; `-ini:...KeyboardIsPlayer=True`): game 1 in a match, game 2 UC2's
menu flow with one pad (`menu_kb=1 scen_menu`). First run (`logs/g9/reg`, load 15): 22 of 32 in the match (everything up
to the unplug passed; the unplug opened Epic's `ControllerRemovedMenu`, which pauses the game until the pad's owner
presses Start: the scenario did not, so everything after it failed), menu flow failed on `click` missing from the
inject namespace. Scenario fixed (Start on the re-plugged pad closes the menu, Epic's `ConfirmControllerAdded`; the
expect order of the menu's log line); second run (`logs/g9/kb`, load 5): 33 of 34 (the one: the expect order) and the
menu flow 11/11. Numbers (`logs/g9/kb/g9-kbplayer.inject`): unjoined pad's stick moved player one 0.0; after the join
ports [0, 1], both views with their own rectangle and split HUD; keys W S A D: the keyboard's pawn accelerates
(+-939,+-1820)/(+-1820,+-939), the pad's (0,0); walks 185.9/158.1/186.0/186.0 vs 0.0; the pad's left stick: 0.0 vs
186.6/158.9; the mouse turned 2002 vs 0, the right stick 0 vs 10483; mouse fire 2 shots vs 0, pad fire 0 vs 3.

Final run on the binary with the corona fix (`logs/g9/final`, load 15.7 at start, 22 by the end: my own windows build
ran meanwhile): `PASS g9-kbplayer (34 checks)`, `PASS g9-kbmenu (11 checks)`; G-6's model (`KeyboardIsPlayer=False`):
`PASS g6-input (39 checks)`, `PASS g6-menu (9 checks)`.

### G-9 step 2b - a crash at full detail with four players: Scion's per-tick projector cache

`logs/g9/final/g6-render.out` (binary cf60be5f: steps 2 and 3 plus the corona fix, full detail): SIGSEGV in
`RenderStaticMesh` (`UnRenderStaticMesh.cpp:1067`, `ProjectorInfo->Projector->bProjectStaticMesh` of the "dynamic
projectors hitting the static mesh" loop; addr2line on the build's uc2.bin) during the four-player shots, right after
a POSTPROCESS toggle, time frozen (`TimeDilation 0.0001`). render-full (the same code without the corona fix) had
passed the same scenario: intermittent.

Cause (read, Epic's shared code): `FDynamicActor` caches the projectors that hit it (`ActorProjectors`: raw pointers,
no reference held) and rebuilds the list only when `Level->TimeSeconds` changed since the last build (Scion's "only
update its lighting once per frame": `UnRenderVisibility.cpp` RenderActor and `UnRender.cpp` FActorSceneNode::Render).
`AProjector::Detach` (a pawn's shadow projector re-attaches as the pawn animates) sets the info's `Projector=NULL`,
`Expires=now` and drops its reference; the level's list drops the info in the next visibility pass, after which it is
deleted. With `TimeDilation 0.0001` at TimeSeconds ~100 the tick (16 ms x 0.0001) is below the float's resolution, so
TimeSeconds stands still and the cached lists live on across ticks: `RenderStaticMesh` then calls `Render()` on an
info it holds no reference to (which removes someone else's reference when it has expired) or on a deleted one. The
Xbox's four-player detail had projectors off (`SSDetailLevels`), so its four-player frames never had shadow projectors
to cache; with full detail they do (four pawns, four views).

Fix (`UnProjector.h`/`.cpp`, `UnRender.h`/`.cpp`, `UnRenderVisibility.cpp`): `GProjectorRenderInfoGeneration`, bumped
by `AProjector::Detach`, `AProjector::Abandon` and `~FProjectorRenderInfo`; an `FDynamicActor` remembers the value its
`ActorProjectors` was built with (`LastProjectorGeneration`) and rebuilds when it changed, as when TimeSeconds
changed. In normal play nothing changes: TimeSeconds moves every tick, so the lists were rebuilt every tick anyway, and
a detach happens in a tick, not between the views of one frame.

Corona proof after the corona fix (binary 00fd3b32, which also has this fix; `logs/g9/detail2-full`, `detail2-xbox`,
sheet `logs/g9/detail-praxis.png`, `logs/g9/detail-praxis.txt`): corona pixels per quadrant / the single player at the
same spot: full detail P1 2,077 / 2,086 (player one sees its coronas), P2 11 / 144, P3 10 / 85, P4 14 / 4 (no corona
from another view); Xbox detail 2 / 2,256, 8 / 102, 8 / 73, 21 / 34 (no coronas at four players, Epic's level 3);
before the fix P2 3,329 / 44. `PASS g9-detail-praxis` (the before-fix run reported, not counted). Decals and
projectors: the log line says they are on at four players with full detail and off with the Xbox's; the pictures did
not show them apart (three Nakhti shots at the floor in front, PD, left no decal I can see in either; the pawn's shadow
is not separable at that size): **seen only as settings**, not numerically checked. That four-player frames now carry
shadow projectors is what step 2b's crash showed. Harness note: in these four-player runs one player was sometimes
dead in the frozen frame (a frag at spawn; "You are dead" scoreboard in its quadrant), which only touches the P3/P4
baselines. Single-player shot of `render`/`fov` (n1): in 2 of 5 runs the frozen frame showed a first-person view with
no pawn or the old place (render-xbox, final2: every same-view check then fails at ~16-17); the frame was frozen 2 s
after the second placement, soon after the spawn's teleport-in; the scenario now waits 5 s.

### G-9 step 4 - regressions and builds on the final binary (uc2.bin sha256 00fd3b320fb2016b..., commit f39fbdb)

Builds (`bin/capped 6G`, `-j2`/`-j4` by load): native, native64, windows (MinGW uc2.exe), wasm-perf: 0 errors
(`logs/g9/build-*.log`; the first windows link failed on `GProjectorRenderInfoGeneration` because its objects mixed the
sources before and after step 2b's edit mid-build; the incremental rebuild linked, `build-windows-2.log`). No script
changed: no package to switch.

| test | run | result |
|---|---|---|
| `g6-splitscreen.sh g9 kbplayer` (KeyboardIsPlayer=True) | `logs/g9/final2` | PASS 34 + kbmenu PASS 11 |
| `g6-splitscreen.sh g9 input` (G-6's model, KeyboardIsPlayer=False) | `logs/g9/final2` | PASS 39 |
| `g6-splitscreen.sh g9 menu` | `logs/g9/final2` | PASS 9 |
| `g6-splitscreen.sh g9 audio` | `logs/g9/final2` | PASS 6; fire cue 1.0 per shot alone, with four (player one), player three; all 2D |
| `g6-splitscreen.sh g9 render` (full detail) | `logs/g9/final3` | g6-measure 0 failed (same view as single |diff| 3.51/3.21/3.83/3.87, every split rectangle its own ramp); render 6/7: `render-every-player-alive-at-the-end` FAIL, see below |
| `g6-splitscreen.sh g9 detail` + `g9-detail.py --praxis` | `logs/g9/detail2-*` | PASS (coronas, step 2b section) |
| `m10-controls.sh` (M10_TREE=g9) | `logs/g9/m10` | keyboard 16, mouse 18, text 7, pad 33, both 14: PASS |
| `d6-mouse.sh look pad` (D6_TREE=g9, D6_SHIP_TREE=g9ship) | `logs/g9/d6` | 48 PASS 0 FAIL (pad curves vs shipped worst 3.55% walk "one frame of running" rule, 0.13% yaw) |
| `ucc-bin verifyclasses` (g9, g9ship) | | "All native class layouts match", rc 0 both |
| `fps-bench.sh DM-Deadbolt 20` single player, alternating, base = main's build (run/g9base, sha256 64be3015...) | `logs/g9/fps` | base 103.4 / 91.5 / 100.5 / 94.6 / 91.6 / 98.9 (mean 96.8) fps; new 90.7 / 89.6 / 97.6 / 91.6 / 97.8 / 99.2 (mean 94.4); load 4.8-11 |

- fps single player: -2.4% on the means, inside the run-to-run spread (89.6-103.4 for the same binary). The first two
  pairs were both lower for the new binary, so I checked step 2b's cache change, the only per-frame work added for one
  viewport: a build with the generation test disabled ran 92.9 / 98.0 against the final 97.8 / 99.2 and base 91.6 /
  98.9 in the same rounds: no cost from it. Unchanged at this machine's noise; a quiet-machine measurement would settle
  the last percent.
- `render-every-player-alive-at-the-end` (final3, also render-full): player two was telefragged right after joining.
  Cause in the log: it spawned on the PlayerStart under SPOT_B and player one was NETPLACEd onto SPOT_B ("FirstBlood",
  `Gib_Explosion on UCPawn_Nakhti_Anubis1` at distance 106 right after `place Player-1 ok`). All five of G-6's render
  spots (A-E) are PlayerStarts (the map's t3d: within 22 units), so a joining player can spawn on the next target:
  a race in G-6's scenario, not caused by G-9 (it passed in final2 and render-xbox). Left open (moving the spots off
  the starts changes G-6's pictures).
- G-6's earlier numbers for comparison: input 35-39, render 7 + g6-measure 59/0, menu 9, audio 6, m10 five PASS, d6 47
  PASS; fps base/new 84.6-89.6.

### G-9 - what is open for the owner

1. **FOV**: choose from `logs/g9/fov-{2,3,4}players.png` (table in step 1): AutoFOV on (139 across on strips, the
   default today), Epic's maths (90 x 31 strips, 45 x 13 three-player top), or the proposal
   `[ufront.AutoFOV] SplitClampToFullScreen=True` (106 x 41 strips). Nothing is decided here.
2. **Pad-only players with KeyboardIsPlayer=True**: a person with only a pad never gets player one; its Start makes it
   player two beside an idle keyboard player (in the menus too: the title page allows a join). Options: keep (set
   False for pad-only play, the launcher could offer it), or a rule such as "the first pad takes player one until the
   keyboard or the mouse is used" (not built: a design change to ask about first).
3. Keyboard player cannot drop out alone: Epic's `RemoveSplitViewport` never removes the first view's player ("don't
   delete default player"), and `SPLIT REMOVE` removes the last view. "Leaving in either order" was tested as: the pad
   player leaves (SPLIT REMOVE) and the keyboard stays player one; the keyboard player ends the session (pause menu,
   FORFEIT) and everyone leaves, after which the pad's Start on the main menu takes no player.
4. Four-player cost of full detail: 16-23 fps against 18-26 with the Xbox's detail on the Quadro P620 (two sets of
   alternating runs, load 11-19: the spread is as large as the difference); frames of 200-260 ms appeared in some full
   runs. To measure on a quiet machine before telling players what four players cost.
5. Decals and projectors in split screen: on (log lines), not shown apart in a picture (step 2b section).
6. G-6's render scenario race (spots on PlayerStarts), above.

SDL2Drv edits of G-9 (for G-5's merge): `SDL2Drv.h` (USDL2Client: SplitScreenFullDetail, KeyboardIsPlayer,
SplitScreenKeepsFullDetail); `SDL2Client.cpp` (StaticConstructor keys and defaults, the comment above Tick, PadJoin's
loop start); `SDL2Pads.cpp` (header comment, GetPadViewport comment, PadAdded's first port and log lines).

## G-11 - the owner's final rule for pads, keyboard+mouse and player slots (2026-10-10)

Worktree `/home/ion/WorkUC2/engine-uc2-g11`, branch `g11-pads` (from main 245d414); frontend worktree
`/home/ion/WorkUC2/ufrontend-pads`, branch `uc2-pads` (its notebook `docs/migration/notes/gamepad.md`, G-11 section).
Run trees `run/g11*`, logs `/home/ion/WorkUC2/logs/g11/`. Everything through `bin/capped` (game 2G, builds 6G).

The owner's rule (verbatim where it matters):
- One setting, a launcher checkbox and the same ini key natively: `[SDL2Drv.SDL2Client] KeyboardIsPlayer` renamed
  `PadsStartAtPlayerTwo`, default False natively AND on the web (the web delta dropped). Launcher label "Force gamepads
  to start with second player", blurb "forces gamepads to plug into port 2 and on, with the keyboard and mouse in port 1
  exclusively".
- Unchecked/False: "both KB&M and gamepad [1] are player 1 and can be exchanged dynamically; to play a second player we
  need to plug another controller [and press Start]" (= G-6's model, `KeyboardIsPlayer=False`).
- Checked/True: "the game would see KB&M/Gamepad 1 (but this gamepad is dummied out) + Gamepad 2 (which is now gamepad
  1). We push all of the other gamepads" (= G-9's model, `KeyboardIsPlayer=True`).
- "The 'gamepad disconnected' message should never trigger for player 1 because we always have the keyboard and mouse
  available. If it really disconnects then we just reconnect it and we're back." Pad-only players keep Epic's page.

### G-11 step 1 - the rename, the default, and no "controller removed" page for player one

How Epic's page opens (read): `PadRemoved` (`SDL2Pads.cpp`, `UXboxClient::DetectDevices`) sends `IK_JoySlider2` to the
pad's viewport; `DefUser.ini:205` binds `JoySlider2=ControllerRemoved`; `PlayerController.ControllerRemoved`
(`PlayerController.uc:1498`) clears the triggers, sets the HUD's `bControllerUnplugged` (`UCHUD.SetGamepadStatus`),
calls `GUIController.SetGamepadStatus(false, Player)` (`GUIController.uc:2837`), which opens `UCMenu.ControllerRemovedMenu`
for that port (not on `StartMenu`), and pauses a standalone game. `JoySlider1` (insert) is bound to `ControllerAdded`, an
empty exec; the page closes on the owner's Start (`ConfirmControllerAdded`). `GUIController.NativeKeyEvent` only resets
its input on either key (`UnGUICore.cpp:1080`).

Change (file:line at this step):
- `Source/SDL2Drv/Inc/SDL2Drv.h:106` member `PadsStartAtPlayerTwo` (was `KeyboardIsPlayer`); `:161` `ViewportHasKeyboard`.
- `Source/SDL2Drv/Src/SDL2Client.cpp:55` config key `PadsStartAtPlayerTwo` (Input); `:96` compiled default 0 (was 1);
  `:304-308` the comment above `Tick`; `:483-485` `PadJoin` (unchanged logic, renamed).
- `Source/SDL2Drv/Src/SDL2Pads.cpp:17-26` header; `:262-266` `GetPadViewport` comment; `:275-282` new
  `USDL2Client::ViewportHasKeyboard` (the window's own viewport: G-6 sends the keyboard and the mouse to it only);
  `:305-345` `PadAdded` (renamed; the connect line now says `(player one, with the keyboard and mouse)` when the port's
  viewport is the keyboard's, `(keyboard and mouse are player one on port 0: Start joins)` with the key on);
  `:398-415` `PadRemoved`: the held buttons are released as before, then `JoySlider2` goes to a pad-only player's
  viewport or to the interaction master (pad bound to nobody) as on the Xbox, and to NO viewport that has the keyboard
  and the mouse; the line reads `SDL2 pads: pad 0 disconnected: <name> (player one keeps the keyboard and mouse: no
  controller-removed page; a pad plugged in takes this port again)`. The viewport keeps its `GamepadID` (Epic's PC fake 0),
  so the next pad plugged in takes the first free port, this one, and `GetPadViewport` binds it to player one at once;
  its `JoySlider1` reaches the empty `ControllerAdded`: silent.
- `Source/Engine/Src/UnLevAct.cpp:919` comment only.
- `Engine/Config/Default.ini` `[SDL2Drv.SDL2Client] PadsStartAtPlayerTwo=False` with its comment;
  `tools/dev/sync-web-ini.py` DELTAS entry removed (a comment says why); `Engine/Config/web/Default.ini` regenerated
  (`--check` clean): the web file now equals the native one there. `Engine/Config/README.md` row rewritten.
- With True, `ViewportHasKeyboard` changes nothing: no pad ever has port 0 (`PadAdded` starts at 1, `PadJoin` never gives
  a pad the window's viewport), so every pad is a pad-only player's and keeps Epic's page (G-9's kbplayer test checks it).
- Tools: the `-ini:System:[SDL2Drv.SDL2Client]:KeyboardIsPlayer=False` G-9 added is the default now and was removed from
  `m10-controls.sh`, `fx-owner.sh`, `fps-bench.sh`, `g4-language.sh`, `m12-wine-smoke.sh`, `jit-strip.sh`,
  `f5-flicker.sh`, `decay-flash.sh`, `net-test.py`; `g6-splitscreen.sh`'s `$KBOLD` is gone (G-6's parts use the default)
  and `kbplayer` sets `PadsStartAtPlayerTwo=True` (`$KBPLAYER`). A run against an OLDER binary (G-9's, default True)
  would now need the key by hand: those scripts no longer pin G-6's model.
- New test part `tools/dev/g6-splitscreen.sh <tree> unplug` (`scen_unplug`, Xvfb, default ini, MSAA off as m10): pad 0
  on port 0 is player one (connect line), stick and keyboard both walk player one; pad 0 pulled: the disconnect line,
  NO `OpenMenu [UCMenu.ControllerRemovedMenu]`, no `pad 0 JoySlider2`, probe menu None and paused 0, port 0 kept, the
  keyboard still walks; pad 0 back: port 0, player one's line, no page, its stick walks player one; pad 1 joins as player
  two (Start in the match); pad 1 pulled: Epic's page opens (probe menu `ControllerRemovedMenu*`); back on port 1, its
  Start closes the page; ports [0, 1].

Native build at this source: `build-native` 0 errors (`logs/g11/build-native-0.log`; the strings are in `uc2.bin`).

### G-11 step 2 - native results (binary build-native at 2e17abe; run trees g11, g11ship = Epic's shipped GUI/UCGameplay/UCMenu)

| test | logs | result |
|---|---|---|
| `g6-splitscreen.sh g11 unplug` (new, default ini) | `logs/g11/unplug1` | PASS 26/26 (load 15.9 at start, 18.9 at the end) |
| `g6-splitscreen.sh g11 input menu kbplayer` | `logs/g11/g6` | input PASS 39, menu PASS 9, kbplayer (`PadsStartAtPlayerTwo=True`) PASS 34, kbmenu PASS 11 (load 15.8 at start, 22-24 during) |
| `m10-controls.sh` all five (M10_TREE=g11, default ini: the virtual pad is player one) | `logs/g11/m10`, `logs/g11/m10b` | first run: mouse 18, text 7, both 14 PASS; keyboard 15/16, pad 30/33; rerun (load < 16): keyboard PASS 16, pad PASS 34 |
| `d6-mouse.sh look pad` (D6_TREE=g11, D6_SHIP_TREE=g11ship) | `logs/g11/d6`, `logs/g11/d6.txt` | 48 PASS 0 FAIL (pad curves vs shipped worst 0.11%) |

Numbers from the unplug run (`logs/g11/unplug1/g11-unplug.inject`): pad 0's stick walked player one 442.2/329.6 units
(forward/back), the keyboard 450.5/364.9; after the pull: probe menu None, paused 0, viewport 0 still port 0, the keyboard
walked 543.8/365.9; pad 0 back: `SDL2 pads: pad 0 connected: UC2 M10 virtual pad (rumble) (player one, with the keyboard
and mouse)`, its stick 505.6/420.3, the keyboard 508.5/453.0; pad 1 joined (ports [0, 1]); pad 1 pulled:
`GUI.GUIController OpenMenu [UCMenu.ControllerRemovedMenu]`, probe menu `ControllerRemovedMenu*`; back on port 1, its
Start closed the page, ports [0, 1].

m10's first-run failures: the `pad` scenario's three unplug checks were written for Epic's page on player one's pad
(`pd-unplug-sends-joyslider2`, `pd-unplug-pauses-with-controller-removed-menu`, `pd-start-after-replug-resumes`): exactly
what the owner's rule removes. The scenario now checks the rule (`tools/dev/m10-controls.sh` `scen_pad`): the disconnect
line, no `JoySlider2`, no `ControllerRemovedMenu`, menu None and not paused, the re-plug line "player one, with the
keyboard and mouse" and the re-plugged stick walking player one (442 units). `keyboard`'s `kb-e-switches-to-melee` (w1
held a SniperRifle, a bot 62 units away and locked on) passed in the rerun with the same binary: not pad-related; the
keyboard scenario never touches a pad.

### G-11 step 3 - builds and the browser (both pad modes)

Builds (`bin/capped 6G`): native `-j4` (`logs/g11/build-native-0.log`), wasm-perf `-j2` (`build-wasm-perf.log`; the
preloaded data has `PadsStartAtPlayerTwo=False` and no `KeyboardIsPlayer`), windows MinGW `-j2` (`build-windows.log`,
`uc2.exe` has the new line): 0 errors each. No script changed: no package to switch.

Browser (frontend worktree `ufrontend-pads` at d51dfe1, serve-web assembled with this worktree's `build-wasm-perf`,
sha256 `af499b50d5ef18225652ef8426430f9bbd0e89419fb95632db0180192f761257` before and after each run; harness
`e2e-mem-guard --max-gb=23` inside `bin/capped 7G`, one run machine-wide, null sink by module index;
`logs/g11/run-gamepad-e2e.sh`):
- box unticked (default): 1 passed, 37/37 steps, peak 9.2 GB (`logs/g11/unticked-1.out`): the pad on port 0 "player one,
  with the keyboard and mouse", stick 409.1 and keyboard 502.1 units on the same player, pulled: no
  ControllerRemovedMenu, no JoySlider2, the keyboard walked 635.7 (not paused), back on port 0, stick 540.4.
- box ticked: 1 passed, 23/23 steps, peak 9.5 GB (`logs/g11/ticked-1.out`): the pad on port 1 ("Start joins"), moved
  nobody unjoined, Start joined player two (2 split views), its stick moved player two 829.5 / player one 0.0, the
  keyboard player one 631.6 / player two 0.0; pulled: Epic's ControllerRemovedMenu; back, Start closed it, stick 552.7.
  The first two-player split screen in a browser (G-6: "untested in a browser"); it used Epic's split detail (level 1).

Open: (1) with the box unticked, when player one's pad AND player two's are out and player two's is plugged back first,
it takes port 0 (the first free port) and becomes player one's, leaving player two's page waiting for port 1: SDL has no
fixed ports; a pad does not remember its port. Not built (a "port per device" rule would be a design change). (2) Not
measured: the old binary failing the new unplug test (a control run); the rule's code path is one `if` and the
removal line names it.

## H-3 (2026-10-11): the owner's system-link flow, the mouse after a join, the pad camera

`h3-pads.md`. In short: a player who joins in the menus left viewport 0's `bShowWindowsMouse` set (the GUI moved to the
overlay), so the mouse was never captured again in the split-screen match (fixed in SDL2Drv `CursorWanted`/`Tick` and
`UnSplitScreenMgr`); pad-only players got the mouse's camera rules until they touched the right stick (fixed in the
`mouse-look` patch set); in the browser the Start press that makes Chrome list the pad was lost in SDL's Emscripten
driver (fixed in `SDL2Pads.cpp`). New part `g6-splitscreen.sh <tree> h3menu`.
