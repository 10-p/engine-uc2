#!/usr/bin/env bash
# M12: the Windows build (MinGW, i686) checked under wine, headless and repeatable. One PASS/FAIL line per check.
#
#   tools/dev/m12-wine-smoke.sh                 every check
#   tools/dev/m12-wine-smoke.sh verify server   only these (names below)
#
# Needs: the wine run tree  <WorkUC2>/run/m12         (tools/dev/mkrun-uc2.sh build-windows m12)
#        the Linux reference <WorkUC2>/run/m12-native  (tools/dev/mkrun-uc2.sh build-native m12-native), only for
#        the 'pinned' comparison; without it that check says SKIP.
# Every program runs inside <WorkUC2>/bin/capped (3G for wine, 2G for the Linux reference) through
# tools/dev/m12-wine.sh (own prefix <WorkUC2>/.cache/wine-m12, wineserver killed after every run, no owner display,
# Xvfb where a window is needed). Output: <WorkUC2>/logs/m12/smoke-<check>.* (stable names), the M10 runner's
# files in <WorkUC2>/logs/m12/smoke-controls/, and the PASS/FAIL lines also in <WorkUC2>/logs/m12/smoke-summary.txt.
#
# Checks (PASS/FAIL is decided by log lines and measurements, never by looking):
#   verify     ucc.exe verifyclasses: exit 0 and "VERIFY: all native class layouts match"
#   server     ucc.exe server DM-Deadbolt: level up, WinSock listening, still running at 30 s, Ctrl+C -> clean
#              shutdown ("Unloading: Package Engine" at >= 25 s, "Log file closed"), 0 Critical
#   null       uc2.exe -nullrenderer: menu level up, null device, Ctrl+C at 45 s -> exit 0, "Exiting.", 0 Critical
#   gl         uc2.exe (OpenGL): GL_VENDOR/RENDERER/VERSION logged, menu up, -glerrors -> 0 GL errors, engine
#              screenshot not blank, Ctrl+C -> exit 0, 0 Critical
#   pinned     (post-processing off: glow and depth of field spread the moving things over the static part)
#              DM-Deadbolt from a pinned view (tools/dev/m12/deadbolt-pinned.scen) under wine AND on Linux:
#              same position/rotation, 0 GL errors, and the two engine screenshots identical (within 2 levels)
#              in >= 99.9% of the pixels outside the player model and the pickups (the only things that move)
#   audiocheck ucc.exe UC2ALAudio.UC2AudioCheck: "0 failures"
#   selftest   ucc.exe UC2ALAudio.UC2AudioSelfTest: "39 checks, 0 failed"
#   capture    uc2.exe -audiowave= on the menu for 60 s (OpenAL32.dll's wave writer) -> the music is NuBattle at
#              -19.0 dB (tools/dev/audio-music-check.py, Recoil as control), 0 Critical
#   keyboard   the M10 keyboard scenario (real X key events through wine; tools/dev/m10-controls.sh)
#   pad        the M10 pad scenario (SDL's virtual controller through the engine's test channel)
#   netjoin    the Linux client (run/m12-native) connects to a ucc.exe server under wine on 127.0.0.1:7797: the
#              server logs "Login request", the client "Welcomed by server: LEVEL=DM-Deadbolt", the server then
#              shuts down cleanly (SKIP without the tree). The join does not complete on any platform yet (BLOCKERS.md).
#   nosound    OpenAL32.dll moved aside for one run: "sound is off", the menu still comes up, exit 0
#   leftovers  no wineserver of the M12 prefix, no uc2.exe/ucc.exe left running
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
capped=$work/bin/capped
wine=$repo/tools/dev/m12-wine.sh
tree=$work/run/m12
sys=$tree/System
ref=$work/run/m12-native/System
out=$work/logs/m12
summary=$out/smoke-summary.txt
prefix=${UC2_WINEPREFIX:-$work/.cache/wine-m12}
mkdir -p "$out"
[ -f "$sys/uc2.exe" ] && [ -f "$sys/ucc.exe" ] || { echo "no wine run tree at $tree (tools/dev/mkrun-uc2.sh build-windows m12)" >&2; exit 2; }

