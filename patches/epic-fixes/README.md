# epic-fixes

Epic's 30 load-time script fixes, written as script. The Xbox title update could not replace the `.u` files on
the disc, so Epic shipped these fixes as bytecode inside the executable (`Source/Engine/Inc/ScriptPatches.h`) and
patched the packages in memory as they load (`Engine/Src/UnScriptPatcher.cpp`). That patcher stays: the shipped
packages keep getting these fixes in memory. A **rebuilt** package is not patched in memory (it is not the file
the patch was made for), so it must carry the fixes itself: that is this set.

- **Generated** by `tools/dev/uc-patches-regen.sh`: the base against `uc-decompile.py --patches`, i.e. the
  package as the load-time patcher leaves it, decompiled. UCMenu (text base): the three classes the patch table
  names, as Epic's later source compiles (our compiler's preprocessing of it gives Epic's retail text byte for
  byte, so these hunks are in the same form as the base).
- **What it changes** (21 functions/states replaced, 9 objects added): Engine PlayerController (ClientSetHUD,
  ReceivedViewport, RoundIntermission.EndState + its local `P`), CharacterInformation.FindComboIndex; UnrealGame
  DeathMatch.EndGame; UCGameplay UCPawn (AnimateStanding, LoopIfNeeded), UCPlayerController.SetThirdCam,
  UCWeap_Melee.BusyShieldOn (EndState, HitWall), CTFFlag.home, OverflowBall.PostBeginPlay; UCInventory
  UCWeap_SniperRifle.ServerSetZoomLevel; UCMutators GameRules_WeaponMaster.ScoreKill, MutRadioactive.Reset and
  WeaponMasterReplicationInfo.Reset (new); GUI GUIController (SetProfilePlayerName, InitializeController); UCMenu
  UC2Arena_Character (InternalOnReopened, BClicked), ControllerRemovedMenu (Opened, new InternalOnOpen),
  ConnectionFailureMenu (InternalOnOpen, MessageBoxClicked, new Timer).
- **Proof** (P1, `tools/scripts/uc-patches-proof.sh p1`): base + this set, compiled and stripped, equals the
  shipped package + the patcher for all 7 packages (script-diff.py: problems 0; every one of the 30 entries
  identical to the patcher's bytecode/object). What the patcher cannot do and a compiler does anyway shows as
  findings: CTFFlag.home's LabelTableOffset (the patcher left the shipped 16), ConnectionFailureMenu's ProbeMask
  (the new Timer is probed), Engine.NetworkDevice's config default (the static build's).
- **Packages**: Engine, UnrealGame, UCGameplay, UCInventory, UCMutators, GUI, UCMenu. Only those another set
  changes are rebuilt by default (GUI, UCGameplay, UCMenu, UCMutators); see ../README.md to rebuild the others.
- **Ini keys**: none. **Depends on**: nothing; every other set depends on it.
