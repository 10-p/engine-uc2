# Script patches

Unreal Championship 2's game logic is UnrealScript, compiled into the `.u` packages in the game's `System`
folder. This repository holds none of that script. What it holds are **patch sets**: our changes to the scripts,
as plain unified diffs against the scripts of **your own copy of the game**. The build turns your disc's `.u`
files into text, applies the sets you choose, and compiles the result. Nothing of Epic's script text comes from
here; everything comes from your files.

Without any of this the game runs the shipped scripts unchanged (with Epic's title-update fixes, applied in memory
when a package loads); the patch sets are optional extras.

## Building them

The engine build does all of this for you (`BUILD.md`, "Script patches"):

```
cmake --preset native -DUC2_ASSETS_DIR=/path/to/your/game/folder      # System/ with the 17 shipped .u
cmake --build build-native                                           # -> build-native/Scripts/System/*.u
```

`-DUC2_SCRIPT_PATCH_SETS=epic-fixes;mouse-look` picks sets (default: those marked `on` in `sets.txt`);
`-DUC2_SCRIPT_PATCHES=OFF` builds the engine without any. The build first checks that your `.u` are the shipped ones
(`shipped.sha256`, the retail disc's files: `sha256sum -c shipped.sha256` inside your `System` folder checks them by
hand); a package rebuilt from source cannot be the base and is refused.

## How a build goes

1. **Base.** `tools/scripts/uc-decompile.py <System>/<Pkg>.u --out <base>/<Pkg>/Classes` writes one `.uc` per
   class, decompiled from the package. UCMenu is the exception: the shipped `UCMenu.u` still carries Epic's own
   text, so `--shipped-text` writes that text (with a decompiled `defaultproperties` block, which the stored
   text lacks). The base is the same, byte for byte, for everyone with the same disc: line 1 of every file names
   the decompiler version and the `.u` it came from (`// decompiled by uc-decompile 1.0 from GUI.u 2eb7e89933c0`).
2. **Apply.** `tools/scripts/uc-patches.py apply --base <base> --default --out <tree>` applies the sets in the
   order of `sets.txt`. A hunk must match its context exactly (no fuzz); if it does not, the command stops and
   says which hunk.
3. **Build.** `tools/scripts/uc-build.sh --classes <tree> --packages UCAnnouncements,UCGameplay,GUI,UCMenu
   --keep UCGameplay,GUI,UCMenu --out <dir>` compiles each package with the game's own script compiler
   (`ucc-bin make`) against your shipped packages and strips the text as Epic did. The result is
   `<dir>/System/<Pkg>.u`. UCAnnouncements is compiled only so UCGameplay can be (the shipped one refers to
   packages that come after it); `uc-patches.py packages --default --compile-list <System>` prints the list.

`uc-patches.py packages --default` prints the packages the default sets rebuild. Those are the only packages that
change: every other package stays the shipped one.

## The sets (`sets.txt`)

| Set | What it gives | Packages | Default |
|---|---|---|---|
| `epic-fixes` | Epic's 30 load-time script fixes (`ScriptPatches.h`, the Xbox title update) written as script | Engine, UnrealGame, UCGameplay, UCInventory, UCMutators, GUI, UCMenu | on |
| `epic-source` | three changes in Epic's later source that the load-time patcher could not carry | UCGameplay | on |
| `mouse-look` | the PC mouse: mouse look apart from the pad, the rigid/lazy third-person camera, double-tap dodge | UCGameplay | on |
| `player-name` | the launcher's player name when there is no profile | GUI | on |
| `players34` | players three and four join from the character page (a cursor each, a line of text, their own character and name in the match; a 2x2 armory for three or four) | GUI, UCMenu | on |
| `key-glyphs` | player one's keys in place of the pad's button glyphs while they use the keyboard or the mouse (the engine draws them; the set switches it on) | GUI, UCMenu | on |
| `keys-menu` | the KEYBOARD / MOUSE settings page | UCMenu | on |

Each set's folder has a `README.md` (what it changes, its ini keys, what it depends on), one `<Pkg>.diff` per
package it changes (paths `<Pkg>/Classes/<Class>.uc`; `patch -p1` reads them too) and whole new classes under
`new/<Pkg>/Classes/`. A set's resources, files its new classes import with `#exec` (key-glyphs' `Textures/
KeyGlyphSheet.tga`), sit beside them in `new/<Pkg>/<folder>/`: `uc-patches.py apply` copies them into the tree as
`<Pkg>/<folder>/`, `uc-build.sh` links them beside the package's `Classes` (where the compiler looks: `FILE=Textures\...`
is relative to the package's folder), and `uc-patches.py make` leaves them alone (it re-makes only diffs and classes). A
resource that is generated says so, with its generator, in the set's README.

**`epic-fixes` must be on whenever a package is rebuilt.** The in-memory patcher only patches the packages it was
made for (the shipped files); a rebuilt package is skipped, so it must carry Epic's fixes itself. Every other set
depends on it.

**Packages that only `epic-fixes` touches are not rebuilt by default** (Engine, UnrealGame, UCInventory,
UCMutators): the shipped files get the same fixes in memory. To rebuild them anyway, name them:
`uc-patches.py packages --sets epic-fixes --with-epic-only` lists them, and `uc-build.sh --packages` takes them.
Engine cannot be rebuilt completely from the disc: it contains editor icons imported from image files the disc
does not have (see `docs/porting/analysis/script-patches.md`).

