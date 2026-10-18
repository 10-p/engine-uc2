# players34

GUI + UCMenu (X-1 parts A and B, `docs/porting/analysis/x1-players34.md`): players three and four join from the character page
(`UC2Arena_Character`): their pad's Start there makes a player (Epic's GUI stops at two), each gets a cursor on the
honeycomb (3P green, bottom-left tag; 4P magenta, bottom-right) and a line of text under it, and the match spawns them
with the character they chose and the names "Player 3" / "Player 4"; three or four players choose their weapons on a
2x2 armory (part B). No preview, handicap panel, alternative skin or
profile for them (the owner's decisions, `docs/porting/PLAN.md` "X-1 decisions"). One and two players: as Epic's.

- `GUI.diff`
  - `GUIPage.MaxControllerJoins()` (new, script only): how many players may be joined while a page is up; 2 (Epic's
    limit) unless a page says otherwise. `GUIController.AllowControllerJoin` asks the active page instead of
    `MAX_VIEWPORTS` (at most 4, the split-screen code's limit). The title page keeps two.
  - `GUIPage.PlayerUnplugged(Player)` (new, script only): a pad pulled out while the page is up; true when the page
    lets that player go. `GUIController.SetGamepadStatus` then opens no `ControllerRemovedMenu`.
  - The focus clamps Epic forgot: `GUIController.Get/SetFocusedControlAt` and `GUIMultiComponent.SetFocusedControl`
    keep the index inside the two focus slots (native arrays `[MAX_VIEWPORTS]`: their layout cannot change), so three or
    four players in a match share player two's slot without the script VM's "Accessed array 'FocusedControl' out of
    bounds" lines (167 of them in the vanilla run of `g6-splitscreen.sh x1menu4`, 0 with this set).
  - `GameStartInfo.VerifyPlayerIndex` keeps indices 2 and 3 (their URL options are per index); `ExtraPlayerName` /
    `SetExtraPlayerName` (new): players three and four have no slot in `GameStartInfoBase.PlayerName[2]` (an Engine
    class), so their name is a `Name` entry of their own in `URLInformation`, which `GetSpawnURL` appends after the
    clamped one (a URL keeps the last value of an option, `FURL::AddOption`). `GUIController.AddChildViewport` and
    `SetProfilePlayerName` use it for child viewports 2 and 3 (Epic's code wrote player two's or one's name slot).
- `UCMenu.diff`
  - `UC2Arena_Character`: slots 3-4 (`CharIndex34`, `img_Cursor34`, `lbl_Players34`); every raw key of player three's
    or four's pad is handled by the page (`Players34Input` from the icons' `InternalRawXController` and the page's own
    `OnRawXController`) and used up: stick and d-pad move their cursor (Epic's hexagon moves), A or Start chooses
    (ready: the cursor dims), B takes it back (or leaves when not ready), Back leaves; the viewport is removed at the
    next GUI tick (`Timer`: `SPLIT REMOVE ID=<pad>`, engine since X-1); pulling the pad out leaves too.
    `NotifyAdd/RemoveViewport` (no profile prompt; the players after one leaving move up a place), `IsDisplayingHandicap`
    (none for them), `UpdateJoinText`/`UpdateControlVisibility`, `UpdateSelectedCharacter` (their choice moves no focus
    of player two), `SaveSettings` (names), `InternalOnReopened`/`ConfirmLaunchGame`, the opening and closing animations,
    `VerifyInputs` for every player. The line under the honeycomb: "PLAYER 3 / 4", "PRESS START" (Epic's localised
    `sPlayerTitle`, `sPressStart`: five languages without an `.int` of ours) until one joins, then a line each with the
    tags' own labels: "3P" while choosing (the cursor shows the choice), "3P READY" once chosen (Epic's localised
    `sReady`), "4P PRESS START" for one not there; no character names (the owner, 2026-10-11).
  - `UC2Arena_DualBase.InternalOnXControllerEvent`: players three and four's buttons go to the page's
    `Players34XControl` and are used up there (Epic's code would act on player two's panel, and a B nobody takes closes
    the page): the character page and the 2x2 armory give them their own, Epic's two-panel armory nothing.
  - `UC2Arena_Character.CloseComplete`: three or four players go on to the 2x2 armory `UC2Arena_QuadArmory`; one and two
    to Epic's `UC2Arena_Armory` / `UC2Arena_DualArmory` as before.
  - New `UC2Arena_QuadArmory` (X-1 part B, extends `UC2Arena_DualArmory`): four quarters in a 2x2 grid, player one top
    left, two top right, three bottom left, four bottom right (the corners of the 3P/4P tags); with three players the
    fourth stays empty. Players one and two through Epic's two-panel code (their quarters are `pnl_PlayerArmory`, the
    GUI's two focus slots, a controller mask per quarter); players three and four through `Players34XControl` and their
    own quarters (`pnl_Players34`). Each chooses an explosive and an energy weapon; the match starts when all are ready
    (`IsFinished`). B takes a player's last choice back; with nothing chosen, B of player one or two goes back to the
    character page as Epic's (not while player three or four has chosen something); B of three or four then does
    nothing. Their loadout is saved into their (transient) profile, which makes their spawn URL (`?ExplosiveWeapon=`,
    `?EnergyWeapon=`). Log (DevUI): `players34 armory (<why>): 1P <quarter>; 2P ...; 3P ...; 4P ...; players <n>`.
  - New `Armory_PanelQuad` (extends `Armory_PanelDual`): one quarter, Epic's split-screen armory panel at half its height:
    the two lists side by side (the weapon names condensed, `TextScaleX`/`TextScaleY`), the short loadout under them.
    Players three and four's quarters never take the GUI's focus (`bOwnInput`: the GUI has two focus slots): `PadInput`
    moves their lists (up/down, left/right between the lists), chooses (A/Start) and takes back (B/Back) the way the
    lists, `ExplosiveOnClick`, `EnergyOnClick` and `BClicked` do, and draws the list in use as focused.
- `new/UCMenu/Textures/UI_Char_3PSelector.tga`, `UI_Char_4PSelector.tga` (GENERATED from YOUR `T_UI_2.utx` by
  `tools/dev/make-player-tags.py`: never edit them; 64x64 RGBA, run-length coded): imported by
  `UC2Arena_Character`'s `#exec TEXTURE IMPORT` as `UCMenu.UI_Char_3PSelector` / `_4PSelector`, RGBA8 (16 KB each: the
  importer converts an image only to L8/A8, `UTexture::ConvertFormat`; a DXT3 would need a DDS of ours). Regenerate:

      cd <run tree>/System && ./ucc-bin batchexport T_UI_2.utx Texture dds <dds dir>
      tools/dev/make-player-tags.py --src <dds dir> --out <scratch dir> --final patches/players34/new/UCMenu/Textures

- **Engine** (shared code, no switch, `Source/Engine/Src/UnSplitScreenMgr.cpp` `CreateSplitPlayer`, `UnCamMgr.cpp`
  `SPLIT REMOVE ID=`): see the notebook. Without this set the engine's changes do nothing for one or two players.
- **Ini keys**: none. **Depends on**: epic-fixes (it rebuilds GUI and UCMenu). Comes before key-glyphs in `sets.txt`
  (`uc-patches.py make` cannot make a set for a package to which an earlier set added classes).
- Not in this set: a network client's own split players (X-1 part B tested one client with two: see the notebook).
- Generated by `tools/dev/uc-patches-regen.sh`. Notebook: `docs/porting/analysis/x1-players34.md`, "Part A".
