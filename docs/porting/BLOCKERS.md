# Blockers and open questions

Audited 2026-10-10 with the owner ("make sure that all of them are needed or close them"): the closed entries are
listed at the end with the reason; PLAN.md "Owner review of 2026-10-10" has the task list (D-1..D-4 ours, N-1..N-5 the owner's).

## Open
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
- M10, pads: no physical pad on this machine. The tests use SDL's virtual game controller, so everything after
  SDL is exercised, but SDL's own device mapping of a real pad (and real rumble motors) is not. `/dev/uinput`
  is writable for user ion (seat ACL), so a kernel-level virtual pad would be possible, but it would appear on
  the owner's whole desktop session while a test runs; not used. Owner (2026-10-06): tested later, from CI builds once the engine is on GitHub. Wanted then: one session with a real
  pad (any XInput/PlayStation pad SDL knows): menu with D-pad/A/B, a match with sticks/triggers, unplug and
  replug, and whether rumble is felt; `uc2.bin -inputlog` logs every pad event as `SDL2Input:` lines.
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
- **Not closed (needs a profile-aware change and a two-profile test)**: the character. A menu-driven launch takes it from
  the active profile; a launch that names no class (the web launcher's Play form, a console `open`) takes
  `[DefaultPlayer] Class`, the previous launch's character, which after switching profile without launching is the OTHER
  profile's. Fix, if wanted: put the profile's `CharacterClassName` (URL form, `CharacterInformation.GetCharacterClass`) into
  the player's login URL in `MatchViewportsToActors`, like `UC2ProfileNameInURL` does for the name.
- **Static link registers two classes in the wrong package (pre-existing, not M14's)**: `UHTTPDownload` and
  `UPrefab` say `DECLARE_CLASS(...,Engine)` but live in the IpDrv/Editor DLLs, which registered them under
  their own package; our static build registers them as `Engine.HTTPDownload`/`Engine.Prefab`. Default.ini's
  `DownloadManagers=IpDrv.HTTPDownload` therefore names a class that does not exist here (package download has
  never been exercised). Found by the header-export comparison.

- **Closed on branch `g7-profiles` (G-7, 2026-10-10; merge pending)**: the character. A launch straight into a map with a
  saved profile active now plays the profile's character (`UC2Profiles.cpp` `UC2ProfileInURL`: Epic's
  `PlayerProfile.GetURL` puts the `Class=` into the primary viewport's login URL in `MatchViewportsToActors`), and
  `[DefaultPlayer] Class` is no longer overwritten while a profile is active, so without a profile the ini's class
  plays. Two-profile test `tools/dev/d3-character.sh` ALL PASS; on main's engine it fails at exactly these points
  (analysis/profiles.md G-7 step 3). Still open: a direct NETWORK join from the command line logs in with
  `[DefaultPlayer]` Name and Class (not on the `MatchViewportsToActors` path).

## Closed in the 2026-10-10 audit

- (was line 18) Reddit thread by VBelov_UZ (`/r/unrealtournament/comments/1t9dn3v`): blocked for the agent's fetch tools. Only needed if it says more than `referenc… — **closed:** the owner saved the thread (2026-10-06); only the key bindings mattered
- (was line 35) Optional tool: xdotool **installed 2026-10-06**; it would make input-injection tests one-liners. Not blocking: key and mouse events are injected thr… — **closed:** installed; tests use the engine's input channel instead
- (was line 44) Scope, to decide when reached: split-screen on PC; web multiplayer visible at launch; arm64 hardware. … — **closed:** decided: split-screen G-6 (2026-10-10), web multiplayer E-2 done, arm64 hardware still open (N-2)
- (was line 63) M6 leftovers, **not done at M7 either** (`analysis/postprocess.md`, "Deferred"): the 12 projector pixel shaders need the engine's `_XBOX`-only proje… — **closed:** done by M7b (projectors) and M11
- (was line 71) CLOSED 2026-10-05 (branch `net-join`, `analysis/network.md`): network join. The client's `check(GObjBeginLoadCount)` in `UObject::GetPackageLinker` … — **closed:** network join done (net-join)
- (was line 80) M6 leftovers: the 12 projector pixel shaders need the engine's `_XBOX`-only projector batching lifted behind a capability (same kind of change as M7… — **closed:** done by M7b
- (was line 84) GL4ES and the M6 shaders: every generated fragment shader compiles as GLSL ES 1.00 on Mesa; the vertex shaders are not yet compiled as ES and nothin… — **closed:** done by M11, superseded by F-3 (WebGL 2 / GLSL ES 3.00)
- (was line 86) Per-bank packed OGG or one file per wave? (decide at M8)… — **closed:** decided at M8: per-bank packs
- (was line 87) Localised defaults of native classes look empty in one place: `ucc help` prints blank names and descriptions for native commandlets. Unknown whether… — **closed:** cosmetic; no effect found in play
- (was line 92) The community one-byte `Core.dll` patch (file offset 0x940c0, first byte of a non-exported routine near the end of `.text`, replaced by RET): which … — **closed:** irrelevant: we build from source
- (was line 98) Zone reverb: Epic's Xbox code has the zone -> reverb preset switch inside `#if 0` (`XACTAudioSubsystem.cpp` `NotifyZoneChange`), so the shipped game… — **closed:** owner 2026-10-06: zone reverb stays on
- (was line 101) Localised taunts in the wrong slots: in `A_TauntSapphire_{deu,fra,ita,spa}` (and one `A_Tutorial1_spa` line) the localised wave sits at a different … — **closed:** owner 2026-10-06: Xbox behaviour kept
- (was line 106) M8 inferences without documentation, for whoever can check against an Xbox: XACT priority direction (we use lower value = more important), the "Recu… — **closed:** no Xbox check possible; kept as a note in analysis/audio.md (N-5)
- (was line 124) **Cooked sound and movies for the web (coordinator's decision).** The frontend builds the uc2 target from ONE asset root, `assets/uc2`, which has no… — **closed:** decided and done (M11, F-6 cook)
- (was line 135) In first person, standing still, the waterfall's distortion is drawn every frame on Mesa llvmpipe and on this machine's NVIDIA Quadro P620 (headless… — **closed:** O-6: owner 2026-10-09 "everything seems fixed"
- (was line 142) Side finding for a separate task: on the NVIDIA GPU the fixed-function path has 4 texture units and no `combine3`; materials with `D3DTOP_MODULATEAL… — **closed:** done by O-9/O-9b
- (was line 148) **Decisions for the owner/coordinator, found while proving the rebuilt scripts equal to shipped + patches** (none is changed by M14; each is a behav… — **closed:** decided and done (M14, saveconfig)
- (was line 163) **Not accessible**: the extension coverage pages of `opengl.gpuinfo.org` (how many drivers have `GL_ATI_texture_env_combine3` / `GL_NV_texture_env_c… — **closed:** moot since F-3 (WebGL 2)
- (was line 167) **Seen, not O-9's (both binaries, NVIDIA and Mesa)**: `POSTPROCESS 0` at run time leaves the DM-Azure cobra waterfall drawn as an opaque violet stre… — **closed:** the POSTPROCESS console toggle is an A/B aid (N-1 decides its fate); starting with UsePostProcess=False draws the PC fallback by design
- (was line 173) **Decision for the owner/coordinator, found while enabling script `SaveConfig()`**: a script save of a BASE class writes that class's defaults over … — **closed:** decided and done (saveconfig: a base-class save leaves out what a subclass sets)
- (was line 185) **Epic's `UCPlayerInput.UpdateSensitivityX` assigns the instance twice** (`MouseSensitivity_X = F; MouseSensitivity_X = F;`), where `UpdateSensitivi… — **closed:** cosmetic, Epic's code, unchanged
- 19 wave entries have no source WAV, but the shipped `.xwb` has them (ffmpeg decodes the Xbox ADPCM when it is wrapped as WAVE format 0x69). Decode t… — **closed:** done by F-6 (decoded from the shipped .xwb)
- `Default.ini` suppresses the `DevUI`/`DevGUI` log names (Epic's setting, kept), so menu activity is not in the log by default. Tests that need it re… — **closed:** informational, Epic's setting kept
- (was line 7) Exit-time crash on a dedicated server after SIGTERM, both widths (M13 censuses): `StaticExit` -> `PurgeGarbage` "crashes" in `DetachExport`/`realloc`, then a hang in `appExit`… — **closed (D-1, branch d12-fixes, `analysis/d12-fixes.md`):** not a crash: `timeout` signals the server and its process group, and at the second SIGTERM Epic's `HandleSignal` ran its abort path (`appExit` inside the signal handler) over the main thread's own exit, or deadlocked on a lock the interrupted thread held. Repeated exit signals are now ignored, the handlers only write and set `GIsRequestingExit`, the crash path arms `alarm(10)`, the file-stream wait is bounded; `map-census.sh` uses `timeout -k` and fails hangs/crash handlers. Censuses 58/58 at both widths, ASan census 58/58 with no report beyond Epic's two known script over-reads
- (was line 47) Static link registers `UHTTPDownload`/`UPrefab` as `Engine.HTTPDownload`/`Engine.Prefab`; `DownloadManagers=IpDrv.HTTPDownload` names a class that does not exist… — **closed (D-2, branch d12-fixes):** `DECLARE_CLASS(...,IpDrv)` / `(...,Editor)` as Epic's DLL build registered them; HTTPDownload's non-blocking connect also accepts `EINPROGRESS` (as engine-ut2004); package download exercised: `net-test.sh download download-http` 12/12 PASS (ChannelDownload, and HTTP through `IpDrv.HTTPDownload`)
