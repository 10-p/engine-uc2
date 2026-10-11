# H-3: "Force gamepads to start with second player" on the web, the pointer, the pad camera (2026-10-11)

Worktrees: engine `/home/ion/WorkUC2/engine-uc2-h3-pads` (branch `h3-pads` from main 7ceeee7), frontend
`/home/ion/WorkUC2/ufrontend-h3` (branch `uc2-h3-pads`). Logs `/home/ion/WorkUC2/logs/h3`, run trees `run/h3*`.
Everything through `bin/capped` (builds 6G, game 2G, harness 7G).

The owner (verbatim where it matters): "'Force gamepads to start with second player' doesn't work, I have the gamepad
plugged in, but it still controls the first player. ... if I press start (for the second player) the first player
starts the game." Flow: Network combat, Host System link game, ignore the profile nag, accept 3 times, the character
page reads "Player 1 No Profile" / "Player 2 Press start". Once it worked "kinda broken": mouse lock gone, could not
lock again, the browser's pointer on screen (temp/POINTERWAT.png). And: "I don't think we reenable the traditional
camera control for players 2 and above".

## Step 0 - reading (no run yet)

- Port assignment: `SDL2Pads.cpp` `PadAdded` is the ONLY place a pad gets a port, for start-up enumeration and
  hot-plug alike (both arrive as `SDL_CONTROLLERDEVICEADDED`); it starts at 1 when `PadsStartAtPlayerTwo`. `PadJoin`
  (`SDL2Client.cpp`) never gives the window's viewport to a pad with the key on. So by reading, the hot-plug path
  honours the key; the cause has to be measured (the key's value at run time, the menu flow, the page).
- The G-11 browser test (`uc2-wasm-gamepad.spec.js`, "box ticked") joins IN A MATCH (`SplitScreenJoinInMatch`), never
  through UC2's character page; the native kbmenu test joins on the Instant Action character page with the pad
  attached at the main menu. Neither is the owner's flow (system link host, pad arriving on the character page).
- The cursor: `USDL2Client::CursorWanted` = any viewport with `bShowWindowsMouse` that is viewport 0 or shows the menu.
  GUIController's `NativePreRender` (`UnGUICore.cpp:1600`) sets `bShowWindowsMouse = bWindowsMouseAvailable` on ITS
  `ViewportOwner` every menu frame, `SetControllerStatus(false)` (GUIController.uc) clears it on the owner when the last
  menu closes. When a split-screen player joins in the menus, `USplitScreenMgr::CreateOverlayViewport`
  (`UnSplitScreenMgr.cpp:200-201`) moves the GUIController to the overlay viewport (`ViewportOwner = OverlayView`), so
  viewport 0 keeps the `bShowWindowsMouse=1` the menus left on it, nobody clears it, and `CursorWanted()` stays true
  through the match: every `SetMouseCapture( !CursorWanted() )` releases the mouse (on the web: SDL exits pointer lock).
  Hypothesis, to measure (instrumented: `SetMouseCapture` logs who wants the cursor when the capture changes, with
  LogInput; the test channel's `viewports` line has `captured=` `cursor=`).
- The camera: patch set `mouse-look` (`UCPlayerInput.PlayerInput`): `bMouseOwnsLook` starts as `!Level.IsXBox()`
  (the mouse on PC) for EVERY player, and only a right-stick motion (`aMouseX/aMouseY` not counted as mouse) gives it to
  the pad. A pad-only player (2-4) therefore starts under the mouse's camera rules (rigid camera, no auto-centre pitch,
  no aim adhesion) until it touches its right stick, although the mouse never reaches its viewport (G-6: keyboard and
  mouse go to the window's viewport only). To measure (`vp ... mouselook=` per viewport in the test channel).

## Step 1 - native reproduction (`tools/dev/g6-splitscreen.sh <tree> h3menu`, new part; logs/h3/n1-n3)

New part `h3menu` (`scen_h3menu`): the owner's flow natively under Xvfb, both modes (`H3_MODES="ticked unticked"`):
main menu -> NETWORKED COMBAT -> HOST SYSTEM LINK GAME -> the profile box (Escape) -> Return x3 -> `UC2Arena_Character`;
the virtual pad is ATTACHED THERE (hot-plug, as a browser lists a pad at its first press) and its Start pressed; player
two's own profile box answered with the pad's B; player one: Left + Return (Arclite), player two: A; both accept on
through the following pages; the match. Test channel additions (general, LogInput/test only): the `viewports` line ends
`captured=<window captured> cursor=<CursorWanted>`, every `vp` line `mouselook=<bMouseOwnsLook or -> pawnclass=<class>
winmouse=<bShowWindowsMouse>/<bWindowsMouseAvailable> guiowner=<self|other|none>`; `SetMouseCapture` logs
`mouse capture on|off (cursor wanted N): <per viewport>` when the capture changes and LogInput is on.

Measured on main's code + the instrumentation (run tree h3, load 13-21):
- Ports and the join: NOT broken natively. Ticked: `SDL2 pads: pad 1 connected: UC2 M10 virtual pad (rumble) (keyboard
  and mouse are player one on port 0: Start joins)`, `Split screen: pad 1 Start in the menus: joined`, two split views
  with pads [0, 1], player one Arclite (`UCPawn_Jugg_Arclite`), player two `UCPawn_Nakhti_Anubis`, the match
  (CTF-Aqueduct) with two views. Unticked (pad 0 at the main menu = port 0, pad 1 on the character page): the same.
- The cursor (n2/n3 `vps`/`vp`): character page before the join `captured=0 cursor=1` (windowed menus: the OS cursor,
  as in every single-player menu); after the join viewport 0 `winmouse=1/1 guiowner=other` (the GUI went to the
  overlay, `UnSplitScreenMgr.cpp:200-201`), the overlay `winmouse=0/0 guiowner=self` (a sub-viewport never got
  `bWindowsMouseAvailable`: its GUI drew the game's own cursor too); IN THE MATCH `captured=0 cursor=1`, viewport 0 still
  `winmouse=1/1 guiowner=other`: the stale flag keeps `USDL2Client::CursorWanted()` true, the mouse is never captured
  again. Same natively (Xvfb) as the owner saw in the browser (SDL's Emscripten driver exits pointer lock on
  `SDL_SetRelativeMouseMode(SDL_FALSE)`). Also: `UGameEngine::Tick`'s "release the cursor while paused / take it back"
  (`UnGame.cpp:3650`) only runs with ONE viewport, so in split screen nothing took the mouse back after the menus.
- The camera (`vp m2 ... mouselook=`): player two (pad only) `True` in the match: the mouse's camera rules on a player
  the mouse never reaches.

## Step 2 - the fixes (native)

Cursor and capture:
- `Source/SDL2Drv/Src/SDL2Client.cpp` `CursorWanted`: a viewport counts only while it owns the GUIController
  (`GUIController->ViewportOwner==V`) and the GUI is active. Single viewport: unchanged (the GUI is always viewport 0's,
  and the flag is only set while it is active).
- `Source/SDL2Drv/Src/SDL2Client.cpp` `Tick`: with more than one viewport the window's capture follows
  `CursorWanted()` (Epic's one-viewport rule of `UGameEngine::Tick`, re-capture only with `CaptureMouse`).
- `Source/Engine/Src/UnSplitScreenMgr.cpp` `SetupOverlayViewport` / `RemoveOverlayViewport`: `bShowWindowsMouse` moves
  with the GUI (cleared on viewport 0 when the overlay takes the GUI; set back from the GUI's state when it returns), so
  Epic's one-viewport tick after the last split player left is not fooled by a stale 1 either.
- `Source/SDL2Drv/Src/SDL2Viewport.cpp` `OpenWindow` (sub-viewport branch) and `ResizeViewport` (sub-viewport):
  `bWindowsMouseAvailable` = the window viewport's, so the split menus show the window's cursor exactly as the single
  player's menus do (G-6's `UpdateMousePosition` already reads it for the overlay), no second, software cursor.
Camera (patch set `mouse-look`, `UCPlayerInput.PlayerInput`; `patches/reference.sha256` regenerated, P2 PASS):
- `bPadOnly` = `GetSplitscreenLayout` index > 0 (a viewport other than the window's: G-6 sends keyboard and mouse to the
  window's viewport only); a pad-only player starts with the pad's look and never gets the mouse's.
- Any pad stick hands the look to the pad: `aForward`/`aStrafe` (the left stick: the keys move through `aBaseX/aBaseY`)
  as well as the right stick; the mouse still wins a tick in which both moved. So player one who puts the mouse down
  and walks with the pad gets Epic's pad camera at once (before: only after touching the right stick).

Native result (logs/h3/n5, load 16-21): `PASS h3-ticked (11 checks)`, `PASS h3-unticked (13 checks)`: in the match
`captured=1 cursor=0`; mouselook [player one, player two] after mouse motion = [True, False]; unticked: after pad 0's
left stick alone [False, False], after the mouse again [True, False].
