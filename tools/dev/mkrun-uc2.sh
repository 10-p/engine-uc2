#!/usr/bin/env bash
# Assemble a native run tree from a build directory.
#
#   tools/dev/mkrun-uc2.sh <build-dir> [<tree-name> [<more-build-dirs>...]]
#   tools/dev/mkrun-uc2.sh build-native            ->  <WorkUC2>/run/native
#   tools/dev/mkrun-uc2.sh build-native m4w build-wasm   ->  native and web binaries in one tree
#
# The tree is hard links into the canonical asset tree for the content folders (they are never written in
# place; symlinks would break the engine's package lookup) and a real copy of System/, because the engine
# derives and rewrites its inis and writes its logs there. The build's executables go into System/.
# Re-running replaces the tree: it only ever removes <WorkUC2>/run/<tree-name>, which it created itself.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
build=$(cd "${1:?usage: mkrun-uc2.sh <build-dir> [<tree-name>]}" && pwd)
name=${2:-$(basename "$build" | sed 's/^build-//')}
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

# M8: the cooked audio (tools/cook-audio.py: Sounds/ and Music/), hard-linked like the content folders. The
# audio device reads ../Sounds and ../Music from System/, as the Xbox read ..\Sounds. A missing cooked tree
# only means a silent game.
cooked=${UC2_COOKED_DIR:-$work/assets/uc2-cooked}
for d in Sounds Music; do
  if [ -d "$cooked/$d" ]; then
    cp -al "$cooked/$d" "$run/$d"
  else
    echo "note: no cooked $d at $cooked (tools/cook-audio.py makes it); the game runs silent" >&2
  fi
done

# Our tracked configuration wins over the asset tree's (identical until we start changing defaults).
cp -f "$repo"/Engine/Config/*.ini "$run/System/"

copy_bins() {
  for bin in "$1"/Source/UCC/ucc-bin "$1"/Source/SDLLaunch/uc2*; do
    [ -f "$bin" ] && [ -x "$bin" ] && cp -f "$bin" "$run/System/"
  done
  # M8: the OpenAL Soft the build fetched; UC2ALAudio loads it from beside uc2.bin (dlopen).
  if [ -f "$1"/_deps/openalsoft-build/libopenal.so.1 ]; then
    cp -fL "$1"/_deps/openalsoft-build/libopenal.so.1 "$run/System/libopenal.so.1"
  fi
  # M12: a Windows (MinGW) build: ucc.exe, uc2.exe and the OpenAL32.dll the build compiled (LoadLibrary'd
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
