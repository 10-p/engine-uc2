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
#          audio   Xvfb, audio to a .wav through OpenAL Soft's wave writer, the cue log (DevSound): player one's
#                  weapon single-player and with four players, player three's with four: cue lines per shot
#          fov     (G-9) GPU as render: the same frozen frame with AutoFOV on and with Epic's maths, 1-4 players, and a
#                  second run with the proposal [ufront.AutoFOV] SplitClampToFullScreen=True; one sheet per layout
#                  (<out>/fov-<n>players.png) with every viewport's FOV from the engine's "[AutoFOV] shown" lines
#          detail  (G-9) GPU, DM-Praxis: coronas, shadows, decals single player and in a 4-player quadrant
#          bench4  (G-9) GPU: four players at fixed spots, the engine's FPS log (G6_BENCH_SECS=20, G6_BENCH_NAME)
#          kbplayer (G-9) Xvfb, PadsStartAtPlayerTwo=True (G-9's KeyboardIsPlayer; not the default since G-11):
#                  keyboard+mouse player one alone, a pad that joins as player two, every device moving/turning/firing
#                  its own player only, unplug (Epic's page for the pad-only player)/replug, leave, rejoin; then the
#                  menus' flow with one pad (menu_kb=1)
#          unplug  (G-11) Xvfb, the default PadsStartAtPlayerTwo=False: player one's pad (with the keyboard and mouse)
#                  pulled out opens no menu and pauses nothing, plugged back it is player one's again; a pad-only
#                  player two pulled out gets Epic's ControllerRemovedMenu, its Start closes it after the re-plug
#          hud     (B-3) GPU as render: where each viewport's HUD sits, 1-4 players joined in the match, player one
#                  at spot A, frozen frame: the picture and the HUD alone (TOGGLERENDERLEVEL); measured per viewport
#                  by tools/dev/g6/b3-hud-measure.py (health bar, weapon list, score box, reticle in design pixels).
#                  G6_RES and G6_ARGS (-ini:System:[SDL2Drv.SDL2Client]:UIScaleMode=...) choose the shape
#          hudmenu (B-3) Xvfb: G-6's menu flow (two players joined on the character page, Epic's own path), the
#                  match's HUDs alone, measured as hud's n2 (<out>/hudmenu/)
#          (default: input render menu audio; G-6's parts use the default PadsStartAtPlayerTwo=False, the pad as player one)
#   G6_RES=1280x720   window size;   G6_OUT=<dir>  output (default <WorkUC2>/logs/g6)
#   G6_ARGS="-ini:..."  more settings for every game run (G-9: -ini:System:[SDL2Drv.SDL2Client]:SplitScreenFullDetail=False)
#
# Every game run is one <WorkUC2>/bin/capped 2G run (CLAUDE.md). Settings go in with -ini: (no switches): the test
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
# G-6's parts test the default model (G-11: [SDL2Drv.SDL2Client] PadsStartAtPlayerTwo=False), where the first pad
# drives player one with the keyboard; G-9's kbplayer part sets PadsStartAtPlayerTwo=True, keyboard+mouse alone.
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
  local args=(-log -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan"
    "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=$W" "-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY=$H"
    "-ini:System:[SDL2Drv.SDL2Client]:FullscreenViewportX=$W" "-ini:System:[SDL2Drv.SDL2Client]:FullscreenViewportY=$H" "$@" ${G6_ARGS:-})
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
SCEN
  # The keyboard: W S A D in turn. During each hold the input's walk (the pawn's acceleration, whatever stands in
  # the way) must be player one's only; over the four, player one must move and nobody else. (Measured walk
  # distances alone depended on where the earlier steps left player one: facing a wall after the right stick's
  # turn, W moved 0 with the acceleration on, 4 runs in 5; G-6 step 10.)
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

# G-9: the split-screen FOV A/B pictures. The same frozen frame is shot with AutoFOV on (AUTOFOV 2, the default: Hor+
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

# B-3: the HUD of every viewport, 1-4 players. The frame is frozen long after the last placement, so the join and
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

# G-9: the frame rate with four players (GPU): the four placed at spots A C D E (fixed views, time running), then the
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

# G-9: split-screen detail where the single player shows coronas, shadows and decals: DM-Praxis (DM-Deadbolt has no
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

# G-9: keyboard+mouse as a player of their own ([SDL2Drv.SDL2Client] PadsStartAtPlayerTwo=True, G-11's name). The
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

