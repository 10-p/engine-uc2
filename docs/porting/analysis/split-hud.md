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
