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

## Step 1 (2026-10-09 02:10-02:30): the vanilla state measured (main 1372dec's engine and inis, Epic's packages)

Tree `run/f1-van0`: `build-native` at 1372dec, `UC2_SCRIPTS=assets/uc2-shipped/System` (GUI.u 2eb7e899, UCGameplay.u
c675a442, UCMenu.u 4538a4ba = Epic's), main's `Engine/Config` (run from an export of 1372dec's `tools/` and
`Engine/`, `logs/f1/base`, so that the worktree could change meanwhile). Driver `logs/f1/suite.sh s1-van0 f1-van0`,
results `logs/f1/s1-van0/`. Load average 7 at the start, 21-34 during d6-d5 (other agents): timing-sensitive menu
checks there are not judged.

| Suite | Result | Failures, and whether expected (needs a rebuilt package) or a defect (vanilla should work) |
|---|---|---|
| m10 keyboard | 15/16 | `kb-right-arrow-turns-right`: **defect** (arrows on `aTurn`, overwritten by Epic's script) |
| m10 mouse | 13/18 | `ms-mouse-right-turns-right`, `-left-turns-back`, `-up-looks-up`: **defect** (the mouse does not move the view at all); `ms-mouse-turn-amount` (10.012/count) and `ms-invert-option-flips-pitch` (`MouseLookInvert`): expected (PC mouse = rebuilt) |
| m10 text | 7/7 | |
| m10 pad | 29/33 | `pd-left-stick-moves-forward`, `-at-0.6-moves-backward`, `-just-outside-dead-zone-moves`, `-x-strafes-the-right-way`: **defect** (the left stick walks nowhere on PC with Epic's script) |
| m10 both | 11/14 | `both-keyboard-move-and-pad-turn-together`, `both-pad-stick-moves-during-mouse-and-keys`, `both-mouse-turns-during-pad-move`: **defect** (the same two causes) |
| d6 vanilla (new) | 1/6 | mouse turns, up looks up, Epic's capped path, Right arrow, PageUp: all **defect** (nothing moves) |
| d6 look | 5/18 | every mouse measurement 0: **defect** for "moves at all", expected for the linear numbers |
| d6 pad | 3/4 + 5/5 curves | `pad-left-stick-walks`: **defect**; the right stick's curves (vs itself here) fine |
| d6 lock | 6/12 | mouse-driven lock checks: expected (PC mouse); `lock-switches-target-with-lock-and-d`: expected (keys as stick is rebuilt-only) |
| d9 menu | 2/6 | no KEYBOARD / MOUSE button: expected (UCMenu.u) |
| d3 profiles | all but 1 | `r1-character-changed-from-the-default` at load 18-34: re-run in step 7 (not a package feature) |
| d4 names | n3 PASS, n1/n2/n4 FAIL | n1/n2/n4 name checks: expected (D-4 is GUI.u); `n1-deadbolt-up`/`match-started`: cascade at load 34, re-run in step 7 |
| d5 text | ALL PASS | |

So, with Epic's packages, our engine and main's curated ini: keyboard walking, menus, text, the right stick and
profiles work; **the mouse, the look keys and the pad's left stick do not** (three defects, all ours: two ini
bindings Epic's script overwrites, and the left stick's PC scale that Epic's script applies only on the Xbox).

## Steps 2 and 3 (2026-10-09 02:10-02:50): Epic's mouse path in the ini, the PC mouse on top, the lazy camera key

Changes (worktree; file:line at the commit):
- `Engine/Config/DefUser.ini` (and the generated `web/DefUser.ini`): `MouseX/MouseY=Count bXAxis | Axis aMouseX
  Speed=6.0` (Epic's PC and retail binding, byte for byte); `Left/Right=Axis aMouseX Speed=-/+4.0`,
  `PageUp/PageDown=Axis aMouseY Speed=+/-4.0`; new `[UCGameplay.UCPlayerInput] bLazyMouseCamera=False`; comments say
  which package reads what. The pad section is the retail disc's, unchanged (it already was).
- `Source/Engine/Src/UC2InputMigrate.cpp` deleted, its call and declaration in `UnGame.cpp` removed (that part of
  `UGameEngine::Init` is Epic's again); `-noinimigrate` gone with it. No start-up warning from the engine either: the
  rebuilt script logs one line per level, `UCPlayerInput: PC mouse look (rebuilt UCGameplay.u),
  MouseLookSensitivity=2.5, rigid|lazy camera under the mouse`; Epic's package logs nothing (and the existing
  `ScriptPatcher: UCGameplay patched` / `... not the package the patch was made for` line already says which one).
- `Source/Engine/Src/UnIn.cpp` AXIS branch: the stick's walk on PC (see "Decisions"), for both packages.
- Rebuilt `UCGameplay.u`: `UCPlayerInput.PlayerInput` takes the mouse's share out of `aMouseX/aMouseY` by the Count
  bytes (step 0's design) and feeds it to D-6's unchanged `AddMouseLook` as `aMouseX / 6`; the x24000 is gone,
  `KeysAsStick` leaves the stick's walk out; `bLazyMouseCamera` (globalconfig, default false):
  `UCPlayerController.MouseLookCamera` returns at once when it is set, so Epic's lazy camera runs under the mouse
  and `MouseCameraLeftBehind` (the jitter fix) never fires (it needs the rigid camera's time stamp), which is what
  the lazy camera always had.
- Script build `tools/dev/script-build.sh --tree f1-make` (3G): `Success - 0 error(s), 85 warning(s)`; compare:
  UCGameplay 19 problems (the changed functions, as at the jitter gate), UCMenu 5, GUI different-explained (D-4),
  every other package as shipped (`logs/f1/f1-make-compare.txt`).
- Tests: `tools/dev/d6-mouse.sh`: `migrate` (and `-noinimigrate`) removed; new `vanilla` (Epic's package with our
  ini: the mouse turns through Epic's PC path, the look keys turn), `lazy`, `accel` (step 6); the pad scenario walks
  from a fixed spot (jit-trace's) and checks the walk on BOTH trees, and the walk is compared like the curves.

Measured (`logs/f1/s2-d6/`, trees `f1-reb` = this branch's packages, `f1-van` = Epic's, same binary and inis; load
22-28): **76 PASS, 1 FAIL**.
- vanilla 6/6: 20 counts at one a tick turn 4,466 units, 20 in one tick 440, 200 in one tick 440 (Epic's PC mouse:
  the stick's clamp; D-6's `epic` evidence had 4,442/440/440), pitch 4,432; Right arrow 91.1 deg/s, PageUp looks up.
- look 36/36 on the rebuilt package: 10.012 units per count at every speed and distance, circle, camera, settings,
  invert, auto-centre under the pad only, the mouse right after the stick still linear (a pad attached: its Count
  baseline 1 per tick is told apart). ini 4/4.
- lazy 5/5: still 10.012 per count, the camera moves on 17.2 units in the 2 s after a 400-count flick and then rests,
  the start-up line says "lazy camera under the mouse".
- pad 4/4 + curves vs Epic's package within 0.17% (worst: pitch 0.5, 0.17%; yaw 0.35/0.5/0.75/1.0: 0.13/0.10/0.08/
  0.09%); **the left stick walks on Epic's package now** (fixed spot: -163.9 / +399.2 / -353.4 units with Epic's,
  -163.9 / +408.4 / -353.8 with ours; 2.3% on one segment, frame quantisation of the 1.0 s hold at 30 fps).
- lock 11/12: `lock-pulls-back-after-the-mouse-stops` (offset 1,773 -> 1,757, wanted -50): the check the jitter task
  already found spawn-sensitive (jitter.md step 4); re-run in step 7. Adhesion 0 under the mouse, 215 under the pad.

## Step 4 (2026-10-09, written while step 1 ran): review of bKeysAsStick, the stick on PC, the binding audit

The owner (2026-10-09): "Keys weren't backwards, just really hard to use on the PC and maybe we needed better
controls. Review all the work you've done here to make sure you haven't overthought things and that they're positive
changes." Assessment per change (nothing changed for this step except where step 2 forced it, marked FORCED):

| Change (where) | What it does | Verdict | Why |
|---|---|---|---|
| `bKeysAsStick` (rebuilt `UCPlayerInput.KeysAsStick`, ini key, default True) | WASD count as a full left stick in `Ctrl_Move_*_Pct`: Space while moving dodges / wall-dodges, Lock + A/D switches target, the "push the stick" prompts can be answered | **keep** | It is the mechanism of the owner's own decision "Space + direction dodges, as on the Xbox: keep". Without it a keyboard player can never dodge (`SmartJump` reads only `Ctrl_Move_*_Pct` against `fSmartJumpDodgePct=0.5`). The ini key is the off switch the standing rules ask for; 20 lines. Possible simplification (owner's call): drop the key and keep the behaviour always on. |
| The stick's walk on PC (rebuilt script x24000) | the shipped scripts multiply the stick by 24000 only on the Xbox, so on PC the left stick walked nowhere | **FORCED: moved to the engine** (`UnIn.cpp`), removed from the script | Step 2 needs Epic's shipped package to walk with a pad; once the engine does it the script's own scale would double the walk. Simpler: one place, both packages. `KeysAsStick` now leaves the stick's share of `aBaseX/aBaseY` out (5 lines). |
| Wheel `NextSkill/PrevSkill` -> `NextWeapon` both ways (`DefUser.ini`) | Epic's commands exist in no script package (dead wheel); UC2's `PrevWeapon` is empty | **keep** | A dead control made live; ini only. |
| Look keys (Left/Right/PageUp/PageDown) | M10 made the arrows turn (UT2004's default; Epic's PC file strafed with them); D-6 put them on the mouse's linear `aTurn/aLookUp` | **FORCED: simplified** to the look stick at 0.8 (`Axis aMouseX/aMouseY Speed=+-4.0`) | `aTurn/aLookUp` are dead with the shipped script. Same feel with both packages now (about 90 deg/s, Epic's curve, no hold acceleration). Open for the owner: arrows turn (kept) or strafe as in Epic's PC file. |
| "Backwards" audit (`mouse.md` step 4) | every binding looked up; nothing inverted | **keep as a record, nothing to do** | Matches the owner's clarification. The C/V order and the dead commands Epic left bound (Shift `Walking`, G `Grab`, ...) stay as they are: harmless, and rebinding covers taste. |
| `MouseLockBreakAngle` (ini key, 25 deg) | how far the mouse must push past a lock's clamp to break it | **simplify candidate** (owner's call) | The behaviour is needed (Epic's break-away is a stick push held 0.25 s, which a mouse cannot do); the ini key is probably more than anyone needs. Left as it is. |
| Pad-only aim adhesion and auto-centre, lock pull held off while the mouse pushes against it, tutorial invert per device (rebuilt) | the pad's aim assists do not act on a mouse | **keep** | Part of "the PC mouse"; each one fixed a measured symptom (adhesion 881 units under the pad, 0 under the mouse). Rebuilt package only. |
| D-6's `aTurn/aLookUp` still read as linear input (rebuilt) | a User.ini from D-6 (mouse on `aTurn`) still gets the PC mouse | **keep (zero code)** | It is the same two lines that read the mouse; dropping it would not simplify anything. The curated ini no longer binds them. |

## Step 5 (2026-10-09): D-9 conflicts clear; exploration of UT2004's double-tap dodge (report only, no code)

Code: `Source/UCMenu/Classes/UC2Settings_Keys.uc` `AssignKey`: the action that had the new key loses it (its cell
cleared, "MOVE BACK LOST S."), the cell's old key is unbound; `sSwapped` removed. `tools/dev/d9-rebind.sh` follows
(`conflict-clears`, `live-conflict-cleared`, W walks nowhere after the conflict, `K: JUMP / DODGE. 1ST / 3RD PERSON
LOST K.`, Space unbound after it). Space + direction dodging stays (bKeysAsStick, step 4).

**What UT2004-style double-tap dodging would take in UC2** (read, not built):
- The machinery is all there and runs every tick: `UCPlayerController.PlayerWalking.PlayerMove`
  (`UCPlayerController.uc:3942`) calls `ModifyDoubleClickMove( PlayerInput.CheckForDoubleClickMove(DeltaTime) )`;
  UC2 overrides `CheckForDoubleClickMove` in `UCPlayerInput.uc:951-1010` (UT2004's code: edges, `DoubleClickTimer`,
  `DoubleClickTime` = 0.25 s from `Engine.PlayerInput` defaults), and `ProcessMove` -> `UCPawn.Dodge` handles any
  direction, on the ground and against walls (the wall dodge), replicated through `ReplicateMove` as in UT2004.
- Two things switch it off in UC2, both deliberate: (1) `ModifyDoubleClickMove` (`:4007-4045`, "Only allow double
  click dodge to pass if Dodge key has been pressed") drops every double-click result unless the dodge key set
  `bWantsToDodge`; (2) `UCPlayerInput.PlayerInput` computes the edges `bEdgeForward/...` from `aForward/aStrafe`
  before the keyboard's `aBaseY/aBaseX` are added (`UCPlayerInput.uc:388-395`), i.e. from the stick only, so a key
  tap never makes an edge (UT2004's `PlayerInput` used `aBaseY` for forward/back).
- What it would take: in `UCPlayerInput.PlayerInput`, when a new ini key (say `bDoubleTapDodge`, default False) is on,
  compute the four edges from the keys (`aBaseY/aBaseX` minus the stick's share, as `KeysAsStick` does): about 10
  lines; in `PlayerWalking.ModifyDoubleClickMove`, when it is on, pass a direction from `CheckForDoubleClickMove`
  through (`bWantsToDodge` style, honouring `UCPawn.Dodge`'s own tests and the delay-dodge timer): about 8 lines; the
  ini key and `DoubleClickTime` in `DefUser.ini`. Script only (UnrealScript, no native, no engine change), about
  20-30 lines, all in classes of `UCGameplay.u`, so **it can live in the rebuilt `UCGameplay.u` only**; Epic's shipped
  package would keep Space + direction. `PlayerSpidering`/other states that call `ModifyDoubleClickMove` need the same
  look (`:4098` area). UCGameplay is a server package, but the dodge itself is already replicated by Epic's code; a
  rebuilt client and a rebuilt server is the supported pair anyway.
- Interactions to decide first: Lock + a tapped A/D switches target in UC2 (a double tap while locked would also
  dodge), the timed input sequences that watch the stick (`ProcessInputMatching`), and whether a double tap should
  also wall-dodge in the air (UCPawn.Dodge does if allowed). Estimate: half a day with a test in `d6-mouse.sh`.

## Step 6 (2026-10-09): no mouse acceleration natively

- engine-ut2004 (`/home/ion/WorkUnreal/engine-ut2004/Source/SDL2Drv/Src/SDL2Viewport.cpp`, read-only): no mouse hint
  at all (`grep SDL_SetHint` finds `SDL_HINT_ORIENTATIONS` only); `SetMouseCapture` is `SDL_SetRelativeMouseMode`
  and motion is `Ev.motion.xrel/yrel`. So it relies on SDL 2's defaults, which read raw motion in relative mode.
- `Source/SDL2Drv/Src/SDL2Client.cpp` `Init`, before `SDL_InitSubSystem(SDL_INIT_VIDEO)`: `SDL_HINT_MOUSE_RELATIVE_MODE_WARP
  "0"`, `SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE "0"`, `SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE "1.0"` (SDL 2.32's
  `SDL_mouse.c` reads them; `SDL_HINT_MOUSE_RELATIVE_SCALING` acts on an `SDL_Renderer` only, `SDL_render.c:1108`, so
  it is not set). The same values as the defaults: nothing changes today; it pins them. Normal priority, so an
  `SDL_MOUSE_*` environment variable still overrides (SDL's own rule).
- Measured, `tools/dev/d6-mouse.sh accel` on `f1-reb` (`logs/f1/s2-d6/d6/accel.*`): real X pointer motion (XTEST,
  `rmove`) with the Xvfb server's pointer acceleration set to 4x above a threshold of 1 (`XChangePointerControl`, read
  back `[4, 1, 1]`): 200 counts slowly (100 events of 2, 20 ms apart) turn **10.010** units per count, 200 counts fast
  (5 events of 40) **10.005**; pitch 10.010 / 10.000 (the linear mouse's 10.012 per count, within its carried
  fraction): **N counts in = N counts out at both speeds**. Label: measured.
- The control run with `SDL_MOUSE_RELATIVE_MODE_WARP=1` (SDL reads the warped system cursor) gave 9.91 / 10.01: Xvfb
  did not accelerate XTEST's relative motion visibly even then, so this run shows exact counts through SDL's
  relative path at two speeds, but it cannot show that the test would catch a desktop's acceleration. Label: the
  control is inconclusive; a real desktop (X11 with libinput, Windows, Wayland) was not measured.
- Web: pointer lock with `unadjustedMovement` is the frontend's (`rawPointerLock.ts`); not touched (frontend parked).

## Step 7 (2026-10-09, from 02:35): both ways, every suite

Builds at the final engine code (`16d60cb`; nothing in `Source/` outside `Classes/` changed after it): native
`bin/capped 6G 3600 -- cmake --build build-native -j4` rc 0 (`logs/f1/build-native-3.log`); wasm-perf
`source env.sh; bin/capped 4G 1200 -- cmake --preset wasm-perf` + `bin/capped 6G 7200 -- cmake --build
build-wasm-perf -j4` rc 0, 0 errors (`logs/f1/build-wasm-perf-1.log`; `SDLLaunch.wasm` sha256 2b0554bc353e4dcf...;
its `SDLLaunch.data` carries the web inis with `aMouseX Speed=6.0` and `bLazyMouseCamera=False`); windows below.
Script build: `tools/dev/script-build.sh --tree f1-make` rc 0, `Success - 0 error(s), 85 warning(s)`.

Null-renderer suites (`logs/f1/s7b/`, driver `logs/f1/s7b.sh`; load 36-41 from other agents):
- `ucc-bin verifyclasses`: "All native class layouts match" on `f1-van` and `f1-reb`.
- `jit-trace.sh` at uneven pacing (judder RMS per segment fwd/back/right/left, px; limit 1.0):
  rebuilt kbm (rigid) 0.53/0.42/0.54/0.53 PASS; rebuilt kbm lazy 0.43/0.53/1.44/1.49; rebuilt pad 0.38/0.45/1.19/1.55;
  vanilla kbm 0.37/0.48/0.96/1.46; vanilla pad 0.38/0.46/1.17/1.57. Every lazy-camera run (pad on both packages,
  vanilla's mouse, rebuilt with `bLazyMouseCamera=True`) has the same strafe figure: Epic's lazy camera trailing the
  pawn down and up the ramp of the strafe segments (jitter.md step 3: "pad strafes 0.6-2.2 judder at every pacing ...
  identical in the shipped scripts"); forward/back are clean in all five. So the lazy key gives exactly Epic's camera,
  and the rigid camera keeps the jitter fix (b = 0 there, judder 0.42-0.54 = the walk bob).
- `net-test.sh --keep-tree listen` (port 7862) on `f1-netvan` and `f1-netreb`: listen.hit, listen.kill, listen.clean,
  leftovers PASS on both (`logs/f1/s7b/net-*`).
- `d9-rebind.sh` (menu match restart nodefaults pad actions) on `f1-reb`: **119 PASS, 0 FAIL** (`logs/f1/s7-reb/d9/`),
  with the clear-on-conflict expectations: "S: MOVE FORWARD. MOVE BACK LOST S.", S = MoveForward and W unbound in the
  live bindings, in User.ini and after a restart, W walks nowhere in the match, "K: JUMP / DODGE. 1ST / 3RD PERSON
  LOST K." from the pad, the look keys' new commands listed under TURN LEFT/RIGHT, LOOK UP/DOWN.
- windows: `bin/capped 4G 1200 -- cmake --preset windows` + `bin/capped 6G 5400 -- cmake --build build-windows -j4`
  rc 0, 0 errors (`logs/f1/build-windows-1.log`; `uc2.exe` built 02:58).

GL suites (software GL, Xvfb; `logs/f1/s7-reb/`, `logs/f1/s7-van/`, driver `logs/f1/suite.sh`; load 20-43 from other
agents throughout):

| Suite | (b) rebuilt `f1-reb` | (a) vanilla `f1-van` | Difference expected? |
|---|---|---|---|
| m10 keyboard | 16/16 | 16/16 | |
| m10 mouse | 18/18 | 16/18: `ms-mouse-turn-amount` (10.012/count), `ms-invert-option-flips-pitch` (`MouseLookInvert`) | yes: the linear PC mouse and its invert command are the rebuilt package's |
| m10 text | 7/7 | 7/7 | |
| m10 pad | 14/33 at load 37 (the menu never reached the armory: cascade), re-run below | **33/33** (the left stick walks with Epic's package now) | no |
| m10 both | 13/14 (`both-keyboard-move-and-pad-turn-together`: 96 units, wanted 100), re-run below | 14/14 | no |
| d9 | 119/0 (all scenarios) | menu 2/6: no KEYBOARD / MOUSE button | yes: the page is the rebuilt UCMenu.u |
| d3 profiles | ALL PASS | ALL PASS (step 1's one failure was load) | |
| d4 names | ALL PASS | n1/n2/n4 name checks FAIL (the character's name, Epic's rule) | yes: D-4 is the rebuilt GUI.u |
| d5 text | ALL PASS | ALL PASS | |

Re-runs (`logs/f1/s7-reb/m10r*`, `d6lock*`, `logs/f1/s7-rerun.txt`):
- m10 pad on `f1-reb`: at load 37 twice 14/33, both times the same cascade: the character page took the pad's A
  while its open animation was still blocking (log: the "Unblocking animation" line comes AFTER the A), so the armory
  opened one press late and every later step missed (frame-counted menu animations under software GL, as in
  mouse.md steps 8-10); with `M10_SLOW=2` 32/33 (the right-stick turn, held 1.0 s instead of 0.5, passed 180 degrees
  and wrapped: 49,431 units); at load 30 normally: **33/33**.
- m10 both on `f1-reb`: **14/14** (the first run's 96-vs-100 units was the turn swinging the walk).
- d6 lock on `f1-reb`, twice: **12/12** both times (the step-2 failure was the spawn-sensitive check).

Browser (frontend clone, uc2 branch, nothing committed there): `run/f1-webassets` = the asset tree hard-linked with
`System/GUI.u`, `UCGameplay.u`, `UCMenu.u` replaced by this branch's (the canonical tree untouched: its UCGameplay.u
is still 9ca783e7); manifests regenerated into `logs/f1/web/` (1,224 files; 208 packages, 1,804 references all
resolved); `corepack yarn build --target uc2 --assets run/f1-webassets --manifest ... --deps ... --engine-artifact
<worktree>/build-wasm-perf/Source/SDLLaunch` (4G); `serve-web/SDLLaunch.wasm` sha256 2b0554bc353e4dcf... = this
build's, before and after every run (`logs/f1/web/engine-proof.txt`); null sink `uc2f1` loaded and unloaded by module
index; `PULSE_SINK=uc2f1 bin/capped 6G 1500 -- node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=N -- --target uc2
uc2-wasm-boot.spec.js`. Attempt 1 (`--max-gb=14`) and 2 (`--max-gb=18`) were stopped by the guard before or during
the second test: the machine already had 13.5-13.6 GB in use by other agents at the start (the guard's budget is
machine-wide); attempt 3 (18, with 11 GB in use at the start): **4 passed (4.9m)**, peak 13.4 GB machine-wide
(`logs/f1/web/e2e-boot-3.txt`): menu drawn, DM-Deadbolt drawn, keyboard + mouse path, and "mouse look (D-6)": 10 /
10.0125 / 10.01 / 10.01 rotation units per pixel (100 px slow, 400 px slow, 400 px fast, 100 px up) with the mouse on
Epic's binding through the rebuilt package (`ScriptPatcher: UCGameplay is not the package the patch was made for
... rebuilt from source`). No ceiling (`capped`) was hit.

Final script build at HEAD (`tools/dev/script-build.sh --tree f1-make2`): `Success - 0 error(s), 85 warning(s)`;
GUI.u, UCGameplay.u, UCMenu.u byte-identical to `f1-make`'s (the tested packages). The packages to switch:

| Package | sha256 | vs the asset tree now |
|---|---|---|
| GUI.u | 4e5b1af0add70b57d88893694498b84249a98ffc96484912847819a6e780f204 | identical (D-4's; unchanged) |
| UCGameplay.u | 5d76f3de819ba7d613fd5e852c5cbb50d7884f3285d0acac5946dace26660acb | replaces 9ca783e7d17da28a (server package: all peers together) |
| UCMenu.u | a31eb5d9ca4804649e4199710827090118603004265aa9c61122c25d819db2ff | replaces fa149401ff492b4f |

Expected differences between (a) vanilla and (b) rebuilt, all seen and no other: the linear PC mouse (m10
`ms-mouse-turn-amount`, `ms-invert-option-flips-pitch`; d6 look/ini/lazy/lock/accel are rebuilt-only scenarios; with
Epic's package the mouse is Epic's PC mouse, d6 `vanilla`), the rigid camera under the mouse (jit kbm), keys as a
stick (Lock + D target switch, Space dodge by keyboard), the D-4 name rule (d4 n1/n2/n4), the D-9 page (d9). The pad
(curves, dead zones, left-stick walk), keyboard, menus, text, profiles, network: the same on both.

Memory: builds 6G (-j4), configures 4G, script make 3G, game runs 2G, net listen 6G, frontend build 4G, browser 6G
(mem-guard 14 then 18 machine-wide, see above); no `capped` ceiling was hit (no exit 137).

## Follow-up (2026-10-09, from 09:40): the owner's decisions after the F-1 report

Decisions (coordinator's message): (1) the `UnIn.cpp` left-stick fix accepted as made; (2) keyboard "PC friendly":
movement keys strafe, never turn; WASD and the arrows move/strafe (Epic's PC layout for the arrows), PageUp/PageDown may
stay look keys; Shift held = walk if UC2 has a way; (3) `bKeysAsStick` and `MouseLockBreakAngle` hard-wired, their ini
keys and README rows removed; (4) WHEREAMI/TELEPORT as cheats, the NET* test commands behind the same guard, documented
for the other engines; (5) the gate again, both ways.

Not done as asked: main's `-maxfps` -> `[Engine.Engine] FrameRateCap` change (14d1b99) could not be brought onto
this branch (cherry-pick refused by the session's permission rules; "do not merge main"), so this branch's engine
still has `-maxfps` and the scripts here still pass it. At the merge, the new lines in `tools/dev/d6-mouse.sh`
(scenarios vanilla, lazy, accel, walk, cheat) need main's form `"-ini:System:[Engine.Engine]:FrameRateCap=60"`.

### F-1b: the keyboard moves and strafes; does UC2 walk? (measured)

- `Engine/Config/DefUser.ini`: `Left=StrafeLeft`, `Right=StrafeRight` (Epic's PC file; Up/Down were already
  MoveForward/MoveBackward); PageUp/PageDown stay the look stick at 0.8. D-9 rows: STRAFE LEFT/RIGHT list `A,Left` /
  `D,Right`; TURN LEFT/RIGHT stay as actions with no key by default ("THE MOUSE TURNS").
- **UC2 has no walk mode.** No `bIsWalking`/`bRun`/`WalkingPct` in any UC2 class (`APawn`'s is commented out,
  `UnController.cpp:122`); `Shift=Walking` (Epic's binding) names an alias no package defines. Movement speed: the
  stick (or a key) sets the acceleration (`aForward x 24000`), clamped to `AccelRate` 2048 (`UnPhysic.cpp:1036`),
  and the speed grows to `GroundSpeed` whatever the acceleration (`calcVelocity`); `CalcMaxSpeed` only slows
  backpedalling. So a half-pushed stick runs at full speed on the Xbox too.
- Measured, `tools/dev/d6-mouse.sh walk` (from a fixed open spot, steady speed 0.6-1.3 s after the start; watch
  traces at 60 fps; `logs/f1/u1/d6/walk*.json`), on BOTH packages: W **430.0**, pad left stick 1.0 **430.0**, 0.5
  **430.0**, 0.35 **430.0**, Shift+W **430.0** units/s. A Shift that made the keys a half stick would therefore not
  slow the pawn at all; it would only stop Space from dodging (SmartJump dodges above `fSmartJumpDodgePct` 0.5) and
  Lock + A/D from switching. Not implemented: **owner decision needed** (a walk speed is new gameplay: it would need
  a speed scale in the rebuilt script, e.g. in `PlayerMove`, and has no Xbox counterpart). `Shift=Walking` left as
  Epic bound it (dead).

### F-1c: bKeysAsStick and MouseLockBreakAngle hard-wired

`UCPlayerInput.uc`: the two `globalconfig` properties and their defaults removed; `KeysAsStick()` runs whenever a
movement key is held; `MouseBreaksLock` uses 25.0 (a literal: a `const` added a link-order "problem" in script-diff).
`DefUser.ini` keys and their comments removed; README rows rewritten. Script build `--tree f1-make3`: 0 errors, 85
warnings; compare UCGameplay 19 problems (as before), UCMenu 5, GUI different-explained.

### F-1d: WHEREAMI / TELEPORT, cheats, and the NET* test commands behind the same guard

- New `Source/Engine/Src/UC2Cheats.cpp`: `UC2CheatsAllowed` (the guard) and `UC2CheatExec` (WHEREAMI, TELEPORT).
  Native, in Engine, so the commands exist with Epic's packages too. Guard: the local `PlayerController` has a
  `CheatManager` (UC2's rule, `PlayerController.AddCheats`: standalone only), or `[Engine.GameEngine]
  AllowNetCheats=True` (a test key in no shipped ini). Refusals and every run log `Cheat: ...` on `NAME_Log` (the
  engine's log has no name prefix, so the prefix is in the text).
- Hooks: `Source/Engine/Src/UnPlayer.cpp` `UPlayer::Exec`, first thing after the `Actor` check (Epic's
  `CheatManager.Teleport()` has no parameters and would otherwise take `TELEPORT x y z`; bare `TELEPORT` stays
  Epic's); `UnGame.cpp` `UGameEngine::Exec` before `UC2NetTestExec` (the -nettest file, a process without a player).
- `UC2NetTest.cpp`: NETPROBE, NETPLACE, NETPLACENEAR call the guard first (a fourth, aiming command was removed in F-1b).
- `tools/dev/net-test.py`: every process gets `-ini:System:[Engine.GameEngine]:AllowNetCheats=True`. The other users
  (`jit-trace.sh`, `jit-strip.sh`, `m7-effects.sh`, `m7b.sh`, `o9/*.scen`; on main also `fx-owner.sh`,
  `fps-bench.sh`, `f5-flicker.sh`) run standalone games, where the guard allows them: no change needed (checked by
  reading each launch: a map URL without `?Listen`, no server).
- Frontend (not touched, parked): `packages/e2e/uc2-wasm-net.spec.js` sends `NETPROBE` in a browser network game; it
  will be refused unless its launch adds `-ini:System:[Engine.GameEngine]:AllowNetCheats=True`.
  `uc2-wasm-jitter.spec.js` uses NETPLACE standalone: unaffected.
- Reference for other engines: `docs/CHEAT-COMMANDS.md` (names, format, guard, log lines; for the coordinator to copy
  into the frontend's docs).
- Measured, `tools/dev/d6-mouse.sh cheat` on `f1-reb` (`logs/f1/u2/d6/cheat.*`): **13 PASS, 0 FAIL**: `Cheat:
  WHEREAMI 1229.6 -672.9 -130.1 40448 0`; `TELEPORT 1977.8 583.7 -34.1 33000 -1400` -> `Cheat: TELEPORT ok: 1977.8
  583.7 -34.1 33000 -1400` and the probe there; WHEREAMI gives it back exactly; TELEPORT without yaw/pitch keeps the
  view; NETPROBE allowed and logged standalone; bare TELEPORT is Epic's and logs nothing; after `open
  DM-Deadbolt?Listen`: WHEREAMI, TELEPORT and NETPLACE each `Cheat: ... refused: cheats are only for a standalone
  game (UC2's CheatManager rule)`.

### F-1e: the gate again, both ways (2026-10-09 10:00-10:55; load 11-30 from other agents)

Builds at `77897e7`: native `bin/capped 6G 3600 -- cmake --build build-native -j4` rc 0 (`logs/f1/build-native-5.log`);
wasm-perf `bin/capped 6G 7200 -- cmake --build build-wasm-perf -j4` rc 0, 0 errors (`build-wasm-perf-2.log`,
`SDLLaunch.wasm` sha256 7f059866125f88d9...); windows `bin/capped 6G 5400 -- cmake --build build-windows -j4` rc 0, 0
errors (`build-windows-2.log`). Scripts `tools/dev/script-build.sh --tree f1-make3`: `Success - 0 error(s), 85
warning(s)`; compare UCGameplay 19 problems, UCMenu 5, GUI different-explained, all else as shipped.

| Suite | (b) rebuilt `f1-reb` | (a) vanilla `f1-van` | Expected difference |
|---|---|---|---|
| d6 (vanilla look ini lazy pad lock accel walk cheat) | 97 PASS, 0 FAIL (one run, `logs/f1/s8/d6.txt`); vanilla 6/6 and pad-shipped/walk-shipped run on (a) inside it | | |
| pad curve vs Epic's package | worst 0.43% (yaw 0.35, frame quantisation at the slow end), 0.17/0.04/0.18% (yaw 0.5/0.75/1.0), 0.02% (pitch 0.5); left-stick walk 2.44% | | within 0.5% |
| walk speeds | W, pad 1.0/0.5/0.35, Shift+W: 430.0 units/s each | the same, 430.0 each | none |
| m10 keyboard/mouse/text/pad/both | 16/18/7/33/14, all PASS | keyboard 15/16 (E-melee once; 16/16 on two re-runs after the strafe check allowed aim adhesion's yaw), mouse 16/18, text 7, pad 33, both 14 | mouse 2 (linear PC mouse) |
| d9 | 119 PASS, 0 FAIL | menu 2/6 | the page is UCMenu.u's |
| d3 / d4 / d5 | ALL PASS / ALL PASS / ALL PASS | ALL PASS / n1,n2,n4 name checks / ALL PASS | D-4 is GUI.u's |
| verifyclasses | layouts match | layouts match | |
| jit-trace uneven (judder RMS fwd/back/right/left px) | kbm rigid 0.54/0.39/0.53/0.53 PASS; pad 0.38/0.48/0.98/2.09 | kbm 0.44/0.49/0.96/1.60; pad 0.36/0.44/1.56/1.68 | every lazy-camera run: Epic's camera on the strafes' ramp (as in jitter.md step 3) |
| net-test (with `AllowNetCheats` for the harness) | dedicated1 8, dedicated2 9, listen 7, travel 6, leftovers: all PASS | dedicated2 + listen all PASS | |
| dedicated server without the key | `Cheat: NETPROBE / NETPLACE / WHEREAMI / TELEPORT refused: ...` (`run/f1-netreb/System/Netref.log`) | | |
| m7-effects praxis-panes (NETPLACE via -glexec, standalone) | `Cheat: NETPLACE LocalPlayer ...` then `place ... ok`, the A/B pair as before (diff 25.47%, critical 0) | | |
| browser boot spec | 4 passed (3.7m), wasm 7f059866 before and after, UCGameplay 4ed72f0b served, mouse 10/10.0125/10.01/10.01; mem-guard peak 12.3 GB | | |

Packages to switch (`run/f1-make3/System`): GUI.u 4e5b1af0add70b57d88893694498b84249a98ffc96484912847819a6e780f204
(unchanged), UCGameplay.u 4ed72f0baba99fd0636b39367bcc44a6edb799550b7ed54347cf9951d095e6f5, UCMenu.u
b9202a59c2af4820b1028ebffe88b693422081171952bd8c42d0882ee4c1e8a5.
Memory: builds 6G, script make 3G, game runs 2G, net 6G, frontend build 4G, browser 6G (mem-guard 18); no ceiling hit.

## F-1b (2026-10-09, worktree engine-uc2-f1b, branch f1b-cheats from main 9764bc8): the cheat guard, final rule

Coordinator's decision, then the owner's (replacing its point 2): allowed without any key for a standalone game AND
the local player of a listen server (the host is the server); refused for every network client and a dedicated
server; `[Engine.GameEngine] AllowNetCheats=True` opens them for the network tests only and must never be in a
shipped ini. The aiming test command is DELETED from the engine (owner: "test-specific commands do not ship in the
binary; general mechanisms such as -ini: may"); the tests do without it.

Code:
- `Source/Engine/Src/UC2Cheats.cpp` `UC2CheatsAllowed` (`:56-87`): the net mode is the local player's level's, else
  the game engine's level's (a dedicated server has no player); `Allowed = PC && (PC->CheatManager || NetMode ==
  NM_ListenServer)` (`:71`); otherwise the test key (`:73-80`); refusal: "Cheat: X refused: cheats are only for a
  standalone game or the host of a listen server".
- `Source/Engine/Src/UC2NetTest.cpp`: the aiming command, its help text and its dispatch removed (`:253` notes it, without its name);
  NETPROBE, NETPLACE, NETPLACENEAR keep the guard. The engine binary has no "NETAIM" string (`strings | grep -ic
  netaim` = 0).
- `tools/dev/net-test.py`: a client's view cannot be turned from outside (UC2's Controller.ClientSetRotation exists
  but would need a command to send it), so the players are lined up around the client's OWN view:
  `view_of` (the client's SDL2 probe: loc, rot), `line_up` (victim at P0 = jit-trace's open spot, the shooter
  NETPLACEd 300 units behind it along its own yaw), `first_person` (the shooter's own `ToggleThirdCam`: UC2's
  third-person shot leaves the camera ~93 units above the pawn, so a level view passes over a pawn on the same
  floor; measured cam z - pawn z = 93 in third, 48 in first person), `walk_toward` (the walker's left stick pushed
  toward the other player in its own view frame). `fight` lines up every 5 shots; the health sequence too; glshot
  lines up at 350. New check `cheats` (listen host and a client without the key, then a client with it; its players
  NetC/NetK). The first try without first person: every shot missed (hit 0/40, the camera's level ray over the
  victim's head).
- `tools/dev/d6-mouse.sh cheat`: the listen part expects the host to be allowed (WHEREAMI, TELEPORT, NETPLACE).
- `docs/CHEAT-COMMANDS.md`, `Engine/Config/README.md`, `analysis/network.md` follow. No script changed.

Results (native build of this worktree rc 0; `logs/f1b/`):
- `D6_TREE=f1b-reb tools/dev/d6-mouse.sh cheat`: **13 PASS, 0 FAIL** (`d6b/`): standalone as before; on the listen
  host `Cheat: WHEREAMI 1531.9 3111.5 -34.1 24656 0`, `Cheat: TELEPORT ok: 1977.8 583.7 -34.1 33000 0`, `place
  LocalPlayer ok`.
- `NET_TREE=f1b-net tools/dev/net-test.sh --keep-tree cheats dedicated1 dedicated2 listen travel` (`net-7`): **all
  PASS** (cheats 8 + clean, dedicated1 8, dedicated2 9: hit on shot 6, kill after 14, health seen alike by all three;
  listen 8: hit on shot 1, kill after 9; travel 6; leftovers). The first shots of dedicated2 missed until the second
  line-up (the first was taken right after the toggle to first person: not investigated further).
- Earlier runs of this step (with the server-side aiming that is now deleted): `net-1`..`net-6`; one `listen.kill`
  failure in `net-1` from an unexplained SIGINT to a client at 35 s, not reproduced.
- Browser: `uc2-wasm-net.spec.js` once (wasm e48cbf22..., before the deletion; host = listen server): **2 failed**
  (`logs/f1b/web/e2e-net.txt`). The host's NETPROBE answered every time (E2EH1..E2EH14, net=ListenServer: the
  host is allowed now), but the spec ALSO probes the guest (`probe(clientPage, client, 'C')`, spec :320, :371,
  :386) and a client's NETPROBE is refused without the key, so "the guest player spawned" got null. It passes again
  only with a frontend change (the guest page's launch with `-ini:System:[Engine.GameEngine]:AllowNetCheats=True`,
  or the spec reading the guest's state from the host's probe): parked frontend work, not done.
- Builds at the end of F-1b: native rc 0 (`logs/f1b/build-native-3.log`), wasm-perf rc 0, 0 errors
  (`build-wasm-perf-3.log`, `SDLLaunch.wasm` sha256 04dce632a959d418...; no aiming-command string in it). The browser
  spec was not re-run with this final wasm: its guest probe needs the frontend change above whatever the wasm.

## F-1c (2026-10-09 11:40-11:55): NETPROBE on a network client answers about the local player only

Coordinator's rule: a client's NETPROBE without the key reads only the client's own state (what its HUD shows), so
it is not a cheat there.
- `Source/Engine/Src/UC2Cheats.cpp` `UC2CheatsWouldAllow` (`:60-80`): the rule without logging (standalone or
  listen host: `:74`; else the test key); `UC2CheatsAllowed` (`:84-`) uses it, so the rule lives in one place.
- `Source/Engine/Src/UC2NetTest.cpp`: NETPROBE (`:271-279`): on a network client that the rule does not allow,
  `Probe(..., SelfOnly=1)`: the summary line as before, a "`<tag> self only (network client)`" line (`:108-109`),
  then only the local player's own pawn and PRI lines (`:115-117`); no refusal and no `Cheat:` line. Elsewhere the
  guard as before (full answer with the key, standalone or a listen host; a dedicated server without the key
  refused).
- WHEREAMI stays a cheat on clients (refused): the owner asked for the pair to be marked as cheats; documented in
  `docs/CHEAT-COMMANDS.md`, which also describes the self-only NETPROBE; `Engine/Config/README.md` row follows.
- `tools/dev/net-test.py` `cheats`: asserts the self-only answer (summary + "self only" line + pawn/pri lines about
  NetC only, no `Cheat:` line) and the full answer with the key (pri lines about NetK and NetL).

Results:
- `NET_TREE=f1b-net tools/dev/net-test.sh --keep-tree cheats listen` (`logs/f1b/net-8`): **all PASS** (cheats 8
  including `client-netprobe-self-only-without-key`: "NetProbe: C0 summary net=Client ... local=NetC", "NetProbe: C0
  self only (network client)", "NetProbe: C0 pri name=NetC ..." and nothing about NetL; `client-netprobe-full-with-key`:
  pri lines about NetK and NetL; listen 8: hit on shot 1, kill after 9).
- wasm-perf rc 0 (`logs/f1b/build-wasm-perf-4.log`), `SDLLaunch.wasm` sha256
  a6bafa7e10b79805bb6d1c350d4f3ff8002a974e744d032c5fe18eb2e74319bc (contains the "self only" string).
- Browser, `uc2-wasm-net.spec.js` once (serve-web assembled with that wasm, sha256 equal before and after; asset tree
  `run/f1-webassets`; null sink by module index; mem-guard 18): **2 passed (4.0m)**, peak 13.9 GB
  (`logs/f1b/web2/e2e-net.txt`).
- Noticed, not mine: `ufrontend` has an uncommitted change to `targets/uc2/file-manifest.json` dated 11:28:28, when I
  ran no frontend command (my frontend builds ran at 11:15 and 11:48). Left untouched.

## G-8 double-tap dodge (2026-10-10, worktree engine-uc2-g8-dodge, branch g8-dodge from main 352dd5a)

Owner's decision (PLAN.md, review of 2026-10-10 task list via the coordinator): UT2004-style double-tap dodging in the
REBUILT `UCGameplay.u` only, behind `[UCGameplay.UCPlayerInput] bDoubleTapDodge` (default False), key taps counted as
well as the stick. Epic's shipped package keeps Space + direction only (it does not know the key).

### Step 1: the code (script only, no native change)

- `Source/UCGameplay/Classes/UCPlayerInput.uc`: `var globalconfig bool bDoubleTapDodge` (non-native class: no C++
  layout); in `PlayerInput`, before Epic's edge lines, when the key is on, `KeyX/KeyY = KeyAxis(aBaseX/aBaseY, the
  stick's -1..1)`; Epic's eight edge/was lines OR them in (`aForward > 0 || KeyY > 0`): off, KeyX/KeyY stay 0, the
  edges are the stick's alone, as Epic wrote them. `KeyAxis` is KeysAsStick's arithmetic (the keys' 24000 without
  the stick's walk that UnIn.cpp adds on PC; the Xbox branch without it) moved into a function; KeysAsStick calls it
  (same values). The first-tick log adds "UCPlayerInput: double-tap dodge on (bDoubleTapDodge), DoubleClickTime=..."
  when the key is on (a general diagnostic like the mouse line next to it). Default `bDoubleTapDodge=false`.
- `Source/UCGameplay/Classes/UCPlayerController.uc` `PlayerWalking.ModifyDoubleClickMove`: after Epic's two cases
  (the dodge key's direction, the delayed dodge), a double-tap result (`DCLICK_Forward..Right`) is returned instead of
  dropped when the key is on and the Lock button is not held (`!bWantsToLock`). Then Epic's `UnrealPlayer.
  PlayerWalking.ProcessMove` calls `UCPawn.Dodge` exactly as for Space + direction (direction from Ctrl_Move_*,
  which KeysAsStick fills; replicated through ServerMove as before). `CheckForDoubleClickMove` is untouched
  (UT2004's code, `DoubleClickTime` 0.25 from `[Engine.PlayerInput]`).
- `Engine/Config/DefUser.ini` (+ `web/DefUser.ini` by `tools/dev/sync-web-ini.py`): `bDoubleTapDodge=False` with a
  comment. `tools/dev/script-build.sh`: tree names may start with `g<N>` (logs to `logs/g<N>`).

Design decisions:
1. **Lock + tapped A/D.** Target switching happens only while the Lock button is held (`SearchForLockTarget`
   runs while `bWantsToLock`; the first press of a direction switches at once when the 0.3 s search timer has run
   out). The first tap of a double tap cannot be told from a single tap when it happens, so "a double tap dodges and
   does not switch" with the button held would need every keyboard switch delayed by DoubleClickTime (0.25 s) or a
   switch undone afterwards. Decided instead: **while the Lock button is held, taps only pick targets (no
   double-tap dodge; a double tap switches once, as before); locked on with the button released (the usual
   circle-strafe), a double tap dodges and keeps the target** (a dodge never touches Enemy). Space + direction dodges
   in both. The pad stick follows the same rule.
2. **Wall dodge.** `UCPawn.Dodge` already does it: in `PHYS_Falling` it calls `CanWallDodge` (a wall behind the dodge
   direction, `NextWallDodgeTime`, `MaxWallDodgeCount`, the air-dodge variant), `bCanWallDodge=true` in UCPawn's
   defaults; Space + direction in the air uses it today. So a double tap in the air against a wall wall-dodges, as in
   UT2004 (and as in UT2004 not right after a ground dodge: `DoubleClickDir` stays Active until landing).
3. The timed input sequences (`ProcessInputMatching`) read the stick and keys as before; nothing changes for them.

### Step 2: build, compare, run trees (2026-10-10 04:05-04:20)

- Native: `cmake --preset native && cmake --build build-native` under `bin/capped 6G 3600` (started -j4 at load 17,
  stopped and resumed -j2 when the load passed 24; `logs/g8/build-native*.log`, exit 0).
- Scripts: `tools/dev/script-build.sh --tree g8-make` (make under 3G): **Success - 0 error(s), 85 warning(s)** (the
  same 85 and the same `ucc-bin make` exit 1 as main's d12-make). Compare against the shipped packages: every package
  as on main (Core identical ... UCMenu different, problems 5, unchanged); UCGameplay problems **20** (main's d12
  build: 19). The per-function listing differs from d12-make's in exactly two lines: `UCPlayerController.PlayerWalking.
  ModifyDoubleClickMove` 457 -> 538 bytes (now a PROBLEM: changed against the shipped one) and `UCPlayerInput.
  PlayerInput` 2444 -> 2759; the appended exports add `KeyAxis` (+ params, local V), `PlayerInput.KeyX/KeyY`,
  `bDoubleTapDodge`. So the compare shows only these hunks. (A direct compare against the asset tree's rebuilt
  UCGameplay.u, `UC2_SHIPPED_SYSTEM=assets/uc2/System`, crashes script-diff.py on a function that exists on one side
  only, `explain` `funcflags` None; not pursued: the d12 listing diff above answers the question.)
- **New `UCGameplay.u` sha256 45dd2558ac16c48f82a2c4ccb49ad09f0aa4cf6a070968ab0c6f07caa8e7881f** (2856360 bytes before
  strip as built; the stripped file in `run/g8-make/System`). The asset tree's (main) is 4ed72f0b..., shipped c675a442....
- Run trees: `run/g8-run` = the asset tree with only this UCGameplay.u (`UC2_SCRIPTS=logs/g8/ucg`); `run/g8-ship` = the
  shipped GUI/UCGameplay/UCMenu (`assets/uc2-shipped/System`); `run/g8-base` = the asset tree as is (main's package).

### Step 3: the tests (`tools/dev/d6-mouse.sh dodge`, new; runs started at load below 16)

Exploration first (`logs/g8/explore`): from the open spot (1977.8 583.7 -34.1, yaw 33000) D held 6 s runs 1764 units
without a wall, A stops after 542 units at **1938.2 1125.8 -34.3** (a wall on the left; WHEREAMI), W runs on, S drops
to a lower floor. The wall part therefore jumps there and double-taps D (away from the wall).

Run 1 (`logs/g8/d6-1`): 12 PASS, 5 FAIL: the wall part had no wall (D held 3 s, fixed as above) and the lock checks
were tied to the probe order of the bots (one bot could not be placed at 400 units; the lock took the other one
first). The behaviour itself was right in that run (switch, switch back without a rise, locked-on dodge kept the
target); the checks became relative (`la` any target, `le` another one, `lf` back to `la`'s, `lg` = `lf`).

Run 2 (`logs/g8/d6-2`, load 15.9): **17 PASS, 0 FAIL**. Measurements (hop(): rise = highest Z above the start,
side = displacement to the right of the start's view at the end, peak = top horizontal speed over 3 ticks):

| part (0.6 s of key time unless said) | rise | side | peak u/s |
|---|---|---|---|
| A held | 0.0 | -240.4 | 430.7 |
| A double tap (0.06 down, 0.06 up, 0.48 down), key on | **52.2** | **-432.8** | **624.5** |
| Space + A, key on | 52.2 | -435.9 | 624.4 |
| at the wall: jump, then D held 0.4 s | 67.2 | 115.2 | 289.7 |
| at the wall: jump, then D double tap | **131.1** | **582.6** | **624.4** (wall dodge) |
| Lock held, A double tap (switches back) | 0.0 | -30.7 | 193.9 |
| locked on, button released, D double tap | 52.2 | 406.8 | 624.4 (still locked, same target) |
| key off (the default): A held / A double tap / Space + A | 0.0 / 0.0 / 52.2 | -231.4 / -201.3 / -434.4 | 430.5 / 430.5 / 624.4 |

The dodge's 624 u/s and 52-unit rise are the same from Space + A (Epic's path) and from the double tap: the same
`UCPawn.Dodge`. Log: "UCPlayerInput: double-tap dodge on (bDoubleTapDodge), DoubleClickTime=0.25" with the key on,
no such line with it off (`dodge-key-on-logged`, `dodge-off-key-off-by-default`). Labels: double-tap dodge, Space
dodge, wall dodge, the Lock rules and the key off: **measured** (native, null renderer, 60 fps); network play: **not
exercised** (the dodge goes through Epic's ServerMove like Space + direction; both peers need this package); the pad
stick's double flick: **not exercised** (the same edges; the pad scenario only pushes and releases).

Pad (`logs/g8/d6-pad`, load 15.1): `d6-mouse.sh pad` **10 PASS, 0 FAIL**: right-stick curves against the shipped
scripts worst 0.27% (yaw 0.35), 0.08% (0.5), 0.12% (0.75), 0.17% (1.0), pitch 0.07%; left-stick walk 2.36% (limit 3%).

Lock regression with the key off (`d6-mouse.sh lock`, Lock + D, mouse break-away, adhesion): on `g8-run` at load 15.7
11 PASS + 1 FAIL twice, a different check each time: `lock-aim-adhesion-under-the-pad` (`logs/g8/d6-lock`: the pawn
strafed only 44 units, the check wants 50; adhesion yaw 75 < 100) and `lock-pulls-back-after-the-mouse-stops`
(`logs/g8/d6-lock2`: offsets [1801, 1801]; the same non-pulling values [1753, 1753] / [1773, 1757] are in earlier
agents' main-based runs, `logs/d6/mouse/lock.json`, `logs/jit/d6/lock.json`). Quieter (load 14): main's package
(`run/g8-base`, `logs/g8/d6-lock-base`) **12 PASS, 0 FAIL**, this package (`logs/g8/d6-lock3`) **12 PASS, 0 FAIL**.
Read as the scenario's known timing flakes under load, not a change of behaviour (key off, the edges and
ModifyDoubleClickMove's result are Epic's).

### Gate (G-8)

`tools/dev/d6-mouse.sh dodge` 17/17 (`logs/g8/d6-2`), `pad` 10/10 with the curves within 0.27% of the shipped scripts
(`logs/g8/d6-pad`), `lock` 12/12 (`logs/g8/d6-lock3`); script build 0 errors, compare shows only
ModifyDoubleClickMove and PlayerInput (+ KeyAxis, KeyX/KeyY, bDoubleTapDodge); UCGameplay.u sha256 45dd2558....
Open: the asset tree switch (coordinator: `assets/uc2/System/UCGameplay.u` from `run/g8-make/System`, and every
network peer with it); the web build's ini already has the key (False).
