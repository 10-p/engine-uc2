# H-5 notebook: key glyphs for keyboard players

Worktree `/home/ion/WorkUC2/engine-uc2-h5-glyphs`, branch `h5-glyphs` (from main 7ceeee7). Run trees
`<WorkUC2>/run/h5*`, logs `<WorkUC2>/logs/h5/`. Every run goes through `<WorkUC2>/bin/capped` (CLAUDE.md). Appended
after every step.

Owner (2026-10-11): menu options show Xbox pad buttons; for player one (only player one), when the keyboard or the
mouse is touched, the icons turn into THE CURRENTLY BOUND KEY. Example: the character page of INSTANT ACTION (A
select, Y profiles, White more info, Black help, B back) and the in-game adrenaline menu (the three combo
buttons). Phase 1: the key as text in the game's own font. Phase 2: key bitmaps (a glyph sheet in a `.u`, `.utx`
only if impossible) so the mouse buttons, Esc or Enter show nicely.

## Step 0 (2026-10-11): reading and the plan

### How a button glyph is drawn today (file:line in this tree)
- One font for every pad glyph: `T_Fonts2.Controller_Buttons_LG` (`Canvas.XBoxButtonFontName`, `UCHUD.ButtonFontName`,
  `GUI.fntXboxSymbols`, `StartMenu`/`InGame_MainMenu`). 256x256 DXT3 page, decoded with Pillow:
  `logs/h5/font/Controller_Buttons_LG_PageA.png` (+ `buttons_view.png`, 3x on blue). The font is REMAPPED
  (`IsRemapped`, 50 `CharRemap` entries: `a`->`A`, `x`->`X`, `y`->`Y`; every other code -> the null character):
  A B X Y (coloured gems), `b`/`c` black/white balls, `l`/`r` trigger pills (wider: ~38x27 texels against ~22x27),
  `L`/`R` sticks, `<`/`>` Back/Start, `1 3 5 7` (and `2 4 6 8`) arrows, the localised letters `D G I d g i o p { } [ ]`
  and the Korean/Italian ones.
- Which character is which button is LOCALISED: `GUIController.XBoxButtonChars[eXControllerCodes]`
  (`GUI.int`: A B X Y b c l r 7 3 1 5 > < L R; German uses `G`/`D` for the sticks and `g`/`d` for the triggers,
  French `{ } [ ]`, Italian `I i d D`). The HUD's own table `UCHUD.XboxButtons` (not localised) uses A B X Y l r and
  `1 3 5 7` for the left stick directions.
- In a string a glyph is `0x02` + the character (`GUI.XboxButtonCode`); `UCanvas::XBoxClippedStrLen`
  (`UnCanvas.cpp:2028`), `DrawXboxTextJustified` (:2120), `WrapStringToArray`'s `GetCharWidthXBox` (:608) switch to
  `XBoxButtonFont` for the character after it. The HUD sets `Canvas.Font = XBoxButtonFont` and draws button
  characters directly (`UCHUD.DrawRandomSequence`: "X,Y,..."), `DrawTextDualFont` draws `%`-words with the
  secondary font (`UnCanvas.cpp:2421`; training texts, pickup, awaken), and the native combo menu
  `AUCHUD::DrawComboMenu` (`Source/UCGameplay/Src/UCHUD.cpp:865`) prints `l`, `A`, `r` with `ButtonFont`.
- **Every one of them ends in `DrawString(Canvas, Font, ...)` (`UnCanvas.cpp:392`) with Font = the button font, and
  measures with `UFont::GetCharSize` (`UnFont.cpp:111`).** That is the one place to substitute.
- Who draws is already known per draw: `pCanvasUtil->UIContext` = `UICONTEXT_Menu` while the GUI controller draws
  (`UnGUICore.cpp:1664`), `UICONTEXT_Game` for the HUD and script (D-2, `UnRenderUtil.h:257`).

### Inventory: where player one sees a pad glyph
Menus (UICONTEXT_Menu):
- The button bar of every page: `ButtonPanel`/`ControllerButton` (a 23x23 `GUILabel` with style XBoxButton whose
  caption is `GetXBoxCharacter(code)`, the caption beside it at a fixed `CaptionX`). Character page
  (`UC2Arena_Character`, `UC2SP_CharacterPanel`, `UC2MidGame_Character`): A, Y, White, Black, B.
