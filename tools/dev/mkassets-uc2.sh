#!/usr/bin/env bash
# Build the canonical UC2 asset tree from the pristine Epic tree.
#
#   tools/dev/mkassets-uc2.sh <epic-sources> <asset tree>      e.g. ../epic-sources/uc2-avaris-2226 <dir>
#
# The tree is Epic's PC-format content (package version 151, licensee 0) plus the script packages, inis and
# localisation. Real copies, never symlinks (the engine's package lookup does not follow them) and never
# hard links (a tool writing in place would damage the pristine tree). Existing files are left alone, so the
# script can be re-run; it never deletes.
#
# Not copied, on purpose:
#   Sounds/ SoundData/ Music/ Movies/ Textures/Movies/   cook inputs (audio, video) read from epic-sources;
#                                                         their cooked outputs are added from UC2_COOKED_DIR below
#   Xbox*/ *.xpr *.uix *.lin *.xbe xbox.bin               Xbox-format data
#   MapsOnHold/ TestMaps/                                 cut and test content
#   System/*.dll *.exe *.bat *.pl EditorRes/ OriginalScript/ LIPSincData/   Windows binaries, editor, duplicates
set -euo pipefail

here=$(cd "$(dirname "$0")/../.." && pwd)
src=${1:?usage: mkassets-uc2.sh <epic-sources> <asset tree> (UC2_COOKED_DIR=<dir>: the cooked Sounds, Music and Movies)}
dst=${2:?usage: mkassets-uc2.sh <epic-sources> <asset tree>}
src=$(cd "$src" && pwd)
mkdir -p "$dst"
dst=$(cd "$dst" && pwd)

copy() { # copy <relative dir> <glob>...   (non-recursive)
  local dir=$1; shift
  mkdir -p "$dst/$dir"
  local g f
  for g in "$@"; do
    for f in "$src/$dir"/$g; do
      [ -f "$f" ] || continue
      cp --update=none --preserve=timestamps "$f" "$dst/$dir/"
    done
  done
}

copy Maps '*.unr'
copy Maps/ComboMatinees '*.unr'
copy Textures '*.utx'
copy StaticMeshes '*.usx'
copy Animations '*.ukx'
copy System '*.u' '*.ini' 'Packages.md5'
copy System/Ladders '*'
for lang in "$src"/System/Localization/*/; do
  copy "System/Localization/$(basename "$lang")" '*'
done
# The cooked sound and movies (tools/cook-audio.py, tools/cook-movies.py --out <dir>; UC2_COOKED_DIR=<dir> here) are part
# of the canonical tree, so the frontend's manifest and every run tree see them. Hard links: these are the cook's
# outputs (re-cooking replaces them in both places),
# not Epic's pristine files. The cook's by-products (Movies/.cook, reports) come along and are excluded from
# the web manifest by targets/uc2/target.json's manifestExclude.
cooked=${UC2_COOKED_DIR:-}
link_cooked() { # link_cooked <relative dir>
  [ -n "$cooked" ] || { echo "note: no UC2_COOKED_DIR: no cooked $1"; return 0; }
  [ -d "$cooked/$1" ] || { echo "note: no cooked $1 in $cooked (run the cook first)"; return 0; }
  mkdir -p "$dst/$(dirname "$1")"
  cp -al --update=none "$cooked/$1" "$dst/$(dirname "$1")/"
}
link_cooked Sounds
link_cooked Music
link_cooked Movies
link_cooked Textures/Movies
# The canonical tree holds the 17 shipped script packages and nothing else; the patched packages are built by the
# engine (build-<preset>/Scripts/System, patches/README.md) and reach a run tree through tools/dev/mkrun-uc2.sh and
# the web through the artifact's scripts/ overlay. Rebuilt copies from UC2_REBUILT_SCRIPTS=<System dir> (+ SCRIPTS.txt)
# are added only with an explicit UC2_REBUILT_PACKAGES="GUI ..." for an experiment, never by default.
rebuilt_dir=${UC2_REBUILT_SCRIPTS:-}
rebuilt_pkgs=${UC2_REBUILT_PACKAGES-}
rm -f "$dst/System/SCRIPTS.txt"
for pkg in $rebuilt_pkgs; do
  if [ -f "$rebuilt_dir/$pkg.u" ]; then
    cp -f --preserve=timestamps "$rebuilt_dir/$pkg.u" "$dst/System/$pkg.u"
    echo "$pkg.u rebuilt from source ($(sha256sum "$rebuilt_dir/$pkg.u" | cut -c1-16)): an EXPERIMENT (UC2_REBUILT_PACKAGES); the shipped file is in the Epic drop's System/" >> "$dst/System/SCRIPTS.txt"
  else
    echo "note: no rebuilt $pkg.u in $rebuilt_dir: the shipped one stays"
  fi
done
chmod -R u+w "$dst"
( cd "$dst" && find . -type f ! -name MANIFEST.sha256 -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > MANIFEST.sha256 )
echo "assets tree: $dst"
echo "files: $(wc -l < "$dst/MANIFEST.sha256")   size: $(du -sh "$dst" | cut -f1)"
