# bot-variants

UCMenu + UCGameplay: the character page's second switch. Eight fighters of `UCCharacters.u` have no slot of their own on
the character page (`UC2Arena_Character`); each is now the bot of one slot of its own weight class, so the slot's health
and agility bars stay true for it. The pad's Back switches a slot between its own character and its bot; X stays the
switch to the slot's X variant (the Xbox Live skin). A slot shows one of the three at a time.

| Slot (index) | X variant | Bot | Class | Weapons (race / melee index) | Combos (`ComboClasses` indices) |
|---|---|---|---|---|---|
| Raptor (2) | Syzygy | Corrosion | `UCPawn_Liandri_Corrosion` | 4 / 5 | 4, 20, 34, 37, 31, 24, 0 |
| Torgr (6) | Garek | Korig | `UCPawn_Skaarj_Korig` | 9 / 14 | 3, 20, 21, 25, 31, 22, 0 |
| Anubis (9) | Merc Anubis | Mirage | `UCPawn_Nakhti_Mirage` | 7 / 4 | 26, 20, 9, 15, 31, 40, 0 |
| Sobek (8) | Apophis | Hyena | `UCPawn_Nakhti_Hyena` | 7 / 4 | 26, 20, 6, 15, 31, 40, 0 |
| Selket (11) | Necris Selket | Nepthys | `UCPawn_Nakhti_Nepthys` | 7 / 10 | 26, 20, 10, 15, 32, 40, 0 |
| Raiden (5) | none | Ra | `UCPawn_Nakhti_Ra` | 7 / 7 | 26, 20, 13, 15, 31, 40, 0 |
| Sapphire (14) | Calypso | Memphis | `UCPawn_Nakhti_Memphis` | 7 / 10 | 26, 20, 6, 15, 32, 40, 0 |
| Malcolm (1) | none | Darius | `UCPawn_Nakhti_Darius` | 7 / 7 | 26, 20, 10, 15, 31, 40, 0 |

The weapons are each class's `RequiredEquipment` (its own or inherited), the combos its `LoadSpecificReferences`
ComboList in order; `UCCombo_Heal_Light` (Nepthys, Memphis) is 15, Heal, as for Selket in `CharacterInformation`, whose
combo table has no light Heal.

- `new/UCMenu/Classes/BotVariants.uc` (our own text): the table above (`Bots`), and its lookups: a slot's bot, a class's
  bot, the bot's index among the characters a profile unlocks (`UnlockIndex`: after the 15 slots, one per
  `CharacterInformation.BotNames` entry), its icons.
- `UCMenu.diff`
  - `UC2Arena_Character`:
    - The switch: `BackClicked` (the pad's Back; the keyboard's C through `[GUI.EmulatedJoypad]`, which the engine
      passes as Back only to a page that shows a Back hint, so C on any other page backs out of nothing; a click on its
      hint, which the engine's mouse turns into its pad button) switches the slot under the player's cursor between its
      own character and its bot, from the X variant too, and Back never leaves the page (B does). With no bot
      to switch to it does nothing; with the player's handicap panel up it is refused with the error sound, as X is.
      `XClicked` from the bot goes to the X variant. Players three and four keep their own Back (players34: they leave).
    - The state: `BotSlotMask`, a bit per slot that shows its bot, a state of the slot as the X variant's
      (`ToggleAltSkin`) is, which both players see; `BotMask`, the slots whose bot is unlocked (`UpdateBotMask`, in
      `CreateIcons` and `Refresh`): the game's own unlock, the bot's bit in a profile's character mask (the bot lists'
      rule), or the Unlock-all-content flag (`bUnlockCharacters`), as `UC2PageBase.ValidCharacterIndex` answers.
    - The hint: `btn_BotChar[2]`, one per player as `btn_AltChar`, above the X hint (`PlaceBotHints`; in its place on a slot
      without an X variant; centred on it in split screen). Shown while the slot's bot is unlocked: usable on a slot the
      player has, shown unusable on a locked slot; hidden on Random, on a slot without a bot, with a locked bot, while the
      handicap panel is up. Each hint names the character its button switches to, or the slot's own one while that is
      shown; switching between the bot and the X variant updates both. Another player's bot hint is inert to the mouse
      (`MouseInert`), as their X hint is.
    - What the bot shows: its name over the preview, its icon on the slot (`SlotIcon`: `Portrait_Icon_<Name>`, Corrosion's
      `_0`/`_1`), the preview's static in place of the turntable movie (`PreviewScreen.ClearMovie`), no announcer call,
      the stats panel with its weapons and its first and third combos and the slot's bars, the White page with its
      weapons and combos and no bio.
    - The choice: `SaveCharacterSelections` keeps the slot's own character in the profile (`SetCharacterIndex`, no
      variant) and `SetBotChoice` puts the bot's class into the player's URL as `BotClass` (`GameStart`'s entry for the
      player, which `UpdatePlayerURL` and `GetSpawnURL` hand on), or removes it (and clears it from the player's URL).
      So the profile, the URL's Class, and the `[DefaultPlayer] Class` the engine writes back to User.ini without a profile
      never hold a bot, and the next visit shows the slot's own character. A random pick is never the bot.
    - Log (DevUI): `bot-variants: player <n> switched slot <i> to its bot <Name>` / `to its own character`, and
      `bot-variants: player <n> chose the bot <class>` / `chose no bot`, `; the profile's character <class>`.
  - `CharacterStatsPanel.Update(..., optional string BotClassName)`: a bot's weapons and combos.
  - `UC2CharacterInfoPage.HandleParameters`: a bot's class in place of the variant flag (the White page from the
    character page): its name, its weapons and combos, no bio.
  - `PreviewScreen.ClearMovie`: no movie; the static the panel shows before a movie (the character frame's
    `Portrait_Char_Null_LG` on this page) stays up and flickers in a loop (`LoopStatic`: GUIComponent's looping colour
    frames, between full strength and two thirds of it, a step every few hundredths of a second), until a movie comes.
  - `Armory_PanelBase.ChosenBot` (the player's `BotClass`): the armory's loadout shows the bot's weapons, and
    `Armory_PanelSingle` no character portrait for it.
  - `UC2MidGame_Character.SaveSettings`: a character chosen there during a match replaces the bot (its `BotClass` goes).
  - `ScoreboardGroup_General.GetCharIcon`: the icon of a player in a bot's class (below).
- `UCGameplay.diff`
  - `UCDeathmatch.GetPlayerClassName`: a player logs in with the class of their URL's `BotClass` when it is a pawn class
    (a ladder rung's own character comes first); else with Class. The host decides, so a guest's bot plays on a
    host with this set. The login URL (a network join's too) and the split-screen spawn URL carry it.
  - `ScoreboardPanelBase`, `ScoreboardPanelSurvival`, `UCHUD_Survival`: a player in a bot's class gets the bot's icon:
    `CharacterInformation.GetCharacterIcon` knows a bot by its name only, which is the last part of its class name
    (`UCPawn_Nakhti_Ra`: Ra).

- **Not saved**: the bot choice lives in the page and the player's URL; nothing writes it to the profile or an ini.
- **Names**: Memphis, Nepthys, Darius, Ra, Korig and Corrosion inherit their parent's `sCharacterName` (Selket, Apophis,
  Torgr, Raptor). It is read by `UCDeathmatch.EndGame` (a ladder rung only, which never plays a bot of this page) and
  `UCScoreboard_Arcade` (no game type uses it).
- **Ini keys**: none. **Depends on**: players34 and menu-mouse (its diff is made on their text; both depend on
  epic-fixes).
- Generated by `tools/dev/uc-patches-regen.sh`.
