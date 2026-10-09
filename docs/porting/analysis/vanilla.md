# F-1 notebook: Epic's shipped scripts play as shipped; the rebuilt packages add the PC mouse on top

Worktree `/home/ion/WorkUC2/engine-uc2-f1-vanilla`, branch `f1-vanilla` (from main 1372dec). Run trees
`<WorkUC2>/run/f1*`, logs `<WorkUC2>/logs/f1/`. Every run goes through `<WorkUC2>/bin/capped` (CLAUDE.md). Appended
after every step. Brief: PLAN.md "Owner review of 2026-10-09", task F-1, plus the coordinator's detail (steps 1-7).

Trees: **vanilla** = `UC2_SCRIPTS=<WorkUC2>/assets/uc2-shipped/System tools/dev/mkrun-uc2.sh build-native f1-van`
(Epic's shipped GUI.u, UCGameplay.u, UCMenu.u over the asset tree; our engine and curated inis); **rebuilt** =
`tools/dev/mkrun-uc2.sh build-native f1-reb` (the asset tree as it is: the three rebuilt packages), later with this
branch's own script build.

## Step 0 (2026-10-09): reading, and the design question of step 2

What the curated ini does today (main 1372dec, `Engine/Config/DefUser.ini`):
- `MouseX=Count bXAxis | Axis aTurn Speed=1.0` (`:101-102`), look keys `Left/Right=Axis aTurn Speed=-/+16`,
  `PageUp/PageDown=Axis aLookUp Speed=+/-16` (`:62-66`, `:107`). The shipped `UCPlayerInput.PlayerInput`
  (`Source/UCGameplay/Classes/UCPlayerInput.uc`, Epic's part) computes `aTurn/aLookUp` from `aMouseX/aMouseY` every
  tick and overwrites them, so with the shipped package the mouse AND the look keys are dead.
- Epic's PC leak (`epic-sources/System/DefUser.ini`) and the retail disc (`reference/iso-extract/System/DefUser.ini`)
  both bind `MouseX=Count bXAxis | Axis aMouseX Speed=6.0` (Y the same), `Left/Right=StrafeLeft/StrafeRight`,
  `PageDown=LookDown` (an alias no script defines). The retail disc binds the right stick
  `JoyU=Axis aMouseX SpeedBase=100.0 DeadZone=0.3 | Count bXAxis`: **the stick counts `bXAxis` too**, and a profile's
  swap-sticks (`Engine/Classes/ControllerConfiguration.uc:84-111`) writes `| Count bXAxis` on whichever stick looks.

How the rebuilt script can tell the mouse from the stick when both arrive in `aMouseX/aMouseY` (Epic's binding):
- `bXAxis/bYAxis` (`PlayerController.uc:99`, `var input byte`) are incremented by `Count` once per binding execution
  (`UnIn.cpp` COUNT): once per SDL motion event for the mouse, once per tick for a stick bound with `Count`
  (`UInput::DirectAxis` runs every bound pad's four axes every player tick, `SDL2Pads.cpp:425-433`). Nothing in UC2's
  shipped scripts reads them (only `Engine.PlayerInput.SmoothMouse`, which `UCPlayerInput` never calls: no
  `Super.PlayerInput`), and nothing resets them.
- In a tick where `aMouseX == 0 && aMouseY == 0` nothing turned the view, so the counts of that tick are exactly what
  the sticks add each tick (0 without a pad, 1 per axis with the retail binding). A later tick whose count exceeds that
  baseline had a mouse event. The baseline only changes when a pad is plugged/unplugged or a profile rewrites the
  stick bindings, and "still" ticks are the common case, so it stays current. Plan for the rebuilt script.
- A mouse count via Epic's binding is `0.01 x 6.0` before `ReadInput`'s `20/dt`: counts this tick =
  `aMouseX x dt / 1.2`, i.e. D-6's `aTurn` formula with `aMouseX / 6`. A different `Speed=` in a player's ini scales the
  mouse proportionally (documented, not parsed).
- A tick where both devices moved: the stick's part (at most 100 = 100 x dt / 1.2 "counts", 1.4 at 60 fps) is read as
  mouse for that tick. Negligible; D-6 already let the mouse win such a tick.

Other things to measure in step 1 (expected with the shipped scripts, from REPORT.md 30.x):
- the pad's LEFT stick does not walk on PC (`UCPlayerController.uc:3920-3928`, `:4098-4106`: the x24000 only when
  `Level.IsXBox()`); D-6 measured -0.0 units with `SpeedBase=100`.
- no keyboard dodge (`bKeysAsStick` is in the rebuilt script only), no KEYBOARD / MOUSE page (UCMenu.u), D-4's name
  rule (GUI.u).

Decisions taken while step 1 runs (code in the worktree, uncommitted until measured):
- **The left stick on PC** (found reading; measured in step 1): with the shipped scripts the stick does not walk at
  all, so "drop the iso and truck along" with a pad would not move. The engine adapts (owner's standing rule):
  `Source/Engine/Src/UnIn.cpp` AXIS branch, non-Xbox, a stick (`IST_Hold`, `SpeedBase`) bound to `aForward/aStrafe`
  also adds 240 x its value to `aBaseY/aBaseX`, the keyboard's channel, which `UCPlayerInput` adds to the walk AFTER
  taking the stick's direction (`Ctrl_Move_*_Pct`) from `aForward/aStrafe`. Walk = (24000 + 1) x d (the Xbox:
  24000 x d), direction = d exactly as on the Xbox; works for the ini's bindings and for the profile's swap-sticks
  bindings (`ApplyJoysticks` writes `aStrafe/aForward SpeedBase=100`). Rejected: M10's `SpeedBase=2400000` (the
  stick's direction saturates at any push, so SmartJump dodges at 0.31 instead of past `fSmartJumpDodgePct`; undone
  by a swap), an ini route `| Axis aBaseY SpeedBase=24000` (undone by a swap). This forces step 4's "stick scale on
  PC" in the rebuilt script: its own x24000 would now double the walk, so it goes, and `KeysAsStick` leaves the
  stick's share of `aBaseX/aBaseY` out.
- **The look keys** (Left/Right/PageUp/PageDown) cannot stay on `aTurn/aLookUp` (dead with the shipped script).
  They go to `aMouseX/aMouseY Speed=+-4.0` = the look stick at 0.8 (Epic's curve: (0.8)^2 x 1024 x 0.8 profile
  sensitivity x 32 = 16,777 units/s = 92 deg/s, below the hold acceleration's 95 threshold), the same with both
  packages. D-6 had them linear at 88 deg/s under the mouse's sensitivity; now they follow the pad's sensitivity and
  hand the look to the pad in the rebuilt script (lazy camera, adhesion while a look key is held).
- **D-9 conflicts clear** (`UC2Settings_Keys.AssignKey`): the other action loses the key, the cell's old key is
  unbound; `d9-rebind.sh` expectations follow (MOVE BACK LOST S, W unbound and walks nowhere).
- **SDL relative-mode hints** (step 6): engine-ut2004's SDL2Drv sets NONE (`grep SDL_SetHint` finds only
  `SDL_HINT_ORIENTATIONS`); it relies on SDL 2's defaults, which are unaccelerated (raw input / XInput2 raw / Wayland
  unaccelerated). `SDL2Client::Init` now names the three that matter at normal priority (`MODE_WARP=0`,
  `SYSTEM_SCALE=0`, `SPEED_SCALE=1.0`; `RELATIVE_SCALING` only acts on an `SDL_Renderer`, not used, so not set).
