#!/usr/bin/env bash
# F-5 step 4: consecutive frames of one spot of a map on this machine's GPU, headless, for the CTF-Aqueduct shadow
# flicker (docs/porting/analysis/render-f5.md). One game run per call; the pictures are the frames themselves (one
# `exec shot` per tick, as tools/dev/jit-strip.sh), so a flicker between frames is caught frame by frame.
#
#   tools/dev/f5-flicker.sh <name> <tree> [frames]
#
# Environment:
#   F5_MAP=CTF-Aqueduct            map
#   F5_PLACE="4300 300 900 0 -3000"  NETPLACE x y z yaw pitch (the pawn is dropped there and stands)
#   F5_VIEW=1p                     1p (first person: the camera is the pawn's eyes) or 3p
#   F5_PHASES="run frozen move"    run: time running, camera still; frozen: game time stopped (TimeDilation
#                                  0.0001), camera still; move: time stopped, the view turned by F5_STEP yaw units
#                                  (default 8 = 0.044 degrees) before every frame; walk: time running, the pawn
#                                  walks forward (virtual pad, left stick) from 0.6 s before the first frame
#   F5_DXY="0 0"                  move: the pawn also moves by dx dy units per frame (time stopped: a path that
#                                  repeats exactly, so two runs can be compared frame by frame)
#   F5_PRE="cmd;cmd"               console commands run before the pawn is placed (';' separates), e.g. "Fly"
#                                  (the pawn then stays where it is put, also with time running)
#   F5_TELEPORT="x y z yaw pitch"  B-1: place the pawn with the TELEPORT cheat instead (WHEREAMI's output: the owner's
#                                  coordinates), twice like NETPLACE; F5_PLACE is then only the base of 'move'
#   F5_RMODE=7                     B-1: the viewport's render mode (RMODE <n>; 7 = lighting only), set after placing
#   F5_MOUSE="dx dy"               B-1: phase 'mouse' (time running, pawn standing still): one relative mouse motion
#                                  of dx dy counts (the test channel's 'mouse', as a real mouse) before every frame;
#                                  phase 'mousefz' the same with time stopped
#   F5_PIXTRACE="x y"              B-1: GLPIXTRACE x y all before every frame (log only: what each frame draws there)
#   F5_ARGS="..."                  more uc2.bin arguments; F5_GL4ES=1 runs the GLES twin instead of the desktop device;
#                                  F5_SOFT=1 renders with Mesa llvmpipe on the Xvfb display instead of the GPU
#   F5_BIN=uc2.bin                 another executable of the tree's System folder (before/after pictures)
#   F5_RES=1280x720                window size; F5_SECS=300: time limit of the run (software GL under load: more)
#   F5_OUT=<WorkUC2>/logs/f5/flicker  output folder
# Output: <out>/<name>.{out,inject,result}, <name>-<phase>-NN.png. Inside <WorkUC2>/bin/capped 2G; nothing is left
# running. SDL's offscreen video driver (EGL): no window on any display, as tools/dev/fx-owner.sh UC2_GPU=1.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
name=${1:?name}; tree=${2:?tree}; frames=${3:-12}
map=${F5_MAP:-CTF-Aqueduct}
place=${F5_PLACE:-4300 300 900 0 -3000}
view=${F5_VIEW:-1p}
phases=${F5_PHASES:-run frozen move}
step=${F5_STEP:-8}
read -r dx dy <<<"${F5_DXY:-0 0}"   # move: also this many units along x and y per frame
res=${F5_RES:-1280x720}
out=${F5_OUT:-$work/logs/f5/flicker}
sys=$work/run/$tree/System
inject=$repo/tools/dev/m10-inject.py
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree $work/run/$tree" >&2; exit 2; }
read -r px py pz pyaw ppitch <<<"$place"

