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

# ---------------------------------------------------------------------------------------------------------------
# Scenarios. Names of checks are <scenario>-<what>; each check is a log line ('expect'), the absence of one
# ('absent'), or a comparison of probe states ('check'); m10-inject.py documents the language.
# ---------------------------------------------------------------------------------------------------------------

# From the start screen to a deathmatch on DM-Deadbolt. $1 = check prefix; $2..$5 = the commands for
# "down", "accept" (A), "back" (B) and "start the match" (Fire).
menu_to_match() {
  local p=$1 down=$2 accept=$3 back=$4 fire=$5
  cat <<SCEN
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
$down
sleep 0.5
$accept
expect $p-main-menu-opens-arena OpenMenu \[UCMenu.UC2Arena_Main\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
$back
expect $p-back-closes-dialog CloseMenu \[GUIMessageBox\] 10
sleep 1.5
seek 10 P['_'].item.startswith('DeathMatch|') ;; $down
$accept
sleep 2
$accept
sleep 2
seek 40 'DM-Deadbolt' in P['_'].item ;; $down
shot maplist
$accept
expect $p-map-chosen-opens-character OpenMenu \[UCMenu.UC2Arena_Character\] 15
sleep 2
$accept
expect $p-character-opens-armory OpenMenu \[UCMenu.UC2Arena_Armory\] 15
sleep 2
$accept
sleep 2
$accept
expect $p-armory-opens-launch-confirm OpenMenu \[UCMenu.ConfirmLaunchMenu\] 15
sleep 1.5
$accept
expect $p-travel-to-deathmatch Game class is 'UCDeathmatch' 120
expect $p-deadbolt-up Bringing Level DM-Deadbolt.MyLevel up for play 60
sleep 6
$fire
sleep 4
probe spawn
check $p-match-started-pawn-spawned P['spawn'].pawn==1 and P['spawn'].level=='DM-Deadbolt'
shot match
SCEN
}

scen_keyboard() {
  menu_to_match kb "key Down" "key Return" "key Escape" "key Alt_L 0.2"
  cat <<'SCEN'
probe f0
keydown w
sleep 1.5
keyup w
sleep 0.5
probe f1
check kb-w-moves-forward fwd('f0','f1') > 150 and abs(side('f0','f1')) < 0.5*fwd('f0','f1')
probe t0
keydown Right
sleep 1
keyup Right
sleep 0.3
probe t1
check kb-right-arrow-turns-right yawdiff('t0','t1') > 4000
probe w0
key e
sleep 2.5
probe w1
check kb-e-switches-to-melee P['w1'].weapon.startswith('UCWeap_Melee') and not P['w0'].weapon.startswith('UCWeap_Melee')
shot melee
key q
sleep 2.5
probe w2
check kb-q-switches-back-to-ranged P['w2'].weapon == P['w0'].weapon
probe a0
key Alt_L 0.3
sleep 1.5
probe a1
check kb-alt-fires P['a1'].shots != P['a0'].shots
key Escape
expect kb-escape-opens-pause-menu OpenMenu 10
sleep 1.5
probe p1
check kb-pause-menu-up P['p1'].menu != 'None'
shot pause
key Escape
sleep 2
probe p2
check kb-escape-closes-pause-menu P['p2'].menu == 'None'
SCEN
}

# Mouse in the menus (window 640x480: main menu items at x 140, y 146 Story, 184 Instant Action, 299 Options; Arena gametype rows y 90 CTF, 109 DeathMatch) and mouse look in the match.
scen_mouse() {
  cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
move 140 146
sleep 0.5
move 140 299
expect ms-hover-selects-item Mouse hover selects UC2MainMenu 5
sleep 0.5
key Return
expect ms-hovered-item-is-the-one-enter-opens OpenMenu \[UCMenu.UC2Settings_Main\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
click 3
expect ms-right-click-answers-no CloseMenu \[GUIMessageBox\] 10
sleep 1.5
click 3
expect ms-right-click-is-back CloseMenu \[UC2Settings_Main\] 10
sleep 3
mark
click 1 140 184
expect ms-click-opens-item-under-cursor OpenMenu \[UCMenu.UC2Arena_Main\] 15
absent ms-click-does-not-open-focused-item OpenMenu \[UCMenu.UC2Settings_Main\]
sleep 3
probe g0
move 130 90
sleep 0.3
click 1 130 109
expect ms-click-selects-list-row Mouse click selects the row under the cursor in UC2Arena_Main 5
sleep 2
probe g1
shot gametype-clicked
key Escape
sleep 1.5
probe g2
check ms-click-on-row-accepts-that-row P['g0'].item.startswith('Capture') and P['g1'].item == '' and P['g2'].item.startswith('DeathMatch|')
shot gametype
key Return
sleep 2
key Return
sleep 2
probe m0
click 5
sleep 0.5
click 5
sleep 0.5
probe m1
check ms-wheel-down-moves-list P['m1'].item != P['m0'].item and P['m0'].item != ''
click 4
sleep 0.5
click 4
sleep 0.5
probe m2
check ms-wheel-up-moves-back P['m2'].item == P['m0'].item
seek 40 'DM-Deadbolt' in P['_'].item ;; click 5
key Return
wait OpenMenu \[UCMenu.UC2Arena_Character\] 15
sleep 2
key Return
wait OpenMenu \[UCMenu.UC2Arena_Armory\] 15
sleep 2
key Return
sleep 2
key Return
wait OpenMenu \[UCMenu.ConfirmLaunchMenu\] 15
sleep 1.5
key Return
expect ms-deadbolt-up Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
click 1
sleep 4
probe spawn
check ms-left-click-starts-match P['spawn'].pawn==1
probe s0
click 1 0.3
sleep 1.5
probe s1
check ms-left-click-fires P['s1'].shots != P['s0'].shots
probe l0
rmove 200 0 20
sleep 0.5
probe l1
check ms-mouse-right-turns-right yawdiff('l0','l1') > 0
check ms-mouse-turn-amount 0.6*200*65.5*0.94 < yawdiff('l0','l1') < 1.4*200*65.5
rmove -200 0 20
sleep 0.5
probe l2
check ms-mouse-left-turns-back abs(yawdiff('l0','l2')) < 0.25*yawdiff('l0','l1')
rmove 0 -100 10
sleep 0.5
probe l3
check ms-mouse-up-looks-up pitchdiff('l2','l3') > 0
chan exec set UCPlayerInput bInvertMouse True
sleep 0.5
rmove 0 -100 10
sleep 0.5
probe l4
check ms-invert-option-flips-pitch pitchdiff('l3','l4') < 0
chan exec set UCPlayerInput bInvertMouse False
shot look
SCEN
}

run_scenario() {
  local name=$1 secs=$2 f
  f=$(mktemp)
  "scen_$name" > "$f"
  cp "$f" "$out/$name.scenario"
  run_one "$name" "$f" "$secs"
  rm -f "$f"
}

all="keyboard mouse"
for s in ${@:-$all}; do
  case "$s" in
    keyboard) run_scenario keyboard 300 ;;
    mouse) run_scenario mouse 360 ;;
    *) echo "unknown scenario $s (known: $all)" >&2 ;;
  esac
done
