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
