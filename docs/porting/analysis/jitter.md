# Jitter notebook: the player's own pawn lags and snaps in front of the third-person camera

Worktree `/home/ion/WorkUC2/engine-uc2-jit`, branch `jitter` (from main 1519163). Run trees `<WorkUC2>/run/jit*`,
logs `<WorkUC2>/logs/jit/`. Every run goes through `<WorkUC2>/bin/capped` (CLAUDE.md). Appended after every step.

Owner's words (2026-10-08): "Character movement in third person is jittery, not the camera, the camera moves
smoothly, it is the character that moves jittery. Typical, the camera moves forward, but the character stays still
in place while the camera moves forward and then it updates." Plays mostly on the web preview, also natively; input
not specified.

## Step 0 (2026-10-08): reading — the order of a frame, and a hypothesis to measure

The order of one engine frame (file:line in this tree):
- `UGameEngine::Tick` (`Source/Engine/Src/UnGame.cpp:3505`): `GLevel->Tick` (`:3577`), then `Client->Tick` (`:3792`)
  which repaints the viewport: `UGameEngine::Draw` (`:3130`).
- In the level tick a pawn is ticked after its controller (`APawn::CheckOwnerUpdated`, `UnLevTic.cpp:169-174`,
  "controller-first updating"). The local `PlayerController` runs `PlayerTick` (`UnLevTic.cpp:593-595`) ->
  `PlayerWalking.PlayerMove` (`UCPlayerController.uc:3883-3985`): `UpdateRotation`, then **`CalculateAimInfo`**
  (`:3973`) -> native `execCalculateAimInfo` (`UCGameplay.cpp:176-178`) -> **`eventPlayerCalcView`**: the camera of
  this frame is computed here, from the pawn's location BEFORE this tick's physics, and then `ProcessMove` only
  sets the acceleration. The pawn moves afterwards, in its own tick (`AActor::TickAuthoritative` ->
  `performPhysics`, `UnLevTic.cpp:242-243`).
- `UCPlayerController.PlayerCalcView` (`:3134-3158`) computes the view "only once per frame": a second call with
  the same `Level.TimeSeconds` returns `CurrentCameraLocation/Rotation`. `UGameEngine::Draw` calls PlayerCalcView
  (`UnGame.cpp:3147`) and so renders with the camera from before the move, while the pawn's mesh is drawn where the
  physics left it.

Hypothesis (to be measured, not established): on screen the pawn is displaced from its rest place by its movement
during the frame, `Velocity x DeltaTime`.
- Epic's lazy camera (`execCalcThirdPersonView`, `UCGameplay.cpp:289-619`; still used under the pad) moves toward
  its target (pre-move pawn + offset) by `k x dt` of the remaining distance (MoveVectorTo with speed
  `CameraSpeed x distance`). Lag after frame n: `L_n = v dt_n + (1 - k dt_n) L_(n-1)`; in steady running
  `L = v/k` and then `L_n = v/k` whatever `dt_n` is: the pawn's screen position does not depend on the frame time.
- D-6's mouse camera (`MouseLookCamera`, `UCPlayerController.uc:3061-3067`, called from `CalcBehindView` `:3227`)
  puts the camera at the target at once: `L_n = v dt_n`. With uneven frame times (the web: 25-48 ms frames measured
  in web-renderer.md) the pawn sits `v x dt` ahead of its rest place, different every frame: ahead after a long
  frame, back after a short one ("the character stays still while the camera moves forward and then it updates").
  At ~600-700 units/s and 16.7 vs 33.3 ms frames that is 10-12 vs 20-23 units at a camera ~150-250 units away.
- So the order (camera before the move) was harmless under Epic's lazy camera and shows under the rigid one.
Other candidates to rule in or out with the same trace: `WalkBob` (the camera's target includes it:
`UCGameplay.cpp:418`; Bob 0.008 x speed = a few units of sway), the mesh drawn away from `Location` (PrePivot, root
bone motion), AutoFOV's per-frame FOV, frame-rate-dependent smoothing.

Plan: (1) a quiet-by-default per-frame trace in `UGameEngine::Draw` (camera used for the frame, FOV, pawn location,
velocity, WalkBob, the mesh's root bone as drawn, frame dt, the pawn's projected screen position) and a native
scenario (keyboard+mouse, pad; shipped and rebuilt UCGameplay.u; 60 fps, uneven frame times, uncapped), a browser
spec logging the same; (2) cause with evidence; (3) fix at the cause; (4) verify.

