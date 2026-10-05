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

## Step 1 - V1: `tools/cook-movies.py` (2026-10-04)

### Cooked format (version 1; a format we own)

| File | Content |
|---|---|
| `Movies/<Name>.ogv` | Ogg: Theora video (constant 30 or 15 fps, the XMV's size, 4:2:0, BT.601 limited range as decoded) + the **international track** (XMV audio stream 0, English) as Vorbis, when the movie has sound |
| `Movies/<Name>_<lang>.oga` | Ogg Vorbis only: XMV audio stream 1 `deu`, 2 `fra`, 3 `ita`, 4 `spa`. Written only when the track's decoded PCM differs from stream 0 (in the shipped data all 4 differ in all 19 cinematics: 76 files) |
| `Textures/Movies/<Name>.ogv` | Ogg Theora only (MovieMaterial clips; the XMVs have no sound) |
| `Movies/movie-manifest.json` | per movie: source (path, bytes, SHA-1), video (size, fps, XMV packets, CFR slots, held frames, pow2), every audio track (codec, rate, samples in, PCM SHA-1, language, identical-to-int), every output (bytes, SHA-1, frames by packet count and by granule, samples by granule and by ffmpeg decode), PSNR/SSIM, checks, every ffmpeg warning |
| `Movies/cook-report.txt` | the same as a table, plus the warnings grouped |
| `Movies/.cook/<Name>.json` | per-file state: a re-cook skips a file whose source SHA-1 and settings are unchanged |

**Language rule at run time** (the player, `Engine/Src/UnMovie.cpp`): language `L` = `UObject::GetLanguage()`;
if `L` is not `int` and `<Name>_<L>.oga` exists, the sound comes from it, otherwise from the `.ogv`'s own track
(the Xbox's "falling back to English", XMVHelper.cpp:215-219). A web client therefore fetches the `.ogv`
(video + English sound) and at most one `.oga` (about 1 MB per minute) - never five languages.

**Frame timing**: the XMV keeps millisecond timestamps and the WMV2 encoder dropped frames (held pictures). The
cook makes a constant-rate stream: `ffmpeg -vf fps=<30|15>` puts at every 1/fps slot the XMV picture shown
there; libtheora writes a repeated picture as a **zero-byte packet** (a Theora "dropped frame"). So frames out =
packets = slots = round(last XMV pts x fps) + 1, and the Theora granule position of the last page says the same.
ffmpeg's decoder skips zero-byte packets (UC2_Logos: 600 packets, 593 decoded pictures): **count packets, not
decoded frames**. The player shows the previous picture for a zero-byte packet.

**Audio**: decoded from the XMV to a WAV first, then encoded. Reason (an ffmpeg defect found here): the `xmv`
demuxer stamps PCM packets with the ADPCM time base (16/11025 s x 93 ticks per packet), so a PCM track muxed
straight from the XMV gets Ogg granule positions 3.55 x too large (UC2_Logos: 3,124,828 for 880,500 samples;
ffprobe then reports 70.1 s). ADPCM tracks were stamped correctly; every track goes through the WAV anyway.
Vorbis end trimming: the end-of-stream granule equals the source's sample count for every output (checked
below). ffmpeg's Vorbis decoder ignores that trim (UC2_Logos decodes to 881,344 = 880,500 + 844): the player
must honour the granule (libvorbis does when told).

### Encoder settings, with the measurements behind them

- Video `libtheora -q:v 7 -g 150`. Quality grid against the decoded XMV (same CFR filter), samples UC2_Logos
  (20 s), 01_AscPre1 (first 30 s), Portrait_Arena_Deadbolt (256x256), Portrait_Char_Anubis (256x512), g=64:

  | q | logos bytes / PSNR-Y / SSIM | cinematic | 256x256 | 256x512 |
  |---|---|---|---|---|
  | 5 | 1.63 MB / 40.63 / 0.9771 | 2.61 MB / 41.01 / 0.9801 | 0.38 MB / 36.52 / 0.9576 | 1.63 MB / 38.26 / 0.9754 |
  | 6 | 2.12 MB / 42.03 / 0.9823 | 3.34 MB / 42.45 / 0.9849 | 0.52 MB / 38.13 / 0.9693 | 2.09 MB / 39.94 / 0.9818 |
  | **7** | 2.88 MB / 43.99 / 0.9873 | 4.47 MB / 44.44 / 0.9893 | 0.76 MB / 40.29 / 0.9796 | 2.77 MB / 42.28 / 0.9879 |
  | 8 | 3.62 MB / 45.60 / 0.9906 | 5.65 MB / 45.95 / 0.9919 | 1.02 MB / 41.82 / 0.9843 | 3.44 MB / 44.05 / 0.9914 |

  The XMV video of those samples is 3.27 MB (logos), 0.98 MB (Deadbolt), 3.42 MB (Anubis): q7 is about 85% of
  the WMV2 size, the step to q8 costs +25% for +1.6 dB, the step down to q6 saves 25% and loses 2 dB (the 256x256
  clip falls to SSIM 0.969). Keyframe interval at q7 (cinematic / 256x256): g=12 4.94 MB / 0.82 MB, g=64 4.47 /
  0.76, **g=150 4.40 / 0.757**, g=300 4.38 / 0.753 (PSNR within 0.3 dB). Seeking is never needed (looping
  restarts from byte 0, and libtheora also makes a keyframe at every scene cut), so 150 (5 s) takes nearly all
  the saving while keeping coarse seek points for later.
- Audio `libvorbis -q:a 4` for the ADPCM tracks (speech + effects, M8's choice for speech), `-q:a 5` for the PCM
  tracks (logos, credits, attract, A51: music, M8's choice for music). Signal-to-distortion against the decoded
  XMV track, first 60 s: 01_AscPre1 track 0 q3 17.6 dB / q4 18.3 dB / q5 20.0 dB (112 / 123 / 158 kbit/s);
  credits q3 23.1 / q4 23.8 / q5 25.6 dB (34 / 37 / 47 kbit/s). The Xbox's own ADPCM is 352 kbit/s per track.
- Pixel format yuv420p in and out (no conversion: the Y'CbCr values pass from the WMV2 decoder to Theora).
  `-map_metadata -1 -fflags +bitexact -flags +bitexact`.
- Production sources in `TestMaps/` were **not used**. Checked: `AscPre3_comp2.avi` and `AscPre7_UTT_Final.avi`
  are the same shots as `03_AscPre3`/`05_AscPre7` at the start (SSIM 0.905 / 0.936 over the first 20 s at offset
  0, 0.79 / 0.80 at 15 frames off) but not the same cut (44.5 s vs 45.0 s; 69.8 s vs 68.0 s), video only
  (the language mixes exist only in the XMVs), and stored as MS Video 1 / QuickTime RLE at 15-bit RGB, whose
  colour banding is not obviously better than WMV2. `Sobek_meets_Selket_sml.mov` matched no cinematic tested
  (SSIM 0.72 against 08_Asc03); the other AVIs are 352x240 previews.
- MovieMaterial clips keep their sizes: 85 of 87 are powers of two; `TVDisplaySelket2` (128x96) and
  `UI_LoadingMovie` (640x480, drawn full screen, not as a material) are not. The GL device takes
  non-power-of-two textures without mipmaps (movie textures have one level and clamp, `FMovieTexture`), as
  WebGL 1 does with clamp and no mips.

### Result: `logs/m9/cook-1.out`, `assets/uc2-cooked/Movies/cook-report.txt`

```
cook-movies.py: 110 XMV in (1052.5 MB) -> 186 files out (653.7 MB, 62.1% of the XMV bytes)
  movie     23 in    881.4 MB ->    501.5 MB
  texture   87 in    171.0 MB ->    152.2 MB
frame/sample accounting: 110 of 110 exact
quality vs decoded XMV: PSNR-Y min 35.50 median 40.14 max 45.13 dB; SSIM(all) min 0.9722 median 0.9831
```
- Every file: packets out = granule frames = CFR slots; every audio output: granule samples = decoded XMV
  samples (the full per-file table is in cook-report.txt; e.g. `01_AscPre1` 4,805 packets -> 4,827 frames,
  7,094,144 samples in and out, A-V -0.035 s; `UC2_Logos` 592 -> 600, 880,500 = 880,500).
- A-V duration difference (audio minus video): -0.034..-0.037 s for 21 movies, i.e. the sound ends at the start
  of the last picture (one frame short); -0.025 s `07_Asc02`, -0.001 s `14_Asc10`. The player pads the sound
  with silence to the video's length (Step 2).
- Worst quality: `Crab_Display02` (128x64) PSNR-Y 35.5 dB, SSIM 0.975; `Portrait_Char_Corrosion` SSIM 0.9722.
- Sizes: `Movies/*.ogv` 423.2 MB (of which A51 74.8, credits 66.3, attract 29.1, logos 3.2), `*.oga` 78.3 MB
  (19.5-19.7 MB per language), `Textures/Movies` 152.2 MB.
- ffmpeg warnings, all recognised, none hidden (cook-report.txt lists every line with its count): 417 x
  `[in#0/xmv] Error during demuxing: Input/output error` = the xmv demuxer reading a packet header at the exact
  end of the file (checked on UC2_Logos: the last packet ends at byte 6,889,472 = the file size; 592 packets
  read, 592 frames decoded, 0 decode errors); 671 x `Guessed Channel Layout: stereo` (the XMV stores no channel
  layout). **0 decode errors**, 0 unrecognised warnings.
- Command: `python3 tools/cook-movies.py --jobs 4` (each ffmpeg/ffprobe inside `bin/capped 2G`, at most 4 at a
  time; 4 encoders + the orchestrator). Wall time about 25 min.

### Language-track evidence (`tools/dev/movie-lang-evidence.py`, `logs/m9/lang-evidence*.out`)

- First attempt (`lang-evidence.out`): 50 ms **level envelopes** of each XMV track against the English voice
  stem in `TestMaps/` (`*-voice.wav`). Not discriminative: 1 of 4 cinematics picked track 0 (03_AscPre3 0.436 vs
  0.304-0.380); 04_AscPre6, 05_AscPre7, 07_Asc02 picked deu/fra with all five tracks within 0.05 of each other.
  Reason: dubbing keeps each line's timing and loudness, so the envelopes of all languages look alike.
- Second (`lang-evidence-2.out`): **waveform** cross-correlation in the voice band (200-3400 Hz, 8 kHz mono),
  envelope only for the coarse lag, then +-100 ms of sample lags (`tools/dev/xcorr.c`, compiled on the fly).
  Only the mix that contains the English recording correlates. Literally:
  ```
  03_AscPre3 track 0 (int) 0.067 | deu 0.013 | fra 0.014 | ita 0.011 | spa 0.010   (sfx-stem control 0.147-0.170 for all)
  04_AscPre6 track 0 (int) 0.619 | deu 0.009 | fra 0.009 | ita 0.009 | spa 0.007   (control 0.378-0.485)
  05_AscPre7 track 0 (int) 0.248 | deu 0.013 | fra 0.010 | ita 0.010 | spa 0.014   (control 0.339-0.382)
  07_Asc02   track 0 (int) 0.668 | deu 0.025 | fra 0.024 | ita 0.026 | spa 0.025   (control 0.413-0.462)
  ```
  The cooked `.ogv` gives the same numbers as XMV track 0 (0.067/0.613/0.251/0.664). So **track 0 = English is
  established from the content** (4 of 4 cinematics with a stem, 10-60 x the other tracks), and the sfx control
  shows all five tracks share the effects bed. **Tracks 1-4 = deu, fra, ita, spa rests on the Xbox code alone**
  (`XMVHelper.cpp:160-189`): no German/French/Italian/Spanish reference recording exists in the tree. 03_AscPre3's
  English correlation is weak (0.067) but still 5x the others: its voice stem is probably not the final mix's
  take/processing.
- Note: the Theora vendoring (`Thirdparty/Theora`, see V2 below) went into commit 76259e5 with the docs by
  accident of staging; nothing else is in it.

## Step 2 - V2: player, interface, audio stream (WIP; state at the 2026-10-04 22:50 cut-off, written 2026-10-05)

### Vendored decoder
`Thirdparty/Theora`: libtheora **1.2.0** from the official Xiph release tarball
`https://downloads.xiph.org/releases/theora/libtheora-1.2.0.tar.xz`, SHA-256
`ebdf77a8f5c0a8f7a9e42323844fa09502b34eb1d1fece7b5f54da41fe2122ec` (= Xiph's SHA256SUMS). Decoder sources only,
no legacy `theora_*` wrapper, no assembly; byte-identical copies (README.uc2 lists them). The vendored libogg is
1.0 (from M8), which lacks `ogg_uint16_t`/`ogg_uint64_t`: the Theora CMake file defines both as PUBLIC macros.
Checked by hand before wiring: every decoder file compiles with `gcc -m32 -O2 -Wall` and with `emcc -O2`, no
warning.

### Code written (committed WIP, nothing of it built yet)
- `Core/Inc/UnMovieBase.h`: `FMovieThread`, `FMoviePlayer` moved unchanged; `UnVcWin32.h` and `UnGnuG.h` include
  it instead of each defining them (two small hunks).
- `Engine/Inc/UnAudio.h`: one virtual appended to `UAudioSubsystem`:
  `OpenMovieAudio(Rate, Channels, bExclusive)` returning an `FMovieAudioStream*` (default NULL). Why: the movie's
  sound must go through the game's audio device (one OpenAL context, the wave-writer capture, the device's
  volume and pause handling) and the video must follow that sound's clock; the engine interface had no way to
  stream PCM. A virtual appended at the end changes no object size (verifyclasses unaffected: to be checked).
  NullAudioDevice inherits NULL -> movies run silent on real time.
- `Engine/Inc/UnMovie.h`, `Engine/Src/UnMovie.cpp` (new, ours): `FMovieAudioStream` (FreeFrames/Write/
  PlayedFrames/QueuedFrames/Pause, deleted to close), `FPortableMoviePlayer : FMoviePlayer` (Open/Advance/
  IsPlaying/HasEnded/Pause/Rewind/frame, revision, BGRA), `GCreateMoviePlayer` factory (NULL = no movies),
  `WITH_PORTABLE_MOVIES` (the named capability that will lift the `_XBOX` gates in UnGame.cpp);
  `appFindMovie` (Xbox names `D:\Movies\..`, `D:\Textures\Movies\..`, `..\Movies\..` -> `../Movies` or
  `../Textures/Movies` `.ogv`, case-insensitive match because the content says `Portrait_Gametype_NS`, and the
  language rule), `appOpenMovie`, `appCanPlayFullscreenMovie` (client, viewport, not NullRenderDevice; `-NOMOVIE`
  all, `-NOSTARTUPMOVIES` logos: Epic's UE3 switch names, UE2 had none; ini `[UC2Movie] StartupMovies,
  Cinematics, LoadingMovie`), `appPlayMovieFullscreen` (blocking loop: pump input via `Viewport->UpdateInput`,
  Advance, draw a transient MovieMaterial wrapping the player with Canvas->DrawTile keeping aspect, the
  HideSubtitles bands with a black ConstantColor when `GShowSubtitles` is off, present, `-movieshot=<t>,..`
  for verification screenshots at movie times, one log line a second), `appMovieInputEvent` (to be called first
  in UEngine::InputEvent: swallows all input during a full-screen movie, any press after 0.5 s skips),
  `appUpdateMovieMaterial` (the Xbox SetMovieMaterial logic minus D3D: bReset, open on first use, advance when
  bNeedsFrameUpdate, OnDonePlaying once at the end, USize/VSize), and the UnGame.cpp hook functions with the Xbox
  names (PlayIntroCinematic/PlayOutroCinematic/PlayLoadingMovie/SetEngineReady/IsPlayingMovie).
- `Source/UC2Movie/Src/UC2MoviePrivate.h`, `UC2MoviePlayer.cpp`: `FUC2OggReader` and `FUC2MoviePlayer`.

### Design decisions taken
- **Threading: none.** Decoding happens in `Advance()` on the calling thread (render device for materials, the
  full-screen loop otherwise). Works the same natively and in the single-threaded web build. The Xbox's thread
  only existed to keep the loading movie running during a blocking load; see the loading-movie decision below.
- **Sync**: with sound, the clock is the sound stream's played frames (`PlayedFrames`, OpenAL buffer accounting +
  `AL_SAMPLE_OFFSET`), as the XMV decoder synced to the chosen audio stream. Picture n is shown for clock in
  [n/fps,(n+1)/fps), computed as floor(played*fps/rate) (1470 sound frames per picture at 44.1 kHz/30 fps).
  The sound is padded with silence to the video length (cooked sound ends one frame early), so a pass is
  exactly FrameCount/fps for both and a loop is seamless; when the sound queue starves, the picture waits.
  Without sound or audio device: real time (appSeconds, pause excluded); a material not drawn for > 1 s
  continues from its picture instead of decoding the whole gap. `MOVIE_ManualSync` = one frame per Advance.
- **Memory bounds**: one `FArchive` per stream (video and audio each open their own reader on the file, even
  when it is the same .ogv), 16 KB reads, only the selected stream's pages enter libogg, so the buffered data is
  one page plus one packet whatever the interleaving; the sound queue is bounded by the device (planned 6 x 8192
  frames); one BGRA picture (640x480x4 = 1.2 MB) plus libtheora's reference frames. No whole-file loads (the
  Xbox's `bDecodeFromMemory` background load is not needed and not done).
- **Texture format**: BGRA bytes (`TEXF_RGBA8`, what `FMovieTexture` declares and the GL driver uploads as
  GL_BGRA), top row first, alpha 255; BT.601 limited-range matrix in 8.8 fixed point, chroma replicated per 2x2.
  To check numerically against ffmpeg in V3.
- **Frame count**: from the last page's granule (`th_granule_frame`+1), so zero-byte duplicate packets count
  as frames (the cook's convention); decoding uses `th_decode_packetin` (TH_DUPFRAME keeps the picture).
- **Vorbis end trim**: decoding stops at the end-of-stream granule (from the file's tail).
- **Loading movie**: no thread, and drawing from inside the loader would re-enter the renderer while the level is
  being replaced, so the animated version stays off behind its hook; `PlayLoadingMovie` draws the movie's first
  picture with "LOADING..." once before the load (static loading screen). To measure: it must not slow loading.
- **Exclusive sound** for full-screen movies: the device pauses cues and music while the stream lives (the Xbox
  played full-screen movies with nothing else running). Movie sound gain 0 dB, no category: XMV sound went
  through DirectSound directly, outside XACT's categories (to state in the final report).

### Builds: nothing of the WIP has been compiled yet. Next, in order
1. `Source/UC2Movie/{CMakeLists.txt, Inc/UC2Movie.h, Src/UC2Movie.cpp}` (package, factory registration in
   `AUTO_INITIALIZE_REGISTRANTS_UC2MOVIE`), root CMakeLists M9 block (Theora + UC2Movie in UC2_PACKAGES, also on
   the web), UCC + SDLLaunch registration.
2. UC2ALAudio: `OpenMovieAudio` + the stream class (source + 6 buffers, played-frame accounting, exclusive
   pause/resume, detach on device destroy).
3. Build native; then the self-test commandlet (V3) with ffmpeg `framecrc` references.
4. V4: GL driver `SetMovieMaterial` PC body in a NEW file `UC2GLDrv/Src/UC2GLMovie.cpp` + deleting the PC stub in
   `UC2GLMaterialState.cpp` (one hunk); launcher logos; UnGame.cpp gates; UnEngine.cpp input hook.

## Step 3 - V2 built, V3 self-test green (2026-10-05)

- Build: `bin/capped 6G 3000 -- bash -c 'cmake --preset native && cmake --build build-native -j8'` -> rc 0,
  `uc2.bin` and `ucc-bin` contain the player (`UC2CreateMoviePlayer`, `appPlayMovieFullscreen`). No warning in
  our files beyond the engine's own DECLARE_CLASS `-Wwrite-strings` pattern. Packages: `Thirdparty/Theora`
  (root CMakeLists, M9 block), `Source/UC2Movie` in `UC2_PACKAGES` before UCC (not native-only: it compiles for
  the web too), registered in `UCC.cpp` and `SDLLaunch.cpp` (`UC2_WITH_UC2MOVIE`, one include + one registrant
  line each, like UC2ALAudio). `tools/dev/mkrun-uc2.sh` hard-links `Movies/` and `Textures/Movies/` from the
  cooked tree into the run tree.
- UC2ALAudio: `OpenMovieAudio` (one line in `UC2ALAudio.h`) and `FUC2ALMovieStream` appended to
  `UC2ALAudioSubsystem.cpp` (+1 member in `FUC2AudioState`, +2 lines in `Destroy`): one relative 2D source, 12
  buffers x 4096 frames (1.1 s at 44.1 kHz), played frames = unqueued buffers + `AL_SAMPLE_OFFSET`, made
  monotonic (a buffer can finish between the two queries); a starved source restarts when data arrives;
  exclusive: `NotifyPauseChange(1)` for the cues and `alSourcePause` for playing music, undone on delete; the
  device detaches live streams in `Destroy` so a stream deleted after the device does nothing.
- Defect found in our own first draft: `TArray<FString>::AddItem` (UE2's AddItem assigns into unconstructed
  memory, the M8 finding) crashed the commandlet in `realloc`; `new(Array) FString(...)` instead.
- **Self-test** `tools/dev/movie-selftest.sh m9` (ffmpeg references into `logs/m9/selftest/`, then
  `bin/capped 2G 900 -- ./ucc-bin UC2Movie.UC2MovieSelfTest REF=...`): **96 checks, 0 failed**, 12 s.
  Cases: UC2_Logos (sound int), 03_AscPre3 with the `deu` track, UI_LoadingMovie (640x480), Portrait_Arena_Deadbolt
  (256x256), Portrait_Char_Anubis (256x512), Reactor_01 (128x128, 15 fps), TVDisplaySelket2 (128x96, 15 fps).
  Per case: frame count = packet count; a new picture exactly at ffmpeg's decoded pictures (03_AscPre3: 1,310 new
  + 13 held of 1,323); **every picture identical** to ffmpeg's decode (Adler-32 of the Y'CbCr planes = ffmpeg
  `framecrc`; e.g. 1310/1310, 600/600-of-593 decoded...); BGRA of two pictures vs ffmpeg swscale BT.601 limited
  range with replicated chroma: **max difference 1** (0 values differ by more than 1); the fixed-point matrix vs
  the float formula: max 1; loop by frame steps (frame 0's picture after the last), loop on the real-time clock
  (two wraps, 0 wrong), random 5-40 ms real-time steps (frame error 0, ends at the length); with sound (simulated
  device, the player's own feed path): **A/V sync error 0 frames** over the whole movie (03_AscPre3: 1,943
  random steps; ends when 1,944,810 = 1,323 x 1,470 frames have played), sound length = granule = cook's
  XMV-decoded count (1,943,296), **sound values vs ffmpeg's Vorbis decoder max difference 1** (16-bit steps; 0
  differ by more than 1; libvorbis 1.0 vs ffmpeg's own float decoder: within rounding), the padding (1,514
  frames) is silence, loop with sound: one wrap N-1 -> 0, no skipped frame, second pass's first second of sound
  identical to the first. Language: int/deu/fra/ita/spa/jpn for 03_AscPre3 give `.ogv`/`_deu`/`_fra`/`_ita`/
  `_spa`/`.ogv`; deu for UC2_Logos falls back to the `.ogv` ("no track 1 of its own"); `Portrait_Gametype_NS`
  (the content's spelling) finds `Portrait_GameType_NS.ogv`; `CH_Flak_Fire` (referenced, never shipped) finds
  nothing. Literal summary line: `UC2MovieSelfTest: 96 checks, 0 failed`.
- Two things the first run taught (both fixed in the references, not in the player):
  1. **ffmpeg's frame-threaded Theora decoder is not deterministic.** TVDisplaySelket2: two `framecrc` runs with
     default threads gave two different checksum lists (md5 of the lists de8e6c48... / 0837f9a9...); with
     `-threads 1` both runs agree, and agree with libtheora on all 400 frames (also checked with libtheora's own
     `examples/dump_video.c` built from the vendored sources: 0 of 400 differ). In a threaded run some frames
     came out with Cb=Cr=0 (MSE 5212 against the XMV source; libtheora 11). Every ffmpeg decode in the self-test
     now runs `-threads 1`, and so does the cook's quality measurement (the first cook's chroma figures for
     TVDisplaySelket2, u 26.3 dB / min 10.7, were this artefact: single-threaded u 47.6, min 37.2). The whole cook
     is re-running with that change (`logs/m9/cook-2.out`) so every PSNR/SSIM in the report is single-threaded.
  2. The default swscale conversion interpolates chroma; the player replicates each chroma sample over its 2x2
     block. Against swscale's interpolation the difference reaches 18-36 levels at colour edges; against swscale
     with replicated chroma (neighbor to 4:4:4, then the BT.601 matrix) it is at most 1. Replication is a
     choice: it is what a 4:2:0 texture shows without filtering, and cheaper. (The Xbox decoder's choice is not
     documented in the XDK.)
