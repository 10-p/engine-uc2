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
# "down", "accept" (A), "back" (B) and "start the match" (Fire); $6 = noboot when the caller already waited
# for the main menu.
menu_to_match() {
  local p=$1 down=$2 accept=$3 back=$4 fire=$5
  [ "${6-}" = noboot ] || printf '%s\n' 'wait OpenMenu \[UCMenu.UC2MainMenu\] 90' 'sleep 3' 'focus'
  cat <<SCEN
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
wait test: exec set UCPlayerInput bInvertMouse True 10
sleep 0.5
rmove 0 -100 10
sleep 0.5
probe l4
check ms-invert-option-flips-pitch pitchdiff('l3','l4') < 0
chan exec set UCPlayerInput bInvertMouse False
shot look
SCEN
}

# Text entry: a new profile's name on the on-screen keyboard page (UC2ProfileKeyboard), typed on the real keyboard.
# The letters are the ones the menus otherwise use as pad keys (W A S D) or Epic's table used (K X Y B).
scen_text() {
  cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
key Down
key Down
key Down
sleep 0.5
key Return
expect tx-profiles-opens OpenMenu \[UCMenu.UC2Profile\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
key Return
expect tx-on-screen-keyboard-opens OpenMenu \[UCMenu.UC2ProfileKeyboard\] 15
sleep 3
probe k0
shot keyboard
mark
type WaSdKxYb
sleep 1
probe k1
check tx-typed-exactly-those-characters P['k1'].text == 'WaSdKxYb'
absent tx-typing-does-not-navigate handled XC_
key BackSpace
sleep 0.5
probe k2
check tx-backspace-deletes-one P['k2'].text == 'WaSdKxY'
key b
sleep 0.5
probe k3
check tx-typing-again P['k3'].text == 'WaSdKxYb'
shot typed
key Return
expect tx-enter-finishes-entry CloseMenu \[UC2ProfileKeyboard\] 10
sleep 3
probe k4
shot after
SCEN
}

# The virtual pad (SDL virtual joystick through the engine's test channel): hot-plug after start, the same menu
# path and match checks, trigger threshold and stick dead zone just below/above, rumble reaching the device,
# unplug/replug in the match. SDL stick Y is positive down: lefty -32767 = stick up.
scen_pad() {
  cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 2
focus
probe nopad
check pd-no-pad-at-start P['nopad'].pads == 0
pad attach
expect pd-hotplug-attach-after-start SDL2 pads: pad 0 connected 10
expect pd-attach-sends-joyslider1 pad 0 JoySlider1 press 5
sleep 1
SCEN
  menu_to_match pd "padpress dpdown" "padpress a" "padpress b" "padtrigger righttrigger" noboot
  cat <<'SCEN'
probe f0
pad axis lefty -32767
sleep 1.5
pad axis lefty 0
sleep 0.5
probe f1
check pd-left-stick-moves-forward fwd('f0','f1') > 150 and abs(side('f0','f1')) < 0.5*fwd('f0','f1')
probe h0
pad axis lefty -19661
sleep 1.5
pad axis lefty 0
sleep 0.5
probe h1
check pd-stick-at-0.6-moves-forward fwd('h0','h1') > 100
probe z0
pad axis leftx 6554
sleep 1.2
pad axis leftx 0
sleep 0.3
probe z1
check pd-stick-inside-dead-zone-does-nothing dist('z0','z1') < 5
pad axis leftx 9830
sleep 1.2
pad axis leftx 0
sleep 0.3
probe z2
check pd-stick-just-outside-dead-zone-moves dist('z1','z2') > 5
pad axis leftx 32767
sleep 1.2
pad axis leftx 0
sleep 0.3
probe z3
pad axis leftx -32767
sleep 1.2
pad axis leftx 0
sleep 0.3
probe z4
# A wall may stop one direction (it did once, beside the spawn point); a wrong sign fails both.
check pd-left-stick-x-strafes-the-right-way side('z2','z3') > 100 or side('z3','z4') < -100
check pd-left-stick-x-never-strafes-the-wrong-way side('z2','z3') > -20 and side('z3','z4') < 20
probe t0
pad axis rightx 32767
sleep 1
pad axis rightx 0
sleep 0.3
probe t1
check pd-right-stick-turns-right yawdiff('t0','t1') > 8000
probe a0
pad axis righttrigger 3700
sleep 0.4
pad axis righttrigger 0
sleep 0.8
probe a1
check pd-trigger-below-threshold-does-not-fire P['a1'].shots == P['a0'].shots
mark
pad axis righttrigger 4200
sleep 0.4
pad axis righttrigger 0
expect pd-trigger-above-threshold-pressed pad 0 button 7 value 3[1-9] > 30 5
expect pd-rumble-reaches-the-device virtual pad: rumble request reached the device: low [1-9] 5
expect pd-rumble-sent-to-sdl pad 0 rumble [1-9][0-9]* [0-9]+ -> SDL_GameControllerRumble 0 5
sleep 0.8
probe a2
check pd-trigger-above-threshold-fires P['a2'].shots != P['a1'].shots
probe w0
padpress b
sleep 2.5
probe w1
check pd-b-switches-to-melee P['w1'].weapon.startswith('UCWeap_Melee')
padpress y
sleep 2.5
probe w2
check pd-y-switches-back-to-ranged P['w2'].weapon == P['w0'].weapon
padpress start
expect pd-start-opens-pause-menu OpenMenu \[UCMenu.InGame_MainMenu\] 10
sleep 1.5
padpress b
sleep 2
probe p2
check pd-b-closes-pause-menu P['p2'].menu == 'None'
pad detach
expect pd-unplug-sends-joyslider2 pad 0 JoySlider2 press 5
expect pd-unplug-disconnects SDL2 pads: pad 0 disconnected 5
sleep 3
probe u1
check pd-unplug-pauses-with-controller-removed-menu P['u1'].menu == 'ControllerRemovedMenu' and P['u1'].paused == 1
shot unplugged
pad attach
expect pd-replug-connects SDL2 pads: pad 0 connected 5
sleep 3
probe u2
check pd-alive-after-replug P['u2'].level == 'DM-Deadbolt'
padpress start
sleep 2
probe u3
check pd-start-after-replug-resumes P['u3'].menu == 'None' and P['u3'].paused == 0
shot replugged
SCEN
}

# Keyboard, mouse and pad interleaved: menu steps alternate between them, then in the match each one's input
# must survive the other's (the pad sends its sticks every tick even at rest).
scen_both() {
  cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 2
focus
pad attach
wait SDL2 pads: pad 0 connected 10
sleep 1
key Down
sleep 0.5
padpress a
expect both-key-down-then-pad-a-opens-arena OpenMenu \[UCMenu.UC2Arena_Main\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
key Escape
expect both-escape-closes-dialog CloseMenu \[GUIMessageBox\] 10
sleep 1.5
seek 10 P['_'].item.startswith('DeathMatch|') ;; padpress dpdown
key Return
sleep 2
padpress a
sleep 2
seek 40 'DM-Deadbolt' in P['_'].item ;; key Down
padpress a
expect both-pad-a-opens-character OpenMenu \[UCMenu.UC2Arena_Character\] 15
sleep 2
key Return
expect both-enter-opens-armory OpenMenu \[UCMenu.UC2Arena_Armory\] 15
sleep 2
padpress a
sleep 2
key Return
expect both-launch-confirm OpenMenu \[UCMenu.ConfirmLaunchMenu\] 15
sleep 1.5
padpress a
expect both-deadbolt-up Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
padtrigger righttrigger
sleep 4
probe spawn
check both-match-started P['spawn'].pawn==1
probe b0
keydown w
sleep 1.5
keyup w
sleep 0.4
probe b1
check both-keyboard-moves-while-pad-idle fwd('b0','b1') > 150
probe b2
pad axis rightx 32767
keydown w
sleep 1
keyup w
pad axis rightx 0
sleep 0.4
probe b3
check both-keyboard-move-and-pad-turn-together dist('b2','b3') > 100 and yawdiff('b2','b3') > 4000
probe b4
pad axis lefty -32767
rmove 150 0 15
key Alt_L 0.3
sleep 0.5
pad axis lefty 0
sleep 0.4
probe b5
check both-pad-stick-moves-during-mouse-and-keys dist('b4','b5') > 30
check both-mouse-turns-during-pad-move yawdiff('b4','b5') > 3000
check both-keyboard-fires-during-pad-move P['b5'].shots != P['b4'].shots
key Escape
expect both-escape-opens-pause-menu OpenMenu \[UCMenu.InGame_MainMenu\] 10
sleep 1.5
padpress b
sleep 2
probe p1
check both-pad-b-closes-it P['p1'].menu == 'None'
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

all="keyboard mouse text pad both"
for s in ${@:-$all}; do
  case "$s" in
    keyboard) run_scenario keyboard 300 ;;
    mouse) run_scenario mouse 360 ;;
    text) run_scenario text 180 ;;
    pad) run_scenario pad 360 ;;
    both) run_scenario both 300 ;;
    *) echo "unknown scenario $s (known: $all)" >&2 ;;
  esac
done
