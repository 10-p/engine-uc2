# X-1 (examination): players 3 and 4 joining from the character page (2026-10-11)

Worktree `/home/ion/WorkUC2/engine-uc2-x1-players34`, branch `x1-players34` (from main a16ad39). Logs and pictures
`/home/ion/WorkUC2/logs/x1/`, run trees `run/x1` (this build's packages), `run/x1proto` (the prototype GUI.u below),
`run/x1proto-scripts` (the prototype's script tree and ucc work tree). Everything ran through `bin/capped` (build 6G,
ucc 2G, game 2G; no exit 137). This task only examines: nothing here ships. Committed: this notebook, two test parts
(`tools/dev/g6-splitscreen.sh <tree> x1probe | x1proto`) and the artwork generator (`tools/dev/make-player-tags.py`).

The owner's request (2026-10-11, verbatim where it matters): "How do they join? The game only had support for 2. Maybe
we can add at the bottom of the hexagon list a small blurb that reads 'Player 3 and 4 press start'. If they do we spawn
two extra hexagons with different colours with the text being 3P and 4P ... 3P as bottom left (just flip horizontally
the artwork, change the hue of it, clean that area and write 3P) and 4P as bottom right (same thing but with a
horizontal and vertical clear) ... a new patch for 3p and 4p that embeds the new artwork in the u class."

Labels used below: **measured** (log lines / numbers from a run), **seen** (a picture), **read** (code only),
**never exercised**.

## 1. How it works today

### The character page (`UCMenu.UC2Arena_Character`, Epic's text `epic-sources/UCMenu/Classes/UC2Arena_Character.uc`)

- One page, owned by the GUIController of player one's viewport (H-5: the menus draw on player one's canvas; once a
  second player joins, `USplitScreenMgr::SetupOverlayViewport` moves the GUI to the full-window overlay viewport, which
  draws only the menus: measured in the probe, overlay rect (0,0,1280,720) `menu=1`, the split views `disabled=1`).
- Layout (640x480 design space, defaultproperties): player one's preview panel on the left (x 38, w 182), the
  honeycomb in the middle (`HoneyCombLeft=226`, `HoneyCombBottom=335`, `HoneyCombDX=53.25`, `HoneyCombDY=61.5`,
  `IconSize=68`, `ColumnCount=5,5,5`: 15 icons in 3 columns, the middle column half a step higher), player two's
  preview (x 418) or, while alone, the stats panel. Under each preview `lbl_PlayerText` ("PLAYER 1", y 0.7625) and
  `lbl_JoinText` (profile name / "NO PROFILE" / "PRESS START", y 0.79375), `btn_Info` (MORE INFO, x 0.64 y 0.829),
  then the button bar (`ButtonPanel`). Pictures: `logs/x1/probe1/x1-probe-x1-character-one-player.png` (alone: right
  side reads "PLAYER 2 / PRESS START"), `...-two-players.png`.
- The "hexagon tags" are the players' cursors `img_Cursor[MAX_VIEWPORTS]` (GUIImage, `ImageStyle=ISTY_Scaled`,
  `MSTY_Alpha`: the WHOLE texture scaled into the rectangle, no UV sub-rectangle), Image =
  `SelectorMaterial[i]`: `T_UI_2.UI_Char_1PSelector` / `UI_Char_2PSelector` (entries 2 and 3, the alternative-skin
  cursors, are the same two textures). `PlaceReticle` puts it over the focused icon at (X-4, Y-4, W+7, H+7): 81-82
  design px; on the top icon (272.25, 51.25, 82, 82) = (568, 77, 122 px) at 1280x720 Fit43 (measured on the shot).
- Two players are hard-wired: `CharIndex`, `ProfileCharIdx`, `pnl_Preview`, `img_Cursor`, `lbl_PlayerText`,
  `lbl_JoinText`, `lbl_CharacterName`, `pnl_Handicap`, `btn_AltChar` are `[MAX_VIEWPORTS]` arrays
  (`Core/Object.uc:219 const MAX_VIEWPORTS = 2`), `SecondPlayerActive()` = more than one child viewport, `SetFocus`,
  `FocusFirst`, `UpdateJoinText`, `UpdateControlVisibility`, `MapControls` name indices 0 and 1 explicitly
  (`PlayerOwner(1)` 12 times in UCMenu), the opening/closing animations loop over `MAX_VIEWPORTS`.
- The "PLAYER 2 / PRESS START" text: `UpdateJoinText` -> `localize(sPlayerTitle) @ "2"` and `localize(sPressStart)`,
  from `Engine.<lang> [Localizer]` (int "Player"/"Press Start", deu "Spieler"/"Start drücken", fra "Joueur"/
  "Appuyer sur Start", ita "Giocatore"/"Premi Start", spa "Jugador"/"Presionar START"; `sNoProfile`, `sReady` too).
  Hidden in the Xbox Live area (`InXboxLiveArea()`).
- Input per player: the page is `MIM_Simultaneous` (`UC2Arena_DualBase.GetMenuInputMode`). Every key reaches
  `UGUIController::NativeKeyEvent` (`Source/GUI/Src/UnGUICore.cpp`) with `Engine->ActiveControllerId` = the pad's port;
  `FindViewportIndex(port)` = the viewport with that `GamepadID`, else the key is swallowed ("unbound gamepads");
  `ControllerId = port`; the focused control is looked up per player (`GetFocusedControl` ->
  `FindControllerPortIndex(255)` = the child viewport index of that port -> `FocusedControl[index]`, an array of
  `MAX_VIEWPORTS` in `GUIController`, `GUIMultiComponent` and `GUIComponent.LastFocusedControl`, all three NATIVE
  classes: `Source/GUI/Inc/GUIClasses.h:760, 1926, 2849`); the raw key goes first to that control's
  `RawXController(ControllerId, ...)`: the icons' `InternalRawXController` moves `CharIndex[FindControllerPortIndex(Id)]`
  over the hexagon by the stick, the d-pad goes through the icons' `Links`. So player two is "the second focus slot".

### How player two's Start reaches the page (H-3's path, measured)

Pad's Start (button 12) on a pad bound to no viewport -> `SDL2Pads.cpp SendButtonEdges` -> `USDL2Client::PadJoin`
(`SDL2Client.cpp:487`) -> `UClient::AllowControllerJoin` -> script `GUIController.AllowControllerJoin`
(`bActive`, no teaser, **`ChildViewports.Length >= MAX_VIEWPORTS` -> false**, then `ActivePage.AllowsControllerJoin()`:
true only on `StartMenu` (the title) and `UC2Arena_Character` outside Xbox Live) -> `BeginSplitScreen` ->
`USplitScreenMgr::CreateSplitPlayer(NULL, port)` (`UnSplitScreenMgr.cpp:487`: new split viewport, `SpawnPlayActor`,
`GUIController.AddChildViewport` -> the page's `NotifyAddViewport` -> `ForceProfileLoad`, the "no profile loaded"
box, reticle, labels). Log of the probe run: `Split screen: pad 1 Start in the menus: joined`,
`UC2Arena_Character::NotifyAddViewport ... GamepadID:1 ControllerId:1 PlayerIndex:'1'`.