- Hints, extended help, message boxes (`%a%` -> `0x02A`): 37 decompiled classes of `Scripts/base` carry `0x02`
  bytes (UCMenu 35, GUI 2), `UCMenu.int` 108 and `Engine.int` 17 glyphs (A 37, B 26, X 21, Y 10, 1/5 7 each, c 6,
  l 4, r 3, > 3, L 1). Per class: Arena_PanelMain 1 5 A B X Y; Arena_PanelMaplist A B Y; Arena_PanelServerMain 1 5 X;
  ControllerRemovedMenu >; RungStatsPanel c; StandbyWaitMenu B; UC2AdminMenu B; UC2Arena_Armory A B Y;
  UC2Arena_BotConfig A B l r X Y; UC2Arena_Character A c Y; UC2Browser A B c Y; UC2CheatMenu B;
  UC2ControllerLayoutMenu B X; UC2Extras A B; UC2Keyboard A; UC2LiveBrowser A B c X Y; UC2LiveMenu(Basic) A B Y;
  UC2MidGame_* A B; UC2NetworkMain B Y; UC2Optimatch > A; UC2ProfileKeyboard A; UC2QuickMatch B; UC2SP_* A B c;
  UC2Stat_Detail A B L; UC2Stat_Players A X; WeaponPriorityPanel X Y.
- The combo instructions on the character pages: `PlayerComboPanel.lbl_Instruct` = `CharacterInformation.
  ComboKeyPressInfo` ("X,l" "X,A" "X,r" "X,X,l" ...): these name IN-GAME buttons inside a menu.
HUD (UICONTEXT_Game):
- the adrenaline menu (native `DrawComboMenu`: `l` = combo 0 / 3, `A` = 1 / 4, `r` = 2 / 5; the game fires them from
  `AltFire`, `SmartJump 0`, `Fire`: `UCPlayerController.DelayedTryCombo`, `SmartJump`);
- the coup de grace sequence (`DrawRandomSequence`: X, Y then stick directions), the stun text `AwakenText`
  ("Wiggle %L and hit %A"), the pickup text ("%Y TO PICKUP"), spectator "PRESS %b TO LOCK VIEW", the training
  texts (`UCHUD_Training`, `%A %B %X %Y %b %l %r %L %R`), the voice menu's blinking `5` arrow (`Element_VoiceMenu`).

### Button -> action -> key
| glyph | pad button | in a menu (UICONTEXT_Menu): `[GUI.EmulatedJoypad]`, first key in the section with that code | in the game: the pad key's live binding (`Joy<n>`) -> the keyboard/mouse key bound to the same command |
|---|---|---|---|
| A | XBC_A / Joy1 | Enter (Space; text page: Space) | `SmartJump 0` -> Space |
| B | XBC_B / Joy2 | Escape (Backspace, RightMouse) | `MeleeWeapon | ...` -> E (B) |
| X | XBC_X / Joy3 | X | `button bCombo | ComboPressed` -> X |
| Y | XBC_Y / Joy4 | Y | `MeleeFinishAttack | NextWeapon` -> Q (Y) |
| b (Black) | Joy5 | E | `BlackButton | ToggleThirdCam` -> C (K) |
| c (White) | Joy6 | Q | `WhiteButton | VoiceMenuNext` -> V |
| l (left trigger) | Joy7 | PageUp ([) | `AltFire | Shield 1 | onrelease Shield 0` -> RightMouse |
| r (right trigger) | Joy8 | PageDown (]) | `Fire` -> LeftMouse (Alt) |
| > (Start) | Joy13 | Tab | `ShowMenu` -> Escape |
| < (Back) | Joy14 | F1 | `ShowScores` -> Tab (F1) |
| L (left stick / click) | Joy15 | the arrow keys | move: the keys of MoveForward/StrafeLeft/MoveBackward/StrafeRight |
| R (right stick / click) | Joy16 | NumPad 8/2/4/6 | look: the mouse |
| 1 3 5 7 (arrows) | d-pad / stick directions | unchanged: an arrow reads the same on a keyboard | unchanged |
Live: the menu table is the ini's (re-read when it changes is not needed: the keys page does not edit it); the game
side reads `UInput::Bindings` of player one's viewport at draw time, so the keys page (D-9, `SET Input`) is followed
at once. Several keys for one command: the keys page's own order: mouse buttons, Space, letters (left hand first:
`QWERTASDFGZXCVB`, then the rest), digits, the named keys, F-keys, the wheel. A command bound to no key: the pad glyph
stays (nothing to press).
Combo instructions in menus are game buttons: the key-glyphs set marks them (each glyph character `c` becomes
`c|0x80`; the engine gives the button font an identical glyph for `c|0x80` and takes the high bit as "game").

