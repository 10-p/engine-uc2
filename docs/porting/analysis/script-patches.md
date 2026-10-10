# T1 notebook: the script patch pipeline (decompiled base, patch sets, build, proofs)

Task T1 of the UC2 port. Owner's decision (2026-10-10): every `.uc` leaves the repo (UnrealScript is content); our
script changes become unified-diff patch sets against the DECOMPILED form of the shipped `.u`, so a user with only
the retail disc can build them; Epic's 30 load-time ScriptPatches are also a patch set (`epic-fixes`) and the
load-time patcher stays; for UCMenu, which ships with its text, the shipped (retail) text is the base.

Worktree `engine-uc2-t1-scripts`, branch `t1-scripts` (main + the S1 branch). Logs `<WorkUC2>/logs/t1/`, run trees
`<WorkUC2>/run/t1*`. Every run through `<WorkUC2>/bin/capped` (build 6G, ucc 3G, Python 2G). Background reading:
`decompile-spike.md` (S1), `scripts.md` (M14), `ucc-windows.md` (S2), `vanilla.md`, `rebind.md`.

## Step 0 (2026-10-10): what the shipped packages give, re-checked

- Build: `bin/capped 4G 600 -- cmake --preset native`, `bin/capped 6G 3600 -- cmake --build build-native -j4`:
  rc 0 in 9 min (`logs/t1/build-native.log`).
- Reference build of this branch's `Source/` (leak + our 9 files, S2's Core ordering fixes):
  `tools/dev/script-build.sh --tree t1-ref` (script-build.sh now takes `t<N>` trees): 15 s for assemble + make +
  strip + compare; the summary is M14's with our intended changes as problems in UCGameplay (20) and UCMenu (5).
  `run/t1-ref/System` is P2's reference.
- **UCMenu's shipped text is not a complete source** (measured on all 171 `ScriptText` buffers of the shipped
  UCMenu.u): it is what Epic's preprocessor left, i.e. comments blanked to empty lines, `` `include ``/macros
  expanded, `#linenumber N` marks where an include returned (119 of 171), CR LF line ends, and **no
  `defaultproperties` block** (the class factory cuts it off the text and keeps the values only in the class
  defaults; 0 of 171 texts contain one). 8 of the 171 texts start with their `class` line on line 1 (the other 163
  start with the blank lines of a stripped comment header). None contains a 0x02 byte (those are in defaults). So
  the UCMenu base is: the shipped text (CR LF -> LF) + a `defaultproperties` block decompiled from the binary
  defaults. The compiler accepts `#linenumber` (UnScrCom.cpp: the `linenumber` directive sets the input line).
- The 3 UCMenu classes the patch table touches (UC2Arena_Character, ControllerRemovedMenu, ConnectionFailureMenu)
  differ between the retail text and the leak. The retail text is the base; `epic-fixes` carries the leak's
  version of what Epic patched, in the retail text's form (the preprocessed text of the leak, see step 2).

## Step 1 (2026-10-10): `tools/scripts/` (shared reader, production decompiler)

- `tools/scripts/ucpkg.py` (new): the package reader (`Package`, was `tools/dev/upkg151.py`) and the export,
  bytecode and patch-table parsers (`parse_export`, `disasm`, `tagged`, `parse_patches`, `PatchedView`, were in
  `tools/dev/script-diff.py`), moved, not copied. `tools/dev/script-diff.py` and `tools/dev/upkg151.py` import them
  (upkg151.py keeps only its command-line dumper). Regression check: the old and the new script-diff.py on
  `run/s1-gui/System` write byte-identical summary, details, per-function and patch-table files.
- `PatchedView(old, patch, full=True)` (new): every shipped export with the patched bytecode in place (the export
  re-serialised with the new script size; `parse_export(..., spans)` locates the script) plus the appended objects:
  the package as `FLinkerPatchData::Apply` leaves it in memory. All 30 entries parse.
