#!/usr/bin/env bash
# Console and AutoFOV checks: one PASS/FAIL line per check (tools/dev/d78-measure.py).
#
#   tools/dev/d78.sh                 run everything, then measure
#   tools/dev/d78.sh run             only the game runs
#   tools/dev/d78.sh measure         only the measurements (on what the runs left)
#
# Trees (tools/dev/mkrun-uc2.sh): D78_TREE (default d78) has the build under test; D78_REF_TREE (default d78-ref)
# has main's binary (the 4:3 regression anchor). Every game run is one tools/dev/m10-controls.sh --file run
# (Xvfb, software GL, -ini:System:[UC2GLDrv.UC2GLRenderDevice]:NumAASamples=0 -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:NumAASamples=0, under <repo>/../bin/capped: 2G at 640x480/1024x768, 3G for the run that goes up
# to 2560x1080). Pictures and logs: <repo>/../logs/d78/run/<what>/, measurements in <repo>/../logs/d78/run/summary.txt.
#   ident/<res>/   the pinned DM-Deadbolt view (tools/dev/d2/deadbolt.scen), -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False, ours and main's,
#                  at 640x480 and 1024x768 (AutoFOV must change nothing at 4:3)
#   fov/           tools/dev/d78/fov.scen, -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False: one frozen game at 1440x1080, 1920x1080 and 2560x1080
#                  (third person, cinematic letterbox, first person with the level and on black, zoom 40,
#                  lock-on), AUTOFOV 0 for the before pictures
#   fov-pp/        the same with post-processing (pictures for review; not measured)
#   sniper/        tools/dev/d78/sniper.scen, -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False: the sniper rifle's zoom and scope at the three sizes
#   console/       tools/dev/d78/console-game.scen and console-menu.scen
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${D78_TREE:-d78}
ref=${D78_REF_TREE:-d78-ref}
out=${D78_OUT:-$work/logs/d78/run}   # D78_OUT: pictures and summary elsewhere (another worktree's rerun)
ctl=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"

# one <tree> <dir> <res> <name> <scenario> <url> [uc2.bin args...]
one() {
  local t=$1 dir=$2 res=$3 name=$4 scen=$5 url=$6; shift 6
  local mem=2G; [ "${res%x*}" -ge 1440 ] && mem=3G
  mkdir -p "$out/$dir"
  M10_TREE=$t M10_RES=$res M10_OUT=$out/$dir M10_MEM=$mem M10_URL=$url M10_SECS=${D78_SECS:-900} M10_XVFB=${D78_XVFB:-} \
    "$ctl" --file "$scen" "$name" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True "$@" 2>&1 | tail -1 | sed "s|^|[$dir] |"
}

runs() {
  for res in 640x480 1024x768; do
    one "$tree" "ident/$res" "$res" pinned "$repo/tools/dev/d2/deadbolt.scen" "DM-Deadbolt?NumBots=0" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False
    one "$ref" "ident/$res" "$res" main-pinned "$repo/tools/dev/d2/deadbolt.scen" "DM-Deadbolt?NumBots=0" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False
  done
  D78_XVFB=2560x1080 one "$tree" fov 1440x1080 fov "$repo/tools/dev/d78/fov.scen" "DM-Deadbolt?NumBots=0" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False
  D78_XVFB=2560x1080 one "$tree" sniper 1440x1080 fov "$repo/tools/dev/d78/sniper.scen" "DM-Deadbolt?NumBots=0" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False
  one "$tree" console 640x480 console-game "$repo/tools/dev/d78/console-game.scen" "DM-Deadbolt?NumBots=0"
  one "$tree" console 640x480 console-menu "$repo/tools/dev/d78/console-menu.scen" ""
  [ -n "${D78_NO_PP:-}" ] || D78_XVFB=2560x1080 one "$tree" fov-pp 1440x1080 fov "$repo/tools/dev/d78/fov.scen" "DM-Deadbolt?NumBots=0"
}

measure() {
  python3 "$repo/tools/dev/d78-measure.py" "$out" | tee "$out/summary.txt"
}

case "${1:-all}" in
  run) runs ;;
  measure) measure ;;
  all) runs; measure ;;
  *) echo "usage: $0 [run|measure]" >&2; exit 2 ;;
esac