result() {	# result <PASS|FAIL|SKIP> <check> <details>
  echo "$1 $2: $3" | tee -a "$summary"
}
crit() { grep -a -c "Critical" "$1" 2>/dev/null || true; }
# A fresh configuration in the wine tree, as the M10 runner does: tracked inis, nothing derived.
prep() {
  cp -f "$repo"/Engine/Config/*.ini "$1/"
  rm -f "$1/Avaris.ini" "$1/User.ini" "$1"/Shot*.bmp
}
bmp2png() { python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" "$1" "$2"; }

check_verify() {
  local o=$out/smoke-verify.out rc
  rm -f "$sys/ucc.log"
  "$capped" 3G 600 -- "$wine" "$sys" ucc.exe verifyclasses > "$o" 2>&1; rc=$?
  cp -f "$sys/ucc.log" "$out/smoke-verify.log" 2>/dev/null
  if [ $rc = 0 ] && grep -a -q "VERIFY: all native class layouts match" "$out/smoke-verify.log"; then
    result PASS verify "exit 0, 'VERIFY: all native class layouts match', Critical $(crit "$out/smoke-verify.log")"
  else
    result FAIL verify "exit $rc; $(grep -a 'VERIFY:' "$out/smoke-verify.log" 2>/dev/null | tail -1)"
  fi
}

check_server() {
  local o=$out/smoke-server.out l=$out/smoke-server.log rc ms
  rm -f "$sys/ucc.log"
  UC2_WINE_STOP_AFTER=30 "$capped" 3G 120 -- "$wine" "$sys" ucc.exe server DM-Deadbolt -log > "$o" 2>&1; rc=$?
  cp -f "$sys/ucc.log" "$l" 2>/dev/null
  ms=$(grep -a -o "[0-9.]*ms Unloading: Package Engine" "$l" | head -1 | cut -d. -f1)
  if grep -a -q "Bringing Level DM-Deadbolt.MyLevel up for play" "$l" && grep -a -q "TcpNetDriver on port" "$l" \
     && [ "${ms:-0}" -ge 25000 ] && grep -a -q "Log file closed" "$l" && [ "$(crit "$l")" = 0 ]; then
    result PASS server "DM-Deadbolt up, $(grep -a -o 'WinSock: version[^,]*' "$l" | head -1), $(grep -a -o 'TcpNetDriver on port [0-9]*' "$l"), ran until the Ctrl+C, engine unloaded at ${ms} ms, log closed, exit $rc (UCC: 1 = warnings), Critical 0"
  else
    result FAIL server "exit $rc, unload at ${ms:-never} ms, Critical $(crit "$l"); see $l"
  fi
}

# client <name> <stop-after> [uc2.exe args...]: one uc2.exe run under Xvfb, Ctrl+C after <stop-after> s. Sets $rc.
client() {
  local name=$1 stop=$2; shift 2
  prep "$sys"
  # The capture wants the cue log (DevSound), as tools/dev/run-uc2-audio.sh does; in this tree's copy only.
  [ "${CLIENT_DEVSOUND:-0}" = 1 ] && sed -i '/^Suppress=DevSound\r\?$/d' "$sys/Default.ini"
  UC2_WINE_STOP_AFTER=$stop "$capped" 3G $((stop + 60)) -- xvfb-run -a -s "-screen 0 1280x720x24" \
    bash -c 'SDL_AUDIODRIVER=dummy UC2_WINE_DISPLAY=$DISPLAY "$0" "$@"' "$wine" "$sys" uc2.exe "$@" -nostartupmovies > "$out/smoke-$name.out" 2>&1
  rc=$?
  cp -f "$sys/Avaris.log" "$out/smoke-$name.log" 2>/dev/null
}
clean_exit() {	# clean_exit <out-file>: exit 0 (in $rc), "Exiting.", no crash report
  [ "$rc" = 0 ] && grep -a -q "^Exiting\.\|Exit: Exiting\." "$1" && ! grep -a -q "=== CRASH\|Unhandled exception\|wine: Unhandled" "$1"
}

check_null() {
  local o=$out/smoke-null.out
  client null 45 -log -nullrenderer
  if grep -a -q "Render device: Engine.NullRenderDevice" "$o" && grep -a -q "Bringing Level Menu.MyLevel up for play" "$o" \
     && clean_exit "$o" && [ "$(crit "$o")" = 0 ]; then
    result PASS null "menu level up with Engine.NullRenderDevice, $(grep -a -o 'SDL2 video driver: [a-z0-9]*' "$o"), Ctrl+C at 45 s -> exit 0, Critical 0"
  else
    result FAIL null "exit $rc, Critical $(crit "$o"); see $o"
  fi
}

check_gl() {
  local o=$out/smoke-gl.out stats
  client gl 45 -log -glerrors -glshot=30
  [ -f "$sys/Shot00000.bmp" ] && bmp2png "$sys/Shot00000.bmp" "$out/smoke-gl-menu.png"
  stats=$(python3 - "$out/smoke-gl-menu.png" <<'PY' 2>/dev/null
import sys
from PIL import Image, ImageStat
im = Image.open(sys.argv[1]).convert('L'); h = im.histogram(); n = sum(h)
print("%.1f %.1f" % (ImageStat.Stat(im).stddev[0], 100.0 * sum(h[16:]) / n))
PY
)
  local sd=${stats%% *} lit=${stats##* }
  local errs; errs=$(grep -a -o "UC2GLDrv: [0-9]* GL errors in [0-9]* frames" "$o" | tail -1)
  if grep -a -q "GL_VERSION" "$o" && grep -a -q "Bringing Level Menu.MyLevel up for play" "$o" \
     && [[ "$errs" == "UC2GLDrv: 0 GL errors in "* ]] && [ -n "$stats" ] \
     && awk -v s="$sd" -v l="$lit" 'BEGIN{exit !(s>10 && l>20)}' && clean_exit "$o" && [ "$(crit "$o")" = 0 ]; then
    result PASS gl "$(grep -a -o 'GL_RENDERER *: .*' "$o" | head -1 | sed 's/  */ /g'); $(grep -a -o 'GL_VERSION *: .*' "$o" | head -1 | sed 's/  */ /g'); ${errs#UC2GLDrv: }; menu screenshot luminance stddev $sd, ${lit}% of pixels lit (smoke-gl-menu.png); exit 0, Critical 0"
  else
    result FAIL gl "exit $rc, '${errs:-no GL error summary}', screenshot stddev ${sd:-?} lit ${lit:-?}%, Critical $(crit "$o"); see $o"
  fi
}

