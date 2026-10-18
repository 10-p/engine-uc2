Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# Unlock all content (ufront, owner 2026-10-12)

The launcher's Advanced "Unlock all content" button (ufrontend 2.86, target data `unlockAll`) writes
`-ini:System:[GUI.GUIController]:bUnlockAll=True` once. This note is what UC2 locks, why the old ini tweak does not work,
and what the engine does with the new key. Line numbers: `build-native/Scripts/tree/` (T/, the decompiled and patched
scripts the build makes from your own `.u`) and a decompile of the shipped `Engine.u` with `--patches` (PlayerProfile).

## What UC2 keeps locked, and where it records unlocks

Every unlock is recorded in the PROFILE (`System/Profiles/<name>/AVARIS_PROFILE.PRO`), none in an ini:

| Content | Recorded in | Earned by | The menus' gate |
| --- | --- | --- | --- |
| 7 characters: Gorge, Malcolm, Raiden, Torgr, Brock, Selket, Devastation | `PlayerProfile.AvailableCharacterMask` bits 0-14 | Selket: Ascension's last rung; Devastation: Ascension_13; Gorge, Brock, Torgr: the last rung of Malcolm's, Lauren's, Szalor's Tournament; Raiden: the last challenge; Malcolm: beating a human in all six game types (`UnlockableMask` bits 20-25) | `UC2PageBase.ValidCharacterIndex` (T/UCMenu, about :328-359), `UC2SP_CharacterPanel.ValidCharacterIndex` (:496-517) |
| 13 bots (Apophis, Calypso, Corrosion, Darius, Hyena, Jackhammer, Judas, Korig, Kraag, Memphis, Mirage, Nepthys, Syzygy) | `AvailableCharacterMask` bits 15+ | given rungs (`UC2RungDone`) | `UC2Arena_BotConfig` -> the same `ValidCharacterIndex` |
| 19 mutators | `AvailableMutatorMask` | the first win of a rung that uses one | `Arena_PanelMain.RefreshMutatorLocks` (:685-703) |
| Anubis's Tournament, rungs, challenges | `RungProgressions`, `ChallengesCompleted` | finishing Ascension; rungs in order | `PlayerProfile.IsLadderUnlocked`, `UC2SP_Main.RungLocked` (:935-944), `UC2SP_ChallengesPanel` |
| Extras: the Ascension cinematics (credits and trailer are always there) | ladder progress | Ascension | `UC2Extras.LoadSettings` (:152) |
| Xbox Live extras: 10 alternate skins, 4 maps (DM-EvilHands, CTF-Monoxide, DM-Sever, OD-Ascension) | not in the profile: Xbox Live activation files | Xbox Live only | `UBaseGUIController::HasContent` (C++) |

Award writers (`UC2RungDone.CheckPendingAwards`, the Malcolm unlock in `UCPlayerController`, `PlayerProfile.Unlock*`)
test the PROFILE, never the menu flags, so an override must stay in the menus' gates: put into the profile's getters it
would make the game skip awards the player really earned.

## Why `bUnlockCharacters=True` does nothing

`GUIController` has Epic's debug flags `bUnlockCharacters`, `bUnlockRaiden`, `bShowAllMuts`, `bLaddersLocked`, read by
the gates above (`bUnlockCharacters`: characters, bots, alternate skins; `bUnlockRaiden`: Raiden; `bShowAllMuts`:
mutators; `bLaddersLocked=False`: every ladder, rung, challenge and the Extras cinematics). Only `bUnlockCharacters` and
`bShowAllMuts` are `config`, and Epic's title update clears `bUnlockCharacters` and `bUnlockRaiden` at the top of
`InitializeController` (`Source/Engine/Inc/ScriptPatches.h`, `GUI_GUIController_InitializeController`; the same in
`patches/epic-fixes/GUI.diff`), so the PC community's ini tweak (the runesofwar guide) has no effect on the title
update's scripts; only the console execs (`UnlockChars`, `UnlockCharsPlusRaiden`, `ShowAllMuts`, `UnLockLadders`) set
them, for the session.

## What the engine does (`Source/Engine/Src/UnInteraction.cpp`)

- `UBaseGUIController::InitializeController`, after `eventInitializeController()` (so after Epic's reset):
  `[GUI.GUIController] bUnlockAll=True` in the System ini sets the four flags on the controller in memory, by name
  (`FindField<UBoolProperty>`), and logs `Unlock all: every character, bot, mutator, ladder and cinematic is open
  (GUI.GUIController bUnlockAll=True, 4 of 4 menu flags set)`. Nothing calls `SaveConfig` on the controller, so the
  flags never reach a file; the profile is never touched, so what the player wins is recorded as always and survives
  the key being turned off in the ini. The owner's rule: wins in a ladder the key opened count like any other.
- `UBaseGUIController::HasContent`: off the Xbox nothing ever activates Xbox Live content (`FContentManager::Initialize`
  and the activation files are Xbox-only, so `IsActivated` is always false), while the extras' files ship in the game
  folder. Off the Xbox, whatever is installed is available (owner, 2026-10-12), key or not; Epic's branch is kept for
  `_XBOX`. A mod's mutator from another package is no longer hidden either.

No script changes: the flags and their gates are Epic's.

## Proof

ufrontend E2E `uc2 config › unlock-all` (`packages/e2e/lib/unlock-all-test.js`): the button's seed on argv, the
`Unlock all:` line, `GETALL GUIController bLaddersLocked` answering `GUIController Package.GUIController.bLaddersLocked
= False` on the live controller, and the key read back from the engine's Avaris.ini after a reload. Not observed in a
running game: the extras branch (nothing on the console reaches `GameInfo.GetMapList` without writing the profile).