## Step 1 (2026-10-08): the trace, the scenario, the first numbers

Engine, quiet unless asked for:
- `Source/Engine/Src/UC2FrameTrace.cpp`: console command `FRAMETRACE <frames> [tag]` (also the test channel's `exec`,
  the web's `ue_exec_command`) or `-frametrace=<frames>`: one line per frame the first viewport draws, logged by
  `UGameEngine::Draw` after the frame (`UnGame.cpp`, before `LevelAction` is restored; the AutoFOV scope still
  applies): `frametrace <tag> <i> t= dt= cam=() camrot=() fov= view=WxH pawn=() vel=() bob=() root=() scr=() rscr=()
  phys= look=`. `cam/camrot` = what the frame was drawn with; `root` = the skeletal mesh's root bone as posed for the
  frame (`GetBoneCoords(0)`); `scr/rscr` = pawn / root projected as `FCameraSceneNode::UpdateMatrices` does (viewport
  pixels); `look` = `UCPlayerInput.bMouseOwnsLook`. The engine's log lines carry no log-name prefix, hence the literal
  `frametrace`.
- `Source/SDLLaunch/Src/SDLLaunch.cpp`: `-framepattern=<ms>:<ms>:...` gives the frames these lengths in turn (a game
  without a cap of its own): a browser's uneven pacing, repeatable natively. (`:` because `Parse()` stops a value at
  a comma: the first try parsed one length.)
- `tools/dev/jit-trace.sh <name> <tree> <kbm|pad> <60|uneven|uncapped>`: one game run (m10-controls.sh `--file`:
  Xvfb inside `capped 2G`, `-nullrenderer`, DM-Deadbolt without bots, God, 1920x1080 = 16:9 with AutoFOV),
  the player put at a fixed open spot (`NETPLACE LocalPlayer 1977.8 583.7 -34.1 33000`; random spawns had the
  camera arm shortened by walls in two segments: 190 -> 48 units), four segments of 1.3 s with the trace on
  (forward, back, strafe right, strafe left; W/S/D/A or the virtual pad's left stick after a right-stick push
  that gives the pad the look). `uneven` = `-framepattern=16.7:33.3:16.7:16.7:25:33.3` (mean 23.6 ms, the browser's
  25-48 ms frames in web-renderer.md).
- `tools/dev/jit-analyze.py`: per segment the steady frames (walking, horizontal speed within 3% of the top, view
  rotation unchanged, heading within 2 deg); jitter = RMS and max of the frame-to-frame change of the pawn's screen
  position; `ahead = a + b (v dt)`: the pawn's distance in front of the camera along its velocity against its move
  of that frame.
- `tools/dev/script-build.sh`: accepts `jit*` trees (logs in `logs/jit`).

First numbers, main's scripts (asset tree `UCGameplay.u` 17c1750c = what the owner has), keyboard + mouse
(`logs/jit/trace/before-kbm-{60,uneven}.*`):
- 60 fps (dt 16.8 ms +-0.1): jitter RMS 1.7-2.3 px, max 2.5-4.9 px; screen sd 8-9 px in x and 6-9 px in y: a smooth
  sway, the camera's `WalkBob` (max 3.7 units; the mouse's rigid camera carries all of it).
- uneven: jitter RMS 9.5 (forward), 9.9 (back), 21.4 (right), 21.1 (left) px, max 16.6-32.9 px per frame; and
  **`ahead = 190.00 + 1.000 x v dt` (forward, residual 0.00), `-190.00 + 1.000 x v dt` (back), `1.018` / `1.010`
  (strafes)**: in every frame the pawn is drawn exactly its own move of that frame (7.2-15.0 units here) in front of
  its rest place, i.e. the frame's view is from before the move. The hypothesis of step 0 is measured.

## Step 2 (2026-10-08): the cause, and the fix at it

