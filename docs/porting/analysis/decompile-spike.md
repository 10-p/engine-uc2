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

`batchexport` of a stripped class: (see the step 0b entry below once measured).

## Step 1 (2026-10-10): existing decompilers

- `which dotnet mono msbuild csc`: none installed (no .NET runtime on ion-kubuai; nothing installed, per the rules).
- **UELib / UE Explorer** (Eliot Van Uytfanghe; C#): cloned read-only to `logs/s1/ext/UELib` (commit 3207a17,
  2026-08-15). Its README game table lists "Unreal Championship 2: Liandri Conflict | 3323 | 151/002 | Third-party"
  with a link to a BeyondUnreal thread "unreal-championship-2-script-decompiler-release" (403 to WebFetch, not
  readable here). In the source: an enum value `BuildGeneration.UE2X` ("Modified Unreal Engine 2 for Xbox e.g.
  Unreal Championship 2") that **no build entry uses**; comments about UC2 flags/tokens ("UC2 has this bytecode
  [OutVariable] as 0x49", "EatString -> EatReturnValueToken (also in UC2)", "DebugOnly in UC2 ... 0x00400000"),
  i.e. partial knowledge, no registered UC2 build. The listed package version 151 **licensee 2** is the Xbox
  retail format; our packages (the leak, PC build) are **151/0**. Nothing in the source claims recompilable output
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
