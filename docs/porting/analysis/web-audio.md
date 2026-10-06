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
