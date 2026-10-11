#!/usr/bin/env bash
# H-4 regression (owner report 2026-10-11: Lauren's polygons cross at a DM-Decay spot in the browser): Lauren at the
# owner's spot, a step back and the spot again (tools/dev/h4-vertex/spots.scen), at two window widths, on the GL4ES twin
# on this machine's GPU (UC2_GPU=1: the pawn's shadow pass runs, it does not under Mesa llvmpipe at this size), with the
# engine's LODTrace. One capped run per width through tools/dev/decay-flash.sh. Notebook docs/porting/analysis/h4-vertex.md.
#
#   tools/dev/h4-vertex.sh <tree> [widths, default "2882 2883"]
#
#   H4_BIN=uc2-x.bin   another executable of the tree's System folder (A/B: the unfixed engine FAILs)
#   H4_MESA=1          Xvfb + Mesa instead of the GPU (no shadow pass: the check then says so and FAILs)
#
# PASS per width: the shadow pass drew the pawn (draws of both passes in the trace) AND no traced draw is STALE (an
# index buffer of one LOD over vertices skinned for another; the defect) AND no Critical. The engine keeps the window
# width even (SDL2Viewport Align(NewX,2)): 2883 runs at 2884 here. Pictures and logs in <WorkUC2>/logs/<tree>/.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?tree}
widths=${2:-"2882 2883"}
bin=${H4_BIN:-uc2.bin}
fails=0
for w in $widths; do
  name=h4-${bin%.bin}-$w
  if [ -n "${H4_MESA:-}" ]; then gpu=""; else gpu=1; fi
  UC2_GPU=$gpu DF_BIN=$bin DF_RES=${w}x1212 DF_SECS=200 "$repo/tools/dev/decay-flash.sh" "$tree" "$name" "$repo/tools/dev/h4-vertex/spots.scen" \
    -gl4es -ini:System:[Engine.SkeletalMeshInstance]:LODTrace=True -ini:User:[DefaultPlayer]:Class=UCCharacters.UCPawn_Necris_Lauren \
    > "$work/logs/$tree/df-$name.summary" 2>&1
  head -1 "$work/logs/$tree/df-$name.summary"
  log=$work/logs/$tree/df-$name.out
  sum=$(python3 "$repo/tools/dev/h4-vertex/lodtrace.py" "$log" | tail -1)
  main=$(grep -a -c 'LODTrace: tick [0-9]* main ' "$log")
  shadow=$(grep -a -c 'LODTrace: tick [0-9]* shadow ' "$log")
  stale=$(grep -a -c 'LODTrace: tick .* STALE' "$log")
  crit=$(grep -a -c Critical "$log")
  shots=$(ls "$work/logs/$tree/df-$name"-*.png 2>/dev/null | wc -l)
  if [ "$main" -gt 0 ] && [ "$shadow" -gt 0 ] && [ "$stale" = 0 ] && [ "$crit" = 0 ] && [ "$shots" -ge 4 ]; then r=PASS; else r=FAIL; fails=$((fails+1)); fi
  echo "$r h4 $bin width $w: scene draws $main, shadow draws $shadow, STALE $stale, critical $crit, pictures $shots ($sum)"
done
echo "h4-vertex: $([ $fails = 0 ] && echo ALL PASS || echo "$fails FAILED")"
