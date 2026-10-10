# B-3 notebook: where the split-screen HUD sits (the owner's question of 2026-10-10)

Worktree `/home/ion/WorkUC2/engine-uc2-b3`, branch `b3-split-hud` (from main 81fd44e: G-6 and G-9 merged). Run trees
`run/b3` (the asset tree's rebuilt packages) and `run/b3ship` (`UC2_SCRIPTS=assets/uc2-shipped/System`: Epic's GUI.u,
UCGameplay.u, UCMenu.u); logs and pictures `/home/ion/WorkUC2/logs/b3/`. Every run through `bin/capped` (game 2G,
builds 6G, `-j2` above load 24).

The owner's words (picture `logs/g6/owner-split-hud-report.png`, the web preview, two players, `KeyboardIsPlayer=False`):
"Take a look at the position of the UI with split screen mode. You'll see that it is not aligned to the top, the health
bar has a lot of space on the top where the bottom is squished. Moreover, is the reticle aligned to the split screen
center. The fascinating thing is that the reticle looks center aligned, but the health looks for player 1 lower than
player 2. Take a look at the original vanilla game, how does it work in 4:3 split screen? Is it the same? If the
behaviour is exactly the same then maybe we can let it be."

## Step 0 - what the code says (2026-10-10)

File:line in this tree.

Epic's split HUD (scripts; identical in the shipped packages as far as the source tells):
- `PlayerController.uc:1078` `ClientSetHUD`: with more than one client viewport (`GetSplitscreenLayout`, the index in
  `Client->Viewports`, the overlay counted) it spawns `<HUD>_Split` and calls `SetSplitPosition(ViewportIndex, Count)`.