# M10 runner, wine settings. $1 = scenario or --file ...
m10_wine() {
  M10_TREE=m12 M10_OUT=$out/smoke-controls M10_LAUNCH="$wine $sys uc2.exe" M10_STOPSIG=USR1 M10_MEM=3G \
    M10_PROC=uc2.exe M10_SETTLE=12 "$repo/tools/dev/m10-controls.sh" "$@"
}

check_pinned() {
  local line wp lp
  rm -rf "$out/smoke-pinned-wine" "$out/smoke-pinned-linux"
  line=$(M10_URL='DM-Deadbolt?NumBots=0' M10_TREE=m12 M10_OUT=$out/smoke-pinned-wine M10_LAUNCH="$wine $sys uc2.exe" \
    M10_STOPSIG=USR1 M10_MEM=3G M10_PROC=uc2.exe "$repo/tools/dev/m10-controls.sh" --file "$repo/tools/dev/m12/deadbolt-pinned.scen" pinned -glerrors -nopostprocess 2>&1 | tail -1)
  wp=$out/smoke-pinned-wine/pinned-pinned.png
  if [ ! -x "$ref/uc2.bin" ]; then
    result SKIP pinned "no Linux reference tree at $ref (wine run: $line)"
    return
  fi
  M10_URL='DM-Deadbolt?NumBots=0' M10_TREE=m12-native M10_OUT=$out/smoke-pinned-linux \
    "$repo/tools/dev/m10-controls.sh" --file "$repo/tools/dev/m12/deadbolt-pinned.scen" pinned -glerrors -nopostprocess > /dev/null 2>&1
  lp=$out/smoke-pinned-linux/pinned-pinned.png
  local vw vl cmp errs
  vw=$(grep -a "^probe view" "$out/smoke-pinned-wine/pinned.out" | grep -a -o "loc=([^)]*) rot=([^)]*)")
  vl=$(grep -a "^probe view" "$out/smoke-pinned-linux/pinned.out" | grep -a -o "loc=([^)]*) rot=([^)]*)")
  errs=$(grep -a -o "UC2GLDrv: [0-9]* GL errors in [0-9]* frames" "$out/smoke-pinned-wine/pinned.out" | tail -1)
  cmp=$(python3 - "$lp" "$wp" "$out/smoke-pinned-diff.png" <<'PY' 2>&1
import sys, math
from PIL import Image, ImageChops
a = Image.open(sys.argv[1]).convert('RGB'); b = Image.open(sys.argv[2]).convert('RGB')
if a.size != b.size: print("SIZE %s %s" % (a.size, b.size)); sys.exit(0)
d = ImageChops.difference(a, b); r, g, bl = d.split()
mx = ImageChops.lighter(ImageChops.lighter(r, g), bl)
d.point(lambda v: min(255, 4 * v)).save(sys.argv[3])
w, h = a.size; px = mx.load()
# The player model and the two pickups of this pinned view (they animate with the time since the spawn,
# which differs between two runs): x 60..395, y >= 265 of the 640x480 frame.
static = moving = same_static = exact = 0; sq = 0
for y in range(h):
    for x in range(w):
        v = px[x, y]; sq += v * v; exact += (v == 0)
        if 60 * w // 640 <= x <= 395 * w // 640 and y >= 265 * h // 480: moving += 1; continue
        # ... and the aiming reticle in the middle of the screen, which animates too (x 280..360, y 210..265).
        if 280 * w // 640 <= x <= 360 * w // 640 and 210 * h // 480 <= y < 265 * h // 480: moving += 1; continue
        static += 1; same_static += (v <= 2)
mse = sq / float(w * h)
print("OK %.3f %.1f %.2f" % (100.0 * same_static / static, 100.0 * exact / (w * h), 99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse)))
PY
)
  set -- $cmp
  if [ "${1-}" = OK ] && [ -n "$vw" ] && [ "$vw" = "$vl" ] && [[ "$errs" == "UC2GLDrv: 0 GL errors in "* ]] \
     && awk -v s="$2" 'BEGIN{exit !(s>=99.9)}' && [ "$(crit "$out/smoke-pinned-wine/pinned.out")" = 0 ]; then
    result PASS pinned "wine and Linux at the same $vw; static part ${2}% within 2 levels, whole frame ${3}% bit-identical, PSNR ${4} dB (smoke-pinned-diff.png); wine ${errs#UC2GLDrv: }, Critical 0"
  else
    result FAIL pinned "wine '$vw' vs Linux '$vl'; comparison: $cmp; wine '${errs:-no GL summary}'; runner: $line"
  fi
}

