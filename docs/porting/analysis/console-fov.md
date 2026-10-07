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

## Step 1 (2026-10-07): the native code, first runs

Reference: this worktree built unchanged (= main 421cb95; deps copied from main's `build-native/_deps` into
`build-native/_src`, configured with `FETCHCONTENT_FULLY_DISCONNECTED=ON` and `FETCHCONTENT_SOURCE_DIR_*`),
`bin/capped 6G 3600 -- cmake --build build-native -j6` rc 0; copied to `run/d78-bin/uc2-main-421cb95.bin`,
run tree `run/d78-ref` (md5 b7ef9067...). Changed builds: run tree `run/d78`.

Code (commits 6f9a9fa, 868f41d; every hunk says D-7 or D-8):
- `UnRender.h/.cpp`: `[ufront.AutoFOV]` AutoFOV (0/1/2, default 2 Adaptive), ReferenceAspect (4/3),
  WeaponScale (1); `ufrontEffectiveFov`, `ufrontBaseFov` (inverse), `ufrontWeaponFov`, `ufrontAutoFovSize`
  (split viewports: SizeX/ScaleX x SizeY/ScaleY), `GAutoFovScopeActor/FrameBase/FrameEffective`, console
  `AUTOFOV [Off|HorPlus|Adaptive|0|1|2]` (session). Log lines in the template's format:
  `[AutoFOV] mode=...`, `[AutoFOV] viewport=WxH aspect=A base=B effective=E` (per change of size, base by
  more than 0.5 deg, or mode), plus `[AutoFOV] weapon viewport=WxH base=B drawFov=D` (not the UT99-only
  `placeFov` format, so the frontend's shared viewmodel test is not triggered). Within 1e-4 of the reference
  aspect the FOV is returned untouched (the ini's 1.333333 is not 4/3 exactly; atan(tan(x)) is not x).
  Letterbox (`FPlayerSceneNode::Render`): when widened, `ScaleFOVY` is set so the band shows the 4:3 band's
  vertical extent (Epic multiplies the half-angle; Hor+ is tan space); the band height keeps 1/CinematicsRatio.
- `UnGame.cpp`: `FScopedAutoFov` in `UGameEngine::Draw` after PlayerCalcView; a FOV written during the frame
  (teleport effect) is kept as `ufrontBaseFov(value)` (snapped to the saved/desired FOV within 0.001);
  `AUTOFOV` in `UGameEngine::Exec`; the console's `ConsoleKey` 9 (Tab) moved to 192 at start-up, logged
  `Console: Engine.Console, ConsoleKey 192`.
- `UnCanvas.cpp`: `execDrawActor`'s omitted FOV is -1 (was the actor's FovAngle); `DrawActor` resolves it to
  the frame's base FOV (inside the frame) and corrects every canvas-drawn actor's FOV with
  `ufrontWeaponFov(F, ufrontEffectiveFov(F))` (template's viewmodel pass; no equality heuristic: it could
  misfire when a zoom passes the weapon's DisplayFOV).
- `UnController.cpp`: `WorldToScreen`/`ScreenToWorld` take `AutoFovViewFov` (the frame's value inside the
  frame, the corrected base outside).
- `UnInteractionMaster.cpp`: the release (and the control character) of a key whose press the console took
  while typing stops at the console (see step 2).
- `SDL2InputTest.cpp` probe: `console= typed="" scores= fov= desiredfov= view=WxH` appended (m10-inject.py
  parses them, optional). `Engine/Config/Default.ini`: `ConsoleKey=192`, `[ufront.AutoFOV]`; web regenerated
  (`tools/dev/sync-web-ini.py`, `--check` 0).
- `m10-controls.sh`: `M10_XVFB=<WxH>` (Xvfb screen big enough for a scenario that resizes with TEMPSETRES).

First AutoFOV run (`tools/dev/d78/fov.scen`, `M10_RES=1440x1080 M10_XVFB=2560x1080 M10_MEM=3G`, `-nopostprocess`,
`logs/d78/t1`): 5/5 checks PASS (sizes, base FovAngle untouched outside the frame = 90 at every size, zoom 40).
Log: `effective=106.260216` at 1920x1080 (template: 106.26), `121.284508` at 2560x1080, weapon 60 -> 75.178 /
91.493; zoom 40 -> 51.774 / 65.810; 4:3 -> unchanged. World frozen with `set LevelInfo TimeDilation 0.001`
(slomo stops at 0.1). Pictures compared with the centre 1440 columns of the wider ones: third person 99.28% of
pixels within 2 levels at 16:9, 97.94% at 21:9 (AUTOFOV 0 at 16:9: 8.55%); first person 89%: the differences
are the weapon's outline (and two animated pickups); weapon on black: right hand bbox (948,702) / (948,701) /
(946,698), area +0.5% / +1.8%, AUTOFOV 0 (1023,753) area -7%: same size and place within a few pixels, the
drift to be separated from the idle animation (the shots were in aspect order = time order). Lock-on: the
reticle on the target at all three sizes (seen); the camera still turned under the lock (real-time interpolation),
so whole-picture comparison does not apply there: measure reticle vs figure instead (D-2's `lock_marker`).

## Step 2 (2026-10-07): the console, measured; Escape's release

- Main's binary (`run/d78-ref`, console-game.scen as "ref-console-game"): Tab opened the console's typing line,
  typing and Enter worked (`32.000000` replied in the log); its probe has no console fields, so the other checks
  cannot be read there. So the console was never locked: it was on Tab, where PC players do not look, and it
  hid the scoreboard and the menus' Start.
- Ours, first run: Escape on a non-empty line closed the console at once: Epic's Console.uc clears the line on
  the press and closes on the release (the Escape branch does not look at the action). So "Escape closes" is
  Epic's behaviour; the scenario now expects that. Escape on an EMPTY line closed the console on the press,
  and the release went on to the menu page under it (`UGUIPage::NativeKeyEvent` closes a page on the release
  of Escape): fixed natively in the interaction master (868f41d), no script change.
- `tools/dev/d78/console-game.scen` (DM-Deadbolt, 640x480): 12/12 PASS (`logs/d78/t3`): ConsoleKey 192 logged;
  Tab toggles the scoreboard (scores 1, console 0; again: 0); `grave` opens the console; typing
  `get Engine.GameEngine CacheSizeMegs` fills the line and the pawn does not move; Enter -> `32.000000` in the
  log, the line closed; Escape with text closes (no pause menu); Escape on an empty line closes; `AUTOFOV` ->
  `[AutoFOV] mode=2 (Adaptive)`; Up brings back the last line. `console-menu.scen`: 8/8 PASS: Tab in the main
  menu does not open the console; grave does; typing `was AUTOFOV` (W/A/S are D-pad in the menus' key table)
  leaves the menu's focus where it was; Escape closes and the page and focus stay; Enter runs `AUTOFOV`; on a
  sub-page, grave + Escape leaves the sub-page open.

Next: README rows, the measurement script (d78-measure.py, d78.sh), the 640x480/1024x768 identity against main.

## Step 3 (2026-10-07): the verification script, first full pass

`tools/dev/d78.sh` (runs) + `tools/dev/d78-measure.py` (checks), output `logs/d78/run/`. First pass
(`logs/d78/d78-run-1.txt`): ident 640x480/1024x768 (ours and main's binary, pinned, -nopostprocess), fov, console
game/menu, fov-pp (post-processing on, the owner's pictures). Measured so far:
- identical-to-main 640x480 and 1024x768: static part 100.000% within 2 levels (93.7%/92.7% of the whole frame
  bit-identical: the animated player model, pickups and reticle), `[AutoFOV] ... base=90 effective=90`.
- fov-numbers: every logged effective FOV = 2*atan(tan(base/2)*aspect/(4/3)) within 0.001 (world 90 and zoom 40,
  weapon 60, at 16:9 and 21:9; unchanged at 4:3).
- world-third-person 99.23% / 97.82% (16:9 / 21:9 centre vs 4:3), control AUTOFOV 0 8.58%; letterbox 99.76% /
  99.72% (control 45.97%); world-first-person (above the weapon) 100.00% / 100.00% (control 10.70%); zoom 40
  100.00% / 100.00%; vertical scale of the scene against 4:3: 1.000 at 16:9 and 21:9 and zoomed, controls 1.310
  (stock 16:9 is 1.333) and 1.762 (1.778) - the HUD, which does not scale, pulls the controls slightly toward 1.
- weapon on black: area -2.3% / -1.3%, centroid 2.2 / 1.3 px against 4:3 - but the SAME 1440x1080 picture taken
  again 40 s later moved by 4.8% / 4.7 px: the weapon's idle animation, not AutoFOV (TimeDilation 0.001 does
  not stop it). The scenario now also switches `PlayersOnly` on (only the player's controller ticks) for the
  first-person and lock-on shots.
- Lock-on: the reticle's ring of arcs turns, and over a lit scene the orange thresholds catch only ~500 pixels,
  so D-2's bounding-box method spread 14 px between sizes. Now each size is shot twice in the same frozen frame:
  with the level (the target's purple figure) and without (the reticle alone on black: centroid and mean
  radius of its pixels, which do not depend on the arcs' angle). Needs the rerun.
- The teleport camera effect (the FOV write-back path) did not run at spawn in DM (no `base=170` lines):
  implemented, never exercised so far.

Prepared: windows and wasm-perf configured offline (deps copied from main's build dirs into
`build-windows/_src`, `build-wasm-perf/_src`), not built yet (the game runs are timing-sensitive under load).

## Step 4 (2026-10-07): the post-processing run, the lock-on on black, the sniper rifle

- `fov-pp` (post-processing on; owner's pictures): with `PlayersOnly` the first-person weapon no longer moves
  between shots: the same 1440x1080 picture taken again later: area 0.4%, centroid 0.1 px; against it the 16:9
  and 21:9 weapon: area +0.5% / +0.5%, centroid 0.1 / 0.1 px; AUTOFOV 0: area -6.0%, centroid 52.6 px.
  Lock-on reticle on black: centre (612.3,669.1) at 1440x1080, (852.4-240, 668.2) at 1920x1080, (1171.6-560,
  668.8) at 2560x1080: within 1.0 px of the 4:3 one shifted by the side columns, radius 272.8 / 272.8 / 272.6.
  The figure's purple box is a coarse measure (DOF/bloom edges): offsets spread 7 px; the check now rests on
  the reticle's position (<= 2 px) and radius (<= 1%), with the figure inside it, the box spread <= 10 px as
  information. With post-processing the world centre checks give 98.1% / 95.6% (third person) and 97.2% /
  99.2% (first person, above the weapon): screen-space effects (bloom, depth of field) at the frame's edges;
  the measured run is `-nopostprocess`, as the pinned comparisons of M12/M13/D-2.
- Seen in the pictures (fov-pp): third person, letterbox (the CINEMATICS command), first person and zoom 40 at
  4:3/16:9/21:9 show the same vertical extent with more scene left and right; AUTOFOV 0 at 16:9 is the zoomed
  view the owner saw. The weapon and the HUD stay in the centred 4:3 area.
- The sniper rifle: a match started from a URL gives the player only the race weapon (NextWeapon x8 stayed on
  `UCWeap_Race_Nakhti`; the `[DefaultPlayer] EnergyWeapon` loadout applies through the arena/profile flow), and
  no cheat gives weapons (`AllWeapons` is commented out in CheatManager.uc). Its zoom is `DesiredFOV`/`FOVAngle`
  = `ZoomFOV[...]` (UCWeap_SniperRifle.uc:388-392), the same path as the measured "zoom 40". NOT exercised: its
  scope overlay at 16:9 - it is drawn in the 4:3 box (four quarter tiles of `ClipX/2 x ClipY/2`, D-2), so on a
  wide window the side columns would show the zoomed world unmasked, presumably; a D-2 follow-up if the owner
  sees it (a native rule in `UCanvas` or a script change to extend the mask).
