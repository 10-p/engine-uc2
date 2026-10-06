# Blockers and open questions

Newest first. Remove an entry when it is resolved and say how in `LOG.md`.

## Open (M13, 2026-10-05)

- Exit-time crash on a dedicated server after SIGTERM, both widths (found by M13's censuses, which only notice it
  as a server that does not end): `UObject::StaticExit` -> `PurgeGarbage` -> `ConditionalDestroy` crashes in
  `ULinkerLoad::DetachExport` (64-bit OD-Overflow; 32-bit ACT-HallwayAmbush, `UObject::IsValid` on a destroyed
  export) or in `realloc` from `UClass::Destroy` (32-bit DM-Nexus: heap already corrupt); about 2 maps in 58 per
  census. The crash handler then calls `appExit`, whose `FFileStream::Destroy` waits for the file-stream thread
  forever, so the process hangs (two seen, killed by PID). `map-census.sh` reports PASS anyway (its `timeout`
  has no `-k`). Linker teardown is the net-join branch's area; not changed by M13. Logs:
  `logs/m13-census/OD-Overflow.log`, `logs/m13-census32/{ACT-HallwayAmbush,DM-Nexus}.log`.

## Needs the owner

- Reddit thread by VBelov_UZ (`/r/unrealtournament/comments/1t9dn3v`): blocked for the agent's fetch tools.
  Only needed if it says more than `reference/community/uc2-test-suite/README_TESTER.md`.
- Xbox-look visual oracle: **provided 2026-10-06** (`<WorkUC2>/xbox-emulation/run-uc2-iso.sh`, xemu; slow on this
  machine: last resort). The owner's xemu captures are in `<WorkUC2>/temp/`.
- arm64 (M13): **compiler and qemu-user installed by the owner 2026-10-06**; still missing for the client: `sudo apt install g++-aarch64-linux-gnu qemu-user qemu-user-binfmt` for `cmake --preset arm64`
  (toolchain file and preset exist since M13, never configured: no compiler here). `ucc-bin` needs nothing more; the
  client additionally needs the arm64 X11/GL development packages through multiarch (`sudo dpkg --add-architecture
  arm64`, then `libx11-dev:arm64 libxext-dev:arm64 libxrandr-dev:arm64 libxcursor-dev:arm64 libxi-dev:arm64
  libxss-dev:arm64 libgl-dev:arm64 libasound2-dev:arm64 libpulse-dev:arm64`) because SDL2 is built from source.
- **Decided by the owner, 2026-10-06** (`<WorkUC2>/answers.txt`, PLAN.md "Owner review"): canonical config
  and localisation = the leak (unless a difference changes behaviour); depth-of-field alpha = the shipped value
  65 (`DOFShippedBlurAlpha=True` in Default.ini, O-3); dynamic gamma on, as the Xbox ("it works as it should");
  distortion size: the DM-Praxis panes close up are "spot on" (other distortions: O-5..O-7); the pause overlay's
  teal negative is what the Xbox shows; localised taunt indices: keep the Xbox behaviour (no relink); zone
  reverb: keep on.
- Optional tool: xdotool **installed 2026-10-06**; it would make input-injection tests one-liners. Not blocking: key
  and mouse events are injected through the X test extension with a few lines of Python (M10's
  `tools/dev/m10-inject.py`; `python3-xlib` is not installed either, ctypes is used).
- M10, pads: no physical pad on this machine. The tests use SDL's virtual game controller, so everything after
  SDL is exercised, but SDL's own device mapping of a real pad (and real rumble motors) is not. `/dev/uinput`
  is writable for user ion (seat ACL), so a kernel-level virtual pad would be possible, but it would appear on
  the owner's whole desktop session while a test runs; not used. Owner (2026-10-06): tested later, from CI builds once the engine is on GitHub. Wanted then: one session with a real
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

- CLOSED 2026-10-05 (branch `net-join`, `analysis/network.md`): network join. The client's
  `check(GObjBeginLoadCount)` in `UObject::GetPackageLinker` came from Epic's FScriptPatcher (AppendImport outside
  BeginLoad on a client's USES placeholder packages; latent on the FINAL_RELEASE Xbox build). Fixed with the next
  defects on the join path (package-map counts vs patched-in exports/names, exit-time channel use-after-free, UCC
  commandlet freed under its Main). `tools/dev/net-test.sh` (dedicated server + 1/2 clients, listen server, travel,
  disconnect/timeout, lag/loss, GL screenshot, 10-min soak) passes. Not done: real Windows / wine server against the
  fixed client (the wine `netjoin` smoke check needs a Windows build of this branch on the server side too, since
  both ends must count the package map the same way).

- M6 leftovers: the 12 projector pixel shaders need the engine's `_XBOX`-only projector batching lifted behind a
  capability (same kind of change as M7's post-process gates; do it with M7); `PS_Terrain3Layer` needs the
  device to report three simultaneous terrain layers, which changes every terrain map and wants its own
  before/after pictures. Both are translated and tested numerically, neither is drawn yet.
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

## Owner review O-6 (DM-Azure waterfall, first person): not reproduced (2026-10-07, `analysis/fx-fixes.md`)

- In first person, standing still, the waterfall's distortion is drawn every frame on Mesa llvmpipe and on this
  machine's NVIDIA Quadro P620 (headless, EGL): measured in the distortion accumulation target, moving changes
  nothing but the waterfall's size on screen. Needed from the owner to reproduce: where exactly (a screenshot
  with the HUD, or the console's `NETPROBE x` line), the window size, first or third person at spawn, and
  whether the view had been switched with the camera key before. The "drawn over the character" part is, as
  far as I can see, Epic's half-resolution depth mask and screen-space read (the Xbox's design too); a
  picture of it would settle it.
- Side finding for a separate task: on the NVIDIA GPU the fixed-function path has 4 texture units and no
  `combine3`; materials with `D3DTOP_MODULATEALPHA_ADDCOLOR` (DM-Azure's wooden pillars) fall back to
  MODULATE2X and look like orange metal there, wood on llvmpipe. `GL_NV_texture_env_combine4` can express it.
