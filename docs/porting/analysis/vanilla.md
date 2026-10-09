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
