#!/usr/bin/env bash
# Frame pacing of one native run in a window on this machine's desktop: a match with no bots, the player at a fixed
# spot (TELEPORT), then the pace trace of a number of frames (PACETRACE, Source/Engine/Src/UC2PaceTrace.cpp: per frame
# its start and DeltaTime, the swap's call and return, the frame limiter's sleep), analysed by tools/dev/pace-analyze.py.
#
#   tools/dev/pace-run.sh <name> <tree> [frames]
#   PACE_VSYNC=1 tools/dev/pace-run.sh vsync-on pace 900
#
# Environment:
#   PACE_VSYNC=0|1          UseVSync of both GL devices and the NVIDIA driver's __GL_SYNC_TO_VBLANK (default 0)
#   PACE_CAP=<fps>          [Engine.Engine] FrameRateCap (default 0: uncapped)
#   PACE_MAP=DM-Praxis      map; PACE_TELEPORT="x y z yaw pitch" the spot (default: DM-Praxis's ledge at the first
#                           PlayerStart, facing the round room)
#   PACE_FULLSCREEN=1       start fullscreen (borderless, the desktop's resolution) instead of a PACE_RES window
#   PACE_RES=1280x720       window size
#   PACE_ARGS="..."         more uc2.bin arguments, e.g. the injected stutter -ini:System:[SDLLaunch]:PaceJitter=10:8
#   PACE_VIDEO=x11          SDL's video driver: x11 (Xwayland, DISPLAY=:0) or wayland (WAYLAND_DISPLAY=wayland-0)
#   PACE_HZ=<Hz>            the display's refresh for the analysis (default: xrandr's current rate)
#   PACE_WAIT=600           seconds to wait for other games on the machine to end before starting (then exit 3)
#   PACE_LOCK=<file>        the real screen's lock, held for the run only (default <repo>/../locks/screen.lock)
#   PACE_OUT=<repo>/../logs/pace   output: <name>.out (stdout), <name>.log (Avaris.log), <name>.inject,
#                           <name>.others (other games seen during the run), <name>.txt (the analysis)
# The window is on the desktop session's real screen, so the display's own refresh, vsync and compositor pace it; input
# goes through the engine's test channel only (no X events, no mouse capture). Inside <repo>/../bin/capped 2G under
# flock on the screen's lock; nothing is left running. Timing is only meaningful on a quiet machine: the result line
# prints the load and whether another game ran during the trace.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
name=${1:?name}; tree=${2:?tree}; frames=${3:-900}
vsync=${PACE_VSYNC:-0}; cap=${PACE_CAP:-0}
map=${PACE_MAP:-DM-Praxis}
spot=${PACE_TELEPORT:--1220 288 690 5220 -600}
res=${PACE_RES:-1280x720}
out=${PACE_OUT:-$work/logs/pace}
sys=$work/run/$tree/System
[ -x "$sys/uc2.bin" ] || { echo "no run tree $work/run/$tree (tools/dev/mkrun-uc2.sh)" >&2; exit 2; }
mkdir -p "$out"
hz=${PACE_HZ:-$(env -u WAYLAND_DISPLAY DISPLAY=:0 xrandr --current 2>/dev/null | grep -o -E '[0-9]+\.[0-9]+\*' | head -1 | tr -d '*')}
vs=False; [ "$vsync" = 1 ] && vs=True

# Other games on the machine share its CPU and GPU: wait until none runs (process names as the kernel keeps them, at
# most 15 characters; a headless browser runs a web game).
others() { for c in uc2.bin ucc-bin ut2004.bin ut2004-bin UnrealTournamen Game wineserver; do pgrep -x "$c"; done; pgrep -f -- '--headless' ; }
t0=$(date +%s)
while [ -n "$(others)" ]; do
  [ $(( $(date +%s) - t0 )) -ge "${PACE_WAIT:-600}" ] && { echo "SKIP pace $name: other games kept running"; exit 3; }
  sleep 5