check_nosound() {	# no OpenAL32.dll anywhere: the game must still come up, silently
  local o=$out/smoke-nosound.out
  mv "$sys/OpenAL32.dll" "$sys/OpenAL32.dll.off"
  client nosound 30 -log -nullrenderer
  mv "$sys/OpenAL32.dll.off" "$sys/OpenAL32.dll"
  if grep -a -q "UC2ALAudio: no OpenAL library (OpenAL32.dll) beside the game or on the system: sound is off" "$o" \
     && grep -a -q "Bringing Level Menu.MyLevel up for play" "$o" && clean_exit "$o" && [ "$(crit "$o")" = 0 ]; then
    result PASS nosound "without OpenAL32.dll: 'sound is off', menu up, exit 0, Critical 0"
  else
    result FAIL nosound "exit $rc; $(grep -a 'UC2ALAudio' "$o" | head -1); Critical $(crit "$o")"
  fi
}

check_netjoin() {	# the Linux client joins the server running under wine (UDP on 127.0.0.1, port 7797)
  local so=$out/smoke-netjoin-server.out sl=$out/smoke-netjoin-server.log co=$out/smoke-netjoin-client.out spid
  if [ ! -x "$ref/uc2.bin" ]; then result SKIP netjoin "no Linux reference tree at $ref"; return; fi
  rm -f "$sys/ucc.log"
  UC2_WINE_STOP_AFTER=75 "$capped" 3G 150 -- "$wine" "$sys" ucc.exe server DM-Deadbolt PORT=7797 -log > "$so" 2>&1 &
  spid=$!
  sleep 25
  ( cd "$ref" && "$capped" 2G 70 -- xvfb-run -a -s "-screen 0 1280x720x24" \
      env SDL_AUDIODRIVER=dummy timeout -s INT 40 ./uc2.bin 127.0.0.1:7797 -log -nullrenderer ) > "$co" 2>&1
  wait $spid
  cp -f "$sys/ucc.log" "$sl" 2>/dev/null
  # PASS = the handshake through the wine server's WinSock (HELLO, CHALLENGE, LOGIN, WELCOME) and a clean server
  # shutdown afterwards. The join itself does not complete yet on ANY platform: the client then fails
  # check(GObjBeginLoadCount) in UObject::GetPackageLinker (the same between two Linux builds; BLOCKERS.md), which
  # the result line reports.
  local asrt; asrt=$(grep -a -c "Assertion failed: GObjBeginLoadCount" "$co")
  if grep -a -q "Login request: " "$sl" && grep -a -q "Welcomed by server: LEVEL=DM-Deadbolt" "$co" \
     && grep -a -q "Log file closed" "$sl" && [ "$(crit "$sl")" = 0 ]; then
    result PASS netjoin "handshake Linux uc2.bin -> wine ucc.exe server over WinSock: server 'Login request', client 'Welcomed by server: LEVEL=DM-Deadbolt'; server shut down cleanly, Critical 0; join then stops at the known client assertion (GObjBeginLoadCount, $asrt lines; same Linux<->Linux)"
  else
    result FAIL netjoin "server: $(grep -a -c 'Login request' "$sl" 2>/dev/null) login requests, client: $(grep -a -c 'Welcomed by server' "$co") welcomes, server Critical $(crit "$sl"); see $sl, $co"
  fi
}

