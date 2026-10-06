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