- `UCHUD.uc:2839` `SetSplitPosition` ("if we are going to support more than 2 split-screen, this function needs to be
  adjusted"): `SplitScaleY = 0.5` (used only by the debug safe-zone box), `SafeZoneOffsetY = 0` for index 0 and `-29`
  for index 1, nothing for other indices; the offset goes to every `CanvasElement` (`CanvasElement.uc:48`: "necessary
  for bottom split-screen (since it doesn't have an upper safezone)") and the scoreboard.
- `UCHUD_Split.uc`: the 640x240 layout of a half screen (the Xbox's top/bottom halves of 640x480): health at
  `pos_HealthBackground (X=225,Y=32,W=-185,H=17)` i.e. x 40..225, y 32..49; adrenaline y 43..52; the weapon selectors
  (`Element_WeaponSelector_*_Split`, `DrawY=300 ScaleY=0.53` = 159 + the list); the compass at y 148..252
  (`pos_CompassCardsRot`). `UCHUD.uc:108` `SAFE_ZONE_X=46 SAFE_ZONE_Y=34`.
- The reticle: `UCWeapon.uc:3504` `CreateScriptedCrosshair` gives the crosshair `ScaleY=0.5` under a split HUD, so its
  `DrawY=240` becomes 120 = the centre of a 240-high half.

So Epic's two-player screen (640x480 = two 640x240 halves) is laid out as ONE screen with a safe zone around its outer
edge: the top half's HUD starts 32 px below the screen's top and runs down to the split line (its weapon list ends at
the half's bottom edge), the bottom half's HUD is moved up 29 px, so it starts right below the split line and ends ~32
px above the screen's bottom. Within its own half, player one's health is 29 px lower than player two's: by design.

Ours:
- `UnSplitScreenMgr.cpp:860` `UpdateSizes`: Scion's layouts (2 = top/bottom halves; 3 = top half + two bottom
  quarters; 4 = quadrants), the overlay last in `Client->Viewports` (so two players are indices 0 and 1, as Epic's).
- `UnCanvas.cpp:1007` `ApplyUIBox`: the design space of a split viewport is 640 x 480*ScaleY (640x240 for halves and
  quarters), fitted (scale `min(W/640, H/240)`) and centred in the viewport's rectangle. A half of a 16:9 window
  (e.g. 1920x540) gets a 1440x540 box: exactly the half of the full-screen 1440x1080 4:3 box, i.e. Epic's 4:3 two-player
  screen scaled and pillar-boxed. A quarter of a 16:9 window (960x540) gets 960x360, centred: 90 px empty above and
  below.
- `UnSplitScreenMgr.cpp:1031` `RefreshPlayerHUDs` (G-6): after a join/leave in a match, ClientSetHUD again and the
  cached crosshairs' ScaleY set to 0.5 / the class default.

Expectation before measuring: two players at any window size reproduce Epic's 4:3 split exactly (scaled); three and
four players are our extension of a layout Epic never shipped (the index-1 offset lands on the top-right quarter in the
four-player layout and on the bottom-left quarter in the three-player one). To prove with pictures: the next steps.

Plan: (1) a `hud` part in `tools/dev/g6-splitscreen.sh` (GPU, frozen frame, the HUD alone with TOGGLERENDERLEVEL, 1-4
players) and `tools/dev/g6/b3-hud-measure.py` (per viewport: health bar, weapon list, score box, reticle, in design
pixels through the viewport's UI box); (2) Epic's reference: shipped packages, 640x480, `UIScaleMode=Native640`;
(3) ours: rebuilt packages at 640x480, 1280x720, 1920x1080 with Fit43; (4) conclude, fix if ours; (5) regressions.

## Steps 1-3 - measured: Epic's 4:3 split against ours (2026-10-10)

Build: this worktree unchanged (= main 81fd44e), `bin/capped 6G 5400 -- cmake --build build-native -j4` rc 0
(`logs/b3/build-native-0.log`); trees `run/b3` (rebuilt packages, UCGameplay.u sha256 45dd2558...) and `run/b3ship`
(Epic's shipped GUI.u/UCGameplay.u/UCMenu.u, UCGameplay.u sha256 c675a442..., = `assets/uc2-shipped/System`).

The test (new parts of `tools/dev/g6-splitscreen.sh`, G-6's `KeyboardIsPlayer=False` model as on the web):
- `hud` (GPU, SDL offscreen, ShotRamp): DM-Deadbolt, pads join in the match one by one (1, 2, 3, 4 players), every
  player placed at G-6's fixed spots, 8 s for the join/spawn messages to go, time frozen, the picture and the HUD alone
  (TOGGLERENDERLEVEL), the viewports probed. `tools/dev/g6/b3-hud-measure.py` maps every viewport's pixels back into
  the design space its canvas gives the HUD (as `UCanvas::ApplyUIBox`) and takes per element the bounding box of a
  colour in a design region: health = the green fill, weapons = the lit list at the lower left, score = the rank box,
  reticle = the crosshair's bright pixels around the design centre; the reticle's centre against the viewport's centre.
  `b3-hud-compare.py` tabulates the runs against the reference; `b3-hud-sheet.py` makes one sheet per layout.
- `hudmenu` (Xvfb): G-6's menu flow (player two's Start on the character page, Epic's own way to two players: the split
  HUDs are made at the map's start), the match's HUD alone, measured the same way.

Runs (all `PASS b3-hud` 7 checks / `PASS b3-hudmenu` 9 checks, 0 Critical, load 13-31: frozen-frame pictures):
`logs/b3/epic640` (b3ship, 640x480, `UIScaleMode=Native640`: the Xbox's own screen and Epic's script untouched by any
UI box: THE REFERENCE), `ours640` (b3, 640x480, Fit43), `ours720` (b3, 1280x720), `ours1080` (b3, 1920x1080),
`ship1080` (b3ship, 1920x1080, Fit43), `epic640menu` (b3ship 640x480 Native640, menu flow), `ours720menu` (b3,
1280x720, menu flow). Table: `logs/b3/compare-before.txt`.

Two players, top-left corner of each element in design pixels of the 640x240 half (x, y), and the reticle's centre
minus the viewport's centre:

| viewport | element | Epic's intent (script) | Epic 640x480 shipped | ours 640x480 | ours 1280x720 | ours 1920x1080 | shipped .u 1920x1080 |
|---|---|---|---|---|---|---|---|
| top (player 1, index 0) | health fill | (39, 33) | (40.0, 33.0) | (40.0, 33.0) | (40.0, 32.7) | (40.0, 32.9) | (40.0, 32.9) |
| | weapon list | (38, 155) | (38.0, 155.0) | (38.0, 155.0) | (38.0, 155.3) | (37.8, 155.6) | (37.8, 155.6) |
| | score box | | (290.0, 32.0) | (290.0, 32.0) | (290.0, 32.0) | (290.2, 32.0) | (290.2, 32.0) |
| | reticle - centre | (0, 0) (anchor 320,120) | -1.00, 0 | -0.50, 0 | -0.33, 0 | -0.67, 0 | -0.89, 0 |
| bottom (player 2, index 1) | health fill | (39, 4) = 33 - 29 | (40.0, 4.0) | (40.0, 4.0) | (40.0, 4.0) | (40.0, 4.0) | (40.0, 4.0) |
| | weapon list | (38, 126) | (38.0, 126.0) | (38.0, 126.0) | (38.0, 126.7) | (37.8, 126.7) | (37.8, 126.7) |
| | score box | | (283.0, 3.0) | (283.0, 3.0) | (283.3, 3.3) | (283.1, 3.1) | (283.1, 3.1) |
| | reticle - centre | (0, 0) | -1.00, 0 | -1.00, 0 | -0.67, 0 | -0.67, 0 | -0.67, 0 |

(weapon list's script position: `Element_WeaponSelector_*_Split` DrawY 300 x ScaleY 0.53 = 159, the list's frame
starts 4 px above; the reticle's -0.3..-1 design px is the crosshair art's own off-centre, D-2 step 4, the same in
Epic's picture.) Largest difference from Epic's picture, every element, every size: **0.7 design px** (1 screen pixel
of rounding at 1.5x/2.25x). `PASS b3-hud-{ours640,ours720,ours1080,ship1080}-as-epic640`. The menu flow gives the same:
Epic 640x480 health (40,33) / (40,4), ours 1280x720 (40,33.3) / (40.7,4.7), reticle -0.5 / -0.67 design px.

What this says about the owner's three observations (picture `logs/b3/sheet-n2.png`: Epic at 640x480 | ours at
1920x1080 | ours with Epic's shipped scripts at 1920x1080):
- "player 1's health lower than player 2's": **Epic's script** (cause b): `UCHUD.SetSplitPosition` gives the
  bottom half (viewport index 1) `SafeZoneOffsetY=-29`; Epic's own 4:3 picture shows player one's health at y 33 and
  player two's at y 4 of their halves, exactly as ours.
- "a lot of space on the top where the bottom is squished": the same design. Epic laid the two halves out as one TV
  screen with the safe zone (`SAFE_ZONE_Y=34`) at the screen's outer edges only: the top half keeps 32 px above its
  health bar and its weapon list ends at y 238 of 240, on the split line; the bottom half starts at the split line
  and ends 31 px above the screen's bottom. Same numbers in Epic's picture.
- "is the reticle aligned to the split screen centre": yes, in both: the crosshair's anchor is the half's centre
  (320, 120: `CreateScriptedCrosshair` ScaleY 0.5) and the measured centre is 0.3-1 design px left of it (the art).
- Our UI box (cause a) adds nothing for two players: a half of a 16:9 window gets the 640x240 box at the half's full
  height, pillar-boxed, which is precisely the half of the single player's 4:3 box: Epic's 4:3 screen scaled. The HUD's
  own scaling (cause c) is not involved (`SplitScaleY` only sizes the debug safe-zone box).

**Conclusion for the owner: two-player split screen is identical to the original game's 4:3 split screen (every HUD
element within 0.7 design pixels, at 640x480, 1280x720 and 1920x1080, with our packages and with Epic's): it can be
let be.** Nothing changed in the engine or the scripts.

Seen on the way (not placement):
- The reticle's SIZE differs between runs (player one's 37-38 x 14 in the rebuilt-package runs, 51-52 x 20 with the
  shipped ones; 29 x 10 in both menu-flow runs; player two 48-58 wide): the crosshair's animation state at the frozen
  moment (`CanvasElement` ScaleX/ScaleY move positions only, sizes come from the element's scale actions). Its centre
  is the same in all. Seen only; why the rebuilt package's player one sits in the small state more often was not
  investigated.

## Three and four players: not Epic's, ours to decide (owner question)

Epic's scripts lay out halves only ("if we are going to support more than 2 split-screen, this function needs to be
adjusted"), and UC2's menus stop at two players. The 640x480 shipped run shows what Epic's code does with Scion's
quarters on its own screen (`logs/b3/sheet-n3.png`, `sheet-n4.png`, left column): the 640-wide half layout drawn into a
320-wide quarter, the right half of the HUD (score box, the reticle at 137 px right of the quarter's centre) cut off.
Ours (G-6) fits the 640x240 layout into each quarter, centred (at 1920x1080 a 960x540 quarter gets a 960x360 box, 90 px
empty above and below; at 1280x720 640x240 in 640x360, 60 px), reticle at the quarter's centre (-0.3..-1 design px).
What is not consistent: `SetSplitPosition`'s -29 goes to whatever has CLIENT index 1 (`Client->Viewports`, which
Scion's `ZSortViewports` orders with an unstable sort among equal `OffsetZ`): four players -> the top-RIGHT quarter's
HUD is 29 design px higher than the other three (health y 4 against 33); three players -> the bottom-RIGHT quarter's
is, not the bottom-left's. Options (not built, the owner chooses):
1. leave it (G-6 as merged);
2. Epic's rule by row: every view whose top edge is the split line gets the bottom half's offset (3 players: both bottom
   quarters -29; 4 players: both bottom quarters -29, top ones 0); two players unchanged. Natively, without touching
   the scripts, by giving `ClientSetHUD`'s `GetSplitscreenLayout` call the view's row (0 top, 1 below) as the index
   when more than two views are up (the other callers keep the client index, which they use as "first player");
3. no offset for quarters (all four like the top-left one).

## B-3b - the owner's choice: Epic's bottom-half rule by row for three and four players (2026-10-10)

Worktree `/home/ion/WorkUC2/engine-uc2-b3b`, branch `b3b-split-rows` (from main 65a73d9, B-3 merged). Logs
`/home/ion/WorkUC2/logs/b3b/`, run trees `run/b3b`, `run/b3bship`. The owner chose option 2: every view whose top edge
is the split line (the bottom half, the bottom-row quarters) gets Epic's bottom-half safe-zone offset (-29), a view at
the screen's top edge none; two players byte-identical to today.

Path taken, and why: the shipped `UCHUD.SetSplitPosition(ViewportIndex, TotalViewports)` knows nothing but the index
(0 -> offset 0, 1 -> -29, anything else -> left as the freshly spawned HUD has it, 0), and `ClientSetHUD` takes that
index from the native `GetSplitscreenLayout` (the view's place in `Client->Viewports`). The scripts must stay as
shipped, so the engine hands `ClientSetHUD` the index whose offset Epic's script applies: the view's ROW in the layout.
- `USplitScreenMgr::HUDLayoutRow` (`UnSplitScreenMgr.cpp`, declared in `USplitScreenMgr.h`): with three views 0 for the
  top half and 1 for both bottom quarters, with four 0 for the two top quarters and 1 for the two bottom ones (the
  same cases as `UpdateSizes`, by the view's place in `ActiveSplitViews`, not by its rectangle, which is not laid out
  yet when a joining player's HUD may be made); -1 with two views or fewer.
- `APlayerController::execGetSplitscreenLayout` (`UnController.cpp`): when its caller is `ClientSetHUD`
  (`Stack.Node`'s name) and the row is >= 0, the row replaces the client index. Every other caller keeps the client
  index: `GameInfo.uc:2461`, `PlayerProfile.uc:762`, `PlayerController.uc:5423/6091` and `HUD.uc:986` use it as "the
  first player" / player order, which must not change. Every way a HUD is made goes through `ClientSetHUD` (login,
  `Possess` re-sending it, G-6's `RefreshPlayerHUDs` after a join/leave, `UCHUD`'s toggle), so all get the row, with
  Epic's packages and ours alike. With one or two views nothing differs from before (the row is -1 and the code path
  is the old one), so two players stay exactly as measured in B-3.
- `tools/dev/g6/b3-hud-compare.py`: the rows check (n3/n4: health top y 33 at the top edge, 4 below the split line,
  reticle within 1.5 design px of the view's centre; `--rows` counts it). On B-3's "before" runs it fails as it should:
  ours720/ours1080 n3 split 1 at 33 (want 4), n4 split 1 at 4 (want 33), n4 splits 2 and 3 at 33 (want 4).

Measured (binary uc2.bin sha256 2d2f3ebb53e8c3f7..., `tools/dev/g6-splitscreen.sh <tree> hud`, GPU, load 19-22, frozen
frames; `logs/b3b/{ours640,ours720,ours1080,ship1080}`, table `logs/b3b/compare-after.txt`): all four `PASS b3-hud`
(7 checks, 0 Critical); `PASS b3-hud-{ours640,ours720,ours1080,ship1080}-as-epic640` (two players unchanged, worst 0.7
design px, the same numbers as B-3) and `PASS b3-hud-{...}-rows` (0 outside). Health fill top y in design px at
1920x1080, before (main 65a73d9, B-3's `logs/b3/ours1080`) -> after; reticle centre from the view's centre after:

| players | view (split order) | row | before | after (rebuilt .u) | after (shipped .u) | reticle after |
|---|---|---|---|---|---|---|
| 3 | top half | 0 | 32.9 | 32.9 | 32.9 | -0.67, 0 |
| 3 | bottom-left quarter | 1 | **32.7** | 4.0 | 4.0 | -0.67, 0 |
| 3 | bottom-right quarter | 1 | 4.0 | 4.0 | 4.0 | -1.00, 0 |
| 4 | top-left quarter | 0 | 32.7 | 32.7 | 32.7 | -0.33, 0 |
| 4 | top-right quarter | 0 | **4.0** | 32.7 | 32.7 | -0.67, 0 |
| 4 | bottom-left quarter | 1 | **32.7** | 4.0 | 4.0 | -1.00, 0 |
| 4 | bottom-right quarter | 1 | **32.7** | 4.0 | 4.0 | -0.67, 0 |

(1280x720 and 640x480 the same within 1 px: 640x480 quarters 32.0/4.0, i.e. half-scale rounding.) The weapon lists and
score boxes move with the offset (bottom row 127/3, top row 155/32). Two players' HUD-alone pictures before/after at
the same size: identical outside the crosshair's animation and the adrenaline meter's blinking segment (0-16 pixels
of 0.9-2.1 million with our packages; up to 5,448 with the shipped ones, all inside those two elements' boxes). The
engine log still names each HUD by client index ("gets its HUD again ... viewport 2 of 5"): unchanged on purpose.
Sheets (Epic 4:3 | before | after | after with shipped .u, picture and HUD alone measured): `logs/b3b/sheet-n3.png`,
`logs/b3b/sheet-n4.png` (n1/n2 for completeness).