### The last-device mechanism
- Engine global `FKeyGlyphs GKeyGlyphs` (new `Source/Engine/Inc/UnKeyGlyphs.h`, `Src/UnKeyGlyphs.cpp`): player one's
  last device (keyboard+mouse or pad). Player one = the client's first viewport (G-11: the window's viewport, the only
  one that gets the keyboard and the mouse; split-screen viewports are pads only).
- SDL2Drv tells it: a key press, a mouse button, the wheel or a mouse move of at least a few pixels on the window
  viewport -> keyboard; a button press or a stick/trigger pushed past half way on the pad bound to player one, or that
  pad being connected -> pad. Start-up: keyboard (a PC); a pad already plugged in when the game starts counts as
  "connected" and makes it pad.
- `KeyGlyphs:` log lines (a new log name, suppressed in Default.ini like DevUI; tests un-suppress it): every device
  switch, and every change of the key a glyph resolves to.

### The package decision
GUI.u (patched) rather than a new `UC2Glyphs.u`:
- GUI.u is already rebuilt by the default build, shipped in the artifact and precached by the frontend: no new
  EditPackages entry, no change to `uc-build.sh`'s fixed list of 17 packages, `UC2Scripts.cmake`'s keep list or the
  frontend's precache. A new package needs all four.
- The gate falls out of it: the engine looks for `GUI.KeyGlyphs` (a new non-native class of the set, LOAD_Quiet): a
  vanilla GUI.u has none, so the glyphs stay Xbox buttons with no log line and no error.
- GUI owns the glyph table already (`XBoxButtonChars`), and a non-native class adds nothing to `GUIClasses.h`.
- Phase 2's sheet is imported into GUI.u by an `#exec TEXTURE IMPORT` in that class (`uc-build.sh --link
  GUI/Textures=...` exists for exactly this).

### Phase 1 (text)
- `DrawString` (`UnCanvas.cpp:392`): a button-font character of player one, keyboard active, that maps to a key ->
  draw the key's label in its place: a dark rounded backdrop + the label in the UI font (`T_Fonts2.SuiGeneris_12pt`,
  D-2's sharp sizes apply), scaled to fit the glyph's own cell. The advance width does not change, so no layout moves
  (the button bar's 23x23 labels, the combo menu's fixed positions). Labels: the key's name, short where long:
  ENTER ESC SPACE TAB SHIFT CTRL ALT BKSP DEL INS HOME END PGUP PGDN, UP DOWN LEFT RIGHT, LMB RMB MMB M4 M5, WHL+
  WHL-, F1..F12, NUM0..9, letters, digits, punctuation as itself.
- Patch set `key-glyphs` (depends on epic-fixes; on): `GUI/Classes/KeyGlyphs.uc` (new: the gate; phase 2: the sheet)
  and UCMenu `PlayerComboPanel` (the combo instructions marked as game buttons).
- Proof: scripted scenario (m10-controls `--file`): character page with the pad (Xbox glyphs), a key (keys shown),
  the pad again; the adrenaline menu in a match (pad / keyboard); a rebind of FIRE / ALT-FIRE / JUMP on the keys page
  and the combo glyph following; vanilla tree (UC2_SCRIPTS=none) clean; m14-scripts-check and m10 menu phases.

### Phase 2 (bitmaps)
- `tools/dev/make-key-glyphs.py` (Pillow): one sheet of key caps in the button font's style (dark cap, light rim,
  white legend; icons for Enter, Backspace, Tab, Shift, the arrows and the mouse buttons/wheel), committed as the
  generated image + the generator; imported into GUI.u; the engine draws the key's cell of the sheet in the glyph's
  cell instead of the text. Keys without a cell keep the phase 1 text.
