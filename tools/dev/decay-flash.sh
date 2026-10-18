#!/usr/bin/env bash
# DM-Decay bright flash check (a white cloud that stays): one capped game run of a
# tools/dev/m10-inject.py scenario on DM-Decay with no bots, pictures named after the scenario's shots, then the
# luminance measure of every picture (tools/dev/decay-flash.py).
#
#   tools/dev/decay-flash.sh <tree> <name> <scenario-file> [more uc2.bin arguments]
#
#   DF_RES=2285x1290     another window size (default 1280x720)
#   DF_AA=4              the device's NumAASamples (default 0)
#   DF_URL='?NumBots=0'  map URL options (default ?NumBots=0)
#   DF_SECS=170          time limit
#   DF_MAP=DM-Fury       another map (default DM-Decay)
#   DF_BIN=uc2-x.bin     another executable of the tree's System folder (A/B builds)
#   UC2_GPU=1            this machine's GPU through SDL's offscreen video driver (as tools/dev/fx-owner.sh); default
#                        Xvfb + Mesa llvmpipe
#   add -gl4es to the arguments for the GL4ES twin (GL ES 3.0 context, as tools/dev/m11-twin.sh)
#
# The scenario runs after the common start (level up, match started with Fire, God, SetAutoCenterPitch 0). Output in
# <repo>/../logs/<tree>/: df-<name>-<shot>.png, df-<name>.out (game log), .inject, .lum (measure).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?tree}; name=${2:?name}; scenfile=${3:?scenario}; shift 3
sys=$work/run/$tree/System
out=$work/logs/$tree
res=${DF_RES:-1280x720}; W=${res%x*}; H=${res#*x}
aa=${DF_AA:-0}
url=${DF_URL:-?NumBots=0}
map=${DF_MAP:-DM-Decay}
secs=${DF_SECS:-170}
bin=${DF_BIN:-uc2.bin}
video=""; [ -n "${UC2_GPU:-}" ] && video="SDL_VIDEODRIVER=offscreen"
export MESA_GLES_VERSION_OVERRIDE=${MESA_GLES_VERSION_OVERRIDE:-3.0}
inject=$repo/tools/dev/m10-inject.py
mkdir -p "$out"
tag=df-$name
scen=$(mktemp)
{
  echo "wait Bringing Level $map.MyLevel up for play 150"
  echo "sleep 6"
  if [ -n "${UC2_GPU:-}" ]; then
    printf 'pad attach\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\nsleep 3\n'
  else
    printf 'focus\nmove 640 360\nclick 1\nsleep 1\nclick 1\nsleep 1\nclick 1\nsleep 3\n'
  fi
  printf 'chan exec God\nchan exec SetAutoCenterPitch 0\nsleep 1\n'
  cat "$scenfile"
  echo 'sleep 1.5'
} > "$scen"
cp -f "$repo"/Engine/Config/*.ini "$sys/"
sed -i -E "s/^(WindowedViewportX|FullscreenViewportX)=.*/\\1=$W/; s/^(WindowedViewportY|FullscreenViewportY)=.*/\\1=$H/" "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$out/$tag"-*.png "$out/$tag".result*
chan=$out/$tag.channel; : > "$chan"
glargs=$("$repo/tools/dev/glini" NumAASamples=$aa CheckGLErrors=True | tr '\n' ' ')
inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy $video ./$bin "$map$url" -log -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan" $glargs "\$@" > "$out/$tag.out" 2>&1 &
pid=\$!
python3 "$inject" "$scen" "$out/$tag.out" "$chan" "$out/$tag.result" > "$out/$tag.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
exit 0
INNER
"$work/bin/capped" 2G "$secs" -- xvfb-run -a -s "-screen 0 $((W>1280?W:1280))x$((H>720?H:720))x24" bash "$inner" "$@"
rc=$?
rm -f "$inner" "$scen"
shots=""
if [ -f "$out/$tag.result.shots" ]; then
  i=0
  while read -r shot; do
    bmp=$(printf "%s/Shot%05d.bmp" "$sys" "$i")
    # appCreateBitmap (Core UnMisc.cpp) writes rows without the BMP's 4-byte padding: read the 24-bit rows as they are
    [ -f "$bmp" ] && python3 -c "
import sys, struct
from PIL import Image
d = open(sys.argv[1], 'rb').read(); off, = struct.unpack_from('<I', d, 10); w, h = struct.unpack_from('<ii', d, 18)
Image.frombytes('RGB', (w, h), d[off:off + 3*w*h], 'raw', 'BGR', 0, -1).save(sys.argv[2])" "$bmp" "$out/$tag-$shot.png" && shots="$shots $shot"
    i=$((i+1))
  done < "$out/$tag.result.shots"
  rm -f "$sys"/Shot*.bmp
fi
python3 "$repo/tools/dev/decay-flash.py" "$out/$tag" > "$out/$tag.lum" 2>&1
crit=$(grep -a -c "Critical" "$out/$tag.out" 2>/dev/null)
glerr=$(grep -a "GL errors so far" "$out/$tag.out" 2>/dev/null | tail -1 | sed -E 's/.*, ([0-9]+) GL errors.*/\1/')
left=0
for p in $(pgrep -x "$bin") $(pgrep -x Xvfb); do
  case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"|"$sys"/*) left=$((left+1));; esac
done
echo "df $name: critical=${crit:-?} glerrors=${glerr:-?} shots=[${shots# }] capped_exit=$rc left_behind=$left"
cat "$out/$tag.lum"
