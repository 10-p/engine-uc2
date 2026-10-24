#!/usr/bin/env bash
# Split-screen test: players 2, 3 and 4 join on SDL virtual pads, then
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
#          audio   Xvfb, audio to a .wav through OpenAL Soft's wave writer, the cue log (DevSound): player one's
#                  weapon single-player and with four players, player three's with four: cue lines per shot
#          fov     GPU as render: the same frozen frame with AutoFOV on unclamped (Hor+) and with Epic's maths,
#                  1-4 players, and a second run with the default [ufront.AutoFOV] SplitClampToFullScreen=True (no
#                  viewport wider than the full-window FOV); one sheet per layout
#                  (<out>/fov-<n>players.png) with every viewport's FOV from the engine's "[AutoFOV] shown" lines
#          detail  GPU, DM-Praxis: coronas, shadows, decals single player and in a 4-player quadrant
#          bench4  GPU: four players at fixed spots, the engine's FPS log (G6_BENCH_SECS=20, G6_BENCH_NAME)
#          kbplayer Xvfb, PadsStartAtPlayerTwo=True (not the default):
#                  keyboard+mouse player one alone, a pad that joins as player two, every device moving/turning/firing
#                  its own player only, unplug (Epic's page for the pad-only player)/replug, leave, rejoin; then the
#                  menus' flow with one pad (menu_kb=1)
#          h3menu  Xvfb: the flow NETWORKED COMBAT -> HOST SYSTEM LINK GAME -> character page, the pad
#                  plugged in there; ticked (PadsStartAtPlayerTwo=True) and unticked; Arclite + a pad player, two views
#          unplug  Xvfb, the default PadsStartAtPlayerTwo=False: player one's pad (with the keyboard and mouse)
#                  pulled out opens no menu and pauses nothing, plugged back it is player one's again; a pad-only
#                  player two pulled out gets Epic's ControllerRemovedMenu, its Start closes it after the re-plug
#          hud     GPU as render: where each viewport's HUD sits, 1-4 players joined in the match, player one
#                  at spot A, frozen frame: the picture and the HUD alone (TOGGLERENDERLEVEL); measured per viewport
#                  by tools/dev/g6/b3-hud-measure.py (health bar, weapon list, score box, reticle in design pixels).
#                  G6_RES and G6_ARGS (-ini:System:[SDL2Drv.SDL2Client]:UIScaleMode=...) choose the shape; G6_HUD_URL
#                  the match (default DM-Deadbolt?NumBots=0; e.g. ...?Mutator=UCMutators.MutRetribution for its HUD items)
#          x1probe Xvfb: UC2's menu flow with pads 0 and 1, then pads 2 and 3 press Start on the character page
#                  (no player: GUIController.AllowControllerJoin's MAX_VIEWPORTS=2) and in the match (players 3 and 4),
#                  player three's pause menu, back to the main menu
#          x1menu4 Xvfb: four players from the character page (patch set players34): pads 1, 2, 3 press
#                  Start there, each pad moves its own cursor only, the line under the honeycomb, players three and four
#                  leave (Back, the pad pulled out) and come back, choose, the 2x2 armory (part B: all four choose;
#                  x1armory checks it), the match with four players (own classes and names, the split layout), player three's
#                  pause menu, back to the main menu; no script out-of-bounds line. On a tree with Epic's shipped
#                  GUI.u/UCMenu.u (no UI_Char_3PSelector in UCMenu.u) the vanilla variant: two players on the page, three
#                  and four join in the match, no crash. Pictures <out>/x1-menu4-page-{1..4}.png, -match-four.png
#          x1armory Xvfb: the 2x2 armory (patch set players34, UC2Arena_QuadArmory): four players, then three
#                  (X1A_PLAYERS="4 3"), join on the character page, choose, the armory: each pad moves its own quarter only
#                  (the page's "players34 armory" lines name the port), B takes a choice back, the match waits for
#                  everybody; then the match: the split players' spawn URLs and NETPROBE's PlayerReplicationInfo carry the
#                  weapons each chose. Pictures <out>/x1-armory<n>-armory-<n>.png, -armory-<n>-chosen.png, -armory-<n>-match.png
#          hudmenu Xvfb: the menu flow (two players joined on the character page, Epic's own path), the
#                  match's HUDs alone, measured as hud's n2 (<out>/hudmenu/)
#          (default: input render menu audio; the parts use the default PadsStartAtPlayerTwo=False unless they say otherwise)
#   G6_RES=1280x720   window size;   G6_OUT=<dir>  output (default <repo>/../logs/g6)
#   G6_ARGS="-ini:..."  more settings for every game run (e.g. -ini:System:[SDL2Drv.SDL2Client]:SplitScreenFullDetail=True)
#
# Every game run is one <repo>/../bin/capped 2G run (CLAUDE.md). Settings go in with -ini: (no switches): the test
# channel [SDL2Drv.SDL2Client] InputTestFile=, [UC2Movie] StartupMovies=False, the window size. PASS/FAIL per check
# in <out>/g6-<part>.result; one summary line per part on stdout.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=g6
if [ $# -gt 0 ] && [ -d "$work/run/$1" ]; then tree=$1; shift; fi
parts=${*:-input render menu audio}
sys=$work/run/$tree/System
out=${G6_OUT:-$work/logs/g6}
res=${G6_RES:-1280x720}; W=${res%x*}; H=${res#*x}
inject=$repo/tools/dev/m10-inject.py
checks=$repo/tools/dev/g6/g6-checks.py
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }
mkdir -p "$out"
# The parts test the default model ([SDL2Drv.SDL2Client] PadsStartAtPlayerTwo=False), where the first pad drives
# player one with the keyboard; the kbplayer part sets PadsStartAtPlayerTwo=True, keyboard+mouse alone.
KBPLAYER=-ini:System:[SDL2Drv.SDL2Client]:PadsStartAtPlayerTwo=True

# run_game <name> <gpu:0|1> <seconds> <url> <scenario-file> [more uc2.bin arguments]
run_game() {
  local name=$1 gpu=$2 secs=$3 url=$4 scen=$5; shift 5
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d; /^Suppress=DevSplitScreen\r\?$/d; /^Suppress=DevSound\r\?$/d' "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$out/$name".result* "$out/$name"-*.png
  rm -rf "$sys/Profiles" "$work/run/$tree/Save"/*.pro 2>/dev/null; mkdir -p "$sys/Profiles"
  local chan=$out/$name.channel; : > "$chan"
  local video=""; [ "$gpu" = 1 ] && video="SDL_VIDEODRIVER=offscreen"
  local args=(-log -UFRONTLOG -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan"
    "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=$W" "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY=$H"
    "-ini:System:[SDL2Drv.SDL2Client]:FullscreenViewportX=$W" "-ini:System:[SDL2Drv.SDL2Client]:FullscreenViewportY=$H" "$@" ${G6_ARGS:-})
  local inner; inner=$(mktemp)
  cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x0000/0x0000 $video ./uc2.bin ${url:+"$url"} ${args[@]@Q} > "$out/$name.out" 2>&1 &
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
SCEN
  # The keyboard: W S A D in turn. During each hold the input's walk (the pawn's acceleration, whatever stands in
  # the way) must be player one's only; over the four, player one must move and nobody else. (Walk distances
  # alone depend on where the earlier steps left player one: facing a wall after the right stick's turn, W
  # moves 0.)
  local k
  echo "vprobe k0"
  for k in w s a d; do
    cat <<SCEN
keydown $k
sleep 0.8
vprobe kh$k
sleep 0.4
keyup $k
sleep 0.7
vprobe k$k
pyexec print('  key $k: acceleration', [r['acc'] for r in vsplit('kh$k')])
check keyboard-$k-walks-player1-only only_accel('kh$k',0)
SCEN
  done
  cat <<SCEN
pyexec print('  keyboard moved', moved('k0','kw'), moved('kw','ks'), moved('ks','ka'), moved('ka','kd'))
check keyboard-wsad-moves-player1-only only_moved4(['k0','kw','ks','ka','kd'],0)
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
sleep 5
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

# The split-screen FOV A/B pictures. The same frozen frame is shot with AutoFOV on (AUTOFOV 2, the default: Hor+
# per viewport) and with Epic's maths (AUTOFOV 0: SSDetailLevels' ReduceFOV, the cropped strips), single player and
# with 2, 3 and 4 players, player one at spot A. Each shot is preceded by a "g9fov <tag>" log marker; the engine's
# "[AutoFOV] shown" lines before it give every viewport's horizontal and vertical FOV (tools/dev/g6/g9-fov-sheet.py).
fovshots() {  # fovshots <tag>
  local m v
  for m in 2 0; do
    v=on; [ "$m" = 0 ] && v=epic
    cat <<SCEN
chan exec AUTOFOV $m
sleep 2
chan log g9fov $1-$v
shot $1-$v
vprobe $1-$v
pyexec dump('$out/${fovrun:-g9-fov}-$1-$v.json')
SCEN
  done
  echo "chan exec AUTOFOV 2"
}

scen_fov() {
  match_start
  cat <<SCEN
vprobe _p
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $SPOT_A')
sleep 2
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $SPOT_A')
sleep 5
chan exec set LevelInfo TimeDilation 0.0001
SCEN
  fovshots n1
  echo "chan exec set LevelInfo TimeDilation 1"
  join 1
  place_all "$SPOT_A" "$SPOT_C"; echo "chan exec set LevelInfo TimeDilation 0.0001"; fovshots n2
  echo "chan exec set LevelInfo TimeDilation 1"
  join 2
  place_all "$SPOT_A" "$SPOT_C" "$SPOT_D"; echo "chan exec set LevelInfo TimeDilation 0.0001"; fovshots n3
  echo "chan exec set LevelInfo TimeDilation 1"
  join 3
  place_all "$SPOT_A" "$SPOT_C" "$SPOT_D" "$SPOT_E"; echo "chan exec set LevelInfo TimeDilation 0.0001"; fovshots n4
}

# The HUD of every viewport, 1-4 players. The frame is frozen long after the last placement, so the join and
# spawn messages are gone; the picture, then the HUD alone (the level not drawn: black around the HUD).
hudshots() {  # hudshots <tag>
  cat <<SCEN
sleep 8
chan exec set LevelInfo TimeDilation 0.0001
sleep 1.5
shot $1
chan exec TOGGLERENDERLEVEL
sleep 1.5
shot $1-hud
chan exec TOGGLERENDERLEVEL
vprobe $1
pyexec dump('$out/b3-hud-$1.json')
chan exec set LevelInfo TimeDilation 1
SCEN
}

scen_hud() {
  match_start
  cat <<SCEN
vprobe _p
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $SPOT_A')
sleep 2
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $SPOT_A')
SCEN
  hudshots n1
  join 1; place_all "$SPOT_A" "$SPOT_C"; hudshots n2
  join 2; place_all "$SPOT_A" "$SPOT_C" "$SPOT_D"; hudshots n3
  join 3; place_all "$SPOT_A" "$SPOT_C" "$SPOT_D" "$SPOT_E"; hudshots n4
  echo "check hud-every-player-alive-at-the-end all(r['pawn'] == 1 for r in vsplit('n4'))"
}

# The frame rate with four players (GPU): the four placed at spots A C D E (fixed views, time running), then the
# engine's FPS log over G6_BENCH_SECS (20) seconds. The line goes to <out>/<name>.fps.
scen_bench4() {
  match_start
  join 1; join 2; join 3
  place_all "$SPOT_A" "$SPOT_C" "$SPOT_D" "$SPOT_E"
  cat <<SCEN
sleep 2
vprobe b4
check bench-four-players layout_ok('b4') and VS['b4'][1] == 4
chan exec FPSLOG ${G6_BENCH_SECS:-20}
wait [0-9.]+ s [0-9]+ frames [0-9.]+ fps; frame ms $(( ${G6_BENCH_SECS:-20} + 40 ))
chan exec FPSLOG 0
sleep 0.5
SCEN
}

# Split-screen detail where the single player shows coronas, shadows and decals: DM-Praxis (DM-Deadbolt has no
# corona light at all: the t3d export has none of its 176 lights with bCorona). The spots (found with the corona part):
# P1 looks up at the ring of corona lights over the round room, P2 looks at the ceiling slab the ring is behind (no
# corona in a single player's view: the corona's line check is occluded), P3/P4 elsewhere (path nodes, not player
# starts: a player who spawns on a start would be telefragged by the one placed there); PD looks steeply down at the
# floor in front (the three shots leave decals there, the pawn's shadow falls there). Single player: every spot shot with the client's
# coronas on and off (s<k>, s<k>nc), then PD after three shots (sd). Four players at P1..P4: the picture with coronas
# on and off (q, qnc), then player one at PD after three shots (qd). tools/dev/g6/g9-detail.py --praxis counts, per
# quadrant, the pixels a corona lights and compares them with the single player at the same spot: player one's
# quadrant must show its coronas (full detail), and no quadrant may show more than its single player (a corona seen by
# another player drawn through a wall). Run with each SplitScreenFullDetail (G6_ARGS).
G9_P1="632 503 380 26372 9000"
G9_P2="-1220 288 690 3929 7000"
G9_P3="265 138 360 0 0"
G9_P4="436 1220 360 32768 0"
G9_PD="632 503 380 26372 -8000"
# coronas: single player on DM-Praxis, each of G9_SPOTS ("x y z yaw pitch;...") shot with the client's coronas on and
# off (g9-corona-c<i>.png / -c<i>nc.png): where a view shows coronas (finding the spot for the detail part).
scen_corona() {
  cat <<SCEN
pyfile $checks
wait Bringing Level DM-Praxis.MyLevel up for play 150
sleep 6
pad 0 attach
sleep 1
seek 20 P['_'].pawn == 1 ;; padtrigger righttrigger
sleep 2
chan exec God
chan exec SetAutoCenterPitch 0
sleep 1
vprobe _p
SCEN
  local i=0 spot
  IFS=';' read -ra spots <<< "${G9_SPOTS:-0 770 700 0 10000}"
  for spot in "${spots[@]}"; do
    i=$((i+1))
    cat <<SCEN
chan exec set LevelInfo TimeDilation 1
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $spot')
sleep 2
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $spot')
sleep 3
chan exec set LevelInfo TimeDilation 0.0001
sleep 1.5
shot c$i
chan exec set SDL2Client Coronas False
sleep 1.5
shot c${i}nc
chan exec set SDL2Client Coronas True
sleep 1
SCEN
  done
}
scen_detail() {
  local k spot i
  cat <<SCEN
pyfile $checks
wait Bringing Level DM-Praxis.MyLevel up for play 150
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
vprobe _p
SCEN
  k=0
  for spot in "$G9_P1" "$G9_P2" "$G9_P3" "$G9_P4"; do
    k=$((k+1))
    cat <<SCEN
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $spot')
sleep 2
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $spot')
sleep 3
chan exec set LevelInfo TimeDilation 0.0001
sleep 1.5
shot s$k
chan exec set SDL2Client Coronas False
sleep 1.5
shot s${k}nc
chan exec set SDL2Client Coronas True
sleep 1
chan exec set LevelInfo TimeDilation 1
SCEN
  done
  cat <<SCEN
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $G9_PD')
sleep 2
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $G9_PD')
sleep 2
SCEN
  for i in 1 2 3; do printf 'pad 0 axis righttrigger 32767\nsleep 0.4\npad 0 axis righttrigger 0\nsleep 1.0\n'; done
  cat <<SCEN
pyexec chan('exec NETPLACE ' + V['_p'][0]['player'] + ' $G9_PD')
sleep 2
chan exec set LevelInfo TimeDilation 0.0001
sleep 1.5
shot sd
chan exec set LevelInfo TimeDilation 1
SCEN
  join 1; join 2; join 3
  cat <<SCEN
vprobe _q
pyexec [chan('exec NETPLACE ' + vsplit('_q')[k]['player'] + ' ' + s) for k, s in ((1, '$G9_P2'), (2, '$G9_P3'), (3, '$G9_P4'))]
sleep 2
pyexec [chan('exec NETPLACE ' + vsplit('_q')[k]['player'] + ' ' + s) for k, s in ((0, '$G9_P1'), (1, '$G9_P2'), (2, '$G9_P3'), (3, '$G9_P4'))]
sleep 3
pyexec [chan('exec NETPLACE ' + vsplit('_q')[k]['player'] + ' ' + s) for k, s in ((0, '$G9_P1'), (1, '$G9_P2'), (2, '$G9_P3'), (3, '$G9_P4'))]
sleep 3
chan exec set LevelInfo TimeDilation 0.0001
sleep 1.5
shot q
chan exec set SDL2Client Coronas False
sleep 1.5
shot qnc
chan exec set SDL2Client Coronas True
sleep 1
vprobe q
pyexec dump('$out/g9-detail-q.json')
check detail-four-players layout_ok('q') and VS['q'][1] == 4
chan exec set LevelInfo TimeDilation 1
pyexec chan('exec NETPLACE ' + vsplit('_q')[0]['player'] + ' $G9_PD')
sleep 2
SCEN
  for i in 1 2 3; do printf 'pad 0 axis righttrigger 32767\nsleep 0.4\npad 0 axis righttrigger 0\nsleep 1.0\n'; done
  cat <<SCEN
pyexec chan('exec NETPLACE ' + vsplit('_q')[0]['player'] + ' $G9_PD')
sleep 2
chan exec set LevelInfo TimeDilation 0.0001
sleep 1.5
shot qd
SCEN
}

# Keyboard+mouse as a player of their own ([SDL2Drv.SDL2Client] PadsStartAtPlayerTwo=True). The
# keyboard's click starts the match alone; a virtual pad attached then is unbound (port 1: port 0 is the keyboard's)
# and moves nobody until its Start joins it as player two; then every device drives its own player only; the pad
# unplugged and plugged again comes back to its own player; the pad player leaves (SPLIT REMOVE) and the keyboard is
# player one alone again; the pad joins again.
kb_walks() {  # kb_walks <tag prefix> <split index of the keyboard's player>
  local k
  echo "vprobe $1k0"
  for k in w s a d; do
    cat <<SCEN
keydown $k
sleep 0.8
vprobe $1kh$k
sleep 0.4
keyup $k
sleep 0.7
vprobe $1k$k
pyexec print('  key $k: acceleration', [r['acc'] for r in vsplit('$1kh$k')])
check $1keyboard-$k-walks-keyboard-player-only only_accel('$1kh$k',$2)
SCEN
  done
  cat <<SCEN
pyexec print('  keyboard moved', moved('$1k0','$1kw'), moved('$1kw','$1ks'), moved('$1ks','$1ka'), moved('$1ka','$1kd'))
check $1keyboard-wsad-moves-keyboard-player-only only_moved4(['$1k0','$1kw','$1ks','$1ka','$1kd'],$2)
SCEN
}

pad_walk() {  # pad_walk <tag> <virtual pad> <split index>
  cat <<SCEN
vprobe $1a
pad $2 axis lefty -32767
sleep 1.2
pad $2 axis lefty 0
sleep 0.7
vprobe $1b
pad $2 axis lefty 32767
sleep 1.2
pad $2 axis lefty 0
sleep 0.7
vprobe $1c
pyexec print('  moved', moved('$1a','$1b'), moved('$1b','$1c'))
check $1-pad-left-stick-moves-its-player-only only_moved2('$1a','$1b','$1c',$3)
SCEN
}

scen_kbplayer() {
  cat <<SCEN
pyfile $checks
wait Bringing Level DM-Deadbolt.MyLevel up for play 150
sleep 6
focus
seek 20 P['_'].pawn == 1 ;; click 1
sleep 2
chan exec God
chan exec SetAutoCenterPitch 0
sleep 1
vprobe k1
check kb-alone-full-window-on-port-0 layout_ok('k1') and VS['k1'][1] == 0 and V['k1'][0]['pad'] == 0 and V['k1'][0]['hud'] == 'UCHUD'
pad 0 attach
expect kb-pad-connects-on-port-1 SDL2 pads: pad 1 connected 15
sleep 2
vprobe k1p
check kb-pad-attached-takes-no-player VS['k1p'][:2] == (1, 0) and V['k1p'][0]['pad'] == 0
pad 0 axis lefty -32767
sleep 1.2
pad 0 axis lefty 0
sleep 0.7
vprobe k1q
pyexec print('  unjoined pad: player one moved', round(dist_v0('k1p','k1q'), 1))
check kb-unjoined-pad-moves-nobody dist_v0('k1p','k1q') < 2.0
pad 0 button start 1
sleep 0.3
pad 0 button start 0
expect kb-pad-start-joins-player-two Split screen: pad 1 Start in the match: joined 20
sleep 3
check kb-pad-player-spawned spawn_port(1, 0)
chan exec set PlayerController bGodMode True
chan exec set UCPlayerController bAutoCenterPitch False
sleep 1
vprobe k2
pyexec print('  ports', ports('k2'))
check kb-two-views-own-rectangles-keyboard-port0-pad-port1 layout_ok('k2') and VS['k2'][1] == 2 and ports('k2') == [0, 1] and all_split_huds('k2')
SCEN
  kb_walks "" 0
  pad_walk p1 0 1
  cat <<SCEN
vprobe ma
rmove 200 0 20
sleep 0.7
vprobe mb
pyexec print('  mouse turned', turned('ma','mb'))
check kb-mouse-turns-keyboard-player-only only_turned('ma','mb',0)
vprobe ta
pad 0 axis rightx 32767
sleep 0.8
pad 0 axis rightx 0
sleep 0.5
vprobe tb
pyexec print('  right stick turned', turned('ta','tb'))
check kb-pad-right-stick-turns-pad-player-only only_turned('ta','tb',1)
sleep 2
vprobe fka
click 1
sleep 0.8
click 1
sleep 0.8
click 1
sleep 1.2
vprobe fkb
pyexec print('  mouse fire', fired('fka','fkb'))
check kb-mouse-fire-fires-keyboard-player-only only_fired('fka','fkb',0)
sleep 2
vprobe fpa
pad 0 axis righttrigger 32767
sleep 0.6
pad 0 axis righttrigger 0
sleep 1.2
vprobe fpb
pyexec print('  pad fire', fired('fpa','fpb'))
check kb-pad-fire-fires-pad-player-only only_fired('fpa','fpb',1)
pad 0 detach
expect kb-pad-unplugged-opens-epics-controller-removed-menu OpenMenu \\[UCMenu.ControllerRemovedMenu\\] 15
expect kb-pad-unplugged SDL2 pads: pad 1 disconnected 15
sleep 2
vprobe u1
check kb-pad-unplugged-its-player-stays VS['u1'][1] == 2 and ports('u1') == [0, 1] and all(r['pawn'] == 1 for r in vsplit('u1'))
pad 0 attach
expect kb-pad-plugged-again-port-1 SDL2 pads: pad 1 connected 15
sleep 2
seek 6 P['_'].menu == 'None' ;; padpress start
check kb-replugged-pad-start-closes-the-menu P['_'].menu == 'None'
sleep 1
SCEN
  pad_walk p1r 0 1
  cat <<SCEN
chan exec SPLIT REMOVE
sleep 3
vprobe r1
check kb-pad-player-leaves-keyboard-alone-full-window layout_ok('r1') and VS['r1'][:2] == (1, 0) and V['r1'][0]['pad'] == 0 and V['r1'][0]['hud'] == 'UCHUD' and V['r1'][0]['pawn'] == 1
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
check kb-keyboard-still-player-one-after-the-pad-left dist('after','after2') + dist('after2','after3') > 50
vprobe r1a
pad 0 axis lefty -32767
sleep 1.2
pad 0 axis lefty 0
sleep 0.7
vprobe r1b
check kb-left-pad-moves-nobody dist_v0('r1a','r1b') < 2.0
pad 0 button start 1
sleep 0.3
pad 0 button start 0
expect kb-pad-joins-again Split screen: pad 1 Start in the match: joined 20
sleep 3
check kb-pad-player-spawned-again spawn_port(1, 0)
vprobe j2
check kb-rejoined-two-views layout_ok('j2') and VS['j2'][1] == 2 and ports('j2') == [0, 1]
SCEN
  kb_walks j 0
}

# Player one never gets the "gamepad disconnected" message: the keyboard and the mouse are always there. With the
# default PadsStartAtPlayerTwo=False virtual pad 0 takes port 0 and is player one together with the keyboard and the
# mouse (either drives it). Pulled out: SDL2Drv sends no JoySlider2 (no ControllerRemoved: no HUD flag, no
# ControllerRemovedMenu, no pause), the keyboard goes on walking player one; plugged back: port 0 again, player one's
# at once, its stick walks player one. Then virtual pad 1 joins as player two (a pad-only player): pulled out, Epic's
# ControllerRemovedMenu opens for it as on the Xbox; plugged back, its Start closes the page.
kb_walk1() {  # kb_walk1 <tag>: W then S on the keyboard, player one (the one viewport or split view 0)
  cat <<SCEN
vprobe $1a
keydown w
sleep 1.2
keyup w
sleep 0.7
vprobe $1b
keydown s
sleep 1.2
keyup s
sleep 0.7
vprobe $1c
pyexec print('  keyboard: player one moved', round(dist_v0('$1a','$1b'), 1), round(dist_v0('$1b','$1c'), 1))
check $1-keyboard-walks-player-one dist_v0('$1a','$1b') + dist_v0('$1b','$1c') > 50
SCEN
}

pad_walk1() {  # pad_walk1 <tag> <virtual pad>: the left stick forward then back, player one (viewport 0)
  cat <<SCEN
vprobe $1a
pad $2 axis lefty -32767
sleep 1.2
pad $2 axis lefty 0
sleep 0.7
vprobe $1b
pad $2 axis lefty 32767
sleep 1.2
pad $2 axis lefty 0
sleep 0.7
vprobe $1c
pyexec print('  pad $2: player one moved', round(dist_v0('$1a','$1b'), 1), round(dist_v0('$1b','$1c'), 1))
check $1-pad-$2-walks-player-one dist_v0('$1a','$1b') + dist_v0('$1b','$1c') > 50
SCEN
}

scen_unplug() {
  cat <<SCEN
pyfile $checks
wait Bringing Level DM-Deadbolt.MyLevel up for play 150
sleep 6
focus
pad 0 attach
expect u-pad0-port-0-is-player-one SDL2 pads: pad 0 connected: .*\\(player one, with the keyboard and mouse\\) 15
sleep 1
seek 20 P['_'].pawn == 1 ;; padtrigger righttrigger
sleep 2
chan exec God
chan exec SetAutoCenterPitch 0
sleep 1
vprobe u0
check u-one-player-on-port-0 layout_ok('u0') and VS['u0'][1] == 0 and V['u0'][0]['pad'] == 0 and V['u0'][0]['pawn'] == 1
SCEN
  pad_walk1 u1 0
  kb_walk1 u2
  cat <<SCEN
mark
pad 0 detach
expect u-pad0-pulled-player-one-keeps-the-keyboard SDL2 pads: pad 0 disconnected: .*no controller-removed page 15
sleep 3
probe ud
pyexec print('  after the pull: menu', P['ud'].menu, 'paused', P['ud'].paused)
absent u-pad0-pulled-no-controller-removed-menu OpenMenu \\[UCMenu.ControllerRemovedMenu\\]
absent u-pad0-pulled-no-JoySlider2-to-player-one pad 0 JoySlider2
check u-pad0-pulled-no-menu-no-pause P['ud'].menu == 'None' and P['ud'].paused == 0 and P['ud'].pawn == 1
vprobe ud
check u-pad0-pulled-player-one-keeps-port-0 V['ud'][0]['pad'] == 0
SCEN
  kb_walk1 u3
  cat <<SCEN
pad 0 attach
expect u-pad0-plugged-back-port-0-player-one SDL2 pads: pad 0 connected: .*\\(player one, with the keyboard and mouse\\) 15
sleep 2
probe ur
absent u-pad0-back-no-controller-removed-menu OpenMenu \\[UCMenu.ControllerRemovedMenu\\]
check u-pad0-back-no-menu-no-pause P['ur'].menu == 'None' and P['ur'].paused == 0
SCEN
  pad_walk1 u4 0
  kb_walk1 u5
  cat <<SCEN
pad 1 attach
expect u-pad1-connects-on-port-1 SDL2 pads: pad 1 connected 15
sleep 1
pad 1 button start 1
sleep 0.3
pad 1 button start 0
expect u-pad1-start-joins-player-two Split screen: pad 1 Start in the match: joined 20
sleep 3
check u-pad1-player-two-spawned spawn(1)
chan exec set PlayerController bGodMode True
chan exec set UCPlayerController bAutoCenterPitch False
sleep 1
vprobe u6
pyexec print('  ports', ports('u6'))
check u-two-players-ports-0-1 layout_ok('u6') and VS['u6'][1] == 2 and ports('u6') == [0, 1]
mark
pad 1 detach
expect u-pad1-pulled-epics-controller-removed-menu OpenMenu \\[UCMenu.ControllerRemovedMenu\\] 15
expect u-pad1-pulled SDL2 pads: pad 1 disconnected 15
absent u-pad1-pulled-not-player-ones disconnected: .*no controller-removed page
sleep 2
probe up
pyexec print('  player two pulled: menu', P['up'].menu, 'paused', P['up'].paused)
check u-pad1-pulled-the-page-is-up P['up'].menu.startswith('ControllerRemovedMenu')
pad 1 attach
expect u-pad1-plugged-back-port-1 SDL2 pads: pad 1 connected 15
sleep 2
seek 6 P['_'].menu == 'None' ;; pyexec (chan('pad 1 button start 1'), time.sleep(0.2), chan('pad 1 button start 0'))
check u-pad1-start-closes-epics-page P['_'].menu == 'None'
sleep 1
vprobe u7
check u-two-players-again-ports-0-1 VS['u7'][1] == 2 and ports('u7') == [0, 1]
SCEN
}

# The flow NETWORKED COMBAT -> HOST SYSTEM LINK GAME -> (the profile box) -> accept three
# times -> the character page ("Player 1 No Profile", "Player 2 Press start"), the pad arriving there by hot-plug (as a
# browser lists a pad only at its first press). h3_ticked=1: PadsStartAtPlayerTwo=True, one pad (virtual pad 0, port 1)
# whose Start joins player two; h3_ticked=0 (default ini): pad 0 attached at the main menu is player one's (port 0),
# pad 1 attached on the character page joins. Player one picks the character to the left (Arclite) with the keyboard,
# player two accepts its own with A; the match must start with two split views, the mouse captured, no cursor. The look
# owner (mouse-look's bMouseOwnsLook) is '-' with Epic's shipped UCGameplay.u (no mouse look: Epic's pad camera for all).
scen_h3menu() {
  local jp=0 jport=1 early="" start="pyexec (click(1), fire_all(1))"
  if [ "${h3_ticked:-1}" = 0 ]; then jp=1; early="pad 0 attach"; start="pyexec (click(1), fire_all(2))"; fi
  cat <<SCEN
pyfile $checks
wait OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
focus
$early
sleep 1
seek 8 P['_'].caption == 'NETWORKED COMBAT' ;; key Down
key Return
wait OpenMenu \\[UCMenu.UC2NetworkMain\\] 15
sleep 2
seek 8 P['_'].caption == 'HOST SYSTEM LINK GAME' ;; key Down
key Return
wait OpenMenu \\[UCMenu.UC2Arena_ServerMain\\] 15
wait OpenMenu \\[GUI.GUIMessageBox\\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
key Escape
sleep 2
key Return
wait handled XC_A 15
sleep 2
key Return
wait handled XC_A 15
sleep 2
key Return
tryseek 10 P['_'].menu == 'UC2Arena_Character' ;; sleep 1
tryseek 0 P['_'].menu == 'UC2Arena_Character' ;; key Return
tryseek 10 P['_'].menu == 'UC2Arena_Character' ;; sleep 1
sleep 3
probe cp
check h3-character-page P['cp'].menu == 'UC2Arena_Character'
vprobe c0
pyexec print('  capture on the character page before the join', VC['c0'])
pad $jp attach
sleep 0.5
pad $jp button start 1
sleep 0.3
pad $jp button start 0
expect h3-pad-on-port-$jport SDL2 pads: pad $jport connected 15
expect h3-pad$jport-start-joins-player-two Split screen: pad $jport Start in the menus: joined 15
sleep 3
vprobe c1
pyexec print('  capture after the join', VC['c1'])
check h3-two-players-on-the-character-page VS['c1'][1] == 2 and sorted(r['pad'] for r in vsplit('c1')) == [0, $jport]
shot h3-character-two-players
sleep 1
pad $jp button b 1
sleep 0.3
pad $jp button b 0
sleep 2
shot h3-character-box-answered
key Left
sleep 1
key Return
sleep 2
pad $jp button a 1
sleep 0.3
pad $jp button a 0
sleep 2
shot h3-after-choice
SCEN
  local i
  for i in 1 2 3 4 5 6; do
    cat <<SCEN
pad $jp button a 1
sleep 0.3
pad $jp button a 0
sleep 1.5
key Return
sleep 2.5
SCEN
  done
  cat <<SCEN
expect h3-travel-to-the-match Bringing Level \\S+.MyLevel up for play 150
sleep 6
seek 20 P['_'].pawn == 1 ;; $start
check h3-player2-spawns spawn_port($jport, $jp)
sleep 2
vprobe m2
pyexec print('  capture in the match', VC['m2'])
check h3-match-has-two-split-players layout_ok('m2') and VS['m2'][1] == 2 and pads_in_join_order('m2')
pyexec print('  pawns', [(r['i'], r['pad'], r['pawnclass']) for r in V['m2']])
check h3-player-one-is-arclite 'Arclite' in vsplit('m2')[0]['pawnclass']
check h3-mouse-captured-in-the-match VC['m2'] == (1, 0)
pyexec vml = lambda tag: [r['mouselook'] for r in vsplit(tag)]
check h3-pad-player-two-has-the-pad-camera vml('m2')[1] in ('False', '-')
shot h3-match-two-players
rmove 60 0 6
sleep 0.5
vprobe x1
check h3-mouse-gives-player-one-the-mouse-camera-not-player-two vml('x1') in (['True', 'False'], ['-', '-'])
SCEN
  # The default mode: player one's own pad (port 0) takes its look back with the left stick alone, the mouse again after.
  [ "${h3_ticked:-1}" = 0 ] && cat <<SCEN
pad 0 axis lefty -32767
sleep 1.2
pad 0 axis lefty 0
sleep 0.5
vprobe x2
check h3-player-one-left-stick-gives-the-pad-camera-back vml('x2') in (['False', 'False'], ['-', '-'])
rmove 60 0 6
sleep 0.5
vprobe x3
check h3-player-one-mouse-again vml('x3') in (['True', 'False'], ['-', '-'])
SCEN
  return 0
}

# The menus' own flow (UC2's two-player split screen): main menu -> Instant Action -> DeathMatch -> DM-Deadbolt ->
# character page, where pad 1's Start joins player two (UC2Arena_Character allows it); both choose and launch.
scen_menu() {
  # menu_kb=1 is the same flow with PadsStartAtPlayerTwo=True: one pad (virtual pad 0 on port 1) joins on the
  # character page, the keyboard is player one throughout (and starts the match with its click).
  local jp=1 jport=1 attach="pad 0 attach
sleep 1
pad 1 attach" start="pyexec fire_all(2)"
  if [ "${menu_kb:-0}" = 1 ]; then jp=0; attach="pad 0 attach"; start="pyexec (click(1), fire_all(1))"; fi
  cat <<SCEN
pyfile $checks
wait OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
focus
$attach
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
pad $jp button start 1
sleep 0.3
pad $jp button start 0
expect menu-pad$jport-start-joins-on-character-page Split screen: pad $jport Start in the menus: joined 15
sleep 3
vprobe c2
check menu-two-split-views-overlay-draws-menu VS['c2'][1] == 2 and [r['disabled'] for r in vsplit('c2')] == [1, 1] and [r for r in V['c2'] if r['overlay']][0]['scene'] == 1
shot character-two-players
pad $jp button a 1
sleep 0.3
pad $jp button a 0
sleep 2
key Return
sleep 3
shot after-choice
SCEN
  # Both players accept through armory and launch (the keyboard drives player one, pad 1 player two). Two
  # players still get Epic's two-panel armory (UC2Arena_Character.CloseComplete opens the 2x2 one for three or four).
  # The page's log is read directly (a probe would move the scenario's expect position past the OpenMenu line); the
  # picture is taken the first time the armory is up after a step.
  local nm=g6-menu; [ "${menu_kb:-0}" = 1 ] && nm=g9-kbmenu; [ "${menu_hud:-0}" = 1 ] && nm=b3-hudmenu
  cat <<SCEN
pyexec ARM2 = lambda: 'OpenMenu [UCMenu.UC2Arena_DualArmory]' in open('$out/$nm.out', encoding='latin-1').read()
pyexec SHOT2 = [0]
SCEN
  local i
  for i in 1 2 3 4 5 6; do
    cat <<SCEN
pad $jp button a 1
sleep 0.3
pad $jp button a 0
sleep 1.5
key Return
sleep 2.5
pyexec (time.sleep(3), chan('exec shot'), open('$out/$nm.result.shots', 'a').write('armory-2\\n'), SHOT2.append(1)) if ARM2() and len(SHOT2) == 1 and 'Bringing Level DM-' not in open('$out/$nm.out', encoding='latin-1').read() else None
SCEN
  done
  cat <<SCEN
check menu-two-players-get-the-dual-armory ARM2() and 'OpenMenu [UCMenu.UC2Arena_QuadArmory]' not in open('$out/$nm.out', encoding='latin-1').read()
SCEN
  cat <<SCEN
expect menu-travel-to-deathmatch Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
seek 20 P['_'].pawn == 1 ;; $start
check menu-player2-spawns spawn_port($jport, $jp)
sleep 2
vprobe m2
check menu-match-has-two-split-players layout_ok('m2') and VS['m2'][1] == 2 and pads_in_join_order('m2') and all_split_huds('m2')
shot match-two-players
SCEN
  # hudmenu: the HUD of two players who joined in the menus (Epic's own path: the split HUDs made at the map's
  # start), alone, for b3-hud-measure.py.
  [ "${menu_hud:-0}" = 1 ] && cat <<SCEN
sleep 8
chan exec set LevelInfo TimeDilation 0.0001
sleep 1.5
shot n2
chan exec TOGGLERENDERLEVEL
sleep 1.5
shot n2-hud
chan exec TOGGLERENDERLEVEL
vprobe n2
pyexec dump('$out/hudmenu/b3-hud-n2.json')
chan exec set LevelInfo TimeDilation 1
SCEN
  cat <<SCEN
key Escape
expect menu-pause-menu-opens OpenMenu \[UCMenu.InGame_MainMenu\] 15
sleep 2
shot pause-two-players
seek 8 P['_'].caption == 'FORFEIT GAME' ;; key Down
key Return
wait OpenMenu \[GUI.GUIMessageBox\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
tryseek 4 P['_'].caption.upper() in ('YES', 'OK', 'ACCEPT', 'FORFEIT') ;; key Up
key Return
expect menu-forfeit-back-to-main-menu OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
vprobe back
check menu-players-leave-back-to-one-viewport layout_ok('back') and VS['back'][0] == 1 and VS['back'][1] == 0
SCEN
  [ "${menu_kb:-0}" = 1 ] && cat <<SCEN
check kbmenu-keyboard-player-one-on-port-0 V['back'][0]['pad'] == 0
pad 0 button start 1
sleep 0.3
pad 0 button start 0
sleep 2
vprobe back2
check kbmenu-pad-start-on-the-main-menu-takes-no-player VS['back2'][:2] == (1, 0) and V['back2'][0]['pad'] == 0
SCEN
  return 0
}

# What a third and a fourth pad's Start do on Epic's character page. UC2's menu
# flow as scen_menu (pad 0 = player one, pad 1 joins on the character page), then pads 2 and 3 press Start on the
# character page (and A, and their stick), then the match with two players, where pads 2 and 3 press Start again
# (SplitScreenJoinInMatch), then player three's Start (the pause menu) and the way back to the main menu.
scen_x1probe() {
  cat <<SCEN
pyfile $checks
wait OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
focus
pad 0 attach
sleep 1
pad 1 attach
sleep 1
pad 2 attach
sleep 1
pad 3 attach
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
expect x1-character-page OpenMenu \\[UCMenu.UC2Arena_Character\\] 15
sleep 3
vprobe c1
shot x1-character-one-player
pad 1 button start 1
sleep 0.3
pad 1 button start 0
expect x1-pad1-start-joins-player-two Split screen: pad 1 Start in the menus: joined 15
sleep 3
vprobe c2
check x1-two-split-views VS['c2'][1] == 2
shot x1-character-two-players
mark
pad 2 button start 1
sleep 0.3
pad 2 button start 0
expect x1-pad2-start-reaches-the-engine pad 2 button 12 value 255 15
sleep 2
pad 3 button start 1
sleep 0.3
pad 3 button start 0
expect x1-pad3-start-reaches-the-engine pad 3 button 12 value 255 15
sleep 2
pad 2 button a 1
sleep 0.3
pad 2 button a 0
sleep 1
pad 2 axis leftx 32767
sleep 1
pad 2 axis leftx 0
sleep 2
absent x1-pad2-no-join-on-the-character-page Split screen: pad 2
absent x1-pad3-no-join-on-the-character-page Split screen: pad 3
absent x1-no-third-viewport-on-the-page NotifyAddViewport .*GamepadID:2
vprobe c3
probe cp3
pyexec print('  menu after pads 2 and 3 pressed Start:', P['cp3'].menu)
check x1-still-two-split-views-and-the-character-page VS['c3'][1] == 2 and P['cp3'].menu in ('UC2Arena_Character', 'GUIMessageBox')
shot x1-character-third-and-fourth-pad-pressed-start
pad 1 button a 1
sleep 0.3
pad 1 button a 0
sleep 2
key Return
sleep 3
SCEN
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
expect x1-travel-to-deathmatch Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
seek 20 P['_'].pawn == 1 ;; pyexec fire_all(2)
check x1-player2-spawns spawn_port(1, 1)
sleep 2
vprobe m2
check x1-match-has-two-split-players VS['m2'][1] == 2 and pads_in_join_order('m2')
SCEN
  join 2; join 3
  cat <<SCEN
vprobe m4
check x1-four-split-players-in-the-match layout_ok('m4') and VS['m4'][1] == 4 and pads_in_join_order('m4') and all_split_huds('m4')
pyexec print('  match views', [(r['i'], r['pad'], r['rect'], r['pawnclass']) for r in vsplit('m4')])
shot x1-match-four-players
mark
pad 2 button start 1
sleep 0.3
pad 2 button start 0
expect x1-player3-start-opens-the-pause-menu OpenMenu \\[UCMenu.InGame_MainMenu\\] 15
sleep 2
probe pm
pyexec print('  pause menu of player three:', P['pm'].menu, P['pm'].caption)
shot x1-pause-menu-player-three
pad 2 button dpdown 1
sleep 0.3
pad 2 button dpdown 0
sleep 1
probe pm2
pyexec print('  after player three pressed down:', P['pm2'].menu, P['pm2'].caption)
shot x1-pause-menu-player-three-down
pad 2 button b 1
sleep 0.3
pad 2 button b 0
sleep 2
probe pm3
pyexec print('  after player three pressed B:', P['pm3'].menu, P['pm3'].caption)
check x1-player-three-closes-its-pause-menu P['pm3'].menu == 'None'
mark
key Escape
expect x1-pause-menu-opens-for-player-one OpenMenu \\[UCMenu.InGame_MainMenu\\] 15
sleep 2
seek 8 P['_'].caption == 'FORFEIT GAME' ;; key Down
key Return
wait OpenMenu \\[GUI.GUIMessageBox\\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
tryseek 4 P['_'].caption.upper() in ('YES', 'OK', 'ACCEPT', 'FORFEIT') ;; key Up
key Return
expect x1-forfeit-back-to-main-menu OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
vprobe back
check x1-back-to-one-viewport layout_ok('back') and VS['back'][0] == 1 and VS['back'][1] == 0
SCEN
  return 0
}

# The x1proto part (a run tree with a GUI.u whose GUIController.AllowControllerJoin allows four child viewports):
# pads 2 and 3 join on the vanilla character page; what the page and the match do.
scen_x1proto() {
  cat <<SCEN
pyfile $checks
wait OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
focus
pad 0 attach
sleep 1
pad 1 attach
sleep 1
pad 2 attach
sleep 1
pad 3 attach
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
expect x1p-character-page OpenMenu \\[UCMenu.UC2Arena_Character\\] 15
sleep 3
SCEN
  local k
  for k in 1 2 3; do
    cat <<SCEN
pad $k button start 1
sleep 0.3
pad $k button start 0
expect x1p-pad$k-start-joins-in-the-menus Split screen: pad $k Start in the menus: joined 15
sleep 3
vprobe j$k
probe pj$k
pyexec print('  after pad $k joined: menu', P['pj$k'].menu, 'views', VS['j$k'])
shot x1p-joined-$k
pad $k button b 1
sleep 0.3
pad $k button b 0
sleep 2
SCEN
  done
  cat <<SCEN
vprobe c4
check x1p-four-split-views-on-the-character-page VS['c4'][1] == 4 and sorted(r['pad'] for r in vsplit('c4')) == [0, 1, 2, 3]
pyexec print('  menu views', [(r['i'], r['pad'], r['rect'], r['disabled']) for r in V['c4']])
shot x1p-character-four-players
mark
pad 2 axis leftx 32767
sleep 1.2
pad 2 axis leftx 0
sleep 2
shot x1p-player3-stick-right
pad 3 axis leftx -32767
sleep 1.2
pad 3 axis leftx 0
sleep 2
shot x1p-player4-stick-left
SCEN
  local i
  for i in 1 2 3 4 5 6 7; do
    cat <<SCEN
pad 1 button a 1
pad 2 button a 1
pad 3 button a 1
sleep 0.3
pad 1 button a 0
pad 2 button a 0
pad 3 button a 0
sleep 1.5
key Return
sleep 2.5
probe l$i
pyexec print('  loop $i menu', P['l$i'].menu)
SCEN
  done
  cat <<SCEN
shot x1p-after-the-loop
expect x1p-travel-to-deathmatch Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
seek 20 P['_'].pawn == 1 ;; pyexec fire_all(4)
sleep 3
pyexec fire_all(4)
sleep 3
vprobe m4
pyexec print('  match views', [(r['i'], r['pad'], r['rect'], r['pawnclass'], r['player']) for r in vsplit('m4')])
check x1p-four-split-players-in-the-match VS['m4'][1] == 4
shot x1p-match-four-players
SCEN
  return 0
}

# Four players from the character page. x1_vanilla=1 (a tree with Epic's shipped GUI.u/UCMenu.u): the page keeps two
# players, players three and four join in the match.
# x1press <pad> <button>: a pad's button pressed and released.
x1press() {
  printf 'pad %s button %s 1\nsleep 0.3\npad %s button %s 0\nsleep 1.5\n' "$1" "$2" "$1" "$2"
}
# x1tap: a shorter press (the GUI repeats a button held 1.5 x ButtonRepeatDelay = 0.285 s; a loaded machine stretched
# x1press's 0.3 s into a second step on the armory, measured)
x1tap() {
  printf 'pad %s button %s 1\nsleep 0.12\npad %s button %s 0\nsleep 1.5\n' "$1" "$2" "$1" "$2"
}
scen_x1menu4() {
  local v=${x1_vanilla:-0}
  cat <<SCEN
pyfile $checks
pyfile $repo/tools/dev/g6/x1-checks.py
pyexec X1_LOG = '$out/x1-menu4.out'
wait OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
mark
focus
pad 0 attach
sleep 1
pad 1 attach
sleep 1
pad 2 attach
sleep 1
pad 3 attach
sleep 1
SCEN
  if [ "${x1_host:-0}" = 1 ]; then
    # The flow NETWORKED COMBAT -> HOST SYSTEM LINK GAME -> the character page (a listen server)
    cat <<SCEN
seek 8 P['_'].caption == 'NETWORKED COMBAT' ;; key Down
key Return
wait OpenMenu \\[UCMenu.UC2NetworkMain\\] 15
sleep 2
seek 8 P['_'].caption == 'HOST SYSTEM LINK GAME' ;; key Down
key Return
wait OpenMenu \\[UCMenu.UC2Arena_ServerMain\\] 15
wait OpenMenu \\[GUI.GUIMessageBox\\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
key Escape
sleep 2
key Return
wait handled XC_A 15
sleep 2
key Return
wait handled XC_A 15
sleep 2
key Return
tryseek 10 P['_'].menu == 'UC2Arena_Character' ;; sleep 1
tryseek 0 P['_'].menu == 'UC2Arena_Character' ;; key Return
tryseek 10 P['_'].menu == 'UC2Arena_Character' ;; sleep 1
probe hc
check x1h-character-page-of-the-host P['hc'].menu == 'UC2Arena_Character'
sleep 3
vprobe c1
SCEN
  else
    cat <<SCEN
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
expect x1m-character-page OpenMenu \\[UCMenu.UC2Arena_Character\\] 15
sleep 4
vprobe c1
SCEN
  fi
  [ "$v" = 0 ] && cat <<SCEN
check x1m-text-one-player x1_text() == 'PLAYER 3 / 4 | PRESS START'
shot page-1
SCEN
  cat <<SCEN
pad 1 button start 1
sleep 0.3
pad 1 button start 0
expect x1m-pad1-start-joins-player-two Split screen: pad 1 Start in the menus: joined 15
sleep 3
tryseek 3 'MessageBox' not in P['_'].menu ;; pyexec (chan('pad 1 button b 1'), time.sleep(0.3), chan('pad 1 button b 0'), time.sleep(2))
sleep 1
vprobe c2
check x1m-two-split-views VS['c2'][1] == 2 and vrects('c2') == layout(2, *VS['c2'][2]) and [r['disabled'] for r in vsplit('c2')] == [1, 1]
SCEN
  x1press 1 dpright
  [ "$v" = 0 ] && echo "shot page-2"
  if [ "$v" = 1 ]; then
    # Epic's GUI: a third and a fourth pad's Start on the page make no player (as x1probe shows)
    cat <<SCEN
pad 2 button start 1
sleep 0.3
pad 2 button start 0
expect x1v-pad2-start-reaches-the-engine pad 2 button 12 value 255 15
sleep 2
pad 3 button start 1
sleep 0.3
pad 3 button start 0
sleep 3
absent x1v-pad2-no-join-on-the-page Split screen: pad 2 Start
absent x1v-pad3-no-join-on-the-page Split screen: pad 3 Start
vprobe c3
check x1v-still-two-split-views VS['c3'][1] == 2
shot vanilla-page-two-and-pads-2-3-pressed
SCEN
  else
    cat <<SCEN
mark
pad 2 button start 1
sleep 0.3
pad 2 button start 0
expect x1m-player-three-on-the-page players34 cursors \\(player 3 port 2 joined\\) 15
expect x1m-pad2-start-joins-player-three Split screen: pad 2 Start in the menus: joined 15
sleep 3
absent x1m-no-profile-box-for-player-three OpenMenu \\[GUI.GUIMessageBox\\]
vprobe c3
check x1m-three-split-views-g6-layout VS['c3'][1] == 3 and vrects('c3') == layout(3, *VS['c3'][2]) and sorted(r['pad'] for r in vsplit('c3')) == [0, 1, 2]
check x1m-text-player-three-joined x1_text() == '3P | 4P PRESS START'
pyexec x1_snap('j3')
pad 2 axis lefty 4000
pad 2 axis leftx 32767
sleep 1.4
pad 2 axis leftx 0
pad 2 axis lefty 0
sleep 2
pyexec x1_snap('s3')
check x1m-pad2-stick-moves-only-the-3p-cursor x1_only('j3', 's3', 2)
SCEN
    # 3P to Arclite (index 4: the left column's top), 4P to Szalor (3), 1P to Sapphire (14), 2P to Lauren (13)
    local k
    for k in dpleft dpleft dpup dpup dpup dpup; do x1press 2 $k; done
    cat <<SCEN
pyexec x1_snap('m3')
check x1m-pad2-dpad-moves-only-the-3p-cursor x1_only('s3', 'm3', 2) and X1['m3']['idx'][2] == 4
shot page-3
pad 3 button start 1
sleep 0.3
pad 3 button start 0
expect x1m-player-four-on-the-page players34 cursors \\(player 4 port 3 joined\\) 15
expect x1m-pad3-start-joins-player-four Split screen: pad 3 Start in the menus: joined 15
sleep 3
vprobe c4
check x1m-four-split-views-g6-layout VS['c4'][1] == 4 and vrects('c4') == layout(4, *VS['c4'][2]) and sorted(r['pad'] for r in vsplit('c4')) == [0, 1, 2, 3] and all(r['disabled'] == 1 for r in vsplit('c4'))
pyexec print('  menu views', [(r['i'], r['pad'], r['rect'], r['disabled'], r['player']) for r in V['c4']])
pyexec x1_snap('j4')
pad 3 axis lefty 4000
pad 3 axis leftx 32767
sleep 1.4
pad 3 axis leftx 0
pad 3 axis lefty 0
sleep 2
pyexec x1_snap('s4')
check x1m-pad3-stick-moves-only-the-4p-cursor x1_only('j4', 's4', 3)
SCEN
    for k in dpleft dpleft dpup dpup dpup dpup dpdown; do x1press 3 $k; done
    cat <<SCEN
pyexec x1_snap('m4')
check x1m-pad3-dpad-moves-only-the-4p-cursor x1_only('s4', 'm4', 3) and X1['m4']['idx'][3] == 3
SCEN
    x1press 1 dpdown
    cat <<SCEN
pyexec x1_snap('m2')
check x1m-pad1-moves-only-player-two x1_only('m4', 'm2', 1) and X1['m2']['idx'][1] == 13
key Right
sleep 2
pyexec x1_snap('m1')
check x1m-keyboard-moves-only-player-one x1_only('m2', 'm1', 0) and X1['m1']['idx'][0] == 14
check x1m-text-players-three-and-four x1_text() == '3P | 4P'
shot page-4
probe pg4
check x1m-still-the-character-page P['pg4'].menu == 'UC2Arena_Character'
mark
SCEN
    # leaving: player four's Back; back with Start; then pulled out; back again (standalone only: the host run
    # checks the joins, the choices and the listen server's match)
    if [ "${x1_host:-0}" = 0 ]; then
    x1press 3 back
    cat <<SCEN
expect x1m-back-removes-player-four Split screen: removing the player of pad 3: 15
sleep 3
vprobe l1
check x1m-three-views-after-the-back VS['l1'][1] == 3 and vrects('l1') == layout(3, *VS['l1'][2]) and sorted(r['pad'] for r in vsplit('l1')) == [0, 1, 2]
check x1m-text-player-four-gone x1_text().endswith('| 4P PRESS START')
absent x1m-no-page-after-the-back OpenMenu
pad 3 button start 1
sleep 0.3
pad 3 button start 0
expect x1m-player-four-joins-again Split screen: pad 3 Start in the menus: joined 15
sleep 3
mark
pad 3 detach
expect x1m-unplug-removes-player-four Split screen: removing the player of pad 3: 20
sleep 3
absent x1m-no-controller-removed-page OpenMenu \\[UCMenu.ControllerRemovedMenu\\]
vprobe l2
check x1m-three-views-after-the-unplug VS['l2'][1] == 3 and sorted(r['pad'] for r in vsplit('l2')) == [0, 1, 2]
pad 3 attach
sleep 2
pad 3 button start 1
sleep 0.3
pad 3 button start 0
expect x1m-player-four-plugged-back-joins Split screen: pad 3 Start in the menus: joined 15
sleep 3
vprobe l3
check x1m-four-views-again VS['l3'][1] == 4 and [r['pad'] for r in vsplit('l3')] == [0, 1, 2, 3]
SCEN
    for k in dpleft dpleft dpup dpup dpup dpup dpdown; do x1press 3 $k; done
    cat <<SCEN
pyexec x1_snap('m4c')
check x1m-the-cursors-before-choosing X1['m4c']['idx'] == [14, 13, 4, 3]
key Escape
wait CloseMenu \\[UC2Arena_Character\\] 15
sleep 3
key Return
expect x1m-page-again-with-four-players players34 cursors \\(page opened\\): .* players 4 20
sleep 3
pyexec x1_snap('re')
check x1m-cursors-three-four-on-the-reopened-page all(0 <= i < 15 for i in X1['re']['idx'][2:])
vprobe l4
check x1m-four-views-on-the-reopened-page VS['l4'][1] == 4
shot page-4-reopened
SCEN
    for k in dpleft dpleft dpup dpup dpup dpup; do x1press 2 $k; done
    for k in dpleft dpleft dpup dpup dpup dpup dpdown; do x1press 3 $k; done
    x1press 1 dpright
    x1press 1 dpdown
    cat <<SCEN
key Right
sleep 2
pyexec x1_snap('m4d')
check x1m-the-cursors-again X1['m4d']['idx'] == [14, 13, 4, 3]
SCEN
    fi
    # players three and four choose; three takes it back and chooses again
    x1press 2 a
    cat <<SCEN
check x1m-player-three-ready x1_cursors()['ready'][2] and x1_text() == '3P READY | 4P'
SCEN
    x1press 2 b
    cat <<SCEN
vprobe rb
check x1m-player-three-b-takes-it-back not x1_cursors()['ready'][2] and VS['rb'][1] == 4
SCEN
    x1press 2 a
    x1press 3 a
    cat <<SCEN
check x1m-players-three-and-four-ready x1_cursors()['ready'][2:] == [True, True] and x1_text() == '3P READY | 4P READY'
pyexec print('  chosen by players three and four', x1_chosen(), 'text', x1_text())
shot page-4-ready
probe pg5
check x1m-page-waits-for-players-one-and-two P['pg5'].menu == 'UC2Arena_Character'
SCEN
  fi
  # players one and two choose (pad 1: player two; the keyboard: player one), through the handicap panels
  x1press 1 a
  cat <<SCEN
key Return
sleep 2
SCEN
  x1press 1 a
  cat <<SCEN
key Return
sleep 3
SCEN
  # part B: four players choose their weapons on the 2x2 armory (the x1armory part tests it in detail); players three and
  # four take the first explosive and energy weapon (A twice), players one and two in the loop below
  [ "$v" = 0 ] && cat <<SCEN
pyexec X1_NETLOG = '$sys/Avaris.log'
expect x1m-armory-four-quarters OpenMenu \\[UCMenu.UC2Arena_QuadArmory\\] 30
sleep 4
mark
SCEN
  [ "$v" = 0 ] && { x1tap 2 a; x1tap 2 a; x1tap 3 a; x1tap 3 a; } && cat <<SCEN
sleep 1
probe ar
check x1m-armory-players-three-four-ready P['ar'].menu == 'UC2Arena_QuadArmory' and x1a_snap('ar') and [q['ready'] for q in X1A['ar']['p']] == [False, False, True, True]
absent x1m-armory-not-closed-by-players-three-four OpenMenu \\[UCMenu.UC2Arena_Character\\]
SCEN
  local i
  for i in 1 2 3 4 5 6 7; do
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
expect x1m-travel-to-the-match Bringing Level \\S+.MyLevel up for play 150
sleep 6
SCEN
  if [ "$v" = 1 ]; then
    cat <<SCEN
seek 20 P['_'].pawn == 1 ;; pyexec fire_all(2)
check x1v-player2-spawns spawn(1)
sleep 2
SCEN
    join 2; join 3
    cat <<SCEN
vprobe m4
check x1v-four-split-players-in-the-match layout_ok('m4') and VS['m4'][1] == 4 and pads_in_join_order('m4') and all_split_huds('m4')
SCEN
  else
    cat <<SCEN
seek 20 P['_'].pawn == 1 ;; pyexec (click(1), fire_all(4))
pyexec fire_all(4)
sleep 2
pyexec fire_all(4)
check x1m-player-three-spawns spawn(2)
check x1m-player-four-spawns spawn(3)
sleep 2
vprobe m4
pyexec print('  match views', [(r['i'], r['pad'], r['rect'], r['pawnclass'], r['player']) for r in vsplit('m4')])
check x1m-four-split-players-g6-layout-huds layout_ok('m4') and VS['m4'][1] == 4 and pads_in_join_order('m4') and all_split_huds('m4')
absent x1m-no-too-many-splitscreens Too many splitscreens
check x1m-players-three-four-own-classes all(x1_chosen()[k].split('.')[-1].lower() in vsplit('m4')[k - 1]['pawnclass'].lower() for k in (3, 4))
check x1m-four-different-classes len(set(r['pawnclass'] for r in vsplit('m4'))) == 4
check x1m-players-one-two-classes 'Sapphire' in vsplit('m4')[0]['pawnclass'] and 'Lauren' in vsplit('m4')[1]['pawnclass']
check x1m-players-three-four-named-player-3-4 [vsplit('m4')[k]['player'].replace('_', ' ') for k in (2, 3)] == ['Player 3', 'Player 4']
check x1m-players-one-two-not-named-player-3-4 all(not vsplit('m4')[k]['player'].replace('_', ' ').startswith('Player 3') and not vsplit('m4')[k]['player'].replace('_', ' ').startswith('Player 4') for k in (0, 1))
SCEN
  fi
  local p=x1m; [ "$v" = 1 ] && p=x1v
  cat <<SCEN
shot match-four
pad 2 button start 1
sleep 0.3
pad 2 button start 0
expect $p-player3-start-opens-the-pause-menu OpenMenu \\[UCMenu.InGame_MainMenu\\] 15
sleep 3
SCEN
  x1press 2 dpdown
  x1press 2 b
  cat <<SCEN
sleep 3
probe pm
check $p-player-three-closes-its-pause-menu P['pm'].menu == 'None'
key Escape
expect $p-pause-menu-opens-for-player-one OpenMenu \\[UCMenu.InGame_MainMenu\\] 15
sleep 2
seek 8 P['_'].caption == 'FORFEIT GAME' ;; key Down
key Return
wait OpenMenu \\[GUI.GUIMessageBox\\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
tryseek 4 P['_'].caption.upper() in ('YES', 'OK', 'ACCEPT', 'FORFEIT') ;; key Up
key Return
expect $p-forfeit-back-to-main-menu OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
vprobe back
check $p-back-to-one-viewport layout_ok('back') and VS['back'][0] == 1 and VS['back'][1] == 0
pyexec print('  out-of-bounds lines in the run, by array:', x1_oob())
pyexec open('$out/x1-menu4-oob.txt', 'w').write(repr(x1_oob()) + '\n')
SCEN
  [ "$v" = 0 ] && cat <<SCEN
check x1m-no-out-of-bounds-line x1_oob() == {}
SCEN
  return 0
}

# The 2x2 armory (UC2Arena_QuadArmory) with x1a_n=4 or 3 players from the character page: each pad drives its
# own quarter, B takes a choice back, the match waits for everybody, and the loadouts reach the spawn URLs and the match.
scen_x1armory() {
  local n=${x1a_n:-4} k
  cat <<SCEN
pyfile $checks
pyfile $repo/tools/dev/g6/x1-checks.py
pyexec X1_LOG = '$out/x1-armory$n.out'
pyexec X1_NETLOG = '$sys/Avaris.log'
wait OpenMenu \\[UCMenu.UC2MainMenu\\] 90
sleep 3
mark
focus
pad 0 attach
sleep 1
pad 1 attach
sleep 1
pad 2 attach
sleep 1
pad 3 attach
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
expect x1a-character-page OpenMenu \\[UCMenu.UC2Arena_Character\\] 15
sleep 4
pad 1 button start 1
sleep 0.3
pad 1 button start 0
expect x1a-pad1-joins-player-two Split screen: pad 1 Start in the menus: joined 15
sleep 3
tryseek 3 'MessageBox' not in P['_'].menu ;; pyexec (chan('pad 1 button b 1'), time.sleep(0.3), chan('pad 1 button b 0'), time.sleep(2))
sleep 1
pad 2 button start 1
sleep 0.3
pad 2 button start 0
expect x1a-pad2-joins-player-three Split screen: pad 2 Start in the menus: joined 15
sleep 3
SCEN
  if [ "$n" = 4 ]; then
    cat <<SCEN
pad 3 button start 1
sleep 0.3
pad 3 button start 0
expect x1a-pad3-joins-player-four Split screen: pad 3 Start in the menus: joined 15
sleep 3
SCEN
  fi
  # players three (and four) choose the character under their cursor; one and two theirs (A, then the handicap panel)
  x1press 2 a
  [ "$n" = 4 ] && x1press 3 a
  x1press 1 a
  cat <<SCEN
key Return
sleep 2
SCEN
  x1press 1 a
  cat <<SCEN
key Return
sleep 3
expect x1a-the-2x2-armory OpenMenu \\[UCMenu.UC2Arena_QuadArmory\\] 30
sleep 4
vprobe av
check x1a-$n-split-views-under-the-armory VS['av'][1] == $n
check x1a-$n-quarters x1a_snap('o') and X1A['o']['n'] == $n and [q is not None for q in X1A['o']['p']] == [True] * $n + [False] * (4 - $n)
check x1a-player-three-on-its-explosive-list X1A['o']['p'][2]['on'] == 'x' and not X1A['o']['p'][2]['ready']
shot armory-$n
SCEN
  # player three's B with nothing chosen: nothing (the page stays)
  echo mark
  x1tap 2 b
  cat <<SCEN
probe a0
check x1a-player-three-b-with-nothing-chosen-keeps-the-page P['a0'].menu == 'UC2Arena_QuadArmory'
absent x1a-no-character-page-from-player-three OpenMenu \\[UCMenu.UC2Arena_Character\\]
pyexec x1a_snap('t0')
SCEN
  # player three: explosive row 2, energy row 1; each step moves its own quarter only
  x1tap 2 dpdown
  cat <<SCEN
pyexec x1a_snap('t1')
check x1a-pad2-down-moves-only-quarter-three x1a_only('t0', 't1', 2) and X1A['t1']['p'][2]['x'] == X1A['t0']['p'][2]['x'] + 1
check x1a-the-line-names-pad-2 X1A['t1']['why'].startswith('player 3 port 2 XC_PadDown')
SCEN
  x1tap 2 dpdown
  x1tap 2 a
  cat <<SCEN
pyexec x1a_snap('t2')
check x1a-player-three-chose-its-explosive X1A['t2']['p'][2]['sx'] >= 0 and X1A['t2']['p'][2]['on'] == 'e' and x1a_only('t1', 't2', 2)
SCEN
  x1tap 2 dpdown
  cat <<SCEN
pyexec x1a_snap('t3')
check x1a-pad2-down-on-the-energy-list x1a_only('t2', 't3', 2) and X1A['t3']['p'][2]['e'] == X1A['t2']['p'][2]['e'] + 1
SCEN
  x1tap 2 a
  cat <<SCEN
pyexec x1a_snap('t4')
check x1a-player-three-ready X1A['t4']['p'][2]['ready'] and x1a_only('t3', 't4', 2)
SCEN
  x1tap 2 b
  cat <<SCEN
pyexec x1a_snap('t5')
check x1a-player-three-b-takes-the-energy-back not X1A['t5']['p'][2]['ready'] and X1A['t5']['p'][2]['sx'] == X1A['t4']['p'][2]['sx'] and X1A['t5']['p'][2]['se'] == -1
SCEN
  x1tap 2 a
  cat <<SCEN
pyexec x1a_snap('t6')
check x1a-player-three-ready-again X1A['t6']['p'][2]['ready'] and X1A['t6']['p'][2]['exp'] == X1A['t4']['p'][2]['exp'] and X1A['t6']['p'][2]['en'] == X1A['t4']['p'][2]['en']
SCEN
  local last=t6
  if [ "$n" = 4 ]; then
    # player four: explosive row 3, energy row 2
    x1tap 3 dpdown
    cat <<SCEN
pyexec x1a_snap('f1')
check x1a-pad3-down-moves-only-quarter-four x1a_only('t6', 'f1', 3)
SCEN
    for k in dpdown dpdown a dpdown dpdown; do x1tap 3 $k; done
    cat <<SCEN
pyexec x1a_snap('f2')
check x1a-pad3-on-its-energy-list x1a_only('f1', 'f2', 3) and X1A['f2']['p'][3]['on'] == 'e' and X1A['f2']['p'][3]['sx'] >= 0
SCEN
    x1tap 3 a
    cat <<SCEN
pyexec x1a_snap('f3')
check x1a-player-four-ready X1A['f3']['p'][3]['ready'] and x1a_only('f2', 'f3', 3)
SCEN
    last=f3
  fi
  cat <<SCEN
sleep 2
absent x1a-no-launch-while-players-one-two-choose OpenMenu \\[UCMenu.ConfirmLaunchMenu\\]
probe a1
check x1a-the-page-waits-for-players-one-and-two P['a1'].menu == 'UC2Arena_QuadArmory'
SCEN
  # player two (pad 1, Epic's focus path): its own quarter only; player one (the keyboard): its own
  x1tap 1 dpdown
  cat <<SCEN
pyexec x1a_snap('p2')
check x1a-pad1-down-moves-only-quarter-two x1a_only('$last', 'p2', 1)
key Down
sleep 1.5
pyexec x1a_snap('p1')
check x1a-keyboard-down-moves-only-quarter-one x1a_only('p2', 'p1', 0)
shot armory-$n-chosen
SCEN
  # players one and two choose (A / Return: explosive, energy), then the launch box (Return)
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
expect x1a-travel-to-the-match Bringing Level \\S+.MyLevel up for play 150
sleep 6
pyexec x1a_snap('fin')
pyexec print('  armory, last line:', X1A['fin'])
seek 20 P['_'].pawn == 1 ;; pyexec (click(1), fire_all($n))
pyexec fire_all($n)
sleep 2
pyexec fire_all($n)
check x1a-player-three-spawns spawn(2)
SCEN
  [ "$n" = 4 ] && echo "check x1a-player-four-spawns spawn(3)"
  cat <<SCEN
sleep 2
vprobe m
pyexec print('  match views', [(r['i'], r['pad'], r['rect'], r['pawnclass'], r['player']) for r in vsplit('m')])
check x1a-$n-split-players-in-the-match layout_ok('m') and VS['m'][1] == $n and pads_in_join_order('m') and all_split_huds('m')
chan exec NETPROBE M
sleep 2
pyexec print('  spawn URLs', x1_match_urls()); print('  NETPROBE pris', x1_pris('M'))
check x1a-spawn-url-of-player-three-carries-its-loadout len(x1_match_urls()) >= $((n - 1)) and x1_url_has(x1_match_urls()[1], X1A['t6']['p'][2]['exp'], X1A['t6']['p'][2]['en'])
check x1a-player-three-plays-with-its-loadout x1_pris('M').get('Player_3', {}).get('explosive') == X1A['t6']['p'][2]['exp'] and x1_pris('M').get('Player_3', {}).get('energy') == X1A['t6']['p'][2]['en']
pyexec print('  the default loadout (a joining player profile', x1_default_loadout(), ')')
check x1a-player-three-not-the-default-loadout x1_default_loadout() is not None and X1A['t6']['p'][2]['exp'] != x1_default_loadout()[0] and X1A['t6']['p'][2]['en'] != x1_default_loadout()[1]
SCEN
  [ "$n" = 4 ] && cat <<SCEN
check x1a-spawn-url-of-player-four-carries-its-loadout x1_url_has(x1_match_urls()[2], X1A['f3']['p'][3]['exp'], X1A['f3']['p'][3]['en'])
check x1a-player-four-plays-with-its-loadout x1_pris('M').get('Player_4', {}).get('explosive') == X1A['f3']['p'][3]['exp'] and x1_pris('M').get('Player_4', {}).get('energy') == X1A['f3']['p'][3]['en']
check x1a-player-four-not-the-default-loadout X1A['f3']['p'][3]['exp'] != x1_default_loadout()[0] and X1A['f3']['p'][3]['en'] != x1_default_loadout()[1]
check x1a-players-three-four-different-loadouts (X1A['t6']['p'][2]['exp'], X1A['t6']['p'][2]['en']) != (X1A['f3']['p'][3]['exp'], X1A['f3']['p'][3]['en'])
SCEN
  cat <<SCEN
check x1a-player-two-spawn-url-its-loadout x1_url_has(x1_match_urls()[0], X1A['fin']['p'][1]['exp'], X1A['fin']['p'][1]['en'])
shot armory-$n-match
pyexec print('  out-of-bounds lines in the run, by array:', x1_oob())
check x1a-no-out-of-bounds-line x1_oob() == {}
SCEN
  return 0
}

# fire_seg <tag> <pad>: a log-marked segment in which only that pad fires, three times.
fire_seg() {
  cat <<SCEN
sleep 1.5
chan log g6audio $1 begin
vprobe $1a
SCEN
  local i
  for i in 1 2 3; do printf 'pad %s axis righttrigger 32767\nsleep 0.4\npad %s axis righttrigger 0\nsleep 1.2\n' "$2" "$2"; done
  cat <<SCEN
sleep 1
vprobe $1b
pyexec print('  shots', '$1', fired('$1a','$1b') if vsplit('$1a') else (V['$1b'][0]['shots'] - V['$1a'][0]['shots']) & 255)
pyexec open('$out/g6-audio-shots.txt', 'a').write('$1 ' + str(fired('$1a','$1b')[$2] if vsplit('$1a') else (V['$1b'][0]['shots'] - V['$1a'][0]['shots']) & 255) + '\n')
chan log g6audio $1 end
SCEN
}

scen_audio() {
  match_start
  fire_seg single 0
  join 1; join 2; join 3
  fire_seg split4-p1 0
  fire_seg split4-p3 2
}

for part in $parts; do
  scen=$(mktemp)
  case $part in
    input)  scen_input  > "$scen"; run_game g6-input  0 420 "DM-Deadbolt?NumBots=0" "$scen";;
    render) scen_render > "$scen"; run_game g6-render 1 480 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True
            python3 "$repo/tools/dev/g6/g6-measure.py" "$out" > "$out/g6-measure.txt" 2>&1; tail -n 30 "$out/g6-measure.txt";;
    menu)   scen_menu   > "$scen"; run_game g6-menu   0 420 "" "$scen";;
    x1proto) # needs a run tree with a prototype GUI.u that allows four child viewports
            scen_x1proto > "$scen"; run_game x1-proto 0 720 "" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:NumAASamples=0;;
    x1menu4) # four players from the character page; Epic's shipped scripts: the vanilla variant
            x1_vanilla=1; grep -a -q UI_Char_3PSelector "$sys/UCMenu.u" && x1_vanilla=0
            x1_vanilla=$x1_vanilla scen_x1menu4 > "$scen"; cp "$scen" "$out/x1-menu4.scen"
            run_game x1-menu4 0 900 "" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:NumAASamples=0
            # hosting (the system-link flow, a listen server): players three and four join and play there too
            if [ $x1_vanilla = 0 ] && [ "${X1_HOST:-1}" = 1 ]; then
              x1_vanilla=0 x1_host=1 scen_x1menu4 | sed "s#x1-menu4.out#x1-host4.out#; s#x1-menu4-oob#x1-host4-oob#" > "$scen"
              cp "$scen" "$out/x1-host4.scen"
              run_game x1-host4 0 900 "" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:NumAASamples=0
            fi
            echo "  x1menu4: $([ $x1_vanilla = 1 ] && echo "vanilla (Epic's GUI.u/UCMenu.u)" || echo "patch set players34"); out-of-bounds lines $(cat "$out/x1-menu4-oob.txt" 2>/dev/null)";;
    x1armory) # the 2x2 armory with four, then three players (patch set players34)
            for n in ${X1A_PLAYERS:-4 3}; do
              x1a_n=$n scen_x1armory > "$scen"; cp "$scen" "$out/x1-armory$n.scen"
              run_game x1-armory$n 0 900 "" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:NumAASamples=0
            done
            ;;
    x1probe) # a third and fourth pad's Start on the character page and in the match (MSAA off as m10)
            scen_x1probe > "$scen"; run_game x1-probe 0 600 "" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:NumAASamples=0;;
    h3menu) # the system-link flow, the pad arriving on the character page; both pad modes
            # (H3_MODES="ticked unticked" by default; one of them alone for a quicker look)
            for m in ${H3_MODES:-ticked unticked}; do
              t=1; ka=$KBPLAYER; [ "$m" = unticked ] && t=0 ka=""
              h3_ticked=$t scen_h3menu > "$scen"; cp "$scen" "$out/h3-$m.scen"; run_game h3-$m 0 480 "" "$scen" $ka
            done;;
    hud)    # the split HUD's placement per viewport (GPU, frozen frame, the HUD alone)
            scen_hud > "$scen"; run_game b3-hud 1 480 "${G6_HUD_URL:-DM-Deadbolt?NumBots=0}" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True
            uimode=Fit43; case "${G6_ARGS:-}" in *UIScaleMode=Native640*) uimode=Native640;; esac
            python3 "$repo/tools/dev/g6/b3-hud-measure.py" "$out" $uimode > "$out/b3-hud-measure.txt" 2>&1; tail -n 40 "$out/b3-hud-measure.txt";;
    hudmenu) # two players joined in UC2's menus (Xvfb, the menu flow), their HUDs measured as the hud part's n2
            mkdir -p "$out/hudmenu"
            menu_hud=1 scen_menu > "$scen"; run_game b3-hudmenu 0 480 "" "$scen"
            for k in n2 n2-hud; do cp -f "$out/b3-hudmenu-$k.png" "$out/hudmenu/b3-hud-$k.png" 2>/dev/null; done
            uimode=Fit43; case "${G6_ARGS:-}" in *UIScaleMode=Native640*) uimode=Native640;; esac
            python3 "$repo/tools/dev/g6/b3-hud-measure.py" "$out/hudmenu" $uimode > "$out/hudmenu/b3-hud-measure.txt" 2>&1; cat "$out/hudmenu/b3-hud-measure.txt";;
    corona) scen_corona > "$scen"; run_game g9-corona 1 480 "DM-Praxis?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True
            python3 - "$out" <<'PY'
import glob, os, sys
from PIL import Image, ImageChops
out = sys.argv[1]
for on in sorted(glob.glob(os.path.join(out, 'g9-corona-c*.png'))):
    if on.endswith('nc.png'):
        continue
    off = on[:-4] + 'nc.png'
    if os.path.exists(off):
        d = ImageChops.subtract(Image.open(on).convert('L'), Image.open(off).convert('L')).point(lambda v: 255 if v > 25 else 0)
        print(f"{os.path.basename(on)}: {d.histogram()[255]} pixels brighter with coronas on, box {d.getbbox()}")
PY
            ;;
    detail) # coronas, shadows, decals in a quadrant (GPU, DM-Praxis); G6_ARGS chooses SplitScreenFullDetail
            scen_detail > "$scen"; run_game g9-detail 1 480 "DM-Praxis?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True;;
    bench4) # four players' frame rate on the GPU (G6_BENCH_NAME names the output, default g9-bench4)
            bn=${G6_BENCH_NAME:-g9-bench4}
            scen_bench4 > "$scen"; run_game "$bn" 1 $(( ${G6_BENCH_SECS:-20} + 400 )) "DM-Deadbolt?NumBots=0" "$scen"
            grep -a -E "[0-9.]+ s [0-9]+ frames [0-9.]+ fps; frame ms" "$out/$bn.out" | head -1 | sed 's/^FPS: //' > "$out/$bn.fps"
            echo "  $bn: $(cat "$out/$bn.fps"); load $(cut -d' ' -f1-3 /proc/loadavg)";;
    kbplayer)  # PadsStartAtPlayerTwo=True: in a match, then UC2's menu flow with one pad
            scen_kbplayer > "$scen"; run_game g9-kbplayer 0 480 "DM-Deadbolt?NumBots=0" "$scen" $KBPLAYER
            cp "$scen" "$out/g9-kbplayer.scen"
            menu_kb=1 scen_menu > "$scen"; run_game g9-kbmenu 0 420 "" "$scen" $KBPLAYER;;
    unplug) # player one's pad pulled out and back (no page), a pad-only player's (Epic's page); the default ini
            # (No MSAA: llvmpipe then draws a few frames a second, so a 1.2 s key or stick hold is read; m10-controls.sh does the same.)
            scen_unplug > "$scen"; run_game g11-unplug 0 420 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:NumAASamples=0
            cp "$scen" "$out/g11-unplug.scen";;
    fov)    # two GPU runs: the unclamped Hor+ maths (SplitClampToFullScreen=False) with AutoFOV on and Epic's maths,
    # and the default (SplitClampToFullScreen=True: one player's FOV for every viewport); then one sheet per layout
            fovrun=g9-fov; scen_fov > "$scen"
            run_game g9-fov      1 420 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True \
              -ini:System:[ufront.AutoFOV]:SplitClampToFullScreen=False
            fovrun=g9-fovclamp; scen_fov > "$scen"
            run_game g9-fovclamp 1 420 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True
            python3 "$repo/tools/dev/g6/g9-fov-sheet.py" "$out" > "$out/g9-fov-sheet.txt" 2>&1; cat "$out/g9-fov-sheet.txt";;
    audio)  rm -f "$out/g6-audio-shots.txt" "$out/g6-audio.wav"
            scen_audio  > "$scen"; run_game g6-audio  0 420 "DM-Deadbolt?NumBots=0" "$scen" "-ini:System:[UC2ALAudio.UC2ALAudioSubsystem]:WaveFile=$out/g6-audio.wav"
            python3 - "$out" <<'PY'
import re, sys, os
out = sys.argv[1]
lines = open(os.path.join(out, 'g6-audio.out'), encoding='latin-1').read().split('\n')
shots = dict(l.split() for l in open(os.path.join(out, 'g6-audio-shots.txt')) if l.strip())
seg, counts = None, {}
for l in lines:
    m = re.search(r'test: log g6audio (\S+) (begin|end)', l)
    if m:
        seg = m.group(1) if m.group(2) == 'begin' else None
        continue
    if seg and re.search(r'Fire on \S+: played', l):
        mode = '2D' if 'played 2D' in l else '3D'
        counts.setdefault(seg, {}).setdefault(mode, 0)
        counts[seg][mode] += 1
fails = 0
ratio = {}
for seg in ['single', 'split4-p1', 'split4-p3']:
    c = counts.get(seg, {}); n = int(shots.get(seg, '0')); total = sum(c.values())
    ratio[seg] = total / n if n else None
    print(f"audio {seg}: {n} shots, fire cues played {c} -> {ratio[seg]} per shot")
ok = ratio['single'] and ratio['split4-p1'] == ratio['single'] and counts.get('split4-p1', {}).get('3D', 0) == 0 and counts.get('split4-p3', {}).get('3D', 0) == 0 and ratio['split4-p3']
print(f"{'PASS' if ok else 'FAIL'} g6-audio-cues-once: player one's fire cue {ratio['split4-p1']} per shot with four players, {ratio['single']} alone; every shooter's cues 2D (its own viewer)")
PY
            ;;
    *) echo "unknown part $part" >&2;;
  esac
  cp "$scen" "$out/g6-$part.scen"; rm -f "$scen"
done
