#!/usr/bin/env bash
# Pictures for the owner's review items O-5, O-6, O-7 (DM-Azure pool, DM-Azure waterfall, DM-Praxis pane
# distortion range), regenerated deterministically: fixed camera (NETPLACE with pitch), fixed times, the
# virtual pad for movement. Notebook: docs/porting/analysis/fx-fixes.md.
#
#   tools/dev/fx-owner.sh [<tree>] [case ...]       default tree: fx; default: every case
#   tools/dev/fx-owner.sh fx pool waterfall-1p
#   tools/dev/fx-owner.sh list
#   UC2_BIN=uc2-before.bin tools/dev/fx-owner.sh fx pool   another executable from the tree's System folder
#                                                          (the pictures get the suffix "-before")
#   FX_ARGS="-ini:System:[UC2GLDrv.UC2GLRenderDevice]:MatCensus=True" tools/dev/fx-owner.sh ...       more uc2.bin arguments (O-9)
#   FX_SECS=240 (custom case only): another time limit for the run (M7b: the GL4ES twin under load)
#   UC2_GPU=1 tools/dev/fx-owner.sh fx waterfall-1p        render on this machine's GPU instead of Mesa
#                                                          llvmpipe: SDL's offscreen video driver (EGL, no
#                                                          window, nothing on the owner's display); the
#                                                          pictures get the suffix "-gpu"
#
# Each case is one game run of tools/dev/m10-inject.py's scenario language (wait/sleep/chan/pad/shot) inside
# <WorkUC2>/bin/capped 2G, headless (Xvfb, software GL), 1280x720, map URL <map>?Name=FxCam, -ini:System:[UC2Movie]:StartupMovies=False,
# the engine's input test channel (-ini:System:[SDL2Drv.SDL2Client]:InputTestFile=, SDL2Drv) for console commands and the virtual pad. Output in
# <WorkUC2>/logs/<tree>/: fx-<case>[-before]-<shot>.png, fx-<case>[-before].out (game log), .inject (runner),
# and one line per case: Critical count, GL errors, failed programs, the shots taken, anything left running.
#
# Console commands used (all exist without cheats): SetAutoCenterPitch 0 (UCPlayerController), ToggleThirdCam,
# NETPLACE <name> x y z yaw [pitch] (UC2NetTest.cpp; places the pawn and the view), GLPIXTRACE x y (driver),
# DISTORTIONCULLDISTANCE (driver, O-7), set ... (property of every object of a class).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=fx
if [ $# -gt 0 ] && [ -d "$work/run/$1" ]; then tree=$1; shift; fi
sys=$work/run/$tree/System
out=$work/logs/$tree
bin=${UC2_BIN:-uc2.bin}
# AutoFOV off unless UC2_AUTOFOV is set: the before/after pairs compare with executables older than D-8.
fovarg="-ini:System:[ufront.AutoFOV]:AutoFOV=0"; [ -n "${UC2_AUTOFOV:-}" ] && fovarg=""
# Likewise the UI as before D-2 (640x480 drawn 1:1, top-left) unless UC2_UIBOX is set: the references (e.g. the
# wine oracle's DM-Praxis view, made by the original PC binary) have the old HUD. tools/dev/d2-ui-scale.sh
# measures the UI box itself and does not use this runner.
[ -z "${UC2_UIBOX:-}" ] && fovarg="$fovarg -ini:System:[SDL2Drv.SDL2Client]:UIScaleMode=Native640"
suffix=""; [ "$bin" != uc2.bin ] && suffix="-${bin%.bin}"; suffix=${suffix/-uc2-/-}
video=""; [ -n "${UC2_GPU:-}" ] && { video="SDL_VIDEODRIVER=offscreen"; suffix="$suffix-gpu"; }
inject=$repo/tools/dev/m10-inject.py
mkdir -p "$out"

# The common start: level up, match started with Fire (left button, as run-uc2-shot.sh), invulnerable, view
# pitch held.
start() {  # start <map>
  echo "wait Bringing Level $1.MyLevel up for play 150"
  echo "sleep 6"
  if [ -n "${UC2_GPU:-}" ]; then
    # No game window on the X display: Fire is the virtual pad's right trigger.
    printf 'pad attach\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\nsleep 3\n'
  else
    printf 'focus\nmove 640 360\nclick 1\nsleep 1\nclick 1\nsleep 1\nclick 1\nsleep 3\n'
  fi
  cat <<SCEN
chan exec God
chan exec SetAutoCenterPitch 0
sleep 1
SCEN
}
place() {  # place x y z yaw pitch: twice, the second once the pawn stands (the pitch holds only then)
  printf 'chan exec NETPLACE FxCam %s %s %s %s %s\nsleep 3\nchan exec NETPLACE FxCam %s %s %s %s %s\nsleep 2\n' "$@" "$@"
}

# case | map | seconds | what the shots show
CASES='
pool|DM-Azure|150|O-5: the centre pool from the near rim, third person, looking down at it (the camera of the owner'"'"'s AzureWaterDistortion.png, approximately); shot "on", then "nowater" (FluidSurfaceInfo hidden: the pool floor alone)
pool-splash|DM-Azure|150|O-5: the pawn dropped into the pool; shots every 0.25 s from the splash on
waterfall-1p|DM-Azure|170|O-6: first person in front of the cobra waterfall: still (two shots), strafing with the pad (two shots), still again
waterfall-3p|DM-Azure|150|O-6: the same spot in third person: still, then the view turned so the waterfall is between camera and pawn
praxis-near|DM-Praxis|150|O-7: a large window pane from close (owner'"'"'s PraxisWindowPanesClose.png), time frozen: "xbox" with DistortionCullDistance 5000, "raised" with 15000 (no visible change expected this close)
praxis-far|DM-Praxis|150|O-7: the panes of the round room from the ledge at the first PlayerStart, about 2650 units away (owner'"'"'s PraxisWindowPanesDistance.png), time frozen: "xbox" (5000), "raised" (15000), "nofade" (1000000), and "off" (no distortion at all: all PSDistortionMaterial amounts 0)
custom|FXMAP|150|an ad-hoc scenario: FX_SCENARIO=<file> (lines after the common start), FX_MAP=<map>, FX_NAME=<name>
'

scenario() {  # scenario <case>
  case $1 in
  pool)
    start DM-Azure
    place 8320 3480 -1218 16384 -5000
    echo 'shot on'
    echo 'chan exec set FluidSurfaceInfo bHidden True'
    echo 'sleep 1'
    echo 'shot nowater'
    ;;
  pool-splash)
    start DM-Azure
    place 8320 3480 -1218 16384 -5000
    echo 'shot dry'
    echo 'chan exec NETPLACE FxCam 8320 3800 -1150 16384 -5000'
    for i in 0 1 2 3 4 5 6 7; do echo 'sleep 0.25'; echo "shot t$i"; done
    ;;
  waterfall-1p)
    start DM-Azure
    echo 'chan exec ToggleThirdCam'
    place 8200 4200 -1200 14000 1500
    echo 'shot still-a'; echo 'sleep 0.4'; echo 'shot still-b'
    echo 'pad attach'; echo 'sleep 1'
    echo 'pad axis leftx 14000'; echo 'sleep 0.5'
    echo 'shot move-a'; echo 'sleep 0.3'; echo 'shot move-b'
    echo 'pad axis leftx 0'; echo 'sleep 2'
    echo 'shot still-c'
    ;;
  waterfall-3p)
    start DM-Azure
    place 8200 4200 -1200 14000 1500
    echo 'shot still-a'; echo 'sleep 0.4'; echo 'shot still-b'
    place 8330 4330 -1200 -16384 -1500
    echo 'shot behind'
    ;;
  praxis-near)
    start DM-Praxis
    place 0 1850 450 16384 0
    echo 'chan exec set LevelInfo TimeDilation 0.0001'; echo 'sleep 1'
    echo 'chan exec DISTORTIONCULLDISTANCE 5000'; echo 'sleep 0.5'; echo 'shot xbox'
    echo 'chan exec DISTORTIONCULLDISTANCE 15000'; echo 'sleep 0.5'; echo 'shot raised'
    ;;
  praxis-far)
    start DM-Praxis
    place -1220 288 690 5220 -600
    echo 'chan exec set LevelInfo TimeDilation 0.0001'; echo 'sleep 1'
    echo 'chan exec DISTORTIONCULLDISTANCE 5000'; echo 'sleep 0.5'; echo 'chan exec GLPIXTRACE 360 380 all'; echo 'sleep 0.5'; echo 'shot xbox'
    echo 'chan exec DISTORTIONCULLDISTANCE 15000'; echo 'sleep 0.5'; echo 'chan exec GLPIXTRACE 360 380 all'; echo 'sleep 0.5'; echo 'shot raised'
    echo 'chan exec DISTORTIONCULLDISTANCE 1000000'; echo 'sleep 0.5'; echo 'chan exec GLPIXTRACE 360 380 all'; echo 'sleep 0.5'; echo 'shot nofade'
    echo 'chan exec set PSDistortionMaterial DistortionAmount 0'; echo 'sleep 0.5'; echo 'shot off'
    ;;
  custom)
    start "${FX_MAP:-DM-Azure}"
    cat "$FX_SCENARIO"
    ;;
  esac
}

