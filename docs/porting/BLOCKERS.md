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
- arm64 (M13, F-4c): the cross compiler (`g++-aarch64-linux-gnu`, GCC 15.2) and `qemu-user` + `qemu-user-binfmt` are
  installed (owner, 2026-10-06; checked 2026-10-09 with `dpkg -l`). `cmake --preset arm64` configures and `ucc-bin`
  builds and verifies under `qemu-aarch64` (F-4c, `analysis/platform-f4.md`). Still missing, for the arm64 **client**
  only: the arm64 X11/GL development packages through multiarch (`sudo dpkg --add-architecture arm64`, then
  `libx11-dev:arm64 libxext-dev:arm64 libxrandr-dev:arm64 libxcursor-dev:arm64 libxi-dev:arm64 libxss-dev:arm64
  libgl-dev:arm64 libasound2-dev:arm64 libpulse-dev:arm64`): without them the fetched SDL2 configures with X11 off.
  arm64 hardware to run on is an open owner question (M13).
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

## O-1/O-2 sound and movies (2026-10-06, branch `m11-audio`, details in `analysis/web-audio.md`)

- **PipeWire for the 32-bit game (optional; tell the owner).** The PipeWire backend of OpenAL Soft is compiled
  when `libpipewire-0.3-dev` is installed (it was installed on this machine during the session, amd64 and
  i386, at 22:11 on 2026-10-06; a reconfigure picked it up: `Building OpenAL with support for the following
  backends: PipeWire, PulseAudio, ALSA, OSS, WaveFile, Null`). At run time the 32-bit backend then fails:
  `Failed to create PipeWire event context (errno: 2)`, because the i386 client modules
  (`/usr/lib/i386-linux-gnu/pipewire-0.3/libpipewire-module-protocol-native.so` and the rest) are not
  installed. The game falls back to PulseAudio, which PipeWire serves through its pulse server, and the log
  says so (`order pipewire,pulse,alsa,oss: pipewire failed, pulse initialised; playing through pulse`).
  Sound works either way. To get the native PipeWire backend in the 32-bit game:
  `sudo apt install libpipewire-0.3-modules:i386` (the 64-bit game, preset `native64`, has the amd64 modules
  already). A player's machine needs the same package for the same reason; nothing else changes.
- **Cooked sound and movies for the web (coordinator's decision).** The frontend builds the uc2 target from ONE
  asset root, `assets/uc2`, which has no `Sounds/`, `Music/`, `Movies/`, `Textures/Movies/` (they are in
  `assets/uc2-cooked`), so the committed `targets/uc2/file-manifest.json` has no sound and no movie and the web
  game runs silent (it says so once in the log) with black/fallback movie textures. Proposal: hard-link the four
  cooked folders into `assets/uc2` (`cp -al`, as `tools/dev/mkrun-uc2.sh` does for run trees), regenerate the
  target's manifest (`generate-manifest.cjs assets/uc2 targets/uc2/file-manifest.json --exclude-from
  targets/uc2/target.json`: 1,223 files, 1,899 MB; `manifestExclude` already drops the cook's by-products), then
  add the precache candidates listed in `targets/uc2/README.md` (cue tables, menu music and page sound, logos:
  about 7 MB). Tested meanwhile on a local tree (`run/m11audio-assets`, `analysis/web-audio.md` step 3).
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

## M14 scripts (2026-10-07, branch `m14-scripts`, details in `analysis/scripts.md`)

- **Decisions for the owner/coordinator, found while proving the rebuilt scripts equal to shipped + patches**
  (none is changed by M14; each is a behaviour difference between the shipped scripts and the source):
  - `UCGameplay.CTFFlag.home`: Epic's script patch for it is defective (wrong label name, stale label-table
    offset), so with the shipped scripts a flag returning home never runs `Sleep(0.1); CheckTouching()`. The
    rebuilt packages run it as the source says. Presumably the Xbox title update had the same defect.
  - `GUIController.bUnlockCharacters`/`bShowAllMuts` are `config` in the shipped GUI.u, not in the source: the
    community's `bUnlockCharacters=True` ini tweak stops working with rebuilt scripts (D-phase can restore it).
- **Static link registers two classes in the wrong package (pre-existing, not M14's)**: `UHTTPDownload` and
  `UPrefab` say `DECLARE_CLASS(...,Engine)` but live in the IpDrv/Editor DLLs, which registered them under
  their own package; our static build registers them as `Engine.HTTPDownload`/`Engine.Prefab`. Default.ini's
  `DownloadManagers=IpDrv.HTTPDownload` therefore names a class that does not exist here (package download has
  never been exercised). Found by the header-export comparison.

## O-9 NVIDIA fixed-function gap (2026-10-07, branch `o9-nvidia`, details in `analysis/o9-nvidia.md`)

- **Not accessible**: the extension coverage pages of `opengl.gpuinfo.org` (how many drivers have
  `GL_ATI_texture_env_combine3` / `GL_NV_texture_env_combine4`) are filled by script; WebFetch gets only the page
  frame. The O-9 decision (GLSL for the texture stages) does not depend on the numbers, but they would have been
  the evidence for "combine3 or combine4 everywhere". If the owner wants them, a browser can read the pages.
- **Seen, not O-9's (both binaries, NVIDIA and Mesa)**: `POSTPROCESS 0` at run time leaves the DM-Azure cobra
  waterfall drawn as an opaque violet stream (`logs/o9/fx-azure-*-nopp.png`): the distortion material's
  fallback when post-processing is switched off while running. Starting with `-nopostprocess` was not checked.

## saveconfig (2026-10-08, branch `saveconfig`, details in `analysis/saveconfig.md`)

- **Decision for the owner/coordinator, found while enabling script `SaveConfig()`**: a script save of a BASE class
  writes that class's defaults over what its subclasses set on purpose, because `globalconfig` properties are read from the
  declaring class's section into every subclass (naively enabled, `SetWeaponHand` pinned `UCPlayerController.DesiredFOV`
  to 85 instead of 90). The natives now write only what changed and leave out a property another class sets on purpose
  (`UC2NoteDefaultOverrides`, UnObj.cpp); a script that wants exactly one key calls `SAVECONFIGKEYS` through
  `ConsoleCommand`. The sites in `Engine.u` (`SetHand`, `UpdateSensitivity`...) were NOT changed (a rebuilt Engine.u is a
  bigger switch than this task): they work through the native rule.
- **Not closed (needs a profile-aware change and a two-profile test)**: the character. A menu-driven launch takes it from
  the active profile; a launch that names no class (the web launcher's Play form, a console `open`) takes
  `[DefaultPlayer] Class`, the previous launch's character, which after switching profile without launching is the OTHER
  profile's. Fix, if wanted: put the profile's `CharacterClassName` (URL form, `CharacterInformation.GetCharacterClass`) into
  the player's login URL in `MatchViewportsToActors`, like `UC2ProfileNameInURL` does for the name.
- **Epic's `UCPlayerInput.UpdateSensitivityX` assigns the instance twice** (`MouseSensitivity_X = F; MouseSensitivity_X = F;`),
  where `UpdateSensitivityY` sets `default.MouseSensitivity_Y`: the X default is never updated. Left as it is (identical in
  the shipped package, no effect on the mouse since D-6).

