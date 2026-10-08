#!/usr/bin/env bash
# D-9 check (native): the KEYBOARD / MOUSE settings page (UCMenu.UC2Settings_Keys): rebinding keys in game, the mouse
# sensitivity slider and invert, persistence across a restart. One PASS/FAIL line per check on stdout, then a summary.
#
#   tools/dev/d9-rebind.sh [scenario...]        (default: menu match restart pad)
#
#   menu     from the main menu (OPTIONS -> KEYBOARD / MOUSE, software GL, 640x480): the page opens with DefUser.ini's
#            keys; the slider by mouse (a click on the bar, on its plus end) and CANCEL; the slider by keyboard to 5.0;
#            mouse invert on; a click on a key cell captures; a capture takes letters raw (S does not move the
#            selection); JUMP / DODGE := J (J's old command replaced); a conflict (MOVE FORWARD := S, MOVE BACK gets
#            W); Escape cancels a capture and keeps the page; the console key opens the console during a capture
#            and the capture waits on; a mouse button (JUMP ONLY :=
#            mouse button 4); clearing with X, with Delete, with Backspace in a capture; a fixed row; leaving saves;
#            the live bindings; the page reopens with them; then User.ini: exactly the changed keys differ from
#            DefUser.ini, the pad's keys are untouched, the mouse settings are there
#   match    a NEW process on the same User.ini (the restart): DM-Deadbolt, null renderer: the bindings and the mouse
#            settings are back; J jumps and Space does not; mouse button 4 jumps; S walks forward and W back; the mouse
#            turns twice the default per count (sensitivity 5.0, d6-mouse.sh's measurement) and up looks down
#            (invert); then from the PAUSE menu (OPTIONS -> KEYBOARD / MOUSE): the page shows them, DEFAULTS and CANCEL
#            and DEFAULTS again, leave: Space jumps, J does not, the mouse turns the default amount and up looks up
#   restart  another new process: the defaults persisted (live bindings and User.ini = DefUser.ini's, mouse 2.5)
#   pad      the page with the game pad (virtual SDL pad): D-pad to the list, A captures, a pad button cancels a
#            capture, the keyboard key goes in, D-pad right picks key 2, B leaves and saves
#   look     pictures only (no checks of their own): the page from the main menu at 640x480 and 1920x1080 (the D-2 box),
#            the list, a capture, and the page from the pause menu in a match
#
# Run tree: D9_TREE (default d9; tools/dev/mkrun-uc2.sh build-native d9 with the rebuilt UCMenu.u: UC2_SCRIPTS=<dir with
# UCMenu.u>). Every game run is tools/dev/m10-controls.sh --file (Xvfb inside <WorkUC2>/bin/capped 2G). Output: D9_OUT
# (default <WorkUC2>/logs/d9/rebind): <scenario>.out (engine log), .result, .inject, <scenario>-<shot>.png.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${D9_TREE:-d9}
out=${D9_OUT:-$work/logs/d9/rebind}
sys=$work/run/$tree/System
m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree $work/run/$tree (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }
# Turn per mouse count at FOV 90 with MouseLookSensitivity 2.5 (D-6): 0.022 deg x 2.5 x 65536/360
K=10.012444

