# D-9 notebook: rebinding the keyboard and mouse in game, and the mouse slider

Worktree `/home/ion/WorkUC2/engine-uc2-d9`, branch `d9-rebind` (from main 9ec4326). Run trees `<WorkUC2>/run/d9*`,
logs `<WorkUC2>/logs/d9/`. Every run goes through `<WorkUC2>/bin/capped` (CLAUDE.md). Appended after every step.

Owner (addendum, PLAN.md D-9): "We need to have an extra IN GAME option to rebind controls. This is the most
absolute last of them all." Also from D-6 (mouse.md "Not done"): a mouse sensitivity slider and a mouse invert
option in the menus.

## Step 0 (2026-10-08): reading — where bindings live, how the menus take keys, what the page can reuse

### Where a binding lives and how it persists (file:line in this tree)
- `UInput` (`Source/Engine/Inc/UnIn.h:42-50`, `StaticConfigName` "User", `CLASS_Config`): `Bindings[IK_MAX]` is
  one config `FString` property per key, created at start-up and named after `EInputKey` without `IK_`
  (`Source/Engine/Src/UnIn.cpp:72-86`), loaded from User.ini `[Engine.Input]` into the class default, which every
  viewport's `UInput` copies. So a binding is the User.ini key `[Engine.Input] <Key>=<command>`.
- Script reaches the live object: `PlayerController.GetRawInputObject()` (`Engine/Classes/PlayerController.uc:422`)
  and `Object.GetPropertyText/SetPropertyText` (what `ControllerConfiguration.Apply` does for the pad layouts,
  `Engine/Classes/ControllerConfiguration.uc:27-50`: it writes ONLY `Joy*`/`JoyX..V`, never a keyboard key).
- Console `SET Input <Key> <command...>` (`UObject::StaticExec`, `Core/Src/UnObj.cpp:3012`, `GlobalSetProperty`
  :2059) sets every live UInput and the class default; the value is the rest of the line, `|` included
  (`UStrProperty::ImportText` takes the whole buffer when not delimited, `UnProp.cpp:1476`). It does not save
  (only under `GIsEditor`).
- Saving: the saveconfig work's `SAVECONFIGKEYS <Class> <Prop>...` (`Core/Src/UnObj.cpp:2762`) writes exactly the
  named config properties of the class default with `UObject::SaveConfig` (file = class config name, section =
  class path: User.ini `[Engine.Input]`), flushes, honours `[Engine.Engine] ScriptSaveConfig`. Works for
  `Engine.Input` and its per-key properties (`FindField` by key name). On the web User.ini is player data (C-2).
