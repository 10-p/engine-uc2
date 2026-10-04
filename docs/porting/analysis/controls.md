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
