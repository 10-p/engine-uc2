#!/usr/bin/env bash
# uc-patches-regen.sh: change a script patch set (patches/<set>/). The repository holds no UnrealScript,
# so a set is changed the way it is applied: on the build's decompiled base.
# This is the shorthand of the steps in patches/README.md, "Changing a set" (it runs exactly those commands and prints
# them).
#
#   tools/dev/uc-patches-regen.sh edit <set> [--build <dir>] [--work <dir>]
#       makes two trees from the build's base (<build>/Scripts/base, the shipped .u decompiled by the `scripts`
#       target): <work>/before = base + every set that comes before <set> in patches/sets.txt, and <work>/edit =
#       the same + <set>. Edit the classes under <work>/edit/<Pkg>/Classes (a new file there is a new class).
#   tools/dev/uc-patches-regen.sh make <set> [--build <dir>] [--work <dir>]
#       re-makes patches/<set>/ from before -> edit (uc-patches.py make; only the packages the base has: a set's
#       .diff of a package the build does not decompile, e.g. epic-fixes' Engine.diff, is kept as it is), then
#       checks that the default sets still apply (uc-patches.py check). Rebuild (cmake --build <build>) to compile.
#     --build  the host build directory (default build-native)
#     --work   scratch folder (default <build>/Scripts/edit-<set>; the build directory is scratch, never committed)
#
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
mode=${1:-}; set_name=${2:-}
[ "$mode" = edit ] || [ "$mode" = make ] || { sed -n '2,17p' "$0" >&2; exit 2; }
[ -n "$set_name" ] || { echo "uc-patches-regen: which set? (patches/sets.txt)" >&2; exit 2; }
shift 2
build=build-native W=""
while [ $# -gt 0 ]; do
  case "$1" in
    --build) build=$2; shift 2;;
    --work) W=$2; shift 2;;
    *) echo "unknown argument $1" >&2; exit 2;;
  esac
done
case "$build" in /*) ;; *) build=$repo/$build;; esac
base=$build/Scripts/base
W=${W:-$build/Scripts/edit-$set_name}
[ -d "$base" ] || { echo "uc-patches-regen: no $base: configure and build $build with the script patches first" >&2; exit 2; }
capped=$work/bin/capped
py() { if [ -x "$capped" ]; then "$capped" 2G 900 -- python3 "$@"; else python3 "$@"; fi; }
run() { echo "+ $*" | sed "s#$repo/##g"; "$@"; }
UP=$repo/tools/scripts/uc-patches.py

# The sets in apply order (sets.txt), the ones before <set>.
mapfile -t order < <(sed 's/#.*//' "$repo/patches/sets.txt" | awk 'NF==4 {print $1}')
before=() found=0
for s in "${order[@]}"; do
  [ "$s" = "$set_name" ] && { found=1; break; }
  before+=("$s")
done
[ $found = 1 ] || { echo "uc-patches-regen: no set $set_name in patches/sets.txt (${order[*]})" >&2; exit 2; }
blist=$(IFS=,; echo "${before[*]}")
alist=${blist:+$blist,}$set_name
pk=$(cd "$base" && ls -d */Classes 2>/dev/null | cut -d/ -f1 | paste -sd,)

case $mode in
  edit)
    rm -rf "$W"; mkdir -p "$W"
    if [ -n "$blist" ]; then run py "$UP" apply --base "$base" --sets "$blist" --out "$W/before"
    else echo "+ cp -a $base $W/before"; cp -a "$base" "$W/before"; fi
    run py "$UP" apply --base "$base" --sets "$alist" --out "$W/edit"
    echo "base packages: $pk (the set's files of other packages are kept as they are)"
    echo "now edit the classes under $W/edit/<Pkg>/Classes, then: $0 make $set_name${W:+ --work $W}"
    ;;
  make)
    [ -d "$W/before" ] && [ -d "$W/edit" ] || { echo "uc-patches-regen: no $W/before and $W/edit: run '$0 edit $set_name' first" >&2; exit 2; }
    run py "$UP" make "$set_name" --base "$W/before" --changed "$W/edit" --packages "$pk"
    run py "$UP" check --base "$base" --default
    echo "patches/$set_name re-made; review with git diff -- patches/$set_name, rebuild (cmake --build $build), and"
    echo "if the default sets' packages changed on purpose: tools/scripts/uc-patches-proof.sh p2 --write-reference"
    ;;
esac
