# Spike S1: can the shipped script packages be decompiled into a source base that recompiles to the same bytecode?

Feasibility study with measurements (not the implementation; nothing here ships). Worktree
`engine-uc2-s1-decompile`, branch `s1-decompile`. Logs `<WorkUC2>/logs/s1/`, run trees `<WorkUC2>/run/s1-*`.
Every run through `<WorkUC2>/bin/capped` (build 6G, ucc 3G, Python 2G). Time box about 6 hours.

Question from the owner: the `.uc` files leave the repo; our script changes become patch sets applied at build
time to a BASE; the preferred base is the DECOMPILED form of the shipped `.u` (17 packages, from the disc), so a
user with only the ISO can build the patched scripts.

## Step 0 (2026-10-10): what the shipped packages contain

Reader: `script-diff.py`'s `parse_export` on every Class export of `<WorkUC2>/epic-sources/System/*.u` (the shipped
files; note `assets/uc2/System` now holds REBUILT GUI.u, UCGameplay.u, UCMenu.u since asset tree v2, see its
`SCRIPTS.txt`; the shipped copies are in `epic-sources/System` and `assets/uc2-shipped/System`). Result
(`logs/s1/step0-scripttext.txt`):

| Package | classes | ScriptText with source | ScriptText = " " |
|---|---|---|---|
| Core | 4 | 0 | 4 |
| Engine | 262 | 0 | 262 |
| UCAnnouncements | 104 | 0 | 104 |
| **Editor** | 16 | **16** | 0 |
| UnrealEd | 10 | 0 | 10 |
| IpDrv | 13 | 0 | 13 |
| UWeb | 7 | 0 | 7 |
| GamePlay | 102 | 0 | 102 |
| UnrealGame | 20 | 0 | 20 |
| UCElements | 41 | 0 | 41 |
| UCGameplay | 232 | 0 | 232 |
| UCEffects | 672 | 0 | 672 |
| UCCharacters | 271 | 0 | 271 |
| UCInventory | 137 | 0 | 137 |
| UCMutators | 60 | 0 | 60 |
| GUI | 94 | 0 | 94 |
| **UCMenu** | 171 | **171** | 0 |

So the premise holds for 15 packages, NOT for UCMenu and Editor: both ship with full source text (the M14 notebook,
scripts.md step 2a, found the same: UCMenu.u was never stripped, Editor.u is skipped by `stripsource`). UCMenu is
one of the three packages we patch: its base can be read straight out of the shipped UCMenu.u (the retail text,
which by construction compiles to the shipped bytecode). Only GUI and UCGameplay need a decompiler.
`cpptext` buffers: stripped too (the 143 classes with a CppText reference hold " ").

`batchexport` of a stripped class (measured: tree `run/s1` from `mkrun-uc2.sh build-native s1` with the shipped
GUI/UCGameplay/UCMenu copied in from `epic-sources/System`; `bin/capped 3G 600 -- ./ucc-bin batchexport UCGameplay
Class uc <dir>`, exit 0, 232 files, `logs/s1/batchexport/`): each file is two blank lines (the " " text buffer)
followed by a `defaultproperties { ... }` block written from the in-memory class defaults (UExporter: e.g.
CTFFlag.uc, 776 bytes, `MaxDropTime=25.000000 ... RotationRate=(Pitch=30000,Roll=30000)`). No class line, no
declarations, no code. The same command on the shipped UCMenu.u writes the full retail source (UC2MainMenu.uc 4579
bytes).

## Step 1 (2026-10-10): existing decompilers

