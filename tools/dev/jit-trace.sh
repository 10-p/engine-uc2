#!/usr/bin/env bash
# Jitter trace (native): where the player's own pawn is on the screen, frame by frame, while it runs with the
# third-person camera following (docs/porting/analysis/jitter.md). One summary line per segment and an overall
# PASS/FAIL per configuration on stdout.
#
#   tools/dev/jit-trace.sh <name> <tree> <input> <pacing> [engine arguments...]
#     <tree>    run tree under <WorkUC2>/run (tools/dev/mkrun-uc2.sh)
#     <input>   kbm  keyboard (W/S/D/A) with the mouse owning the look (the PC default; a 1-count mouse move first)
#               pad  the virtual pad's left stick, the pad owning the look (a short right-stick push first)
#     <pacing>  60        -ini:System:[Engine.Engine]:FrameRateCap=60
#               uneven    -ini:System:[SDLLaunch]:FramePattern=16.7:33.3:16.7:16.7:25:33.3 (frame lengths in turn, ms: a browser's
#                         uneven pacing made repeatable; mean 23.6 ms)
#               uncapped  no cap (the null renderer's ~1,000 frames a second)
#
# Each segment runs 1.3 s (forward, back, right, left) with `FRAMETRACE` on (Source/Engine/Src/UC2FrameTrace.cpp:
# a line per drawn frame: the camera of the frame, the pawn, its projected screen position); tools/dev/jit-analyze.py
# measures the steady part. The game runs as the M10 scenarios do (tools/dev/m10-controls.sh --file: Xvfb inside
# <WorkUC2>/bin/capped 2G, -nullrenderer, DM-Deadbolt without bots, God after the spawn), at 1920x1080 (16:9:
# AutoFOV on, as the owner plays), the player put at JIT_PLACE (NETPLACE, default "1977.8 583.7 -34.1 33000 -1400":
# room for every segment). Output: JIT_OUT (default <WorkUC2>/logs/jit/trace)/<name>.{scen,out,result,
# summary,json}.
#
# JIT_LIMIT=<px> (default 1.0): the configuration PASSes when every segment's judder RMS (jit-analyze.py: each frame's
# distance from the line between its neighbours at their times; a smooth sway such as the camera's walk bob gives ~0,
# a place that depends on the frame's own length does not) is below it.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
name=${1:?name}; tree=${2:?tree}; input=${3:?input}; pacing=${4:?pacing}; shift 4
out=${JIT_OUT:-$work/logs/jit/trace}
limit=${JIT_LIMIT:-1.0}
# A fixed start with room for every segment (the spawn is random): forward 560 units, back, right down a ramp, left
# back, the camera never shortened by a wall (measured: arm 205-214 units in all four segments); the view pitched at
# -1400, the pad's auto-centring target (AUTOPITCH_THIRDPERSON), so the pad's runs keep their view still too.
place=${JIT_PLACE:-1977.8 583.7 -34.1 33000 -1400}
# JIT_VIEW=1p: first person (ToggleThirdCam after the placement); the screen measures then say nothing (the pawn is
# behind the camera), the slope b of jit-analyze.py does (the eye computed before or after the pawn's move).
firstperson=""; [ "${JIT_VIEW:-}" = 1p ] && firstperson=$'chan exec ToggleThirdCam\nsleep 1'

m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"
[ -x "$work/run/$tree/System/uc2.bin" ] || { echo "no run tree $work/run/$tree (tools/dev/mkrun-uc2.sh)" >&2; exit 2; }

case $pacing in
  60) pace=("-ini:System:[Engine.Engine]:FrameRateCap=60");;
  uneven) pace=("-ini:System:[SDLLaunch]:FramePattern=16.7:33.3:16.7:16.7:25:33.3");;
  uncapped) pace=();;
  *) echo "unknown pacing $pacing (60 uneven uncapped)" >&2; exit 2;;
esac

segment() {   # segment <tag> <start command> <stop command>
  cat <<SCEN
chan exec FRAMETRACE 1000000 $1
wait frametrace $1 armed 10
$2
sleep 1.3
$3
sleep 0.4
chan exec FRAMETRACE 0 end-$1
wait frametrace end-$1 armed 10
probe $1
SCEN
}

{
  cat <<SCEN
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 4
focus
key Alt_L 0.3
sleep 3
tryseek 6 P['_'].pawn == 1 ;; key Alt_L 0.3
probe spawn
check jit-spawned P['spawn'].pawn==1 and P['spawn'].level=='DM-Deadbolt'
chan exec God
wait test: exec God 10
chan exec NETPLACE LocalPlayer $place
sleep 1
chan exec NETPLACE LocalPlayer $place
sleep 1.5
probe placed
${firstperson}
check jit-placed abs(P['placed'].loc[0] - ($(echo "$place" | cut -d' ' -f1))) < 2 and abs(P['placed'].loc[1] - ($(echo "$place" | cut -d' ' -f2))) < 2
SCEN
  if [ "$input" = pad ]; then
    cat <<SCEN
pad attach
wait SDL2 pads: pad 0 connected 10
sleep 1
pad axis rightx 16000
sleep 0.15
pad axis rightx 0
sleep 0.5
probe owner
check jit-pad-owns-the-look P['owner'].mouselook == 'False'
SCEN
    segment fwd   "pad axis lefty -32767" "pad axis lefty 0"
    segment back  "pad axis lefty 32767"  "pad axis lefty 0"
    segment right "pad axis leftx 32767"  "pad axis leftx 0"
    segment left  "pad axis leftx -32767" "pad axis leftx 0"
  else
    cat <<SCEN
chan mouse 1 0
sleep 0.5
probe owner
SCEN
    segment fwd   "keydown w" "keyup w"
    segment back  "keydown s" "keyup s"
    segment right "keydown d" "keyup d"
    segment left  "keydown a" "keyup a"
  fi
} > "$out/$name.scen"

echo "== $name: tree $tree, input $input, pacing $pacing ${pace[*]:-} $*  (load: $(cut -d' ' -f1-3 /proc/loadavg))"
M10_TREE=$tree M10_OUT=$out M10_SECS=300 M10_URL="DM-Deadbolt?NumBots=0" M10_RES=1920x1080 \
  "$m10" --file "$out/$name.scen" "$name" -nullrenderer "${pace[@]}" "$@" | sed 's/^/    /'
python3 "$repo/tools/dev/jit-analyze.py" "$out/$name.out" --json "$out/$name.json" --tags fwd,back,right,left \
  > "$out/$name.summary"
sed 's/^/    /' "$out/$name.summary"
python3 - "$out/$name.json" "$limit" "$name" <<'PY'
import json, math, sys
r = json.load(open(sys.argv[1])); limit = float(sys.argv[2]); name = sys.argv[3]
worst = 0.0; ok = True; segs = 0
for tag in ('fwd', 'back', 'right', 'left'):
    s = r.get(tag, {})
    if not s.get('steady') or s['steady'] < 20:
        ok = False; print(f"    {tag}: too few steady frames ({s.get('steady', 0)})"); continue
    segs += 1
    worst = max(worst, s['judder']['rms'])
ok = ok and worst < limit
line = ' '.join(f"{t}={r[t]['judder']['rms']:.2f}/{r[t]['jitter']['rms']:.2f}" for t in ('fwd', 'back', 'right', 'left') if r.get(t, {}).get('steady'))
print(f"{'PASS' if ok else 'FAIL'} {name}: worst segment judder RMS {worst:.2f} px (limit {limit} px), {segs}/4 segments; judder/jitter RMS px: {line}")
PY