## Changing a script: change its set (developers)

This repository holds no UnrealScript, not even ours: there is no source tree to edit (the sources left on
2026-10-11, `docs/porting/analysis/scripts-removed.md`). **A change to the game's scripts is a change to a patch set**,
made on the text the build decompiles from your own `.u` files. Sets are generated, never edited by hand: you edit
classes in a scratch tree and `uc-patches.py make` writes the set's diffs. A set is a diff against the base **plus every
set before it** in `sets.txt`, so you edit a tree of exactly that (not `Scripts/tree`, which has the later sets in it
too). Example: one change to `UCPlayerInput` in the `mouse-look` set.

```
cmake --preset native && cmake --build build-native -j4         # 1. build: Scripts/base (decompiled), Scripts/tree (patched)
tools/dev/uc-patches-regen.sh edit mouse-look                   # 2. build-native/Scripts/edit-mouse-look/{before,edit}
$EDITOR build-native/Scripts/edit-mouse-look/edit/UCGameplay/Classes/UCPlayerInput.uc
                                                                # 3. edit (Latin-1, LF; a new file = a new class)
tools/dev/uc-patches-regen.sh make mouse-look                   # 4. patches/mouse-look/ re-made; the default sets checked
cmake --build build-native                                      # 5. compile -> build-native/Scripts/System/UCGameplay.u
```

Steps 2 and 4 are these commands (the script prints them as it runs them; `W` = `build-native/Scripts/edit-mouse-look`,
the sets before `mouse-look` are `epic-fixes,epic-source`, `--packages` names the packages the base has so that a set's
diffs of other packages, e.g. `epic-fixes`' `Engine.diff`, are kept):

```
tools/scripts/uc-patches.py apply --base build-native/Scripts/base --sets epic-fixes,epic-source --out $W/before
tools/scripts/uc-patches.py apply --base build-native/Scripts/base --sets epic-fixes,epic-source,mouse-look --out $W/edit
tools/scripts/uc-patches.py make mouse-look --base $W/before --changed $W/edit --packages GUI,UCAnnouncements,UCGameplay,UCMenu
tools/scripts/uc-patches.py check --base build-native/Scripts/base --default
```

Then:

6. **See it.** Before step 5, keep the previous build: `cp -a build-native/Scripts/work /tmp/prev` (its `System/` has
   all 17 packages, beside links to the content folders, which the comparison loads). After step 5:
   `tools/scripts/uc-patches.py compare build-native/Scripts/System/UCGameplay.u /tmp/prev/System/UCGameplay.u
   --system-a build-native/Scripts/work/System --system-b /tmp/prev/System` lists every function, state and default
   that differs (`differences 0` when nothing moved). A package that only depends on the changed one can get new bytes
   with `differences 0` (its classes record the CRC of the script text they depend on).
7. **Headers.** A change to a native class's declarations (variables, functions, events of a class with a C++ side)
   changes its generated header: `cmake --build build-native --target check-headers` says STALE with the diff, and
   `tools/dev/regen-headers.sh` writes `Source/<Pkg>/Inc/<Pkg>Classes.h` (a class's `cpptext` and `struct native`
   blocks are not in the shipped packages: they stay as committed; change them by hand when the patch needs it).
8. **Reference.** `patches/reference.sha256` holds the sha256 and size of each package the default build makes; the
   proofs (`cmake --build build-native --target scripts-proof`, P2) fail until it is regenerated:
   `tools/scripts/uc-patches-proof.sh p2 --write-reference`.
9. **Commit** the set, the reference and any header together, and say in the set's `README.md` what changed.

If a later set has a hunk on the lines you changed, step 4's check names it: re-make that set the same way
(`uc-patches-regen.sh edit <later set>`: its trees are built on the set you just changed). Epic's leaked script text
(`<WorkUC2>/epic-sources/<Pkg>/Classes`, on the development machine only, never a build input) is a reading aid: its
comments and names are what the decompiler cannot recover.

## The proofs

`tools/scripts/uc-patches-proof.sh` (CMake target `scripts-proof`, not in ALL) proves that the sets compile to what
they should. **P1**: base + `epic-fixes` equals the shipped packages plus Epic's in-memory patches, function by
function. **P2**: base + the default sets gives exactly the packages of `reference.sha256` (a regression reference;
until 2026-10-11 P2 compared with a build of the repo's own script sources, which are gone; on the development machine
`--ref <WorkUC2>/run/t1-ref/System` still runs that bytecode comparison against T1's build of them). **P3**: decompile,
apply and build are deterministic. When `uc-decompile.py` changes, the base text changes: `uc-patches-proof.sh rebase
--old-decompiler <old uc-decompile.py>` re-makes every set against the new base and shows that the compiled result did
not move.

Files are Latin-1 with LF line ends; some strings contain 0x02 bytes (button glyphs), in the decompiled trees
(`Scripts/base`, `Scripts/tree`) and possibly in a set: search them with `grep -a`. The tools read and write bytes and
the diffs carry those bytes unchanged.
