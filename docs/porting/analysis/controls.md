# Controls (M10): keyboard, mouse and SDL game controllers

Running notebook of milestone M10, appended after every step so a successor can continue. Worktree
`/home/ion/WorkUC2/engine-uc2-m10`, branch `m10-controls`. Run tree `/home/ion/WorkUC2/run/m10`, logs and
screenshots `/home/ion/WorkUC2/logs/m10/`. Every run goes through `/home/ion/WorkUC2/bin/capped`.

File:line references are to this tree (`Source/` = Epic's code with LF endings).

## Step 0 - reading the specification (2026-10-04)

### The Xbox pad, as the Xbox build delivers it

`Source/XboxDrv/Src/XboxViewport.cpp:16-42` (`GamepadButtonMap`) and `:455-476` (the poll loop): button
index `i` of the XInput state is sent as `GamepadButtonMap[i]`. Indices 0-7 are the eight *analog* buttons
(`bAnalogButtons[]`), 8-15 the bits of `wButtons`. The index order is the XDK's
(`reference/xdk/unpacked/2005-02-5849.15/XDK/xbox/include/Xbox.h:586-610`). The same order is the script's
`ControllerConfigurationBase.ButtonMap` (`Source/Engine/Classes/ControllerConfigurationBase.uc`, defaults) and
the GUI's `eXBoxControllerCode` (`Source/GUI/Src/GUIPrivate.h:27-50`), so three independent sources agree.

| Xbox control | XInput (Xbox.h) | Engine key | ButtonMap name (script) | GUI code | SDL_GameController |
|---|---|---|---|---|---|
| A | analog 0 `XINPUT_GAMEPAD_A` | `IK_Joy1` (200) | `AButton` | `XBC_A` | `BUTTON_A` |
| B | analog 1 | `IK_Joy2` | `BButton` | `XBC_B` | `BUTTON_B` |
| X | analog 2 | `IK_Joy3` | `XButton` | `XBC_X` | `BUTTON_X` |
| Y | analog 3 | `IK_Joy4` | `YButton` | `XBC_Y` | `BUTTON_Y` |
| Black | analog 4 | `IK_Joy5` | `BlackButton` | `XBC_Black` | `BUTTON_RIGHTSHOULDER` (convention, see below) |
| White | analog 5 | `IK_Joy6` | `WhiteButton` | `XBC_White` | `BUTTON_LEFTSHOULDER` (convention) |
| Left trigger | analog 6 | `IK_Joy7` | `LeftTrigger` | `XBC_LeftTrigger` | `AXIS_TRIGGERLEFT` thresholded |
| Right trigger | analog 7 | `IK_Joy8` | `RightTrigger` | `XBC_RightTrigger` | `AXIS_TRIGGERRIGHT` thresholded |
| D-pad up | bit 0 `0x0001` | `IK_Joy9` | `DPadUp` | `XBC_PadUp` | `BUTTON_DPAD_UP` |
| D-pad down | bit 1 | `IK_Joy10` | `DPadDown` | `XBC_PadDown` | `BUTTON_DPAD_DOWN` |
| D-pad left | bit 2 | `IK_Joy11` | `DPadLeft` | `XBC_PadLeft` | `BUTTON_DPAD_LEFT` |
| D-pad right | bit 3 | `IK_Joy12` | `DPadRight` | `XBC_PadRight` | `BUTTON_DPAD_RIGHT` |
| Start | bit 4 | `IK_Joy13` | `Start` | `XBC_Start` | `BUTTON_START` |
| Back | bit 5 | `IK_Joy14` | `Back` | `XBC_Back` | `BUTTON_BACK` |
| Left stick click | bit 6 | `IK_Joy15` | `LeftStickButton` | `XBC_LeftThumb` | `BUTTON_LEFTSTICK` |
| Right stick click | bit 7 | `IK_Joy16` | `RightStickButton` | `XBC_RightThumb` | `BUTTON_RIGHTSTICK` |
| Left stick X / Y | `sThumbLX/LY / 32768` | `IK_JoyX` / `IK_JoyY` (240/241), axis, up = + | `LeftStick` | `XBC_LeftStickX/Y` | `AXIS_LEFTX` / `-AXIS_LEFTY` |
| Right stick X / Y | `sThumbRX/RY / 32768` | `IK_JoyU` / `IK_JoyV` (232/233) | `RightStick` | `XBC_RightStickX/Y` | `AXIS_RIGHTX` / `-AXIS_RIGHTY` |
| pad inserted / removed | `XGetDeviceChanges` | `IK_JoySlider1` / `IK_JoySlider2` press+release | - | `XBC_CtrlInsert/Remove` | `SDL_CONTROLLERDEVICEADDED/REMOVED` |

Notes on the table:
- **Y axes**: XInput's thumb Y is positive *up*; SDL's is positive *down*, so SDL's value is negated.
- **Black/White -> shoulders**: there is no physical equivalent on a modern pad; Black/White are the two small
  buttons. Evidence for the side: the script's default layout names them `BlackButton=Next`, `WhiteButton=Prev`
  (`ControllerConfiguration.uc` defaults; next = right, previous = left, the UT shoulder convention) and every
  `KeyBinds.ini` layout keeps that pairing (Black = view/weapon "next", White = taunt/"prev"). xemu maps them the
  same way (Black = RB, White = LB). It is a convention, not something the Xbox code decides.
- **Threshold**: every one of the 16 buttons goes through the same edge test: pressed when the value rises above
  `XINPUT_GAMEPAD_MAX_CROSSTALK` (30 of 255, `Xbox.h:610`), released when it falls to 30 or below
  (`XboxViewport.cpp:464-476`). Digital buttons are 0/255, so in practice the threshold matters for the
  analog ones: A/B/X/Y/Black/White (analog on the Duke pad, digital on modern pads) and the two triggers.
  SDL triggers are 0..32767, so the Epic threshold is 30/255 of that.
- **Stick dead zone**: the Xbox driver applies *none*: sticks go out raw (`XboxViewport.cpp:452-489`). The dead
  zone is in the bindings: `DefUserXbox.ini:208-213` `DeadZone=0.3`, `ControllerConfiguration.ApplyJoysticks`
  `DeadZone=0.25` (applied when a profile is applied), evaluated by `UInput` (`UnIn.cpp:298-313`). The GUI has its
  own (`GUIController.JoyDeadZone=0.9`, `UnGUICore.cpp:898`). `UXboxClient::JoyDeadZone` (ini 0.35,
  `Default.ini:270`) is only stored by the `SETDEADZONE` exec (`XboxClient.cpp:424-440`, called from
  `GUIController.uc:1905/1969` and `UCPlayerController.uc:845/863`); nothing reads it.
- **Key repeat**: a held button repeats as `IST_Hold` after 0.2 s, then every 0.1 s (`XboxViewport.cpp:468-499`).
- **Routing**: a pad bound to a viewport (`UViewport::GamepadID`) is read in that viewport's `UpdateInput` with
  `Engine->ActiveControllerId` = pad, buttons through `Engine->InputEvent`, sticks through
  `InteractionMaster->MasterProcessJoyInput` (GUI) and, if not taken, `Input->DirectAxis`
  (`XboxViewport.cpp:440-503`). Unbound pads go to `InteractionMaster->MasterProcessKeyEvent` only; Start on an
  unbound pad binds it to a viewport without one or begins split screen
  (`XboxClient.cpp:834-944`). Hot-plug sends `IK_JoySlider1/2` press+release to the pad's viewport or the
  interaction master (`XboxClient.cpp:678-786`). `UXboxClient::Tick` restores `ActiveControllerId` afterwards
  (`:465-482`).
- **Rumble**: `PlayerController.InitInputSystem` creates a `WaveformManager`; script plays waveforms with
  `ClientPlayWaveform` (weapons, melee hits, shield, death). The driver polls
  `Actor->WaveformManager->GetRumbleState(dt, L, R)` every frame for the bound pad and writes the motor speeds
  (`XboxViewport.cpp:364-392`, `UnForceFeedbackWaveform.cpp:64-97`), and zeroes them on travel (`:330-351`).
  All of this except the final `XInputSetState` is platform-independent engine/script code that the PC build
  already runs: nothing in script needs to change for rumble.
- **Analog amount**: `UPlayer::GetAnalogButtonAmount` (`UnPlayer.h:61`) returns 255 on every non-Xbox viewport;
  `UCPlayerInput.GetMatchTypeValue` reads triggers/A/X through it for the timed input sequences
  (`UCPlayerController.uc:8090-8170`). On PC every analog button therefore reads as fully pressed (defect, see
  below).

### What the PC build does with the same keys

- `UInput` axis code (`UnIn.cpp:289-327`): `SpeedBase` axes (the pad's) add `SpeedBase*Speed` per call with no time
  factor; `UInput::ReadInput` then multiplies every axis by `20/DeltaSeconds` on PC and by 1 on the Xbox
  (`UnIn.cpp:468-500`). So on PC a stick's contribution is 20/dt times the Xbox one (1,200x at 60 fps): the stick
  saturates `UCPlayerInput`'s `aMouseX_Limit=100` clamp at any deflection past the dead zone, and how far it
  saturates depends on the frame rate. UT2004 (`engine-ut2004/Source/Engine/Src/UnIn.cpp:355-375`, "amb") fixed
  the same thing on PC by multiplying by `GetInputDelta()`; UC2's tree does not have that fix. **Defect in Epic's
  PC path**, to fix in M10 (step C1).
- `UCPlayerInput.PlayerInput` (`UCPlayerInput.uc:312-397`) treats `aMouseX/aMouseY` as a stick: clamp to
  `aMouseX_Limit` (100), square fall-off (`aTurn_FallOff=2`), time-based acceleration, then a turn *rate*
  `aTurn <= aTurn_Limit (1024)` -> at most 32*1024 = 32,768 rotation units/s = 180 deg/s. The mouse feeds the same
  variable (`MouseX=Count bXAxis | Axis aMouseX Speed=6.0`), hence the PC leak's sluggish, capped mouse look.
- GUI: `bEmulatedJoypad` turns letters into pad codes (`UnGUICore.cpp:858-889` `GetXCode`, `:1024-1067`): `A B X Y`
  -> face buttons, `K`/`W` -> Black/White, `[`/`]` triggers, arrows D-pad, Enter Start, Escape Back, `;`/`'`
  stick clicks, NumPad 8/6/2/4 right stick. Typing W or A into a text field therefore also navigates/accepts.
- Mouse in the GUI: `LookUnderCursor` only records `ActiveControl` (hover highlighting is `#if 0`,
  `UnGUICore.cpp:2246-2303`); a left click runs Epic's `MousePressed/MouseReleased` chain (component under the
  cursor gets focus, then its `OnClick` delegate); UC2's menus act on `XControllerEvent` (`XC_A`/`XC_Start`), so
  whether a click does anything depends on the component.

### Plan (scope C1-C4 of the brief)
C1 pads in SDL2Drv; C2 GUI key table + mouse; C3 match bindings + mouse look; C4 `tools/dev/m10-controls.sh`.

## Step 1 - C1: pads through SDL's GameController API (SDL2Drv)

New files `Source/SDL2Drv/Src/SDL2Pads.cpp` (the driver side) and `SDL2InputTest.{h,cpp}` (the test channel);
`SDL2Client.cpp`/`SDL2Viewport.cpp`/`SDL2Drv.h` wire them in. What it does, mapped to the Xbox code:

| Xbox (XboxDrv) | SDL2Drv |
|---|---|
| `XGetDeviceChanges`, four fixed ports (`UXboxClient::DetectDevices`) | `SDL_CONTROLLERDEVICEADDED/REMOVED` in `PumpEvents`; first free port of four (`PadAdded/PadRemoved`); `IK_JoySlider1/2` press+release to the pad's viewport or the interaction master, `ActiveControllerId` = port meanwhile |
| `XInputGetState` + 16-button edge test against `XINPUT_GAMEPAD_MAX_CROSSTALK` | `ReadPad` (state polled with `SDL_GameControllerGetButton/GetAxis`, mapped through `[SDL2Drv.PadMap]` to the 16 Xbox button positions 0..255 and four stick axes) + `SendButtonEdges` with `PadButtonThreshold` (default 30) |
| bound pad read in `UXboxViewport::UpdateInput` | `USDL2Client::UpdateBoundPad`, called from `USDL2Viewport::UpdateInput` only when `DeltaSeconds>0` (the player tick's `UViewport::ReadInput`; the launcher's per-frame pump passes 0) |
| sticks: `MasterProcessJoyInput` then `Input->DirectAxis` | same |
| repeat 0.2 s then 0.1 s as `IST_Hold` | same, `PadRepeatDelay`/`PadRepeatRate` |
| unbound pads -> `InteractionMaster->MasterProcessKeyEvent` | `UpdateUnboundPads` from `PollGamepads` (client tick) |
| rumble `WaveformManager->GetRumbleState` -> `XInputSetState` | same -> `SDL_GameControllerRumble(low=left, high=right, 1000 ms)`, sent on change and refreshed every 0.5 s while non-zero; `PadRumble`, `PadRumbleScale` |
| rumble zeroed in `SetClientTravel` | `USDL2Viewport::SetClientTravel` |
| `SETDEADZONE` stores `JoyDeadZone` | same (`PadExec`); also `PADS` lists the ports |
| `UPlayer::GetAnalogButtonAmount` = 255 on PC | `USDL2Viewport::GetAnalogButtonAmount`: bound pad's analog value, or 255 while a keyboard/mouse key is held whose binding equals that pad button's binding |

Deviation from the Xbox, on purpose: when a pad is removed, the buttons it held are released first (the Xbox only
cleared its table, leaving `UInput` keys down until a `ResetInput`).

Not done: Start on an unbound pad does not call `UClient::BeginSplitScreen` (split screen is an owner question).
For split screen the SDL client would need: one SDL window per viewport or one window with sub-viewports
(`UXboxViewport::IsFullscreen` compares with the initial size), `AllowControllerJoin`/`BeginSplitScreen` from
`UpdateUnboundPads` on Start, and `UXboxClient::Tick`'s "blit only the last viewport".

Settings (`[SDL2Drv.SDL2Client]`, compiled defaults identical): `UseJoystick=True` (was the WinDrv copy's False),
`PadButtonThreshold=30`, `PadStickDeadZone=0`, `PadRepeatDelay=0.2`, `PadRepeatRate=0.1`, `PadRumble=True`,
`PadRumbleScale=1`, `JoyDeadZone=0.35`, `LogInput=False`. New section `[SDL2Drv.PadMap]` (SDL control name =
`Joy1..16` or `JoyX/Y/U/V` [`Invert`]); the compiled default is the same table, used for any control the section
does not name.

Log name `SDL2Input` (quiet: only with `LogInput=True`, `-inputlog` or `-inputtest=`). Found while doing it: a
name made at run time loses its slot when the garbage collector purges unreferenced names (`UnObj.cpp`
`RemoveNames`), and the lines then came out as `Question:`; `SetFlags(RF_Native)` keeps it.

Test channel `-inputtest=<file>` (`SDL2InputTest.h` lists the commands): a harness appends lines; `pad attach`
creates a virtual game controller with `SDL_JoystickAttachVirtualEx` (type GAMECONTROLLER, button/axis masks so
SDL's own virtual mapping makes joystick button i = controller button i), `pad button/axis` set its state,
`probe <tag>` logs level, menu page, focused control and its text, pause, pawn location, controller rotation,
weapon, ammo, GUI mouse position. The virtual device's `Rumble` callback logs the request SDL delivers.

First run (scratch, `bin/capped 2G 60 -- xvfb-run ...`, menu level): attach -> `SDL2 pads: pad 0 connected: UC2 M10
virtual pad (rumble)`, `pad 0 JoySlider1 press (bound)`; D-pad down -> `pad 0 Joy10 press (bound)` ->
`UC2MainMenu->ItemSelectionPanel handled XC_PadDown for id 0`; left trigger 3700 (= 29/255) -> nothing, 4200
(= 33/255) -> `pad 0 button 6 value 33 > 30`, `Joy7 press`, `UC2MainMenu handled XC_LeftTrigger`; A -> `GUILabelButton
handled XC_A` -> `OpenMenu [UCMenu.UC2Profile]`; detach -> `JoySlider2`, `pad 0 disconnected`; re-attach -> connected
again. A 0.5 s hold gives one press and two repeats (the 0.2/0.1 s Xbox repeat), so tests hold buttons ~0.15 s.

## Step 2 - C1: the PC stick scaling defect (Engine, `UnIn.cpp`)

`UInput::ReadInput` multiplies every axis by `20/DeltaSeconds` on PC (`#else` branch; 1 on the Xbox). Mouse
and key axes are deltas and need that; a stick's `SpeedBase` contribution (DirectAxis, `IST_Hold`, once per
tick) is a position. So on PC the stick was 20/dt times its Xbox strength and frame-rate dependent: at 60 fps
any deflection past the dead zone saturated `UCPlayerInput`'s `aMouseX_Limit=100` (look) and moved at full
speed (walk). Epic's PC joystick (WinDrv DirectInput, off by default) had the same problem; UT2004's tree fixed
the same lines differently ("amb": multiply by the delta, PC values retuned). Fix, non-Xbox only: in the
`SpeedBase` branch, when the action is `IST_Hold` with a positive delta, add `SpeedBase*Speed*Invert*dt/20`,
so after `ReadInput` the axis holds exactly the Xbox value. Nothing else uses `SpeedBase` in our bindings.
Verified in the pad scenario (step C4): a half and a full stick give proportionate movement.

## Step 3 - C2: the GUI key table, mouse clicks, hover, typing into on-screen keyboards (GUI, `UnGUICore.cpp`)

- `[GUI.EmulatedJoypad]` (system ini) replaces the hard-wired table in `GetXCode` and `NativeKeyEvent`:
  `<engine key>=<pad code>[/<pad code while the active page has a text field>]`, codes `A B X Y Black White
  LeftTrigger RightTrigger PadUp PadDown PadLeft PadRight Start Back LeftThumb RightThumb AltUp AltDown AltLeft
  AltRight Type Raw`. No section = Epic's table (kept verbatim in `LoadEmulatedPad`), so an Epic or community
  ini behaves as before. Loaded once, on the first menu key.
- "Text field on the page" = the active page has a visible, enabled, writable `GUIEditBox` (`TextEntryTarget`).
  Then keys whose second code is `Type`, and any other key that makes a character, type: on an on-screen
  keyboard page (the field never has the focus, its key buttons do) the characters go to the field from
  `NativeKeyType`, Backspace/Delete/Home/End to its `NativeKeyEvent`, and nothing else on the page sees them.
- Left click: Epic's `MousePressed/MouseReleased` already give the item under the cursor the focus and call its
  `OnClick` (that is how `GUIButton`/`GUILabelButton`, e.g. the main menu, work). Added: on press, a list selects
  the row under the cursor (its own `InternalOnClick`, found by name; UC2 pages replace a list's `OnClick` with
  their accept handler, which then used the previously selected row); on release over the same item, an item
  with no `OnClick` but an `XControllerEvent` handler gets `XC_A`. Right click, wheel: table entries
  (`RightMouse=B`, `MouseWheelUp/Down=PadUp/PadDown`).
- Hover: `LookUnderCursor` (Epic's hover code is `#if 0`) gives the focus to a tab-stop item under the cursor
  when the mouse actually moved, so a resting cursor never takes the focus back from keys or pad.
- DevUI log lines for the test: `Mouse hover selects <path>`, `Mouse click selects the row under the cursor in
  <path>`, `Mouse click is A on <path>`.

Default table (Default.ini) and why:

| Keys | Pad code | Why |
|---|---|---|
| arrows | D-pad | always; Epic's |
| W A S D | D-pad / Type | navigation like the game's movement keys, but they type on pages with a text field |
| Enter | A / Start | accept; on a text page Start = "done" (UC2KeyboardPanel/UC2PageBase `StartClicked`) |
| Space | A / Type | accept; types a space in text |
| Escape | B | back, always (cancels text entry too) |
| Backspace | B / Type | back; deletes in text |
| right mouse button | B | back |
| X, Y | X, Y / Type | the letters the on-screen glyphs show |
| Q, E | White, Black / Type | left/right of WASD, as the shoulders they replace |
| PageUp, PageDown | LeftTrigger, RightTrigger | always (no character) |
| `[` `]` | LeftTrigger, RightTrigger / Type | Epic's |
| Tab | Start | 15 files act on `XC_Start` separately from A (`UC2PageBase.StartClicked`) |
| F1 | Back | `XC_Back` is the scoreboard button in game, F1 is `ShowScores` |
| Home, End | LeftThumb, RightThumb / Type | stick clicks (`XC_RightThumb` is used in 7 menu files) |
| NumPad 8 2 4 6 | right stick | Epic's |
| wheel | PadUp/PadDown | scrolls lists by moving the selection |
| Delete | Raw / Type | edits text |

Every pad code the menus act on (count of uses in UCMenu+GUI script: A 118, X 41, White 36, B 29, Y 28, PadLeft/Right
27, Black 22, Start 18, RightThumb 16, triggers 15, Back 12, PadUp/Down 10-11, Alt* 9-12, LeftThumb 3) has a key.

### Defect found on the way: `Delete` as a system-ini key stopped the engine (Core)

With `Delete=...` in `[GUI.EmulatedJoypad]` both `uc2.bin` and `ucc-bin` stopped at startup with
`Can't find 'intUObjectexecDelete' in 'Core.dll'`. The system ini is read before the script packages; its keys
are FNames; an FName keeps the spelling of its first creator; UObject's script function is `delete`, and the
static-link native lookup (`UnCoreNative.h`, `IMPLEMENT_NATIVE_HANDLER`) compared `"int"+class+"exec"+name` with
`appStrcmp`. Fix: `appStricmp` (all 788 native names are distinct without case; checked with
`grep MAP_NATIVE ... | sort | uniq -di` = empty). Epic's own `DefUser.ini` has `Delete=LogInput`, but User.ini is
read after the packages load, which is why nobody saw it.

## Step 4 - C3 (first part) and C4: PC bindings, and the keyboard scenario passes

`Engine/Config/DefUser.ini` `[Engine.Input]` (Epic's PC file = the community `User.ini`, identical; rows in
`Engine/Config/README.md`): RightMouse gets the pad's left trigger command `AltFire | Shield 1 | onrelease
Shield 0` (Epic's PC binding was `AltFire` only: no block/reflect on PC); MiddleMouse = lock-on (`Lock 1 |
onrelease Lock 0`, the pad's right-stick click; Epic: `Zoom`, a UT2004 leftover with no UC2 meaning found); E =
melee (pad B), Q = back to ranged (pad Y; Epic: `AllAmmo`, a cheat next to WASD), C = view 1P/3P (pad Black),
V = taunt/voice (pad White; Epic: `CallForHelp`), Ctrl = `SmartJump 1` (pad left-stick click), Tab = scoreboard
(pad Back; T stays chat), Left/Right arrows turn and PageUp/PageDown look (`Axis aMouseX/aMouseY Speed=2.5`:
Epic's arrows strafed, and Epic's `LookUp/LookDown` aliases write `aLookUp`, which `UCPlayerInput` overwrites, so
they never worked); debug/editor keys removed (P PlayersOnly, O ShowDebug, U, I d3d, M actor select, NumPad 0-6
camera hacks, the ACTOR ALIGN on Home/End); Joy1..16/JoyX..V/JoySlider = KeyBinds.ini Layout_0 (so a pad works
before a profile applies its layout; Epic's `DefUserXbox.ini` had Joy11/Joy12 swapped against the XInput bit
order, `ControllerConfigurationBase.ButtonMap` and `GUIPrivate.h`, fixed here). Mouse:
`MouseX/MouseY ... Speed=1.0` (was 6.0); `[UCGameplay.UCPlayerInput]` linear (`bNonlinearAccel=false`,
`bAccelMultipliers=false`, limits 1,000,000 and 10,240,000 = the Xbox's 1024/100 ratio); `bAutoCenterPitch=false`.
Why and the numbers: step 5.

Test harness (C4): `tools/dev/m10-controls.sh` (entry; one capped Xvfb run per scenario, fresh inis, DevUI
unsuppressed in the run tree's copy only) + `tools/dev/m10-inject.py` (XTEST keys/mouse, the engine test channel,
waits on log lines, probe checks). The probe has `shots=` (`AWeapon::FlashCount`, +1 per shot): the race weapon a
player starts with has unlimited ammo (`UCWeapon.GetAmmoAmount` returns 999 for `AC_Race`), so ammo cannot show a
shot; `item=` shows a list's selected row (`Elements[Index].Item|ExtraStrData`).

Menu path found by running: main menu Down (Instant Action) A -> `UC2Arena_Main` + "no profile" box (B = no) ->
gametype list (seek `DeathMatch`) A -> rules (`CUSTOMIZE NO`) A -> map list (seek `DM-Deadbolt`) A ->
`UC2Arena_Character` A -> `UC2Arena_Armory` A (explosive) A (energy) -> `ConfirmLaunchMenu` A -> travel; in the
level Fire spawns the player. NextWeapon does not cycle ranged weapons at spawn (the explosive and energy weapons
start without ammo, `UCPlayerController.NextWeapon` skips them), so the weapon test is melee (E) <-> ranged (Q).

`tools/dev/m10-controls.sh keyboard` -> `PASS keyboard (16 checks, 0 Critical, capped exit 0)`: menu path (8
log checks), spawned, W forward 600 units along the facing, Right arrow +13,018 yaw in 1 s, E melee
(`UCWeap_Melee_Anubis`), Q back to `UCWeap_Race_Nakhti`, Alt fired (shots 0 -> 2), Escape opens
`InGame_MainMenu` (paused=1), Escape closes it.

## Step 5 - C2/C4: mouse and text scenarios pass

- `mouse` -> PASS, 18 checks. Hover over OPTIONS logs `Mouse hover selects ...` and Enter then opens
  `UC2Settings_Main` (hover moved the focus); right click answers the "no profile" box with B and leaves the page;
  a click on INSTANT ACTION while OPTIONS has the focus opens `UC2Arena_Main` and not Settings; a click on the
  DeathMatch row while CTF is selected selects that row and accepts it (the focus moves on to the rules; Escape
  back shows `DeathMatch` selected); wheel down x2 moves the map list (Acid Rain -> Ascension), up x2 back; left
  click starts the match and fires (shots 0 -> 1); mouse look: +200 px -> +10,480 yaw (52.4 units/px; linear:
  -200 px returns to exactly the start yaw), 100 px up -> pitch +5,240, `set UCPlayerInput bInvertMouse True` ->
  the same motion lowers the pitch.
- `text` -> PASS, 7 checks: Profiles -> "create?" box (A) -> `UC2ProfileKeyboard` with "Player" preselected;
  typing `WaSdKxYb` on the real keyboard puts exactly `WaSdKxYb` into the field (probe reads the page's
  GUIEditBox), no `handled XC_*` line while typing, Backspace deletes one, Enter (= Start on a text page) closes
  the keyboard page (UC2KeyboardPanel's done). Screenshot `logs/m10/text-typed.png` shows the name in the field.
  The first attempt delivered characters through `UGUIComponent::NativeKeyType`, which refuses because the
  field has `bAcceptsInput` false (it is display-only for focus purposes); now its `OnKeyType` delegate is called,
  the same handler `UC2KeyboardPanel.KeyboardOnClick` calls for its own key buttons.

Sensitivity numbers (mouse look; "feels right" cannot be judged here): yaw per pixel = 6.4 x binding `Speed` x
(`aTurn_Limit`/`aMouseX_Limit` = 10.24) x `MouseSensitivity_X` x FOV/90, x `[SDL2Drv.SDL2Client] MouseXMultiplier`;
with our defaults 65.5 x FOV/90 units/px (measured 52.4 at the default FOV: 0.29 deg/px; 16 cm per 360 deg at
800 dpi). Change it with `MouseSensitivity_X/Y` (`[UCGameplay.UCPlayerInput]`, User.ini) or the bindings'
`Speed=`. Keyboard turn (arrows): `Speed=2.5` -> 13,018 units/s measured (72 deg/s).
