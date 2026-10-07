#!/usr/bin/env bash
# saveconfig check: script SaveConfig() / StaticSaveConfig() on PC ([Engine.Engine] ScriptSaveConfig), the engine's
# SAVECONFIGKEYS command, and the D-6 mouse console commands that use it. One PASS/FAIL line per check, then a summary.
#
#   tools/dev/sc-saveconfig.sh [part...]        parts: u g m w   (default: u g m, and w when the wasm tree exists)
#
#   u  ucc-bin (native) runs the natives without a game (`ucc-bin saveconfigtest`): StaticSaveConfig of a class default
#      writes only what changed (SetHand's class: Handedness and nothing else), a changed globalconfig property another
#      class sets on purpose is left out (DesiredFOV: UCPlayerController says 90), the instance form, SAVECONFIGKEYS
#      (one key of a 36-key section), and ScriptSaveConfig=False (-ini:) writes nothing; a restart reads the values back
#   w  the same checks with the WebAssembly build of ucc-bin under Node (the web build's file layer; NODERAWFS = the host
#      file system, not the browser's WasmFS: see tools/dev/c1-web-saves.sh)
#   g  the game, native, DM-Deadbolt, the engine killed (SIGKILL: nothing is written at exit) after: MouseLookSens 1.5,
#      MouseLookInvert True and SetWeaponHand Left are in User.ini, nothing else changed (the profile's pad values and
#      the FOV are not), a level change keeps them, a restart gives them back (class defaults and the running player),
#      the profile's pad invert/sensitivity win over the ini's and the mouse's settings are the ini's; with
#      ScriptSaveConfig=False the commands last for the session and nothing is written
#   m  a menu round trip and a full match (the C-2 flow: profile, Instant Action, DM-Deadbolt) with ScriptSaveConfig on and
#      off: no script save runs, the inis come out the same, and every key the run wrote is on the allow-list below
#
# Trees (<WorkUC2>/run/<name>, tools/dev/mkrun-uc2.sh; UC2_SCRIPTS=<dir with the UCGameplay.u of this branch>):
# SC_TREE (default sc, native), SC_WASM_TREE (default scw: mkrun-uc2.sh build-native scw build-wasm-perf).
# Every run is inside <WorkUC2>/bin/capped (ucc 3G, wasm under Node 3G, game 2G through m10-controls.sh). Output:
# SC_OUT (default <WorkUC2>/logs/sc/check).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${SC_TREE:-sc}
wtree=${SC_WASM_TREE:-scw}
sys=$work/run/$tree/System
wsys=$work/run/$wtree/System
out=${SC_OUT:-$work/logs/sc/check}
capped=$work/bin/capped
m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] && [ -x "$sys/ucc-bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }

parts=("$@")
if [ ${#parts[@]} -eq 0 ]; then
  parts=(u g m)
  [ -f "$wsys/ucc-bin.js" ] && parts+=(w)
fi
has() { local p; for p in "${parts[@]}"; do [ "$p" = "$1" ] && return 0; done; return 1; }

fails=0
result() { echo "$1 $2: $3"; [ "$1" = PASS ] || fails=$((fails+1)); }
check() { local n=$1 d; shift; if d=$("$@" 2>&1); then result PASS "$n" "$d"; else result FAIL "$n" "$d"; fi; }

# ini_get <file> <section> <key>: the value of the first Key= line in [Section] (case-insensitive names), or exit 1.
ini_get() {
  awk -v sec="$2" -v key="$3" '
    BEGIN { sec = tolower(sec); key = tolower(key) }
    { sub(/\r$/, "") }
    /^\[/ { cur = tolower(substr($0, 2, length($0) - 2)); next }
    cur == sec { i = index($0, "="); if (i > 1 && tolower(substr($0, 1, i-1)) == key) { print substr($0, i+1); found = 1; exit } }
    END { exit found ? 0 : 1 }' "$1"
}
expect_ini() {	# expect_ini <file> <section> <key> <value>
  local got; got=$(ini_get "$1" "$2" "$3") || { echo "[$2] $3 is not in $(basename "$1")"; return 1; }
  [ "$got" = "$4" ] && echo "[$2] $3=$got in $(basename "$1")" || { echo "[$2] $3 is '$got', expected '$4'"; return 1; }
}
absent_ini() {	# absent_ini <file> <section> <key>
  local got; got=$(ini_get "$1" "$2" "$3") && { echo "[$2] $3=$got is in $(basename "$1")"; return 1; }
  echo "[$2] $3 is not in $(basename "$1")"
}
# keys <ini>: every "section.key=value" (lower case names, comments dropped), sorted: the file as a set
keys() { tr -d '\r' < "$1" | awk '/^\[/ { s = tolower($0); next } /^[;#]/ { next } /=/ { i = index($0, "="); print s "." tolower(substr($0, 1, i-1)) "=" substr($0, i+1) }' | LC_ALL=C sort -u; }
# newkeys <before ini> <after ini>: the keys of <after> that are new or have another value
newkeys() { LC_ALL=C comm -13 <(keys "$1") <(keys "$2"); }
export -f keys ini_get newkeys
logline() {	# logline <fixed text> <log...>
  local text=$1 f l; shift
  for f in "$@"; do
    l=$(grep -a -F -m1 -- "$text" "$f" 2>/dev/null | tr -d '\r') && [ -n "$l" ] && { echo "$(basename "$f"): $l"; return 0; }
  done
  echo "no log line '$text' in $*"; return 1
}
nolog() {	# nolog <fixed text> <log...>: none of the logs has the line
  local text=$1 f; shift
  for f in "$@"; do grep -a -q -F -- "$text" "$f" 2>/dev/null && { echo "'$text' is in $(basename "$f")"; return 1; }; done
  echo "no '$text' in $(for f in "$@"; do basename "$f"; done | paste -sd' ')"
}
# fromresult <result file> <check name>: re-print one of the game scenario's own checks
fromresult() {
  local line; line=$(grep -a -P "^(PASS|FAIL)\t$2\t" "$1" 2>/dev/null | head -1)
  if [ -z "$line" ]; then result FAIL "$2" "not in $1"
  else result "${line%%$'\t'*}" "$2" "$(printf '%s' "$line" | cut -f3 | cut -c1-160)"; fi
}

# ---------------------------------------------------------------------------------------------------------------
# u / w: ucc-bin without a game
# ---------------------------------------------------------------------------------------------------------------
# uccprep <sys>: our tracked inis, nothing derived, no logs
uccprep() { cp -f "$repo"/Engine/Config/*.ini "$1/"; rm -f "$1/Avaris.ini" "$1/User.ini" "$1"/*.log "$1"/*.log.*; }
# ucc <L> <sys> <name> <args...>: one saveconfigtest run; output $out/<L>-<name>.out, the log $out/<L>-<name>.log
ucc() {
  local L=$1 s=$2 name=$3; shift 3
  rm -f "$s"/*.log
  if [ "$L" = w ]; then ( cd "$s" && "$capped" 3G 600 -- node ucc-bin.js saveconfigtest "$@" > "$out/$L-$name.out" 2>&1 ); local rc=$?
  else ( cd "$s" && "$capped" 3G 300 -- ./ucc-bin saveconfigtest "$@" > "$out/$L-$name.out" 2>&1 ); local rc=$?; fi
  cp -f "$s/ucc.log" "$out/$L-$name.log" 2>/dev/null
  return $rc
}
# gotvalue <L> <name> <pattern>: the answer of a "get" run
gotvalue() { grep -a -E -m1 "$3" "$out/$1-$2.out" | tr -d '\r'; }

ucc_checks() {
  local L=$1 s=$2 base=$s/DefUser.ini
  echo "== $L: ucc-bin saveconfigtest ($( [ "$L" = w ] && echo 'wasm under Node' || echo native ), tree $s)"

  # static: SetHand's case. A class save of Engine.PlayerController after Handedness changed.
  uccprep "$s"
  ucc $L static Engine.PlayerController Handedness -1 load=UCGameplay; rc=$?
  check $L-static-exits-ok test "$rc" = 0
  cp -f "$s/User.ini" "$out/$L-static-User.ini"
  check $L-static-writes-the-changed-key expect_ini "$s/User.ini" Engine.Controller Handedness -1.000000
  check $L-static-writes-nothing-else bash -c "newkeys '$base' '$s/User.ini' > '$out/$L-static-written.txt'; n=\$(wc -l < '$out/$L-static-written.txt'); if [ \$n = 1 ]; then cat '$out/$L-static-written.txt'; else echo \"\$n keys written:\"; head -5 '$out/$L-static-written.txt'; exit 1; fi"
  check $L-static-is-logged logline 'Script SaveConfig: StaticSaveConfig on Engine.PlayerController' "$out/$L-static.log"
  ucc $L get UCGameplay.UCPlayerController Handedness;
  check $L-restart-reads-handedness-back test "$(gotvalue $L get '^-?[0-9]')" = "-1.000000"
  ucc $L get UCGameplay.UCPlayerController DesiredFOV
  check $L-restart-fov-is-still-90 test "$(gotvalue $L get '^-?[0-9]')" = "90.000000"

  # shadow: a changed globalconfig property that UCPlayerController sets on purpose (DesiredFOV=90) is not written
  uccprep "$s"
  ucc $L static Engine.PlayerController DesiredFOV 100 load=UCGameplay
  check $L-shadow-not-written absent_ini "$s/User.ini" Engine.PlayerController DesiredFOV
  check $L-shadow-is-logged logline 'not written, another class sets its own default' "$out/$L-static.log"
  ucc $L get UCGameplay.UCPlayerController DesiredFOV
  check $L-shadow-fov-stays-90 test "$(gotvalue $L get '^-?[0-9]')" = "90.000000"

  # instance form (execSaveConfig on a class default), a property the class declares and the ini has no key for
  uccprep "$s"
  ucc $L instance Engine.PlayerInput MouseSmoothingMode 2
  check $L-instance-writes-the-changed-key expect_ini "$s/User.ini" Engine.PlayerInput MouseSmoothingMode 2
  check $L-instance-writes-nothing-else bash -c "n=\$(newkeys '$base' '$s/User.ini' | wc -l); [ \$n = 1 ] && echo '1 key' || { echo \"\$n keys written\"; exit 1; }"

  # SAVECONFIGKEYS: one key of the 36 in [UCGameplay.UCPlayerInput]
  uccprep "$s"
  ucc $L keys UCGameplay.UCPlayerInput MouseLookSensitivity 1.75
  check $L-keys-writes-the-key expect_ini "$s/User.ini" UCGameplay.UCPlayerInput MouseLookSensitivity 1.750000
  check $L-keys-writes-nothing-else bash -c "n=\$(newkeys '$base' '$s/User.ini' | wc -l); [ \$n = 1 ] && echo '1 key' || { echo \"\$n keys written\"; exit 1; }"
  check $L-keys-reply-is-logged logline 'SaveConfigKeys: UCPlayerInput.MouseLookSensitivity=1.750000 written to User.ini' "$out/$L-keys.log" "$out/$L-keys.out"

  # ScriptSaveConfig=False (the launcher's -ini: override): the three natives write nothing
  uccprep "$s"
  ucc $L get Engine.PlayerController Handedness
  cp -f "$s/User.ini" "$out/$L-off-before-User.ini"
  local off='-ini:System:[Engine.Engine]:ScriptSaveConfig=False'
  ucc $L static Engine.PlayerController Handedness -1 "$off"
  check $L-off-is-logged logline 'Script SaveConfig: off' "$out/$L-static.log"
  ucc $L instance Engine.PlayerInput MouseSmoothingMode 2 "$off"
  ucc $L keys UCGameplay.UCPlayerInput MouseLookSensitivity 1.75 "$off"
  check $L-off-keys-answers-off logline 'SaveConfigKeys: off' "$out/$L-keys.out"
  check $L-off-writes-nothing cmp "$out/$L-off-before-User.ini" "$s/User.ini"

  # the default: on, and in the derived Avaris.ini
  uccprep "$s"
  ucc $L get Engine.PlayerController Handedness
  check $L-default-is-on logline 'Script SaveConfig: on' "$out/$L-get.log"
  check $L-default-key-in-Avaris.ini expect_ini "$s/Avaris.ini" Engine.Engine ScriptSaveConfig True
}

if has u; then ucc_checks u "$sys"; fi
if has w; then
  [ -f "$wsys/ucc-bin.js" ] || { echo "no wasm ucc at $wsys (tools/dev/mkrun-uc2.sh build-native $wtree build-wasm-perf)" >&2; exit 2; }
  ucc_checks w "$wsys"
fi

# ---------------------------------------------------------------------------------------------------------------
# g: the game
# ---------------------------------------------------------------------------------------------------------------
gout=$out/game
mkdir -p "$gout"
# game <name> <scenario> <fresh|keep> <stopsig> <uc2.bin args...>: DM-Deadbolt without bots, the null renderer at 60 fps
game() {
  local name=$1 scen=$2 mode=$3 sig=$4; shift 4
  rm -f "$sys"/*.log "$sys"/*.log.*
  ( [ "$mode" = keep ] && export M10_KEEP=1
    M10_STOPSIG=$sig M10_TREE=$tree M10_OUT=$gout M10_SECS=420 M10_URL='DM-Deadbolt?NumBots=0' \
      "$m10" --file "$scen" "$name" -nullrenderer -maxfps=60 "$@" | sed 's/^/    /' )
  cp -f "$sys/Avaris.log" "$gout/$name-Avaris.log" 2>/dev/null
  cp -f "$sys/User.ini" "$gout/$name-User.ini" 2>/dev/null
  cp -f "$sys/Avaris.ini" "$gout/$name-Avaris.ini" 2>/dev/null
}
# the start of every game scenario: the level, a spawn
gstart() { cat <<'SCEN'
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 4
focus
key Alt_L 0.3
sleep 3
tryseek 6 P['_'].pawn == 1 ;; key Alt_L 0.3
SCEN
}

if has g; then
  echo "== g1: set the mouse, the hand; change level (tree $tree, SIGKILL after)"
  { gstart; cat <<'SCEN'
probe a
check g1-defaults-before P['a'].sens.startswith('2.5') and P['a'].invert == 'False' and P['a'].padinvert == 'False'
chan exec getall UCPlayerInput MouseSensitivity_Y
expect g1-pad-sensitivity-is-the-profiles-not-the-inis MouseSensitivity_Y = 0\.8 10
chan exec MouseLookSens 1.5
expect g1-sens-written SaveConfigKeys: UCPlayerInput\.MouseLookSensitivity=1\.500000 written to User\.ini 10
chan exec MouseLookInvert True
expect g1-invert-written SaveConfigKeys: UCPlayerInput\.bInvertMouseLook=True written to User\.ini 10
chan exec SetWeaponHand Left
expect g1-hand-saves-the-class Script SaveConfig: StaticSaveConfig on Engine\.PlayerController 10
probe b
check g1-session-has-the-values P['b'].sens.startswith('1.5') and P['b'].invert == 'True' and P['b'].padinvert == 'False'
chan exec open DM-Deadbolt?NumBots=0
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 4
key Alt_L 0.3
sleep 3
tryseek 6 P['_'].pawn == 1 ;; key Alt_L 0.3
probe c
check g1-the-next-level-keeps-the-values P['c'].sens.startswith('1.5') and P['c'].invert == 'True' and P['c'].padinvert == 'False'
sleep 1
SCEN
  } > "$gout/g1.scen"
  game g1 "$gout/g1.scen" fresh KILL
  for c in g1-defaults-before g1-pad-sensitivity-is-the-profiles-not-the-inis g1-sens-written g1-invert-written g1-hand-saves-the-class \
           g1-session-has-the-values g1-the-next-level-keeps-the-values; do fromresult "$gout/g1.result" $c; done
  u=$gout/g1-User.ini
  check g1-file-sensitivity expect_ini "$u" UCGameplay.UCPlayerInput MouseLookSensitivity 1.500000
  check g1-file-invert expect_ini "$u" UCGameplay.UCPlayerInput bInvertMouseLook True
  check g1-file-hand expect_ini "$u" Engine.Controller Handedness -1.000000
  check g1-file-pad-sensitivity-is-the-inis-own-not-the-profiles expect_ini "$u" UCGameplay.UCPlayerInput MouseSensitivity_Y "$(ini_get "$repo/Engine/Config/DefUser.ini" UCGameplay.UCPlayerInput MouseSensitivity_Y)"
  check g1-file-fov-is-not-pinned absent_ini "$u" Engine.PlayerController DesiredFOV
  check g1-file-saved-before-the-kill test -s "$u"
  check g1-no-critical nolog Critical "$gout/g1.out"

  echo "== g2: a new process on the same tree"
  { gstart; cat <<'SCEN'
probe a
check g2-mouse-sensitivity-came-back P['a'].sens.startswith('1.5')
check g2-mouse-invert-came-back P['a'].invert == 'True'
check g2-pad-invert-is-the-profiles P['a'].padinvert == 'False'
chan exec get UCGameplay.UCPlayerInput MouseLookSensitivity
expect g2-class-default-sensitivity ^1\.500000 10
chan exec get UCGameplay.UCPlayerInput bInvertMouseLook
expect g2-class-default-invert ^True 10
chan exec get UCGameplay.UCPlayerController Handedness
expect g2-handedness-came-back ^-1\.000000 10
chan exec get UCGameplay.UCPlayerController DesiredFOV
expect g2-fov-is-still-90 ^90\.000000 10
chan exec getall UCPlayerInput MouseSensitivity_Y
expect g2-pad-sensitivity-is-the-profiles-not-the-inis MouseSensitivity_Y = 0\.8 10
sleep 1
SCEN
  } > "$gout/g2.scen"
  game g2 "$gout/g2.scen" keep TERM
  for c in g2-mouse-sensitivity-came-back g2-mouse-invert-came-back g2-pad-invert-is-the-profiles g2-class-default-sensitivity g2-class-default-invert \
           g2-handedness-came-back g2-fov-is-still-90 g2-pad-sensitivity-is-the-profiles-not-the-inis; do fromresult "$gout/g2.result" $c; done
  check g2-no-critical nolog Critical "$gout/g2.out"

  echo "== g3: ScriptSaveConfig=False (-ini: override): the same commands, a fresh tree"
  { gstart; cat <<'SCEN'
mark
chan exec MouseLookSens 1.5
chan exec MouseLookInvert True
chan exec SetWeaponHand Left
sleep 1.5
probe b
check g3-session-has-the-values P['b'].sens.startswith('1.5') and P['b'].invert == 'True'
absent g3-no-key-written SaveConfigKeys: UCPlayerInput
absent g3-no-script-save Script SaveConfig: (Static)?SaveConfig on
chan exec open DM-Deadbolt?NumBots=0
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 4
key Alt_L 0.3
sleep 3
tryseek 6 P['_'].pawn == 1 ;; key Alt_L 0.3
probe c
check g3-the-next-level-keeps-the-session-values P['c'].sens.startswith('1.5') and P['c'].invert == 'True'
sleep 1
SCEN
  } > "$gout/g3.scen"
  game g3 "$gout/g3.scen" fresh KILL '-ini:System:[Engine.Engine]:ScriptSaveConfig=False'
  for c in g3-session-has-the-values g3-no-key-written g3-no-script-save g3-the-next-level-keeps-the-session-values; do fromresult "$gout/g3.result" $c; done
  u=$gout/g3-User.ini
  check g3-file-sensitivity-unchanged expect_ini "$u" UCGameplay.UCPlayerInput MouseLookSensitivity 2.5
  check g3-file-invert-unchanged expect_ini "$u" UCGameplay.UCPlayerInput bInvertMouseLook False
  check g3-file-no-hand absent_ini "$u" Engine.Controller Handedness
  check g3-off-is-logged logline 'Script SaveConfig: off' "$gout/g3-Avaris.log"
  # what the script wrote in g1: the difference between g1 and g3 (same flow, same engine; only ScriptSaveConfig differs)
  newkeys "$gout/g3-User.ini" "$gout/g1-User.ini" > "$gout/g1-vs-g3-written.txt"
  check g1-script-wrote-exactly-the-three-keys bash -c "cat '$gout/g1-vs-g3-written.txt'; [ \$(wc -l < '$gout/g1-vs-g3-written.txt') = 3 ] && grep -a -q '^\[engine.controller\].handedness=-1.000000\$' '$gout/g1-vs-g3-written.txt' && grep -a -q '^\[ucgameplay.ucplayerinput\].mouselooksensitivity=1.500000\$' '$gout/g1-vs-g3-written.txt' && grep -a -q '^\[ucgameplay.ucplayerinput\].binvertmouselook=true\$' '$gout/g1-vs-g3-written.txt'"
  check g1-vs-g3-avaris-ini-differs-only-by-the-setting bash -c "d=\$(LC_ALL=C comm -3 <(keys '$gout/g1-Avaris.ini') <(keys '$gout/g3-Avaris.ini') | tr -d '\t' | LC_ALL=C sort | tr '\n' ' '); echo \"\$d\"; [ \"\$d\" = '[engine.engine].scriptsaveconfig=false [engine.engine].scriptsaveconfig=true ' ]"
fi

# ---------------------------------------------------------------------------------------------------------------
# m: a menu round trip and a full match, with the natives on and off
# ---------------------------------------------------------------------------------------------------------------
if has m; then
  name=${SC_PROFILE:-Saver}
  # The flow of tools/dev/c1-saves.sh run 1 (profile, Instant Action, DeathMatch, DM-Deadbolt, a character change, the match).
  cat > "$gout/m.scen" <<SCEN
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
key Down
key Down
key Down
sleep 0.5
key Return
expect m-profile-page-opens OpenMenu \[UCMenu.UC2Profile\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
key Return
expect m-keyboard-opens OpenMenu \[UCMenu.UC2ProfileKeyboard\] 15
sleep 3
type $name
sleep 1
probe k1
check m-typed-the-name P['k1'].text == '$name'
key Return
expect m-keyboard-closes CloseMenu \[UC2ProfileKeyboard\] 10
sleep 3
chan exec getall PlayerProfile ProfileName
expect m-new-profile-is-the-viewports-profile ProfileName = $name\s*$ 10
key Escape
expect m-profile-page-closes CloseMenu \[UC2Profile\] 10
sleep 3
key Up
key Up
sleep 0.5
key Return
expect m-main-menu-opens-arena OpenMenu \[UCMenu.UC2Arena_Main\] 15
sleep 3
seek 4 P['_'].menu != 'GUIMessageBox' ;; key Escape
sleep 1
seek 10 P['_'].item.startswith('DeathMatch|') ;; key Down
key Return
sleep 2
key Return
sleep 2
seek 40 'DM-Deadbolt' in P['_'].item ;; key Down
key Return
expect m-map-chosen-opens-character OpenMenu \[UCMenu.UC2Arena_Character\] 15
sleep 2
key Right
sleep 2
key Return
expect m-character-opens-armory OpenMenu \[UCMenu.UC2Arena_Armory\] 15
sleep 2
key Return
sleep 2
key Return
expect m-armory-opens-launch-confirm OpenMenu \[UCMenu.ConfirmLaunchMenu\] 15
sleep 1.5
key Return
expect m-travel-to-deathmatch Game class is 'UCDeathmatch' 120
expect m-deadbolt-up Bringing Level DM-Deadbolt.MyLevel up for play 60
sleep 6
key Alt_L 0.2
sleep 4
probe spawn
check m-match-started-pawn-spawned P['spawn'].pawn==1 and P['spawn'].level=='DM-Deadbolt'
keydown w
sleep 2
keyup w
sleep 1
SCEN
  mgame() {	# mgame <name> <uc2.bin args...>: the menu flow, a fresh tree, the normal exit (SIGTERM)
    local n=$1; shift
    rm -f "$sys"/*.log "$sys"/*.log.*
    M10_STOPSIG=TERM M10_TREE=$tree M10_OUT=$gout M10_SECS=480 "$m10" --file "$gout/m.scen" "$n" "$@" | sed 's/^/    /'
    cp -f "$sys/Avaris.log" "$gout/$n-Avaris.log" 2>/dev/null
    cp -f "$sys/User.ini" "$gout/$n-User.ini" 2>/dev/null
    cp -f "$sys/Avaris.ini" "$gout/$n-Avaris.ini" 2>/dev/null
  }
  echo "== m1: menu round trip and a full match, ScriptSaveConfig on (default)"
  mgame m1
  echo "== m2: the same with ScriptSaveConfig=False"
  mgame m2 '-ini:System:[Engine.Engine]:ScriptSaveConfig=False'
  for r in m1 m2; do
    check $r-scenario-checks-passed bash -c "! grep -a -q '^FAIL' '$gout/$r.result' && echo \"\$(grep -a -c '^PASS' '$gout/$r.result') checks\""
    check $r-no-script-save-ran nolog 'Script SaveConfig: StaticSaveConfig on' "$gout/$r.out"
    check $r-no-script-instance-save-ran nolog 'Script SaveConfig: SaveConfig on' "$gout/$r.out"
    check $r-no-save-config-keys nolog 'SaveConfigKeys:' "$gout/$r.out"
    check $r-no-critical nolog Critical "$gout/$r.out"
  done
  check m-user-ini-identical-with-the-natives-on-and-off bash -c "d=\$(LC_ALL=C comm -3 <(keys '$gout/m1-User.ini') <(keys '$gout/m2-User.ini') | tr -d '\t'); [ -z \"\$d\" ] && echo 'every key and value the same' || { echo \"\$d\" | head; exit 1; }"
  check m-avaris-ini-differs-only-by-the-setting bash -c "d=\$(LC_ALL=C comm -3 <(keys '$gout/m1-Avaris.ini') <(keys '$gout/m2-Avaris.ini') | tr -d '\t' | LC_ALL=C sort | tr '\n' ' '); echo \"\$d\"; [ \"\$d\" = '[engine.engine].scriptsaveconfig=false [engine.engine].scriptsaveconfig=true ' ]"
  # The allow-list: every key a full session wrote (new or changed against the files the engine derives at its first
  # start), with who writes it. Written by the ENGINE'S C++ (SaveURLConfig, the profile remember-poll, the window
  # settings), not by script. Anything else is a FAIL.
  cat > "$gout/allow-user.txt" <<'ALLOW'
^\[defaultplayer\]\.(name|team|class|skin|face|voice|energyweapon|explosiveweapon|playerhandicap|overrideclass)=	UGameEngine::LoadMap SaveURLConfig("DefaultPlayer", ...), UnGame.cpp: the URL of each map loaded
^\[profiles\]\.lastprofile=	UC2RememberProfile (D-3): the profile the player last used
ALLOW
  cat > "$gout/allow-avaris.txt" <<'ALLOW'
^\[engine\.engine\]\.scriptsaveconfig=	this task: the setting itself (m2: the -ini: override is written back, C-1)
^\[sdl2drv\.sdl2client\]\.	the viewport/window settings the client writes when it opens its window (SDL2Viewport.cpp SaveConfig)
^\[uc2movie\]\.	-nostartupmovies' StartupMovies=False is written like any switch (UnMovie.cpp)
ALLOW
  # the derived files of a first start: a run to the main menu only, then compared with the finished session
  echo "== m0: the files a first start derives (main menu only), the baseline for the allow-list"
  cat > "$gout/m0.scen" <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 4
SCEN
  rm -f "$sys"/*.log "$sys"/*.log.*
  M10_STOPSIG=TERM M10_TREE=$tree M10_OUT=$gout M10_SECS=240 "$m10" --file "$gout/m0.scen" m0 | sed 's/^/    /'
  cp -f "$sys/User.ini" "$gout/m0-User.ini" 2>/dev/null; cp -f "$sys/Avaris.ini" "$gout/m0-Avaris.ini" 2>/dev/null
  for pair in "User:allow-user.txt" "Avaris:allow-avaris.txt"; do
    f=${pair%%:*}; a=$gout/${pair#*:}
    newkeys "$gout/m0-$f.ini" "$gout/m1-$f.ini" > "$gout/m1-written-$f.txt"
    check m-every-key-written-to-$f.ini-is-on-the-allow-list bash -c "bad=0; while IFS= read -r k; do hit=\$(awk -F'\t' -v k=\"\$k\" 'k ~ \$1 { print \$2; exit }' '$a'); if [ -n \"\$hit\" ]; then echo \"ok   \$k  <- \$hit\" | cut -c1-200; else echo \"NOT ALLOWED \$k\"; bad=1; fi; done < '$gout/m1-written-$f.txt'; echo \"\$(wc -l < '$gout/m1-written-$f.txt') keys\"; exit \$bad"
  done
fi

echo "sc-saveconfig: $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (logs in $out)"
[ "$fails" = 0 ]
