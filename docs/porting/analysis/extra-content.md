# Added content: maps and mutators a player drops in (ufront, owner 2026-10-15)

The owner: maps added as mods (on the web) or dropped into the game's folders (native) must appear in the map menu, in
a new tab "Extra" next to Custom that lists only them; a mutator dropped in must appear in the mutator menu and work,
its settings in an ini. Test content: two UT2004 maps ported by another agent (CTF-FaceClassic, DM-Deck17) and the
UT2004 mutator jcbShadows, ported here (a deliverable outside this repo: no UnrealScript here).

## Maps: what the menus read

- The map menus (custom match `UC2Arena_Main` -> `Arena_PanelMain`, host `Arena_PanelServerMain`, in-game
  `UC2MidGame_Maplist`) show **maplists only**: `GameInfo.GetMapList(<name>)` reads `Maplists.ini [<name>]`
  (`Maplist` is PerObjectConfig, `config(Maplists)`), except `<Acr>_Custom` (the profile's list, seeded from
  `<Prefix>_Full`) and `<Acr>_RANDOM` (`_Full`, random). No directory scan anywhere; `GUIController.GetMapList` (a
  native FindFiles over the map paths) is used only by the `DumpMaps` exec.
- Tabs: `Arena_PanelBase` constants `MAPLIST_Custom`..`MAPLIST_Random` (0..8), named `<Prefix>_Small` etc.; a tab is
  shown when its list has a `Label` and at least one map. "Custom" is NOT all maps: it is the player's saved list.
- A map's player counts: `GameInfo.GetLevelMaxPlayers/RecommendedPlayers` read the game class's config `LevelData`
  (`Gametypes.ini`, one section per game class); -1 without a record: the map is drawn disabled with a set bot count
  (`ValidMap`), hidden from the in-game list, and with AUTO bots `MaxPlayers` = -1 (no bots).
- Title: `Localize("LevelSummary","Title",Map)` (`Localization/int/<Map>.int`), else the name after the prefix.
  Description: `Localize("LevelInfo","Description",Map)`. Preview: `CharacterInformation.GetMapMovie` by the name after
  the prefix, else the game type's movie.
- Epic's own precedent: the Xbox build merged downloaded maps into `MapLists.ini` from `<Map>.ini [MapListsData]`
  (`FContentManager::UpdateMapLists`, `_XBOX` only).

## Maps: what was done

- **Engine** (`Source/Engine/Src/UnExtraMaps.cpp`, called at the top of `UGameEngine::Init`): an added map is a file on
  a `*.unr` map path that no section of `Maplists.ini` names (the `_Extra` sections excepted) and whose prefix some
  game's `<Acr>_Full` list takes (DM_Full takes DM- and OD-; TDM/NS/SV DM-; CTF CTF-; OD OD-; ACT-, TUT-, Entry, Menu
  match none). In memory only: `[<Acr>_Extra] Label="EXTRA" MaplistTitle="Extra Maps" Maps=...`, and a
  `LevelData=(Name="<Map>",RecommendedPlayers=4,MaxPlayers=8)` in every `Gametypes.ini` section whose LevelData lists
  maps of that prefix, unless the map has a record there already (one the player adds wins). Both files keep their
  Dirty flag, so nothing is written; an `_Extra` section a later save wrote is regenerated each boot (emptied when its
  maps are gone).
  - The game classes read LevelData when they LOAD, and the native packages (UCGameplay too) load at startup, before
    `UGameEngine::Init`: so `RefreshLevelData` sets it again on every loaded GameInfo class, parents first (from its
    own section when it has the key there, as `LoadConfig` does, else its parent's: what a subclass without the key
    inherited). `LoadConfig` itself would not do: UC2 skips a class whose `CLASS_ConfigLoaded` is set.
  - ⚠ `TArray<FString>::AddItem`/`AddUniqueItem` assign into unconstructed memory (they are for types without a
    destructor; `checkSlow` only): the first version crashed in `realloc`. Strings are added with `new(Array)
    FString(...)`, as Epic's code does.
  - Log: `ExtraMaps: DM_Extra: DM-Deck17`, `ExtraMaps: [UCGameplay.CTFGame] 1 LevelData record(s) added`, `ExtraMaps:
    LevelData refreshed in 6 loaded game class(es)`, `ExtraMaps: 2 added map(s)`.
- **Script** (patch set `extra-maps`, `patches/extra-maps/README.md`): the Extra tab, shown right after Custom; a still
  (the null portrait) as an added map's preview; the Custom editor offers the added maps too.
