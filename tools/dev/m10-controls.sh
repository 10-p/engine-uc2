#!/usr/bin/env bash
# M10 controls test: keyboard, mouse, text entry and game pad, headless and repeatable.
#
#   tools/dev/m10-controls.sh                    every scenario, one PASS/FAIL line each
#   tools/dev/m10-controls.sh keyboard pad       only these scenarios
#   tools/dev/m10-controls.sh --file <scenario> <name> [uc2.bin arguments...]   one ad-hoc scenario file
#
# Needs the run tree <WorkUC2>/run/m10 (tools/dev/mkrun-uc2.sh build-native m10). Each scenario is one game
# run under Xvfb inside <WorkUC2>/bin/capped 2G (CLAUDE.md, "Running anything: memory ceiling, always"); the
# keyboard and mouse are real X events (XTEST, tools/dev/m10-inject.py), the pad is a virtual SDL game
# controller driven through the engine's test channel (-inputtest=, Source/SDL2Drv/Src/SDL2InputTest.h), so
# the path from SDL onwards is the real one. PASS/FAIL comes from engine log lines and probe state (location,
# rotation, ammo, menu page, text-field contents), never from a picture; screenshots are taken for people.
#
# Output in <WorkUC2>/logs/m10/: <scenario>.out (engine log), <scenario>.result (every check),
# <scenario>-<shot>.png, <scenario>.inject (runner transcript).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
sys=$work/run/m10/System
out=$work/logs/m10
capped=$work/bin/capped
inject=$repo/tools/dev/m10-inject.py
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native m10)" >&2; exit 2; }

# Fresh configuration for every run: our tracked inis, the derived ones and saved profiles removed. The test
# also un-suppresses the DevUI/DevGUI log names (the GUI's own "OpenMenu"/"handled XC_*" lines) in the run
# tree's copy only.
prep_tree() {
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp
  rm -rf "$sys/Profiles" "$work/run/m10/Save"/*.pro 2>/dev/null
  mkdir -p "$sys/Profiles"
}

# run_one <name> <scenario-file> <seconds> [uc2.bin args...]
run_one() {
  local name=$1 scen=$2 secs=$3; shift 3
  prep_tree
  local chan=$out/$name.channel inner
  : > "$chan"
  rm -f "$out/$name".out "$out/$name".result "$out/$name".result.shots "$out/$name"-*.png
  inner=$(mktemp)
  cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy ./uc2.bin -log -inputtest="$chan" "\$@" > "$out/$name.out" 2>&1 &
pid=\$!
python3 "$inject" "$scen" "$out/$name.out" "$chan" "$out/$name.result" > "$out/$name.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
exit 0
INNER
  "$capped" 2G "$secs" -- xvfb-run -a -s "-screen 0 1280x720x24" bash "$inner" "$@"
  local rc=$?
  rm -f "$inner"
  # Screenshots: SHOT writes System/ShotNNNNN.bmp in order; the runner listed the names.
  if [ -f "$out/$name.result.shots" ]; then
    local i=0 shot
    while read -r shot; do
      local bmp; bmp=$(printf "%s/Shot%05d.bmp" "$sys" "$i")
      [ -f "$bmp" ] && python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$bmp" "$out/$name-$shot.png"
      i=$((i+1))
    done < "$out/$name.result.shots"
    rm -f "$sys"/Shot*.bmp
  fi
  local crit; crit=$(grep -a -c "Critical" "$out/$name.out" 2>/dev/null || true)
  local fails passes
  fails=$(grep -c "^FAIL" "$out/$name.result" 2>/dev/null || true)
  passes=$(grep -c "^PASS" "$out/$name.result" 2>/dev/null || true)
  [ -f "$out/$name.result" ] || fails=1
  if [ "${fails:-1}" = 0 ] && [ "${crit:-0}" = 0 ] && [ "${passes:-0}" -gt 0 ]; then
    echo "PASS $name ($passes checks, 0 Critical, capped exit $rc)"
  else
    echo "FAIL $name ($passes passed, $fails failed, $crit Critical, capped exit $rc; see $out/$name.result)"
  fi
  for p in $(pgrep -x uc2.bin) $(pgrep -x Xvfb); do
    case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"|"$sys"/*) echo "WARNING: left running: $p";; esac
  done
}

if [ "${1-}" = "--file" ]; then
  scen=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); name=$3; shift 3
  run_one "$name" "$scen" 240 "$@"
  exit 0
fi

echo "m10-controls: no scenarios defined yet" >&2
exit 2