Cause (measured in step 1 and by every rigid-camera run of step 3): the frame is drawn with the view computed in
`PlayerWalking.PlayerMove` -> `CalculateAimInfo` -> `PlayerCalcView`, i.e. BEFORE the pawn's physics of that tick
(the pawn ticks after its controller), and `UCPlayerController.PlayerCalcView` returns that cached view to
`UGameEngine::Draw`. Under D-6's mouse camera (rigid: `MouseLookCamera` puts the camera at `DesiredCameraLocation`)
the pawn is therefore drawn exactly its own move of that frame ahead of its rest place: `ahead = rest + 1.000 x v dt`
with a residual of 0.00-0.01 units in all four directions. With even frames that is a constant offset (invisible);
with uneven frames (any browser; a native hitch: one 37.5 ms frame at "60 fps" moved the pawn 20 px) the pawn lurches
ahead after a long frame and falls back after a short one. Epic's lazy camera (the pad, the shipped scripts) closes
`k x dt` of the distance each frame, so its lag `L_n = v dt_n + (1 - k dt_n) L_(n-1)` stays `v/k` whatever `dt_n`
is: measured judder 0.15-0.44 px on the flat segments at every pacing; it was never a problem there, and D-6 exposed
it. Not the cause: AutoFOV (FOV 106.26 in every frame of every run), the mesh (the root bone's judder follows the
pawn's: 9.3 vs 10.5 px before, 0.5-0.9 px after), frame-time clamps (none reached), WalkBob (a smooth sway, judder
0.03 px once it is taken out, see below).

