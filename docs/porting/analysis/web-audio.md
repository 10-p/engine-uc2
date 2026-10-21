Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# Sound and movies: native hardening (O-1), the browser (O-2), DM-Azure reverb (O-4)

Running notebook of the `m11-audio` branch (worktree `../engine-uc2-m11audio`), started 2026-10-06 from main's
tip `e7e6d23`. Brief: PLAN.md "Owner review of 2026-10-06", tasks O-1, O-2, O-4. Appended after every step so a
successor can continue. Other agents: `m11-webgl` (the web renderer: UC2GLDrv, the GL parts of
SDLLaunchWeb.cpp and of the web inis, the uc2 e2e boot spec) - not touched from here.

## Step 0 - reading (2026-10-06)

What exists (audio.md, video.md, web-smoke.md):
- Native: `UC2ALAudio` dlopens `libopenal.so.1` (openal-soft 1.25.2 from `10-p/openal-soft-web` `ufront/web`,
  built SHARED by the root CMakeLists M8 block, copied beside the binary by `tools/dev/mkrun-uc2.sh`).
  `UC2ALBind` returns "not built for the web yet" under `__EMSCRIPTEN__`; the package is in
  `UC2_NATIVE_ONLY_PACKAGES`. `UC2Movie` is built everywhere and registered on the web too.
- Web build (`Source/SDLLaunch/cmake/Web.cmake`): PROXY_TO_PTHREAD, Asyncify frame yield, WasmFS + the
  JSImpl backend (`vendor/glue/wasmfs-shim.js`), pool 2, MAXIMUM_MEMORY 2 GB, `-sNO_DISABLE_EXCEPTION_CATCHING`
  everywhere (UC2Web.cmake). `Engine/Config/web/Default.ini`: `AudioDevice=Engine.NullAudioDevice`, no
  `[UC2ALAudio.UC2ALAudioSubsystem]` section.
- Template (engine-ut2004, read-only): openal-soft is linked STATIC into the wasm under Emscripten
  (root CMakeLists "openal-soft on the WEB (ufront 2.61)": `-msimd128 -msse4.1 -fexceptions` for its scope
  only, `ALSOFT_BACKEND_WEB ON`, `ALSOFT_REQUIRE_SSE4_1`, `ALSOFT_EMBED_HRTF_DATA`, NEON/WAVE/DLOPEN off,
  EAX on), SDLLaunch links `OpenAL`, `-sPTHREAD_POOL_SIZE` 2 -> 4 (the web backend's mixer thread + the
  context's event thread), exception catching ON at link (openal-soft throws internally), no `-lopenal`.
- The fork's Web Audio backend (`alc/backends/web.cpp`): mixer on its own pthread renders into a ring in the
  shared wasm memory; an AudioWorklet (`alsoft-web`) reads it; every AudioContext call goes to the browser
  main thread (`emscripten_proxy_sync`). Needs `-pthread`, cross-origin isolation, AudioWorklet; otherwise
  open() fails with NoDevice. **Autoplay unlock is inside the backend**: on context creation it adds
  capture-phase `pointerdown`/`keydown`/`touchend` listeners on `document` that call `ctx.resume()`; the
  frontend's Start click is the gesture. Test hook: `globalThis.__alsoftWeb.stats()` (ctxState, quanta,
  mixerChunks, underruns, ring) and `.drainForTest()`; the frontend's `packages/e2e/lib/audio-skips.js`
  already reads it and taps `__alsoftWeb.node` into an AnalyserNode (`audibleFrac`).
- openal-soft backend order (alc.cpp `BackendList`): pipewire, pulse, alsa, oss, ..., null, wave; the first
  whose `init()` succeeds and supports playback is used (pulse init connects to a server, so no server ->
  alsa). Without `ALSOFT_DRIVERS` the null and wave backends are never chosen. `alsoft_set_log_callback`
  (exported) receives every log line whatever ALSOFT_LOGLEVEL is, including "Initialized backend", "Failed
  to initialize backend", "Added ... for playback": this is how the device can say which backend it got.