# fresh inis (ours), DevUI unsuppressed (the GUI's and the page's log lines), no User.ini, no profiles
prep() {
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp
  rm -rf "$sys/Profiles" "$work/run/$tree/Save"/*.pro 2>/dev/null
  mkdir -p "$sys/Profiles"
}

# run <name> <seconds> [m10 environment...] -- [engine arguments...]: one game run of $out/<name>.scen on the tree as it is
run() {
  local name=$1 secs=$2; shift 2
  local envs=()
  while [ $# -gt 0 ] && [ "$1" != "--" ]; do envs+=("$1"); shift; done
  [ "${1:-}" = "--" ] && shift
  env "${envs[@]}" M10_KEEP=1 M10_TREE=$tree M10_OUT=$out M10_SECS=$secs "$m10" --file "$out/$name.scen" "$name" "$@" | sed 's/^/    /'
  [ -f "$out/$name.result" ] || return 0
  sed -n 's/^\(PASS\|FAIL\)\t\([^\t]*\)\t\(.*\)$/\1 \2: \3/p' "$out/$name.result" | cut -c1-240
}

# mouse lines: mlines <dx-total> <dy-total> <per-tick> (one channel line per tick, exact total; d6-mouse.sh's)
mlines() {
  python3 - "$@" <<'PY'
import sys
dx, dy, step = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
n = max(1, -(-max(abs(dx), abs(dy)) // step))
px = py = 0
for i in range(1, n + 1):
    x, y = round(dx * i / n), round(dy * i / n)
    print(f'chan mouse {x - px} {y - py}'); px, py = x, y
PY
}

# the main menu -> OPTIONS (a "no profile" box: no) -> KEYBOARD / MOUSE
menu_to_page() {
  cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
move 140 146
sleep 0.5
move 140 299
wait Mouse hover selects UC2MainMenu 5
sleep 0.5
key Return
expect $P-options-opens OpenMenu \[UCMenu.UC2Settings_Main\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
key Escape
wait CloseMenu \[GUIMessageBox\] 10
sleep 2
seek 8 'KEYBOARD' in P['_'].caption ;; key Down
check $P-button-on-options P['_'].caption == 'KEYBOARD / MOUSE'
key Return
expect $P-page-opens-from-main-menu OpenMenu \[UCMenu.UC2Settings_Keys\] 15
expect $P-page-loads-the-keys KeyBinds: rows \(loaded\): 15
sleep 2.5
SCEN
}

# a seek to the row <caption>|<index> with Up or Down
row() { echo "seek 40 P['_'].item == '$1' ;; key $2"; }

# the in-game start: spawn (Alt = Fire), God
start_match() {
  cat <<'SCEN'
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 4
focus
key Alt_L 0.3
sleep 3
tryseek 6 P['_'].pawn == 1 ;; key Alt_L 0.3
probe spawn
check $P-spawned P['spawn'].pawn==1 and P['spawn'].level=='DM-Deadbolt'
chan exec God
wait test: exec God 10
SCEN
}

# jump check: jumps <name> <key-command> <expect-jump 1|0>
jumpcheck() {
  local name=$1 cmd=$2 want=$3
  cat <<SCEN
sleep 1.5
probe ${name}a
$cmd
sleep 0.35
probe ${name}b
SCEN
  if [ "$want" = 1 ]; then
    echo "check \$P-$name P['${name}b'].loc[2] - P['${name}a'].loc[2] > 30"
  else
    echo "check \$P-$name abs(P['${name}b'].loc[2] - P['${name}a'].loc[2]) < 5"
  fi
}

# mouse turn per count: turn <name> <counts> <expected units per count>
turncheck() {
  local name=$1 n=$2 k=$3
  echo "sleep 0.5"
  echo "probe ${name}a"
  mlines "$n" 0 25
  echo "sleep 0.5"
  echo "probe ${name}b"
  echo "check \$P-$name abs(yawdiff('${name}a','${name}b') - $n*$k) < 0.01*$n*$k"
}

# the pause menu (Escape) -> OPTIONS (a "no profile" box: no) -> KEYBOARD / MOUSE
pause_to_page() {
  cat <<'SCEN'
key Escape
expect $P-pause-menu-opens OpenMenu \[UCMenu.InGame_MainMenu\] 15
sleep 2
seek 6 'OPTIONS' in P['_'].caption ;; key Down
key Return
expect $P-pause-options-opens OpenMenu \[UCMenu.UC2Settings_MidGame\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
key Escape
wait CloseMenu \[GUIMessageBox\] 10
sleep 2
seek 10 'KEYBOARD' in P['_'].caption ;; key Down
check $P-button-on-pause-options P['_'].caption == 'KEYBOARD / MOUSE'
key Return
expect $P-page-opens-from-pause-menu OpenMenu \[UCMenu.UC2Settings_Keys\] 15
SCEN
}

# leave the menus back to the game (the pause menu's close animation counts frames: wait for it)
back_to_game() {
  cat <<'SCEN'
sleep 1.5
key Escape
sleep 2
key Escape
sleep 2
tryseek 20 P['_'].menu == 'None' ;; sleep 0.5
probe back
check $P-back-in-the-game P['back'].menu == 'None'
SCEN
}

# ---------------------------------------------------------------------------------------------------------------
scen_menu() {
  menu_to_page
  cat <<'SCEN'
probe k0
check $P-slider-has-the-focus P['k0'].caption.strip() == 'MOUSE SENSITIVITY 2.5'
keys k0 Escape Joy1 Joy2 Joy7 Joy8 Joy13 JoyX JoyY JoyU JoyV
shot page
# the slider by mouse: a click at three quarters of the bar, one on its plus end; CANCEL (Black: E) undoes them
click 1 410 123
sleep 0.5
probe s1
check $P-slider-click-on-bar-sets-value 6.0 <= float(P['s1'].caption.split()[-1]) <= 9.0
pyexec S1=float(P['s1'].caption.split()[-1])
click 1 517 123
sleep 0.5
probe s2
check $P-slider-click-on-plus-steps abs(float(P['s2'].caption.split()[-1]) - S1 - 0.1) < 0.01
key e
expect $P-cancel-undoes KeyBinds: CHANGES UNDONE 10
sleep 0.5
probe s3
check $P-cancel-restores-the-slider P['s3'].caption.strip() == 'MOUSE SENSITIVITY 2.5'
SCEN
  for i in $(seq 25); do echo "key Right"; echo "sleep 0.1"; done
  cat <<'SCEN'
sleep 0.5
probe s4
check $P-slider-by-keyboard-to-5 P['s4'].caption.strip() == 'MOUSE SENSITIVITY 5.0'
key Down
sleep 0.5
probe c0
check $P-invert-checkbox P['c0'].caption.strip() == 'INVERT MOUSE LOOK'
key Return
sleep 0.5
# a click on a key cell: STRAFE RIGHT's key 2 (row 4 of the list as it opens; 640x480 positions)
click 1 470 263
expect $P-click-on-cell-captures KeyBinds: PRESS A KEY 10
sleep 0.5
key g
expect $P-click-captured-g KeyBinds: G: STRAFE RIGHT\. \(IT WAS: Grab\) 10
sleep 0.5
probe l0
check $P-click-selected-that-row P['l0'].item == 'STRAFE RIGHT|3'
SCEN
  row 'MOVE FORWARD|0' Up
  cat <<'SCEN'
key Left
sleep 0.3
probe l1
key Return
expect $P-enter-captures KeyBinds: PRESS A KEY 10
expect $P-the-enter-is-not-the-key Raw key capture: ignored the release of Enter 10
sleep 0.3
key s
expect $P-conflict-swaps KeyBinds: S: MOVE FORWARD\. MOVE BACK MOVED TO W\. 10
sleep 0.5
probe l2
check $P-letters-do-not-navigate-in-a-capture P['l1'].item == 'MOVE FORWARD|0' and P['l2'].item == 'MOVE FORWARD|0'
shot conflict
SCEN
  row 'JUMP / DODGE|4' Down
  cat <<'SCEN'
key Left
key Return
wait KeyBinds: PRESS A KEY 10
sleep 0.3
key j
expect $P-jump-to-j KeyBinds: J: JUMP / DODGE\. \(IT WAS: Taunt Taunt1\) 10
sleep 0.5
key Right
key Return
wait KeyBinds: PRESS A KEY 10
sleep 0.3
shot capture
key Escape
expect $P-escape-cancels-a-capture KeyBinds: NOT CHANGED 10
sleep 1
probe e0
check $P-escape-in-a-capture-keeps-the-page P['e0'].menu == 'UC2Settings_Keys'
# the console key during a capture: the console (an interaction ahead of the menus) opens as anywhere else; Escape
# closes it (D-7: its release does not reach the page), the capture is still waiting, a second Escape cancels it
key Return
wait KeyBinds: PRESS A KEY 10
sleep 0.3
key grave 0.3
sleep 1.5
probe e1
check $P-console-key-opens-the-console P['e1'].console == 1 and P['e1'].menu == 'UC2Settings_Keys'
key Escape 0.3
sleep 1.5
probe e2
check $P-console-closes-page-stays P['e2'].console == 0 and P['e2'].menu == 'UC2Settings_Keys'
key Escape
expect $P-capture-still-waiting-after-the-console KeyBinds: NOT CHANGED 10
sleep 0.5
SCEN
  row 'JUMP ONLY|6' Down
  cat <<'SCEN'
key Right
key Return
wait KeyBinds: PRESS A KEY 10
sleep 0.3
click 8
expect $P-mouse-button-4 KeyBinds: MOUSE 4: JUMP ONLY\. 10
sleep 0.5
SCEN
  row 'CENTER VIEW|11' Down
  cat <<'SCEN'
key Right
key x
expect $P-x-clears KeyBinds: HOME CLEARED \(NO LONGER CENTER VIEW\) 10
sleep 0.5
SCEN
  row 'TAUNTS / VOICE|24' Down
  cat <<'SCEN'
key Left
key Delete
expect $P-delete-clears KeyBinds: V CLEARED \(NO LONGER TAUNTS / VOICE\) 10
sleep 0.5
SCEN
  row 'SCREENSHOT|30' Down
  cat <<'SCEN'
key Left
key Return
wait KeyBinds: PRESS A KEY 10
sleep 0.3
key BackSpace
expect $P-backspace-in-a-capture-clears KeyBinds: F9 CLEARED \(NO LONGER SCREENSHOT\) 10
sleep 0.5
SCEN
  row 'MENU|31' Down
  cat <<'SCEN'
key Return
expect $P-fixed-row KeyBinds: MENU CANNOT BE CHANGED HERE 10
sleep 0.5
shot list
mark
key Escape
expect $P-page-closes CloseMenu \[UC2Settings_Keys\] 10
expect $P-leaving-saves KeyBinds: saved 10 key\(s\) to User.ini 10
expect $P-saves-sensitivity KeyBinds: mouse sensitivity 5.00 saved 10
expect $P-saves-invert KeyBinds: mouse invert True saved 10
sleep 2
keys k1 J Space S W G Mouse4 V F9 Home Period
keys k2 Escape Joy1 Joy2 Joy7 Joy8 Joy13 JoyX JoyY JoyU JoyV
check $P-live-j K['k1']['J'] == 'SmartJump 0'
check $P-live-space-free K['k1']['Space'] == ''
check $P-live-swap K['k1']['S'] == 'MoveForward' and K['k1']['W'] == 'MoveBackward'
check $P-live-g-and-period-replaced K['k1']['G'] == 'StrafeRight' and K['k1']['Period'] == ''
check $P-live-mouse4 K['k1']['Mouse4'] == 'Jump'
check $P-live-cleared K['k1']['V'] == '' and K['k1']['F9'] == '' and K['k1']['Home'] == ''
check $P-live-escape-and-pad-untouched K['k2'] == K['k0'] and K['k2']['Escape'] == 'ShowMenu'
key Return
expect $P-page-reopens OpenMenu \[UCMenu.UC2Settings_Keys\] 15
expect $P-reopened-page-shows-them KeyBinds: rows \(loaded\): .*JUMP / DODGE=J,;.*JUMP ONLY=Backspace,Mouse4; 15
sleep 2
key Escape
sleep 2
SCEN
}

scen_match() {
  start_match
  cat <<'SCEN'
keys r0 J Space S W G Mouse4 V F9 Home
check $P-restart-keeps-the-keys K['r0']['J'] == 'SmartJump 0' and K['r0']['Space'] == '' and K['r0']['S'] == 'MoveForward' and K['r0']['W'] == 'MoveBackward' and K['r0']['Mouse4'] == 'Jump' and K['r0']['V'] == ''
probe m0
check $P-restart-keeps-the-mouse abs(float(P['m0'].sens) - 5.0) < 0.001 and P['m0'].invert == 'True'
SCEN
  jumpcheck j-jumps "key j" 1
  jumpcheck space-does-not-jump "key space" 0
  jumpcheck mouse4-jumps "click 8" 1
  cat <<'SCEN'
sleep 1.5
probe w0
keydown s
sleep 1
keyup s
sleep 0.3
probe w1
check $P-s-walks-forward P['w0'].loc != P['w1'].loc and fwd('w0','w1') > 150
keydown w
sleep 1
keyup w
sleep 0.3
probe w2
check $P-w-walks-back fwd('w1','w2') < -150
SCEN
  turncheck mouse-turns-twice-the-default 500 "$(python3 -c "print($K*2)")"
  cat <<'SCEN'
sleep 0.5
probe i0
chan mouse 0 -50
sleep 0.5
probe i1
check $P-mouse-up-looks-down-inverted pitchdiff('i0','i1') < 0
chan mouse 0 50
sleep 0.5
SCEN
  pause_to_page
  cat <<'SCEN'
expect $P-page-shows-the-saved-keys KeyBinds: rows \(loaded\): .*JUMP / DODGE=J,; 15
expect $P-page-shows-the-saved-mouse KeyBinds: mouse sensitivity 5.00 invert True 10
sleep 2
shot ingame-page
key q
expect $P-defaults-load KeyBinds: rows \(defaults\): .*JUMP / DODGE=Space,; 10
expect $P-defaults-mouse KeyBinds: mouse sensitivity 2.50 invert False 10
sleep 0.5
key e
expect $P-cancel-after-defaults KeyBinds: rows \(loaded\): .*JUMP / DODGE=J,; 10
sleep 0.5
key q
expect $P-defaults-again KeyBinds: rows \(defaults\): .*JUMP / DODGE=Space,; 10
sleep 0.5
key Escape
expect $P-defaults-saved KeyBinds: saved 10
SCEN
  back_to_game
  cat <<'SCEN'
keys r1 J Space S W G Mouse4 V F9 Home
check $P-defaults-live K['r1']['J'] == 'Taunt Taunt1' and K['r1']['Space'] == 'SmartJump 0' and K['r1']['S'] == 'MoveBackward' and K['r1']['W'] == 'MoveForward' and K['r1']['G'] == 'Grab' and K['r1']['Mouse4'] == '' and K['r1']['V'] == 'WhiteButton | VoiceMenuNext' and K['r1']['F9'] == 'shot' and K['r1']['Home'] == 'CenterView'
SCEN
  jumpcheck space-jumps-again "key space" 1
  jumpcheck j-no-longer-jumps "key j" 0
  turncheck mouse-turns-the-default 500 "$K"
  cat <<'SCEN'
sleep 0.5
probe i2
chan mouse 0 -50
sleep 0.5
probe i3
check $P-mouse-up-looks-up-again pitchdiff('i2','i3') > 0
SCEN
}

scen_restart() {
  start_match
  cat <<'SCEN'
keys d0 J Space S W G Mouse4 V F9 Home
check $P-defaults-persisted K['d0']['J'] == 'Taunt Taunt1' and K['d0']['Space'] == 'SmartJump 0' and K['d0']['S'] == 'MoveBackward' and K['d0']['W'] == 'MoveForward' and K['d0']['Mouse4'] == '' and K['d0']['V'] == 'WhiteButton | VoiceMenuNext' and K['d0']['F9'] == 'shot'
probe m1
check $P-mouse-defaults-persisted abs(float(P['m1'].sens) - 2.5) < 0.001 and P['m1'].invert == 'False'
SCEN
  turncheck mouse-turns-the-default 500 "$K"
}

scen_pad() {
  cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
pad attach
wait pad 0 connected 15
sleep 1
SCEN
  menu_to_page | sed '1,3d'
  cat <<'SCEN'
padpress dpdown
sleep 0.5
padpress dpdown
sleep 0.8
probe p0
check $P-dpad-reaches-the-list P['p0'].item == 'MOVE FORWARD|0'
padpress a
expect $P-pad-a-captures KeyBinds: PRESS A KEY 10
sleep 0.5
padpress b
expect $P-pad-button-cancels-a-capture KeyBinds: PAD BUTTONS ARE SET ON THE CONTROLLER PAGE 10
sleep 0.5
probe p1
check $P-page-still-open P['p1'].menu == 'UC2Settings_Keys' and P['p1'].item == 'MOVE FORWARD|0'
seek 10 P['_'].item == 'JUMP / DODGE|4' ;; padpress dpdown
padpress a
wait KeyBinds: PRESS A KEY 10
sleep 0.5
key k
expect $P-keyboard-key-after-pad-a KeyBinds: K: JUMP / DODGE\. 1ST / 3RD PERSON MOVED TO SPACE\. 10
sleep 0.5
padpress dpright
sleep 0.3
padpress a
wait KeyBinds: PRESS A KEY 10
sleep 0.5
key semicolon
expect $P-dpad-right-is-key-2 KeyBinds: ;: JUMP / DODGE\. \(IT WAS: ThrowWeapon\) 10
sleep 0.5
padpress b
expect $P-pad-b-leaves-and-saves KeyBinds: saved 10
sleep 2
keys p2 K Space C Semicolon
check $P-pad-session-saved K['p2']['K'] == 'SmartJump 0' and K['p2']['Space'] == 'BlackButton | ToggleThirdCam' and K['p2']['C'] == 'BlackButton | ToggleThirdCam' and K['p2']['Semicolon'] == 'SmartJump 0'
SCEN
}

# pictures only: the page at the window size of the run (M10_RES), the list and a capture
scen_look() {
  menu_to_page
  cat <<'SCEN'
shot page
key Down
key Down
sleep 1
shot list
key Return
sleep 1
shot capture
key Escape
sleep 1
SCEN
}

scen_lookgame() {
  start_match
  pause_to_page
  cat <<'SCEN'
sleep 2.5
shot page
key Down
key Down
sleep 1
shot list
SCEN
}

# ---------------------------------------------------------------------------------------------------------------
# User.ini against DefUser.ini after the menu scenario: exactly the changed keys differ, the pad keys are untouched
ini_check() {
  python3 - "$sys/User.ini" "$sys/DefUser.ini" "$1" <<'PY'
import sys, re
user, defuser, stage = sys.argv[1:4]
def section(path, name):
    d, cur = {}, None
    for raw in open(path, encoding='latin-1'):
        line = raw.strip()
        if line.startswith('['):
            cur = line[1:-1]
            continue
        if cur == name and '=' in line and not line.startswith(';'):
            k, v = line.split('=', 1)
            d[k.lower()] = v.strip()
    return d
u, du = section(user, 'Engine.Input'), section(defuser, 'Engine.Input')
pu, pd = section(user, 'UCGameplay.UCPlayerInput'), section(defuser, 'UCGameplay.UCPlayerInput')
def report(name, ok, detail):
    print(f"{'PASS' if ok else 'FAIL'} ini-{stage}-{name}: {detail}")
want = {'j': 'SmartJump 0', 'space': '', 's': 'MoveForward', 'w': 'MoveBackward', 'g': 'StrafeRight', 'period': '',
        'mouse4': 'Jump', 'v': '', 'f9': '', 'home': ''} if stage == 'menu' else {}
diff = {k: (du.get(k), u.get(k)) for k in set(u) | set(du) if (u.get(k) or '') != (du.get(k) or '')}
report('changed-keys-written', all(u.get(k) == v for k, v in want.items()),
       ', '.join(f'{k}={u.get(k)!r}' for k in sorted(want)) or 'none expected')
report('nothing-else-differs', set(diff) == set(k for k, v in want.items() if (du.get(k) or '') != v),
       f'{len(diff)} keys differ from DefUser.ini: ' + ', '.join(f'{k}: {a!r} -> {b!r}' for k, (a, b) in sorted(diff.items())))
pad = [k for k in du if re.fullmatch(r'joy(\d+|[xyuvzr]|slider\d)', k)]
report('pad-keys-untouched', all(u.get(k) == du[k] for k in pad), f'{len(pad)} pad keys equal DefUser.ini')
sens, inv = (5.0, 'True') if stage == 'menu' else (2.5, 'False')
try:
    s = float(pu.get('mouselooksensitivity', pd.get('mouselooksensitivity', 'nan')))
except ValueError:
    s = float('nan')
i = pu.get('binvertmouselook', pd.get('binvertmouselook', ''))
report('mouse-settings', abs(s - sens) < 0.001 and i.lower() == inv.lower(), f'MouseLookSensitivity={s} bInvertMouseLook={i}')
PY
}

scenarios=("$@")
[ ${#scenarios[@]} -eq 0 ] && scenarios=(menu match restart pad)
summary=$out/summary.txt
: > "$summary"
for s in "${scenarios[@]}"; do
  case "$s" in
    menu)
      prep
      scen_menu | sed 's/\$P/menu/g' > "$out/menu.scen"
      echo "== menu"
      run menu 420
      ini_check menu;;
    match)
      [ -f "$sys/User.ini" ] || { echo "FAIL match: no User.ini (run the menu scenario first)"; continue; }
      scen_match | sed 's/\$P/match/g' > "$out/match.scen"
      echo "== match (a new process: the restart)"
      run match 480 M10_URL="DM-Deadbolt?NumBots=0" -- -nullrenderer -maxfps=60
      ini_check match;;
    restart)
      scen_restart | sed 's/\$P/restart/g' > "$out/restart.scen"
      echo "== restart"
      run restart 240 M10_URL="DM-Deadbolt?NumBots=0" -- -nullrenderer -maxfps=60
      ini_check restart;;
    pad)
      prep
      scen_pad | sed 's/\$P/pad/g' > "$out/pad.scen"
      echo "== pad"
      run pad 300;;
    look)
      prep
      scen_look | sed 's/\$P/look/g' > "$out/look640.scen"
      echo "== look640"; run look640 300
      prep
      cp -f "$out/look640.scen" "$out/look1080.scen"
      echo "== look1080"; run look1080 420 M10_RES=1920x1080
      prep
      scen_lookgame | sed 's/\$P/lookgame/g' > "$out/lookgame.scen"
      echo "== lookgame"; run lookgame 420 M10_URL="DM-Deadbolt?NumBots=0";;
    *) echo "unknown scenario $s" >&2;;
  esac
done 2>&1 | tee "$summary"
echo "d9-rebind: $(grep -c '^PASS' "$summary") PASS, $(grep -c '^FAIL' "$summary") FAIL (scenarios: ${scenarios[*]}; $summary)"
for p in $(pgrep -x uc2.bin) $(pgrep -x Xvfb); do
  case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"|"$sys"/*) echo "WARNING: left running: $p";; esac
done
