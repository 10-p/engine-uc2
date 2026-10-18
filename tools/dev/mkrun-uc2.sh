#!/usr/bin/env bash
# Assemble a native run tree from a build directory.
#
#   tools/dev/mkrun-uc2.sh <build-dir> [<tree-name> [<more-build-dirs>...]]
#   tools/dev/mkrun-uc2.sh build-native            ->  <repo>/../run/native
#   tools/dev/mkrun-uc2.sh build-native m4w build-wasm   ->  native and web binaries in one tree
#
# The tree is hard links into the canonical asset tree for the content folders (they are never written in
# place; symlinks would break the engine's package lookup) and a real copy of System/, because the engine
# derives and rewrites its inis and writes its logs there. The build's executables go into System/.
# Re-running replaces the tree: it only ever removes <repo>/../run/<tree-name>, which it created itself.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
build=$(cd "${1:?usage: mkrun-uc2.sh <build-dir> [<tree-name>]}" && pwd)
name=${2:-$(basename "$build" | sed 's/^build-//')}
# The tree is HARD-LINKED, and a hard link cannot leave a mount: WorkUnreal's assets/uc2-assets is an overlay mount, so
# there the default is its real copy native-runtrees/_assets/uc2 (`cp -a assets/uc2-assets/. native-runtrees/_assets/uc2`).
if [ -z "${UC2_ASSETS_DIR:-}" ] && [ -d "$work/native-runtrees/_assets/uc2/System" ]; then
  UC2_ASSETS_DIR=$work/native-runtrees/_assets/uc2
fi
assets=${UC2_ASSETS_DIR:-$work/assets/uc2}
run=$work/run/$name

[ -d "$assets/System" ] || { echo "no asset tree at $assets (tools/dev/mkassets-uc2.sh builds it)" >&2; exit 1; }
case "$name" in */*|.|..|"") echo "bad tree name: $name" >&2; exit 1;; esac

rm -rf "$run"
mkdir -p "$run"
for d in "$assets"/*/; do
  d=$(basename "$d")
  [ "$d" = System ] && continue
  cp -al "$assets/$d" "$run/$d"
done
cp -a "$assets/System" "$run/System"
chmod -R u+w "$run/System"

# The cooked audio (tools/cook-audio.py: Sounds/ and Music/), hard-linked like the content folders. The
# audio device reads ../Sounds and ../Music from System/, as the Xbox read ..\Sounds. A missing cooked tree
# only means a silent game.
cooked=${UC2_COOKED_DIR:-$work/assets/uc2-cooked}
# In an asset tree made by tools/dev/mkassets-uc2.sh the cooked folders are in the tree and arrive with the
# content folders above; these links serve a tree without them. An explicit UC2_COOKED_DIR (a cook
# that is not installed yet, e.g. <repo>/../logs/f6/cooked) replaces the asset tree's Sounds/ and Music/ links.
for d in Sounds Music; do
  if [ -n "${UC2_COOKED_DIR:-}" ] && [ -d "$cooked/$d" ]; then
    rm -rf "${run:?}/$d"
    cp -al "$cooked/$d" "$run/$d"
  elif [ -d "$run/$d" ]; then
    :
  elif [ -d "$cooked/$d" ]; then
    cp -al "$cooked/$d" "$run/$d"
  else
    echo "note: no cooked $d at $cooked (tools/cook-audio.py makes it); the game runs silent" >&2
  fi
done

