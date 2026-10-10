# Split-screen on PC (E-1)

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
