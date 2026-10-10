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