- **PipeWire headers appeared during this session**: `libpipewire-0.3-dev` amd64 AND i386 were installed at
  22:11:40 (dpkg), 13 s after this worktree's first configure (which logged `Package 'libpipewire-0.3' not
  found`, HAVE_PIPEWIRE 0). main's build-native also has HAVE_PIPEWIRE 0 (configured before). A reconfigure
  with the pkg-config cache entries dropped should build the PipeWire backend.
- Frontend (`ufrontend`, branch `uc2`): `targets/uc2` has no audio settings; `file-manifest.json` is
  generated from `assets/uc2` (no Sounds/Music/Movies: they live in `assets/uc2-cooked`). The build symlinks
  the manifest's top-level dirs from ONE asset root. `ufront-e2e.mjs` insists on `serve-web/` and the
  Playwright webServer serves `serve-web/` on :8080 with `reuseExistingServer: true`;
  `playwright.config.js` `testMatch` only lists `<target>-wasm-{boot,net,benchmark,proxynet}.spec.js`.

Green-movie theory (O-1c), first reading: our player starts with a zeroed BGRA (transparent black) and
converts only decoded pictures, so "green" does not come from an undecoded buffer in the normal path. Paths to
check: (1) a movie that cannot be opened (missing cooked file: always the case on the web, whose manifest has
no movies) -> `bHasError` -> the GL device's `SetMovieMaterial` returns 0 -> `SetMaterial` falls back to
`Material.DefaultMaterial` = `Engine.DefaultTexture` (the Xbox did the same: its "calcium texture"); (2) an
opened movie with no picture yet and `bNeedsFrameUpdate` 0 -> NULL without fallback -> DefaultMaterial too;
(3) a Theora stream whose first packet is a duplicate (0-byte) frame: libtheora then hands out reference
buffers that were never written (zero planes = green in BT.601). To see: what DefaultTexture looks like.

Next: native build (running), PipeWire reconfigure, backend logging, missing-data messages.

## Step 1 - O-1 native: backend order and report, missing-data messages, no more green movie textures (2026-10-06)

### (a) Backends
- `[UC2ALAudio.UC2ALAudioSubsystem] Backends=` (new config property; compiled default on Linux
  `pipewire,pulse,alsa,oss` = openal-soft's own order, explicit). Init sets `ALSOFT_DRIVERS` to it unless the
  environment already has one (bin/capped's `null`, the wave writer's `wave`); on the web nothing is set.
- Init captures openal-soft's log while the device opens (`alsoft_set_log_callback`, found with
  `alcGetProcAddress(NULL, ...)`: it is NOT a dynamic export of libopenal.so.1, `nm -D` shows only
  `alsoft_get_version`), then reports (`UC2ALBackendReport`, UC2ALDevice.cpp) and removes the callback.
  The callback only sees the first open of the process (backends initialise once).
- PipeWire: libpipewire-0.3-dev appeared mid-session (see step 0); reconfigure -> `Building OpenAL with support
  for the following backends: PipeWire, PulseAudio, ALSA, OSS, WaveFile, Null`, HAVE_PIPEWIRE 1. At run time the
  32-bit backend fails: `/usr/lib/i386-linux-gnu/pipewire-0.3/` does not exist (`libpipewire-0.3-modules:i386`
  not installed; amd64 has them) -> `Failed to create PipeWire event context (errno: 2)` -> pulse. BLOCKERS.md
  has the optional `sudo apt install libpipewire-0.3-modules:i386`.
- Runs (run tree `run/m11audio`, `capped 2G 30 -- xvfb-run ... ./uc2.bin -log -nostartupmovies`), literal lines:
  ```
  null (capped default)  UC2ALAudio: OpenAL Soft backends: order null (ALSOFT_DRIVERS): null initialised; playing through null
                         UC2ALAudio: 1.1 ALSOFT 1.25.2 on "No Output" (backend null) at 48000 Hz, 64 voices, reverb EFX (EAX reverb), loop points yes
  pulse + null sink      UC2ALAudio: OpenAL Soft backends: order pulse (ALSOFT_DRIVERS): pulse initialised; playing through pulse
  (PULSE_SINK=uc2test)   UC2ALAudio: 1.1 ALSOFT 1.25.2 on "uc2test Audio/Sink sink" (backend pulse) at 48000 Hz, 64 voices, reverb EFX (EAX reverb), loop points yes
  default order          UC2ALAudio: OpenAL Soft backends: order pipewire,pulse,alsa,oss: pipewire failed, pulse initialised; playing through pulse
  (ALSOFT_DRIVERS= empty,UC2ALAudio: openal-soft said: Failed to create PipeWire event context (errno: 2)
   PULSE_SINK=uc2test)   UC2ALAudio: 1.1 ALSOFT 1.25.2 on "uc2test Audio/Sink sink" (backend pulse) at 48000 Hz, ...
  pipewire only, named   UC2ALAudio: OpenAL Soft backends: order pipewire (ALSOFT_DRIVERS): pipewire failed; playing through none
  device                 UC2ALAudio: OpenAL could not open device "uc2-no-such-device": sound is off
                         UC2ALAudio: output devices of that backend: none
  ```
  (logs `logs/m11audio/o1-*.out`.) The default-order run was only made after checking that the i386 PipeWire
  modules are absent (`test ! -e .../libpipewire-module-protocol-native.so`), so it could not reach the owner's
  default sink; the null sink was loaded and unloaded around each run (`pactl load-module module-null-sink
  sink_name=uc2test` / `unload-module`), afterwards `pactl list short sinks | grep -c uc2test` = 0.
- On failure to open, the device now also lists the devices of the chosen backend (what `OutputDevice=` takes).

### (b) Run trees and messages
- `tools/dev/mkrun-uc2.sh` already links Sounds/, Music/, Movies/, Textures/Movies/ from assets/uc2-cooked and
  copies `libopenal.so.1` beside uc2.bin (printing a note on stderr when a cooked folder is missing);
  `bin/play-uc2` (not mine, not changed) calls it on main's build-native and sends only stdout to /dev/null, so
  those notes reach the owner's terminal. main's build-native has HAVE_PIPEWIRE 0 (configured before the dev
  package came); nothing else is missing for sound.
- New: the device says at Init (NAME_Init, so with -log on the terminal) and once on the console
  (`PlayerController.ClientMessage`, from `Update`, before the scene is drawn) when ../Sounds has no
  `A_Gameplay.uc2cues` ("Game sound is off ...") or ../Music has no .ogg ("Music is off ..."), when no OpenAL
  library could be loaded, and when no device could be opened (with the backend report). Movies: `UnMovie.cpp`
  says once (NAME_Init + console, from the full-screen player or the audio device's Update) when there is no
  cooked movie at all or no movie player, and once per missing movie name which case it is.
  Run without cooked data (`run/m11audio-nodata`, `logs/m11audio/o1-nodata.out`): `UC2ALAudio: Sound effects
  are off: no cooked sound data in ../Sounds (...)` (text since changed to "Game sound is off (effects, speech,
  music)"), `UC2Movie: Movies are off: there are no cooked movies in ../Movies or ../Textures/Movies (...)`,
  `UC2ALAudio: on the console: ...`. Whether the menu shows ClientMessage text on screen: not looked at (log only).

### (c) "Videos play green": found and fixed
- **Cause, seen**: a MovieMaterial whose movie cannot be opened made `SetMovieMaterial` return 0 and the GL
  device then drew `Material.DefaultMaterial` = `Engine.DefaultTexture`, which in UC2 is **bright green**.
  Run tree without Movies/ and Textures/Movies (`run/m11audio-nomovies`, `m9-movies.py --only menu-movie`):
  `logs/m11audio-nomovies/m9-menu-movie-0.png` shows the arena page's game-type portrait as a solid green
  square. The web build has no movie in its manifest, so every movie texture there was green. (The Xbox drew the
  same DefaultTexture for a movie it could not open, but every movie it referenced on purpose existed.)
  The all-zero-YUV theory (zero planes convert to green in BT.601) does not apply to our player: its BGRA buffer
  starts zeroed (transparent black) and only decoded pictures are converted; libtheora fills its reference
  frames with grey when a stream starts on an inter frame.
- **Fix** (`Engine/Src/UnMovie.cpp`, `appUpdateMovieMaterial`): while a material has no picture (its movie
  cannot be opened, nothing decoded yet, or nothing converted: revision 0) it shows its `LoadingFallback` when it
  has one, else a 4x4 opaque black picture (`FBlackMoviePicture`, revision -2), never NULL-without-fallback.
  One log line per material and reason. The GL device's code is unchanged (it uploads whatever picture it gets).
  After the fix, the same run: the portrait area shows the material's LoadingFallback
  `T_Portrait.Arena.UI_Arena_Null_Portrait` (the page background shows through), log `UC2Movie: MovieMaterial
  T_Portrait.GameType.Portrait_Gametype_CTF shows its LoadingFallback T_Portrait.Arena.UI_Arena_Null_Portrait:
  its movie cannot be opened (see the line above)`.
- Numeric check added to `UC2Movie.UC2MovieSelfTest` (3 checks: missing movie without fallback -> 16 texels all
  (0,0,0,255) and bHasError; with a fallback -> fallback; an existing movie -> its own frame 0, revision 1).
  `tools/dev/movie-selftest.sh m11audio` -> `UC2MovieSelfTest: 99 checks, 0 failed` (96 before + these 3).

Next: O-2, openal-soft into the web build.

## Step 2 - O-2 engine side: openal-soft in the web build (2026-10-06, in progress)

Changes:
- Root `CMakeLists.txt` (M8 block): under Emscripten, openal-soft from the same fork STATIC with
  `ALSOFT_BACKEND_WEB`, NEON/DLOPEN/WAVE/EAX/utils off, `ALSOFT_REQUIRE_SSE4_1`; its scope gets only the
  optimisation level as directory options, and `-pthread -sNO_DISABLE_EXCEPTION_CATCHING -msimd128 -msse4.1` in
  its language flags (its configure checks compile with those; `xmmintrin.h` needs -msimd128). Configure:
  `Building OpenAL with support for the following backends: WebAudio, Null`, CPU extensions `Default, SSE, SSE2,
  SSE3, SSE4.1`. `UC2ALAudio` is no longer native-only.
- `Source/UC2ALAudio/CMakeLists.txt`: links `OpenAL` on the web; `UC2ALBind` fills the table with the linked
  symbols (`UC2AL_WITH_PROTOTYPES` in UC2ALDevice.cpp).
- `Source/SDLLaunch/cmake/Web.cmake`: one delimited block, `-sPTHREAD_POOL_SIZE=4` (the backend's mixer and the
  context's event thread on top of main() and Core's streaming thread).
- `Source/UCC/CMakeLists.txt` (one hunk): `ucc-bin.js` exports `_malloc`/`_free`: first wasm link failed with
  `error: undefined symbol: malloc (referenced by $stringToNewUTF8, ...)` - the fork's alconfig.cpp reads
  `Module.alsoftConfig` through stringToNewUTF8; SDLLaunch exports malloc already.
- `Engine/Config/web/Default.ini` (CRLF kept): `AudioDevice=UC2ALAudio.UC2ALAudioSubsystem`, sections
  `[UC2ALAudio.UC2ALAudioSubsystem]` and `[UC2Movie]`; README rows.
- Logging for the web checks (NAME_Log/NAME_Init, one line per event, also native): `UC2ALAudio: N of 23
  persistent cue tables read in X ms`, `UC2Audio: [t] read wave pack <file> (<bytes>) in X ms` (once per
  pack), `UC2Audio: [t] music <song>: playing ... file read in X ms` (was DevSound only), `UC2Audio: [t] first
  sound: <cue>`; `UC2Movie: decode cost of <movie>: N updates, M new pictures, X ms per update on average, Y ms
  at most` after every full-screen movie.
- `-NULLMOVIES` (UnMovie.cpp, tests): full-screen movies play on the null render device too (file access,
  decoding, sound and clock run; nothing drawn) - how the web build's movie path is tested before its GL device.
- O-4 tool: `ucc-bin UC2ALAudio.UC2ZoneSurvey [MAP=<name>]` (UC2ZoneSurvey.cpp) lists every map's zones and
  their ZoneAudioEffect.
- Xbox reverb reading (O-4): `XACTAudioSubsystem.cpp` Init loads the DSP effects image `..\xbox.bin` with the
  I3DL2 reverb in it (`dsImageLoc.dwI3DL2ReverbIndex = GraphI3DL2_I3DL2Reverb`, :180-185) and never sets an
  I3DL2 listener (NotifyZoneChange's `SetI3dl2Listener` is `#if 0`; the only other call is the "FILTER" debug
  exec, :1069, Underwater). So on the Xbox every sound with an I3DL2 send went through one reverb with the
  DSP image's initial listener parameters, in every map and zone.

