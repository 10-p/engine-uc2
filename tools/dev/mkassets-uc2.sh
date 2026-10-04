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
#   Sounds/ SoundData/ Music/ Movies/ Textures/Movies/   cook inputs (M8 audio, M9 video) read from epic-sources
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
chmod -R u+w "$dst"
( cd "$dst" && find . -type f ! -name MANIFEST.sha256 -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > MANIFEST.sha256 )
echo "assets tree: $dst"
echo "files: $(wc -l < "$dst/MANIFEST.sha256")   size: $(du -sh "$dst" | cut -f1)"