# G-11, the owner's rule: "The 'gamepad disconnected' message should never trigger for player 1 because we always have
# the keyboard and mouse available. If it really disconnects then we just reconnect it and we're back." With the
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

# The menus' own flow (UC2's two-player split screen): main menu -> Instant Action -> DeathMatch -> DM-Deadbolt ->
# character page, where pad 1's Start joins player two (UC2Arena_Character allows it); both choose and launch.
scen_menu() {
  # G-9: menu_kb=1 is the same flow with PadsStartAtPlayerTwo=True: one pad (virtual pad 0 on port 1) joins on the
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
  # Both players accept through armory and launch (the keyboard drives player one, pad 1 player two).
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
expect menu-travel-to-deathmatch Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
seek 20 P['_'].pawn == 1 ;; $start
check menu-player2-spawns spawn_port($jport, $jp)
sleep 2
vprobe m2
check menu-match-has-two-split-players layout_ok('m2') and VS['m2'][1] == 2 and pads_in_join_order('m2') and all_split_huds('m2')
shot match-two-players
SCEN
  # B-3 (hudmenu): the HUD of two players who joined in the menus (Epic's own path: the split HUDs made at the map's
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
    hud)    # B-3: the split HUD's placement per viewport (GPU, frozen frame, the HUD alone)
            scen_hud > "$scen"; run_game b3-hud 1 480 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True
            uimode=Fit43; case "${G6_ARGS:-}" in *UIScaleMode=Native640*) uimode=Native640;; esac
            python3 "$repo/tools/dev/g6/b3-hud-measure.py" "$out" $uimode > "$out/b3-hud-measure.txt" 2>&1; tail -n 40 "$out/b3-hud-measure.txt";;
    hudmenu) # B-3: two players joined in UC2's menus (Xvfb, G-6's menu flow), their HUDs measured as the hud part's n2
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
    detail) # G-9: coronas, shadows, decals in a quadrant (GPU, DM-Praxis); G6_ARGS chooses SplitScreenFullDetail
            scen_detail > "$scen"; run_game g9-detail 1 480 "DM-Praxis?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True;;
    bench4) # G-9: four players' frame rate on the GPU (G6_BENCH_NAME names the output, default g9-bench4)
            bn=${G6_BENCH_NAME:-g9-bench4}
            scen_bench4 > "$scen"; run_game "$bn" 1 $(( ${G6_BENCH_SECS:-20} + 400 )) "DM-Deadbolt?NumBots=0" "$scen"
            grep -a -E "[0-9.]+ s [0-9]+ frames [0-9.]+ fps; frame ms" "$out/$bn.out" | head -1 | sed 's/^FPS: //' > "$out/$bn.fps"
            echo "  $bn: $(cat "$out/$bn.fps"); load $(cut -d' ' -f1-3 /proc/loadavg)";;
    kbplayer)  # G-9: PadsStartAtPlayerTwo=True: in a match, then UC2's menu flow with one pad
            scen_kbplayer > "$scen"; run_game g9-kbplayer 0 480 "DM-Deadbolt?NumBots=0" "$scen" $KBPLAYER
            cp "$scen" "$out/g9-kbplayer.scen"
            menu_kb=1 scen_menu > "$scen"; run_game g9-kbmenu 0 420 "" "$scen" $KBPLAYER;;
    unplug) # G-11: player one's pad pulled out and back (no page), a pad-only player's (Epic's page); the default ini
            # (No MSAA: llvmpipe then draws a few frames a second, so a 1.2 s key or stick hold is read; m10-controls.sh does the same.)
            scen_unplug > "$scen"; run_game g11-unplug 0 420 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:AntiAliasing=0
            cp "$scen" "$out/g11-unplug.scen";;
    fov)    # G-9: two GPU runs, the default ini (AutoFOV on / Epic's maths) and the proposal
            # [ufront.AutoFOV] SplitClampToFullScreen=True; then one sheet per layout
            fovrun=g9-fov; scen_fov > "$scen"
            run_game g9-fov      1 420 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True
            fovrun=g9-fovclamp; scen_fov > "$scen"
            run_game g9-fovclamp 1 420 "DM-Deadbolt?NumBots=0" "$scen" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotRamp=True \
              -ini:System:[ufront.AutoFOV]:SplitClampToFullScreen=True
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