Frontend side (ufrontend, branch uc2, not committed yet): `packages/e2e/uc2-wasm-audio.spec.js` (2 tests),
`playwright.config.js` testMatch + `<target>-wasm-audio.spec.js` (opt-in), `targets/uc2/target.json`
(`webAudio` recommended, `audioBuffer` 10 ms x 3, cook by-products in `manifestExclude`).
Local asset tree for the browser: `run/m11audio-assets` = hard links of assets/uc2/* plus uc2-cooked's
Sounds, Music, Movies (minus .cook, cook-report, movie-manifest) and Textures/Movies (hard links, because the
manifest generator does not follow symlinked directories: `readdirSync(recursive, withFileTypes)` +
`isFile()`). Manifest: `node packages/glue/bin/generate-manifest.cjs run/m11audio-assets <scratch>/uc2-audio-
file-manifest.json --exclude-from targets/uc2/target.json` -> 1223 files, 1899.1 MB (Sounds 421, Music 23,
Movies 99, Textures 149 of which 87 movies); a superset of the committed 593-entry manifest.

## Step 3 - O-2 in the browser: first green spec runs (2026-10-06)

Build: `source ../env.sh; bin/capped 6G 5400 -- cmake --preset wasm-perf && cmake --build build-wasm-perf -j6`
-> rc 0 after two fixes (ucc-bin.js malloc export, see step 2; `RF_ClassDefaultObject` does not exist in UE2).
`SDLLaunch.wasm` 12,035,640 bytes, `ucc-bin.wasm` 11,739,858. Assembled with the frontend's own build into
`run/m11audio-web` (`yarn build --target uc2 --engine-artifact .../build-wasm-perf/Source/SDLLaunch --out
run/m11audio-web --assets run/m11audio-assets --manifest <scratch>/uc2-audio-file-manifest.json`, capped 4G 300,
rc 0; serve-web/ untouched: its marker kept the renderer agent's 22:40 build).

How the spec is pointed at that directory: `ufront-e2e.mjs` checks `serve-web/`'s marker (uc2: yes) and the
Playwright webServer serves `serve-web/` on :8080 with `reuseExistingServer: true`, so the run starts the
frontend's own server first, in the same capped scope, and Playwright reuses it:
```
m=$(pactl load-module module-null-sink sink_name=uc2test)
PULSE_SINK=uc2test bin/capped 6G 600 -- bash -c 'node packages/build/bin/ufront-serve.mjs --port 8080 --root <WorkUC2>/run/m11audio-web & sleep 2;
    node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=12 -- --target uc2 uc2-wasm-audio.spec.js'
pactl unload-module $m
```
This is only correct when no other e2e run and nothing on :8080 exist: check `pgrep -af e2e-mem-guard` and
`ss -ltn | grep :8080` and do NOT start otherwise (see the incident below).

Run 1 (`logs/m11audio/e2e-1.out`): **2 passed (54.1 s)**, plus a teardown error "1 browser profile(s) outlived
their test: profile-KPs5hJ" (lib/global-teardown.js; not explained yet). Literal record:
```
menu up after 8.3 s; music line after 8.3 s; [CTX] before any page input: running
UC2ALAudio: OpenAL Soft backends: order web (the only backend of the web build): web initialised, null initialised; playing through web
UC2ALAudio: 1.1 ALSOFT 1.25.2 on "Web Audio" (backend web) at 48000 Hz, 64 voices, reverb EFX (EAX reverb), loop points yes
UC2ALAudio: 23 of 23 persistent cue tables read in 489 ms
UC2Audio: [   1.34] music NuBattle: playing ../Sounds/../Music/NuBattle.ogg (44100 Hz, 2 ch, looping, volume -1500 mB, fade in 3.0 s; file read in 48 ms)
UC2Audio: [   1.39] read wave pack A_Gameplay.ogp (532633 bytes) in 22 ms
UC2Audio: [   1.35] first sound: A_Gameplay.MenuPageOpened
[AUDIO-SKIPS] menu music: underruns=0 ... audible=0.979 ctx=running clock=0.999 lib=openal-soft-web | 2ch@48000 period=480 ring=1536/1440 target=1504 recentBurst=1024 mixerWorst=4.02ms mixerWorstGap=22.689ms burst=2048 quanta=4968
[FETCH] Sounds: 27 files, 1 MiB; Music: 1 file (NuBattle.ogg); Movies: 0
[MEM] browser PSS peak 1397 MiB, cgroup memory.peak 2952 MiB, wasm memory 256 MiB, JS heap 25 MiB
-- test 2 (-NULLMOVIES) --
UC2Movie: opened ../Movies/UC2_Logos.ogv for ..\Movies\UC2_Logos.xmv: Theora 640x480, 600 frames at 30 fps, Vorbis 44100 Hz 2 ch from ../Movies/UC2_Logos.ogv (880500 frames, padded to 882000); language int: international track 0
UC2Audio: [   1.64] movie sound stream opened (44100 Hz, 2 ch, exclusive: other sounds paused)
UC2Movie: ..\Movies\UC2_Logos.xmv ended at movie time 20.000 s, frame 599 of 600, 2170 pictures presented in 20.035 s
UC2Movie: decode cost of ..\Movies\UC2_Logos.xmv: 2171 updates, 592 new pictures, 1.23 ms per update on average, 36.06 ms at most
UC2Audio: movie sound stream closed after 20.000 s played
[AUDIO-SKIPS] logos: underruns=0 audible=1 ctx=running clock=1.001 ... mixerWorst=3.189ms quanta=8164
[FETCH] Movies: UC2_Logos.ogv 200 3 MiB; [MEM] browser PSS peak 1686 MiB, cgroup memory.peak 2990 MiB, wasm 256 MiB
```
Reading: the context was already `running` when the device opened, without any page input after the Start click
(Chromium's autoplay rule for Web Audio uses the page's sticky activation: the Start click counts); the backend's
own resume-on-gesture listeners are the fallback. Time to first sound: 1.35 s after the device opened (the
menu's page sound), the music at 1.34 s; the device opens during the engine's Init, about 7 s after Start.
Music file (2.6 MB) read in 40-48 ms on localhost; the whole wave pack A_Gameplay.ogp in 22 ms. The logos ran
in real time on the web (20.035 s wall for 20.000 s of movie, the sound stream played exactly 20.000 s); the
decode costs 1.23 ms per update on average, 36 ms at most (the first update includes the whole-file fetch).

**Speaker safety, run 1**: the spec's `test.use({launchOptions})` with `--mute-audio` had NO effect: the suite's
`lib/fixtures.js` launches its own persistent context and ignores launchOptions, and the config runs Chromium
"headed" with `--headless=new` in its args, so Playwright's usual `--mute-audio` is not added either. What kept
the sound off the owner's speakers in run 1 is only `PULSE_SINK=uc2test` (a null sink, loaded before and
unloaded after; libpulse honours PULSE_SINK for a stream opened on the default device). The spec now makes its
own context with lib/persistent-context's `launchPersistent({ extraArgs: ['--mute-audio'] })`.

**Incident, run 2 attempt**: started while the renderer agent's e2e (`uc2-wasm-boot.spec.js`, its own capped
6G scope) was running and its server held :8080 - my preflight printed both facts but the command did not stop
on them. My server could not bind, my run would have reused THEIR server; my mem-guard killed my run at
12.4 GB machine in-use ("over the 12 GB budget", exit 3) before any test result; their run carried on (its log
shows the menu up and its GL milestones afterwards). Two browsers ran at once for a short time, against the rule.
From now on the run command itself refuses to start unless `pgrep -f e2e-mem-guard` finds nothing and :8080 is
free.

Frontend commits (ufrontend, branch uc2, local): `a51b743` test(e2e) the spec + testMatch (opt-in),
`591384d` feat(targets) target.json (webAudio recommended, audioBuffer, cook by-products excluded) + README.
Gates: `yarn test` 28/28, `yarn gates` "all CI-tier gates green", eslint + prettier clean on my files.

## Step 4 - O-4 survey (2026-10-06)

`cd run/m11audio/System && bin/capped 2G 1800 -- ./ucc-bin UC2ALAudio.UC2ZoneSurvey` (`logs/m11audio/
zones-all.out`): **58 maps, 34 with a zone that is not Default; DM-Azure: 18 zones, all Default** (also OD-Azure).
Cross-check with M8's in-game line for DM-Praxis: `Default x2 Auditorium x2 Hangar x1 Hallway x9 Underwater x1`,
the same. Totals over all maps: Default x350 Generic x2 Room x4 Bathroom x5 LivingRoom x7 StoneRoom x14
Auditorium x22 ConcertHall x3 Cave x11 Arena x6 Hangar x16 Hallway x119 StoneCorridor x15 Forest x1 City x17
Mountains x4 Quarry x10 Plain x2 Underwater x2 NoReverb x5. Maps with Default only: OD-Azure, DM-Legacy,
DM-Azure, DM-ColdFusion, DM-AcidRain, CTF-Gauntlet][, OD-Falls, Entry, Menu, DM-Nexus, DM-Undertow,
CTF-TotalControl, ACT-SelketCheats, DM-Evilhands, CTF-ViperPit, ACT-BarrensAmbush, DM-Deadbolt, DM-Meridian,
ACT-HallwayAmbush, DM-Eternal, OD-Zenith, CTF-Emperor, DM-Phoenix, DM-DeepCore.

What the Xbox does instead (the owner hears reverb in DM-Azure): **one I3DL2 reverb with the Default preset, in
every map and zone.** Evidence: the game's `xbox.bin` (ISO and epic-sources, 18,608 bytes, SHA-1 df4bc8c9...) is
byte-identical to the XDK 5849 standard effects image `source/dsound/dsp/dsstdfx.bin`; its `dsstdfx.ini` puts
`I3DL2Reverb` in `GraphI3DL2` with initial state `reverb_Default_24k_state.ini`; XACTAudioSubsystem.cpp loads
that image with `dwI3DL2ReverbIndex = GraphI3DL2_I3DL2Reverb` and never sets a listener (zone switch `#if 0`).
Every 3D sound reached it through its own I3DL2 send level from the XACT project: 1,694 sounds at 0 mB (full
send), 733 at -300 mB, 1 at -333 mB; DM-Azure's own bank A_Azure: 13 sounds, all 0 mB. So in DM-Azure our device
with ZoneReverb=True does what the Xbox did (Default preset everywhere, the same sends); in the 34 maps with other
zone presets it differs from the Xbox by the owner's decision (zone reverb kept on).

## Step 5 - native gate runs and the DM-Azure capture pair (2026-10-06)

`tools/dev/audio-regression.sh m11audio` (run tree from this branch's build-native, `logs/m11audio/audio-summary.txt`):
```
SUMMARY audio-check: 182 index files; waves: 7141 decoded to their exact length of 7141 cooked (19 entries have no cooked wave); music streams: 21 of 21; cues [int]: 2438 in 78 cue tables: 2417 resolve fully, 6 partly, 15 to no decodable wave (0 use a localised bank); UC2AudioCheck: 78 cue tables, 2438 cues, 0 failures
SUMMARY audio-selftest: UC2AudioSelfTest: 39 checks, 0 failed; Pan3D -1.10 dB, Pan2D -1.52 dB
SUMMARY audio-menu-music: music NuBattle.wav in audio-menu.wav: envelope correlation 0.857 at 0.30 s, mix gain -19.02 dB (expected -19.00 dB, error -0.02 dB), control track Recoil.wav correlates 0.115: PASS
SUMMARY audio-menu: 58.645333 s, peak -19.0 dBFS, RMS -34.5 dBFS, silent seconds 0; cue requests 1: played 1 (1 distinct), culled 0 (0 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-deadbolt: 83.605333 s, peak 3.1 dBFS, RMS -23.6 dBFS, silent seconds 0; cue requests 1103: played 490 (55 distinct), culled 613 (75 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-praxis-efx: 83.541333 s, peak -2.9 dBFS, RMS -24.4 dBFS, silent seconds 0; cue requests 1063: played 260 (39 distinct), culled 803 (81 distinct), failed 0 (0 distinct); Critical 0
SUMMARY audio-praxis-dry: 83.626667 s, peak -2.1 dBFS, RMS -25.1 dBFS, silent seconds 0; cue requests 1235: played 292 (44 distinct), culled 943 (88 distinct), failed 0 (0 distinct); Critical 0
```
(Deadbolt peaks at +3.1 dBFS: the float wave writer does not clip; same remark as M8.)

DM-Azure pair (O-4), same input script as the suite's Praxis pair (`UC2_PRECMD="set UCDeathmatch NumPlayers 0"`,
`UC2_POSTCMD="Ghost"` so the walk crosses zones; `tools/dev/run-uc2-audio.sh m11audio azure-zone-efx|azure-zone-dry
85 28 'DM-Azure?NumBots=3' [-NOEFX]`; without Ghost: `azure-efx`/`azure-dry`, no zone change logged):
```
SUMMARY azure-zone-efx: 83.413333 s, peak 3.1 dBFS, RMS -28.0 dBFS, silent seconds 0; cue requests 901: played 263 (48 distinct), culled 638 (61 distinct), failed 0 (0 distinct); Critical 0
SUMMARY azure-zone-dry: 83.562667 s, peak -3.2 dBFS, RMS -29.9 dBFS, silent seconds 0; cue requests 1009: played 236 (36 distinct), culled 773 (70 distinct), failed 0 (0 distinct); Critical 0
efx run:  UC2ALAudio: ... (backend wave) ..., reverb EFX (EAX reverb), ...   UC2Audio: 33 static ambient sounds; zone reverb presets in this level: Default x18
          UC2Audio: [  35.89] zone ZoneInfo15 (ZoneAudioEffect 0): reverb preset Default unchanged   (7 such lines, ZoneInfo15/9/LevelInfo0)
dry run:  UC2ALAudio: EFX switched off (UseEFX=False or -NOEFX): zone reverb is off, sound is dry
          UC2Audio: [  41.29] zone LevelInfo0 (ZoneAudioEffect 0): no EFX, sound stays dry
```
RMS with EFX minus without, by window (from the captures' per-second RMS): azure-zone 5-24 s (before the spawn:
music + 3D ambients) +0.4 dB, 30-55 s +3.0 dB, 55-80 s -0.0 dB; azure (no Ghost) +0.3 / -0.7 / +0.9 dB; the
suite's Praxis pair +0.7 / +0.7 / +0.4 dB. Log-correlated only: gameplay is not identical between two runs. The
pre-spawn window is the comparable one: a fraction of a dB, as expected of the Default preset (Room -1000 mB,
late reverb +200 mB, 1.49 s), whose decay the self-test measures numerically (`PASS reverb Default 3D, send
-300 mB: ... decay time measured 1.68 s, preset 1.49 s (+13%)`). Files: `logs/m11audio/azure-zone-{efx,dry}.
{wav,png,report.txt,cues.txt,out}`.
What it means for the owner: DM-Azure has no reverb zone; its reverb on the Xbox is the one fixed Default
reverb every map had, fed by each sound's I3DL2 send (A_Azure's 13 ambient sounds send at 0 mB, i.e. fully). Our
device plays the same preset there with the same sends. Whether its LEVEL equals the Xbox DSP's is not known
(EFX vs the Xbox's reverb24k DSP program; M8's doubt), and nothing here can measure it without an Xbox capture.

Movie self-test (`tools/dev/movie-selftest.sh m11audio`): `UC2MovieSelfTest: 99 checks, 0 failed`.

## Step 6 - final gate runs (2026-10-06)

Builds (this branch's tip): `bin/capped 6G 1800 -- cmake --build build-native -j6` rc 0
(`logs/m11audio/native-build-6.log`), `bin/capped 6G 5400 -- cmake --build build-wasm-perf -j6` rc 0
(`wasm-perf-build-4.log`; SDLLaunch.wasm 12,036,128 bytes). `./ucc-bin verifyclasses` -> `All native class
layouts match`.

wasm32 twin under Node (`tools/dev/mkrun-uc2.sh build-native m11audio build-wasm-perf`, then in
`run/m11audio/System`, `bin/capped 3G ... node ucc-bin.js ...`): the web build's own openal-soft (static, SIMD
through Emscripten's SSE headers) and decoders:
```
UC2AudioCheck: 78 cue tables, 2438 cues, 0 failures      (waves: 7141 decoded to their exact length; music streams: 21 of 21)
UC2AudioSelfTest: 39 checks, 0 failed; Pan3D -1.10 dB, Pan2D -1.52 dB   (loopback device; reverb section all PASS: Default decay 1.68 s for 1.49 s, Hangar 10.07 for 10.05)
UC2MovieSelfTest: 99 checks, 0 failed
```
So EFX reverb (zone reverb) works in the web build's openal-soft, measured by the same numbers as natively;
in the browser the device line says `reverb EFX (EAX reverb)`.

Native: `tools/dev/movie-selftest.sh m11audio` 99/0; `python3 tools/dev/m9-movies.py --tree m11audio-m9` ->
`m9-movies: 19 checks, 0 failed` (all PASS, `logs/m11audio-m9/m9-summary.txt`); audio suite as in step 5;
PulseAudio null sink run with the final binary (`logs/m11audio/gate-pulse-nullsink.out`):
`UC2ALAudio: OpenAL Soft backends: order pipewire,pulse,alsa,oss: pipewire failed, pulse initialised; playing
through pulse` / `UC2ALAudio: 1.1 ALSOFT 1.25.2 on "uc2test Audio/Sink sink" (backend pulse) at 48000 Hz, ...`.

Browser spec, with the run command refusing to start unless no node process runs e2e-mem-guard and :8080 is
free (`pgrep -a node | grep e2e-mem-guard`; NOT `pgrep -f`, which matched the command's own shell):
- run 2 attempt 2 (`e2e-3.out`): stopped by my mem-guard at 12.1 GB machine in-use (10.6 GB at start: other
  agents' builds and GL runs); no test result. Waited (20 min loop on in-use < 8.5 GB never reached it: the
  desktop and the other agents keep about 9 GB), retried at 9.1 GB.
- **pass A** (`e2e-4.out`): 3 passed (1.5 m), profiles certified clean, guard peak 11.1 GB.
- **pass B** (`e2e-5.out`): 3 passed (1.6 m), profiles certified clean, guard peak 11.4 GB.
Measurements (pass A / pass B; run 1 of step 3 for the first two tests: menu up 8.3 s on a quieter machine):
| | A | B |
|---|---|---|
| menu up / music line, after Start | 15.3 / 15.5 s | 15.9 / 16.4 s |
| 23 persistent cue tables read | 1,345 ms | 1,584 ms |
| first sound / music, after the device opened | 3.13 / 3.12 s | 3.58 / 3.57 s |
| music file read (2.6 MB) / A_Gameplay.ogp read (0.5 MB) | 98 / 102 ms | - / - (same order) |
| music refill (32768 frames) on the game thread | 5.76 ms avg, 16.03 max | 6.78 avg, 16.23 max |
| menu, 12 s: audible share / underruns / clock ratio | 1.0 / 1 (3 ms) / 0.997 | 1.0 / 1 (1 ms) / 0.958 |
| logos: wall time for 20.000 s / sound played | 20.218 s / 20.000 s | 20.413 s / 20.000 s |
| logos decode cost per update avg / max | 1.93 / 53.48 ms | 2.20 / 118.47 ms |
| logos: audible / underruns | 0.984 / 0 | 0.984 / 1 (1 ms) |
| skip: key at about 3 s -> | skipped at 3.220 s, music after | skipped at 3.000 s, music after |
| files fetched for sound (menu) | 27 Sounds (1 MiB) + NuBattle.ogg | same |
| browser PSS peak / cgroup memory.peak / wasm memory | 1,314-1,364 MiB / 2.6-2.7 GiB / 256 MiB | 1,358-1,369 MiB / 2,748 MiB / 256 MiB |
The context was `running` without any page input after Start in every run. The single 1-3 ms underruns came with
a 67 ms long task on the page (pass A) or a 45-100 ms mixer gap (`mixerWorstGap`), i.e. a busy machine; the
backend's adaptive ring grew to 2,528 frames afterwards.

What remains for the GL device's merge (not seen here: this branch has the null renderer on the web): the
movie PICTURES on the web (logos, cinematics full screen; MovieMaterial textures uploaded by UC2GLMovie.cpp as
GL_BGRA through GL4ES to WebGL 1, which has no BGRA format: GL4ES must convert, unverified), the black/fallback
texture for missing movies on the web, frame pacing of the full-screen loop with a real present, and the decode
cost with rendering competing for the same worker. Cinematics are fetched whole on first touch (01_AscPre1.ogv
29.7 MB + its .oga): a stall at the ladder's travel of the fetch time; a prefetch belongs to the precache work.