- `UC2InputMigrate.cpp` (D-6) rewrites a User.ini key only when its value is EXACTLY one of a fixed list of
  old values (Epic's/M10's mouse axes, M10's look keys and sticks, `NextSkill`/`PrevSkill` on the wheel). Every
  command the page writes is one of today's DefUser.ini commands, none of which is on that list, so the
  migration cannot undo a rebinding.
- The profile (`AVARIS_PROFILE.PRO`, binary) stores the pad layout index and the pad's sliders; nothing of the
  keyboard. Keyboard bindings and the mouse settings stay in User.ini (mouse: saveconfig.md "the ini owns what the
  profile has no field for").
- Defaults: `Engine/Config/DefUser.ini` is in every run tree's System/ (`mkrun-uc2.sh` copies it) and in the
  browser as `/System/DefUser.ini` (`Engine/Config/web/DefUser.ini`, identical, `sync-web-ini.py`). Script reads
  any ini with `Object.GetConfigValue(File, Section, Key, out Value)` (`Core/Classes/Object.uc:1042`,
  `UnCorSc.cpp:5891`), so "reset to defaults" can read our DefUser.ini at run time.

### How the menus take keys (M10, D-5)
- `UGUIController::NativeKeyEvent` (`GUI/Src/UnGUICore.cpp:1055`): BEFORE the `[GUI.EmulatedJoypad]` table turns
  keys into pad codes, a set `OnNeedRawKeyPress` delegate (`GUI/Classes/GUIController.uc:641`, Epic's "for
  KeyConfigs", UT2004's binder uses it) receives the RAW key on its release and everything else is swallowed
  (:1149-1161). So a capture sees Escape, Backspace, letters, mouse buttons (LeftMouse..MiddleMouse, Mouse4/5),
  the wheel (SDL2Viewport sends press+release per notch, `SDL2Viewport.cpp:916-929`), pad buttons as IK_Joy*.
- Problem for UC2: the menus act on the PRESS (XC_A from Enter's press), so the release of the Enter that started
  a capture would be captured as the new key. Needed: deliver a release only when its press was seen while
  capturing (GUI C++, file-static, no layout change).
- UC2 pages save on close: `UC2PageBase.Closed` -> `SaveSettings` if `bDirty` (`UCMenu/Classes/UC2PageBase.uc:178`);
  settings pages: Black = cancel (reload), White = defaults (`UC2Settings_PageBase.uc`), B = back.

### Where the page goes
- Options from the main menu = `UC2Settings_Main`; Options from the pause menu = `UC2Settings_MidGame`, a
  subclass of `UC2Settings_Main` (`InGame_MainMenu.uc:302`). One button added in `UC2Settings_Main.InitOptions`
  reaches both. Shown only when `!Level.IsXBox()`.
- Look: `UC2Settings_Controller` / `UC2Settings_Gameplay` are an `ItemSelectionPanel` of `moSlider`/`moCheckbox`
  items; Gameplay has a list in it (`WeaponPriorityPanel`, a `Settings_ListPanel` with an `AvarisListBox`). The
  key list follows that pattern; its rows are drawn by the list's `OnDrawItem` delegate (`GUIListBase.uc:71`,
  called by `UGUIVertList::Draw`, `UnGUILists.cpp:317`) with the list item style (`STY_ListBoxItem`), three
  columns (action, key 1, key 2).
- `GUISlider` has no numeric readout and no mouse handling (pad left/right only): the page shows the value in the
  slider's caption and handles a click on the slider itself (`OnClick`, set by the page; no GUI.u change).

### The PC commands (DefUser.ini `[Engine.Input]`, each looked up in script)
Real: `MoveForward/MoveBackward/StrafeLeft/StrafeRight` (aliases), `Axis aTurn/aLookUp Speed=+-16` (look keys,
D-6), `CenterView` (alias `Button bSnapLevel`, read by `UCPlayerInput.uc:416`), `SmartJump 0/1`
(`UCPlayerController.uc:4250`: jump, or dodge when a movement key/stick is held; `1` = the pad's stick click: no
Nimble/Speed combo, can be disabled), `Jump`, `Fire`, `AltFire | Shield 1 | onrelease Shield 0`,
`MeleeWeapon | MeleeHeavyAttack | MeleePounceAttack`, `MeleeFinishAttack | NextWeapon`, `NextWeapon`
(`PrevWeapon` is empty in UC2), `Lock 1 | onrelease Lock 0`, `button bCombo | ComboPressed`,
`BlackButton | ToggleThirdCam` (Black = next scoreboard page), `WhiteButton | VoiceMenuNext` (White = previous
scoreboard page; opens/selects the voice menu), `Use`, `ShowScores`, `ShowMenu`, `Talk`, `TeamTalk`
(`Console.uc:40-47`), `shot`; UC2's direct weapon commands `RaceWeapon/EnergyWeapon/ExplosiveWeapon/NextRifle`
(the alternative pad layouts use them, no key has them); voice menu navigation `VoiceMenuUp/VoiceMenuDown` (only
on the D-pad today). Dead or not UC2's: `Strafe`/`LookToggle` (UCPlayerInput never reads bStrafe/bLook),
`Walking`, `Grab`, `Taunt Taunt1`, `ForceReload`, `ShowServerInfo`, `ShowObjectives`, the HUD size keys,
`InventoryNext/Previous`; Epic's `SwitchWeapon N` on 0-9 bypasses UC2's weapon rules (left as it is).

### Plan
1. GUI C++: a capture gets only keys pressed while it runs (the release of the key that started it is dropped).
2. UCMenu: `UC2Settings_Main` gets "KEYBOARD / MOUSE" (PC only) -> new `UC2Settings_Keys`: mouse sensitivity slider
   with a readout (scale and degrees per count), invert mouse, and a `KeyBindingPanel` list of the actions with
   two key columns; A/Enter/click = capture into the selected column (Left/Right pick the column); in a capture
   Escape cancels, Backspace/Delete clear, a pad button cancels; a key used by another action is swapped (that
   action gets the slot's old key, or none) and the hint line says so; White = DefUser.ini's PC bindings, Black =
   cancel, B = save and back. Saving: `SET Input <Key> <cmd>` for the changed keys, then `SAVECONFIGKEYS
   Engine.Input <keys>`; mouse through the D-6 console commands `MouseLookSens`/`MouseLookInvert` (already saved
   by SAVECONFIGKEYS). Pad keys (`Joy*`) are never listed, read or written.
3. Test `tools/dev/d9-rebind.sh` on the m10 harness.
</content>
</invoke>