- Native check (run tree `extra-maps`, the two ported maps dropped in, stock `A_Music.uc2cues`): the lists and records
  above; `DM-Deck17?NumBots=3`: `MaxPlayers: 8`, three bots, the map drawn (`logs/extra-maps/deck17-0.png`).

## Music of an added map

- `LevelInfo.Song` -> `GameInfo.PostLogin` -> `PlayerController.ClientSetMusic` -> `Actor.PlayMusic` ->
  `UUC2ALAudioSubsystem::PlayMusic`, which looked the song up as a cue of ONE table, `Sounds/A_Music.uc2cues` (fixed
  name; no directory scan, no ini list). A map's own song could only be added by editing that stock file (the port's
  INSTALL.md said to merge it), and two such maps overwrite each other.
- **Now**: a song the table does not name plays from the wave bank `M_<Song>` (`Sounds/M_<Song>.uc2wb` and the file its
  `data=` names, e.g. `../Music/<Song>.ogg`), the XACT naming the Xbox build opened directly
  (`XACTAudioSubsystem.cpp:1786`, `..\Sounds\M_%s.xwb`): entry 0, looped, at the level of 19 of the 21 stock songs
  (-900 mB, no headroom boost: -1500 mB played). A song in the sound-effect form `A_Music\<Song>` names the same bank.
  Native check: `UC2Audio: music A_Music\Port_DM_Deck17: playing ../Sounds/../Music/DM_Deck17.ogg (... looping, volume
  -1500 mB ...)` with the stock table.
- What an exporter should write (reported to the map porter): `LevelInfo.Song` a bare name (`Port_DM_Deck17`),
  `Sounds/M_<Song>.uc2wb` (`bank name=M_<Song>`, `data=../Music/<Song>.ogg`, `stream=1`), `Music/<Song>.ogg`, and never
  a copy of `A_Music.uc2cues`.

## Mutators

- Already data-driven (no change needed to list one): the mutator page (`Arena_PanelMain.LoadMutators` ->
  `GUIController.GetMutatorList`) reads `GetRegistryObjects("Engine.Mutator")`, i.e. every
  `Object=(Class=Class,MetaClass=Engine.Mutator,Name=<Pkg>.<Class>,Description="<Groups>|<ChangesLoadout>|<TeamOnly>|<UnlockIndex>")`
  of `System/Localization/int/*.int` (`CacheDrivers`, `UnObj.cpp`); the name and description come from that `.int`'s
  `[<Class>] FriendlyName=, Description=`. UnlockIndex must be -1 (an empty field is 0: locked behind the first unlock
  bit). A selected mutator goes on the URL (`?Mutator=Pkg.Class`); `GameInfo.AddMutator` loads any package on the
  `System/*.u` path, no whitelist. The registry is read once per run: a new file shows after a restart.
- **Now**: `UGameEngine::BuildServerMasterMap` adds each mutator's package to the server's package map, as it adds the
  GameInfo's (UT2004's mutators did it themselves with `AddToPackageMap`, which UC2 lacks): a mutator that replicates
  actors to clients (jcbShadows' mutator runs on every client) needs it. Log: `Mutator Package: <Pkg>`.

## Shadow texture sizes

- `UShadowBitmapMaterial::Get` forced every shadow texture to 256x256 off the Xbox (Epic's PC comment: powers of two
  only, no linear textures, no L8). **Now** a power-of-two size from 32 to 1024 is kept, as the Xbox kept every size;
  anything else is 256 as before. The game's own player shadows ask for 256 (SD_Normal, unchanged); an ambient creature
  can ask for SD_Low..SD_SuperHigh (128..1024, the Xbox's look); a mod's material asks for its own (jcbShadows'
  resolution setting).

## jcbShadows (the mutator, outside this repo)

Ported from the newer of the owner's two copies (2019, config settings) with UC2's API: `new`/`delete` for the shadow
texture (no `Level.ObjectPool`), UC2's 5-argument `FastTrace`, `UCPawn`/`UnrealPawn` shadows, no vehicles and no
settings page; its darkness mapped onto UC2's shadow filter (the Xbox filter, on with post-processing: 127 = none,
255 = darkest); its projector does not fall on actors (UC2 has no `bNoProjectOnOwner`: the pawn would darken itself);
settings in `User.ini [jcbShadows.MutJCBShadows]` (persisted on the web too), written the first time it runs. Built
with this build's `ucc-bin make` against the game's packages. Native check, DM-Azure from one spawn with no shadows /
UC2's own / the mutator: the mutator's shadow darkens the floor (5,824 pixels darker by 11 levels on average, 698
brighter at the edges), log `jcbShadows: real-time shadows for UCPawn_Nakhti_Anubis0: 2 light(s) ...`.
