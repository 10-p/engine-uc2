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

## Step 1 (2026-10-08): the page works natively from the main menu and the pause menu (commits 4567556..3219806)

Builds: `bin/capped 6G 5400 -- cmake --build build-native -j4` from scratch (`logs/d9/build-native-0.log`), then
incremental (`-1`, `-2`), 0 errors. Scripts: `tools/dev/script-build.sh --tree d9-make` -> `Success - 0 error(s), 85
warning(s)` (M14's count); GUI.u and UCGameplay.u come out byte-identical to the asset tree's (D-4/D-6 rebuilt ones),
only UCMenu.u differs. Run tree `run/d9` = `mkrun-uc2.sh build-native d9` with `UC2_SCRIPTS=run/d9-scripts` (UCMenu.u
only), so it is exactly "today's asset tree + the new UCMenu.u + this engine".

What was done:
- GUI C++ (`UnGUICore.cpp`, 4567556): the raw capture gets a key only if it was freshly pressed while the capture ran
  (`GRawCapturePressed`, `GRawKeyHeld` against SDL's auto-repeat presses). Seen in the log:
  `Raw key capture: ignored the release of Enter (pressed before the capture began)`, then `Raw key capture: J`.
- UCMenu: `UC2Settings_Keys` + `KeyBindingPanel`, the button on `UC2Settings_Main` (`!Level.IsXBox()`).
  Design decisions, each from what was seen:
  - The page edits a copy (Work) of every keyboard/mouse key's binding, read from the live input object
    (`GetRawInputObject().GetPropertyText(<Key>)`); keys = every `EInputKey` name except the pad (`Joy*`, `JoyX..R`,
    `JoySlider*`), the mouse axes and `Unknown*`. A key belongs to the row whose command it runs (compared without
    spaces, any case). Leaving the page (B / Escape, `UC2PageBase.Closed` -> `SaveSettings`) writes the changed keys:
    `SET Input <Key> <cmd>` (every viewport + the class default) and `SAVECONFIGKEYS Engine.Input <Key>...` (16 a
    command). Black = cancel (reload from the live object), White = DefUser.ini's PC bindings
    (`GetConfigValue("DefUser","Engine.Input",<Key>)`), both undoable until the page is left, as on UC2's other
    settings pages.
  - Two key columns per action (Left/Right pick one, the heading turns orange); more keys than two show as "+n" in
    key 2 (NEXT WEAPON has four by default: the wheel both ways, N, /). Cells the player set keep what was put there
    while the page is open (`Pin1/Pin2`); otherwise keys are ordered by the action's `Order` list, then letters,
    mouse buttons, digits. (Not persisted: after a restart the order is recomputed; the bindings are the same.)
  - Capture: Enter/A or a click on a cell; Escape cancels ("NOT CHANGED"), Backspace/Delete clear, a pad button cancels
    ("PAD BUTTONS ARE SET ON THE CONTROLLER PAGE"), the console key is refused. Outside a capture, X (pad X / key X) and
    Delete clear the selected cell.
  - Conflicts: the key moves to the new action; the action that had it gets the cell's old key (swap), or nothing if the
    cell was empty; a key whose command no row lists (Epic's `SwitchWeapon N`, `Taunt Taunt1` on J, the gamma keys) is
    simply taken, and the bottom line says what it was (`J: JUMP / DODGE. (IT WAS: Taunt Taunt1)`).
  - Mouse: slider 0.1-10 step 0.1 with the value in its caption and the degrees per count on the bottom line
    (0.022 x value, UCPlayerInput.AddMouseLook); a click on its ends steps, on the bar sets the value under the cursor
    (GUISlider has no mouse handling); invert checkbox. Saved like the D-6 console commands (the live UCPlayerInput, the
    class default, `SAVECONFIGKEYS UCGameplay.UCPlayerInput ...`).
- Test channel: `keys <tag> <Key>...` (the live bindings), `probe4` (the focused caption); `m10-inject.py` reads both.

Seen (exploration runs, `logs/d9/explore/explore2..3`, `match1..2`): the button between CONTROLLER and GAMEPLAY on
OPTIONS; the page in UC2's look (pictures `explore3-*.png`); J bound to MOVE FORWARD key 1 replacing `Taunt Taunt1`; a
swap (C to MOVE BACK key 2: 1ST / 3RD PERSON moved to DOWN ARROW); Escape in a capture keeps the page open and changes
nothing; in a match (null renderer, DM-Deadbolt): Space jumps (z -34.1 -> 40.2); pause menu -> OPTIONS (a "no profile"
box first, as from the main menu) -> KEYBOARD / MOUSE -> JUMP / DODGE key 1 := J -> Escape: `SaveConfigKeys: Input.Space=
written`, `Input.j=SmartJump 0 written`; back in the game J jumps (z -34.2 -> 41.6). (The engine writes the key as `j`:
an FName keeps the spelling of its first use; config keys are case-insensitive, so this is cosmetic.)

Next: the committed test `tools/dev/d9-rebind.sh`.
