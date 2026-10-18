#!/usr/bin/env bash
# Run the native client headless (Xvfb, software GL) under the memory ceiling with its audio rendered to a .wav
# file through OpenAL Soft's wave writer (no sound hardware involved), optionally playing a bit: Fire to start
# the match, then walking, jumping and firing, so footsteps and weapons are heard. Then the capture report.
#
#   tools/dev/run-uc2-audio.sh <tree> <name> <seconds> <fire-at> <url-or-''> [uc2.bin arguments...]
#   tools/dev/run-uc2-audio.sh m8 menu 60 0 ''
#   tools/dev/run-uc2-audio.sh m8 deadbolt 80 28 'DM-Deadbolt?NumBots=3'
#
# Writes <repo>/../logs/<tree>/<name>.wav, .out (the game's log), .png (spectrogram), .report.txt.
# The run tree's Default.ini gets DevSound unsuppressed (the cue log) and loses Avaris.ini/User.ini so the
# change takes effect; nothing outside the run tree is touched.
# Environment: UC2_AUDIO_ENV="VAR=value ..." extra environment for uc2.bin.
#              UC2_PRECMD="..." a console command bound to F1 (run tree's DefUser.ini) and pressed 4 s before
#              Fire, e.g. "set UCDeathmatch NumPlayers 0" so the first PlayerStart is always the spawn (pinned).
#              UC2_POSTCMD="..." a console command bound to F2 and pressed once after the spawn (e.g. "Ghost":
#              the walk then goes through walls, from zone to zone).
# What is fixed: duration, URL, the input script (its random generator is seeded), output names. What is not:
# the game itself (bot AI, frame timing under software GL), so cue counts differ a little from run to run.
# Last line: "SUMMARY <name>: ..." (duration, peak, RMS, silent seconds, cue requests by outcome).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?tree}; name=${2:?name}; secs=${3:?seconds}; fire=${4:?fire-at}; url=${5-}
shift 5 || true
sys=$work/run/$tree/System
out=$work/logs/$tree
mkdir -p "$out"
sed -i '/^Suppress=DevSound\r\?$/d' "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$out/$name.wav"
postcmd=0
if [ -n "${UC2_POSTCMD:-}" ]; then
  sed -i -E "s/^F2=.*/F2=${UC2_POSTCMD}/" "$sys/DefUser.ini"
  rm -f "$sys/User.ini"
  postcmd=1
fi
precmd=0
if [ -n "${UC2_PRECMD:-}" ]; then
  sed -i -E "s/^F1=.*/F1=${UC2_PRECMD}/" "$sys/DefUser.ini"
  rm -f "$sys/User.ini"
  precmd=1
fi

inner=$(mktemp)
cat > "$inner" <<INNER
cd "$sys" || exit 1
# M9: -ini:System:[UC2Movie]:StartupMovies=False keeps the captures as they were (the logos would add 20 s of their own sound first)
env ${UC2_AUDIO_ENV:-} ./uc2.bin "$url" -log -ini:System:[UC2Movie]:StartupMovies=False "-ini:System:[UC2ALAudio.UC2ALAudioSubsystem]:WaveFile=$out/$name.wav" "\$@" > "$out/$name.out" 2>&1 &
pid=\$!
# M16: the game's peak resident memory (VmHWM), sampled every second (the run is killed at the end, not shut down).
( while kill -0 \$pid 2>/dev/null; do grep VmHWM /proc/\$pid/status > "$out/$name.hwm.tmp" 2>/dev/null && mv -f "$out/$name.hwm.tmp" "$out/$name.hwm"; sleep 1; done ) &
if [ "$fire" != 0 ]; then
  sleep \$(( $fire - 4 ))
  python3 - $(( secs - fire - 6 )) $precmd $postcmd <<'PY'
