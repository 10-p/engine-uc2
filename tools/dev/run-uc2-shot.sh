#!/usr/bin/env bash
# Run the native client headless (Xvfb, software GL) under the memory ceiling, press Fire to start the match,
# and collect the engine's own screenshots.
#
#   tools/dev/run-uc2-shot.sh <tree> <name> <seconds> <fire-at> <map-or-''> [uc2.bin arguments...]
#   tools/dev/run-uc2-shot.sh m6 deadbolt 60 28 DM-Deadbolt -glerrors -glshot=38,44 -glpstoggle=41
#
# <tree>      run tree under <WorkUC2>/run (made by tools/dev/mkrun-uc2.sh)
# <name>      output prefix: <WorkUC2>/logs/<tree>/<name>.out, <name>-N.png (from System/Shot0000N.bmp)
# <seconds>   wall-clock limit of the run
# <fire-at>   seconds after launch at which the left mouse button (Fire) is clicked three times; 0 = never
#
# Everything runs inside <WorkUC2>/bin/capped 2G (CLAUDE.md, "Running anything: memory ceiling, always").
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?tree}; name=${2:?name}; secs=${3:?seconds}; fire=${4:?fire-at}; map=${5-}
shift 5 || true
sys=$work/run/$tree/System
out=$work/logs/$tree
mkdir -p "$out"
rm -f "$sys"/Shot*.bmp

inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy ./uc2.bin $map -log "\$@" > "$out/$name.out" 2>&1 &
pid=\$!
if [ "$fire" != 0 ]; then
  sleep "$fire"
  python3 - <<'PY'
import ctypes, time
x = ctypes.CDLL("libX11.so.6"); t = ctypes.CDLL("libXtst.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
d = ctypes.c_void_p(x.XOpenDisplay(None))
t.XTestFakeMotionEvent(d, -1, 320, 240, 0); x.XFlush(d)
for i in range(3):
    t.XTestFakeButtonEvent(d, 1, 1, 0); x.XFlush(d); time.sleep(0.2)
    t.XTestFakeButtonEvent(d, 1, 0, 0); x.XFlush(d); time.sleep(1.0)
PY
fi
wait \$pid
INNER
"$work/bin/capped" 2G "$secs" -- xvfb-run -a -s "-screen 0 1280x720x24" bash "$inner" "$@"
rc=$?
rm -f "$inner"
n=0
for bmp in "$sys"/Shot*.bmp; do
  [ -f "$bmp" ] || continue
  python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$bmp" "$out/$name-$n.png" && n=$((n+1))
done
echo "run-uc2-shot: exit $rc, $n screenshots, log $out/$name.out"
grep -a -c "Critical" "$out/$name.out" | sed 's/^/Critical lines: /'
grep -a "GL errors\|UC2GLDrv: programs\|UC2GLDrv: pixel shaders\|VertexShaderConfigs\|does not\|not usable\|no program" "$out/$name.out" | tail -12
pgrep -x uc2.bin >/dev/null && echo "WARNING: uc2.bin still running"; pgrep -x Xvfb >/dev/null && echo "WARNING: Xvfb still running"
exit 0