if [ "${1:-}" = list ]; then
  echo "$CASES" | awk -F'|' 'NF>1 { printf "%-14s %-10s %s\n", $1, $2, $4 }'
  exit 0
fi
want=" $* "

echo "$CASES" | while IFS='|' read -r name map secs what; do
  [ -n "$name" ] || continue
  if [ "$want" != "  " ] && [ "${want#* $name }" = "$want" ]; then continue; fi
  [ "$map" = FXMAP ] && map=${FX_MAP:-DM-Azure}
  [ "$name" = custom ] && [ -n "${FX_NAME:-}" ] && name=$FX_NAME
  [ -n "${FX_SECS:-}" ] && [ "$map" = "${FX_MAP:-}" ] && secs=$FX_SECS
  tag=fx-$name$suffix
  scen=$(mktemp); { if [ -n "${FX_NAME:-}" ] && [ "$name" = "$FX_NAME" ]; then scenario custom; else scenario "$name"; fi; echo 'sleep 1.5'; } > "$scen"
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  sed -i -E "s/^(WindowedViewportX|FullscreenViewportX)=.*/\\1=1280/; s/^(WindowedViewportY|FullscreenViewportY)=.*/\\1=720/" "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$out/$tag"-*.png "$out/$tag".result*
  chan=$out/$tag.channel; : > "$chan"
  inner=$(mktemp)
  cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy $video ./$bin "$map?Name=FxCam" -log -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:KeyboardIsPlayer=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan" $fovarg ${FX_ARGS:-} > "$out/$tag.out" 2>&1 &
