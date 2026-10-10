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
