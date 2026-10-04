# Blockers and open questions

Newest first. Remove an entry when it is resolved and say how in `LOG.md`.

## Needs the owner

- Reddit thread by VBelov_UZ (`/r/unrealtournament/comments/1t9dn3v`): blocked for the agent's fetch tools.
  Only needed if it says more than `reference/community/uc2-test-suite/README_TESTER.md`.
- Xbox-look visual oracle (wanted at M6/M7): xemu plus its MCPX ROM, BIOS and HDD image.
- arm64 (M13): `sudo apt install g++-aarch64-linux-gnu qemu-user`.
- Canonical config (M15): retail and leak disagree on `Characters.ini`, `Gametypes.ini`, `Maplists.ini` and 14
  localisation files; the owner should see the per-file diff before one side is chosen.
- Depth-of-field alpha (visible from M7): we corrected a header that had fallen behind its script, which
  changes one blur constant from what the shipped binary used (`analysis/runtime-port.md`, "Deviation").
  The owner should confirm which behaviour to keep once the effect is on screen.
- Scope, to decide when reached: split-screen on PC; web multiplayer visible at launch; arm64 hardware.

## Open technical questions

- Does GL4ES accept fragment-only GLSL with fixed-function vertex processing? (spike at M5)
- One GL driver source built twice, or two mirrored directories as in engine-ut2004? (decide at M5)
- Per-bank packed OGG or one file per wave? (decide at M8)
- Commandlet and registry short names (`ucc server`, game-type and mutator lists) come up empty: the registry
  reads `.int` files from where UT2003 kept them, UC2 keeps them in `System/Localization/<lang>/`. (M4)
- The community one-byte `Core.dll` patch (file offset 0x940c0, first byte of a non-exported routine near the
  end of `.text`, replaced by RET): which function is it? A hint at a PC-only crash; identify if a Core
  crash shows up at M3/M4.

## M4 notes (client boot, 2026-10-04; details in `analysis/client-boot.md`)

- Resolved, for the coordinator to remove above: "Commandlet and registry short names". The registry was
  empty because the Unix file manager matched wildcards case-sensitively (`*.INT` vs `*.int`); fixed in
  `Core/Inc/FFileManagerUnix.h`. `ucc server <map>` works by short name.
- Open, cosmetic: `ucc help` prints empty names/descriptions for native commandlet classes (localized
  defaults not on the class default object in UCC's flow). Check at M5 whether script reading
  `class'X'.default.<localized>` of a native class sees the same.
- Tools: `xdotool` and `python3-xlib` are not installed. Not blocking: input was injected through XTEST with
  Python `ctypes` (`libXtst.so.6` is present). `sudo apt install xdotool` would make such tests one-liners.
- `Default.ini` suppresses the `DevUI`/`DevGUI` log names, so menu activity is not in the log by default
  (Epic's setting, kept). Tests that need it use a run-tree `Avaris.ini` without those lines.