done

scen=$(mktemp)
cat > "$scen" <<SCEN
wait Bringing Level $map.MyLevel up for play 150
sleep 6
pad attach
sleep 1
padtrigger righttrigger
sleep 1
padtrigger righttrigger
sleep 1
padtrigger righttrigger
sleep 3
chan exec God
wait test: exec God 10
chan exec SetAutoCenterPitch 0
sleep 0.5
chan exec TELEPORT $spot
sleep 3
chan exec TELEPORT $spot
sleep 3
chan exec PACETRACE $frames $name
wait pacetrace $name done after 300
sleep 1
SCEN

cp -f "$repo"/Engine/Config/*.ini "$sys/"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys/Avaris.log" "$out/$name".result*
chan=$out/$name.channel; : > "$chan"
args=(-log -UFRONTLOG -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan"
  -ini:System:[SDL2Drv.SDL2Client]:CaptureMouse=False
  "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=${res%x*}" "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY=${res#*x}"
  "-ini:System:[UC2GLDrv.UC2GLRenderDevice]:UseVSync=$vs" "-ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UseVSync=$vs"
  "-ini:System:[Engine.Engine]:FrameRateCap=$cap")
[ -n "${PACE_FULLSCREEN:-}" ] && args+=(-ini:System:[SDL2Drv.SDL2Client]:StartupFullscreen=True)
inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x0000/0x0000 ./uc2.bin "$map?Name=PaceCam?NumBots=0" ${args[@]@Q} ${PACE_ARGS:-} > "$out/$name.out" 2>&1 &
pid=\$!
# Other games during the run, one line per check (this run's own game is not one).
( while kill -0 \$pid 2>/dev/null; do
    o=\$( { for c in uc2.bin ucc-bin ut2004.bin ut2004-bin UnrealTournamen Game wineserver; do pgrep -x "\$c"; done; pgrep -f -- '--headless'; } | grep -v -x "\$pid" | tr '\n' ' ')
    [ -n "\$o" ] && echo "\$(date +%T) \$o"
    sleep 2
  done ) > "$out/$name.others" &
python3 "$repo/tools/dev/m10-inject.py" "$scen" "$out/$name.out" "$chan" "$out/$name.result" > "$out/$name.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
wait
exit 0
INNER
video=${PACE_VIDEO:-x11}
case $video in
  x11)     venv=(env -u WAYLAND_DISPLAY DISPLAY=:0 SDL_VIDEODRIVER=x11) ;;
  wayland) venv=(env WAYLAND_DISPLAY=wayland-0 SDL_VIDEODRIVER=wayland) ;;
  *) echo "PACE_VIDEO is x11 or wayland" >&2; exit 2 ;;
esac
lock=${PACE_LOCK:-$work/locks/screen.lock}
load0=$(cut -d' ' -f1-3 /proc/loadavg)
"${venv[@]}" __GL_SYNC_TO_VBLANK="$vsync" vblank_mode="$vsync" \
  flock "$lock" "$work/bin/capped" 2G 400 -- bash "$inner"
rc=$?
load1=$(cut -d' ' -f1-3 /proc/loadavg)
rm -f "$inner" "$scen"
cp -f "$sys/Avaris.log" "$out/$name.log" 2>/dev/null
crit=$(grep -a -c "Critical" "$out/$name.out" 2>/dev/null || true)
over=$(grep -c . "$out/$name.others" 2>/dev/null || true)
python3 "$repo/tools/dev/pace-analyze.py" --hz "${hz:-60}" "$out/$name.log" > "$out/$name.txt" 2>&1
summary=$(grep -m1 '^verdict' "$out/$name.txt" || echo "verdict: no trace (see $out/$name.inject)")
echo "pace $name: $video vsync=$vsync cap=$cap ${PACE_FULLSCREEN:+fullscreen }${PACE_ARGS:-}; $summary; others_seen=$over; load $load0 -> $load1; critical=$crit; capped_exit=$rc"
