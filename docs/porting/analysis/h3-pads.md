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

## Step 3 - the browser (frontend worktree `ufrontend-h3`, `uc2-wasm-gamepad.spec.js`, logs/h3/web)

serve-web assembled in the frontend worktree from this worktree's `build-wasm-perf` (sha256 recorded before/after each
run by `logs/h3/run-gamepad-e2e.sh`); harness `e2e-mem-guard --max-gb=23` inside `capped 7G`; null sink by module index.
New tests in the spec: "the owner's flow" in three variants (box ticked with the pad's FIRST press being Start on the
character page; box ticked with the pad pressed at the main menu first; box unticked with player one's pad listed at the
main menu and a second pad's Start joining), and "box ticked, then a second launch". The fake pad now arrives as a real
one does (`lib/fake-gamepad.js` `held`: the press that makes the browser list a pad is down in its first snapshot) and
there can be several (`use(index)`). Every run saves the whole engine log as `engine.log` in the test's output folder.

Measured on the build with step 2 (wasm 69849688..., owner-ticked-2):
- **The cause on the web**: the pad that appears through its Start on the character page gets port 1 (`SDL2 pads: pad 1
  connected: Standard Gamepad (keyboard and mouse are player one on port 0: Start joins)`), but its Start is NEVER seen:
  no `pad 1 button 12` line, no join, the page stays. SDL's Emscripten joystick driver
  (`emsdk/.../ports/sdl2/SDL-release-2.32.10/src/joystick/emscripten/SDL_sysjoystick.c:76-85`) copies the connect
  event's buttons as the pad's previous state and `EMSCRIPTEN_JoystickUpdate` (`:343-353`) reports changes only, so
  the press that made Chrome list the pad is the starting state, never a press; its release is a release of a button SDL
  has up. The port assignment itself honours `PadsStartAtPlayerTwo` (pad 1 in every ticked run).
- Pad pressed at the main menu first (variant 2): the listing press likewise never reached the engine (`the press that
  listed the pad reached the engine: false`); the unjoined pad's D-pad on the main menu was NOT handled by the menus
  (ticked: an unbound pad on port 1); its later Start on the character page joined (`Split screen: pad 1 Start in the
  menus: joined`), and the rest passed: Arclite / Anubis spawned, `Split screen: 2 views`, `PADS` pad 1 bound to a
  viewport, `mouse capture on (cursor wanted 0)` as the last capture line (step 2's fix), mouselook [false, true].
- Second launch (second-launch-1, 1 passed): the box reads ticked back from the engine's ini on the next page load and a
  pad pressed in the game takes port 1 without a seed: the launcher's `-ini:` seed and THE RULE are not at fault.
- Not reproduced: "it still controls the first player" with the box ticked. In every ticked run the pad was on port 1
  and unbound, and its D-pad did not move the menus. A pad that drives player one's menus and whose Start starts the game
  is what the UNTICKED mode does (port 0 = player one). Owner question (the engine log's `SDL2 pads: pad N connected`
  line of such a session says which).

Fix (web only, `Source/SDL2Drv/Src/SDL2Pads.cpp` under `__EMSCRIPTEN__`, fields in `SDL2Drv.h` `FSDL2Pad`):
`PadAdded` reads the browser pad's pressed buttons once (`WebPadPressed`, a synchronous main-thread call as G-10's
probe), maps them through SDL's own binding of the "standard" mapping (`SDL_GameControllerGetBindForButton/Axis`) to
controller buttons and trigger axes (`HeldAtConnectButtons/Axes`), logs `SDL2 pads: pad N appeared with a press (browser
buttons 0x...)`, and `ReadPad` reads them as down in its first read only (a tap: pressed, released on the next read).
So the Start that lists the pad is a press of Start: `SendButtonEdges` -> `PadJoin`. Native SDL drivers report a held
button's state at open (evdev), so native needs nothing.

## Step 4 - regressions (native, binary at 9d3fcc1; run trees h3 = rebuilt packages, h3ship = Epic's shipped GUI/UCGameplay/UCMenu)

Load 15-29 throughout (other agents' builds and harness runs; nothing quieter was available for hours).

| test | logs | result |
|---|---|---|
| `g6-splitscreen.sh h3 input menu kbplayer unplug` | logs/h3/reg | input PASS 39, menu PASS 9, kbplayer PASS 34, kbmenu PASS 11, unplug PASS 26 |
| `m10-controls.sh` (M10_TREE=h3) | logs/h3/m10 | keyboard 16, mouse 18, text 7, pad 34, both 14: PASS |
| `d6-mouse.sh look pad` (D6_TREE=h3, D6_SHIP_TREE=h3ship) | logs/h3/d6, logs/h3/d6.txt | 48 PASS 0 FAIL (pad yaw vs shipped worst 0.43%) |
| `g6-splitscreen.sh h3 h3menu` | logs/h3/n5, n6, n7 | ticked PASS 11, unticked PASS 13 (three runs) |
| `g6-splitscreen.sh h3ship h3menu` (Epic's shipped scripts: vanilla) | logs/h3/reg-ship3 | ticked PASS 11, unticked PASS 13 (join, Arclite, two views, mouse captured; look owner `-`) |

The first shipped-scripts run (reg-ship) failed only the look-owner checks (Epic's UCGameplay has no `bMouseOwnsLook`:
`-`; the checks now accept that, Epic's pad camera for everybody); a second (reg-ship2, load 29) failed the unticked
flow at the map list (the third accept went to the page while it was still animating, the Left meant for the
character page went to the map list): the scenario now confirms each accept (`wait handled XC_A`) and waits for the
character page by probe.
