#!/usr/bin/env bash
# Key glyphs check (native): player one's keys in place of the pad's button glyphs (Source/Engine/Src/UnKeyGlyphs.cpp, script
# set key-glyphs). One PASS/FAIL line per check on stdout, then a summary.
#
#   tools/dev/h5-glyphs.sh [scenario...]        (default: menu info game travel vanilla; look on request)
#
#   menu     fresh inis, no profile, software GL 640x480: main menu, INSTANT ACTION, the "no profile" box declined,
#            three accepts -> the character page. Keyboard from the start: the menus' keys (A Enter, B Escape, Y Y,
#            White Q, Black E, X X: [GUI.EmulatedJoypad]); the virtual pad plugged in -> the pad's glyphs; a pad button
#            keeps them; a mouse move -> keys; a stick pushed -> pad; a key -> keys. Pictures char-keys, char-pad,
#            char-mouse, and a pixel check: the green A gem of the button bar is there only with the pad.
#   info     the same path, then MORE INFO (White = Q) scrolled to the adrenaline abilities: the combo instructions are
#            the GAME's buttons (marked by the set): X, the right/left mouse button, Space; pictures info-keys, info-pad.
#   game     DM-Deadbolt (no bots), adrenaline: the combo menu (X) shows RMB / SPACE / LMB (the keys bound to AltFire,
#            SmartJump 0, Fire); the pad's combo menu shows the Xbox glyphs; the pause menu's keys; then the KEYBOARD /
#            MOUSE page from the pause menu: JUMP / DODGE := J, and the combo menu's A shows J. Pictures combo-keys,
#            combo-pad, pause-keys, combo-rebound; pixel check on the combo menu's green A.
#   look     pictures only (no checks of their own but the scenario's): the character page and the combo menu at
#            1920x1080 (the UI box, the key caps at 2.25x), keys and pad: look-char-keys, look-char-pad, look-combo-*.
#   travel   the menu path on into a match (the character page, the armory, the launch): after the level change the
#            adrenaline menu still draws the key caps (the gate's class, font and sheet are kept from the collector).
#   vanilla  the menu path on a tree with the shipped GUI.u and UCMenu.u (H5_VANILLA_TREE, default h5v:
#            UC2_SCRIPTS=none tools/dev/mkrun-uc2.sh build-native h5v): no key glyph line at all, the Xbox A gem with the
#            keyboard, no error or critical line.
#
# Run tree: H5_TREE (default h5: tools/dev/mkrun-uc2.sh build-native h5). H5_GL4ES=1: the same on the GL4ES twin
# (-gl4es). Every game run is tools/dev/m10-controls.sh --file (Xvfb inside <repo>/../bin/capped 2G). Output: H5_OUT
# (default <repo>/../logs/h5/glyphs[-gl4es]): <scenario>.out (engine log), .result, .inject, <scenario>-<shot>.png.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${H5_TREE:-h5}
vtree=${H5_VANILLA_TREE:-h5v}
suffix=; extra=()
if [ -n "${H5_GL4ES:-}" ]; then suffix=-gl4es; extra=(-gl4es); fi
out=${H5_OUT:-$work/logs/h5/glyphs$suffix}
m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"

fails=0 passes=0
result() { echo "$1 $2: $3"; if [ "$1" = PASS ]; then passes=$((passes+1)); else fails=$((fails+1)); fi; }

