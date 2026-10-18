#!/usr/bin/env bash
# UI scaling verification: the menus and the HUD in a centred 4:3 box at every resolution, measured.
#
#   tools/dev/d2-ui-scale.sh                 run everything, then measure (one PASS/FAIL line per check)
#   tools/dev/d2-ui-scale.sh run [res...]    only the game runs (default: every resolution below)
#   tools/dev/d2-ui-scale.sh extras          only the reference/switch/mouse runs
#   tools/dev/d2-ui-scale.sh measure         only the measurements (on what the runs left)
#   D2_NO_REF=1                              extras without the reference runs (their pictures stay in <run>)
#   D2_OUT=<dir>                             pictures and summary there instead of <repo>/../logs/d2/run (e.g. a
#                                            run from another worktree)
#
# Trees (tools/dev/mkrun-uc2.sh): D2_TREE (default d2) has the build under test; D2_REF_TREE (default d2-ref)
# has main's binary (the regression anchor at 640x480 and the "before" pictures). Every game run is one
# tools/dev/m10-controls.sh --file run (Xvfb, software GL, -ini:System:[UC2GLDrv.UC2GLRenderDevice]:AntiAliasing=0 -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:AntiAliasing=0, under <repo>/../bin/capped: 2G, 3G from
# 2560x1440 up). Pictures and logs: <repo>/../logs/d2/run/<res>/ (<what>-<shot>.png), measurements in
# <repo>/../logs/d2/run/summary.txt, a contact sheet in <repo>/../logs/d2/run/contact-sheet.png.
# Scenarios: tools/dev/d2/menu.scen (main menu, arena page; Fill and Box; the level on and off) and
# tools/dev/d2/deadbolt.scen (the pinned DM-Deadbolt view, HUD with the level on and off).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${D2_TREE:-d2}
ref=${D2_REF_TREE:-d2-ref}
out=${D2_OUT:-$work/logs/d2/run}
ctl=$repo/tools/dev/m10-controls.sh
all_res="640x480 1280x720 1920x1080 2560x1440 3840x2160 1280x1024 1920x1200 1024x1280"
mkdir -p "$out"

mem_for() { local w=${1%x*}; [ "$w" -ge 2560 ] && echo 3G || echo 2G; }

# one <tree> <res> <name> <scenario> [url] [uc2.bin args...]
one() {
  local t=$1 res=$2 name=$3 scen=$4 url=${5-}; shift 5 || shift $#
  local dir=$out/$res
  mkdir -p "$dir"
  M10_TREE=$t M10_RES=$res M10_OUT=$dir M10_MEM=$(mem_for "$res") M10_URL=$url \
    "$ctl" --file "$repo/tools/dev/d2/$scen" "$name" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True "$@" 2>&1 | tail -1 | sed "s|^|[$res] |"
}

run_res() {
  local res=$1
  one "$tree" "$res" menu menu.scen ""
  one "$tree" "$res" hud deadbolt.scen "DM-Deadbolt?NumBots=0"
}

extras() {
  # D2_NO_REF=1: keep the reference pictures already in <run> (main's binary does not change between passes).
  if [ -z "${D2_NO_REF:-}" ]; then
  # The regression anchor at 640x480. D2_REF_TREE's uc2.bin is main's source plus only the measurement switch
  # (TOGGLERENDERLEVEL and its black clear: logs/d2/refplus.diff), so the UI can be compared on black; its
  # uc2-main.bin is main's binary unchanged (the pinned pictures). Two runs of the reference say how much two
  # runs of one binary differ.
  one "$ref" 640x480 ref-menu menu.scen ""
  one "$ref" 640x480 ref2-menu menu.scen ""
  one "$ref" 640x480 ref3-menu menu.scen ""
  one "$ref" 640x480 ref-hud deadbolt.scen "DM-Deadbolt?NumBots=0"
  one "$ref" 640x480 ref2-hud deadbolt.scen "DM-Deadbolt?NumBots=0"
  M10_LAUNCH=./uc2-main.bin M10_PROC=uc2-main.bin one "$ref" 640x480 main-hud deadbolt.scen "DM-Deadbolt?NumBots=0"
  # The pinned picture without post-processing (the static-part comparison of the Windows and 64-bit checks), main's binary and ours.
  M10_LAUNCH=./uc2-main.bin M10_PROC=uc2-main.bin one "$ref" 640x480 main-pinned deadbolt.scen "DM-Deadbolt?NumBots=0" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False
  # Before (main) at 1920x1080, for the sheet and the switch below.
  one "$ref" 1920x1080 ref-menu menu.scen ""
  one "$ref" 1920x1080 ref-hud deadbolt.scen "DM-Deadbolt?NumBots=0"
  fi
  one "$tree" 640x480 pinned deadbolt.scen "DM-Deadbolt?NumBots=0" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False
  # The switch: UIScaleMode=Native640 in Default.ini gives today's behaviour back.
  M10_INI="UIScaleMode=Native640" one "$tree" 1920x1080 native-menu menu.scen ""
  # A marker on an actor (the lock-on reticle, placed by WorldToScreen): boxed and unboxed, same window pixels.
  one "$tree" 1920x1080 lockon lockon.scen "DM-Deadbolt?NumBots=0"
  # The scoreboard and the in-game pause menu (a GUI page over the world), for the sheet.
  one "$tree" 1920x1080 pause pause.scen "DM-Deadbolt?NumBots=0"
  # The mouse scenario of m10-controls.sh at 640x480 and 1920x1080 (its 640x480 positions mapped into the box).
  for res in 640x480 1920x1080; do
    # on the big window: keys held longer and every pause 3x as long (m10-inject.py M10_KEYHOLD, M10_SLOW): its
    # frames in software GL are slow enough to swallow a short press or merge two wheel clicks
    hold=0.12 slow=1; [ "$res" = 640x480 ] || { hold=0.5; slow=3; }
    M10_KEYHOLD=$hold M10_SLOW=$slow M10_TREE=$tree M10_RES=$res M10_OUT=$out/$res M10_MEM=2G "$ctl" mouse 2>&1 | tail -1 | sed "s|^|[$res] |"
  done
}

measure() {
  python3 "$repo/tools/dev/d2-measure.py" "$out" $all_res | tee "$out/summary.txt"
}

case "${1-}" in
  run) shift; for r in ${@:-$all_res}; do run_res "$r"; done ;;
  extras) extras ;;
  measure) measure ;;
  "") for r in $all_res; do run_res "$r"; done; extras; measure ;;
  *) echo "usage: $0 [run [res...]|extras|measure]" >&2; exit 2 ;;
esac
