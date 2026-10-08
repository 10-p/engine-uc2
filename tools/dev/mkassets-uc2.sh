#!/usr/bin/env bash
# Build the canonical UC2 asset tree (v0) from the pristine Epic tree.
#
#   tools/dev/mkassets-uc2.sh [<epic-sources>] [<assets/uc2>]
#
# v0 is exactly Epic's PC-format content (package version 151, licensee 0) plus the script packages, inis and
# localisation. Real copies, never symlinks (the engine's package lookup does not follow them) and never
# hard links (a tool writing in place would damage the pristine tree). Existing files are left alone, so the
# script can be re-run; it never deletes.
#
# Not copied, on purpose:
#   Sounds/ SoundData/ Music/ Movies/ Textures/Movies/   cook INPUTS (M8 audio, M9 video) read from epic-sources;
#                                                         their cooked OUTPUTS are added from assets/uc2-cooked below
#   Xbox*/ *.xpr *.uix *.lin *.xbe xbox.bin               Xbox-format data (M16)
#   MapsOnHold/ TestMaps/                                 cut and test content (M15 decides)
#   System/*.dll *.exe *.bat *.pl EditorRes/ OriginalScript/ LIPSincData/   Windows binaries, editor, duplicates
set -euo pipefail

here=$(cd "$(dirname "$0")/../.." && pwd)
src=${1:-$here/../epic-sources}
dst=${2:-$here/../assets/uc2}
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
# v1 (2026-10-06): the cooked sound and movies (tools/cook-audio.py, tools/cook-movies.py write them to
# assets/uc2-cooked) are part of the canonical tree, as PLAN.md's layout always said, so the frontend's manifest
# and every run tree see them. Hard links: these are our own outputs (re-cooking replaces them in both places),
# not Epic's pristine files. The cook's by-products (Movies/.cook, reports) come along and are excluded from
# the web manifest by targets/uc2/target.json's manifestExclude.
cooked=${UC2_COOKED_DIR:-$here/../assets/uc2-cooked}
link_cooked() { # link_cooked <relative dir>
  [ -d "$cooked/$1" ] || { echo "note: no cooked $1 in $cooked (run the cook first)"; return 0; }
  mkdir -p "$dst/$(dirname "$1")"
  cp -al --update=none "$cooked/$1" "$dst/$(dirname "$1")/"
}
link_cooked Sounds
link_cooked Music
link_cooked Movies
link_cooked Textures/Movies
# v2 (2026-10-07): script packages rebuilt from source (M14, tools/dev/script-build.sh) replace the shipped ones
# where a change needs them; today only GUI.u (D-4: without a profile the player keeps the launcher's name;
# bUnlockCharacters/bShowAllMuts config again). The rebuilt files are kept in assets/uc2-scripts/System (copied
# there from a script-build tree by the coordinator; D-6 adds UCGameplay.u, a SERVER package: every peer must run it), the shipped ones are put aside in assets/uc2-shipped/System
# (never deleted), and SCRIPTS.txt in the tree says which packages are rebuilt. GUI is not a server package, so
# shipped and rebuilt peers still play together; Core.u (the frontend's fingerprint) stays the shipped file.
rebuilt_dir=${UC2_REBUILT_SCRIPTS:-$here/../assets/uc2-scripts/System}
# D-9 adds UCMenu.u (the KEYBOARD / MOUSE settings page; not a server package). A package missing from rebuilt_dir
# keeps the shipped file (the note below), so the list may run ahead of the copies.
rebuilt_pkgs=${UC2_REBUILT_PACKAGES-GUI UCGameplay UCMenu}
shipped_dir=$here/../assets/uc2-shipped/System
: > "$dst/System/SCRIPTS.txt"
for pkg in $rebuilt_pkgs; do
  if [ -f "$rebuilt_dir/$pkg.u" ]; then
    mkdir -p "$shipped_dir"
    cp --update=none --preserve=timestamps "$src/System/$pkg.u" "$shipped_dir/$pkg.u"
    cp -f --preserve=timestamps "$rebuilt_dir/$pkg.u" "$dst/System/$pkg.u"
    echo "$pkg.u rebuilt from source ($(sha256sum "$rebuilt_dir/$pkg.u" | cut -c1-16)); shipped copy in assets/uc2-shipped/System" >> "$dst/System/SCRIPTS.txt"
  else
    echo "note: no rebuilt $pkg.u in $rebuilt_dir: the shipped one stays"
  fi
done
chmod -R u+w "$dst"
( cd "$dst" && find . -type f ! -name MANIFEST.sha256 -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > MANIFEST.sha256 )
echo "assets tree: $dst"
echo "files: $(wc -l < "$dst/MANIFEST.sha256")   size: $(du -sh "$dst" | cut -f1)"
