# Video (M9): cooked Theora/Vorbis movies and a portable movie player

Running notebook for milestone M9 (PLAN.md row M9). Branch `m9-video`, worktree `../engine-uc2-m9`. Appended
after every step so a successor can continue. Runtime XMV decoding is M16; web playback is M11.

## Step 0 - reading (2026-10-04)

### The source movies (`epic-sources`, read-only), measured with ffprobe/ffmpeg

- `Movies/*.xmv` (23, 841 MB): all WMV2 640x480 yuv420p.
  - 19 ladder cinematics (`01_AscPre1` .. `19_AscFin6`): 5 audio streams each, Xbox IMA ADPCM, 44.1 kHz
    stereo (1.76 Mbit/s of audio per file). The decoded audio of every track has the same sample count, equal
    to the video's last timestamp (e.g. `01_AscPre1`: 7,094,144 samples = 160.865 s; last video pts 160.865 s).
  - `UC2_Logos`, `21_UC2_Credits`, `UC2_Attract`, `A51` (attract-mode trailer, 487 s, 182 MB): one PCM s16le
    44.1 kHz stereo track.
- `Textures/Movies/*.xmv` (87, 164 MB): video only. 65 at 256x256, 16 at 256x512 (`Portrait_Char_*`), and
  `CIN_TV_Screens_Mov` 512x512, `UI_Start_Movie01` 512x256, `Crab_Display02` 128x64, `Reactor_01` 128x128
  (15 fps), `TVDisplaySelket2` 128x96 (15 fps, **not a power of two**), `UI_LoadingMovie` 640x480.
- Frame timing: XMV packets carry millisecond timestamps (33/34 ms steps at 30 fps). Frames are sometimes
  **dropped by the encoder** (the previous picture is held): `UC2_Logos` has 592 video packets over 20.0 s
  (gaps of 66 ms x2 and 233 ms x1), `01_AscPre1` 4,805 packets (gaps 66, 100, 668 ms), `07_Asc02` a 1,035 ms
  gap. ffprobe's `r_frame_rate` guesses 30000/1001 for `07_Asc02` and 1000/1 for `A51`; both are 30 fps in
  their timestamps. So "frames in" (packets) is not "frames shown"; the cook makes a constant 30 (or 15) fps
  stream that shows at every 1/30 s slot the frame the XMV shows there (see Step 1).
- Content references (strings in the shipped packages): `T_Portrait.utx` (77 `Portrait_*` MovieMaterials),
  `T_UI_2.utx` (20 `D:\Movies\NN_*.xmv`: the "View movies" menu plays the cinematics and credits as
  MovieMaterials with `bEnableAudio=true`, `bPlayLooping=true`, per `UCMenu.u`), `T_Cin_TVs.utx`,
  `T_LI_Meshskins.utx`, `T_LI_Drill.utx`, `CIN_Stadium_Tex.utx`, `T_NK_TechSkins.utx`, `RL_Hud_Movies.utx`,
  `T_Crosshair.utx` (35 `CH_*.xmv`). Several referenced names do not exist in the shipped tree (all `CH_*`,
  `RL_XB_*`, `NKTech_Signs`, `Anubis_vs_Selket`, `TVDisplayTest`, `AscPre06_Satellite_View_f90`,
  `AscPre3_Selket_TV_Recut`): on the Xbox those materials failed to open (`bHasError`, fallback material).
- Ladder cinematics: `System/Ladders/Ascension.ini` `IntroCinematic=`/`OutroCinematic=` per rung (14 intros,
  5 outros); `UCGameplay.u` turns them into `?StartCinematic=`/`?EndCinematic=` URL options.
- `TestMaps/`: AVI/MOV/WAV production sources (sfx/voice stems) of ~10 cinematics. Not used for the cook yet;
  see Step 1 for the decision.

### The Xbox player (the specification), file:line in `Source/XboxDrv`

- `Inc/XMVHelper.h`, `Src/XMVHelper.cpp`: `CXMVPlayer : FMoviePlayer` wraps the XDK's `XMVDecoder`.
  `OpenFile(name, format, device, bAllocateTextures, bEnableAudio, bShouldLoop, bManualSync)` (:273):
  `XMVFLAG_SYNC_ON_NEXT_VBLANK` unless manual sync, `XMVFLAG_FULL_LOOP` when looping. Two
  `LIN_A8R8G8B8` textures (decode one, show the other, :235-256). `AdvanceFrameForTexturing` (:524): asks the
  decoder for the next frame; NEWFRAME swaps; ENDOFFILE: looping -> `Reset()` and report NEWFRAME (so the
  last frame is followed directly by frame 0), else `m_bPlaying=FALSE`; returns NULL until the first frame.
