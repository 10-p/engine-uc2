#!/usr/bin/env bash
# O-9: material census of maps (GLMATCENSUS, UC2GLPixelTrace.cpp): every material in memory compiled with
# 8 stages + three-operand ops (A), 4 stages + ops (B: Epic's D3DDrv, the Xbox), 4 stages without (C: NVIDIA
# before O-9). Nothing is drawn for it, so it runs on Mesa (Xvfb). Notebook: docs/porting/analysis/o9-nvidia.md.
#
#   tools/dev/o9-census.sh [map ...]          default: every DM-, CTF- and OD- map; 3 runs at a time
#
# Run trees <WorkUC2>/run/o9c0..2 (made from build-native when missing), logs <WorkUC2>/logs/o9c*/census-<map>.out,
# one line per map in <WorkUC2>/logs/o9/census-summary.txt. Every run: tools/dev/run-uc2-shot.sh (capped 2G).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
maps=${*:-$(cd "$work/assets/uc2/Maps" && ls DM-*.unr CTF-*.unr OD-*.unr | sed 's/\.unr$//')}
mkdir -p "$work/logs/o9"
for i in 0 1 2; do
  [ -d "$work/run/o9c$i/System" ] || "$work/bin/capped" 2G 600 -- "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" "o9c$i" > /dev/null
  cp -f "$repo/build-native/Source/SDLLaunch/uc2.bin" "$work/run/o9c$i/System/uc2.bin"
done
summary=$work/logs/o9/census-summary.txt
: > "$summary"
one() {  # one <slot> <map>
  local t=o9c$1 m=$2
  "$repo/tools/dev/run-uc2-shot.sh" $t census-$m 150 0 "$m" -glexec=45:GLMATCENSUS@55:exit > /dev/null 2>&1
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
