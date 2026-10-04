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
# Environment:
#   UC2_RES=1280x720     windowed resolution (edits the run tree's Default.ini; default: what the tree has)
#   UC2_POSTCMD="..."    a console command bound to F2 and pressed 3 s after Fire (e.g. "NextWeapon")
#   UC2_PRECMD="..."     a console command bound to F1 in the run tree's DefUser.ini and pressed 4 s before
#                        Fire, e.g. "set UCDeathmatch NumPlayers 0" (the first PlayerStart is then always
#                        chosen: the wine reference script does the same)
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
if [ -n "${UC2_RES:-}" ]; then
  sed -i -E "s/^(WindowedViewportX|FullscreenViewportX)=.*/\\1=${UC2_RES%x*}/; s/^(WindowedViewportY|FullscreenViewportY)=.*/\\1=${UC2_RES#*x}/" "$sys/Default.ini"
  rm -f "$sys/Avaris.ini"
fi
precmd=0
if [ -n "${UC2_PRECMD:-}" ]; then
  sed -i -E "s/^F1=.*/F1=${UC2_PRECMD}/" "$sys/DefUser.ini"
  rm -f "$sys/User.ini"
  precmd=1
fi
postcmd=0
if [ -n "${UC2_POSTCMD:-}" ]; then
  sed -i -E "s/^F2=.*/F2=${UC2_POSTCMD}/" "$sys/DefUser.ini"
  rm -f "$sys/User.ini"
  postcmd=1
fi

inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy ./uc2.bin $map -log "\$@" > "$out/$name.out" 2>&1 &
pid=\$!
if [ "$fire" != 0 ]; then
  sleep \$(( $fire - 4 ))
  python3 - $precmd $postcmd <<'PY'
import ctypes, sys, time
x = ctypes.CDLL("libX11.so.6"); t = ctypes.CDLL("libXtst.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XStringToKeysym.restype = ctypes.c_ulong
x.XKeysymToKeycode.restype = ctypes.c_ubyte
x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
d = ctypes.c_void_p(x.XOpenDisplay(None))
if sys.argv[1] == '1':
    kc = x.XKeysymToKeycode(d, x.XStringToKeysym(b"F1"))
    for i in range(2):
        t.XTestFakeKeyEvent(d, kc, 1, 0); x.XFlush(d); time.sleep(0.15)
        t.XTestFakeKeyEvent(d, kc, 0, 0); x.XFlush(d); time.sleep(0.6)
    time.sleep(2.5)
else:
    time.sleep(4)
t.XTestFakeMotionEvent(d, -1, 320, 240, 0); x.XFlush(d)
for i in range(3):
    t.XTestFakeButtonEvent(d, 1, 1, 0); x.XFlush(d); time.sleep(0.2)
    t.XTestFakeButtonEvent(d, 1, 0, 0); x.XFlush(d); time.sleep(1.0)
if sys.argv[2] == '1':
    kc = x.XKeysymToKeycode(d, x.XStringToKeysym(b"F2"))
    t.XTestFakeKeyEvent(d, kc, 1, 0); x.XFlush(d); time.sleep(0.15)
    t.XTestFakeKeyEvent(d, kc, 0, 0); x.XFlush(d)
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
grep -a "UC2GLDrv: program [0-9]" "$out/$name.out" | sed -E 's/.*pixel shader ([0-9]+) \(([A-Za-z0-9_]+)\.psh\).*/\1 \2/' | sort -n | uniq -c | tr '\n' ';'; echo
grep -a "GL errors\|UC2GLDrv: programs\|does not\|not usable\|no program" "$out/$name.out" | tail -3
pgrep -x uc2.bin >/dev/null && echo "WARNING: uc2.bin still running"; pgrep -x Xvfb >/dev/null && echo "WARNING: Xvfb still running"
exit 0
