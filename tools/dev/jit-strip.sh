#!/usr/bin/env bash
# A strip of consecutive rendered frames of the player's pawn running forward with the third-person camera following
# for review: the same view from two builds, side by side.
#
#   tools/dev/jit-strip.sh <name> <tree> [frames]      e.g. jit-strip.sh before jit-main 12
#
# One game run on this machine's GPU, headless (SDL's offscreen video driver: no window on any display; as
# tools/dev/fx-owner.sh UC2_GPU=1), inside <repo>/../bin/capped 2G, 1280x720, DM-Deadbolt without bots, uneven frame
# pacing (JIT_STRIP_PATTERN, [SDLLaunch] FramePattern set with -ini:, default 40:70:40:40:55:70 ms: below). The player
# is put at jit-trace.sh's spot, the mouse given the look (one count through the test channel), and runs forward on
# the virtual pad's left stick (the left stick does not take the look from the mouse; there is no X window for keys);
# FRAMETRACE is on and once the run is steady, <frames> consecutive frames are saved (one `exec shot` per tick: each
# picture is the frame whose trace line precedes the shot's "test: exec shot" line). tools/dev/jit-strip.py crops them
# around the pawn and marks the screen spot where it stands still.
# Output: JIT_OUT (default <repo>/../logs/jit/strip)/<name>.{out,inject,result} and <name>-NN.png.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
name=${1:?name}; tree=${2:?tree}; frames=${3:-12}
out=${JIT_OUT:-$work/logs/jit/strip}
sys=$work/run/$tree/System
inject=$repo/tools/dev/m10-inject.py
place=${JIT_PLACE:-1977.8 583.7 -34.1 33000 -1400}
# Frame lengths in turn (ms). Saving a picture costs ~35-45 ms of the frame here, so the pattern is that of a slow
# browser frame (25-48 ms) and still uneven.
pattern=${JIT_STRIP_PATTERN:-40:70:40:40:55:70}
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree $work/run/$tree" >&2; exit 2; }

scen=$(mktemp)
{
  cat <<SCEN
wait Bringing Level DM-Deadbolt.MyLevel up for play 150
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
chan exec NETPLACE LocalPlayer $place
sleep 1
chan exec NETPLACE LocalPlayer $place
sleep 1.5
chan mouse 1 0
sleep 0.5
chan exec FRAMETRACE 1000000 strip
wait frametrace strip armed 10
pad axis lefty -32767
sleep 0.8
SCEN
  for i in $(seq "$frames"); do echo "chan exec shot"; done
  cat <<SCEN
sleep 1.5
pad axis lefty 0
sleep 0.3
chan exec FRAMETRACE 0 end-strip
wait frametrace end-strip armed 10
sleep 1
SCEN
} > "$scen"

cp -f "$repo"/Engine/Config/*.ini "$sys/"
sed -i -E "s/^(WindowedViewportX|FullscreenViewportX)=.*/\\1=1280/; s/^(WindowedViewportY|FullscreenViewportY)=.*/\\1=720/" "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$out/$name"-*.png "$out/$name".result*
rm -rf "$sys/Profiles"; mkdir -p "$sys/Profiles"
chan=$out/$name.channel; : > "$chan"
inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x0000/0x0000 SDL_VIDEODRIVER=offscreen ./uc2.bin "DM-Deadbolt?NumBots=0" -log -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True "-ini:System:[SDLLaunch]:FramePattern=$pattern" -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan" > "$out/$name.out" 2>&1 &
pid=\$!
python3 "$inject" "$scen" "$out/$name.out" "$chan" "$out/$name.result" > "$out/$name.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
exit 0
INNER
echo "== strip $name: tree $tree, $frames frames, pattern $pattern (load: $(cut -d' ' -f1-3 /proc/loadavg))"
"$work/bin/capped" 2G 300 -- xvfb-run -a -s "-screen 0 1280x720x24" bash "$inner"
rc=$?
rm -f "$inner" "$scen"
n=0
for bmp in "$sys"/Shot*.bmp; do
  [ -f "$bmp" ] || continue
  python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$bmp" "$out/$name-$(printf %02d $n).png"
  n=$((n+1))
done
rm -f "$sys"/Shot*.bmp
crit=$(grep -a -c "Critical" "$out/$name.out" 2>/dev/null || true)
echo "strip $name: $n pictures, capped exit $rc, Critical $crit, $(grep -a -c '^frametrace strip [0-9]' "$out/$name.out") trace lines"
