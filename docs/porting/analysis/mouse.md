# D-6 notebook: mouse and game pad separated (mouse look like a PC shooter, the pad as on the Xbox)

Worktree `/home/ion/WorkUC2/engine-uc2-d6`, branch `d6-mouse` (from main 47b1b18). Run trees `<WorkUC2>/run/d6*`,
logs `<WorkUC2>/logs/d6/`. Every run goes through `<WorkUC2>/bin/capped` (CLAUDE.md). Appended after every step.

Owner's words (PLAN.md, review of 2026-10-06): mouse "mapped to the gamepad joystick ... no relative movement, if
you exceed the gamepad movement speed, you get capped. It is extremely sensitive ... a tiny bit of movement will
make the camera jerk ... when you move the camera, the camera itself slides to the direction of the movement as if
it wanted to show you the area you're seeing", "the camera movement feels hexagonal if I try to do a circle",
"separating the inputs of both really rigorously", and "keyboard bindings being backwards" (no example).

## Step 0 (2026-10-07): reading — what the owner played, and where each symptom comes from

What the owner played: `run/play` (made 2026-10-06 22:04 by `bin/play-uc2` from main with M10): its User.ini has
M10's values (`MouseX=... Axis aMouseX Speed=1.0`, `bNonlinearAccel=false`, limits 1,000,000 / 10,240,000,
`bAccelMultipliers=false`, `bAutoCenterPitch=false`). So the complaints are about M10's mouse, not Epic's.

The input path today (file:line in this tree):
- `UInput` (`Source/Engine/Src/UnIn.cpp:289-327`): a mouse count adds `0.01*Speed` to its axis (IST_Axis); a held
  key adds `dt*Speed` (IST_Hold); a stick (`SpeedBase`) adds `SpeedBase*deflection` (M10's PC pre-division).
  `ReadInput` (`:479-500`) scales every input float by `20/dt` on PC, and `ReadInput(-1)` zeroes them after
  `PlayerTick` (`UnLevTic.cpp:593-595`).
- Mouse and right stick feed the SAME variable `aMouseX/aMouseY` (`DefUser.ini` `MouseX=Count bXAxis | Axis aMouseX`,
  `JoyU=Axis aMouseX SpeedBase=100`; the profile's `ControllerConfiguration.ApplyJoysticks` writes the stick there
  too, `Source/Engine/Classes/ControllerConfiguration.uc:84-111`).
- `UCPlayerInput.PlayerInput` (`Source/UCGameplay/Classes/UCPlayerInput.uc:312-410`) treats that variable as a stick:
  clamp to `aMouseX_Limit`, friction (`GetTurnFriction` `:456`), hold-acceleration (`AccelMouse` `:429`), power
  fall-off, then a turn RATE `aTurn` (x `aTurn_Limit`, `MouseSensitivity_X`, `DesiredFOV/90`); invert `bInvertMouse`.
- `UCPlayerController.UpdateRotation` (`UCPlayerController.uc:2705-3017`): `Yaw += 32*dt*aTurn` (int truncation per
  tick), aim adhesion `GetTurnAdhesion/GetLookUpAdhesion` (`:2740-2741` -> `UCPlayerInput.uc:522,569`), lock-on
  pull/clamp/break-away (`:2767-2918`, break-away `Abs(aMouseX) >= LockPullOffStrength (98)` for 0.25 s at the clamp,
  `:2857`), auto-centre pitch while moving (`:2935-2983`).

Symptom by symptom:
1. "Extremely sensitive": M10's numbers give 52.4 yaw units per count = 0.29 deg/count (controls.md step 5; its
   "16 cm per 360 at 800 dpi" is wrong by 4x: 360/0.29 = 1,240 counts = 3.9 cm at 800 dpi). Common PC defaults are
   0.022 deg/count x 1..3 (Quake/Source convention; CS's default 2.5 = 0.055 deg/count, about 21 cm per 360 at 800 dpi).
   On top, a profile (D-3 starts the game in the last one) applies its pad sliders (`PlayerProfile.SensitivityX/Y`,
   default 0.8) to `MouseSensitivity_X/Y` (`PlayerProfile.uc:1146-1175` -> `UCPlayerInput.UpdateSensitivityX/Y`),
   i.e. the mouse's speed is the pad's slider.
2. "Capped": with Epic's values the clamp `aMouseX_Limit=100` caps the mouse at 500*dt/Speed counts per tick (M10
   made the limits huge, so not that any more). What still makes fast motion "stop": the third-person camera (3, the
   view swings back) and friction (a target near the crosshair scales the mouse by down to 0.75/0.65, `DefUser.ini`
   PC values) — friction is a stick feature applied to the mouse.