- **Audio track by language** `GetLocalizedAudioStream` (:160-189): XGetLanguage German -> 1, French -> 2,
  Italian -> 3, Spanish -> 4, anything else -> 0; if the movie has fewer streams, fall back to 0 with
  "XMV doesn't support selected localized audio stream, falling back to English" (:211-220). The chosen
  stream is the sync stream (`SetSynchronizationStream`, :232): **video follows the audio clock**.
- `Src/XboxMovieThread.cpp`: `XboxMovieThread` renders a movie full screen on its own thread
  (`Run`/`Render`, :760-831): clear, draw the frame on a 640x480 quad, the "LOADING..." text for the loading
  movie (`RenderMapName`, bottom right), and **when subtitles are off (`GShowSubtitles == FALSE`) and it is not
  the credits, black boxes over rows 0-96 and 384-480 hide the burnt-in subtitles** (`HideSubtitles`,
  :134-145, :824-827). Update period 1 ms with audio, 28 ms without (:680-683).
  Skip: once the engine says it is ready (`EngineIsReady`), any pad button (`WasInterruptRequested`, any
  analog button byte non-zero) ends an interruptible movie; a looping movie ends as soon as the engine is
  ready (:769-789). `CreateMovie(name, bWaitForComplete, bLoop, msg)` (:312) blocks when asked
  (`EngineIsReady(1)` after 0.5 s: wait for the thread).
- Names: `GetMovieInfo` (:358) builds `D:\Movies\<option>` and appends `.xmv` when there is no dot.
- `PlayOutroCinematic`/`PlayIntroCinematic` (:389-415): blocking, skippable. `PlayLoadingMovie` (:486):
  `UI_LoadingMovie.xmv` from memory if preloaded, looping, message "LOADING..." (`BuildLoadingMessage`, :426).
- Start-up (`FInitialMovieThread`, :1031-1134, called from `XboxLaunch/Src/Launch.cpp:380-436`
  `ShowSplashScreens`): legal screen 5 s, rating screen 4.9 s (both from `Startup/UC2Startup.xpr`), then
  `Movies/UC2_Logos.xmv` from memory (skippable by any button once the file cache is ready), then the loading
  movie until the menu is up.
- Engine side (`Engine/Src/UnGame.cpp`, all `#if defined(_XBOX) && !defined(_DEBUG)`): `LoadMap` start
  (:1787-1813): unless loading Entry or a movie is already playing, play the outro (`?EndCinematic=`, only
  when the previous map was not the menu) blocking, then start the loading movie (non-blocking);
  `LoadMap` end (:2914-2924): with `?StartCinematic=`, stop the loading movie and play the intro blocking;
  first `Draw` after loading (:3099-3107) stops the loading movie (`SetEngineReady`); `PaintProgress`
  (:53-60) draws nothing while a movie plays.
- MovieMaterial: `Engine/Classes/MovieMaterial.uc` (`MovieFileName`, `bPlayLooping`, `bEnableAudio`,
  `bDecodeFromMemory`, `bAdditive`, `bManualFramerateSynch`, `bUpdateWhenPaused`, `Mask`, `LoadingFallback`,
  transient `MovieDecoder`, `RenderInterface`, `LastD3DTexture`, `bNeedsFrameUpdate`, `bHasError`, `bReset`;
  delegate `OnDonePlaying`); `PreSetMaterial` sets `bNeedsFrameUpdate` once per level time step.
  `Engine/Src/UnMaterial.cpp:1096-1140` `FMovieTexture` (RGBA8, clamp, revision 0, "manually fixed up" by the
  driver), `Dispose` deletes the decoder as `FMoviePlayer*` (:1191). `D3DDrv/Src/D3DMaterialState.cpp:4539`
  `SetMovieMaterial` (`#ifdef _XBOX`): open on first use (from memory if `bDecodeFromMemory`, after a
  background load, showing `LoadingFallback` meanwhile), advance only when `bNeedsFrameUpdate`, fire
  `OnDonePlaying` at the end, fall back to `LoadingFallback`/default material until the first frame, set the
  movie texture on stage 0 (SELECTARG1 colour and alpha), blending SRCALPHA/INVSRCALPHA (or ONE/INVSRCCOLOR
  when `bAdditive`), Z write and test on, optional `Mask` texture's alpha on stage 1. PC builds return 0
  (default material). The GL driver (`UC2GLDrv/Src/UC2GLMaterialState.cpp:4575-4842`) carries the same two
  branches.
- `FMoviePlayer`/`FMovieThread` (`Core/Inc/UnVcWin32.h:792-806`): only a virtual destructor / `Exit()`; a copy
  is in `Core/Inc/UnGnuG.h:465-478` for our builds.
- Language on our builds: `UObject::GetLanguage()` (`int`, `deu`, `fra`, `ita`, `spa`: `GLanguageExt`,
  `Core/Src/UnObj.cpp:51`) from `[Engine.Engine] Language`; `appGetLanguageID()` returns `LC_INT` off Xbox.
