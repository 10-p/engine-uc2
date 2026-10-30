# Cooking the game folder

The game runs from a game folder: the maps, textures, sounds, music and movies in the formats the engine reads, and
`System/` with the script packages, the settings and the game's binaries. `cook.py` makes that folder from your copy of
the game:

- **the game's Xbox disc**: a disc image (`.iso`) or a folder with the disc's files;
- **Epic's source tree** (`uc2-avaris-2226`).

## Quick steps

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

- **Python 3.8 or newer**: <https://www.python.org/downloads/> (Linux and macOS have it).
- **ffmpeg**: Debian/Ubuntu `sudo apt install ffmpeg`, Fedora `sudo dnf install ffmpeg` (RPM Fusion), Arch
  `sudo pacman -S ffmpeg`, macOS `brew install ffmpeg`, Windows `winget install Gyan.FFmpeg`.
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

`python3 cook.py --help` lists the rest.

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
