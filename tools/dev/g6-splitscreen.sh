#!/usr/bin/env bash
# G-6 split-screen test (docs/porting/analysis/splitscreen.md): players 2, 3 and 4 join on SDL virtual pads, then
# numbers: viewport count and rectangles, which player each pad / the keyboard / the mouse moves, players leaving,
# and pictures (GPU, headless) measured per sub-viewport by tools/dev/g6/g6-measure.py.
#
#   tools/dev/g6-splitscreen.sh [<tree>] [part ...]     default tree g6 (tools/dev/mkrun-uc2.sh build-native g6)
#   parts: input   Xvfb + Mesa llvmpipe: joins in a match, layouts, pad/keyboard/mouse separation, leaving
#          menu    Xvfb: the menus' own flow (pad 2's Start on the character page, the match with two players,
#                  back to the main menu)
#          render  the machine's GPU through SDL's offscreen driver (as tools/dev/fx-owner.sh UC2_GPU=1): one
#                  view single-player, the same view as player 1 and as player 2 with 2, 3 and 4 players,
#                  post-processing on/off, the HUD alone (TOGGLERENDERLEVEL); pictures with the dynamic gamma
#                  ramps applied as the present pass applies them (test key ShotRamp); measured by g6-measure.py
#          (default: input render menu)
#   G6_RES=1280x720   window size;   G6_OUT=<dir>  output (default <WorkUC2>/logs/g6)
#
# Every game run is one <WorkUC2>/bin/capped 2G run (CLAUDE.md). Settings go in with -ini: (no switches): the test
# channel [SDL2Drv.SDL2Client] InputTestFile=, [UC2Movie] StartupMovies=False, the window size. PASS/FAIL per check
# in <out>/g6-<part>.result; one summary line per part on stdout.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=g6
if [ $# -gt 0 ] && [ -d "$work/run/$1" ]; then tree=$1; shift; fi
parts=${*:-input render menu}
sys=$work/run/$tree/System
out=${G6_OUT:-$work/logs/g6}
res=${G6_RES:-1280x720}; W=${res%x*}; H=${res#*x}
inject=$repo/tools/dev/m10-inject.py
checks=$repo/tools/dev/g6/g6-checks.py
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }
mkdir -p "$out"

# run_game <name> <gpu:0|1> <seconds> <url> <scenario-file> [more uc2.bin arguments]
run_game() {
  local name=$1 gpu=$2 secs=$3 url=$4 scen=$5; shift 5
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d; /^Suppress=DevSplitScreen\r\?$/d' "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$out/$name".result* "$out/$name"-*.png
  rm -rf "$sys/Profiles" "$work/run/$tree/Save"/*.pro 2>/dev/null; mkdir -p "$sys/Profiles"
  local chan=$out/$name.channel; : > "$chan"
  local video=""; [ "$gpu" = 1 ] && video="SDL_VIDEODRIVER=offscreen"
  local args=(-log -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan"
    "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=$W" "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY=$H"
    "-ini:System:[SDL2Drv.SDL2Client]:FullscreenViewportX=$W" "-ini:System:[SDL2Drv.SDL2Client]:FullscreenViewportY=$H" "$@")
  local inner; inner=$(mktemp)
  cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy $video ./uc2.bin ${url:+"$url"} ${args[@]@Q} > "$out/$name.out" 2>&1 &
pid=\$!
M10_UIMAP=${res} python3 "$inject" "$scen" "$out/$name.out" "$chan" "$out/$name.result" > "$out/$name.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
exit 0
INNER
  local sw=$W sh=$H; [ "$sw" -lt 1280 ] && sw=1280; [ "$sh" -lt 720 ] && sh=720
  "$work/bin/capped" 2G "$secs" -- xvfb-run -a -s "-screen 0 ${sw}x${sh}x24" bash "$inner"
  local rc=$?
  rm -f "$inner"
  if [ -f "$out/$name.result.shots" ]; then
    local i=0 shot
    while read -r shot; do
      local bmp; bmp=$(printf "%s/Shot%05d.bmp" "$sys" "$i")
      [ -f "$bmp" ] && python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$bmp" "$out/$name-$shot.png"
      i=$((i+1))
    done < "$out/$name.result.shots"
    rm -f "$sys"/Shot*.bmp
  fi
  local crit fails passes left=0
  crit=$(grep -a -c "Critical\|Signal: SIG[^T]" "$out/$name.out" 2>/dev/null || true)
  fails=$(grep -c "^FAIL" "$out/$name.result" 2>/dev/null || true)
  passes=$(grep -c "^PASS" "$out/$name.result" 2>/dev/null || true)
  [ -f "$out/$name.result" ] || fails=1
  for p in $(pgrep -x uc2.bin) $(pgrep -x Xvfb); do
    case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"|"$sys"/*) left=$((left+1));; esac
  done
  if [ "${fails:-1}" = 0 ] && [ "${crit:-0}" = 0 ] && [ "${passes:-0}" -gt 0 ]; then
    echo "PASS $name ($passes checks, 0 Critical/signal, capped exit $rc, left running $left; load $(cut -d' ' -f1 /proc/loadavg))"
  else
    echo "FAIL $name ($passes passed, $fails failed, $crit Critical/signal, capped exit $rc, left running $left; see $out/$name.result)"
  fi
}

# The match: DM-Deadbolt without bots, pad 0 = player one (its trigger starts the match), invulnerable.
match_start() {
  cat <<SCEN
pyfile $checks
wait Bringing Level DM-Deadbolt.MyLevel up for play 150
sleep 6
pad 0 attach
sleep 1
pad 1 attach
sleep 1
pad 2 attach
sleep 1
pad 3 attach
sleep 1
seek 20 P['_'].pawn == 1 ;; padtrigger righttrigger
sleep 2
chan exec God
chan exec SetAutoCenterPitch 0
sleep 1
SCEN
}

# join <pad>: Start on the pad (SplitScreenJoinInMatch), then its trigger (spawn into the match).
join() {
  cat <<SCEN
pad $1 button start 1
sleep 0.3
pad $1 button start 0
expect join-pad$1 Split screen: pad $1 Start in the match: joined 20
sleep 3
check join-pad$1-spawned spawn($1)
chan exec set PlayerController bGodMode True
chan exec set UCPlayerController bAutoCenterPitch False
sleep 1
SCEN
}

scen_input() {
  match_start
  cat <<SCEN
focus
vprobe n1
check n1-one-full-window-viewport layout_ok('n1') and VS['n1'][1] == 0
SCEN
  local k
  for k in 1 2 3; do
    join $k
    cat <<SCEN
vprobe n$((k+1))
check n$((k+1))-layout-rectangles layout_ok('n$((k+1))') and VS['n$((k+1))'][1] == $((k+1))
check n$((k+1))-pads-in-join-order pads_in_join_order('n$((k+1))')
check n$((k+1))-every-player-has-pawn-and-split-hud all_split_huds('n$((k+1))')
pyexec dump('$out/g6-input-n$((k+1)).json')
SCEN
  done
  # Each pad's left stick moves its own player only.
  for k in 0 1 2 3; do
    cat <<SCEN
vprobe p${k}a
pad $k axis lefty -32767
sleep 1.2
pad $k axis lefty 0
sleep 0.7
vprobe p${k}b
pad $k axis lefty 32767
sleep 1.2
pad $k axis lefty 0
sleep 0.7
vprobe p${k}c
pyexec print('  moved', moved('p${k}a','p${k}b'), moved('p${k}b','p${k}c'))
check pad$k-left-stick-moves-player$((k+1))-only only_moved2('p${k}a','p${k}b','p${k}c',$k)
SCEN
  done
  # Each pad's fire (right trigger) fires its own player's weapon only (pad buttons go through the engine's
  # input events and every viewport's interactions; the sticks through the viewport's own UInput).
  for k in 0 1 2 3; do
    cat <<SCEN
sleep 2
vprobe f${k}a
pad $k axis righttrigger 32767
sleep 0.6
pad $k axis righttrigger 0
sleep 0.6
vprobe f${k}b
pyexec print('  fired', fired('f${k}a','f${k}b'))
check pad$k-fire-fires-player$((k+1))-only only_fired('f${k}a','f${k}b',$k)
SCEN
  done
  # Each pad's right stick turns its own player only.
  for k in 0 2; do
    cat <<SCEN
vprobe t${k}a
pad $k axis rightx 32767
sleep 0.8
pad $k axis rightx 0
sleep 0.5
vprobe t${k}b
pyexec print('  turned', turned('t${k}a','t${k}b'))
check pad$k-right-stick-turns-player$((k+1))-only only_turned('t${k}a','t${k}b',$k)
SCEN
  done
  cat <<SCEN
vprobe ka
keydown w
sleep 1.2
keyup w
sleep 0.7
vprobe kb
keydown s
sleep 1.2
keyup s
sleep 0.7
vprobe kc
pyexec print('  moved', moved('ka','kb'), moved('kb','kc'))
check keyboard-ws-moves-player1-only only_moved2('ka','kb','kc',0)
vprobe ma
rmove 200 0 20
sleep 0.7
vprobe mb
pyexec print('  turned', turned('ma','mb'))
check mouse-turns-player1-only only_turned('ma','mb',0)
chan exec SPLIT REMOVE
sleep 3
vprobe r3
check leave-to-3-layout layout_ok('r3') and VS['r3'][1] == 3 and all_split_huds('r3')
chan exec SPLIT REMOVE
sleep 3
vprobe r2
check leave-to-2-layout layout_ok('r2') and VS['r2'][1] == 2 and all_split_huds('r2')
chan exec SPLIT REMOVE
sleep 3
vprobe r1
check leave-to-1-full-window layout_ok('r1') and VS['r1'][0] == 1 and V['r1'][0]['hud'] == 'UCHUD' and V['r1'][0]['pawn'] == 1
probe after
keydown w
sleep 1
keyup w
sleep 0.5
probe after2
keydown s
sleep 1
keyup s
sleep 0.5
probe after3
check keyboard-still-player1-after-leaving dist('after','after2') + dist('after2','after3') > 50
SCEN
  join 1
  cat <<SCEN
vprobe j2
check rejoin-after-leaving layout_ok('j2') and VS['j2'][1] == 2 and pads_in_join_order('j2')
SCEN
}

# Fixed views. A = DM-Deadbolt's lamp-lit corridor of tools/dev/fps-bench.sh (emissive glow and bloom), B/C/D other
# open spots. NETPLACE by the player's name (vprobe's player=), twice (the pitch holds once the pawn stands).
place_cmd() {  # place_cmd <split index> <x y z yaw pitch>
  echo "pyexec chan('exec NETPLACE ' + vsplit('_p')[$1]['player'] + ' $2')"
}
SPOT_A="1977.8 583.7 -34.1 33000 -1400"
SPOT_B="-2367.6 3733.8 -482.1 0 -600"
SPOT_C="-1908.7 -1097.8 -130.1 16384 -600"
SPOT_D="1025.2 1698.1 -528.2 -16384 -600"
SPOT_E="1531.9 3111.5 -34.1 0 -600"

shots() {  # shots <tag>: the picture, post-processing off, the HUD alone, the picture without HUD
  cat <<SCEN
sleep 1.5
shot $1
chan exec POSTPROCESS
sleep 1.5
shot $1-nopp
chan exec POSTPROCESS
sleep 1
chan exec TOGGLERENDERLEVEL
sleep 1.5
shot $1-hud
chan exec TOGGLERENDERLEVEL
chan exec set HUD bHideHUD True
sleep 1.5
shot $1-nohud
chan exec POSTPROCESS
sleep 1.5
shot $1-nohud-nopp
chan exec POSTPROCESS
chan exec set HUD bHideHUD False
sleep 1
vprobe $1
pyexec dump('$out/g6-render-$1.json')
SCEN
}

# place_all <spot of split 0> <spot of split 1> ...
# The player going to spot A is placed last, after whoever stood there has left (a pawn put onto another is a
# telefrag).
place_all() {
  echo "vprobe _p"
  local pass i s
  for pass in 1 2; do
    i=0; for s in "$@"; do [ "$s" != "$SPOT_A" ] && place_cmd $i "$s"; i=$((i+1)); done
    echo "sleep 1"
    i=0; for s in "$@"; do [ "$s" = "$SPOT_A" ] && place_cmd $i "$s"; i=$((i+1)); done
    echo "sleep 2"
  done
}

scen_render() {
  match_start
  cat <<SCEN
vprobe _p
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $SPOT_A')
sleep 2
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $SPOT_A')
sleep 2
chan exec set LevelInfo TimeDilation 0.0001
SCEN
  shots n1
  echo "chan exec set LevelInfo TimeDilation 1"
  # Every move goes to a free spot (a pawn put onto another is a telefrag, god mode or not): the targets below
  # never name a spot someone else still stands on, and the player for spot A moves last.
  join 1
  place_all "$SPOT_B" "$SPOT_A"; echo "chan exec set LevelInfo TimeDilation 0.0001"; shots n2b
  echo "chan exec set LevelInfo TimeDilation 1"
  place_all "$SPOT_A" "$SPOT_C"; echo "chan exec set LevelInfo TimeDilation 0.0001"; shots n2a
  echo "chan exec set LevelInfo TimeDilation 1"
  join 2
  place_all "$SPOT_A" "$SPOT_C" "$SPOT_D"; echo "chan exec set LevelInfo TimeDilation 0.0001"; shots n3
  echo "chan exec set LevelInfo TimeDilation 1"
  join 3
  place_all "$SPOT_A" "$SPOT_C" "$SPOT_D" "$SPOT_E"; echo "chan exec set LevelInfo TimeDilation 0.0001"; shots n4
  echo "chan exec set LevelInfo TimeDilation 1"
  place_all "$SPOT_B" "$SPOT_C" "$SPOT_D" "$SPOT_A"; echo "chan exec set LevelInfo TimeDilation 0.0001"; shots n4d
  echo "pyexec print('  pawns', [r['pawn'] for r in vsplit('n4d')])"
  echo "check render-every-player-alive-at-the-end all(r['pawn'] == 1 for r in vsplit('n4d'))"
}

# The menus' own flow (UC2's two-player split screen): main menu -> Instant Action -> DeathMatch -> DM-Deadbolt ->
# character page, where pad 1's Start joins player two (UC2Arena_Character allows it); both choose and launch.
scen_menu() {
  cat <<SCEN
pyfile $checks
wait OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
focus
pad 0 attach
sleep 1
pad 1 attach
sleep 1
key Down
sleep 0.5
key Return
wait OpenMenu \\[UCMenu.UC2Arena_Main\\] 15
wait OpenMenu \\[GUI.GUIMessageBox\\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
key Escape
sleep 1.5
seek 10 P['_'].item.startswith('DeathMatch|') ;; key Down
key Return
sleep 2
key Return
sleep 2
seek 40 'DM-Deadbolt' in P['_'].item ;; key Down
key Return
expect menu-character-page OpenMenu \\[UCMenu.UC2Arena_Character\\] 15
sleep 3
vprobe c1
pad 1 button start 1
sleep 0.3
pad 1 button start 0
expect menu-pad1-start-joins-on-character-page Split screen: pad 1 Start in the menus: joined 15
sleep 3
vprobe c2
check menu-two-split-views-overlay-draws-menu VS['c2'][1] == 2 and [r['disabled'] for r in vsplit('c2')] == [1, 1] and [r for r in V['c2'] if r['overlay']][0]['scene'] == 1
shot character-two-players
pad 1 button a 1
sleep 0.3
pad 1 button a 0
sleep 2
key Return
sleep 3
shot after-choice
SCEN
  # Both players accept through armory and launch (the keyboard drives player one, pad 1 player two).
  local i
  for i in 1 2 3 4 5 6; do
    cat <<SCEN
pad 1 button a 1
sleep 0.3
pad 1 button a 0
sleep 1.5
key Return
sleep 2.5
SCEN
  done
  cat <<SCEN
expect menu-travel-to-deathmatch Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
padtrigger righttrigger
sleep 1
pad 1 axis righttrigger 32767
sleep 0.3
pad 1 axis righttrigger 0
sleep 4
vprobe m2
check menu-match-has-two-split-players layout_ok('m2') and VS['m2'][1] == 2 and pads_in_join_order('m2') and all_split_huds('m2')
shot match-two-players
key Escape
expect menu-pause-menu-opens OpenMenu 15
sleep 2
shot pause-two-players
SCEN
}

for part in $parts; do
  scen=$(mktemp)
  case $part in
    input)  scen_input  > "$scen"; run_game g6-input  0 420 "DM-Deadbolt?NumBots=0" "$scen";;
    render) scen_render > "$scen"; run_game g6-render 1 480 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True
            python3 "$repo/tools/dev/g6/g6-measure.py" "$out" > "$out/g6-measure.txt" 2>&1; tail -n 30 "$out/g6-measure.txt";;
    menu)   scen_menu   > "$scen"; run_game g6-menu   0 420 "" "$scen";;
    *) echo "unknown part $part" >&2;;
  esac
  cp "$scen" "$out/g6-$part.scen"; rm -f "$scen"
done
