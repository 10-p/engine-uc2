# D-7 / D-8 notebook: the console, and AutoFOV (Hor+ widescreen field of view)

Tasks (PLAN.md Phase D, owner's review of 2026-10-06): D-7 "Is the console locked or hidden? Can we unlock it?";
D-8 "Lets implement the AutoFOV work that has worked so well in all of the other engines." Worktree
`engine-uc2-d78`, branch `d78-console-fov` (from main 421cb95). Every run through `bin/capped`; run trees
`run/d78*`, logs `logs/d78/`.

## Step 0 (2026-10-07): what exists

### The console (D-7)

- It exists and is not locked. `[Engine.Engine] Console=Engine.Console`; `UGameEngine::Init` (UnGame.cpp:257)
  adds it as a GLOBAL interaction after the GUI controller. No `FINAL_RELEASE`-style gate, no enable flag: the
  only switch is `[Engine.Console] ConsoleKey` (globalconfig byte), and the shipped Default.ini (PC and Xbox)
  says `ConsoleKey=9`, which is **Tab**.
- Key order (UnInteractionMaster.cpp `MasterProcessKeyEvent`): script global interactions first (the Console
  is a script interaction), then native globals (the GUI controller has `bNativeEvents=True`), then the
  viewports' local interactions, then the input bindings. So the Console sees every key before the menus and
  before `[Engine.Input]`. Consequences of `ConsoleKey=9`:
  - in a match, Tab opens the console's typing line, so M10's `Tab=ShowScores` binding never fires (F1 and the
    pad's Back still show the scores). D-2 saw this: "Tab in the pinned match opened the console's typing line";
  - in the menus, Tab opens the console too, so M10's `[GUI.EmulatedJoypad] Tab=Start` never reaches the GUI.
  - A PC player presses the key left of 1 (`~`/`` ` ``, `IK_Tilde` 0xC0) and nothing happens: that is "hidden".
- What the console is: Epic's minimal UE2 console (Console.uc): one typing line (`(> text_`) drawn at
  ConsoleX/ConsoleY = 32,32 (since D-2 inside the 4:3 box), history Up/Down, line editing. `Message()` is empty:
  it shows NO output. Commands go to `Viewports(0)->Exec` with the viewport as output device, whose `Serialize`
  writes to the log (and to the interactions' `Message`, ignored). So replies are only in the log. Enter runs
  the line (on release) and closes it; Escape clears a non-empty line, Escape on an empty line closes; the
  typing filters `~` and `` ` `` (Epic designed it for the tilde key). Typed characters arrive through SDL's
  text input (`SDL_TEXTINPUT` -> `UEngine::Key` -> `MasterProcessKeyType`), console first.
- Template: engine-ut2004 binds `Tilde=ConsoleToggle` in DefUser.ini (its ExtendedConsole). UC2's Console has no
  `ConsoleToggle`; its own mechanism is `ConsoleKey`. UC2's DefUser.ini has `Tilde=` (empty).
- Web: the persisted `System/Avaris.ini` (C-2, web-ini-saves.md) keeps whatever ConsoleKey a player's first
  boot derived (9), so changing Default.ini alone does not reach an existing web profile.

Plan: `ConsoleKey=192` (IK_Tilde, the key left of 1 whatever the layout: SDL scancode GRAVE) in Default.ini (and
the web copy through sync-web-ini.py); a stale `ConsoleKey=9` (the shipped value, which collides with the
scoreboard and the menus' Start) is moved to Tilde once at start-up with a log line. Tab is then the scoreboard
in a match and Start in the menus, as M10 intended.

### The field of view (D-8)

- UE2 `FovAngle` is the HORIZONTAL FOV: `FCameraSceneNode::UpdateMatrices` (UnRender.cpp:1777) builds
  `FPerspectiveMatrix(FOVX, FOVY, 1, Aspect)` with FOVX = FOVY = FovAngle/2 (times `ScaleFOVX/Y`), row 1 =
  Aspect/tan(FOVY). So at 16:9 the horizontal stays 90 and the vertical shrinks from 73.74 to 58.72 deg (Vert-):
  the world looks zoomed vertically (D-2's "world looks zoomed at 16:9"). On the Xbox, widescreen TV mode did the
  same: `FD3DRenderInterface::GetPixelAspectRatio` returns 4/3 with `D3DPRESENTFLAG_WIDESCREEN`, so the 640x480
  buffer got a 16:9 projection with the horizontal kept (Vert-). The GL device returns 1.
- Native consumers of the player's FOV during a frame: the scene nodes built in `UGameEngine::Draw`
  (UnGame.cpp:3182 world, :3229 split-screen menu overlay, :3006 precache), skeletal/vertex mesh LOD
  (`FovBias = tan(FovAngle/2)`, UnSkeletalMesh.cpp:5518, UnVertMesh.cpp:1131), the shadow projector's bias
  (UnShadowProjector.cpp:506), `UCanvas::execDrawActor`'s default FOV (UnCanvas.cpp:1424), `WorldToScreen` /
  `ScreenToWorld` (UnController.cpp:3273/3324: their own `FCameraSceneNode` from `FovAngle`), the cull distance
  (UnRenderVisibility.cpp:45, a `static` computed once, clamped to 10..85: not affected). Writers during a frame:
  `UUCScreenTeleportOverlay::PreRender` (UCCameraEffects.cpp:2111) interpolates `FovAngle` to 170 and back (the
  teleport effect; it takes `OriginalFOV` from `FovAngle` inside the frame); `DrawPortal` / scripted textures
  save and restore it. Tick-time readers (aim help, `ScalePawn`, OverflowBall): ratios to `DefaultFOV`, fine
  with the base.
- Script: the first-person weapon is drawn by `UCWeapon.RenderOverlays` (from `HUD.PostRender3DItems`, inside the
  frame) through `Canvas.DrawActor(self,false,true,ScaledDisplayFOV)` with its own `DisplayFOV` (60, shock rifle
  80, split-screen x1.25 for the race weapon); combo effects and `Inventory.RenderOverlays` call DrawActor with no
  FOV (= the actor's). The weapon's placement (`Pawn.CalcDrawOffset`) divides by `Weapon.DisplayFOV`, not by the
  player's FOV, so UT99's placement trap (2.29) does not exist here. Zoom: `UCWeap_SniperRifle` sets
  `DesiredFOV`/`FOVAngle` (ZoomFOV), `UCPlayerController.GetDesiredFOV` (third/first person, `LockedFOVShift`
  while locked, `HijackFOV` for combos), `UCMeleeAttack_Jump` charge FOV, matinee `USubActionFOV` (tick). Mouse
  sensitivity uses `DesiredFOV` (`UCPlayerInput` FOVScale) - must not change.
- 3D markers: `Element_TargetReticle` (lock-on), `UCHUD.DrawExtra` (reflect icon), `UCCombo_Sonar` all use
  `PlayerController.WorldToScreen` from the HUD's PostRender (inside the frame).
- Cinematic letterbox (`bRenderCinematics`, matinee `bCinematicView`): `ScaleFOVY = 1/CinematicsRatio` (1.66)
  multiplies the vertical half-ANGLE; the band is `SizeY*ScaleFOVY` high. Split screen: `ScaleFOVX/Y =
  ReduceFOV` (1.0 for two players, 0.5 for three/four), the split viewports' `ScaleX/ScaleY` fractions.
- Template (engine-ut2004, frontend steps 2.16/2.29): `[ufront.AutoFOV] AutoFOV=2 ReferenceAspect=1.333333
  WeaponScale=1.0`; `ufrontEffectiveFov` (Off / HorPlus / Adaptive = max(base, Hor+)), a scoped override of
  `Viewport->Actor->FovAngle` around `UGameEngine::Draw` after `PlayerCalcView` (so script outside the frame, and
  the mouse, keep the real FOV), `GAutoFovFrameBase`, and the viewmodel pass in `UCanvas::DrawActor`
  (`ufrontWeaponFov`; the default-FOV double-correction trap). The template has no console command and no
  command-line switches (owner's rule there: one home per setting, `-ini:` overrides for tests).

Plan (all native): port the template's helpers and scope; UC2 additions: (1) aspect from the whole window for
split viewports (`SizeX/ScaleX : SizeY/ScaleY`, = SizeX/SizeY for one viewport); (2) a FOV written during the
frame (the teleport effect) is kept, mapped back to the base (inverse Hor+), instead of being discarded by the
restore; (3) the letterbox's vertical scale made exact in tan space when widened (4:3 untouched);
(4) `WorldToScreen`/`ScreenToWorld` use the effective FOV also when called outside the frame; (5) the
omitted-DisplayFOV case of `execDrawActor` detected exactly; (6) console `AUTOFOV [0|1|2]` for the session (A/B
and the measurements); one log line per change of viewport/base.

Verification plan: one game run with the world frozen (TimeDilation), `SETRES` between 1440x1080 (4:3),
1920x1080 (16:9) and 2560x1080 (21:9) at the same height: the 4:3 picture must be the centre crop of the wider
ones (world, third and first person, zoomed); 640x480 and 1024x768 against main's binary (pinned, static part);
the lock-on reticle on the target at 16:9; the console scenario.

Next: build main's binary as the reference (in progress), then the native code.