- `tools/scripts/uc-decompile.py` (moved from tools/dev with `git mv`), new command line
  `uc-decompile.py <System>/<Pkg>.u --out <dir> [--shipped-text] [--patches ScriptPatches.h] [--content <root>]
  [--class ...]`; version 1.0; exit 0 / 1 (a class with a decompile error) / 2 (bad input). Changes:
  - line 1 of every file: `// decompiled by uc-decompile 1.0 from <Pkg>.u <sha256[:12]>` (+ ` + ScriptPatches.h
    <sha>`); shipped text: `// shipped text of <Class> from UCMenu.u <sha>, defaultproperties by uc-decompile 1.0`
    replacing the blank line 1 (or appended to the class line for the 8 texts that start with it: no line moves,
    so `assert` lines and positions keep);
  - canonical form documented in the file header (it is the patches' context);
  - a blank line before every function and state ("soft": dropped for a whole file if an `assert` would no longer
    reach its recorded line; no file needs that in GUI/UCGameplay);
  - `--patches`: the World uses `PatchedView(full=True)` for every patched package and links each appended field
    at the head of its parent's Children unless it is already in the chain (an appended function's own appended
    local is: without that check, UCMutators looped until the 2G ceiling stopped it, exit 137, the only ceiling
    hit so far);
  - `--shipped-text`: the class's ScriptText + the decompiled defaults;
  - static arrays inside a struct default value are written `X[i]=v` (the spike's 34 rejected UCMenu lines,
    `LabelPosn(0)=` inside `Labels(n)=(...)`; Epic's text uses brackets there);
  - the probe-name table is embedded (no `Source/` needed); a content package referenced but not found under
    `--content` is an error (was silently no `#exec OBJ LOAD`, which then failed in `make`).
  - Same output as the spike's decompiler for all 94 GUI classes, line 1 and blank lines aside.
- `--patches` on the 6 decompiled packages the patch table touches changes exactly the 16 expected classes
  (Engine: PlayerController, CharacterInformation; UnrealGame: DeathMatch; UCGameplay: CTFFlag, OverflowBall,
  UCPawn, UCPlayerController, UCWeap_Melee; UCInventory: UCWeap_SniperRifle; UCMutators: GameRules_WeaponMaster,
  MutRadioactive, WeaponMasterReplicationInfo; GUI: GUIController) and the text shows the fixes plainly, e.g.
  `PlayerController.ClientSetHUD`: `if ((myHUD != None)) { if ((newScoringType != None)) {...} }`. And Epic's
  defective CTFFlag patch (scripts.md finding 1) in text form: the patched state `home` has its code under a label
  `WeaponAbility:` (the name index the patch carries), so `GotoState('home')` finds no `Begin` and runs nothing,
  in the shipped game with the title-update patch exactly as here.

## Step 1b (2026-10-10): the per-package round trip again, and Engine

Harness `logs/t1/rt.sh <Pkg> [--patches|--shipped-text]` (decompile with `tools/scripts/uc-decompile.py`, the
package's classes from that folder, the pure leak (7633696) for GUI/UCGameplay/UCMenu and `Source/` for the rest,
`script-build.sh` assemble make strip compare); loop `logs/t1/all.sh`, summaries `logs/t1/all-r7-summary.txt` and
`all-r7e-summary.txt` (final decompiler). Measured:

| Package | decompile | make | compare (exports: byte-identical / neutral) | verdict | S1 |
|---|---|---|---|---|---|
| Core | 4/4 | ok | 1011: 782 / 451 | equivalent | same |
| UCAnnouncements | 104/104 | ok | 215: 110 / 106 | equivalent | same |
| UnrealEd | 10/10 | ok | 127: 117 / 10 | equivalent | same |
| IpDrv | 13/13 | ok | 526: 399 / 239 | equivalent | same |
| UWeb | 7/7 | ok | 153: 106 / 84 | equivalent | same |
| UCCharacters | 271/271 | ok | 2704: 1819 / 1487 | equivalent | same |
| UnrealGame | 20/20 | ok | 882: 657 / 431 | = shipped (1 problem: Epic's patch) | same |
| UCInventory | 137/137 | ok | 1924: 1288 / 1122 | = shipped (1: Epic's patch) | same |
| UCMutators | 60/60 | ok | 579: 415 / 269 | = shipped (6: Epic's 3 patches + objects) | same |
| GUI | 94/94 | ok | 3526: 2712 / 1527 | = shipped (2: Epic's patches) | same |
| UCGameplay | 232/232 | ok | 10842: 8152 / 5154 | = shipped (8: Epic's 7 patches + 1 import) | same |
| GamePlay | 102/102 | ok | 974: 646 / 523 | 14 problems: its `#exec` content | same |
| Editor | 16/16 | ok | 446: 352 / 129 | 27 problems: its `#exec` content | same |
| UCElements | 41/41 | 3 default lines rejected | 2956: 2673 / 263 | 248 problems | same (not needed) |
| UCEffects | 657/672 | ok | 3535: 2482 / 979 | 463 problems (S1: 473) | not needed |
| UCMenu decompiled | 169/171 | **ok** (S1: 34 lines rejected) | 10100: 7260 / 4912 | 20 problems | not needed: text base |
| **UCMenu --shipped-text** | 171/171 | ok | 10100: 9902 / 199 | = shipped (13 problems = exactly Epic's 5 patches + their objects/imports) | new |
| Engine | 262/262 | `Can't find Texture 'Engine.WhiteTexture'` | - | its 68 `#exec` textures (no source on the disc) | S1: SIGSEGV |
| Engine + the leak's 68 texture lines (dev aid) | 262/262 | ok | 12640: 9693 / 5665 | = shipped (6: Epic's 4 patches + object) | new |
| `--patches`: GUI, UnrealGame, UCInventory, UCMutators | all | ok | | **equivalent: problems 0** (= shipped + patcher) | new |
| `--patches`: UCGameplay | 232/232 | ok | | problems 0, 1 finding (CTFFlag.home LabelTableOffset 16 -> 48) | new |
| `--patches`: Engine + texture lines | 262/262 | ok | | problems 0, 1 finding (NetworkDevice config default, scripts.md finding 6) | new |

No regression. **Engine's SIGSEGV** (S1) was not in the shared Python parser: it was the script compiler,
`ParseScripts` (Editor/Src/UnScrCom.cpp) recursing without end on a dependson cycle that the decompiler's inferred
`dependson` created (Actor -> BlockingVolume dependson(Material) -> ProjectorMultiMaterial dependson(Actor)); gdb:
`strstr <- ResolveName <- StaticFindObject <- ParseScripts` x thousands (`logs/t1/engine-gdb.txt`). Fixed in the
compiler (a class already being walked is left to that frame; never fires on an acyclic input). Then three more
Engine-only decompiler gaps, each found by the compiler or script-diff: `var Matrix Matrix, GradientMatrix;` (a
variable named like a struct hides the type: such declarations are joined), explicit `Outer.x` where the Context's
skip counts the member's whole bytes (PlayerInput's `Outer.bDemoOwner`, CheatManager's `Outer.GotoState`, also
inside a nested context: PlayerController's `CheatManager.ViewBot()`), and a label at the end of state code
(PlayerController.GameEnded is just `Begin:`). What remains for Engine is not a decompiler gap: the 68 textures and
26 palettes made by `#exec TEXTURE IMPORT` from .pcx/.tga/.dds files the disc does not have. Not done (cost, not
needed by default): recreating them (export mip 0 + palette as image files with the import options recovered from
the texture properties; DXT ones would need .dds to keep the blocks; palette names depend on import order).

## Step 2 (2026-10-10): `patches/` and `tools/scripts/uc-patches.py`

Sets (`patches/sets.txt`, order = apply order): `epic-fixes`, `epic-source`, `mouse-look`, `player-name`,
`keys-menu`; each folder: `README.md`, `<Pkg>.diff` (paths `<Pkg>/Classes/<Class>.uc`, `patch -p1` reads them),
`new/<Pkg>/Classes/` for whole new classes. Generated by `tools/dev/uc-patches-regen.sh` (47 s; dev only, needs
`run/t1-ref` = `script-build.sh --tree t1-ref` of `Source/`); regenerating gives byte-identical files (checked
twice). Measured sizes:

| Set | Files | Hunks | +/- lines | Bytes |
|---|---|---|---|---|
| epic-fixes | Engine, GUI, UCGameplay, UCInventory, UCMenu, UCMutators, UnrealGame .diff | 4+2+7+1+6+3+1 = 24 | +113 -30 | 7948 |
| epic-source | UCGameplay.diff | 3 | +2 -3 | 728 |
| mouse-look | UCGameplay.diff (UCPlayerInput 7, UCPlayerController 9, UCTrainingController 2) | 18 | +280 -14 | 13980 |
| player-name | GUI.diff (GUIController.SetProfilePlayerName) | 2 | +15 -4 | 1011 |
| keys-menu | UCMenu.diff (UC2Settings_Main) + new KeyBindingPanel.uc (323 lines), UC2Settings_Keys.uc (1236 lines) | 5 | +19 -0 | 1584 + 9816 + 40612 |

How each is made (base -> changed, `uc-patches.py make`):
- `epic-fixes`: base -> `uc-decompile --patches` (the package as the load-time patcher leaves it) for the 6
  decompiled packages; UCMenu: the 3 patched classes as the leak compiles (shipped-text of the reference
  UCMenu.u). **Our ucc's preprocessing of the leak reproduces Epic's retail ScriptText byte for byte** (168 of
  171 UCMenu classes equal, the other 3 are exactly the patched ones), so the leak's version of those 3 classes is
  in the retail form and diffs cleanly against the retail base. This is how the 3 classes whose retail text differs
  from the leak are handled: the retail text is the base, and the leak's changes to them are epic-fixes hunks.
- `epic-source` (**a set the brief did not list**; owner question): decompiling the patched view and decompiling
  a build of the leak differ in exactly three places, all things the patcher cannot do: CTFFlag.home's label
  (Epic's patch names `WeaponAbility`, a wrong name index, scripts.md finding 1: the state's code never runs; the
  leak says `Begin`), OverflowBall's `bUpdateSimulatedPosition=True` default, two unused locals of
  UCWeap_Melee.BusyShieldOn.HitWall. Keeping them out of epic-fixes makes P1 exact (30 of 30 patch entries
  identical, CTFFlag included) and keeping them in a set makes P2 exact (our current rebuilt UCGameplay runs
  CTFFlag's Begin code). GUIController's `config` on bUnlockCharacters/bShowAllMuts needs nothing: the shipped GUI.u
  has it, Epic's later source dropped it, our D-4 put it back, so base and our build agree.
- `mouse-look`, `player-name`, `keys-menu`: every class of UCGameplay / GUI / UCMenu from the reference build
  (the check shows only the expected classes change); keys-menu's new classes are `Source/UCMenu/Classes` text,
  made self-contained (commit 570e84f: the `` `include(GUI.huc) `` was the only thing tying them to Epic's files).
- **keys-menu/mouse-rows: not split**. The compile dependency on mouse-look is 12 lines (`LoadMouse`/`SaveMouse`
  read and set `UCPlayerInput.MouseLookSensitivity`/`bInvertMouseLook`), but the two mouse rows themselves (slider,
  check box: 4 functions, 2 subobjects, 82 lines naming them) only do something with mouse-look; a split means a
  hand-written variant of the page without them. keys-menu depends on mouse-look in sets.txt instead. Owner question.
- The extra blank-line hunks in epic-fixes/GUIController and player-name are `assert` padding (the bytecode
  records the assert's source line; the decompiler pads to it).
- 0x02 bytes: 38 base files hold them (UCMenu/GUI/Engine defaults); apply leaves the 36 unpatched ones
  byte-identical and keeps the 0x02 lines of the 2 patched ones (UC2Arena_Character, ControllerRemovedMenu); a
  synthetic test with 0x02, 0x85 and CR bytes inside lines round-trips make -> apply byte-exactly (GNU `patch
  --dry-run -p1` accepts the diffs too). No set's diff contains a 0x02 byte.

`uc-patches.py`: `make`, `apply` (exact context, no fuzz, a moved hunk is reported; line 1, the identity line,
matches any decompiler version/sha; a package not in the base tree is left out), `check`, `sets`, `packages`
(`--with-epic-only`, `--compile-list <shipped>`), `compare` (P2), `rebase`.

Two things the build needed, found by building against the shipped packages one package at a time:
1. **Compile list.** The shipped packages went through `stripsource` with everything loaded, so they refer
   forward (`UCAnnouncements.u` imports UCEffects, which imports UCGameplay; `UCGameplay.u` imports UCEffects,
   UCCharacters, UCInventory). Compiling UCGameplay with only the earlier packages present (as Epic's scratch build
   had them) cannot load the shipped UCAnnouncements; `uc-patches.py packages --compile-list` adds such packages
   (default build: UCAnnouncements) to be compiled from their base first and not delivered.
2. **Three compiler fixes** (commit 9593193; a scratch build of `Source/` is unchanged: 14 packages
   byte-identical, Engine/Editor/GamePlay differ only in the 78 texture import stamps, script-diff `import-time`):
   the ParseScripts cycle guard (above); a class loaded from a stripped package keeps the CRC of the text it was
   compiled from (else every shipped class is re-parsed from " ": "Object.uc(1): Missing 'Class' definition");
   no `EX_DestroyReturnValue` for an object return value (its CPF_NeedCtorLink, set on load for edit-inline object
   properties, means instancing: UCMenu's `OpenMenu(...)` calls gained the token when GUI.u came from disk:
   62 functions differed before this fix).

## Step 3 (2026-10-10): the proofs, `tools/scripts/uc-patches-proof.sh`

Final run, `tools/scripts/uc-patches-proof.sh` (all three, 2 min 4 s, `logs/t1/proof-final.txt`), literal:

```
P1: packages Engine,UnrealGame,UCGameplay,UCInventory,UCMutators,GUI,UCMenu
P1: Engine: 68 #exec TEXTURE IMPORT lines from /home/ion/WorkUC2/epic-sources/Engine/Classes (dev aid)
Engine           different-explained exports 12640, byte-identical  9693, neutral 5671, findings  1, problems  0
UnrealGame       equivalent          exports   882, byte-identical   657, neutral  432, findings  0, problems  0
UCGameplay       different-explained exports 10842, byte-identical  8152, neutral 5161, findings  1, problems  0
UCInventory      equivalent          exports  1924, byte-identical  1288, neutral 1123, findings  0, problems  0
UCMutators       equivalent          exports   579, byte-identical   415, neutral  278, findings  0, problems  0
GUI              equivalent          exports  3526, byte-identical  2712, neutral 1530, findings  0, problems  0
UCMenu           different-explained exports 10100, byte-identical  9829, neutral  332, findings  1, problems  0
  patch table (/home/ion/WorkUC2/run/t1-proof/p1-patches.txt): 30 identical, 0 other
PASS P1: problems 0 in 7 packages
P2: packages UCGameplay,GUI,UCMenu (reference /home/ion/WorkUC2/run/t1-ref/System)
UCGameplay: exports 10899 / 10899, functions+states 2474 (2474 bytecode-identical), classes with defaults compared 232, differences 0
GUI: exports 3526 / 3526, functions+states 720 (720 bytecode-identical), classes with defaults compared 94, differences 0
UCMenu: exports 10466 / 10466, functions+states 2360 (2360 bytecode-identical), classes with defaults compared 173, differences 0
PASS P2: 3 packages equal to the reference build
  decompile twice: identical (601 files)
  apply twice: identical
  build twice: UCGameplay.u identical (1589219 bytes); GUI.u identical (370666 bytes); UCMenu.u identical (2409720 bytes);
PASS P3: decompile, apply and build are deterministic
```

- P1's 3 findings are what a compiler does and the patcher could not: Engine.NetworkDevice's config default
  (static build, scripts.md finding 6), CTFFlag.home LabelTableOffset 16 -> 48, ConnectionFailureMenu ProbeMask
  0 -> 256. Neutral: Line/TextPos, dependency lists, strip marks, the patch's own objects' positions (script-diff
  now treats an appended function's Line/TextPos as a position). P1 for Engine uses the dev aid (Epic's 68 texture
  lines + Epic's Textures/); every Engine function and default is otherwise from the decompiled base.
- P2 compares conformed builds by export path (names, imports, exports as sets; names without case: an FName keeps
  the case of its first creation, e.g. `T_UI_2`/`t_ui_2`, neutral as script-diff's name-case), every field but
  Line/TextPos, dependencies, text buffers and Children/Next, the bytecode token streams with references as paths,
  and every class's defaults as the decompiler prints them. Negative check: the same compare against the shipped
  UCGameplay.u reports 88 differences (17 functions: our 10 + Epic's 7).
- P3: decompiles with PYTHONHASHSEED 1 and 2; no `InternalTime` difference appears because GUI/UCGameplay/UCMenu
  have no `#exec` imports.
- Rebase, tested: `uc-patches-proof.sh rebase --old-decompiler tools/scripts/uc-decompile.py --new-decompiler
  <variant without the blank lines, version 0.9-rebase-test> --patches-out run/t1-proof/rebased-patches` (3 min
  22 s): each stage built and decompiled with the variant, sets re-made, `PASS rebase: ... new base + sets builds the
  same as old base + sets` (UCGameplay 2474, GUI 720, UCMenu 2360 functions identical, 0 differences); the re-made
  sets have the same hunk counts as patches/ (mouse-look +269 instead of +280: the blank lines). Found on the way:
  a stage decompiled from a package compiled here spells some names in another case (`t_ui_2`), which gave 150
  noise hunks; rebase now keeps the base's spelling where a line differs only in name case. Rebase re-makes only
  the packages of the default build; the epic-only packages' diffs are kept as they are (not exercised).

## Step 4: interface for the build (for T3)

All bash + Python 3 (no other modules); every command runs through `bin/capped` in our scripts; the decompiler and
`uc-patches.py` are plain Python (Windows: they run under any Python 3; `uc-build.sh` is bash-only, it drives
`ucc-bin` and would need the wine/`ucc.exe` mode of `tools/dev/script-build.sh` for a Windows host, not done).

```
# inputs: S = the shipped System folder (the 17 .u; the retail disc's), A = the game tree (content folders
# Textures/ StaticMeshes/ ... for #exec OBJ LOAD, and System/ with Epic's inis DefaultXbox.ini DefUserXbox.ini
# Default.ini DefUser.ini and Localization/), R = the retail System for Characters/Gametypes/Maplists.ini
# (default <WorkUC2>/reference/iso-extract/System), ucc-bin from build-native.
PK=$(tools/scripts/uc-patches.py packages --default --compile-list $S | awk '{print $1}')   # UCAnnouncements UCGameplay GUI UCMenu
for p in $PK; do                                                        # ~5 s for these 4
  [ $p = UCMenu ] && x=--shipped-text || x=
  tools/scripts/uc-decompile.py $S/$p.u --out base/$p/Classes --content $A $x
done
tools/scripts/uc-patches.py apply --base base --default --out tree      # ~1 s
tools/scripts/uc-build.sh --classes tree --out work --shipped $S --assets $A --retail-inis $R \
    --packages UCAnnouncements,UCGameplay,GUI,UCMenu --keep UCGameplay,GUI,UCMenu        # ~17 s
# outputs: work/System/{UCGameplay,GUI,UCMenu}.u (every other .u there is the shipped one)
```

- Times measured on ion-kubuai, load 4-7: decompile 4 packages 5 s (7 packages + UCAnnouncements: 11 s), apply 1
  s, build 17 s (make UCAnnouncements 1 s, UCGameplay 4 s, GUI 3 s, UCMenu 4 s, strip 5 s), proofs P1 41 s, P2
  35 s, P3 49 s, rebase 3.4 min. Memory: Python under 2G, ucc-bin under 3G ceilings; no ceiling hit except the one
  UCMutators `--patches` loop before the Children fix (exit 137, step 1).
- `#exec` resource folders: **none** for GUI, UCGameplay, UCMenu (no `#exec TEXTURE/MESH IMPORT`; their decompiled
  classes only `#exec OBJ LOAD` content packages, found under `--content`/`--assets`). UCAnnouncements: none.
  Engine (epic-fixes-only, never in the default build) would need its 68 texture source files.
- A package that only epic-fixes touches: `uc-patches.py packages --sets epic-fixes --with-epic-only
  --compile-list $S` lists what to decompile and build; T3 can expose it as an option (UnrealGame, UCInventory,
  UCMutators work; Engine does not from the disc alone).
- Not done here (T3): CMake wiring; putting the outputs where the run tree takes them (`UC2_SCRIPTS`); a Windows
  host without bash.
