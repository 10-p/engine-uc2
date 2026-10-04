# Blockers and open questions

Newest first. Remove an entry when it is resolved and say how in `LOG.md`.

## Needs the owner

- Reddit thread by VBelov_UZ (`/r/unrealtournament/comments/1t9dn3v`): blocked for the agent's fetch tools.
  Only needed if it says more than `reference/community/uc2-test-suite/README_TESTER.md`.
- Xbox-look visual oracle (wanted at M6/M7): xemu plus its MCPX ROM, BIOS and HDD image.
- arm64 (M13): `sudo apt install g++-aarch64-linux-gnu qemu-user`.
- Canonical config (M15): retail and leak disagree on `Characters.ini`, `Gametypes.ini`, `Maplists.ini` and 14
  localisation files; the owner should see the per-file diff before one side is chosen.
- Scope, to decide when reached: split-screen on PC; web multiplayer visible at launch; arm64 hardware.

## Open technical questions

- Does GL4ES accept fragment-only GLSL with fixed-function vertex processing? (spike at M5)
- One GL driver source built twice, or two mirrored directories as in engine-ut2004? (decide at M5)
- Per-bank packed OGG or one file per wave? (decide at M8)
- The community one-byte `Core.dll` patch (file offset 0x940c0, first byte of a non-exported routine near the
  end of `.text`, replaced by RET): which function is it? A hint at a PC-only crash; identify if a Core
  crash shows up at M3/M4.
