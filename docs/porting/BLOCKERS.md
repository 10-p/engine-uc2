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
- Post-processing (M7), four things only the owner can settle, each with a switch and a picture pair
  (`tools/dev/m7-effects.sh`, `analysis/postprocess.md`):
  1. Depth-of-field blur alpha: script's value (100 in third person; default) or the shipped binary's (65):
     `DOFShippedBlurAlpha` / console `DOFSHIPPEDALPHA`; pair `logs/m7/fx-dof-alpha-a.png` / `-b.png`.
  2. Dynamic gamma is on with post-processing because the Xbox build turns it on; on a dark map it brightens
     the frame a lot (`fx-dyngamma-a.png` with, `-b.png` without). Console `DYNAMICGAMMA 0`.
  3. Distortion amounts are scaled by height/480 so the effect covers the same share of the picture as on a
     640x480 Xbox frame (one constant, `PixelScale` in `UC2GLShaders.cpp`); in pixels it would be 1.5 times
     weaker at 720p.
  4. The pause overlay alone turns the scene into a teal negative (`fx-pause-b.png`): that is what the ramp
     texture and Epic's shader give; nobody here knows what the Xbox pause screen looks like.
  None of the "on" pictures has a reference: an Xbox capture of DM-Praxis (the round room), DM-Deadbolt (first
  start) and DM-Azure (first start) would settle more than anything else.
- Optional tool: `sudo apt install xdotool` would make input-injection tests one-liners. Not blocking: key
  and mouse events are injected through the X test extension with a few lines of Python (M10's
  `tools/dev/m10-inject.py`; `python3-xlib` is not installed either, ctypes is used).
- M10, pads: no physical pad on this machine. The tests use SDL's virtual game controller, so everything after
  SDL is exercised, but SDL's own device mapping of a real pad (and real rumble motors) is not. `/dev/uinput`
  is writable for user ion (seat ACL), so a kernel-level virtual pad would be possible, but it would appear on
  the owner's whole desktop session while a test runs; not used. Wanted from the owner: one session with a real
  pad (any XInput/PlayStation pad SDL knows): menu with D-pad/A/B, a match with sticks/triggers, unplug and
  replug, and whether rumble is felt; `uc2.bin -inputlog` logs every pad event as `SDL2Input:` lines.
- Scope, to decide when reached: split-screen on PC; web multiplayer visible at launch; arm64 hardware.

## Incident, 2026-10-04: test run exhausted the machine's memory

The M4w agent's browser smoke test (W2) ran the WebAssembly client in headless Chromium, and at least once in
the Claude desktop app's browser pane, with no memory limit. Browser processes reached 6-9 GB each (kernel
log: three at once, about 24 GB), crashed repeatedly with V8's out-of-memory abort, were retried, and the
kernel killed the desktop app. Two harness servers and one headless Chromium were still running afterwards
and were stopped by hand. Cause in the brief: no ceiling, no retry limit, no ban on the desktop's own browser,
although the sibling workspace's notes already describe a memory guard for its browser tests. Fixed by
`<WorkUC2>/bin/capped` and the "Running anything" rules in `CLAUDE.md`.
Still to do before W2 is attempted again: find where the memory goes (the uncommitted harness in
`../engine-uc2-m4w/tools/web-smoke/ufront-glue.js` fetches each game file whole and keeps it in JavaScript
memory; the build allows unbounded wasm memory growth), under a 4G ceiling.
Left on the machine: 1.4 GB of compressed core dumps in `/var/lib/systemd/coredump` (root-owned) and 16 KDE
crash-notifier processes; both are the owner's to clear.

## Open technical questions

- M6 leftovers, **not done at M7 either** (`analysis/postprocess.md`, "Deferred"): the 12 projector pixel
  shaders need the engine's `_XBOX`-only projector batching (`UnRenderBatch.cpp` 340-905, about 560 lines that
  also depend on packed normals and push buffers, plus the static-mesh and BSP callers) and three more
  vertex-shader fragments; `PS_Terrain3Layer` needs the device to report three simultaneous terrain layers
  and the `_XBOX` parts of `UnTerrain.cpp`. Both are translated and tested numerically, neither is drawn.
  Also not lifted: the pixel-shader shadow blur (`PS_ShadowBlur`, `UnShadowProjector.cpp`), which is not
  post-processing and would change every shadow.
- GL4ES and the M6 shaders: every generated fragment shader compiles as GLSL ES 1.00 on Mesa; the vertex
  shaders are not yet compiled as ES and nothing has run through GL4ES itself (M11).
- Per-bank packed OGG or one file per wave? (decide at M8)
- Localised defaults of native classes look empty in one place: `ucc help` prints blank names and descriptions
  for native commandlets. Unknown whether script reading `class'X'.default.<localized property>` of a native
  class is affected too (menus would show blank text). Check as soon as text is on screen (M5).
- `Default.ini` suppresses the `DevUI`/`DevGUI` log names (Epic's setting, kept), so menu activity is not in
  the log by default. Tests that need it remove those two lines from the run tree's `Avaris.ini` only.
- The community one-byte `Core.dll` patch (file offset 0x940c0, first byte of a non-exported routine near the
  end of `.text`, replaced by RET): which function is it? A hint at a PC-only crash; identify if a Core
  crash shows up at M3/M4.

## M8 audio: owner decisions (2026-10-04, details in `analysis/audio.md`)

- Zone reverb: Epic's Xbox code has the zone -> reverb preset switch inside `#if 0` (`XACTAudioSubsystem.cpp`
  `NotifyZoneChange`), so the shipped game never changed reverb per zone. M8 implements it (the brief asks for
  it) behind a setting; which should be the default?
- Localised taunts in the wrong slots: in `A_TauntSapphire_{deu,fra,ita,spa}` (and one `A_Tutorial1_spa` line)
  the localised wave sits at a different index than the English one, so the Xbox played the wrong line in those
  languages. The cook keeps the Xbox order; `tools/cook-audio.py --relink-localised` repairs it by name.
- 19 wave entries have no source WAV, but the shipped `.xwb` has them (ffmpeg decodes the Xbox ADPCM when it is
  wrapped as WAVE format 0x69). Decode those 19 from the `.xwb` in the cook, or leave the variations out (now)?
- M8 inferences without documentation, for whoever can check against an Xbox: XACT priority direction (we use
  lower value = more important), the "Recurring" flag of volume/pitch events read as "fade", the cue radius of
  non-3D sounds (we use DirectSound's defaults, 4 units: such cues on other actors are culled). Details and the
  evidence in `analysis/audio.md`.
