#!/usr/bin/env bash
# Bloom phase check: scale 1 against PostProcessTargetScale 2 over the 16 positions of the scale-1 bloom grid.
#
#   tools/dev/h1-phase.sh [<tree>] [<WxH>]        default tree h1, 2560x1440
#   H1_CHECK_ONLY=1 tools/dev/h1-phase.sh ...     measure an earlier run again
#   H1_SCALE=4 tools/dev/h1-phase.sh ...          scale 1 against 4 instead of 2 (pictures fx-phase4-...)
#
# At scale 1 the bloom select reads one 2x2 average per 4x4 reference pixels, so what a small or thin light
# contributes, and so its peak, depends on where it falls on that grid (2-4 levels on DM-Deadbolt's lamp and blue
# box); one view is one sample of that. tools/dev/h1/phase.scen moves the view by about one reference pixel at a
# time over the 4 x 4 grid positions (one GPU run, tools/dev/fx-owner.sh custom, bin/capped 2G; waits for a load
# below H1_MAXLOAD, default 14) and takes scale 1 and scale 2 (both with freshly made targets) and the effect off
# at each. Then tools/dev/h1-measure.py check over all 16: set A scale 1, set B scale 2 (H1_TOL_R 3 px of the 720
# frame, H1_TOL_PEAK 1 level). Output: <repo>/../logs/<tree>/fx-phase-<WxH>-gpu-q<n>{s1,s2,off}.png, the lines on
# stdout and in h1-phase-<WxH>.txt.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=h1
if [ $# -gt 0 ] && [ -d "$work/run/$1" ]; then tree=$1; shift; fi
res=${1:-2560x1440}
w=${res%x*}; h=${res#*x}
out=$work/logs/$tree
sc=${H1_SCALE:-2}
name=phase-$res; [ "$sc" != 2 ] && name=phase$sc-$res
scen=$repo/tools/dev/h1/phase.scen
if [ "$sc" != 2 ]; then scen=$(mktemp); sed "s/PostProcessTargetScale 2/PostProcessTargetScale $sc/" "$repo/tools/dev/h1/phase.scen" > "$scen"; fi
if [ -z "${H1_CHECK_ONLY:-}" ]; then
  while [ "$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)" -ge "${H1_MAXLOAD:-14}" ]; do sleep 15; done
  UC2_GPU=1 FX_MAP=DM-Deadbolt FX_NAME=$name FX_SCENARIO=$scen FX_SECS=360 \
    FX_ARGS="-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=$w -ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY=$h" \
    "$repo/tools/dev/fx-owner.sh" "$tree" custom | tail -1
fi
[ "$sc" != 2 ] && rm -f "$scen"
p=$out/fx-$name-gpu
a=(); b=()
for k in $(seq 0 15); do a+=("$p-q${k}s1.png" "$p-q${k}off.png"); b+=("$p-q${k}s2.png" "$p-q${k}off.png"); done
{ echo "== $res: scale 1 (A) against scale $sc (B), 16 positions on the scale-1 grid"
  python3 "$repo/tools/dev/h1-measure.py" check "${H1_TOL_R:-3}" "${H1_TOL_PEAK:-1}" "${a[@]}" -- "${b[@]}"; } | tee "$out/h1-$name.txt"
