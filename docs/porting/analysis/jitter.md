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