import ctypes, sys, time, random
x = ctypes.CDLL("libX11.so.6"); t = ctypes.CDLL("libXtst.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XStringToKeysym.restype = ctypes.c_ulong
x.XKeysymToKeycode.restype = ctypes.c_ubyte
x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
d = ctypes.c_void_p(x.XOpenDisplay(None))
def key(name, down):
    t.XTestFakeKeyEvent(d, x.XKeysymToKeycode(d, x.XStringToKeysym(name)), down, 0); x.XFlush(d)
def button(b, down):
    t.XTestFakeButtonEvent(d, b, down, 0); x.XFlush(d)
if sys.argv[2] == '1':                  # the F1 command (UC2_PRECMD), 4 s before Fire
    for i in range(2):
        key(b"F1", 1); time.sleep(0.15); key(b"F1", 0); time.sleep(0.6)
    time.sleep(2.5)
else:
    time.sleep(4)
t.XTestFakeMotionEvent(d, -1, 320, 240, 0); x.XFlush(d)
for i in range(3):                      # Fire: start the match (as tools/dev/run-uc2-shot.sh)
    button(1, 1); time.sleep(0.2); button(1, 0); time.sleep(1.0)
time.sleep(4)                           # spawn
if sys.argv[3] == '1':                  # the F2 command (UC2_POSTCMD), once
    key(b"F2", 1); time.sleep(0.15); key(b"F2", 0); time.sleep(0.5)
end = time.time() + int(sys.argv[1])
random.seed(7)
while time.time() < end:                # walk forward, turn now and then, jump, fire in bursts
    key(b"Up", 1)
    button(1, 1); time.sleep(1.2); button(1, 0)
    if random.random() < 0.4:
        key(b"space", 1); time.sleep(0.1); key(b"space", 0)
    t.XTestFakeRelativeMotionEvent(d, random.choice([-120, 80, 160]), 0, 0); x.XFlush(d)
    time.sleep(0.8)
    key(b"Up", 0)
    button(3, 1); time.sleep(0.3); button(3, 0)
    time.sleep(0.5)
PY
fi
wait \$pid
INNER
"$work/bin/capped" 2G "$secs" -- xvfb-run -a -s "-screen 0 1280x720x24" bash "$inner" "$@"
rc=$?
rm -f "$inner"
echo "run-uc2-audio: exit $rc, log $out/$name.out"
grep -a -c "Critical" "$out/$name.out" | sed 's/^/Critical lines: /'
grep -a "UC2ALAudio:" "$out/$name.out" | head -3
"$repo/tools/dev/audio-capture-report.sh" "$out/$name.wav" > "$out/$name.report.txt" 2>&1
cat "$out/$name.report.txt"
# Distinct cues by outcome (the cue log: DevSound "UC2Audio: <cue> on <actor>: played|culled|failed ...").
grep -a -E "UC2Audio: \[ *[0-9.]+\] [^ ]+( on [^:]+)?: (played|culled|failed)" "$out/$name.out" \
  | sed -E 's/.*UC2Audio: \[ *[0-9.]+\] ([^ ]+)( on [^:]+)?: (played|culled|failed).*/\3 \1/' | sort | uniq -c | sort -k2,2 -k1,1nr \
  > "$out/$name.cues.txt"
echo "distinct cues: played $(grep -c ' played ' "$out/$name.cues.txt"), culled $(grep -c ' culled ' "$out/$name.cues.txt"), failed $(grep -c ' failed ' "$out/$name.cues.txt") (list: $out/$name.cues.txt)"
sum() { awk -v k=" $1 " 'index($0,k){n+=$1; d++} END{printf "%d (%d distinct)", n, d}' "$out/$name.cues.txt"; }
dur=$(grep -a -o "duration=[0-9.]*" "$out/$name.report.txt" | cut -d= -f2)
peak=$(grep -a "Peak level dB" "$out/$name.report.txt" | awk '{printf "%.1f", $NF}')
rms=$(grep -a "RMS level dB" "$out/$name.report.txt" | awk '{printf "%.1f", $NF}')
silent=$(grep -a -o "silent (< -70 dBFS): [0-9]*" "$out/$name.report.txt" | awk '{print $NF}')
req=$(awk '{n+=$1} END{print n+0}' "$out/$name.cues.txt")
hwm=$(awk '{printf "%d", $2/1024}' "$out/$name.hwm" 2>/dev/null)
echo "SUMMARY $name: ${dur:-?} s, peak ${peak:-?} dBFS, RMS ${rms:-?} dBFS, silent seconds ${silent:-?}; cue requests $req: played $(sum played), culled $(sum culled), failed $(sum failed); Critical $(grep -a -c Critical "$out/$name.out"); peak RSS ${hwm:-?} MB" | tee "$out/$name.summary.txt"
for p in $(pgrep -x uc2.bin) $(pgrep -x Xvfb); do
  case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"*) echo "WARNING: process $p of this run tree still running";; esac
done
exit 0
