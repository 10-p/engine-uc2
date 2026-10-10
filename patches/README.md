# Script patches

Unreal Championship 2's game logic is UnrealScript, compiled into the `.u` packages in the game's `System`
folder. This repository holds none of that script. What it holds are **patch sets**: our changes to the scripts,
as plain unified diffs against the scripts of **your own copy of the game**. The build turns your disc's `.u`
files into text, applies the sets you choose, and compiles the result. Nothing of Epic's script text comes from
here; everything comes from your files.

Without any of this the game runs the shipped scripts unchanged (with Epic's title-update fixes, applied in memory
when a package loads); the patch sets are optional extras.

## How a build goes

1. **Base.** `tools/scripts/uc-decompile.py <System>/<Pkg>.u --out <base>/<Pkg>/Classes` writes one `.uc` per
   class, decompiled from the package. UCMenu is the exception: the shipped `UCMenu.u` still carries Epic's own
   text, so `--shipped-text` writes that text (with a decompiled `defaultproperties` block, which the stored
   text lacks). The base is the same, byte for byte, for everyone with the same disc: line 1 of every file names
   the decompiler version and the `.u` it came from (`// decompiled by uc-decompile 1.0 from GUI.u 2eb7e89933c0`).
2. **Apply.** `tools/scripts/uc-patches.py apply --base <base> --default --out <tree>` applies the sets in the
   order of `sets.txt`. A hunk must match its context exactly (no fuzz); if it does not, the command stops and
   says which hunk.
3. **Build.** `tools/scripts/uc-build.sh --classes <tree> --packages GUI,UCGameplay,UCMenu --out <dir>` compiles
   each package with the game's own script compiler (`ucc-bin make`) against your shipped packages and strips
   the text as Epic did. The result is `<dir>/System/<Pkg>.u`.

`uc-patches.py packages --default` prints the packages the default sets rebuild. Those are the only packages that
change: every other package stays the shipped one.

## The sets (`sets.txt`)

| Set | What it gives | Packages | Default |
|---|---|---|---|
| `epic-fixes` | Epic's 30 load-time script fixes (`ScriptPatches.h`, the Xbox title update) written as script | Engine, UnrealGame, UCGameplay, UCInventory, UCMutators, GUI, UCMenu | on |
| `epic-source` | three changes in Epic's later source that the load-time patcher could not carry | UCGameplay | on |
| `mouse-look` | the PC mouse: mouse look apart from the pad, the rigid/lazy third-person camera, double-tap dodge | UCGameplay | on |
| `player-name` | the launcher's player name when there is no profile | GUI | on |
| `keys-menu` | the KEYBOARD / MOUSE settings page | UCMenu | on |

Each set's folder has a `README.md` (what it changes, its ini keys, what it depends on), one `<Pkg>.diff` per
package it changes (paths `<Pkg>/Classes/<Class>.uc`; `patch -p1` reads them too) and whole new classes under
`new/<Pkg>/Classes/`.

**`epic-fixes` must be on whenever a package is rebuilt.** The in-memory patcher only patches the packages it was
made for (the shipped files); a rebuilt package is skipped, so it must carry Epic's fixes itself. Every other set
depends on it.

**Packages that only `epic-fixes` touches are not rebuilt by default** (Engine, UnrealGame, UCInventory,
UCMutators): the shipped files get the same fixes in memory. To rebuild them anyway, name them:
`uc-patches.py packages --sets epic-fixes --with-epic-only` lists them, and `uc-build.sh --packages` takes them.
Engine cannot be rebuilt completely from the disc: it contains editor icons imported from image files the disc
does not have (see `docs/porting/analysis/script-patches.md`).

## Changing a set (developers)

Sets are generated, never edited by hand: `uc-patches.py make <set> --base <tree> --changed <tree>` writes the
set's diffs from two trees. `tools/dev/uc-patches-regen.sh` regenerates all of them from a build of our sources,
and `tools/scripts/uc-patches-proof.sh` proves that the sets compile to what they should (P1: base + `epic-fixes`
equals the shipped packages plus Epic's in-memory patches; P2: base + all sets equals our reference build; P3:
everything is deterministic). When `uc-decompile.py` changes, the base text changes: `uc-patches.py rebase`
re-makes every set against the new base and the proof shows that the compiled result did not move.

Files are Latin-1 with LF line ends; some strings contain 0x02 bytes (button glyphs). The tools read and write
bytes and the diffs carry those bytes unchanged.
