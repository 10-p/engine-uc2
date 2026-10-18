#!/usr/bin/env bash
# Material census of maps (GLMATCENSUS, UC2GLPixelTrace.cpp): every material in memory compiled with
# 8 stages + three-operand ops (A), 4 stages + ops (B: Epic's D3DDrv, the Xbox), 4 stages without (C: a texture
# environment without combine3/combine4). Nothing is drawn for it, so it runs on Mesa (Xvfb).
#
#   tools/dev/o9-census.sh [map ...]          default: every DM-, CTF- and OD- map; 3 runs at a time
#
# Run trees <repo>/../run/o9c0..2 (made from build-native when missing), logs <repo>/../logs/o9c*/census-<map>.out,
# one line per map in <repo>/../logs/o9/census-summary.txt. Every run: tools/dev/run-uc2-shot.sh (capped 2G).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
maps=${*:-$(cd "${UC2_ASSETS_DIR:-$work/assets/uc2-assets}/Maps" && ls DM-*.unr CTF-*.unr OD-*.unr | sed 's/\.unr$//')}
# O9_CENSUS_PREFIX (default o9c): the three run trees <prefix>0..2; O9_CENSUS_LOG (default <repo>/../logs/o9): the summary.
pre=${O9_CENSUS_PREFIX:-o9c}; logdir=${O9_CENSUS_LOG:-$work/logs/o9}
mkdir -p "$logdir"
for i in 0 1 2; do
  [ -d "$work/run/$pre$i/System" ] || "$work/bin/capped" 2G 600 -- "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" "$pre$i" > /dev/null
  cp -f "$repo/build-native/Source/SDLLaunch/uc2.bin" "$work/run/$pre$i/System/uc2.bin"
done
summary=$logdir/census-summary.txt
: > "$summary"
one() {  # one <slot> <map>
  local t=$pre$1 m=$2
  "$repo/tools/dev/run-uc2-shot.sh" $t census-$m 150 0 "$m" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=45:GLMATCENSUS@55:exit > /dev/null 2>&1
  local line; line=$(grep -a "matcensus summary" "$work/logs/$t/census-$m.out" | sed 's/.*matcensus summary: //')
  echo "$m: ${line:-NO CENSUS (see $work/logs/$t/census-$m.out)}" >> "$summary"
}
set -- $maps
while [ $# -gt 0 ]; do
  pids=""
  for slot in 0 1 2; do
    [ $# -gt 0 ] || break
    one $slot "$1" & pids="$pids $!"; shift
  done
  wait $pids
done
sort -o "$summary" "$summary"
cat "$summary"
