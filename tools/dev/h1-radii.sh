#!/usr/bin/env bash
# Bloom radius check: does PostProcessTargetScale 2 (or 4) keep the bloom and the emissive glow the size they are at 1?
#
#   tools/dev/h1-radii.sh [<tree>] [<WxH> ...]       default tree h1; default 2560x1440 1280x720
#   H1_REPS=3 H1_SCALES="1 2" tools/dev/h1-radii.sh h1 2560x1440
#   H1_CHECK_ONLY=1 tools/dev/h1-radii.sh ...       measure the pictures of an earlier run again, run nothing
#
# Per resolution and scale, H1_REPS (default 3) game runs of tools/dev/h1/radii.scen on the GPU (UC2_GPU=1, through
# tools/dev/fx-owner.sh's custom case: bin/capped 2G; one run at a time; before each run it
# waits until the machine's load is below H1_MAXLOAD, default 14), DM-Deadbolt, the white lamp and the blue emissive
# box in view, time frozen, depth of field off, the scale from the start of the run
# (-ini:System:[UC2GLDrv.UC2GLRenderDevice]:PostProcessTargetScale=<n>), so that both scales have the same history
# (the emissive accumulation is history-dependent: two scale-1 shots of the same frame differ by up to 3 px in r10).
# Then tools/dev/h1-measure.py check with tolerances H1_TOL_R (3 px of the 720 frame) and H1_TOL_PEAK (1 level):
# set A = the scale-1 runs' shots "on" and "on2", set B = each other scale's. Pictures and logs in
# <repo>/../logs/<tree>/fx-radii-s<scale>-r<rep>-<WxH>-gpu-*.png; the check lines on stdout, also in
# <repo>/../logs/<tree>/h1-radii-<WxH>.txt.
#
# The scenario also takes, in each run, "fresh2" / "fresh1" (the scale switched while running with
# `set UC2GLRenderDevice PostProcessTargetScale <n>`: both with freshly made targets) for a second, in-run comparison.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=h1
if [ $# -gt 0 ] && [ -d "$work/run/$1" ]; then tree=$1; shift; fi
out=$work/logs/$tree
ress=${*:-2560x1440 1280x720}
reps=${H1_REPS:-3}
scales=${H1_SCALES:-1 2}
maxload=${H1_MAXLOAD:-14}
measure=$repo/tools/dev/h1-measure.py

for res in $ress; do
  w=${res%x*}; h=${res#*x}
  if [ -z "${H1_CHECK_ONLY:-}" ]; then
    for s in $scales; do
      for r in $(seq "$reps"); do
        while [ "$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)" -ge "$maxload" ]; do sleep 15; done
        UC2_GPU=1 FX_MAP=DM-Deadbolt FX_NAME=radii-s$s-r$r-$res FX_SCENARIO=$repo/tools/dev/h1/radii.scen FX_SECS=200 \
          FX_ARGS="-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=$w -ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY=$h -ini:System:[UC2GLDrv.UC2GLRenderDevice]:PostProcessTargetScale=$s" \
          "$repo/tools/dev/fx-owner.sh" "$tree" custom | tail -1
      done
    done
  fi
  {
    for s in $scales; do
      [ "$s" = 1 ] && continue
      a=(); b=()
      for r in $(seq "$reps"); do
        for shot in on on2; do
          a+=("$out/fx-radii-s1-r$r-$res-gpu-$shot.png" "$out/fx-radii-s1-r$r-$res-gpu-off.png")
          b+=("$out/fx-radii-s$s-r$r-$res-gpu-$shot.png" "$out/fx-radii-s$s-r$r-$res-gpu-off.png")
        done
      done
      echo "== $res: scale 1 (A) against scale $s (B), each scale from the start of its runs"
      python3 "$measure" check "${H1_TOL_R:-3}" "${H1_TOL_PEAK:-1}" "${a[@]}" -- "${b[@]}"
      a=(); b=()
      for r in $(seq "$reps"); do
        a+=("$out/fx-radii-s1-r$r-$res-gpu-fresh1.png" "$out/fx-radii-s1-r$r-$res-gpu-off.png")
        b+=("$out/fx-radii-s1-r$r-$res-gpu-fresh2.png" "$out/fx-radii-s1-r$r-$res-gpu-off.png")
      done
      [ "$s" = 2 ] && { echo "== $res: in-run, freshly made targets: scale 1 (A) against scale 2 (B)"; python3 "$measure" check "${H1_TOL_R:-3}" "${H1_TOL_PEAK:-1}" "${a[@]}" -- "${b[@]}"; }
    done
  } | tee "$out/h1-radii-$res.txt"
done
