#!/usr/bin/env bash
# Frame-rate benchmark: the game on this machine's GPU, headless, a fixed
# camera path, the engine's own FPS log line ([Engine.GameEngine] FpsLogSeconds / FPSLOG, Source/Engine/Src/UC2FpsLog.cpp).
# Prints one summary line.
#
#   tools/dev/fps-bench.sh <tree> <map> <seconds> [WxH] [engine arguments...]
#   tools/dev/fps-bench.sh f4 DM-Praxis 20 1920x1080
#     <tree>     run tree under <repo>/../run (tools/dev/mkrun-uc2.sh)
#     <seconds>  length of the measured window (a multiple of 4 gives whole turns)
#     WxH        window size, default 1280x720 (set with -ini:, as every other setting)
#   FPS_PLACE="x y z yaw pitch"   where the camera stands (default: a fixed spot for DM-Praxis and DM-Deadbolt, else
#                                 the spawn point, which is random)
#   FPS_TURN=0                    no turn: the view stays where FPS_PLACE put it (a fixed view, no teleports)
#   FPS_SOFT=1                    Mesa llvmpipe under Xvfb instead of the GPU
#   FPS_OUT=<dir>                 output (default <repo>/../logs/fps): <name>.out (game log), .scen, .inject
#   FPS_NAME=<name>               output name (default <tree>-<map>-<WxH>)
#
# The path: the level up, the match started with the virtual pad's trigger, God, no bots, the pawn and the view put at
# FPS_PLACE (NETPLACE), then `FPSLOG <seconds>` and a turn on the spot: NETPLACE with the yaw advanced by 4096 every
# 0.25 s (a full turn every 4 s, the same at any frame rate). The measured window is the first FPS line.
# GPU: SDL's offscreen video driver (EGL, no window; as tools/dev/fx-owner.sh UC2_GPU=1). Vsync is whatever the ini says
# ([UC2GLDrv.UC2GLRenderDevice] UseVSync, False by default); nothing caps the frame rate of a standalone game.
# Runs inside <repo>/../bin/capped 2G. Do not judge timings while the machine is loaded: the line prints the load.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?tree}; map=${2:?map}; secs=${3:?seconds}; shift 3
res=1280x720
if [ $# -gt 0 ] && [[ $1 =~ ^[0-9]+x[0-9]+$ ]]; then res=$1; shift; fi
w=${res%x*}; h=${res#*x}
sys=$work/run/$tree/System
out=${FPS_OUT:-$work/logs/fps}
name=${FPS_NAME:-$tree-$map-$res}
[ -x "$sys/uc2.bin" ] || { echo "no run tree $work/run/$tree (tools/dev/mkrun-uc2.sh)" >&2; exit 2; }
mkdir -p "$out"
case $map in
  DM-Praxis)  defplace="-1220 288 690 5220 -600";;     # the ledge at the first PlayerStart, facing the round room
  DM-Deadbolt) defplace="1977.8 583.7 -34.1 33000 -1400";;
  *) defplace="";;
esac
place=${FPS_PLACE:-$defplace}
video="SDL_VIDEODRIVER=offscreen"; [ -n "${FPS_SOFT:-}" ] && video=""

scen=$out/$name.scen
{
  echo "wait Bringing Level $map.MyLevel up for play 150"
  echo "sleep 6"
  if [ -n "${FPS_SOFT:-}" ]; then
    printf 'focus\nmove %s %s\nclick 1\nsleep 1\nclick 1\nsleep 1\nclick 1\nsleep 3\n' $((w/2)) $((h/2))
  else
    printf 'pad attach\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\nsleep 3\n'
  fi
  printf 'chan exec God\nchan exec SetAutoCenterPitch 0\nsleep 1\n'
  if [ -n "$place" ]; then
    read -r x y z yaw pitch <<<"$place"
    printf 'chan exec NETPLACE FpsCam %s %s %s %s %s\nsleep 3\nchan exec NETPLACE FpsCam %s %s %s %s %s\nsleep 2\n' \
      "$x" "$y" "$z" "$yaw" "$pitch" "$x" "$y" "$z" "$yaw" "$pitch"
  fi
  echo "chan exec FPSLOG $secs"
  steps=$(( secs * 4 + 4 ))
  for i in $(seq 1 $steps); do
    if [ -n "$place" ] && [ "${FPS_TURN:-1}" != 0 ]; then
      echo "chan exec NETPLACE FpsCam $x $y $z $(( (yaw + i * 4096) % 65536 )) $pitch"
    fi
    echo "sleep 0.25"
  done
  echo "wait [0-9.]+ s [0-9]+ frames [0-9.]+ fps; frame ms 20"
  echo "chan exec FPSLOG 0"
  echo "sleep 0.5"
} > "$scen"

cp -f "$repo"/Engine/Config/*.ini "$sys/"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$out/$name".result*
chan=$out/$name.channel; : > "$chan"
args=(-log -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan"
  "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=$w" "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY=$h"
  "-ini:System:[SDL2Drv.SDL2Client]:FullscreenViewportX=$w" "-ini:System:[SDL2Drv.SDL2Client]:FullscreenViewportY=$h"
  "$@")
inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x0000/0x0000 $video ./uc2.bin "$map?Name=FpsCam?NumBots=0" ${args[@]@Q} > "$out/$name.out" 2>&1 &
pid=\$!
python3 "$repo/tools/dev/m10-inject.py" "$scen" "$out/$name.out" "$chan" "$out/$name.result" > "$out/$name.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
exit 0
INNER
load=$(cut -d' ' -f1-3 /proc/loadavg)
"$work/bin/capped" 2G $(( secs + 240 )) -- xvfb-run -a -s "-screen 0 ${w}x${h}x24" bash "$inner"
rc=$?
rm -f "$inner"
# stdout (-log) has the line without its "FPS:" name; Avaris.log has it with the name.
line=$(grep -a -E "[0-9.]+ s [0-9]+ frames [0-9.]+ fps; frame ms" "$out/$name.out" | head -1 | sed 's/^FPS: //')
gl=$(grep -a -m1 "GL_RENDERER   :" "$out/$name.out" | sed 's/.*GL_RENDERER   : //' | cut -c1-40)
crit=$(grep -a -c "Critical" "$out/$name.out")
if [ -n "$line" ]; then
  echo "PASS fps-bench $name: $line; renderer '${gl:-?}'; critical=$crit; capped_exit=$rc; load $load"
else
  echo "FAIL fps-bench $name: no FPS line (log $out/$name.out, runner $out/$name.inject); critical=$crit; capped_exit=$rc; load $load"
  exit 1
fi
