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

## Step 1 (2026-10-07): the separation, in script and ini (commits 49d4705, b22c067, 0475725, d75990b)

`UCGameplay` (rebuilt package; `tools/dev/script-build.sh --tree d6-make`: make 0 errors, 85 warnings = M14's):
- `UCPlayerInput.PlayerInput`: before Epic's stick code overwrites `aTurn/aLookUp`, keep them (the mouse and the
  look keys: `LinearTurn/LinearLookUp`); after the pad invert, `AddMouseLook`: counts = Linear x dt x 5 (UInput adds
  0.01 per count, ReadInput x20/dt), rotation units = counts x 0.022 x 65536/360 x `MouseLookSensitivity` x
  `DesiredFOV/90` (exact; Epic's `0.01111` is 0.01% short), own invert `bInvertMouseLook`, fractional unit carried
  to the next tick; added to `aTurn/aLookUp` as `(units +- 0.5)/(32 dt)` so UpdateRotation's `int(32 dt aTurn)` is
  exactly that many units (the stick alone keeps Epic's arithmetic: nothing is added when the mouse is still).
- Who owns the look: `bMouseOwnsLook` (stick moved -> pad, mouse or look key moved -> mouse; the mouse wins a tick
  both moved in; starts as the mouse on PC, the pad on the Xbox). Aim adhesion (`GetTurn/LookUpAdhesionAdjust`)
  returns 0 while the mouse owns it (its state keeps updating); auto-centre pitch (`UpdateRotation`) not while the
  mouse owns it. Friction already acts on `aMouseX/aMouseY` only, which is now the stick only.
- Third-person camera (`UCPlayerController.CalcBehindView` -> `MouseLookCamera`): while the mouse owns the look the
  camera is put at `DesiredCameraLocation` (where Epic's lazy camera is heading: behind the pawn for the view, walls
  taken into account) after `CalcThirdPersonView`, and `OldCameraLoc` with it. First attempt (shift `OldCameraLoc`
  by the change of the ideal offset before the native) left a slide wherever a wall shortens the camera arm (seen:
  189 -> 125 units over ~0.2 s after a flick, `logs/d6/explore/e1n.out`); replaced. Consequence: under the mouse
  the camera lags neither rotation nor movement; under the pad Epic's lazy camera is unchanged.
- Lock-on: the lock's pull is held off while the mouse moves against it (as Epic does for the stick); the mouse
  breaks a lock by moving `MouseLockBreakAngle` (25) degrees against the clamp (decays in 0.5 s);
  `LockMousePush` reset while not locked.
- Stick movement scale: `aForward/aStrafe` (0..1) x 24000 in `PlayerInput` on PC (PlayerMove does it on the Xbox
  only), so `JoyX/JoyY SpeedBase=100` works again, also when a profile's swap-sticks rewrites the bindings, and
  `Ctrl_Move_*_Pct` is the stick's real deflection again.
- Keys as stick (`bKeysAsStick`, default on): the movement keys add a full (diagonal: 0.707) stick to
  `Ctrl_Move_*_Pct`: SmartJump dodges and wall-dodges, Lock + A/D target switching, the on-screen stick prompts.
- Tutorial (`UCTrainingController`): the "look up" question answered with the mouse sets the mouse's invert, and
  its "toggle" toggles the invert of the device that owns the look.
- Console `MouseLookSens <x>`, `MouseLookInvert <bool>` (session only: UC2's script `SaveConfig`/`StaticSaveConfig`
  are `#if 0` in `UnCorSc.cpp` ("rjp - calling SaveConfig() has mucho side-effects"), found when the first test
  checked User.ini; the lasting setting is the ini key, or `-ini:` from a launcher).
- `script-diff.py` against the SHIPPED packages (`logs/d6/d6-make-compare-shipped.txt`; GUI.u taken from
  `assets/uc2-shipped`): every package problems 0 except UCGameplay: 18 problems, all in what was changed
  (UCPlayerInput: PlayerInput, Get*AdhesionAdjust, the new functions/properties; UCPlayerController: UpdateRotation,
  CalcBehindView, the new functions; UCTrainingController: PlayerViewSetup.PlayerMove, PlayerViewTest.Fire/AltFire,
  InvertLookToggle), findings 6 = M14's 5 + the new UCPlayerInput defaults. (Against `assets/uc2/System`, which
  carries the D-4 GUI.u, GUI/UCMenu report the D-4 change as "problems"; the shipped baseline is the right one.)

`Engine/Config/DefUser.ini`: `MouseX/MouseY ... Axis aTurn/aLookUp Speed=1.0`; Left/Right `Axis aTurn Speed=-/+16`,
PageUp/PageDown `Axis aLookUp Speed=+/-16` (89.9 deg/s measured at the default sensitivity); sticks
`SpeedBase=100.0 DeadZone=0.3` (the retail disc's); `[UCGameplay.UCPlayerInput]` the Xbox's values
(`bNonlinearAccel=true`, limits 100/1024, `bAccelMultipliers=true`, friction ranges 0.95/0.9 of the retail disc
instead of the PC leak's 0.75/0.65, because friction is the stick's alone now) plus `MouseLookSensitivity=2.5`,
`bInvertMouseLook=False`, `MouseLockBreakAngle=25.0`, `bKeysAsStick=True`; `[UCGameplay.UCPlayerController]`
`bAutoCenterPitch=true`, `LockSearchTimer=0.3` (the Xbox's; the leak had 0.1); wheel `NextWeapon` (step 4).
Ini comments: the engine keeps only lines with `=` when it rewrites a file, so comments have none.

## Step 2 (2026-10-07): the test channel and `tools/dev/d6-mouse.sh`

SDL2Drv test channel (`SDL2InputTest.h`): `mouse <dx> <dy>` pushes an `SDL_MOUSEMOTION` (xrel/yrel) for the first
viewport's window, so `USDL2Viewport::HandleEvent` onwards is the real path; one per tick. `watch <ticks> [tag]`
logs time, view rotation, camera, pawn, lock and look owner every client tick. The probe has a second line
(`probe2`: camera, lock, enemy, look owner, mouse sensitivity/invert, pad invert, auto-centre) and one line per other
pawn (`probe3`: name, location, distance, line of sight, the rotation that faces it). `place <i> <dist> <yaw>` and
`aim <i>` set up lock-on tests (move a frozen bot in front, face it). `m10-inject.py` parses the new lines,
collects watch traces (`watch <tag> <end-regex>`), loads helpers (`pyfile tools/dev/d6-checks.py`).

The runs use `-nullrenderer`: under software GL the game runs at about 5 frames a second here (load from other
agents), where Epic's lazy camera reaches its target in one frame and no slide can be seen; the null renderer runs
at ~1,300 frames a second. Spawn points are random per run; nothing measured depends on them.

First `look` + `ini` runs (`logs/d6/mouse/look.*`, `ini.*`): 37 PASS, 0 FAIL after two test fixes (the spawn probe
came before the pawn: now a seek; the circle's centre is a least-squares fit, the plain mean was biased by
repeated samples) and the SaveConfig finding above. Measured: 10.01 units per count at 100/500 counts (exact
10.0124 x counts within one unit: the carried fraction of the previous motion), 10.0115 at 2000, the same at
500 counts in one tick, 50 per tick or 5 per tick; pitch 10.012 per count; circle radius 3004.7 (expected 3003.7),
deviation 0.29%, closes within 2 units; camera 0.0 units of motion in the 2 s after the circle and after a flick
(it moved 18 units with the flick, in the flick's frame); sensitivity 1.25 halves the turn, FOV 45 halves it;
the mouse's invert and the pad's are independent both ways; under the mouse W walking leaves the pitch alone, under
the pad it moves toward -1400 (2948 -> 2665 in 1.5 s at ~1,300 fps: Epic's per-tick integer step makes this slower
at high frame rates); Right arrow 89.9 deg/s; the mouse right after the stick is exactly linear.

## Step 3 (2026-10-07): pad against the shipped scripts, lock-on, and what the owner had (evidence)

Added `-maxfps=<n>` to SDLLaunch (a standalone game has no frame cap; the null renderer runs at ~1,300 fps, where
Epic's whole-unit view rotation per tick swallows slow stick turns: at 0.35 deflection 167 units/s = 0.13 units a
tick = 0 at that rate; the shipped and our scripts agree, but the numbers say nothing). Pad curves are measured at
30 fps (the Xbox's rate), everything else at 60.

`tools/dev/d6-mouse.sh pad` (`logs/d6/mouse/pad*.{result,json,inject}`): the right stick held at 0.35/0.5/0.75/1.0
for 1.25 s and up at 0.5 for 0.5 s, rotation reached 0.1-1.0 s after the push, ours vs the SHIPPED scripts with the
same DefUser.ini, both at 30 fps: worst differences 0.13%, 0.15%, 0.08%, 0.06% (yaw) and 0.12% (pitch) of the
shipped value (limit 3%). Full stick: 3,146 units at 0.1 s, 9,343 at 0.25, 23,570 at 0.5, 56,325 at 1.0 s (Epic's
hold-acceleration to 2.5x after 0.5 s); 0.5 deflection 2,123/s. Dead zone 0.3 on both sticks (0.28 turns/walks
nothing), the left stick walks (on our scripts; the shipped ones do not walk with SpeedBase 100 on PC: measured
-0.0, which is why M10 used SpeedBase 2400000).
Note for the owner: the pitch curve equals the yaw curve at the same deflection in both trees although
`MouseSensitivity_Y=0.9`; not investigated (identical in the shipped scripts, so Epic's behaviour).

`lock` (two bots frozen with PlayersOnly, put 400 units in front by the test channel): 12 PASS: acquired with Lock
while the mouse owns the look; held through +-40-count jitter; after 300 counts against it the lock pulls the view
back (2,569 -> 1,761 units off in 1 s; Epic's pull stops within MinLockAdjust, it is a camera lock, not an aim
lock); Lock + D switches to the target on the right (keys as stick); 450 counts (25 deg) inside the clamp keep the
lock, 2,000 counts (110 deg, past the 38.5-deg clamp by more than 25) break it; re-acquired; the stick's full push
for 1.5 s still breaks it (Epic's path); aim adhesion: strafing past a target turns the view 317 units under the
pad, 0 under the mouse (first try had two bots near the crosshair: the adhesion target alternated and Epic resets
its ramp on every change, so the pad also showed 0; the second bot now goes behind the player).

`look` final: 34 PASS (adds the camera there-and-back check: the circle driven one way and back, the camera at the
same rotation is at the same place: median 0.01, max 0.08 units).

Evidence of what the owner had (`before`: shipped scripts + main's M10 DefUser.ini at 47b1b18, as `bin/play-uc2`
ran on 2026-10-06; `epic`: shipped scripts + Epic's DefUser.ini, which the WEB build carried until c66d275 reached
main on 2026-10-07 07:52 with the M11 merge, so the browser the owner played on 2026-10-06 had it):
- native/M10: 52.4 units per count = 0.288 deg/count (5.2x our default; 1,250 counts per 360 deg = 4 cm at
  800 dpi), 52.0 at one count a tick (Epic's per-tick truncation), camera there-and-back median 26.5 / max 37.7
  units, 19 units of camera slide after the circle stopped and 62 after a flick (`before-*`).
- web/Epic: 20 counts at one a tick turn 4,478 units (24.6 deg: 1.23 deg/count), 20 counts in one tick 441,
  200 counts in one tick 440 (capped: ten times the motion, the same turn), the circle comes out 12,286 x 10,954
  units with 16% radius deviation (the per-axis clamp + squared fall-off: the owner's "hexagonal"), camera slide
  after a flick 6.6 units.
So every symptom the owner listed is measured: "capped" and "hexagonal" in the browser's Epic mouse, "extremely
sensitive" in both (1.23 and 0.29 deg/count), "the camera slides toward the movement" in both (the lazy camera).

Correction to step 3's note: the pitch curve equals the yaw curve because the profile applies its controller
sliders even when there is none: the transient "no profile" profile sets `MouseSensitivity_X/Y` to 0.8/0.8
(`getall UCPlayerInput MouseSensitivity_Y` = 0.8 in a fresh tree, `logs/d6/explore/sens.out`); the ini's 1.0/0.9
never act in a game. The full-stick 26,214 units/s base rate (M10 measured 26,006) is 1024 x 0.8 x 32.

## Step 4 (2026-10-07): the keyboard binding audit ("keyboard bindings being backwards", no example given)

Every binding of `[Engine.Input]` in `Engine/Config/DefUser.ini`, each command looked up as an alias, an `exec`
function in any script package, or an engine command (script in the notebook's history: a Python pass over the
section and `Source/*/Classes/*.uc`). Directions, checked against measurements:
- W/S forward/back, A/D strafe left/right (M10 keyboard scenario; `PlayerMove`: `aStrafe > 0` = right), Up/Down
  as W/S; mouse right = turn right, mouse up = look up (d6 look: `look-mouse-up-looks-up`); Right arrow = turn right
  (89.9 deg/s), PageUp = look up; right stick right = turn right, up = look up (M10 pad scenario, d6 pad).
- Invert defaults: mouse not inverted (`bInvertMouseLook=False`), pad not inverted (profile `bInvertY=false`).
Nothing is inverted or swapped in direction. What was not right, and what changed:
- Mouse wheel `NextSkill`/`PrevSkill`: no such command anywhere (dead in a match; the reddit guide says so too).
  Now `NextWeapon` both ways (UC2 has no previous-weapon: `PrevWeapon` is an empty function).
- The movement keys did not count as a stick for the game's stick-direction logic, so on a keyboard SmartJump
  never dodged or wall-dodged, Lock + direction never switched target, and the on-screen "push the stick"
  prompts could not be answered: `bKeysAsStick` (step 1). This is what the community's User.ini fix in the reddit
  thread works around ("WASD remapped ... to true analog axes ... unlocking infinite wall-dodges"). Side effect, as
  on the pad: Space while moving dodges instead of jumping (UC2's SmartJump design); Backspace stays a plain Jump.
- Left/Right/PageUp/PageDown: on the mouse's linear look now (they were on the stick's curve).
Left as they are, for the owner to judge:
- C = Black (1P/3P view, next scoreboard page) sits LEFT of V = White (voice, previous scoreboard page), while on
  the pad Black is the right shoulder ("next") and White the left: in the scoreboard, C goes forward and V back.
  C is the camera key by mnemonic, so not swapped.
- Arrows: Left/Right turn (M10; UT2004's default too), where Epic's PC file strafed (`,` `.` and A/D strafe).
- Dead commands still bound (harmless, none of them exists in UC2's scripts): Shift `Walking`, G `Grab`, J
  `Taunt Taunt1`, R `ForceReload`, F2 `ShowServerInfo`, F3 `ShowObjectives`, `=` `GrowHUD`, `-` `ShrinkHUD`,
  `\` `ChangeHud`, `[` `]` `InventoryPrevious/Next`, and the `Shield 1/0` part of RightMouse/Joy7 (Epic's Xbox
  binding has it too; the shield works through AltFire).
The owner's example is still needed (PLAN.md O-14) to know whether "backwards" meant one of these.

## Step 5 (2026-10-07): a kept User.ini, and the M10 suite on the rebuilt packages

A User.ini is derived from DefUser.ini once and then kept: in the browser the frontend stores it with the player's
files (C-2), so the owner's browser may still hold Epic's or M10's bindings, which would go on sending the mouse into
the stick's variables. `Source/Engine/Src/UC2InputMigrate.cpp` (called first thing in UGameEngine::Init's client
part): a key whose value is EXACTLY one an earlier build wrote is set to D-6's (MouseX/MouseY of Epic's PC file and
M10's, M10's look keys and sticks, the dead wheel commands, Epic's PC friction ranges; the M10 linear pad section,
recognised by its 1,000,000/10,240,000 limits, as a whole), one `Input: User.ini [...] ... is a value from before
D-6; now ...` Init line each; anything else is left alone. Found by its test: the UCPlayerInput class is already
loaded when the game engine starts (the static-link build), so its config had been read; the migration now re-reads
the config of a class that is loaded (`LoadConfig` on the class default). `tools/dev/d6-mouse.sh migrate`: with M10's
User.ini 18 lines, with Epic's 6; both: the mouse is D-6's (500 counts = 5,006 units), the pad's limits 100 and
curve on (8 PASS).

`tools/dev/m10-controls.sh` on the rebuilt tree d6-run (GL, `logs/d6/m10/all-scenarios.txt`), with the scenarios
updated for D-6 (mouse 10.012 units/count within 2% and `MouseLookInvert`; pad dead zone 0.3 tested at 0.35; the
"both" pad turn held 0.4 s, since with the Xbox's acceleration a 1 s full-stick turn passes 180 degrees and wraps):
keyboard 16, mouse 18, text 7, pad 33, both 14 checks, all PASS, 0 Critical (binary before the migration; the
migration acts only on a kept User.ini, and these runs start from DefUser.ini).

## The switch: what D-6 needs in the asset tree and the web (proposal for the coordinator)

What needs a rebuilt package: the mouse separation, the pad-only adhesion/auto-centre, the camera under the mouse,
lock-on with a mouse, the stick scale on PC, keys as stick, the tutorial's invert: all in `UCGameplay.u`. Engine.u
and every other package are unchanged (script-diff: only UCGameplay "different"; GUI.u = D-4's, already in the
tree). The engine-side parts (User.ini migration, `-maxfps`, the test channel) arrive with the next build.

The ini and the package are COUPLED: the D-6 `DefUser.ini` binds the mouse to `aTurn/aLookUp`, which the SHIPPED
`UCPlayerInput` overwrites from the stick, so with the shipped `UCGameplay.u` the mouse would not turn the view at
all (and the engine's migration moves kept User.inis to those bindings too). So merging D-6 and switching
`UCGameplay.u` must happen together, natively and on the web.

Proposal (the D-4 way, one more package):
1. `tools/dev/mkassets-uc2.sh` with `UC2_SCRIPTS=<WorkUC2>/run/d6-make/System` (or a script build of merged main)
   and `UC2_SCRIPT_PACKAGES="GUI UCGameplay"`: the shipped `System/UCGameplay.u` moves to
   `assets/uc2-shipped/System/`, the rebuilt one comes in; `System/SCRIPTS.txt` lists both.
2. Frontend: regenerate `targets/uc2/file-manifest.json` and `dependency-manifest.json` for that tree (one entry
   changes: `system/ucgameplay.u`, 1,579,260 -> 1,584,207 bytes in this build; the dependency check passes: 208
   packages, all resolved, `logs/d6/web/`); `Core.u` is untouched, so the target's fingerprint holds.
3. UCGameplay IS a server package (`Default.ini` `ServerPackages=UCGameplay`): a client and the server it joins
   must have the same `UCGameplay.u`. The rebuilt file keeps the shipped GUID and adds a generation (conformed); a
   peer with the shipped file and one with the rebuilt file were not tested together and are not a goal (M14).
   In practice: native dedicated servers and every client (native, web) switch at once; an old native run tree
   (or an old web cache, if the frontend's manifest does not change the file's identity) must not be mixed with new
   ones. `tools/dev/m14-scripts-check.sh` net (dedicated2 + travel) on the rebuilt tree is the check (step 6).
4. The load-time patcher skips the rebuilt `UCGameplay.u` (its Epic patches are in the source, M14), and still
   patches the shipped packages.
Without the switch (shipped `UCGameplay.u`, D-6 engine and inis) the game would have NO mouse look: do not merge the
ini without the package.

## Step 6 (2026-10-07): the full d6 run on the final native binary

Engine additions since step 5: a start-up warning when the mouse is bound to `aTurn/aLookUp` but the loaded
`UCGameplay.u` has no D-6 mouse look (seen with the shipped package: "Input: the mouse is bound to aTurn/aLookUp (D-6)
but this UCGameplay.u has no D-6 mouse look ..."), and `-noinimigrate` (the evidence runs need the old User.ini as it
was: the first final run's `before` failed because the engine had migrated M10's bindings).

`tools/dev/d6-mouse.sh look ini migrate pad lock before epic` (`logs/d6/d6-mouse-final.txt`): **67 PASS, 0 FAIL**
(look 34, ini 4, migrate 8, pad 4 + 5 curve comparisons, lock 12); the evidence runs fail exactly where the owner's
symptoms are (before: speed-dependence 52.0 vs 52.4 per count, camera there-and-back median 23.6 / max 50.7 units,
23.8 units of slide after the circle, 63.9 after a flick; epic: 20 counts at one a tick 4,442 units, in one tick
440, 200 in one tick 440, circle 12,293 x 11,212 units, 16.6% out of round). Pad vs shipped worst 0.47% (0.35
deflection, where whole units per tick dominate), 0.07-0.09% elsewhere; adhesion under the pad 881 units, under the
mouse 0; Right arrow 87.9 deg/s; auto-centre under the pad 3,016 -> 1,661 in 1.5 s, under the mouse unchanged.

## Step 7 (2026-10-07): builds, D-4/D-5 regressions, the browser

Builds at the final engine code (82f25cc; nothing in `Source/` changed after it):
- native: `bin/capped 6G 3600 -- cmake --build build-native -j2` rc 0 (`logs/d6/build-native-8.log`; the full build
  from scratch `build-native-0.log`, -j6).
- wasm-perf: `source env.sh && bin/capped 4G 1200 -- cmake --preset wasm-perf`, `bin/capped 6G 5400 -- cmake --build
  build-wasm-perf -j6` rc 0, 0 `error:` (`build-wasm-perf-1.log` from scratch, `-2.log` incremental after the last
  engine edits: relinked); SDLLaunch.wasm sha256 3bcfb2ab8a05ca1c...; carries `NOINIMIGRATE`.
- windows: `bin/capped 4G 1200 -- cmake --preset windows`, `bin/capped 6G 5400 -- cmake --build build-windows -j6`
  rc 0, 0 `error:` (`build-windows-1.log`; `-2.log` incremental: up to date); `uc2.exe` carries `NOINIMIGRATE`.

On the rebuilt tree d6-run (all 17 packages from this branch):
- `D4_TREE=d6-run tools/dev/d4-names.sh`: **d4-names: ALL PASS** (n1 11, n2 7, n3 3, n4 4 scenario checks + 8 file
  checks; `logs/d6/d4-names.txt`).
- `D5_TREE=d6-run tools/dev/d5-text.sh`: **d5-text: ALL PASS** (k1 13, k2 14, k3 4, k4 14, k5 10; `logs/d6/d5-text.txt`).

Browser (frontend clone, `uc2` branch): serve-web assembled immediately before with this worktree's wasm-perf and the
asset tree `run/d6-webassets` (the asset tree hard-linked, `System/UCGameplay.u` = `run/d6-make/System/UCGameplay.u`;
manifests regenerated into `logs/d6/web/`: 208 packages, all resolved) by `corepack yarn build --target uc2 --assets
... --manifest ... --deps ... --engine-artifact <worktree>/build-wasm-perf/Source/SDLLaunch` (`logs/d6/fe-build-1.txt`);
`sha256sum serve-web/SDLLaunch.wasm` = this build's before and after every run (`logs/d6/e2e-engine-proof.txt`);
sound to the null sink `uc2d6` (created and removed per run); `PULSE_SINK=uc2d6 bin/capped 6G 1500 -- node
packages/e2e/tools/e2e-mem-guard.mjs --max-gb=14 -- --target uc2 uc2-wasm-boot.spec.js` with `env.sh` sourced (the
guard spawns `yarn`, the workspace's shim in `bin/`; my first try without it failed to start, nothing ran).
- run 2, the whole boot spec (`logs/d6/e2e-boot-2.txt`): the three existing tests passed (menu, DM-Deadbolt drawn,
  keyboard and mouse: "moving the mouse right turns the player right: yaw -40880 -> -37876 (3004) after 300 px" =
  10.01 per px); the new test failed only its "page has pointer lock" step.
- headless Chromium does not grant pointer lock here: the shell's raw request is refused with `WrongDocumentError:
  The root document of this element is not valid for pointer lock`, and the plain lock it asks for at the next click
  was not engaged either; the mouse's relative motion reaches SDL as movementX/Y anyway. The test now clicks twice,
  reports the lock and does not require it.
- run 3, `-g "mouse look"` (`logs/d6/e2e-boot-3.txt`): **1 passed (1.2m)**: rotation units per pixel 100 px slow 10,
  400 px slow 10.0125, 400 px in two steps 10.01, 100 px up 10.01; the view unchanged 1.5 s after the mouse stopped;
  the engine's `ScriptPatcher: UCGameplay is not the package the patch was made for ... rebuilt from source` line
  proves the rebuilt package was loaded. Mem-guard peak 12.0 GB machine-wide (other agents included).
- Frontend gates: `yarn lint` rc 0, `yarn test` 28/28; committed in ufrontend 51cab98 (local), with the uc2 target
  README's "Mouse look" section. serve-web is left assembled with this branch's engine and `run/d6-webassets`.

## What the owner will feel, and what is labelled how

With the mouse (rebuilt UCGameplay.u + this branch's DefUser.ini/engine):
- one count of mouse motion turns 0.055 deg (0.022 x 2.5; was 0.29 natively, 1.23 in the browser), the same at any
  speed (no cap: 200 counts in one frame turn 10x what 20 do), the same up/down as sideways, a circle stays a
  circle; turning stops the moment the mouse stops, and the third-person camera no longer slides behind the view
  after a turn (it sits where the lazy camera was heading at once). No aim assist pulls the mouse; walking no
  longer re-centres the pitch under the mouse (the profile's CENTERVIEW now only acts for the pad).
- lock-on: Lock (middle mouse or L) holds the target; mouse motion away from it is not fought while the mouse moves;
  moving the mouse about 25 deg past the lock's 38.5-deg clamp breaks it; Lock + A/D switches target.
- keyboard: Space while moving dodges (and wall-dodges in the air) as A does on the pad (Backspace = plain jump);
  the wheel cycles weapons; arrows/PageUp/PageDown turn/look at about 88 deg/s.
- settings: `User.ini [UCGameplay.UCPlayerInput] MouseLookSensitivity` / `bInvertMouseLook`, or the console
  `MouseLookSens 1.5` / `MouseLookInvert True` for the session. The menu's Controller page is the pad's.
With the pad: the Xbox's response (squared curve, hold-acceleration to 2.5x after 0.5 s, dead zone 0.3, friction,
adhesion, auto-centre, lock break by a full push), as the shipped scripts do (within 0.5%); M10's linear pad is gone.
The left stick walks with Epic's own binding (also after a profile swaps the sticks).

Labels: measured by the script (d6-mouse, natively, null renderer at 60/30 fps): everything in steps 3 and 6.
Measured in the browser: linear per pixel at two speeds and two distances, both axes, no drift (pointer lock NOT
engaged in headless Chromium: the motion arrived without it). Felt-only (cannot be judged here): whether 0.055
deg/count suits the owner's mouse and desktop (on Linux the browser applies the desktop's pointer acceleration under
pointer lock: rawPointerLock.ts), how the rigid camera feels on stairs and dodges (no movement smoothing under the
mouse now), and the pad on a real controller. Not done: an in-game slider for the mouse (D-9's menu), keeping the
camera's movement smoothing for the mouse (possible as a lag on the pivot only), saving the console's mouse
settings (UC2's script cannot write config; an engine command could), the pad's frame-rate-dependent slow turns
(Epic's whole rotation units per tick lose up to one unit a frame: at 144 fps a 0.35 deflection turns ~40% slower
than at 30; carrying the fraction would fix it but changes Epic's pad, so it is an owner decision).