# fresh inis (ours), DevUI and KeyGlyphs unsuppressed, no User.ini, no profiles
prep() {
  local sys=$work/run/$1/System
  [ -x "$sys/uc2.bin" ] || { echo "no run tree $work/run/$1 (tools/dev/mkrun-uc2.sh build-native $1)" >&2; exit 2; }
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d; /^Suppress=KeyGlyphs\r\?$/d' "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp
  rm -rf "$sys/Profiles" "$work/run/$1/Save"/*.pro 2>/dev/null
  mkdir -p "$sys/Profiles"
}

# run <tree> <name> <seconds> [m10 environment...] -- [engine arguments...]
run() {
  local t=$1 name=$2 secs=$3; shift 3
  local envs=()
  while [ $# -gt 0 ] && [ "$1" != "--" ]; do envs+=("$1"); shift; done
  [ "${1:-}" = "--" ] && shift
  env "${envs[@]}" M10_KEEP=1 M10_TREE=$t M10_OUT=$out M10_SECS=$secs "$m10" --file "$out/$name.scen" "$name" "${extra[@]}" "$@" | sed 's/^/    /'
  [ -f "$out/$name.result" ] || return 0
  while IFS=$'\t' read -r verdict check detail; do
    case "$verdict" in PASS|FAIL) result "$verdict" "$check" "$(echo "$detail" | cut -c1-200)";; esac
  done < "$out/$name.result"
}

# logline <name> <log> <fixed text>: the engine logged that line
logline() {
  if grep -a -q -F -- "$3" "$out/$2.out" 2>/dev/null; then result PASS "$1" "logged: $3"
  else result FAIL "$1" "not logged: $3"; fi
}
# nolog <name> <log> <regex>: the engine logged no such line
nolog() {
  local n; n=$(grep -a -c -E -- "$3" "$out/$2.out" 2>/dev/null); n=${n:-0}
  if [ "$n" = 0 ]; then result PASS "$1" "no line matches /$3/"
  else result FAIL "$1" "$n line(s) match /$3/: $(grep -a -m1 -E -- "$3" "$out/$2.out" | cut -c1-120)"; fi
}
# green <png> <x0> <y0> <x1> <y1>: saturated green pixels in the box (the Xbox A gem is green; the key labels are not)
green() {
  python3 - "$@" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB').crop(tuple(int(v) for v in sys.argv[2:6]))
print(sum(1 for r, g, b in im.get_flattened_data() if g > 120 and g > r + 50 and g > b + 50)
      if hasattr(im, 'get_flattened_data') else
      sum(1 for r, g, b in im.getdata() if g > 120 and g > r + 50 and g > b + 50))
PY
}
# gem <name> <png> <want: yes|no> <box...>
gem() {
  local n=$1 png=$out/$2.png want=$3; shift 3
  [ -f "$png" ] || { result FAIL "$n" "no picture $png"; return; }
  local g; g=$(green "$png" "$@")
  if { [ "$want" = yes ] && [ "$g" -ge 40 ]; } || { [ "$want" = no ] && [ "$g" -le 3 ]; }; then
    result PASS "$n" "$g green pixels in the A glyph's box ($want gem expected)"
  else
    result FAIL "$n" "$g green pixels in the A glyph's box ($want gem expected)"
  fi
}

# caps <name> <png> <ge|le> <count> <box...>: the key cap's light rim (grey, every channel 150+, within 25 of each other) in the
# box, a strip over the top edge of the cap: phase 2 (GUI.KeyGlyphSheet); the phase 1 name and the Xbox gem have none there
caps() {
  local n=$1 png=$out/$2.png op=$3 lim=$4; shift 4
  [ -f "$png" ] || { result FAIL "$n" "no picture $png"; return; }
  local c; c=$(python3 - "$png" "$@" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB').crop(tuple(int(v) for v in sys.argv[2:6]))
px = im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata()
print(sum(1 for r, g, b in px if 150 <= min(r, g, b) and max(r, g, b) - min(r, g, b) <= 25))
PY
)
  if [ "$c" "-$op" "$lim" ]; then result PASS "$n" "$c key cap rim pixels at the glyph's top edge (want -$op $lim)"
  else result FAIL "$n" "$c key cap rim pixels at the glyph's top edge (want -$op $lim)"; fi
}

# main menu -> INSTANT ACTION -> "no profile" box declined -> three accepts -> the character page
to_character() {
  cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
key Down
sleep 0.5
key Return
expect $P-instant-action-opens OpenMenu \[UCMenu.UC2Arena_Main\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
key Escape
sleep 3
key Return
sleep 2
key Return
sleep 2
key Return
expect $P-character-page-opens OpenMenu \[UCMenu.UC2Arena_Character\] 15
sleep 3
SCEN
}

# ---------------------------------------------------------------------------------------------------------------
scen_menu() {
  to_character
  cat <<'SCEN'
shot char-keys
pad attach
expect $P-pad-plugged-in-is-pad \[KeyGlyphs\] player one: pad \(pad connected\) 10
sleep 2
shot char-pad
mark
padpress dpright
sleep 1.5
absent $P-pad-button-keeps-pad player one: keyboard and mouse
chan mouse 8 0
expect $P-mouse-move-is-keys \[KeyGlyphs\] player one: keyboard and mouse \(mouse move\) 10
sleep 1.5
shot char-mouse
pad axis leftx 30000
sleep 0.3
pad axis leftx 0
expect $P-stick-is-pad \[KeyGlyphs\] player one: pad \(pad stick\) 10
sleep 1.5
key Down
expect $P-key-is-keys \[KeyGlyphs\] player one: keyboard and mouse \(Down\) 10
sleep 1.5
shot char-keys2
SCEN
}

scen_info() {
  to_character
  echo "key q"
  echo "expect \$P-more-info-opens OpenMenu \\[UCMenu.UC2CharacterInfoPage\\] 15"
  echo "sleep 3"
  for _ in $(seq 30); do printf 'key KP_2 0.6\nsleep 0.3\n'; done
  cat <<'SCEN'
sleep 1.5
shot info-keys
pad attach
expect $P-pad-plugged-in-is-pad \[KeyGlyphs\] player one: pad \(pad connected\) 10
sleep 2
shot info-pad
SCEN
}

scen_game() {
  cat <<'SCEN'
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 5
focus
key Alt_L 0.3
sleep 3
chan exec God
chan exec InfiniteAdrenaline
sleep 2
key x
sleep 0.4
shot combo-keys
sleep 4
pad attach
expect $P-pad-plugged-in-is-pad \[KeyGlyphs\] player one: pad \(pad connected\) 10
sleep 2
padpress x
sleep 0.4
shot combo-pad
sleep 4
key Escape
expect $P-pause-menu-opens OpenMenu \[UCMenu.InGame_MainMenu\] 15
sleep 2
shot pause-keys
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
key Return
expect $P-keys-page-opens OpenMenu \[UCMenu.UC2Settings_Keys\] 15
wait KeyBinds: rows \(loaded\) 15
sleep 2
key Down
key Down
sleep 0.5
seek 40 P['_'].item == 'JUMP / DODGE|4' ;; key Down
key Left
key Return
wait KeyBinds: PRESS A KEY 10
sleep 0.3
key j
expect $P-jump-to-j KeyBinds: J: JUMP / DODGE\. 10
sleep 0.5
key Escape
expect $P-saved KeyBinds: saved 10
sleep 1.5
key Escape
sleep 2
key Escape
sleep 2
tryseek 8 P['_'].menu == 'None' ;; key Escape 0.3
sleep 2
key x
expect $P-combo-a-follows-the-rebind \[KeyGlyphs\] game A -> J 10
sleep 0.4
shot combo-rebound
SCEN
}

scen_look() {
  to_character
  cat <<'SCEN'
shot char-keys
pad attach
expect $P-pad-plugged-in-is-pad \[KeyGlyphs\] player one: pad \(pad connected\) 10
sleep 3
shot char-pad
SCEN
}

scen_lookgame() {
  cat <<'SCEN'
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 8
focus
key Alt_L 0.5
sleep 4
chan exec God
chan exec InfiniteAdrenaline
sleep 2
keydown x
sleep 0.3
shot combo-keys
keyup x
sleep 4
pad attach
expect $P-pad-plugged-in-is-pad \[KeyGlyphs\] player one: pad \(pad connected\) 10
sleep 2
pad button x 1
sleep 0.3
shot combo-pad
pad button x 0
SCEN
}

# the path on into a match: the character page (the key caps loaded there), the armory, the launch; after the
# level change (which collects what nothing references) the adrenaline menu still draws the caps
scen_travel() {
  to_character
  cat <<'SCEN'
key Return
expect $P-armory-opens OpenMenu \[UCMenu.UC2Arena_Armory\] 15
sleep 2
key Return
sleep 2
key Return
expect $P-launch-confirm-opens OpenMenu \[UCMenu.ConfirmLaunchMenu\] 15
sleep 3
key Return
expect $P-match-up Bringing Level (?!Entry)\S+ up for play 180
sleep 8
key Alt_L 0.3
sleep 4
chan exec God
chan exec InfiniteAdrenaline
sleep 5
keydown x
expect $P-combo-menu-drawn \[KeyGlyphs\] game LeftTrigger -> RMB 6
sleep 0.3
shot combo-keys
keyup x
sleep 2
SCEN
}

scen_vanilla() {
  to_character
  cat <<'SCEN'
key Down
sleep 1.5
shot char-keys
SCEN
}

# ---------------------------------------------------------------------------------------------------------------
scenarios=("$@")
[ ${#scenarios[@]} -eq 0 ] && scenarios=(menu info game travel vanilla)
for s in "${scenarios[@]}"; do
  echo "== $s"
  case $s in
    menu)
      prep "$tree"
      scen_menu | sed 's/\$P/menu/g' > "$out/menu.scen"
      run "$tree" menu 240
      logline menu-starts-on-keys menu "[KeyGlyphs] player one: keyboard and mouse (start)"
      logline menu-a-is-enter menu "[KeyGlyphs] menu A -> ENTER"
      logline menu-b-is-escape menu "[KeyGlyphs] menu B -> ESC"
      logline menu-y-is-y menu "[KeyGlyphs] menu Y -> Y"
      logline menu-x-is-x menu "[KeyGlyphs] menu X -> X"
      logline menu-white-is-q menu "[KeyGlyphs] menu White -> Q"
      logline menu-black-is-e menu "[KeyGlyphs] menu Black -> E"
      gem menu-keys-no-a-gem menu-char-keys no 50 428 78 448
      gem menu-pad-has-a-gem menu-char-pad yes 50 428 78 448
      gem menu-mouse-no-a-gem menu-char-mouse no 50 428 78 448
      gem menu-key-again-no-a-gem menu-char-keys2 no 50 428 78 448
      logline menu-key-caps-loaded menu "key caps from GUI.KeyGlyphSheet"
      caps menu-keys-a-is-a-key-cap menu-char-keys ge 10 50 426 80 429
      caps menu-pad-a-is-no-key-cap menu-char-pad le 2 50 426 80 429
      nolog menu-no-critical menu 'Critical error|appError|Assertion failed|General protection fault|Unhandled signal|Signal: SIG(SEGV|ABRT|BUS|FPE|ILL)|Developer Backtrace'
      ;;
    info)
      prep "$tree"
      scen_info | sed 's/\$P/info/g' > "$out/info.scen"
      run "$tree" info 300
      logline info-combo-x-is-x info "[KeyGlyphs] game X -> X"
      logline info-combo-lt-is-rmb info "[KeyGlyphs] game LeftTrigger -> RMB"
      logline info-combo-a-is-space info "[KeyGlyphs] game A -> SPACE"
      logline info-combo-rt-is-lmb info "[KeyGlyphs] game RightTrigger -> LMB"
      nolog info-no-critical info 'Critical error|appError|Assertion failed|General protection fault|Unhandled signal|Signal: SIG(SEGV|ABRT|BUS|FPE|ILL)|Developer Backtrace'
      ;;
    game)
      prep "$tree"
      scen_game | sed 's/\$P/game/g' > "$out/game.scen"
      run "$tree" game 420 M10_URL="DM-Deadbolt?NumBots=0"
      logline game-lt-is-rmb game "[KeyGlyphs] game LeftTrigger -> RMB"
      logline game-a-is-space game "[KeyGlyphs] game A -> SPACE"
      logline game-rt-is-lmb game "[KeyGlyphs] game RightTrigger -> LMB"
      logline game-pause-menu-b-is-escape game "[KeyGlyphs] menu B -> ESC"
      gem game-combo-keys-no-a-gem game-combo-keys no 305 118 340 145
      gem game-combo-pad-has-a-gem game-combo-pad yes 305 118 340 145
      gem game-combo-rebound-no-a-gem game-combo-rebound no 305 118 340 145
      caps game-combo-keys-a-is-a-key-cap game-combo-keys ge 10 305 118 340 121
      caps game-combo-pad-a-is-no-key-cap game-combo-pad le 2 305 118 340 121
      nolog game-no-critical game 'Critical error|appError|Assertion failed|General protection fault|Unhandled signal|Signal: SIG(SEGV|ABRT|BUS|FPE|ILL)|Developer Backtrace'
      ;;
    look)
      prep "$tree"
      scen_look | sed 's/\$P/look/g' > "$out/look.scen"
      run "$tree" look 420 M10_RES=1920x1080 M10_KEYHOLD=0.6 M10_SLOW=2
      prep "$tree"
      scen_lookgame | sed 's/\$P/look-game/g' > "$out/look-game.scen"
      run "$tree" look-game 420 M10_RES=1920x1080 M10_KEYHOLD=0.6 M10_URL="DM-Deadbolt?NumBots=0"
      for f in "$out"/look-game-*.png; do [ -f "$f" ] && mv -f "$f" "$out/look-${f##*/look-game-}"; done
      ;;
    travel)
      prep "$tree"
      scen_travel | sed 's/\$P/travel/g' > "$out/travel.scen"
      run "$tree" travel 420
      logline travel-caps-loaded travel "key caps from GUI.KeyGlyphSheet"
      caps travel-combo-a-is-a-key-cap travel-combo-keys ge 10 305 118 340 121
      nolog travel-no-critical travel 'Critical error|appError|Assertion failed|General protection fault|Unhandled signal|Signal: SIG(SEGV|ABRT|BUS|FPE|ILL)|Developer Backtrace'
      ;;
    vanilla)
      prep "$vtree"
      scen_vanilla | sed 's/\$P/vanilla/g' > "$out/vanilla.scen"
      run "$vtree" vanilla 240
      nolog vanilla-no-key-glyph-lines vanilla 'KeyGlyphs|Key glyphs'
      gem vanilla-keys-keep-the-a-gem vanilla-char-keys yes 50 428 78 448
      caps vanilla-keys-no-key-cap vanilla-char-keys le 2 50 426 80 429
      nolog vanilla-no-critical vanilla 'Critical error|appError|Assertion failed|General protection fault|Unhandled signal|Signal: SIG(SEGV|ABRT|BUS|FPE|ILL)|Developer Backtrace'
      nolog vanilla-no-load-failure vanilla "Failed to load 'GUI.KeyGlyphs'|Can't find .*KeyGlyphs"
      ;;
    *) echo "unknown scenario $s" >&2; exit 2;;
  esac
done
echo "h5-glyphs$suffix: $passes passed, $fails failed"
[ $fails = 0 ]
