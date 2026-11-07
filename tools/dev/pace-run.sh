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
#   PACE_MAP=DM-Apex        map; PACE_TELEPORT="x y z yaw pitch" the spot (default: DM-Apex's view of the native
#                           benchmark, a light one, so the frame rate stays well above the display's)
#   PACE_FULLSCREEN=1       start fullscreen (borderless, the desktop's resolution) instead of a PACE_RES window
#   PACE_RES=640x480        window size (UC2's default window)
#   PACE_RENDER=full        cheap: the world's geometry and the post-processing hidden from the render
#                           (tools/dev/m10-inject.py 'render cheap'), a light frame whatever else loads the machine
#   PACE_ARGS="..."         more uc2.bin arguments, e.g. the injected stutter -ini:System:[SDLLaunch]:PaceJitter=10:8
#   PACE_VIDEO=x11          SDL's video driver: x11 (Xwayland, DISPLAY=:0; the build's SDL has no Wayland driver) or
#                           offscreen (EGL, no display and no compositor: a reference without the screen)
#   PACE_HZ=<Hz>            the display's refresh for the analysis (default: KScreen's current mode, else xrandr's)
#   PACE_WAIT=600           seconds to wait for other games on the machine to end; then it runs anyway (others_seen says)
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
map=${PACE_MAP:-DM-Apex}
spot=${PACE_TELEPORT:-1218 952 117.9 38972 0}
res=${PACE_RES:-640x480}
out=${PACE_OUT:-$work/logs/pace}
sys=$work/run/$tree/System
[ -x "$sys/uc2.bin" ] || { echo "no run tree $work/run/$tree (tools/dev/mkrun-uc2.sh)" >&2; exit 2; }
mkdir -p "$out"
# The output's real rate: KScreen's current mode on KDE (Xwayland's xrandr lists emulated modes), else xrandr's.
hz=${PACE_HZ:-$(kscreen-doctor -o 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | grep -o -E '@[0-9]+\.[0-9]+\*' | head -1 | tr -d '@*')}
hz=${hz:-$(env -u WAYLAND_DISPLAY DISPLAY=:0 xrandr --current 2>/dev/null | grep -o -E '[0-9]+\.[0-9]+\*' | head -1 | tr -d '*')}
vs=False; [ "$vsync" = 1 ] && vs=True

# Other games on the machine share its CPU and GPU: wait until none runs (process names as the kernel keeps them, at
# most 15 characters; a headless browser runs a web game).
others() { for c in uc2.bin ucc-bin ut2004.bin ut2004-bin UnrealTournamen Game wineserver; do pgrep -x "$c"; done; pgrep -f -- '--headless' ; }
t0=$(date +%s)
while [ -n "$(others)" ]; do
  [ $(( $(date +%s) - t0 )) -ge "${PACE_WAIT:-600}" ] && break
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
render ${PACE_RENDER:-full}
sleep 1
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
# The GPU's performance state, clock and load twice a second (NVIDIA lowers its clocks under a light load).
gpu=
if command -v nvidia-smi >/dev/null; then
  nvidia-smi --query-gpu=timestamp,pstate,clocks.gr,utilization.gpu --format=csv,noheader -lms 500 > "$out/$name.gpu" 2>/dev/null &
  gpu=\$!
fi
# The runner's lines with the time each was reached (the trace's window for the GPU summary).
python3 -u "$repo/tools/dev/m10-inject.py" "$scen" "$out/$name.out" "$chan" "$out/$name.result" 2>&1 |
  while IFS= read -r l; do printf '%s %s\n' "\$(date +%T)" "\$l"; done > "$out/$name.inject"
[ -n "\$gpu" ] && kill \$gpu 2>/dev/null
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
wait
exit 0
INNER
video=${PACE_VIDEO:-x11}
wrap=()
case $video in
  x11)     venv=(env -u WAYLAND_DISPLAY DISPLAY=:0 SDL_VIDEODRIVER=x11) ;;
  # The runner itself needs an X display: a private Xvfb, which the game does not draw on.
  offscreen) venv=(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen); wrap=(xvfb-run -a -s "-screen 0 ${res}x24") ;;
  *) echo "PACE_VIDEO is x11 or offscreen" >&2; exit 2 ;;
esac
lock=${PACE_LOCK:-$work/locks/screen.lock}
load0=$(cut -d' ' -f1-3 /proc/loadavg)
"${venv[@]}" __GL_SYNC_TO_VBLANK="$vsync" vblank_mode="$vsync" \
  flock "$lock" "$work/bin/capped" 2G 400 -- "${wrap[@]}" bash "$inner"
rc=$?
load1=$(cut -d' ' -f1-3 /proc/loadavg)
rm -f "$inner" "$scen"
cp -f "$sys/Avaris.log" "$out/$name.log" 2>/dev/null
crit=$(grep -a -c "Critical" "$out/$name.out" 2>/dev/null || true)
over=$(grep -c . "$out/$name.others" 2>/dev/null || true)
python3 "$repo/tools/dev/pace-analyze.py" --hz "${hz:-60}" "$out/$name.log" > "$out/$name.txt" 2>&1
summary=$(grep -m1 '^verdict' "$out/$name.txt" || echo "verdict: no trace (see $out/$name.inject)")
win=$res; [ -n "${PACE_FULLSCREEN:-}" ] && win=fullscreen
# The GPU during the trace (from the PACETRACE command to the runner's next line): the commonest performance state,
# the median clock and load.
gpu=$(python3 - "$out/$name.gpu" "$out/$name.inject" <<'PY' 2>/dev/null
import sys, statistics as s
rows = [l.strip().split(', ') for l in open(sys.argv[1]) if l.count(',') == 3]
lines = [l.split(' ', 1) for l in open(sys.argv[2])]
a = next((i for i, l in enumerate(lines) if 'exec PACETRACE' in l[1]), None)
if a is not None and a + 2 < len(lines):
    t0, t1 = lines[a][0], lines[a + 2][0]
    rows = [r for r in rows if t0 <= r[0].split()[1][:8] <= t1] or rows
st = max(set(r[1] for r in rows), key=lambda p: sum(1 for r in rows if r[1] == p))
print('gpu %s %d MHz %d%%' % (st, s.median(int(r[2].split()[0]) for r in rows), s.median(int(r[3].split()[0]) for r in rows)))
PY
)
echo "pace $name: $video $win render=${PACE_RENDER:-full} vsync=$vsync cap=$cap ${PACE_ARGS:-}; $summary; ${gpu:-gpu ?}; others_seen=$over; load $load0 -> $load1; critical=$crit; capped_exit=$rc"
