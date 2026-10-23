# Cooking the game folder

The engine runs from a game folder: `System/` (the script packages and inis, and the binaries), the PC-format packages
(`Maps/`, `Textures/`, `StaticMeshes/`, `Animations/`), the sound in the engine's cooked format (`Sounds/`, `Music/`)
and the movies as Ogg Theora (`Movies/`, `Textures/Movies/`). `tools/cook.py` makes one, in one command, from either
source of the game's content:

- **Epic's source tree** (`--source epic`, e.g. `<repo>/../uc2-avaris-2226`): the original assets.
- **The game's Xbox disc** (`--source xbox`): a folder with its files, or a disc image (`.iso`, an xiso or a full dump).

```bash
tools/cook.py --source epic --in ../uc2-avaris-2226 --out ../cooked/uc2-epic --build build-native64
```

```bash
tools/cook.py --source xbox --in UC2.iso --out ../cooked/uc2-retail --build build-native64
```

```bash
cd ../cooked/uc2-epic/System && ./uc2.bin
```

`--check` runs only the first two steps (tools and input) and writes nothing. Run it first on a new machine.

## Steps

1. **Tools.** Python 3.8, `ffmpeg` and `ffprobe` with every codec the cook uses, and with `--build` the libraries the
   game loads. Everything missing is listed with the command that installs it, and the cook stops.
2. **Input.** The folder's layout is checked against `--source` (a disc given as `--source epic` is refused, and the
   other way round). A disc image is extracted once into the cache (`<cache>/disc/<image name>`: `System/`,
   `Sounds/`, `Movies/`, `Textures/` and the `Xbox*/` package folders) by `tools/xiso.py`; nothing else reads disc images.
3. **System and packages.** Copied (a reflink where the file system can). Every package is checked to be PC format
   (licensee 0), and the 17 script packages against `patches/shipped.sha256` (a difference is noted in `COOK.txt`).
   From a disc, the maps, textures, static meshes and animations are converted from the Xbox format by
   `tools/cook-xbox-packages.py` (below), with the report `Maps/xbox-packages.txt`.
4. **Audio** (`tools/cook-audio.py`). The XACT project (`Sounds/Avaris.xap`) becomes `Sounds/*.uc2wb` (wave bank
   index), `*.ogp` (the waves, Ogg Vorbis), `*.uc2cues` (cue tables) and `Music/*.ogg`. The waves come from the
   project's WAVs (`SoundData/`, `Music/`), or are decoded from the shipped `.xwb` where there is no WAV, which on a disc
   is every wave and every music track (Xbox ADPCM, and one WMA track decoded by ffmpeg). `--xbox-audio` copies the
   shipped `.xsb`/`.xwb` instead: the engine plays the Xbox banks itself (no `Music/`: the disc's music is
   `Sounds/M_<Song>.xwb`, and its WMA track stays silent).
5. **Movies** (`tools/cook-movies.py`). Every XMV (WMV2 video, Xbox ADPCM or PCM audio) becomes `Movies/<Name>.ogv`
   (Theora, and the first audio track as Vorbis), `Movies/<Name>_<lang>.oga` for each other language track that
   differs, and `Textures/Movies/<Name>.ogv` (the movie materials, video only).
6. **Build** (`--build <build-dir>`). The build's `uc2.bin`, `ucc-bin` and `libopenal.so.1` (a Windows build: `uc2.exe`,
   `ucc.exe`, `OpenAL32.dll`), its patched script packages (`<build-dir>/Scripts/System`) and the repository's inis
   (`Engine/Config/*.ini`) go into `System/`, plus empty `Save/` and `Cache/`. The folder then runs as it is.
7. **Record.** `COOK.txt` (the command, the input, the tool versions, where every folder came from) and
   `MANIFEST.sha256` (every file the cook wrote).

The cooked formats are specified in `docs/porting/analysis/audio.md` and `video.md` ("Cooked format").

## What each output folder is made from

| Output | `--source epic` | `--source xbox` |
|---|---|---|
| `System/*.u`, `*.ini`, `Localization/`, `Ladders/` | the tree's `System/` | the disc's `System/` (the final inis and localisation), plus `DefaultXbox.ini` and `DefUserXbox.ini` from `Engine/Config` (the script build reads them) |
| `Maps/`, `Textures/*.utx`, `StaticMeshes/`, `Animations/` | the tree | converted from `XboxMaps/`, `XboxTextures/`, `XboxStaticMeshes/`, `XboxAnimations/` and `System/Script.lin` (the entry level); `--pc-packages <dir>` copies PC ones instead (Epic's tree or a cooked game folder) |
| `Sounds/`, `Music/` | `Avaris.xap` with `SoundData/` and `Music/*.wav` | `Avaris.xap` with the shipped `.xwb` |
| `Sounds/` with `--xbox-audio` | the shipped `.xsb`/`.xwb` | the same |
| `Movies/`, `Textures/Movies/` | `Movies/*.xmv`, `Textures/Movies/*.xmv` | the same files |

### The disc's packages

The disc's maps, textures, static meshes and animations are Xbox-format packages (licensee 1, Epic's
`XBOX_ASSET_FORMAT`), which the engine does not load (CLAUDE.md: the Xbox macros are never defined).
`tools/cook-xbox-packages.py` converts them:

- **Maps** are `.LIN` linear-load streams: every byte the Xbox loader read, in its order. They are cut back into
  their exports (`uc2xpkg.read_lin`); the entry level is in `System/Script.lin`. Their textures, static meshes and
  animations are inside the map, renamed `cla<N>`.
- **Bulk data** is in the `XboxTextures/*.xpr` bundles, by object name: texture pixels (`<Map>.xpr`, `Natives.xpr`,
  per character and weapon), vertex, colour and UV arrays (`*_uma.xpr`), index push buffers (`*_pb.xpr`), animation
  data (`*_a_uma.xpr`). The converters put it back into the PC layout: `uc2xbsp.py` (BSP models, with the light map
  records the PC rebuilds the level from), `uc2xtex.py` (textures), `uc2xmesh.py` (static meshes), `uc2xskel.py`
  (skeletal meshes, with the CPU-skinning data the Xbox left out, and animations), `uc2xterrain.py` (terrain, with
  its heightmaps). Every other export is the same in both formats and is copied.
- **What does not come back:** editor-only objects (brushes, cameras), static mesh `RawTriangles` and instance
  `Lights` (editor data), the textures and texture-package static meshes the Xbox never shipped (placeholders and
  meshes without geometry, listed in `Maps/xbox-packages.txt`), and `Maps/ComboMatinees/` (not on the disc). Vertex
  positions and normals come back quantised as the Xbox stored them.

The formats and the measured differences are in `docs/porting/analysis/xbox-packages.md`.

## What has to be installed

| Tool | For | Debian/Ubuntu |
|---|---|---|
| Python 3.8 or newer | every tool here | `python3` |
| `ffmpeg`, `ffprobe` with the `libvorbis` and `libtheora` encoders and the `xmv`, `wmv2`, `adpcm_ima_xbox`, `asf` and `wmav2` readers | audio and movies | `ffmpeg` (Ubuntu's has all of them; Fedora: RPM Fusion's `ffmpeg`) |
| the X11 client libraries and `libGL` | running the game (`--build`) | `libx11-6 libxext6 libxcursor1 libxi6 libxfixes3 libxrandr2 libxss1 libgl1` |

Disc images need nothing more: `tools/xiso.py` reads them. `cook.py` checks all of this first and prints the install
command for the distribution it runs on.

## The output folder and the caches

`--out` is created, or has to be a folder `cook.py` made: it keeps the list of its files in `.cook-files`. A re-cook
rewrites what changed, removes the files it wrote before and no longer writes (switching to `--xbox-audio` removes the
cooked sound), and leaves the rest alone: the engine's `Avaris.ini`, `User.ini`, logs and `Save/`.

A folder cooked without `--build` is a game folder for the build (`-DUE_ASSETS_DIR=<dir>`, BUILD.md "Script
patches"). With `--build`, its `System/` holds the build's patched script packages and the repository's inis, as a run
tree does, so it is no longer the script build's input.

The caches (`--cache`, default `$XDG_CACHE_HOME/uc2-cook` or `~/.cache/uc2-cook`) are shared by every cook: `audio/`
(one encoded Ogg per source WAV and quality), `movies/` (the movie cook, skipped per file when the source and the
settings are unchanged), `disc/` (extracted images) and `logs/` (each step's whole output). A second cook of the same
content only copies. One cook runs at a time per cache (a lock).

`--skip audio,movies` leaves those steps out (an earlier cook's audio or movies in the folder stay); `--jobs N` sets
the parallel encoders (default: half the CPUs; the
movie cook takes at most 4).

## The tools

| Tool | What it does |
|---|---|
| `cook.py` | the entry point above |
| `cook-audio.py` | the audio step: the XACT project to `.uc2wb`, `.ogp`, `.uc2cues` and `Music/*.ogg`, with `Sounds/cook-report.txt` (everything it did not understand, the cross-check with the shipped banks) |
| `cook-movies.py` | the movie step: XMV to Ogg Theora/Vorbis, with `Movies/movie-manifest.json` and `cook-report.txt` (frame and sample accounting, PSNR/SSIM against the decoded XMV) |
| `cook-xbox-packages.py` | the package step of a disc: the Xbox-format maps, textures, static meshes and animations as PC packages, with `Maps/xbox-packages.txt` (per package what was converted, copied or left out, and every texture or mesh without data) |
| `uc2xpkg.py` | reads and writes packages in both formats; cuts a `.LIN` back into its exports |
| `uc2xpr.py` | reads the XPR bundles |
| `uc2xbsp.py`, `uc2xtex.py`, `uc2xmesh.py`, `uc2xskel.py`, `uc2xterrain.py` | the converters of the classes whose native data differs (models, textures, static meshes, skeletal meshes and animations, terrain) |
| `xiso.py` | lists and extracts an Xbox disc image (XDVDFS) |
| `uc2xwb.py` | reads the shipped wave banks (`.xwb`) and decodes Xbox ADPCM and PCM waves |
| `uc2xsb.py` | reads the shipped sound banks (`.xsb`) |
| `psh2glsl.py`, `pshgpu/` | the Xbox pixel shaders as GLSL, and their checks (not a cook step) |
| `scripts/` | the script patch pipeline (BUILD.md, "Script patches") |
| `dev/` | checks and measurements; `dev/mkrun-uc2.sh` makes a run tree from a game folder and a build, `dev/mkassets-uc2.sh` a game folder from Epic's tree and a separate cook |

Each tool's header says how to run it alone.