Fix (`Source/UCGameplay/Classes/UCPlayerController.uc`, `UCPlayerInput.uc`; UCGameplay.u rebuilt): when this frame's
camera was the mouse's rigid one (`MouseLookCamera` stamps `UCPlayerInput.MouseCameraTime = Level.TimeSeconds`) and
the pawn has moved since it was computed (`Pawn.Location != OldPawnLocation`, the place the native
`CalcThirdPersonView` last computed from), `PlayerCalcView` computes the view again instead of returning the cached
one (`MouseCameraLeftBehind()`): the frame is drawn from where the pawn is drawn. Order of operations, no smoothing.
The aim (`CalculateAimInfo`) keeps Epic's order; the pad's lazy camera and the end-of-round focused views
(`CalcFocusedView` in RoundIntermission/GameEnded) are never computed twice in a frame (a second step of a lazy
camera would change its speed); the new variable lives in the non-native `UCPlayerInput` (no C++ layout change).
Side effect, wanted: the HUD's and the next tick's `CurrentCameraLocation` is the view that was drawn.
Script build: `tools/dev/script-build.sh --tree jit-make assemble make strip`: `Success - 0 error(s), 85 warning(s)`
(M14's count; `logs/jit/jit-make-make.log`). Run tree `run/jit-fix` = asset tree + `run/jit-make/System/UCGameplay.u`.

The camera's walk bob (Epic's `Pawn.CheckBob`, kept for third person on purpose by UC2: `UCPawn.CheckBob` skips
UnrealPawn's behind-view test) is in the camera's target (`UCGameplay.cpp:418`): the rigid camera carries all of it
(the pawn sways +-8-9 px at 1080p, smoothly, 1.3 Hz sideways / 2.5 Hz up and down: jitter RMS 2.2 px at 60 fps, judder
0.26 px), the lazy camera passes about 60-70% of it (|i w / (k + i w)| for k 11-15/s). Left as it is (Epic's design);
an owner choice in the report.

## Step 3 (2026-10-08): the native matrix, before and after (measured)

`tools/dev/jit-trace.sh` for every combination (driver `logs/jit/matrix-1.txt`; logs `logs/jit/trace/<run>.*`; the
table re-made with the final analyser: `logs/jit/trace/table-1.txt`). Trees: `jit-main` = main's binary and asset
tree (UCGameplay.u 17c1750c, what the owner has), `jit-fix` = the same + this branch's UCGameplay.u, `jit-ship` = the
SHIPPED GUI/UCGameplay/UCMenu (lazy camera always; the keyboard only: with D-6's ini the shipped package has no
mouse look). 1920x1080, AutoFOV 106.26. Judder RMS / max in px, b = slope of the pawn's place against v x dt:

| run | pacing (dt ms) | fwd | back | right | left | b |
|---|---|---|---|---|---|---|
| kbm main (before) | uneven 16.8-33.8 | 10.51 / 15.7 | 10.97 / 16.3 | 20.44 / 30.1 | 20.67 / 31.9 | 1.000 (all four) |
| kbm fix (after) | uneven | 0.53 / 1.0 | 0.34 / 0.7 | 0.51 / 1.0 | 0.52 / 1.0 | 0.000 (all four) |
| kbm main | 60 (16.8-37.5) | 0.36 / 1.4 | 2.99 / 20.3 | 1.01 / 5.1 | 3.01 / 10.8 | 0.995-1.000 |
| kbm fix | 60 (16.7-19.8) | 0.26 / 0.4 | 0.18 / 0.3 | 0.27 / 0.4 | 0.26 / 0.4 | 0.000-0.015 |
| kbm main | uncapped (0.6-4.8) | 0.20 / 2.5 | 0.23 / 2.6 | 0.40 / 3.4 | 0.80 / 5.1 | 0.97-1.08 |
| kbm fix | uncapped | 0.01 / 0.04 | 0.01 / 0.03 | 0.01 / 0.03 | 0.01 / 0.04 | ~0 |
| pad main | uneven | 0.36 / 0.7 | 0.43 / 0.8 | 1.03 / 4.3 | 1.56 / 5.0 | (lazy: not a straight line) |
| pad fix | uneven | 0.38 / 0.7 | 0.44 / 0.9 | 0.97 / 3.3 | 1.82 / 8.5 | |
| pad main / fix | 60 | 0.15 / 0.15 | 0.18 / 0.17 | 0.56 / 0.74 | 1.95 / 1.88 | |
| pad main / fix | uncapped | 0.01 / 0.00 | 0.01 / 0.01 | 0.26 / 0.30 | 0.36 / 0.38 | |
| shipped scripts, kbm | uneven / 60 / uncapped | 0.89 / 0.78 / 0.01 | 0.52 / 0.22 / 0.01 | 1.08 / 0.67 / 0.30 | 2.20 / 2.19 / 0.32 | |

Read:
- Keyboard + mouse: the defect is the frame time: judder 10-21 px RMS with uneven frames before, 0.3-0.5 px after; at
  "60 fps" ONE late frame (37.5 ms) moved the pawn 20 px before (back segment), nothing after. With the bob taken out
  the pawn stands within 0.03-0.04 px of one spot on the screen after the fix, at every pacing (nobob judder; jitter
  0.04 px RMS). What remains as drawn is the camera's walk bob: a smooth sway, jitter RMS 2.2 px at 60 fps / 3.3 px
  at uneven frames (its frame-to-frame step grows with the frame length), judder 0.26 / 0.5 px.
- Pad: unchanged by the fix (the lazy camera is never computed twice), as expected; the shipped scripts behave the same.
  Its flat segments are clean (0.15-0.44 px). Its strafes keep 0.6-2.2 px judder at EVERY pacing, also at an even 60:
  not the frame time. The strafe segments run down a ramp (z -34 -> -100) and back up; the lazy camera trails the pawn
  vertically (`CameraInterpSpeed.Z` 15/s) and the pawn moves on the screen where the slope begins and ends (seen in
  the frames: screen y +16 px over two frames when the climb stops while the camera keeps rising). Epic's camera,
  identical in the shipped scripts; not the owner's complaint (the camera's lag on slopes is smooth motion, a few px).
- Uncapped (the null renderer's 200-1,500 fps): the defect is there but small (v dt 0.3-1.4 units).

The strip for the owner (`tools/dev/jit-strip.sh` + `jit-strip.py`; GPU, headless, `SDL_VIDEODRIVER=offscreen`,
1280x720, uneven frames 40-70 ms because saving each picture costs ~35-45 ms; the pawn runs forward on the pad's left
stick with the MOUSE owning the look): `logs/jit/strip/strip-2.png` (12 consecutive frames per row, a fixed cross where
the pawn is on average, a chart of its height on the screen per frame) and `strip-2.gif` (before | after, slowed to
150 ms a frame). The pawn's height on the screen, frame by frame (frame length: offset px, + = higher = further ahead):
- before: 70:+13.2 40:-2.1 40:-4.3 55:-1.2 70:+2.5 40:-12.6 70:+8.2 44:-1.5 41:-1.6 55:+4.8 70:+6.6 40:-12.2
  (every long frame throws the pawn ahead, every short one drops it back: up to 25 px between two frames)
- after:  40:+5.4 55:+5.7 70:+0.2 40:-3.7 70:-6.4 40:-4.7 40:-1.1 55:+4.1 70:+5.9 40:+3.6 70:-3.1 40:-5.9
  (a smooth sine whatever the frame length: the walk bob)