- `which dotnet mono msbuild csc`: none installed (no .NET runtime on ion-kubuai; nothing installed, per the rules).
- **UELib / UE Explorer** (Eliot Van Uytfanghe; C#): cloned read-only to `logs/s1/ext/UELib` (commit 3207a17,
  2026-08-15). Its README game table lists "Unreal Championship 2: Liandri Conflict | 3323 | 151/002 | Third-party"
  with a link to a BeyondUnreal thread "unreal-championship-2-script-decompiler-release" (403 to WebFetch, not
  readable here). In the source: an enum value `BuildGeneration.UE2X` ("Modified Unreal Engine 2 for Xbox e.g.
  Unreal Championship 2") that **no build entry uses**; comments about UC2 flags/tokens ("UC2 has this bytecode
  [OutVariable] as 0x49", "EatString -> EatReturnValueToken (also in UC2)", "DebugOnly in UC2 ... 0x00400000"),
  i.e. partial knowledge, no registered UC2 build. The README lists package version 151 **licensee 2**; our packages
  (the leak's PC build) are **151/0** (`upkg151.py`); which build 151/2 is, cannot be checked here. Nothing in the source claims recompilable output
  (UE Explorer decompiles for reading). Not runnable here (no dotnet).
- **UTPT** (Antonio Cordero): closed-source Windows tool; nothing to verify.
- No Python UE2 bytecode decompiler found that claims v151 or licensee builds.
- Conclusion: no existing tool can be verified to decompile UC2 v151/0 into recompilable text; a decompiler would
  be ours.

## Step 2: the prototype `tools/dev/uc-decompile.py`

`tools/dev/uc-decompile.py <shipped System> <Pkg> <outdir>` imports `script-diff.py` (package reader, export
parsers) and writes one `.uc` per class. `tools/dev/script-build.sh` gained `--classes <Pkg>=<dir>` (take one
package's classes from a folder) and `s<N>` tree names (logs default `<WorkUC2>/logs/s<N>`). The test loop for
one package (`logs/s1/try.sh`): decompile, copy the package's non-`.uc` files (`Avaris.huc`), assemble a tree
with that package's classes from the decompiled folder and Epic's text for the other 16, `make`, `strip`,
`compare`.

What the compiler does that the decompiler must reproduce (read in `Editor/Src/UnScrCom.cpp`):
- Field chain: every field is prepended to `Children`, except properties: the first is prepended, later ones are
  appended after the last property. Function parameters/locals are in declaration order. Rebuilding the same
  chain needs the declaration order recovered from it (`Decompiler.decl_order`).
- Control flow (`CompileCommand`, `PopNest`): `if` = JumpIfNot(else/end) ... [Jump end]; `while` = JumpIfNot(end)
  body Jump(start); `do..until` = body JumpIfNot(start); `do{}` without until = Jump(start); `for` = init,
  JumpIfNot(end) body inc Jump(cond), `continue` jumps to the increment (`while` with `continue` jumps to the back
  jump); `switch` = Switch, chained Case offsets, `Case 0xFFFF` for default or as end marker; `break` = Jump to the
  loop/switch end; `foreach` = Iterator expr skip(end) body IteratorNext; IteratorPop, `continue` = IteratorNext +
  Jump(end), `return` inside foreach emits IteratorPop first; `&&`/`||` = native operator with Skip on the second
  operand; `?:` = EX_Conditional with two skips; states end with Stop, alignment Nothings and the LabelTable;
  functions end with Return Nothing. No DebugInfo tokens (`-debug` was not passed).
- Lexer: `-` directly followed by a digit is a negative literal (so binary operators are printed with spaces);
  floats are `atof` of digits and `.` (no exponents in practice, 64-character limit): printed as the shortest fixed
  decimal that reads back to the same float32.

Results per package are in the table below (updated as packages are reached).

| Package | classes | decompiled without error | make | compare verdict | notes |
|---|---|---|---|---|---|
| Core | 4 | 4 | 0 errors | **equivalent** (1011 exports: 782 byte-identical, 454 neutral = Line/TextPos 450 + dependency lists 4; 0 problems) | after two fixes: inline `var enum` in structs, the property-block position in the field chain |
| UCAnnouncements | 104 | 104 | 0 errors | **equivalent** (215: 110 byte-identical, 106 neutral = dependency lists 104 + positions 2; 0 problems) | `for` loops come out as `while` (same bytecode); config values (from the ini at strip time) are written into defaultproperties |
| UCMutators | 60 | 60 | 0 errors | **= shipped** (579: 421 byte-identical, 262 neutral; the 6 "problems" are all Epic's ScriptPatches.h: the decompiled text is the RETAIL code, so the 3 patches (WeaponMaster ScoreKill, the two Reset functions and their appended objects) are absent, as in the shipped file) | needed: struct-member arrays as `Points=(a,b)`, subobject references as `Class'Name'` |
| GUI | 94 | 94 | 0 errors | **= shipped** (3526: 2712 byte-identical, 1528 neutral = positions 1434 + dependency lists 94; the 2 "problems" are Epic's 2 GUIController patches, absent from retail as expected) | needed: enum tags for byte constants (typed from the other operand / the parameter / the assignment target), `EnumName(x)` when a plain byte is assigned to an enum, a fallback enum when an enum tag is assigned to an int, `obj.const.NAME` / `class'X'.const.NAME` recovered from the constant's value, `dependson` inferred from struct types used from another class of the package, delegate defaults (`__X__Delegate` -> `X`; self-bound delegates of subobjects omitted: instancing sets them), `cpptext` blocks with one line (the buffer exists, stripped), assert lines padded to the original line number (the line is in the bytecode), the ECastToken table |

Finding: decompiling the shipped `.u` gives the RETAIL scripts. Epic's `ScriptPatches.h` (30 load-time patches: 21
bytecode, 9 new objects) is not in them; our rebuilt packages would then lack Epic's title-update fixes unless the
decompiler also decompiles the patched view (shipped + `ScriptPatches.h`, which `script-diff.py`'s `PatchedView`
already builds) or the patch sets carry those changes. With the leak's sources the fixes are already in the text.

UCGameplay (2026-10-10): 232 classes, 232 decompiled without error, `make` 0 errors, compare **= shipped**: 10842
exports, 8149 byte-identical, 5175 neutral (positions 4941, dependency lists 231, 1 dependency CRC, 1 name case, 1
patch-config name), 0 findings; the 8 "problems" are again only Epic's ScriptPatches.h (7 patched functions/states
whose recompiled bytecode equals the shipped retail one, and the import the OverflowBall patch appends). Needed on the
way: inherited net flags (an override of a replicated function has FUNC_Net with RepOffset 0xFFFF: not listed in
`replication`), `auto simulated state` (simulated must directly precede `state`), `else if` chains printed flat
(the compiler's nesting limit is 16: a 20-way if/else chain in UCPlayerController failed nested), `Super.X` vs
`Super(Name).X` resolved like the compiler (in a state, `Super(Active)` names the state itself), members of the
`within` class printed without `Outer.` (the compiler's implicit Context has a different skip size than an explicit
`Outer.x`), `#exec OBJ LOAD` for every content package a class references (the original classes rely on an
earlier class having loaded it: invisible in the bytecode), labels at the same offset in reverse table order,
dynamic arrays in defaults emptied first (`X=()`, then `X(i)=`: a subclass array shorter than the inherited one).
In this run GUI and UCMenu were not built: our main-branch UCMenu (UC2Settings_Keys.uc) uses
`UCPlayerInput.MouseLookSensitivity`, which only our patched UCGameplay has (the decompiled one is retail).

## Step 3 (2026-10-10): what cannot round-trip by construction, with evidence

What the shipped `.u` keeps (so the decompiler recovers it), and what it loses:

| Item | In the shipped .u? | Evidence / consequence |
|---|---|---|
| comments, formatting, blank lines | no | irrelevant to the bytecode, BUT they are most of the value of Epic's text for a patch author (Epic's `UCPlayerInput.uc` explains the adhesion maths; the decompiled one is bare `(a * b)`) |
| source line numbers | partly | each function's Line/TextPos (script-diff "positions", neutral: 450 in Core, 4941 in UCGameplay); `assert` stores its line IN the bytecode (moCheckBox.GetMinimumSize: line 88): the decompiler pads with blank lines to put the assert on that line (works while the compact output stays behind the original) |
| script text CRC | no | every decompiled class gets a new CRC: the dependency records of later packages differ (neutral "dependency-list"/"dependency-crc"; incremental-compile bookkeeping only) |
| `#exec OBJ LOAD` | no (no trace) | the decompiler adds one per content package a class references (resolved against the asset tree); the originals often relied on an earlier class's load (UCScoreboard_TeamGame used `t_ui_2` loaded elsewhere: "Can't find Texture" until added) |
| `#exec TEXTURE/STATICMESH/... IMPORT` | the imported objects are exports of the package, the source files are not | Engine (68 textures, 26 palettes), Editor (6 textures, 5 palettes, 2 static meshes), GamePlay (4 textures, 2 palettes); a decompiled class cannot re-import them: measured below for Engine/GamePlay/Editor. None of GUI/UCGameplay/UCMenu has `#exec`-imported content (their non-script exports are defaultproperties subobjects, which round-trip) |
| backtick macros (`` `log ``, `` `logd ``, `` `include ``) | expanded / dropped | irrelevant: `` `log `` became a `log(...)` call, `` `logd `` nothing (scripts.md step 0) |
| `cpptext` | buffer exists, text stripped (" ") | the decompiler writes a one-line block so the buffer exists (an empty block creates none, UnEdFact.cpp); the C++ headers stay committed and are never regenerated (`-noh`) |
| `const` declarations | yes (name + value text verbatim) | uses of a constant in code are inlined literals (name lost there; `obj.const.X` and `class'X'.const.X` recovered by value lookup, MSVC wrap and float equality included) |
| local variable and parameter names | yes | function Children, in declaration order |
| enum / struct names and members | yes | Enum/Struct exports; enums declared inside structs come back as inline `var enum E {...} V;` |
| `replication` | yes | CPF_Net/FUNC_Net + RepOffset into the class script (the condition bytecode); `reliable` for functions = FUNC_NetReliable; for variables the keyword is not stored and does not matter; overrides of replicated functions inherit the flags with RepOffset 0xFFFF |
| `native`, `native(N)`, `event`, `exec`, `simulated`, `latent`, `iterator`, `singular`, `static`, `final`, `private`, `protected`, `operator(P)`/`preoperator`/`postoperator`, `delegate`, `debugonly` | yes (FunctionFlags, iNative, OperPrecedence, FriendlyName) | |
| class flags: `abstract`, `native`, `nativereplication`, `config(X)`, `perobjectconfig`, `transient`, `noexport`, `placeable`, `editinlinenew`, `collapsecategories`, `hidecategories`, `poolable`, `precache`, ... | yes | ClassFlags, RF_Native, ClassConfigName, HideCategories; inherited ones printed only where they differ from the superclass |
| `dependson(X)` | no | only steers the parse order; inferred from struct/enum types a class uses from another class of the same package (GUIController needed `dependson(GUI)` for `GUI.MutatorRecord`) |
| `within X` | yes | ClassWithin |
| interfaces | n/a | UE2 v151 has none |
| delegates | yes | FUNC_Delegate functions, `__X__Delegate` properties, EX_DelegateFunction/EX_FinalDelegate; default bindings in defaultproperties |
| `ignores` | yes but ambiguous | probe names = IgnoreMask bits; other names = empty functions whose super is the class-level function |
| defaultproperties | yes, as values | written vs the superclass defaults; config/localized values from the inis at strip time are in them (re-reading the ini at build time gives the same); subobjects (`Begin Object`) come back from their exports; self-bound delegates of subobjects are omitted (instancing sets them) |
| struct `native`/`transient` keywords | not stored in v151 Struct exports | irrelevant to the bytecode (header export only) |
| `for` vs `while`, `else if` vs nested `else { if }`, `switch` breaks vs fall-through to the end, parentheses, explicit vs implicit casts | compile to the same bytes | the decompiler picks one; `for` with `continue` would need the `for` form (not met in these packages) |
| **Epic's ScriptPatches.h** (30 title-update fixes: 21 bytecode, 9 objects) | **no** | decompiling the shipped `.u` gives the RETAIL scripts: every package with patches shows exactly its patches as the only difference (UCMutators 3, GUI 2, UCGameplay 7 + 1 import, UnrealGame 1). A base for patching would have to decompile the patched view (shipped + ScriptPatches.h; `script-diff.py` already builds it) or carry Epic's fixes as a first patch set |

## Step 4 (2026-10-10): keeping the patch sets stable if the base is decompiled

A text diff against decompiler output breaks whenever the output changes (a new rule changes how a byte constant,
an `else if` chain, a cast or a declaration order is printed), even where the bytecode does not change. Three
ways, from weakest to strongest:

1. **Versioned decompiler**: the base is the output of one tagged `uc-decompile.py`; each patch set records the
   decompiler version it was made against. Changing the decompiler means regenerating the base with the old and new
   versions and rebasing the patches (a 3-way merge). Works, but every decompiler fix is a patch-maintenance event.
2. **Canonical formatting** (one statement per line, fixed parentheses, fixed declaration order): reduces the noise
   but cannot freeze choices that are bytecode-neutral by nature (the decompiler must be free to change them to fix
   round-trip bugs).
3. **Member-level patches** (recommended if (a) is chosen): a patch is not a line diff but a set of whole members
   per class, applied by name to the decompiled class: replace function `UCPlayerInput.PlayerInput`, add var
   `MouseLookSensitivity`, add/replace `defaultproperties` lines, add whole new class files. The applier parses the
   decompiled class only at member granularity (the decompiler already emits every member as one contiguous block).
   A decompiler change can then only break a patch if it renames a member, which it cannot (names are in the
   package). Our change set fits this shape (step 5): 2 whole new classes, 11+66 new functions, 13 replaced
   functions, ~46 new variables, a few defaults lines and flag changes.

## Step 5 (2026-10-10): the size of our change, and three costs

### Our change, measured

Source: `git diff --stat 7633696 HEAD` over every `Source/<Pkg>/Classes` (7633696 = the import): 9 files,
+1975/-30 lines: GUIComponent.uc 4, GUIController.uc 29, UCPlayerController.uc 89, UCPlayerInput.uc 265,
UCTrainingController.uc 31, KeyBindingPanel.uc 323 (new), UC2Settings_Keys.uc 1236 (new), UC2Settings_Main.uc 20, and
PSDistortionBaseMaterial.uc 8 (M7, cpptext only: no bytecode).

Bytecode: `script-diff.py <WorkUC2>/epic-sources/System <WorkUC2>/assets/uc2/System --packages GUI,UCGameplay,UCMenu
--functions` (the asset tree's GUI/UCGameplay/UCMenu are rebuilt from main, `System/SCRIPTS.txt`; details
`logs/s1/ours-vs-shipped-{details,functions}.txt`):
- UCGameplay: 10 existing functions with different bytecode (UCPlayerController CalcBehindView, PlayerCalcView,
  UpdateRotation, PlayerWalking.ModifyDoubleClickMove; UCPlayerInput PlayerInput, GetTurnAdhesionAdjust,
  GetLookUpAdhesionAdjust; UCTrainingController PlayerViewTest.Fire/AltFire, PlayerViewSetup.PlayerMove), 59 new
  exports: 11 functions and 46 properties (class variables incl. `globalconfig` MouseLookSensitivity,
  bInvertMouseLook, bLazyMouseCamera, bDoubleTapDodge, and function locals), 41 new names, 3 new imports.
- UCMenu: 3 changed functions (UC2Settings_Main InitOptions, CloseComplete, Free), 366 new exports: 2 new classes
  (KeyBindingPanel, UC2Settings_Keys) with 66 functions, 16 consts, a struct, ~270 properties and 8 defaultproperties
  subobjects (GUILabel x3, moCheckBox, moSlider, ...), plus a new subobject/var in UC2Settings_Main
  (`btn_Keys`), 167 names, 21 imports.
- GUI: no bytecode change in a function; property flags (`bUnlockCharacters`/`bShowAllMuts` config again) and
  defaults.

### (a) Finish the decompiler, ISO-only users build the patched scripts

Measured reach after about 4.5 hours (tables above and the final run below): Core, UCAnnouncements, UnrealEd, IpDrv,
UWeb, UCCharacters equivalent to shipped; UCMutators, GUI, UCGameplay equal to shipped except Epic's
ScriptPatches; UCMenu not needed (its text ships). Remaining work, in days of one agent:
- GUI + UCGameplay production quality (regression run of all 232+94 classes in a test, the decompiled base
  committed nowhere but generated at build time, error handling): **1 day**.
- Epic's ScriptPatches in the base (decompile the patched view, or Epic's fixes as a first patch set; the
  CTFFlag.home patch is defective, scripts.md finding 1): **0.5 day**.
- A member-level patch format and applier (step 4, option 3), our 9 files expressed in it, and the build wiring
  (`script-build.sh`: decompile -> apply -> make only the patched packages against the shipped others -> conform):
  **1.5-2 days**.
- All 17 packages (not needed for the 3 patched ones): GamePlay/UnrealGame/UCInventory/UCEffects/Engine/Editor
  still fail somewhere (see the final run): the constructs are known kinds (for-with-continue, nested subobjects,
  an Engine compiler crash, `#exec` content imports that cannot be recreated at all for Engine/Editor/GamePlay):
  **2-3 days more**, and Engine/Editor/GamePlay can never be complete from the .u alone (their imported textures
  have no source; a "keep the shipped objects" conform step would be new engineering).
- Total for the three patched packages: **about 3-3.5 days**; all 17: **5-7 days**. Risk: medium-low (every
  remaining failure seen so far was a printing rule, found by the compiler or by script-diff within minutes);
  the lasting cost is that the patch base is unreadable compared to Epic's commented text.

### (b) Patches against Epic's original sources (the leak), supplied by the user as a folder

- The patch sets are plain diffs against `Source/<Pkg>/Classes` of the leak, which never changes: perfectly
  stable. Extract them from git (`git diff 7633696 HEAD -- Source/GUI/Classes Source/UCGameplay/Classes
  Source/UCMenu/Classes` = 3 patch files), `script-build.sh` takes `--classes`/a user folder (done in this spike),
  applies the patches, runs `make` and the conform. **About 1 day**, low risk (M14 already proves the leak's text
  compiles to shipped + patches, `script-diff` problems 0).
- ISO-only users get vanilla (shipped) scripts; Epic's title-update fixes are already in the leak's text.

### (c) Bytecode patches through Epic's ScriptPatcher (`Engine/Src/UnScriptPatcher.cpp`)

- What the mechanism can do (read in `FLinkerPatchData::Apply`): append names and imports, replace the bytecode of
  existing functions/states (ScriptPatches), append new exports (each a serialized object) and link a new UField
  into its parent's Children (functions also into FuncMap; properties trigger `Parent->Link`).
- What it cannot do: change any existing object other than bytecode (flags, defaults, ProbeMask, LabelTableOffset:
  the CTFFlag.home and ConnectionFailureMenu findings of scripts.md are exactly this), so `bUnlockCharacters`
  config flags and the UC2Settings_Main `btn_Keys` subobject/default cannot be expressed; adding class variables
  to an existing class after its default object exists grows PropertiesSize without the defaults buffer (Epic only
  ever appended function locals and functions: the 9 objects in ScriptPatches.h); whole new classes (366 exports
  in UCMenu, with defaults and subobjects) were never done by Epic's patcher and would need new engineering in
  the loader (class linking, default object, config loading of new globalconfig variables).
- Epic's generator (`ucc patchscript`, `UnrealUtil/Src/UnPatchCommandlets.cpp`, 1253 lines) is not built in our
  CMake; it would have to be ported to produce the C++ tables.
- Cost: porting the generator 1-2 days; extending the patcher for new classes, new class variables with defaults
  and config, flag/default changes: **5-10 days** with real crash risk (memory layout of live objects), each patch
  tied to the exact shipped file (NameCount/ExportCount). And the result is not source: changing a patched function
  later means editing source somewhere anyway. Not recommended.

## Final run (2026-10-10, the committed decompiler; `logs/s1/rerun{2,3,4}-summary.txt`)

Every package round-tripped alone (its classes decompiled, Epic's text for the other 16, `make`, `strip`, `compare`
against `epic-sources/System`). "= shipped" means: every difference is neutral (Line/TextPos, dependency
records) except Epic's ScriptPatches.h entries, which the decompiled RETAIL text lacks by construction.

| Package | classes | decompiled without error | `make` | compare (exports: byte-identical / neutral) | verdict | what still fails (one example) |
|---|---|---|---|---|---|---|
| Core | 4 | 4 | ok | 1011: 782 / 454 | **equivalent** | - |
| UCAnnouncements | 104 | 104 | ok | 215: 110 / 106 | **equivalent** | - |
| UnrealEd | 10 | 10 | ok | 127: 117 / 10 | **equivalent** | - |
| IpDrv | 13 | 13 | ok | 526: 399 / 241 | **equivalent** | - |
| UWeb | 7 | 7 | ok | 153: 105 / 87 | **equivalent** | - |
| UCCharacters | 271 | 271 | ok | 2704: 1833 / 1473 | **equivalent** | - |
| UnrealGame | 20 | 20 | ok | 882: 657 / 434 | **= shipped** (1 problem: Epic's DeathMatch.EndGame patch) | - |
| UCInventory | 137 | 137 | ok | 1924: 1302 / 1125 | **= shipped** (1: Epic's SniperRifle.ServerSetZoomLevel patch) | - |
| UCMutators | 60 | 60 | ok | 579: 421 / 264 | **= shipped** (6: Epic's 3 patches and their objects) | - |
| **GUI** | 94 | 94 | ok | 3526: 2712 / 1528; all 720 functions/states bytecode-identical (Line/TextPos only) | **= shipped** (2: Epic's 2 GUIController patches) | - |
| **UCGameplay** | 232 | 232 | ok | 10842: 8149 / 5178 | **= shipped** (8: Epic's 7 patches + 1 appended import) | - |
| GamePlay | 102 | 102 | ok | 974: 655 / 512 | different (14 problems, 6 findings) | only the `#exec`-imported content: 4 textures + 2 palettes absent, and the 6 class defaults that point at them (`Dispatcher.Texture = S_Dispatcher`) |
| Editor | 16 | 16 | ok | 446: 351 / 131 | different (27 problems, 1 finding) | only the `#exec` content (6 textures, 5 palettes, 2 static meshes) and EditorEngine's defaults pointing at them (its text ships anyway) |
| UCElements | 41 | 41 | 3 default lines rejected | 2956: 2672 / 268 | different (248) | a defaultproperties line over the importer's line limit (~1 KB: a `Scripts=(...)` struct with an object array): its subobjects are then unreferenced |
| UCEffects | 672 | 657 | ok | 3535: 2485 / 950 | different (473) | subobjects nested in subobjects (ActionMoveCamera -> SubActionOrientation) are not emitted |
| **UCMenu** | 171 | 169 | 34 default lines rejected | 10100: 7260 / 4921 | different (19 problems, 205 findings) | static arrays inside struct defaults (`LabelPosn(0)=` rejected), `for` loops whose `continue` is in a nested `if` chain, `Outer.Delegate` context; NOT NEEDED: UCMenu ships with its full text |
| Engine | 262 | 261 | **crash** (SIGSEGV while parsing, after TerrainMaterial; exit 139, not the memory ceiling) | - | not compared | not diagnosed (time box); Engine also has 68 `#exec` textures that cannot come back |

Memory: every `ucc-bin` run under `bin/capped 3G` (script-build.sh default), Python 2G, the build 6G (-j2); no
ceiling hit (no exit 137; the one non-zero exit is Engine's SIGSEGV, 139).

Answer to the spike's question: **yes for the three packages we patch** (GUI and UCGameplay decompile into text
that `ucc-bin make` compiles to the shipped bytecode, every function byte-identical, the only differences being
Line/TextPos bookkeeping and Epic's load-time patches; UCMenu needs no decompiler). Not for all 17 from the `.u`
alone: Engine/Editor/GamePlay contain `#exec`-imported content that no decompiler can turn back into source, and
the prototype still has known gaps in UCElements/UCEffects/UCMenu/Engine.