check_audiocheck() {
  local o=$out/smoke-audiocheck.out rc
  "$capped" 3G 900 -- "$wine" "$sys" ucc.exe UC2ALAudio.UC2AudioCheck > "$o" 2>&1; rc=$?
  if [ $rc = 0 ] && grep -a -q "^UC2AudioCheck: .* 0 failures" "$o"; then
    result PASS audiocheck "$(grep -a '^UC2AudioCheck:' "$o"); $(grep -a -o 'waves: [0-9]* decoded to their exact length of [0-9]* cooked' "$o")"
  else
    result FAIL audiocheck "exit $rc; $(grep -a '^UC2AudioCheck:' "$o")"
  fi
}

check_selftest() {
  local o=$out/smoke-selftest.out rc
  "$capped" 3G 900 -- "$wine" "$sys" ucc.exe UC2ALAudio.UC2AudioSelfTest > "$o" 2>&1; rc=$?
  if [ $rc = 0 ] && grep -a -q "^UC2AudioSelfTest: 39 checks, 0 failed" "$o"; then
    result PASS selftest "$(grep -a '^UC2AudioSelfTest:' "$o" | tail -1)"
  else
    result FAIL selftest "exit $rc; $(grep -a '^UC2AudioSelfTest:' "$o" | tail -1); $(grep -a -c '^FAIL' "$o") FAIL lines"
  fi
}

check_capture() {
  local o=$out/smoke-capture.out wav=$out/smoke-capture.wav m
  rm -f "$wav"
  CLIENT_DEVSOUND=1 client capture 60 -log -audiowave="$wav"
  m=$(python3 "$repo/tools/dev/audio-music-check.py" "$wav" "$work/epic-sources/Music/NuBattle.wav" -19.0 \
      "$work/epic-sources/Music/Recoil.wav" 2>&1); local mrc=$?
  "$repo/tools/dev/audio-capture-report.sh" "$wav" > "$out/smoke-capture.report.txt" 2>&1
  if [ $mrc = 0 ] && grep -a -q 'UC2ALAudio: .*ALSOFT' "$o" && clean_exit "$o" && [ "$(crit "$o")" = 0 ]; then
    result PASS capture "$(grep -a -o '"OpenAL Soft on [^"]*"' "$o" | head -1) -> smoke-capture.wav; ${m#CHECK }; exit 0, Critical 0"
  else
    result FAIL capture "music check exit $mrc: $m; uc2.exe exit $rc, Critical $(crit "$o")"
  fi
}

check_controls() {	# keyboard | pad
  local line
  line=$(m10_wine "$1" 2>&1 | grep -a "^PASS\|^FAIL" | tail -1)
  case "$line" in
    PASS*) result PASS "$1" "M10 scenario under wine: ${line#PASS $1 }" ;;
    *) result FAIL "$1" "M10 scenario under wine: ${line:-no result} (see $out/smoke-controls/$1.result)" ;;
  esac
}

check_leftovers() {
  local left=""
  for p in $(pgrep -x wineserver); do
    tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -a -q "^WINEPREFIX=$prefix$" && left="$left wineserver:$p"
  done
  for p in $(pgrep -f 'uc2\.exe|ucc\.exe'); do
    case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"|"$sys"/*) left="$left $p";; esac
  done
  if [ -z "$left" ]; then result PASS leftovers "no wineserver of $prefix, no uc2.exe/ucc.exe of run/m12 running"
  else result FAIL leftovers "still running:$left"; fi
}

all="verify server netjoin null gl pinned audiocheck selftest capture nosound keyboard pad leftovers"
: > "$summary"
for c in ${@:-$all}; do
  case "$c" in
    verify|server|netjoin|null|gl|pinned|audiocheck|selftest|capture|nosound|leftovers) "check_$c" ;;
    keyboard|pad) check_controls "$c" ;;
    *) echo "unknown check $c (known: $all)" >&2 ;;
  esac
done
prep "$sys"
echo "summary: $(grep -c '^PASS' "$summary") PASS, $(grep -c '^FAIL' "$summary") FAIL, $(grep -c '^SKIP' "$summary") SKIP ($summary)"
