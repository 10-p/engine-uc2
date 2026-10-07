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
#   UC2_BIN=uc2-before.bin  another executable in the tree's System folder (before/after pictures from one
#                        tree; default uc2.bin)
#
#   UC2_AA=1             keep the device's anti-aliasing (O-8: on by default since M11); without it every run gets
#                        -glnoaa, so pictures compare with the references made before anti-aliasing existed
#
# Everything runs inside <WorkUC2>/bin/capped 2G (CLAUDE.md, "Running anything: memory ceiling, always");
#   UC2_CAP=3G           another ceiling (pp-scale: 3G for 2880x2160 and larger windows)
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

aaarg=-glnoaa; [ -n "${UC2_AA:-}" ] && aaarg=""
# AutoFOV off unless UC2_AUTOFOV is set: the reference pictures these runs are compared with (the M7 effect
# pairs, the wine oracle's DM-Praxis view, the O-9 before/after pairs) were made with the 4:3 field of view, and
# at 16:9 D-8's Hor+ widens the framing. tools/dev/d78.sh measures AutoFOV itself and does not use this runner.
fovarg="-ini:System:[ufront.AutoFOV]:AutoFOV=0"; [ -n "${UC2_AUTOFOV:-}" ] && fovarg=""
# Likewise the UI as before D-2 (640x480 drawn 1:1, top-left) unless UC2_UIBOX is set: the references (e.g. the
# wine oracle's DM-Praxis view, made by the original PC binary) have the old HUD. tools/dev/d2-ui-scale.sh
# measures the UI box itself and does not use this runner.
[ -z "${UC2_UIBOX:-}" ] && fovarg="$fovarg -ini:System:[SDL2Drv.SDL2Client]:UIScaleMode=Native640"
inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
echo "\$DISPLAY" > "$out/$name.display"
# M8: OpenAL Soft renders to its null backend unless ALSOFT_DRIVERS says otherwise (headless runs stay quiet).
# M9: no logos (the fire-at times assume none)
SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=${ALSOFT_DRIVERS:-null} ./${UC2_BIN:-uc2.bin} $map -log -nostartupmovies $aaarg $fovarg "\$@" > "$out/$name.out" 2>&1 &
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
"$work/bin/capped" "${UC2_CAP:-2G}" "$secs" -- xvfb-run -a -s "-screen 0 1280x720x24" bash "$inner" "$@"
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
# Nothing of this run may be left behind. Other run trees (other agents) have their own uc2.bin and Xvfb:
# only a uc2.bin whose working directory is this tree's System and the Xvfb of this run's display count.
left=0
for pid in $(pgrep -x "${UC2_BIN:-uc2.bin}"); do
  [ "$(readlink /proc/$pid/cwd 2>/dev/null)" = "$sys" ] && { echo "WARNING: uc2.bin $pid of this run tree is still running"; left=1; }
done
disp=$(cat "$out/$name.display" 2>/dev/null); rm -f "$out/$name.display"
if [ -n "$disp" ] && pgrep -f "Xvfb $disp " >/dev/null; then echo "WARNING: this run's Xvfb ($disp) is still running"; left=1; fi
[ $left = 0 ] && echo "nothing left running"
exit 0