### A third and a fourth pad's Start on the page today (measured: `g6-splitscreen.sh x1 x1probe`)

Runs `logs/x1/probe1/` and `logs/x1/probe2/` (Xvfb, llvmpipe, MSAA off, 1280x720, the default
`PadsStartAtPlayerTwo=False`: pad 0 = player one with the keyboard; pads 1-3 virtual):
- `pad 2 button 12 value 255 > 30`, `pad 3 button 12 value 255 > 30`: the presses reach the engine.
- No `Split screen: pad 2` / `pad 3` line, no `NotifyAddViewport ... GamepadID:2`: `GUIController.AllowControllerJoin`
  refuses at 2 child viewports (the engine logs nothing when the GUI refuses; `PadJoin` then finds the menu level and
  returns 0). The press goes on to the GUI as an unbound pad's key and `NativeKeyEvent` swallows it (no viewport has
  that port). Pad 2's A and stick: nothing (no error line, the page unchanged).
- Probe after the presses: still 2 split views, the page (with player two's profile box) unchanged:
  `x1-still-two-split-views-and-the-character-page` PASS (probe2).
So today: **the page has no slot for them and the GUI never creates their viewports**; the engine would (next item).

### In a match (G-6's `SplitScreenJoinInMatch`, measured)

Same runs, after both players launched DM-Deadbolt from the menus: `Split screen: pad 2 Start in the match: joined (3
split views)`, `... pad 3 ... joined (4 split views)`, both spawned; layout (Scion's quadrants): player one (0,0,640,360),
two (640,0), three (0,360), four (640,360), every view its split HUD (B-3b rows), `x1-four-split-players-in-the-match`
PASS. Player three's Start opens the pause menu (`OpenMenu [UCMenu.InGame_MainMenu]`, picture
`probe1/x1-probe-x1-pause-menu-player-three.png`), its d-pad moves the focus (`InGame_MainMenu->ItemSelectionPanel
handled XC_PadDown for id 2`, picture `probe2/...-player-three-down.png`), its B closes it (probe1: menu None; probe2:
`handled XC_B for id 2`, the close finished after the probe at < 1 fps: a test timing artefact; the scenario's last
steps then failed because player one's keys are swallowed while player three owns the menu, Epic's
`ActiveChildViewport` rule). BUT the in-match joins add child viewports 3 and 4 to the GUIController, and every menu
frame then logs script errors: `GUIController.GetFocusedControlAt ... Accessed array 'FocusedControl' out of bounds
(2/2)` 142-144 times, `(3/2)` 1-2, `SetFocusedControlAt` 5, `GUIMultiComponent.SetFocusedControl` 6-8 in about 10 s
of pause menu (the script VM clamps the index: player three and four use player two's focus slot). Works, noisily.

### Prototype: what the engine does when the GUI lets a third and fourth player in (measured)

To answer "does the engine accept 4 players in the menus natively", a scratch GUI.u with one change,
`GUIController.AllowControllerJoin`: `ChildViewports.Length >= 4` (decompiled line 305 of `Scripts/tree`; compiled
with `tools/scripts/uc-build.sh` into `run/x1proto-scripts/work`, run tree `run/x1proto`; not committed). Run
`g6-splitscreen.sh x1proto x1proto`, `logs/x1/proto1/`:
- Pads 1, 2, 3 Start on the character page: `joined` each; 4 split views + the overlay, quadrants, ports [0,1,2,3]
  (`x1p-four-split-views-on-the-character-page` PASS). **The engine side of the menus takes four players as is.**
- The shipped page cannot: `NotifyAddViewport ... PlayerIndex:'2'` / `'3'` then `Accessed array 'CharIndex' out of
  bounds (2/2)` / `(3/2)`; 2,707 out-of-bounds lines in the run (the GUI's `FocusedControl` 2,328, the page's
  `pnl_Handicap` 346, the rest `CharIndex`, `img_Cursor`, `pnl_Preview`, `btn_AltChar`, `lbl_CharacterName`). Players three
  and four drive PLAYER TWO's slot: their sticks moved player two's cursor and preview (Anubis -> Arclite), and
  `PlaceReticle` gave player two's cursor `SelectorMaterial[2]` = the 1P texture (two yellow "1P" cursors on screen):
  `proto1/x1-proto-x1p-player4-stick-left.png`; the page still shows two players (`...-character-four-players.png`).
- The match after the menus: DM-Deadbolt loads with 4 split views, then **`Too many splitscreens.`** three times and
  only player one gets a pawn: `USplitScreenMgr::CreateSplitPlayer` checks `NumActiveSplitViews() >= MaxSplitScreens`
  also for a viewport that ALREADY is one of the split views (`MatchSplitViewportsToActors` after a travel passes the
  existing viewports), so with 4 views (= `MaxSplitScreens`) it refuses all of them. Never reached on the Xbox (2
  views). A real bug in Epic's shared code, a one-line fix (count only when a viewport is to be created).
- The spawn URLs after the travel (`Split player spawn URL:` lines): all three split players carry `Name=Arclite` (the
  native call `GSI->eventGetSpawnURL(ActiveSplitViews.Num() ? 1 : 0, Profile)`, `UnSplitScreenMgr.cpp:537`, hands
  player index 1 to every split player; `GameStartInfoBase.PlayerName[MAX_VIEWPORTS]` clamps 2 and 3 to 1): names
  and URL options of players 3 and 4 would be player two's.
(The run's `x1p-travel-to-deathmatch` FAIL is the scenario's: the travel line came while the scenario was still in its
accept loop; the level did load, line 3391.)

## 2. The artwork (measured on the shipped textures; sheet for the owner)

- `T_UI_2.Characters.UI_Char_1PSelector` / `UI_Char_2PSelector`: 64x64 **DXT3**, one mip (`ucc-bin batchexport
  T_UI_2.utx Texture dds <dir>`; `tga` writes empty files for DXT: the TGA exporter only takes uncompressed formats).
  Alpha bbox (0,5)-(64,60). The hexagon is flat-topped: top edge rows 5-8, bottom 56-59 at x=32; left/right vertices
  x 2-5 / 60-63 at y=32 (1P). The tag is a triangle in the top-left corner (x 0..19, rows 5..~25), its dark diagonal
  edge 2 px; the label "1P" is a 6-row pixel font (rows 10-15, anti-alias rows 9 and 16), "1" x2-5, "P" x7-15, dark
  core about 3% of the fill, two anti-alias levels (about 35% and 65%). 1P hue 57 (yellow), 2P 182 (cyan).
- Epic made 2P by mirroring 1P left-right with x' = 63 - x (alpha IoU 0.865 outside the tags; 1P is itself
  symmetric about x = 32.5, IoU 0.919 with 65 - x): **Epic's 2P outline sits 2 texels left of 1P's**, so the two
  cursors never coincided exactly on a portrait (about 2.5 design px).
- Terminology to confirm (question 1): flipping 1P "horizontally" (left-right) puts the tag top-RIGHT (= 2P's
  place). Bottom-left needs a top-to-bottom flip; bottom-right both. The generator does that: 3P = top-to-bottom
  (y' = 64 - y: the top/bottom edges land exactly on 1P's rows 5-8 / 56-59, measured), 4P = 3P mirrored as Epic
  mirrored 2P (x' = 63 - x: 4P shares 2P's outline). Sides vs 1P/2P: IoU 0.80 / 0.76 (the hexagon is a texel off
  symmetric top-to-bottom, antialiasing and DXT blocks).
- Cleaning: in each label row the tag fill is interpolated across the label up to the dark edge: 0 dark pixels left
  (check). Writing: the label is a multiplier on the fill (keeps the fill's gradient and the hue), in the 3-level font
  transcribed from 1P ("1" and "P"; rewritten at Epic's place: mean |difference| 4.4/255 against Epic's label) with new
  "3" and "4" in the same strokes, 5 columns wide. **The flipped tag narrows where the label's top is** (the tag
  narrows towards the hexagon's middle; text stays upright), so "3P" is written 13 columns wide with the P's anti-alias
  column merged into the digit's (one fill column left before the edge and one at the border, measured). Epic's 1P has
  2-3 spare columns at the top; ours has 1: tight but readable at 4x.
- Candidates: hue rotation (HSV, saturation and value kept) to green 120, red 0, magenta 300, orange 28, violet 270.
  **Sheet: `/home/ion/WorkUC2/logs/x1/tags/player-tags-sheet.png`** (row 1: Epic's 1P, 2P, then 3P/4P per colour at 4x;
  row 2: the four cursors composited on the top portrait of a real 1280x720 shot of the page, for the shipped pair and
  three pairings green+magenta, red+green, magenta+orange: a mock-up made by the script, not the engine's drawing).
  The TGAs (`UI_Char_{3P,4P}Selector-<colour>.tga`, 64x64 RGBA RLE) and `player-tags-report.txt` are beside it.
  Regenerate: `tools/dev/make-player-tags.py --src <dds dir> --out <dir> [--page <shot> --rect 568.4,76.9,121.5]`.
- In the package: `#exec TEXTURE IMPORT NAME=UI_Char_3PSelector FILE=Textures\UI_Char_3PSelector.tga MIPS=OFF
  ALPHATEXTURE=1` imports RGBA8 (16 KB each; the importer also takes a DDS DXT3 as is). Seen only as a mock-up; not
  drawn by the engine in this task.

## 3. The plan (patch set `players34`)

Order of work, each a commit with a test:
1. **Engine (shared code, real fixes, no switch):**
   a. `USplitScreenMgr::CreateSplitPlayer`: the `MaxSplitScreens` check only when a viewport is to be created
      (the prototype's `Too many splitscreens.`); without it, four players chosen in the menus never spawn.
   b. `CreateSplitPlayer`'s `eventGetSpawnURL(ActiveSplitViews.Num() ? 1 : 0, ...)`: pass the split view's own index
      (the script's `VerifyPlayerIndex` takes 2/3 through `FindControllerPortIndex`; `URLInformation` is per index and
      dynamic). Two players: byte-identical (index 1 stays 1).
   c. Nothing else native is needed for the menus (prototype: 4 views, overlay, layouts, input by port). The GUI's
      native arrays stay [2]: they are the shipped GUI.u's layout (F-1 rule), so players 3-4 never get GUI focus slots
      of their own; the page handles them itself (step 3).
2. **GUI.u (`players34` diff, depends on `epic-fixes`):** `GUIController.AllowControllerJoin` lets a 3rd/4th child in
   only when the active page asks for it (a new script-only function on `GUIPage`, e.g. `MaxControllerJoins()`
   returning `MAX_VIEWPORTS`, overridden to 4 by the character page: the title page and every other page keep 2);
   the clamps Epic forgot (`GUIController.Get/SetFocusedControlAt`, `GUIMultiComponent.SetFocusedControl`) so 3-4
   players in a match share player two's slot without the error spam. Script functions on native classes do not
   change the C++ layout (`verifyclasses` to prove it); no header change expected.
3. **UCMenu.u (`players34` diff + the two textures under `new/UCMenu/Textures/`):** `UC2Arena_Character`:
   - new non-native vars for slots 2-3 (`CharIndex34[2]`, `img_Cursor34[2]` GUIImages with the 3P/4P textures,
     `ProfileCharIdx34[2]`), the blurb label; `SelectorMaterial` entries for 3P/4P (variant cursors: same textures);
   - input: in `InternalRawXController` (it sees every key first, with the port as `Id`) a port whose child index is
     2 or 3 is handled entirely by the page (stick/d-pad over the hexagon with the existing hex-neighbour code on
     `CharIndex34`, A = ready, B = leave/unready) and consumed, so it never touches player two's focus slot;
   - `NotifyAddViewport` / `NotifyRemoveViewport` / `IsFinished` / `UpdateJoinText` / `UpdateControlVisibility` /
     the animations / `SaveSettings` (URL per player) for 4 players; `AllowsControllerJoin` + the new limit 4;
   - the blurb under the honeycomb: built from the existing localised strings (`sPlayerTitle` "3 / 4"
     `sPressStart`), so all five languages are right without new `.int` files (question 5);
   - players 3-4: no preview panel, no handicap panel, no alt-skin button (no room; question 3).
   `UC2Arena_DualArmory` (shown when more than one player): see question 4.
4. **Tests:** `g6-splitscreen.sh x1menu4` (four joins on the page, each pad moves only its cursor, three ready, the match
   with four players spawned with their chosen classes and own names, back to the main menu with one viewport, 0
   out-of-bounds lines), the vanilla tree (shipped GUI.u/UCMenu.u: two players exactly as today), `g6 menu`, `h3menu`,
   `m14-scripts-check`, P2 reference; the HUD rows (B-3b) already hold for 3-4.
5. **Web:** a browser run with three virtual pads (see question 7).

## 4. Issues and questions (numbered, with my recommendation)

1. **"Flip horizontally" for 3P.** A left-right flip puts the tag top-right (2P's corner). For bottom-left I flipped
   top-to-bottom and for bottom-right both ways. *Recommend:* confirm that reading.
2. **Colours.** Sheet above. *Recommend* 3P green + 4P magenta (four clearly different hues next to yellow and cyan:
   yellow/cyan/green/magenta); red+green is the classic pair but red/green and yellow/green are the hardest for
   colour-blind players.
3. **Room on the page.** The page is two big previews with the honeycomb between; there is no room for two more
   previews, names, handicap or alt-skin prompts. The free strip under the honeycomb is about 174 x 20 design px
   (x 226-400, y 403-425, between "NOT SIGNED IN" and MORE INFO): "PLAYER 3 / 4 PRESS START" (24 characters) fits in a
   12 pt line in English; "JOUEUR 3 / 4 APPUYER SUR START" (30) needs two lines or a smaller font. *Recommend:*
   players 3-4 are a cursor each (3P/4P) plus that strip, which shows "PLAYER 3 / 4 PRESS START" until they join and
   then "3P <character> · 4P <character>" ([Ready] marks); no preview, no handicap for them (handicap = 100). Owner
   to confirm.
4. **The armory (weapon loadout) after the page.** With 2+ players UC2 opens `UC2Arena_DualArmory`: two panels side by
   side (each 38% of the width), each masked to its pad (`SetControllerMask(1 << GamepadID)`), `[MAX_VIEWPORTS]`
   arrays. Four do not fit. Options: (a) players 3-4 skip it and take their profile's/default loadout (cheap);
   (b) a 2x2 four-panel armory (a new page, about 2-3 agent-days); (c) players choose in turns. *Recommend* (a) first.
5. **Localisation.** No `.int` file is ours to ship (they are the disc's, all five languages; the Italian files are
   lower case: `engine.ita`, `ucmenu.ita`), and a new `localized` string in a patched class falls back to its English
   default in every language (as `keys-menu` does today). *Recommend:* compose the blurb from Epic's own
   `sPlayerTitle`/`sPressStart` (localised for free); if a real sentence ("Players 3 and 4: press Start") is wanted,
   we need a mechanism to ship our own `<Pkg>.<lang>` overlay strings: a separate decision.
6. **Profiles and names for 3-4.** Today a joining player gets the "no profile loaded, load one?" box; with four
   players those boxes (modal pages driven through the focus system) would come one after another and players 3-4 would
   use player two's focus slot. Names: the prototype gave all three split players `Name=Arclite` (index 1 hard-coded in
   native code, `PlayerName[2]` clamped). *Recommend:* players 3-4 skip the profile prompt ("NO PROFILE"), names
   "Player 3"/"Player 4" (localised `sPlayerTitle`) unless a profile is loaded later; fix 1b natively.
7. **The web.** SDL2Drv has 4 pad slots (`SDL2DRV_MAX_PADS = 4`); the browser's Gamepad API reports up to 4. With
   `PadsStartAtPlayerTwo=False` pads take ports 0-3 (pad 0 shares player one with the keyboard): four players. With
   `True` the keyboard keeps port 0 and pads take 1-3: keyboard + 3 pads = four players, a fourth pad finds no port
   (logged). So yes, a 3rd and 4th pad can be ports 2-3 in either mode. **Never exercised** in a browser (G-11 ran one
   pad); four scene renders per frame in the browser will cost (single player 22.5 ms/frame in F-3). *Recommend:* one
   browser run with three virtual pads once the frontend is unparked (owner rule: no frontend work until the merge).
8. **The network (system link).** Server side, `JOINSPLIT` (`UnLevel.cpp:1261`) has no per-host limit (only the game's
   MaxPlayers); a client sends one `JOINSPLIT` per local split player. Split players on a CLIENT were never tested in
   this port (network.md: "splitscreen child connections ... never exercised"), with 2 players either. Hosting with 3-4
   local players is the same as a standalone match (listen server: the same `CreateSplitPlayer`). *Recommend:* scope
   X-1 to standalone and hosting; client-side split players (2, 3 or 4) as their own task.
9. **The title page.** `StartMenu.AllowsControllerJoin` is true too: with Epic's limit lifted globally a 3rd pad's Start
   on the title page would make a player there. *Recommend:* the per-page limit of step 2 (only the character page
   says 4).
10. **Vanilla scripts.** Plain Epic GUI.u/UCMenu.u keep two players in the menus exactly as today (the GUI refuses the
    third); in a match 3-4 can still join (G-6) with the focus-array error lines measured above. Engine steps 1a/1b
    change nothing for 1-2 players. *Recommend:* keep it so (F-1 rule); the patch set is the opt-in.
11. **Detail and FOV with four players** are G-9's settings (full detail by default, the clamped FOV); B-3b's HUD rows
    hold. Nothing new.
12. **The artwork in the repository.** The 3P/4P TGAs are derived from Epic's 1P art. `key-glyphs` committed its sheet
    because it is our own drawing; this one is not. *Recommend:* generate the two TGAs at build time from the player's
    own `T_UI_2.utx` (the build already runs ucc: `batchexport ... dds` + `make-player-tags.py`, Pillow is already a
    dev tool dependency, or a small DXT3 decoder to avoid it) instead of committing them; owner to decide.

## 5. Cost (agent-days) and order

| step | what | estimate |
|---|---|---|
| 1 | engine: CreateSplitPlayer limit fix, spawn URL index, tests for 1-2 players unchanged | 0.5 |
| 2 | GUI patch: per-page join limit, focus clamps, verifyclasses/P2 | 0.5 |
| 3 | UCMenu character page for 4 (input routing for ports 2-3, cursors, blurb, ready/finish, URL per player) | 2.5-3 |
| 3b | artwork: generator final + build-time generation or committed TGA, `#exec` import | 0.5 |
| 3c | armory: (a) skip for 3-4 / (b) four-panel page | 0.5 / 2-3 |
| 4 | tests: `x1menu4` part, vanilla tree, regressions (g6 menu, h3menu, m14, d6, P2/P3) | 1-1.5 |
| 5 | web run with three pads (after the frontend unparks) | 0.5-1 |
| - | client-side network split players (if wanted) | 1-2 (separate) |

Core path (1-4 with armory option a): **about 5.5-6.5 agent-days**; with the four-panel armory 8-9.

## 6. Results of this examination (labelled)

- Third/fourth pad's Start on the character page today: no join, no viewport, no effect: **measured** (probe1, probe2).
- Third/fourth pad's Start in a match: players 3 and 4 join, spawn, own quadrants and HUDs: **measured** (probe1/2,
  `x1-four-split-players-in-the-match` PASS); player three's pause menu opens/navigates/closes: **measured** (with
  the focus-array error lines counted).
- Engine accepts four split players in the menus once the GUI allows it: **measured** (proto1); the shipped page then
  mixes players 2-4: **measured** (error counts) and **seen** (pictures); after the travel players 2-4 do not spawn
  (`Too many splitscreens.`) and share player two's name: **measured** (log lines); the cause: **read**.
- Artwork: sizes, format, glyph rows, mirror offsets, cleaning and label clearance: **measured** by the generator
  (`player-tags-report.txt`); the in-place look: **seen** only, as a mock-up.
- Web pads 3-4, network with split players: **never exercised**.
- Memory ceilings: build 6G (`capped 6G 3600`), ucc batchexport 2G, prototype script compile 6G (ucc runs 3G each
  inside), game runs 2G (`g6-splitscreen.sh`); no ceiling hit.

## Part A (2026-10-11): players 3 and 4 join from the character page (built)

Same worktree and branch (from main 092db72), logs `/home/ion/WorkUC2/logs/x1/` (`a6/` the final rebuilt run, `v1/` the
vanilla one, `reg/` the regressions), run trees `run/x1a` (this build's packages) and `run/x1ship` (the same binary,
Epic's shipped GUI.u/UCMenu.u: `UC2_SCRIPTS=none tools/dev/mkrun-uc2.sh build-native x1ship`). The owner's decisions:
PLAN.md "X-1 decisions (owner, 2026-10-11)". The armory 2x2 page and the client test are part B.

### What was built (file:line at the commit)

Engine (shared code, real fixes, no switch; one or two players: the same path, the same values):
- `Source/Engine/Src/UnSplitScreenMgr.cpp:537-543` `CreateSplitPlayer`: `eventGetSpawnURL` gets the split player's own
  index (`ActiveSplitViews.FindItemIndex`; a viewport not in the list keeps Epic's `Num() ? 1 : 0`). Player two: 1 as
  before; players three and four: 2 and 3 (they took player two's name and URL options).
- `UnSplitScreenMgr.cpp:553-556`: the `MaxSplitScreens` check only when a viewport is to be created (`!Viewport`): with
  four views after a travel Epic's check refused every split player (`Too many splitscreens.`). One or two players: the
  check was never true for a passed viewport (2 < 4), so nothing changes.
- `Source/Engine/Src/UnCamMgr.cpp:310-336` `SPLIT REMOVE ID=<pad>`: removes the split view of that pad (never player
  one's view); without `ID=` Epic's (the last one), unchanged. The character page uses it when player three or four
  leaves; it runs from the page's `Timer` (the GUI tick, `UInteractionMaster::MasterProcessTick`), never inside the
  leaving viewport's own input. A first version called `ClearQueriesForViewport` first (as `ResetState`): SIGSEGV in
  the menus (a never-drawn split view, another viewport's queries; `logs/x1/a1` first run). Removed: Epic's REMOVE
  never cleared either.

Patch set `players34` (`patches/players34/`, README there; `sets.txt` after player-name, before key-glyphs: no hunk
shared with the later sets, and `make` refuses a set for a package to which an earlier set added classes):
- GUI: `GUIPage.MaxControllerJoins()` (2) and `PlayerUnplugged(Player)` (false), script only;
  `GUIController.AllowControllerJoin` asks the active page (at most 4); focus clamps in
  `GUIController.Get/SetFocusedControlAt` and `GUIMultiComponent.SetFocusedControl`; `GameStartInfo.VerifyPlayerIndex`
  keeps 2-3, `ExtraPlayerName`/`SetExtraPlayerName` (a `Name` entry in `URLInformation`: `GameStartInfoBase` is an
  Engine class with `PlayerName[2]`, and its `GetName` clamps; `GetSpawnURL` appends the entries of the index after the
  clamped name, and `FURL::AddOption` keeps the last value), used by `GUIController.AddChildViewport` and
  `SetProfilePlayerName` (Epic's code wrote the name of a third player into player ONE's slot through
  `VerifyPlayerIndex(2, None) -> 0`); `SetGamepadStatus` asks `ActivePage.PlayerUnplugged` before opening
  `ControllerRemovedMenu`. No native layout changed: `check-headers` current, verifyclasses PASS (m14 `verify`).
- UCMenu `UC2Arena_Character` (about 650 lines, the page's own functions `*34`): see the set's README. Input: every raw key
  of ports whose child index is 2-3 goes to `Players34Input` (from the icons' `InternalRawXController`, its first
  lines, and from the page's own `OnRawXController` when player two's focus slot is not an icon) and is used up;
  `UC2Arena_DualBase.InternalOnXControllerEvent` uses up their buttons on the armory (and hands the character page the
  ones the raw path missed, `Players34XControl`).
- Art: `patches/players34/new/UCMenu/Textures/UI_Char_{3P,4P}Selector.tga` written by `tools/dev/make-player-tags.py
  --final` (new option; 3P green, 4P magenta, byte-identical to the examination's candidates), imported RGBA8
  (`UTexture::ConvertFormat` only makes L8/A8 from an image; DXT3 would need a DDS of ours), CLAMP, no mips.
- The text: "PLAYER 3 / 4" + "PRESS START" (Epic's `sPlayerTitle`, `sPressStart`) until one joins; then a line each,
  "3P <CHARACTER> [READY]" / "4P PRESS START". The examination's plan said `sPlayerTitle` per line too: measured on the
  page (`a5/x1-menu4-page-4-ready.png`), "PLAYER 3: ARCLITE [READY]" does not fit the strip under the honeycomb
  (between NOT SIGNED IN and MORE INFO, Sui12pt, the label clips with "..."), so the joined lines use the cursors' own
  tags, at 0.9 scale. Longest measured case fits; other languages: "4P SAPPHIRE [PRONTO]", "4P APPUYER SUR START" are
  20 characters, at the edge (never exercised: the run is English).
- Behaviour decided here (owner to confirm): B of player three or four takes the choice back, or leaves when not ready;
  Back leaves; a pad pulled out on the page leaves (no ControllerRemovedMenu); a ready player's cursor dims (alpha 110);
  when player three leaves with four there, player four becomes player three (its viewport is child 2 now: name, tag,
  index all follow). The cursor of a joining player starts on its profile's character (Anubis for a transient one);
  random picks a real character (no alternative skins for 3-4).

### Tests

`tools/dev/g6-splitscreen.sh <tree> x1menu4` (`scen_x1menu4`, helpers `tools/dev/g6/x1-checks.py`; Xvfb, llvmpipe,
MSAA off, 1280x720, default `PadsStartAtPlayerTwo=False`). The page logs `X-1 players34 cursors (<why>): 1P i; 2P i;
3P i; 4P i; players n` on every move/join/choice of any player while three or more are there, and `X-1 players34 text:
<line one> | <line two>` when the text changes (DevUI). On a tree whose UCMenu.u has no `UI_Char_3PSelector` the
vanilla variant runs; on the rebuilt tree a second run, `x1-host4`, takes H-3's system-link host flow (a listen server).

What x1menu4 checks (rebuilt tree, 58 checks): the line "PLAYER 3 / 4 | PRESS START" alone; pad 1's Start joins
player two (Epic's path); pad 2's and pad 3's Start join players three and four (no profile box), G-6's 3 and 4-view
layouts (rects equal `layout()`, all split views disabled under the menu overlay); each pad moves only its own cursor
(pad 2's stick and d-pad: 3P only; pad 3's: 4P only; pad 1's d-pad: 2P only; the keyboard: 1P only; from the logged
indices); the text in each state; player four's Back removes its view (3 views, "4P PRESS START", no page opens), its
Start joins it again, its pad pulled out removes it (no ControllerRemovedMenu), plugged back and Start: four again,
ports [0,1,2,3]; player one's B leaves the page and Return opens it again with four players, the 3P/4P cursors placed;
players three and four ready, B takes it back, ready again, the page waits for players one and two; the armory (two
panels): players three and four's B, A, d-pad do nothing (it stays open); the match: four split players, G-6's
quadrants, split HUDs, no "Too many splitscreens", 3P/4P pawns of the classes they chose, all four classes different,
players one and two the ones they chose, players three and four named `Player_3` / `Player_4`; player three's Start
opens its pause menu, d-pad, B closes it; FORFEIT back to the main menu, one viewport; **0 out-of-bounds lines**.
x1-host4 (40 checks): the same through NETWORKED COMBAT -> HOST SYSTEM LINK GAME (listen server), without the
leave/reopen/armory parts. Vanilla (19 checks): two players on the page, pads 2/3's Start make nobody there, the match
with two, pads 2 and 3 join in the match (G-6), four split HUDs, player three's pause menu, FORFEIT, no crash.

### Results (labelled)

| what | where | result |
|---|---|---|
| `g6-splitscreen.sh x1a x1menu4` (players34) | `logs/x1/a8` | **measured** PASS x1-menu4 58/58, PASS x1-host4 40/40, out-of-bounds lines `{}` |
| `g6-splitscreen.sh x1ship x1menu4` (Epic's GUI.u/UCMenu.u, this binary) | `logs/x1/v2` | **measured** PASS 19/19; `FocusedControl` out of bounds 172 lines (v1: 167): the "before" number |
| G-6 `menu`, `input`; H-3 `h3menu` (ticked, unticked); G-9 `kbplayer`+`kbmenu`; G-11 `unplug` on x1a | `logs/x1/reg` | **measured** PASS 9, 39, 11, 13, 34, 11, 26 |
| `m14-scripts-check.sh verify menu patcher deadbolt` (M14_PREFIX=x1-m14) | `logs/x1/m14` | **measured** PASS all four (verifyclasses: all layouts match) |
| P2 | `logs/x1/p2-write.log` | reference regenerated (GUI.u 1771600 B `2a32a007...`, UCMenu.u 2476635 B `1b2955ac...`, UCGameplay.u unchanged); PASS |
| `check-headers` | `logs/x1/check-headers.log` | **measured** current (no header change) |
| pictures | `logs/x1/page-{1..4}.png`, `page-4-ready.png`, `page-4-reopened.png`, `match-four.png`, `match-four-host.png` | **seen** |

Note: the `reg` run's last line is a bash syntax error at `g6-splitscreen.sh:1861`: the script was edited (the host
variant) while that run was reading it; every part had finished and passed before.

Memory: builds `capped 6G 3600` (-j4), set regeneration `capped 3G`, P2 and check-headers `capped 6G`, every game run
2G (the harness), m14 its own ceilings. No ceiling hit (no exit 137). One SIGSEGV of my own making (above), fixed.

### For part B (the 2x2 armory, the client test)

- The flow today: with three or four players the page opens `UC2Arena_DualArmory` (`CloseComplete`:
  `SecondPlayerActive()`); it shows players one and two only; `UC2Arena_DualBase.InternalOnXControllerEvent` uses up
  players three and four's buttons there (and on every DualBase page), so they cannot touch player two's panel. They
  take their profile's loadout (a transient profile's defaults). `IsFinished` of the armory counts two panels.
- Hooks: `GUIPage.MaxControllerJoins` (the armory page may stay at 2: joins happen on the character page only);
  `UC2Arena_DualBase.Players34XControl(PlayerIndex, Code)` (override it on a 2x2 page to give players three and four
  their panels; or drop the DualBase guard there); the character page's `CloseComplete` chooses the armory page;
  `GetPlayerIndex()` is 2/3 for their input (the GUI's FOCUS slots stay two: a four-panel page must route their input
  itself, as `Players34Input` does, `Armory_PanelDual.SetControllerMask(1 << GamepadID)` per panel is Epic's way);
  `GameStartInfo` keeps their URL options per index (2, 3) now, so `PlayerProfile` weapon options of a panel reach
  their spawn URL (`GetSpawnURL(2|3)`).
- The client test: `CreateSplitPlayer`'s spawn index is the split view's own on a client too (`JOINSPLIT`); never
  exercised with split players on a client (network.md).

### Open questions (for the owner)

1. Leaving: B (not ready) leaves too, besides Back and pulling the pad out. Keep, or Back only?
2. The joined lines read "3P ARCLITE [READY]" (the tags' labels) because "PLAYER 3: ARCLITE [READY]" does not fit; the
   blurb keeps "PLAYER 3 / 4 PRESS START". Other languages: the longest lines are at the strip's edge (not seen).
3. A ready cursor of player three or four dims (alpha 110), the others' cursors give way to the handicap panel as
   Epic's. Fine?
4. Players three and four get no handicap (100) and no alternative skins (random never picks one for them).

## Part B (2026-10-11): the 2x2 armory, the client test, the browser run

Worktree `/home/ion/WorkUC2/engine-uc2-x1b-armory`, branch `x1b-armory` (from main 47145a2); logs `/home/ion/WorkUC2/logs/x1b/`,
run trees `run/x1b` (this build's packages), `run/x1bship` (the same binary, Epic's shipped GUI.u/UCMenu.u), `run/x1bnet`
(the client test). Frontend worktree `/home/ion/WorkUC2/ufrontend-x1`, branch `uc2-x1-players34` (from `uc2` b1a1051).

### What Epic's armory pages are (read)

- `UC2Arena_Armory` (one player): `Armory_PanelSingle` (portrait, the two lists, the long loadout, the weapon preview).
  `UC2Arena_DualArmory` (two): two `Armory_PanelDual` side by side (x 59 / 343, 244 x 334 design px), each the two
  lists stacked and a `ShortLoadoutPanel`, each masked to its pad (`SetControllerMask(1 << GamepadID)`), the page's
  `SetFocus`/`FocusFirst` give each panel the GUI focus of its own player by setting `Controller.ControllerId` around the
  call. `IsFinished` = every panel `IsReady` (an explosive and an energy weapon chosen); a panel's last choice calls
  `ShowNextMenu` -> `LaunchGame` (the ConfirmLaunchMenu) when all are ready. B undoes the player's last choice; with
  nothing chosen on any panel it goes back to the character page. `Save` writes the choice into the player's profile,
  and the profile's `GetURL` (`GetWeaponsURL`: `?EnergyWeapon= ?ExplosiveWeapon=`) is what `GameStartInfo.GetSpawnURL`
  parses into the player's spawn URL.
- Sizes are a trap: the list panels (`ArenaListPanel`, `ScalingType=SCALE_X`, `bConvertBoundX/Y=false`) take their
  height as a fraction of the SCREEN and their left/top in design pixels of the panel (Dual: `WinTop=105.31`), the
  loadout takes its top as a fraction of the panel and its height of the screen (seen: the first 2x2 try, everything
  overlapping, `logs/x1b/r1/x1-armory4-armory-4.png`).
- The panels' own focus code (`Load`'s `pnl_Explosive.SetFocus`, the lists' `Up`/`Down` at their ends, `ExplosiveOnClick`'s
  `pnl_Energy.SetFocus`, `EnergyOnClick`'s `LoseFocus`) goes through the GUI's focus slots, and with
  `Controller.ControllerId` = port 2 or 3 those are player two's (part A's clamp). Players three and four's panels must
  never call them.

### What was built (patch set players34; file:line at the commit)

- New `patches/players34/new/UCMenu/Classes/UC2Arena_QuadArmory.uc` (extends `UC2Arena_DualArmory`): `CreateArmoryPanel`
  (:16) makes one `Armory_PanelQuad` per joined player (`GetViewportCount(true)`, at most 4) at `PanelLeft[i % 2]`,
  `PanelTop[i / 2]` (44 / 324, 68 / 245), players one and two in Epic's `pnl_PlayerArmory`, three and four in
  `pnl_Players34`; the mask per quarter as Epic's. `IsFinished` (:54) adds three and four; `Players34XControl` (:66, the
  part A hook of `UC2Arena_DualBase`) hands their buttons to their quarter's `PadInput` and logs; `BClicked` (:84) keeps
  Epic's rule and adds "not while three or four have chosen"; `ConfirmLaunchGame`, `UpdateSelectedCharacter`,
  `GetInfoText`, `ValidXControl` never index `pnl_PlayerArmory` with 2 or 3 (`Super(UC2Arena_Armory)` for them);
  `Load/Save/Refresh` and the open/cancel animations for the extra quarters; `ArmoryStatus` (:249) the log line
  `X-1 players34 armory (<why>): 1P <quarter>; 2P ...; 3P ...; 4P ...; players <n>` (DevUI) on every list move or choice
  of any player (the panels' `UpdateStatus` delegate, `PanelStatus`, why = `port <ControllerId>`) and on every button of
  three and four (why = `player 3 port 2 XC_PadDown`).
- New `patches/players34/new/UCMenu/Classes/Armory_PanelQuad.uc` (extends `Armory_PanelDual`): `InitComponent` (:17) the
  quarter's layout (lists side by side 0.49 of the quarter wide, the energy list at 138.7 px, both Dual's 0.216736 screen
  high; the loadout at 0.6 of the quarter, 0.145 screen high; the weapon names condensed `TextScaleX=0.55 TextScaleY=0.85`
  and the loadout captions 0.65 with part A's `GUILabel.ScaleX/ScaleY`: at scale 1 "Grenade Launcher" is 155 design px,
  a list is 133); `MapControls` (:77) left/right between the lists for players one and two too; for players three and
  four (`bOwnInput`): `SetFocus`/`FocusFirst` (:62, :68) do nothing, `Load` (:86) is Epic's without the focus call,
  `ShowList` (:111) draws the list in use as focused (its `MenuState`, which `ExplosiveDrawItem`/`EnergyDrawItem` read),
  `PadInput` (:138): up/down in the list (`SetIndex`, past an end to the other list), left/right, A/Start
  (`ChooseExplosive`/`ChooseEnergy`: Epic's click code without the focus calls; energy before explosive takes the checked
  explosive as Epic's `EnergyOnClick`), B/Back `TakeBack` (:228); `StatusText` (:253) for the log.
- `UC2Arena_Character.CloseComplete`: more than two players -> `UCMenu.UC2Arena_QuadArmory`; one or two as Epic's.
- The owner's change (2026-10-11, during part B): the line under the honeycomb shows no character names. Nobody there:
  "PLAYER 3 / 4" + "PRESS START" (as part A); joined, choosing: "3P"; chosen: "3P READY" (Epic's own `Localizer.sReady`,
  localised in all five `Engine.<lang>`: int "Ready", deu "Bereit", fra "Pr�t", ...: no English word of ours); not
  there: "4P PRESS START". `UC2Arena_Character.UpdatePlayers34Text`; x1menu4's text checks follow (`'3P | 4P'`,
  `'3P READY | 4P READY'`). Picture `logs/x1/page-4-ready.png` (re-taken).
- Layout measured on the pictures (1280x720, Fit43): four quarters between the header and the button bar, every weapon
  name readable (condensed), the loadout icons light up as Epic's when chosen; the loadout captions overlap their icons
  a little ("usable, not pretty"). No "1P..4P" label per quarter: the corners are the cursors' tag corners.
- Behaviour decided here (owner to confirm): B of player three or four with nothing chosen does nothing (they cannot
  send everybody back to the character page); players one and two cannot go back while three or four have chosen
  something (Epic's rule for the other panel); the mouse is not handled on quarters three and four.

### Tests (tools; file:line at the commit)

- `tools/dev/g6-splitscreen.sh x1armory` (`scen_x1armory`; X1A_PLAYERS="4 3" by default): the menus to the character page,
  pads 1-3 (or 1-2) join, everybody chooses, the 2x2 armory: n quarters (`page opened` line), player three on its
  explosive list; player three's B with nothing chosen keeps the page (no character page opens); pad 2's down moves only
  quarter three (the line says `player 3 port 2 XC_PadDown`), its A chooses the explosive (on to the energy list), down,
  A: ready; B takes the energy back, A: ready again with the same weapons; pad 3 the same for quarter four; no launch box
  while players one and two choose; pad 1's down moves only quarter two (Epic's focus path), the keyboard's only quarter
  one; then players one and two choose (A/Return), the launch box, the match: n split views (G-6 layout, split HUDs), the
  split players' spawn URLs carry `?ExplosiveWeapon=`/`?EnergyWeapon=` of what three and four chose (and two), NETPROBE's
  PlayerReplicationInfo of `Player_3`/`Player_4` has those weapons, which are not the default (the joining player's
  profile weapons from the first spawn URL on the character page: RocketLauncher/SniperRifle), three and four differ;
  0 out-of-bounds lines. Presses are `x1tap` (0.12 s): x1press's 0.3 s held a d-pad over the GUI's repeat (1.5 x
  ButtonRepeatDelay 0.19 = 0.285 s) on a loaded machine and stepped twice (measured, run r1: quarter four took the
  defaults). Helpers `tools/dev/g6/x1-checks.py` (`x1a_*`, `x1_match_urls`, `x1_pris` (NETPROBE goes to the engine's
  `Avaris.log`, not the console), `x1_default_loadout`).
- `x1menu4` (part A): the armory step is now the 2x2 armory, players three and four choose (A twice each, ready) before
  the loop of players one and two (rebuilt tree; host variant too: the system-link host gets the 2x2 armory as well).
- `menu` (G-6): two players get Epic's `UC2Arena_DualArmory` (a check, the log read directly: a probe moves the
  scenario's expect position past the OpenMenu line) and its picture `g6-menu-armory-2.png`.
- `tools/dev/net-test.py splitclient` (`tools/dev/net-test.sh` opt-in; below).

### Results (labelled)

| what | where | result |
|---|---|---|
| `g6-splitscreen.sh x1b x1armory`, four players | `logs/x1b/a2` | **measured** PASS 37/37 (0 Critical, 0 out-of-bounds) |
| the same, three players | `logs/x1b/a2` | **measured** PASS 28/28 |
| the loadout proof (four players, run a2) | `x1-armory4.inject` | **measured**: spawn URLs `Player_3 ... ExplosiveWeapon=UCWeap_CanisterGun_Gren EnergyWeapon=UCWeap_ShockRifle_New`, player four RazorJack (Ripjack)/CanisterGun_Bio; NETPROBE pri lines the same; default RocketLauncher/SniperRifle |
| `x1menu4` on x1b (rebuilt: standalone + host) | `logs/x1b/a2` | **measured** PASS 58/58, PASS 43/43, out-of-bounds `{}` |
| `x1menu4` on x1bship (Epic's GUI.u/UCMenu.u, this binary) | `logs/x1b/v1` | **measured** PASS 19/19 (two players in the menus; 167 FocusedControl lines as part A's "before") |
| `menu`, `kbplayer`+`kbmenu` | `logs/x1b/reg3` | **measured** PASS 10, 34, 12 (the dual armory for two) |
| `h3menu` (ticked, unticked), `input`, `unplug` | `logs/x1b/reg` | **measured** PASS 11, 13, 39, 26 (scripts before the owner's text change; the change touches only the line under the honeycomb, which they do not read) |
| `m14-scripts-check.sh verify menu patcher deadbolt` (M14_PREFIX=x1b-m14) | `logs/x1b/m14b.txt` | **measured** PASS all four (verifyclasses: all layouts match) |
| P2 | `logs/x1b/p2-write.log` | reference regenerated: UCMenu.u 2500765 B `a125b78b...`, GUI.u and UCGameplay.u unchanged; PASS |
| `check-headers` | `logs/x1b/check-headers.log` | **measured** current (no header change) |
| pictures | `logs/x1/armory-{2,3,4}.png` | **seen**: Epic's two panels; three quarters (the fourth empty); four quarters, chosen weapons lit |

### The bounded client test (time box 1 h; used about 20 min): BROKEN

`NET_TREE=x1bnet NET_PORT=7871 tools/dev/net-test.sh splitclient` (logs `logs/x1b/client/`, one 6G scope, two
processes): a listen host (uc2.bin DM-Deadbolt?Listen, null renderer) and one GL client under Xvfb that walks its menus
with player one's pad (VERSUS -> DeathMatch -> DM-Deadbolt -> the character page), pad 1's Start joins player two
there, then the client connects (console `open 127.0.0.1:7871`).
- **measured** PASS: the client's menus reach the character page; `Split screen: pad 1 Start in the menus: joined`
  (two split views on the client); the client joins the host (`Join succeeded`; its LOGIN URL carries Epic's
  `NumClients=2`, the host's GameInfo logs `NumClients 2`); 0 Critical/assert/overflow; nothing left listening.
- **measured** FAIL: no `JOINSPLIT` reaches the host; the host has two human players (itself and the client's player
  one); after the welcome the client logs no `Split player spawn URL` line, i.e. `USplitScreenMgr::CreateSplitPlayer`
  never ran for the client's second viewport, so the JOINSPLIT it would send (`UnSplitScreenMgr.cpp` client branch,
  after `NotifyReceivedClientPlayer`, `UnConn.cpp HandleClientPlayer`) never goes out. The client's split view is gone
  by then (no "Split screen: N views" line after the travel).
- Not a one-liner, not fixed. Where to look (read only): what empties `ActiveSplitViews` on the client between the
  `open` and `HandleClientPlayer` (candidates: `MatchSplitViewportsToActors` running while `GLevel` is still the menu
  level -> `IsMenuGameActive` -> `ResetState`, which logs nothing; the GUI's `NotifyLevelChange` removing child
  viewports). A `DevSplitScreen` line in `ResetState` would settle it.
- Also measured on the way: on a client a pad's Start in the match makes nobody (`USDL2Client::PadJoin`: G-6's in-match
  join is for a standalone game or a listen host, by design), and the engine's `SPLIT ADD ID=1` makes nobody in a match
  either (`UClient::BeginSplitScreen` asks the GUI's `AllowControllerJoin`: the character page only). So on a client
  split players can only come from the menus, and that path is the broken one.
- Never exercised: a client with three or four local players, a dedicated server.