# UC2_XBOX_SOUNDS plays the shipped Xbox banks instead (UC2ALAudio reads Sounds/<Bank>.xsb + .xwb itself where
# no cooked file is there):
#   UC2_XBOX_SOUNDS=1    Sounds/ = the shipped banks only (hard links of <repo>/../epic-sources/Sounds, which is the
#                        retail disc's Sounds/ by name and size), and no Music/ (the disc has none: its music is
#                        Sounds/M_<Song>.xwb). A retail-disc tree.
#   UC2_XBOX_SOUNDS=add  the cooked Sounds/ and Music/ as above, plus the shipped .xsb/.xwb beside them (the device
#                        takes the cooked file where both exist: tools/dev/audio-xsb-compare.sh needs both).
# UC2_XBOX_DIR=<dir> takes the banks from another Sounds/ folder (e.g. one extracted from an ISO).
xbox_sounds=${UC2_XBOX_DIR:-$work/epic-sources/Sounds}
case "${UC2_XBOX_SOUNDS:-}" in
  "") ;;
  1)
    rm -rf "${run:?}/Sounds" "${run:?}/Music"
    mkdir -p "$run/Sounds"
    cp -al "$xbox_sounds"/. "$run/Sounds/"
    echo "UC2_XBOX_SOUNDS=1: Sounds/ = $(ls "$run/Sounds" | wc -l) shipped files from $xbox_sounds, no Music/" >&2
    ;;
  add)
    # (Sounds/ is this tree's own directory of hard links: adding links to it touches nothing shared.)
    mkdir -p "$run/Sounds"
    for f in "$xbox_sounds"/*.xsb "$xbox_sounds"/*.xwb; do ln -f "$f" "$run/Sounds/"; done
    echo "UC2_XBOX_SOUNDS=add: the cooked Sounds/ plus $(ls "$xbox_sounds"/*.xsb "$xbox_sounds"/*.xwb | wc -l) shipped banks" >&2
    ;;
  *) echo "UC2_XBOX_SOUNDS must be 1 or add" >&2; exit 1;;
esac

# The cooked movies (tools/cook-movies.py: Movies/ and Textures/Movies/), hard-linked the same way. The
# player reads ../Movies and ../Textures/Movies from System/, as the Xbox read D:\Movies and D:\Textures\Movies.
if [ -d "$run/Movies" ]; then
  rm -rf "$run/Movies/.cook"
elif [ -d "$cooked/Movies" ]; then
  cp -al "$cooked/Movies" "$run/Movies"
  rm -rf "$run/Movies/.cook"
else
  echo "note: no cooked Movies at $cooked (tools/cook-movies.py makes them); no movies will play" >&2
fi
if [ ! -d "$run/Textures/Movies" ] && [ -d "$cooked/Textures/Movies" ]; then
  mkdir -p "$run/Textures"
  cp -al "$cooked/Textures/Movies" "$run/Textures/Movies"
fi

# Our tracked configuration wins over the asset tree's (identical until we start changing defaults).
cp -f "$repo"/Engine/Config/*.ini "$run/System/"

# Script packages built from source over the asset tree's:
# - the build's own, <build-dir>/Scripts/System/*.u (the `scripts` target: the shipped .u decompiled, patches/
#   applied, compiled; only the packages the patch sets rebuild are there). Present unless the build was configured
#   with -DUC2_SCRIPT_PATCHES=OFF.
# - UC2_SCRIPTS=<dir> takes them from <dir> instead (e.g. tools/dev/script-build.sh's <repo>/../run/m14-make/System,
#   or <repo>/../assets/uc2-shipped/System); UC2_SCRIPTS=none: none at all (the asset tree's .u, a "shipped" tree).
scripts_dir=${UC2_SCRIPTS:-}
scripts_from="UC2_SCRIPTS"
if [ -z "$scripts_dir" ] && [ -d "$build/Scripts/System" ]; then
  scripts_dir=$build/Scripts/System
  scripts_from="the build"
fi
if [ -n "$scripts_dir" ] && [ "$scripts_dir" != none ]; then
  n=0 names=""
  for u in "$scripts_dir"/*.u; do
    [ -f "$u" ] || continue
    [ -f "$run/System/$(basename "$u")" ] || continue   # only packages the asset tree ships
    cp -f "$u" "$run/System/"
    n=$((n+1)); names="$names $(basename "$u")"
  done
  echo "script packages from $scripts_from ($scripts_dir): $n:$names" >&2
else
  echo "script packages: the asset tree's ($assets/System)" >&2
fi

copy_bins() {
  for bin in "$1"/Source/UCC/ucc-bin "$1"/Source/SDLLaunch/uc2*; do
    [ -f "$bin" ] && [ -x "$bin" ] && cp -f "$bin" "$run/System/"
  done
  # The OpenAL Soft the build fetched; UC2ALAudio loads it from beside uc2.bin (dlopen).
  if [ -f "$1"/_deps/openalsoft-build/libopenal.so.1 ]; then
    cp -fL "$1"/_deps/openalsoft-build/libopenal.so.1 "$run/System/libopenal.so.1"
  fi
  # A Windows (MinGW) build: ucc.exe, uc2.exe and the OpenAL32.dll the build compiled (LoadLibrary'd
  # from beside the exe). Run under wine from System/ (tools/dev/m12-wine-smoke.sh).
  for bin in "$1"/Source/UCC/ucc.exe "$1"/Source/SDLLaunch/uc2.exe "$1"/_deps/openalsoft-build/OpenAL32.dll; do
    [ -f "$bin" ] && cp -f "$bin" "$run/System/"
  done
  # An Emscripten build: ucc-bin.js/.wasm run under Node from System/ (the browser build's SDLLaunch.* is
  # served from the build directory, not from here).
  for bin in "$1"/Source/UCC/ucc-bin.js "$1"/Source/UCC/ucc-bin.wasm; do
    [ -f "$bin" ] && cp -f "$bin" "$run/System/"
  done
  return 0
}
copy_bins "$build"
shift $(( $# < 2 ? $# : 2 ))
for more in "$@"; do copy_bins "$(cd "$more" && pwd)"; done
mkdir -p "$run/Save" "$run/Cache"
echo "$run: $(cd "$run/System" && ls -1 ucc-bin* uc2* ucc.exe OpenAL32.dll 2>/dev/null | tr '\n' ' ')"