pid=\$!
python3 "$inject" "$scen" "$out/$tag.out" "$chan" "$out/$tag.result" > "$out/$tag.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
exit 0
INNER
  "$work/bin/capped" 2G "$secs" -- xvfb-run -a -s "-screen 0 1280x720x24" bash "$inner"
  rc=$?
  rm -f "$inner" "$scen"
  shots=""
  if [ -f "$out/$tag.result.shots" ]; then
    i=0
    while read -r shot; do
      bmp=$(printf "%s/Shot%05d.bmp" "$sys" "$i")
      [ -f "$bmp" ] && python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$bmp" "$out/$tag-$shot.png" && shots="$shots $shot"
      i=$((i+1))
    done < "$out/$tag.result.shots"
    rm -f "$sys"/Shot*.bmp
  fi
  crit=$(grep -a -c "Critical" "$out/$tag.out" 2>/dev/null)
  glerr=$(grep -a "GL errors so far" "$out/$tag.out" 2>/dev/null | tail -1 | sed -E 's/.*, ([0-9]+) GL errors.*/\1/')
  pfail=$(grep -a "UC2GLDrv: programs:" "$out/$tag.out" 2>/dev/null | tail -1 | sed -E 's/.*linked, ([0-9]+) failed; fragment shaders [0-9]+ compiled, ([0-9]+) failed.*/\1+\2/')
  left=0
  for p in $(pgrep -x "$bin") $(pgrep -x Xvfb); do
    case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"|"$sys"/*) left=$((left+1));; esac
  done
  echo "fx $name$suffix: critical=${crit:-?} glerrors=${glerr:-?} programs_failed=${pfail:-?} shots=[${shots# }] capped_exit=$rc left_behind=$left"
done
