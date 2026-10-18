#!/usr/bin/env bash
# Projector and terrain census of the maps (GLSCENECENSUS, UC2GLSceneCensus.cpp): every projector actor with
# its texture, blending, flags and the surfaces it lies on, every terrain with its layers and render passes.
# Nothing is compared here; it is the list of places where the Xbox-only paths would draw.
#
#   tools/dev/m7b-census.sh [map ...]               default: every map with a projector or a terrain; 3 runs at a time
#   M7B_CENSUS_ARGS="-ini:..." tools/dev/m7b-census.sh ...   more uc2.bin arguments (e.g. the terrain capability on)
#   M7B_CENSUS_TAG=t3 ...                            suffix of the output names (census-<map>-t3.out)
#
# Run trees <repo>/../run/m7b-c0..2 (made from build-native when missing; uc2.bin refreshed every time), logs
# <repo>/../logs/m7b-c*/census-<map>[-tag].out, all census lines of all maps in <repo>/../logs/m7b/census[-tag].txt.
# Every run: tools/dev/run-uc2-shot.sh (bin/capped 2G), no match started (the level's own actors only).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tag=${M7B_CENSUS_TAG:+-$M7B_CENSUS_TAG}
maps=${*:-ACT-AnubisCheats ACT-BarrensAmbush ACT-SelketCheats CTF-Aqueduct CTF-ChemicalDawn CTF-Downfall CTF-Gauntlet][ CTF-Judgment CTF-Monoxide CTF-Recoil CTF-SkyReach CTF-TotalControl CTF-Transistor DM-AcidRain DM-Apex DM-Ascension DM-AxonCompressor DM-Azure DM-ColdFusion DM-Deadbolt DM-Decay DM-DeepCore DM-Eternal DM-Evilhands DM-Fury DM-Legacy DM-Meridian DM-Nexus DM-Phoenix DM-Praxis DM-Remnant DM-SolarFlare DM-Sovereign DM-Tempest DM-Undertow DM-Whisper OD-Ascension OD-Azure OD-Cancer OD-Echo OD-Falls OD-Obelisk OD-Overflow OD-Prism OD-Sovereign TUT-AdvMovement TUT-Combat}
# M7B_CENSUS_PREFIX (default m7b-c): the three run trees <prefix>0..2; M7B_CENSUS_LOG (default <repo>/../logs/m7b): the summary.
pre=${M7B_CENSUS_PREFIX:-m7b-c}; logdir=${M7B_CENSUS_LOG:-$work/logs/m7b}
mkdir -p "$logdir"
for i in 0 1 2; do
  [ -d "$work/run/$pre$i/System" ] || "$work/bin/capped" 2G 600 -- "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" "$pre$i" > /dev/null
  cp -f "$repo/build-native/Source/SDLLaunch/uc2.bin" "$work/run/$pre$i/System/uc2.bin"
done
all=$logdir/census$tag.txt
: > "$all"
one() {  # one <slot> <map>
  local t=$pre$1 m=$2
  "$repo/tools/dev/run-uc2-shot.sh" $t "census-$m$tag" 150 0 "$m" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=40:GLSCENECENSUS@46:exit ${M7B_CENSUS_ARGS:-} > /dev/null 2>&1
  grep -a "UC2GLDrv: scene " "$work/logs/$t/census-$m$tag.out" | sed "s/^.*UC2GLDrv: scene /$m: /" > "$work/logs/$t/census-$m$tag.lines"
  [ -s "$work/logs/$t/census-$m$tag.lines" ] || echo "$m: NO CENSUS (see $work/logs/$t/census-$m$tag.out)" > "$work/logs/$t/census-$m$tag.lines"
}
set -- $maps
while [ $# -gt 0 ]; do
  pids=""
  slots=""
  for slot in 0 1 2; do
    [ $# -gt 0 ] || break
    one $slot "$1" & pids="$pids $!"; slots="$slots $slot:$1"; shift
  done
  wait $pids
  for s in $slots; do cat "$work/logs/$pre${s%%:*}/census-${s#*:}$tag.lines" >> "$all"; done
done
grep -a "summary" "$all"