scen=$(mktemp)
{
  cat <<SCEN
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
SCEN
  [ "$view" = 1p ] && printf 'chan exec ToggleThirdCam\nsleep 1\n'
  IFS=';' read -ra pre <<<"${F5_PRE:-}"
  for c in "${pre[@]}"; do [ -n "$c" ] && printf 'chan exec %s\nsleep 0.5\n' "$c"; done
  if [ -n "${F5_TELEPORT:-}" ]; then
    printf 'chan exec TELEPORT %s\nsleep 3\nchan exec TELEPORT %s\nsleep 2\n' "$F5_TELEPORT" "$F5_TELEPORT"
  else
    printf 'chan exec NETPLACE LocalPlayer %s\nsleep 3\nchan exec NETPLACE LocalPlayer %s\nsleep 2\n' "$place" "$place"
  fi
  [ -n "${F5_RMODE:-}" ] && printf 'chan exec RMODE %s\nsleep 1\n' "$F5_RMODE"
  for ph in $phases; do
    echo "chan exec NETPROBE phase-$ph"
    case $ph in
      run) echo 'chan exec set LevelInfo TimeDilation 1.0'; echo 'sleep 1.5' ;;
      frozen|move|mousefz) echo 'chan exec set LevelInfo TimeDilation 0.0001'; echo 'sleep 1.5' ;;
      mouse) echo 'chan exec set LevelInfo TimeDilation 1.0'; echo 'sleep 1.5' ;;
      walk) echo 'chan exec set LevelInfo TimeDilation 1.0'; echo 'pad axis lefty -32767'; echo 'sleep 0.6' ;;
    esac
    echo "chan exec NETPROBE shots-$ph"
    for i in $(seq "$frames"); do
      if [ "$ph" = move ]; then echo "chan exec NETPLACE LocalPlayer $((px + i*dx)) $((py + i*dy)) $pz $((pyaw + i*step)) $ppitch"; fi
      case $ph in mouse|mousefz) echo "chan mouse ${F5_MOUSE:-4 0}";; esac
      [ -n "${F5_PIXTRACE:-}" ] && echo "chan exec GLPIXTRACE $F5_PIXTRACE all"
      echo "chan exec shot"
    done
    # the channel runs one line per tick: wait until the engine has reached the end of the phase
    echo "chan exec NETPROBE done-$ph"
    echo "wait done-$ph summary ${F5_SECS:-300}"
    [ "$ph" = walk ] && echo 'pad axis lefty 0'
    echo 'sleep 2'
  done
  echo 'sleep 1'
} > "$scen"

cp -f "$repo"/Engine/Config/*.ini "$sys/"
sed -i -E "s/^(WindowedViewportX|FullscreenViewportX)=.*/\\1=${res%x*}/; s/^(WindowedViewportY|FullscreenViewportY)=.*/\\1=${res#*x}/" "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$out/$name"-*.png "$out/$name".result*
rm -rf "$sys/Profiles"; mkdir -p "$sys/Profiles"
chan=$out/$name.channel; : > "$chan"
dev=""; [ -n "${F5_GL4ES:-}" ] && dev="-gl4es"
# F5_SOFT=1: Mesa llvmpipe on the Xvfb display instead of the GPU (the GLES twin runs there with an ES 2.0
# context, as tools/dev/m11-twin.sh: MESA_GLES_VERSION_OVERRIDE=2.0)
video="SDL_VIDEODRIVER=offscreen"; [ -n "${F5_SOFT:-}" ] && video="MESA_GLES_VERSION_OVERRIDE=2.0"
inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy $video ./${F5_BIN:-uc2.bin} "$map?Name=FxCam" -log -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:KeyboardIsPlayer=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True $dev -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan" ${F5_ARGS:-} > "$out/$name.out" 2>&1 &
pid=\$!
python3 "$inject" "$scen" "$out/$name.out" "$chan" "$out/$name.result" > "$out/$name.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
exit 0
INNER
echo "== flicker $name: tree $tree, $map at ($place) $view, phases [$phases] x $frames frames (load: $(cut -d' ' -f1-3 /proc/loadavg))"
"$work/bin/capped" 2G "${F5_SECS:-300}" -- xvfb-run -a -s "-screen 0 ${res}x24" bash "$inner"
rc=$?
rm -f "$inner" "$scen"
# Shots are numbered in the order taken: frames per phase, phases in order.
n=0
for bmp in "$sys"/Shot*.bmp; do
  [ -f "$bmp" ] || continue
  set -- $phases; idx=$((n / frames)); shift $idx 2>/dev/null; ph=${1:-extra}
  python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$bmp" "$out/$name-$ph-$(printf %02d $((n % frames))).png"
  n=$((n+1))
done
rm -f "$sys"/Shot*.bmp
crit=$(grep -a -c "Critical" "$out/$name.out" 2>/dev/null || true)
glerr=$(grep -a -o "[0-9]* GL errors in [0-9]* frames" "$out/$name.out" | tail -1)
left=0
for p in $(pgrep -x uc2.bin) $(pgrep -x Xvfb); do
  case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"|"$sys"/*) left=$((left+1));; esac
done
echo "flicker $name: $n pictures, capped exit $rc, Critical $crit, ${glerr:-no GL error count}, left_behind=$left"