3. "Camera slides toward the movement": the third-person camera is a lazy camera (`execCalcThirdPersonView`,
   `Source/UCGameplay/Src/UCGameplay.cpp:289-610`): its position is the pawn's eye point plus an offset rotated by the
   VIEW YAW, and it moves toward that desired position with `MoveVectorTo` at `CameraInterpSpeed` (about 6-15/s,
   `DefUser.ini` `[UCGameplay.UCPlayerController]`), while its rotation is the controller's rotation at once. A mouse
   turn swings the desired position around the pawn in one frame; the camera then slides there over ~0.2-0.3 s
   after the mouse has stopped. Also: auto-centre pitch is ON whenever a profile is applied (`PlayerProfile.bAutoCenter`
   default true -> `SetAutoCenterPitch`, `PlayerProfile.uc:1131-1144`; the M10 ini value is overwritten), so walking
   with WASD pulls the pitch back to the centre while the mouse rests (the reddit guide's "turn OFF Centerview"),
   and aim adhesion drags the view along with a moving target while the player moves.
4. "Hexagonal circle": per-axis nonlinearities in the look path. Candidates: `MoveVectorTo` (`UnScript.cpp:5483-5525`)
   snaps each world component separately when it overshoots, with view-dependent speeds per axis (the camera's
   position path around the pawn becomes piecewise straight), friction's separate X/Y factors, and with Epic's values
   the per-axis clamp plus the squared fall-off (a circle in, a rounded diamond/square out). To be measured.
5. Look keys: M10 put Left/Right/PageUp/PageDown on `aMouseX/aMouseY` (stick path).
6. Stick movement on PC: `PlayerWalking.PlayerMove` multiplies by 24000 only on the Xbox (`:3849-3857`, `:4027-4035`);
   M10 compensated with `JoyX/JoyY SpeedBase=2400000` in `DefUser.ini`, which a profile with swapped sticks undoes
   (`ApplyJoysticks` writes `SpeedBase=100`) and which saturates `Ctrl_Move_*_Pct` (the stick direction used for
   SmartJump dodges, lock-target search, input sequences).
7. Keyboard and the stick-direction logic: `Ctrl_Move_Fwd_Pct/Stf_Pct` (`UCPlayerInput.uc:369-370`) come from the
   stick only, so with WASD SmartJump never dodges/wall-dodges (`UCPlayerController.uc:4262-4310`), and holding Lock
   + A/D never switches target (`SearchForLockTarget`, `:2252-2289`). The reddit thread's guide works around it by
   binding WASD to `aForward/aStrafe`.

Retail Xbox values (reference/iso-extract/System/DefUser.ini, the disc's): `bNonlinearAccel=true`, limits 100/1024,
fall-off 2, `bAccelMultipliers=true` (1.0-2.5 over 0.5 s in X, 1.0-1.5 over 0.75 s in Y, above 95), friction ranges
`(Min=1.0,Max=0.95)`/`(Min=1.0,Max=0.9)` (Epic's PC DefUser.ini: 0.75/0.65), stick dead zone 0.3 (`JoyU/V/X/Y`),
`MouseSensitivity_X/Y` 1.0/0.9. The profile (`PlayerProfile.uc` defaults): SensitivityX/Y 0.8, bInvertY false
("INVERT UP/DOWN LOOK ON THUMBSTICK", `UC2Settings_Controller.uc`), bAutoCenter true, bCameraAdhesion true (adhesion
and friction on).

Profile and invert (profiles.md asked): Epic's profile has ONE invert flag and its menu hint says it is the
thumbstick's; the profile is a binary file (`AVARIS_PROFILE.PRO`), so a second flag would change its format. Mouse
invert therefore has to live outside the profile.

## Plan

- Bindings: mouse -> `aTurn/aLookUp` (dead as inputs today: `UCPlayerInput` overwrites them), the right stick stays
  on `aMouseX/aMouseY` (Epic's route, also what the profile's `ApplyJoysticks` writes). `UCPlayerInput.PlayerInput`
  keeps Epic's stick code unchanged and adds the mouse linearly: degrees per count = 0.022 x sensitivity x
  DesiredFOV/90, invert of its own, fractional rotation carried so slow motion is exact; no clamp, no curve, no
  acceleration, no friction.
- "Who owns the look": the device that produced the last look input (mouse or right stick). Adhesion and auto-centre
  only while the pad owns it; the lazy camera follows rotation rigidly while the mouse owns it (it still lags the
  pawn's movement); lock-on: the mouse moving against the pull is not pulled back, break-away by mouse motion at the
  clamp in degrees.
- Stick movement scale in `UCPlayerInput` (every platform), `JoyX/JoyY SpeedBase=100` again; the pad's ini values
  back to the Xbox's.
- Keyboard WASD counts as a full stick for the stick-direction logic (dodges, lock-target switch).
- Test: SDL2Drv test channel `mouse <dx> <dy>` (an SDL motion event, the real path from SDL on), a per-tick trace,
  camera fields in the probe; `tools/dev/d6-mouse.sh`.
