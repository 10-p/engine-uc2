# Cooking the game folder

The game runs from a game folder: the maps, textures, sounds, music and movies in the formats the engine reads, and
`System/` with the script packages, the settings and the game's binaries. `cook.py` makes that folder from your copy of
the game:

- **the game's Xbox disc**: a disc image (`.iso`) or a folder with the disc's files;
- **Epic's source tree** (`uc2-avaris-2226`).

## Quick steps on Windows

1. Install Python 3.8 or newer and ffmpeg yourself, on PATH ("What you need" below).
2. Unpack the zip: right-click it, **Extract All...** (`cook.cmd` does not run from inside the zip). Windows may ask
   whether to run `cook.cmd`, a file from the internet: **Run**.
3. Double-click `cook.cmd`, or drop your disc image (`.iso`) onto it. It checks Python and ffmpeg first and names what
   is missing, then asks for the disc image (drag it into the window) and the game folder (Enter: `UC2` beside the
   cook's folder), and cooks (about half an hour, most of it the movies).
4. Unpack the game's Windows release into the game folder: `uc2-<version>-windows-x86.zip`, then
   `uc2-<version>-patched-scripts.zip`, and for the editor `uc2-<version>-unrealed-windows-x86.zip` and
   `uc2-<version>-unrealed-bitmaps.zip`. The release is
   the game's programs and its PC settings; the cook does not make them. Unpack it after the cook: some of its files
   replace ones the cook copies from the disc (the disc's `Default.ini`, `DefUser.ini` and `DefUnrealEd.ini` name the
   Xbox's drivers; `GUI.u`, `UCGameplay.u`, `UCMenu.u`, `UCMutators.u` are the patched script packages). A cook
   overwrites the files it writes, these among them: to cook again, use a new folder and repeat the steps.
5. Start `uc2.exe` (or `UnrealEd.exe`) in the game folder's `System\`. A game folder started before the release was
   in keeps the Xbox save manager in `System\Avaris.ini`, and the game exits at start: delete `Avaris.ini`.

In a Command Prompt or PowerShell opened in the cook's folder, `.\cook.cmd` takes the options below, for example
`.\cook.cmd --check --in "C:\Games\Unreal Championship 2.iso"` (PowerShell runs a file of the current folder only with
the `.\` in front). `py cook.py` and `python cook.py` do the same; `python3` is often the Microsoft Store's
placeholder, not Python.

## Quick steps on Linux and macOS

1. Install Python 3.8 or newer and ffmpeg (below).
2. Check that everything is in place; this writes nothing:

   ```bash
   python3 cook.py --check --in "Unreal Championship 2.iso"
   ```

3. Cook the game folder (about half an hour, most of it the movies):

   ```bash
   python3 cook.py --in "Unreal Championship 2.iso" --out UC2
   ```

4. Copy the game's release into `UC2/`: its `System/` folder, and the files of its patched script packages zip
   (`uc2-<version>-patched-scripts.zip`) into `UC2/System/`. The cook can copy the release's binaries for you:

   ```bash
   python3 cook.py --in "Unreal Championship 2.iso" --out UC2 --uc2-binary-path uc2-release/System/uc2.bin --ucc-binary-path uc2-release/System/ucc-bin
   ```

5. Start the game from `UC2/System/`.

In the repository the cook is `tools/cook.py`.

## What you need

- **Python 3.8 or newer**: Linux and macOS have it. Windows: <https://www.python.org/downloads/> (in the installer,
  tick **Add python.exe to PATH** when it offers it), or Python 3 from the Microsoft Store.
- **ffmpeg** with ffprobe: Debian/Ubuntu `sudo apt install ffmpeg`, Fedora `sudo dnf install ffmpeg` (RPM Fusion),
  Arch `sudo pacman -S ffmpeg`, macOS `brew install ffmpeg`. Windows: `winget install Gyan.FFmpeg`, or a build from
  <https://ffmpeg.org/download.html> whose `bin` folder (the one with `ffmpeg.exe` and `ffprobe.exe`) you add to PATH.
  Without ffmpeg, `--skip audio,movies` cooks everything but the sound, the music and the movies.
- Both on PATH: a new Command Prompt answers `python --version` (or `py -3 --version`) and `ffmpeg -version`. A window
  opened before the install does not see them; open a new one.
- **About 2.3 GB free** for the game folder, and as much again while a disc image is unpacked.

`--check` names anything missing and how to install it.

## Options

| Option | What it does |
| --- | --- |
| `--in <path>` | your disc image, disc folder or Epic's tree |
| `--out <folder>` | the game folder: a new one, or one the cook made before (a second cook updates it and keeps your settings and saves) |
| `--check` | checks the tools and the input, and writes nothing |
| `--uc2-binary-path <file>` | also copies the game (`uc2.bin` or `uc2.exe`) into `System/`, with the settings and the OpenAL library it runs with |
| `--ucc-binary-path <file>` | also copies UCC (`ucc-bin` or `ucc.exe`: the dedicated server and the command-line tools) into `System/` |
| `--skip audio,movies` | leaves the sound and music, or the movies, out (for a machine without ffmpeg) |
| `--xbox-audio` | keeps the disc's own sound banks, which the game plays as they are, instead of converting them |
| `--jobs <n>` | how many sounds and movies are converted at once (default: half the processors) |
| `--ask` | checks the tools, then asks for the disc image and the game folder when they are not given (`cook.cmd` without options) |

`python3 cook.py --help` (Windows: `.\cook.cmd --help`) lists the rest.

## When something goes wrong on Windows

The cook prints the message Windows gave, the file it was about, and what usually helps:

- **Python was not found; run without arguments to install from the Microsoft Store**, or **cook.cmd: the cook needs
  Python 3.8 or newer on PATH**: Python is not installed or not on PATH (without it, `python` and `python3` are the
  Microsoft Store's placeholder). Install it ("What you need"), then open a new window.
- **missing: ffmpeg** (or ffprobe) although it is installed: the window was opened before the install, or the folder
  on PATH is not the one with `ffmpeg.exe` (a build's `bin` folder). Open a new window; `where ffmpeg` shows what it
  finds.
- **cook.cmd: cook.py is not beside it**, or the window closes at once: `cook.cmd` was started from inside the zip.
  Extract All first.
- **Access is denied** (`WinError 5`, `PermissionError`): the game folder has to be one you can write to, so not under
  `C:\Program Files` or `C:\Windows` (`C:\Games\UC2` works); close the game, UnrealEd and any Explorer window showing
  the folder, since Windows does not let a program replace a file another one has open; Windows Security's
  **Controlled folder access** keeps programs out of Documents, Pictures and the Desktop (choose another folder, or
  allow `python.exe` under Virus & threat protection > Ransomware protection); an antivirus that is scanning a new
  file holds it for a moment, so run the cook again.
- **The process cannot access the file because it is being used by another process** (`WinError 32`): close the
  program that has it open (the game, UnrealEd, an Explorer preview).
- **There is not enough space on the disk**: the game folder takes about 2.3 GB, and a disc image as much again while
  the cook runs.
- **The filename or extension is too long**: use a short folder such as `C:\UC2`.
- **A step failed**: the cook shows the end of the step's log, and the whole log is in `.cook-logs\` in the game
  folder.

## What the cook does

- **Maps.** On the disc each map is a `.LIN` file: a linear-load stream, everything the Xbox read to load the level,
  in reading order, from the map and from the packages it uses. It is not a package and does not load on its own, so
  the cook turns each one back into a map package. The Xbox never read a level's brushes (the shapes its geometry was
  built from, which only the level editor uses), so the disc has none: the game plays a cooked map as the Xbox did,
  and the editor opens it after a warning, with its actors editable but its geometry fixed as cooked (rebuilding the
  geometry removes it). The editor opens the cooked textures, meshes and animations.
- **Textures, meshes and animations.** The disc keeps their pictures and geometry apart from the packages, in `.xpr`
  bundles; the cook puts them back together. Much of it is in the bundles only as a map's copy of the asset; the cook
  gives each package asset the data of the copies that stand for it (`uc2-pc-tables.json.gz` says which).
- **The editor's source triangles.** The editor builds a static mesh's render data from source triangles it keeps
  beside it (`RawTriangles`); the Xbox never read them and the disc does not have them. The cook writes them back
  from the render data: each triangle's positions, UVs, colours and material as drawn, and smoothing groups from the
  normals (faces whose normals were summed into a vertex's are smoothed together). The editor then shows each mesh's
  real triangle count, and rebuilding a mesh gives back the cooked vertices, triangles and sections, its normals to
  within the disc's rounding. Where a section's material blends (alpha blending, or additive or multiplicative
  blending that writes depth), the order its triangles are drawn in shows where they overlap: the cook writes those
  triangles in an order under which a rebuild draws them as the disc does, where its search finds one (the search
  adds a few minutes to the cook; `Maps/xbox-packages.txt` names the blended sections it found no order for). Some
  rebuilds still differ: two vertices whose normals differed by less than the disc's rounding weld into one, a
  strip of triangles can come out in another order (the order the meshes were first built in decides it), and on
  large meshes, where the disc rounds positions coarsely, a few normals move by more than a degree.
- **Packages for network play.** The packages come out the same as the PC game folder's, so a player who cooked the
  disc and a player with the PC folder can play together.
- **Sound and music** become Ogg Vorbis, and **movies** Ogg Theora.
- **Scripts, settings and translations** are copied as they are.

`COOK.txt` in the game folder says what was made from what, and `Maps/xbox-packages.txt` lists what the disc does not
have: a texture the disc never carried shows as a placeholder, a static mesh it never carried has no geometry, and a
font none of whose pages it carried is left out (the editor then draws its text with the game's own font). If a step
fails, its log is in `.cook-logs/`.
