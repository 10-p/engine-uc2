#!/usr/bin/env bash
# Mouse and pad check (native): mouse look like a PC shooter, the game pad as on the Xbox, measured. One PASS/FAIL line per
# check on stdout, then a summary line.
#
#   tools/dev/d6-mouse.sh [scenario...]        (default: look ini lazy pad lock before)
#
#   look    linear mouse look: N counts turn N x k at three speeds and three distances, the same k in both axes, a
#           circle of mouse motion gives a circle of view directions, no camera motion after the mouse stops (a
#           flick, the circle), the sensitivity, FOV and invert settings (mouse invert apart from the pad's, both
#           applied), auto-centring only under the pad, the look keys, mouse after stick
#   ini     the mouse's settings read from User.ini as the launcher passes them (-ini:, which also writes them there)
#   lazy    [UCGameplay.UCPlayerInput] bLazyMouseCamera=True: the mouse still turns linearly, the third-person
#           camera is Epic's lazy one under the mouse too (it moves on after a flick, then rests)
#   pad     the right stick's turn and look-up curves (rotation reached 0.1-1.0 s after the stick is pushed to
#           0.35/0.5/0.75/1.0) on our scripts and on the SHIPPED scripts with the same ini: must match; the stick
#           dead zone 0.3; the left stick walks
#   lock    lock-on with a mouse (two bots frozen by PlayersOnly and placed in front): acquire, hold through mouse
#           motion, switch target with Lock + D, break away by mouse motion past the clamp (degrees), not by a
#           smaller one; the stick's break-away still works; aim adhesion follows a target under the pad, not under
#           the mouse
#   before  the look scenario with the shipped scripts and the DefUser.ini that came before the mouse-look settings
#           (read from git): its drift checks are expected to FAIL there (labelled "before-")
#   epic    (on request) the mouse with Epic's PC DefUser.ini (read from git), for comparison
#   accel   (on request) no mouse acceleration: real X motion at two speeds with the X server's acceleration
#           turned up; the same turn per count (and the SDL warp-mode control run, evidence)
#   walk    (on request) steady ground speeds: W, Shift+W, the pad's left stick at 1.0/0.5/0.35 (both trees)
#   cheat   (on request) WHEREAMI/TELEPORT allowed and logged standalone and for a listen host
#   dodge   (on request) bDoubleTapDodge=True: a double tap of A dodges (more sideways than A held as long,
#           the pawn rises), Space + A still does, a double tap in the air against a wall wall-dodges; Lock + D still
#           switches target, with Lock held a double tap switches and does not dodge, locked on it dodges and keeps the
#           target; then the key off (the default): a double tap does not dodge, Space + A still does
#   vanilla (on request) the D6_SHIP_TREE (Epic's shipped scripts) with this repo's DefUser.ini: the mouse turns the view
#           through Epic's PC path (the stick's curve: capped, not linear), up looks up, the Right arrow strafes,
#           PageUp looks up
#
# Trees (<repo>/../run/<name>, made by tools/dev/mkrun-uc2.sh): D6_TREE (rebuilt packages, default d6-run),
# D6_SHIP_TREE (shipped packages, default d6-ship). Every game run is tools/dev/m10-controls.sh --file (Xvfb inside
# <repo>/../bin/capped 2G), -nullrenderer with [Engine.Engine] FrameRateCap=60 (30 for the pad curves, the Xbox's rate): the camera's lag
# shows at real frame rates, and software GL runs at a few frames a second here, where a lazy camera arrives in one
# frame; FrameRateCap, set with -ini:, caps the null renderer's thousand), mouse motion as exact counts through the engine's test
# channel (`mouse <dx> <dy>`: an SDL motion event; Source/SDL2Drv/Src/SDL2InputTest.h). Output: D6_OUT (default
# <repo>/../logs/d6/mouse): <scenario>.out (engine log), .result, .inject, .json (measurements).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${D6_TREE:-d6-run}
ship=${D6_SHIP_TREE:-d6-ship}
out=${D6_OUT:-$work/logs/d6/mouse}
m10=$repo/tools/dev/m10-controls.sh
checks=$repo/tools/dev/d6-checks.py
mkdir -p "$out"
for t in "$tree" "$ship"; do
  [ -x "$work/run/$t/System/uc2.bin" ] || { echo "no run tree $work/run/$t (tools/dev/mkrun-uc2.sh)" >&2; exit 2; }
done
# Turn per count at FOV 90 with the default MouseLookSensitivity 5: 0.022 deg x 5 x 65536/360
K=20.024889

# prep <tree> [defuser-file] [user-ini-file]: fresh inis (ours, or another DefUser.ini; a User.ini an earlier build
# left, for the migration), DevUI unsuppressed, no profiles
prep() {
  local sys=$work/run/$1/System
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  [ -n "${2:-}" ] && cp -f "$2" "$sys/DefUser.ini"
  sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp
  [ -n "${3:-}" ] && cp -f "$3" "$sys/User.ini"
  rm -rf "$sys/Profiles" "$work/run/$1/Save"/*.pro 2>/dev/null
  mkdir -p "$sys/Profiles"
}

# run <name> <tree> <bots> <seconds> [engine arguments...]: one game run of $out/<name>.scen, DM-Deadbolt with <bots> bots
run() {
  local name=$1 t=$2 bots=$3 secs=$4
  shift 4
  rm -f "$out/$name.json"
  D6_RESULTS=$out/$name.json D6_SYS=$work/run/$t/System D6_K=${D6_KVAL:-$K} \
  M10_KEEP=1 M10_TREE=$t M10_OUT=$out M10_SECS=$secs M10_URL="DM-Deadbolt?NumBots=$bots" \
    "$m10" --file "$out/$name.scen" "$name" -nullrenderer "$@" | sed 's/^/    /'
  [ -f "$out/$name.result" ] || return 0
  sed -n 's/^\(PASS\|FAIL\)\t\([^\t]*\)\t\(.*\)$/\1 \2: \3/p' "$out/$name.result" | cut -c1-240
}

# The open spot of tools/dev/jit-trace.sh (DM-Deadbolt; room for every move the scenarios make: forward 560 units,
# back, right down a ramp, left back; the camera arm never shortened by a wall). The spawn is random and a pawn next
# to a wall slides along it or stops (look-right-arrow-strafes-and-never-turns would then fail), so
# every scenario that measures the pawn's or the camera's movement starts from here: TELEPORT (the guarded cheat,
# allowed in the standalone game these runs are; native, so the shipped scripts have it too), twice as jit-trace.sh
# places, then checked. D6_SPOT overrides it ("x y z yaw pitch").
SPOT=${D6_SPOT:-1977.8 583.7 -34.1 33000 -1400}
# The lock scenario's spot: a PlayerStart where the lock's camera pull-back happens (2021 -> 1742 units), which every
# lock check needs. At the open spot above it never happens (1753 -> 1753), nor at (-2367.6, 3733.8), so the lock
# checks would depend on the spawn without a fixed spot.
LOCK_SPOT=${D6_LOCK_SPOT:-1025.2 1698.1 -528.2 22112 0}
# place <probe> <check> [spot]: put the pawn at $SPOT (or the spot given) and check that it is there
place() {
  local spot=${3:-$SPOT} x y
  read -r x y _ <<<"$spot"
  cat <<SCEN
chan exec TELEPORT $spot
sleep 1
chan exec TELEPORT $spot
sleep 1.5
probe $1
check $2 abs(P['$1'].loc[0] - ($x)) < 2 and abs(P['$1'].loc[1] - ($y)) < 2
SCEN
}

# The start of every scenario: the level, a spawn (Fire), God, the helpers.
start() {
  cat <<SCEN
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 4
focus
key Alt_L 0.3
sleep 3
tryseek 6 P['_'].pawn == 1 ;; key Alt_L 0.3
probe spawn
check $1-spawned P['spawn'].pawn==1 and P['spawn'].level=='DM-Deadbolt'
chan exec God
wait test: exec God 10
pyfile $checks
pyexec K=float(os.environ.get('D6_K','$K')); M={}
SCEN
}

# mouse lines: mlines <dx-total> <dy-total> <per-tick>  (one channel line per tick, integer counts, exact total)
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

# look [before]: $1 = "before" for the configuration before the mouse-look settings (reduced distances, no new settings)
scen_look() {
  local p=look sc=1
  [ "${1:-}" = before ] && { p=before; sc=5; }
  start $p
  # 1. linearity and speed: three distances x three speeds (counts per tick); alternate directions
  local sign=1 d s n
  for d in 100 500 1000; do
    for s in fast medium slow; do
      case $s in fast) n=500;; medium) n=50;; slow) n=5;; esac
      local dd=$(( d / sc )) nn=$(( n / sc < 1 ? 1 : n / sc ))
      echo "probe L${d}${s}a"
      mlines $(( sign * dd )) 0 $nn
      echo "probe L${d}${s}b"
      echo "pyexec M['$d$s']=save('$p-lin-$d-$s', abs(per_count('L${d}${s}a','L${d}${s}b',$dd)))"
      sign=$(( -sign ))
    done
  done
  if [ $p = look ]; then
    for d in 100 500 1000; do
      for s in fast medium slow; do
        echo "check $p-linear-$d-counts-$s abs(M['$d$s']/K-1) < 0.002"
      done
    done
  fi
  for d in 100 500 1000; do
    echo "check $p-same-turn-at-every-speed-$d max(M['${d}fast'],M['${d}medium'],M['${d}slow'])*$(( d / sc )) - min(M['${d}fast'],M['${d}medium'],M['${d}slow'])*$(( d / sc )) <= 2"
  done
  echo "check $p-same-turn-at-every-distance max(M['100medium'],M['500medium'],M['1000medium'])/min(M['100medium'],M['500medium'],M['1000medium']) < 1.01"
  # 2. both axes: 500 counts up and back
  echo "probe Ya"
  mlines 0 $(( -500 / sc )) $(( 50 / sc ))
  echo "probe Yb"
  mlines 0 $(( 500 / sc )) $(( 50 / sc ))
  echo "probe Yc"
  cat <<SCEN
check $p-mouse-up-looks-up pitchdiff('Ya','Yb') > 0
pyexec M['y']=save('$p-pitch-per-count', pitchdiff('Ya','Yb')/$(( 500 / sc )).0)
check $p-same-turn-in-both-axes abs(M['y']/M['500medium']-1) < 0.005
check $p-pitch-returns abs(pitchdiff('Ya','Yc')) <= 2
SCEN
  # 3. a circle (radius 300 counts, 72 steps a turn): one turn one way, then back the other way, then nothing:
  #    the circle, the camera at the same place for the same rotation both ways round (no lag), no drift
  python3 - $(( 300 / sc )) <<'PY'
import math, sys
R, N = int(sys.argv[1]), 72
print('probe Ca')
print(f'chan watch {2 * N + 60} circ')
pts = [(round(R * math.cos(2 * math.pi * i / N) - R), round(R * math.sin(2 * math.pi * i / N))) for i in range(N + 1)]
path = pts + pts[-2::-1]
for (x0, y0), (x1, y1) in zip(path, path[1:]):
    print(f'chan mouse {x1 - x0} {y1 - y0}')
print('probe Cb')
print('chan log CIRCLE-END')
PY
  cat <<SCEN
watch circ test: log CIRCLE-END 60
pyexec C=save('$p-circle', circle_stats('circ', *moving_rows('circ')))
check $p-circle-is-a-circle C['dev'] < 0.03 and C['n'] >= 140
pyexec CL=save('$p-camera-there-and-back', camera_lag('circ', *moving_rows('circ')))
check $p-camera-does-not-lag-the-circle CL['n'] > 40 and CL['max'] < 1.0
check $p-circle-closes abs(yawdiff('Ca','Cb')) <= 2 and abs(pitchdiff('Ca','Cb')) <= 2
sleep 2
probe Cc
pyexec save('$p-camera-after-circle', round(cam_moved('Cb','Cc'),2))
check $p-no-drift-after-circle cam_moved('Cb','Cc') < 0.5 and P['Cb'].rot == P['Cc'].rot
SCEN
  # 4. a flick (about 22 degrees in one tick), then nothing
  cat <<SCEN
probe Da
chan mouse $(( 400 / sc )) 0
probe Db
sleep 2
probe Dc
pyexec save('$p-camera-moved-by-flick', round(cam_moved('Da','Db'),2)); save('$p-camera-after-flick', round(cam_moved('Db','Dc'),2))
check $p-camera-follows-flick-at-once cam_moved('Da','Db') > 5
check $p-no-drift-after-flick cam_moved('Db','Dc') < 0.5 and P['Db'].rot == P['Dc'].rot
SCEN
  [ $p = before ] && return 0
  # 5. the settings: sensitivity (and saved to User.ini), FOV, mouse invert apart from the pad's (and saved to User.ini)
  cat <<SCEN
check look-default-sensitivity abs(M['500medium'] - $K) < 0.02 and P['Db'].sens.startswith('5')
chan exec MouseLookSens 2.5
wait test: exec MouseLookSens 2.5 10
probe Ea
chan mouse 500 0
probe Eb
check look-sensitivity-setting abs(yawdiff('Ea','Eb') - 500*K/2) <= 2
# saveconfig task: the console command also writes its key to User.ini now (SAVECONFIGKEYS; it was session-only before)
check look-sensitivity-saved-to-user-ini 'MouseLookSensitivity=2.500000' in open(os.environ['D6_SYS']+'/User.ini',encoding='latin-1').read()
chan exec MouseLookSens 5
wait test: exec MouseLookSens 5 10
chan exec FOV 45
wait test: exec FOV 45 10
sleep 0.3
probe Fa
chan mouse 500 0
probe Fb
check look-follows-the-fov abs(yawdiff('Fa','Fb') - 500*K*45/90) <= 2 and P['Fa'].desiredfov == 45.0
chan exec FOV 90
wait test: exec FOV 90 10
sleep 0.3
chan exec MouseLookInvert True
wait test: exec MouseLookInvert True 10
probe Ia
chan mouse 0 -100
probe Ib
check look-mouse-invert-flips-the-mouse pitchdiff('Ia','Ib') < 0 and P['Ib'].invert == 'True' and P['Ib'].padinvert == 'False'
check look-mouse-invert-saved-to-user-ini 'bInvertMouseLook=True' in open(os.environ['D6_SYS']+'/User.ini',encoding='latin-1').read()
pad attach
wait SDL2 pads: pad 0 connected 10
sleep 1
probe Ic
pad axis righty -20000
sleep 0.15
pad axis righty 0
sleep 0.3
probe Id
check look-mouse-invert-leaves-the-stick pitchdiff('Ic','Id') > 0 and P['Id'].mouselook == 'False'
chan exec SetMouseInvert True
wait test: exec SetMouseInvert True 10
chan exec MouseLookInvert False
wait test: exec MouseLookInvert False 10
probe Ie
chan mouse 0 -100
probe If
check look-pad-invert-leaves-the-mouse pitchdiff('Ie','If') > 0 and P['If'].padinvert == 'True' and P['If'].mouselook == 'True'
pad axis righty -20000
sleep 0.15
pad axis righty 0
sleep 0.3
probe Ig
check look-pad-invert-flips-the-stick pitchdiff('If','Ig') < 0
chan exec SetMouseInvert False
wait test: exec SetMouseInvert False 10
SCEN
  # 6. auto-centring: never under the mouse; under the pad as on the Xbox (target pitch -1400, third person)
  echo "probe Aa"
  mlines 0 -300 50
  cat <<SCEN
probe Ab
keydown w
sleep 1.5
keyup w
sleep 0.3
probe Ac
check look-no-auto-centre-under-the-mouse P['Ac'].rot[0] == P['Ab'].rot[0] and P['Ac'].mouselook == 'True' and P['Ac'].autocenter == 'True' and pitchdiff('Aa','Ab') > 2000
pad axis rightx 16000
sleep 0.05
pad axis rightx 0
sleep 0.3
probe Ad
keydown w
sleep 1.5
keyup w
sleep 0.3
probe Ae
pyexec save('look-auto-centre-under-the-pad', [P['Ad'].rot[0], P['Ae'].rot[0]])
check look-auto-centre-under-the-pad P['Ad'].mouselook == 'False' and abs(((P['Ae'].rot[0]+1400+32768)&65535)-32768) < abs(((P['Ad'].rot[0]+1400+32768)&65535)-32768) - 100
SCEN
  # 7. the keys, from the open spot: the Right arrow strafes and never turns, PageUp looks up; then the mouse
  # right after the stick (no adhesion/friction/curve on the mouse)
  place Kp $p-placed-for-the-keys
  cat <<SCEN
probe Ka
keydown Right
sleep 1.0
keyup Right
sleep 0.3
probe Kb
check look-right-arrow-strafes-and-never-turns side('Ka','Kb') > 100 and yawdiff('Ka','Kb') == 0
probe Kc
keydown Prior
sleep 0.5
keyup Prior
sleep 0.3
probe Kd
pyexec save('look-pageup-deg-per-s', round(pitchdiff('Kc','Kd')*360/65536/0.5,1))
check look-pageup-looks-up pitchdiff('Kc','Kd') > 500
pad axis rightx 32767
sleep 0.2
pad axis rightx 0
sleep 0.3
probe Ma
chan mouse -500 0
probe Mb
check look-mouse-after-stick-is-linear abs(yawdiff('Ma','Mb') + 500*K) <= 2 and P['Mb'].mouselook == 'True'
SCEN
}

# pad [shipped]: the right stick's curves (rotation reached after the stick is pushed), dead zone, left stick
scen_pad() {
  start pad
  cat <<SCEN
pad attach
wait SDL2 pads: pad 0 connected 10
sleep 1
SCEN
  local v
  for v in 0.35 0.5 0.75 1.0; do
    cat <<SCEN
chan watch 20000 pad$v
pad axis rightx $(python3 -c "print(round($v*32767))")
sleep 1.25
pad axis rightx 0
sleep 0.3
chan log PAD-$v-END
watch pad$v test: log PAD-$v-END 30
pyexec save('pad-yaw-$v', rate_curve('pad$v'))
SCEN
  done
  cat <<SCEN
chan watch 20000 padup
pad axis righty -16384
sleep 0.5
pad axis righty 0
sleep 0.3
chan log PAD-UP-END
watch padup test: log PAD-UP-END 30
pyexec save('pad-pitch-0.5', rate_curve('padup', marks=(0.1, 0.25, 0.4), axis='pitch'))
probe za
pad axis rightx 9175
sleep 0.5
pad axis rightx 0
sleep 0.3
probe zb
check pad-right-stick-dead-zone-0.3 yawdiff('za','zb') == 0
chan exec NETPLACE LocalPlayer 1977.8 583.7 -34.1 33000
sleep 1
chan exec NETPLACE LocalPlayer 1977.8 583.7 -34.1 33000
sleep 1.5
probe la
pad axis lefty 32767
sleep 1.0
pad axis lefty 0
sleep 0.5
probe lb
pad axis lefty -32767
sleep 1.0
pad axis lefty 0
sleep 0.5
probe lb2
pad axis lefty 16384
sleep 1.0
pad axis lefty 0
sleep 0.5
probe lb3
pyexec save('pad-left-stick-walk', {'full-1s-a': round(fwd('la','lb'),1), 'full-1s-b': round(fwd('lb','lb2'),1), 'half-1s': round(fwd('lb2','lb3'),1)})
SCEN
  # From a fixed open spot (jit-trace.sh's): back, forward, then half forward. The shipped scripts scale the stick
  # for walking on the Xbox only (PlayerMove); the engine does it on PC (UnIn.cpp), for both packages:
  # checked on both, and the distances compared below.
  echo "check pad-left-stick-walks (fwd('la','lb') < -150 or fwd('lb','lb2') > 150) and fwd('la','lb') < 20 and fwd('lb','lb2') > -20"
  cat <<SCEN
probe lc
pad axis lefty 9175
sleep 1.0
pad axis lefty 0
sleep 0.5
probe ld
check pad-left-stick-dead-zone-0.3 dist('lc','ld') < 5
SCEN
}

# lock: two bots, frozen, placed 400 units in front (0 and +30 degrees)
scen_lock() {
  start lock
  place Lp lock-placed "$LOCK_SPOT"
  cat <<SCEN
chan exec PlayersOnly
wait test: exec PlayersOnly 10
sleep 0.5
pyexec chan('place 0 400 0'); chan('place 1 400 30')
sleep 0.5
tryseek 4 len(P['_'].others) >= 2 and P['_'].others[0]['los'] == 1 and P['_'].others[1]['los'] == 1 ;; pyexec chan('mouse 818 0'); chan('place 0 400 0'); chan('place 1 400 30')
probe pa
check lock-two-targets-in-sight len(P['pa'].others) >= 2 and P['pa'].others[0]['los'] == 1 and P['pa'].others[1]['los'] == 1
chan aim 0
sleep 0.3
chan exec Lock 1
sleep 0.3
chan exec Lock 0
sleep 0.5
probe la
check lock-acquired-with-the-mouse-player P['la'].locked and P['la'].enemy == P['pa'].others[0]['name']
SCEN
  echo "probe lb"
  python3 -c "
for i in range(12):
    print('chan mouse %d 0' % (20 if i % 2 == 0 else -20))"
  cat <<SCEN
sleep 0.5
probe lc
check lock-held-through-mouse-motion P['lc'].locked and P['lc'].enemy == P['la'].enemy
chan mouse 150 0
probe ld0
sleep 1.0
probe ld
pyexec save('lock-offset-after-mouse-and-1s-later', [enemy_off('ld0'), enemy_off('ld')])
check lock-pulls-back-after-the-mouse-stops P['ld'].locked and enemy_off('ld') < enemy_off('ld0') - 50
chan exec Lock 1
sleep 0.2
keydown d
sleep 0.6
keyup d
chan exec Lock 0
sleep 0.5
probe le
check lock-switches-target-with-lock-and-d P['le'].locked and P['le'].enemy == P['pa'].others[1]['name']
SCEN
  echo "probe lf"
  mlines -225 0 15
  cat <<SCEN
sleep 0.3
probe lg
check lock-kept-by-a-push-inside-the-clamp P['lg'].locked
SCEN
  mlines -1000 0 25
  cat <<SCEN
sleep 0.3
probe lh
check lock-broken-by-mouse-past-the-clamp not P['lh'].locked
chan aim 1
sleep 0.3
chan exec Lock 1
sleep 0.3
chan exec Lock 0
sleep 0.5
probe li
check lock-reacquired P['li'].locked
pad attach
wait SDL2 pads: pad 0 connected 10
sleep 1
pad axis rightx -32767
sleep 1.5
pad axis rightx 0
sleep 0.3
probe lj
check lock-stick-break-away-unchanged not P['lj'].locked
SCEN
  # aim adhesion: one target in front (the other one put behind us), our pawn strafes; the view follows it under the
  # pad only (Epic's adhesion: 20% of the target's angular motion when it is near the crosshair). From the open spot
  # again (the lock checks above turned the view).
  place Ap lock-placed-for-adhesion "$LOCK_SPOT"
  cat <<SCEN
chan place 0 500 0
chan place 1 600 180
sleep 0.2
chan aim 0
sleep 0.2
chan mouse 1 0
chan mouse -1 0
sleep 2.0
probe ma
keydown a
sleep 0.8
keyup a
sleep 0.3
probe mb
pyexec save('lock-adhesion-yaw-under-mouse', yawdiff('ma','mb'))
check lock-no-aim-adhesion-under-the-mouse yawdiff('ma','mb') == 0 and P['mb'].mouselook == 'True' and dist('ma','mb') > 50
chan place 0 500 0
chan place 1 600 180
sleep 0.2
pad axis rightx 12000
sleep 0.05
pad axis rightx 0
sleep 0.2
chan aim 0
sleep 2.0
probe mc
keydown a
sleep 0.8
keyup a
sleep 0.3
probe md
pyexec save('lock-adhesion-yaw-under-pad', yawdiff('mc','md'))
check lock-aim-adhesion-under-the-pad abs(yawdiff('mc','md')) > 100 and P['md'].mouselook == 'False' and dist('mc','md') > 50
SCEN
}

# epic: the mouse as Epic's PC files had it (shipped scripts, Epic's DefUser.ini): for comparison only
scen_epic() {
  start epic
  echo "probe Sa"; mlines 20 0 1; echo "probe Sb"
  echo "probe Fa"; echo "chan mouse 20 0"; echo "probe Fb"
  echo "probe Ga"; echo "chan mouse 200 0"; echo "probe Gb"
  echo "probe Ha"; mlines 0 -20 1; echo "probe Hb"; mlines 0 20 1; echo "probe Hc"
  cat <<SCEN
pyexec save('epic-yaw-20-counts-1-per-tick', yawdiff('Sa','Sb')); save('epic-yaw-20-counts-in-1-tick', yawdiff('Fa','Fb')); save('epic-yaw-200-counts-in-1-tick', yawdiff('Ga','Gb'))
pyexec save('epic-pitch-20-counts-1-per-tick', pitchdiff('Ha','Hb'))
check epic-mouse-is-capped-and-nonlinear abs(yawdiff('Ga','Gb')) < 3 * abs(yawdiff('Fa','Fb')) and yawdiff('Sa','Sb') != yawdiff('Fa','Fb')
SCEN
  python3 - 25 <<'PY'
import math, sys
R, N = int(sys.argv[1]), 72
print('probe Ca')
print(f'chan watch {2 * N + 60} circ')
px = py = 0
for i in range(1, 2 * N + 1):
    a = 2 * math.pi * i / N
    x, y = round(R * math.cos(a) - R), round(R * math.sin(a))
    print(f'chan mouse {x - px} {y - py}'); px, py = x, y
print('probe Cb')
print('chan log CIRCLE-END')
PY
  cat <<SCEN
watch circ test: log CIRCLE-END 60
pyexec C=save('epic-circle', circle_stats('circ', *moving_rows('circ')))
check epic-circle-is-not-a-circle C['dev'] > 0.05
probe Da
chan mouse 3 0
probe Db
sleep 2
probe Dc
pyexec save('epic-camera-after-flick', round(cam_moved('Db','Dc'),2))
SCEN
}

# ini: the mouse's settings from User.ini, given the way the launcher gives settings (-ini:, which also writes
# them into User.ini): sensitivity 1.0 and the mouse inverted; the pad's invert stays off
scen_ini() {
  start ini
  cat <<SCEN
probe Na
chan mouse 500 0
probe Nb
check ini-sensitivity-from-user-ini abs(yawdiff('Na','Nb') - 500*K/5) <= 2 and P['Nb'].sens.startswith('1.0')
probe Nc
chan mouse 0 -100
probe Nd
check ini-mouse-invert-from-user-ini pitchdiff('Nc','Nd') < 0 and P['Nd'].invert == 'True' and P['Nd'].padinvert == 'False'
check ini-settings-kept-in-user-ini all(s in open(os.environ['D6_SYS']+'/User.ini',encoding='latin-1').read() for s in ('MouseLookSensitivity=1.0','bInvertMouseLook=True'))
SCEN
}

# lazy: bLazyMouseCamera=True (given as the launcher gives settings, -ini:): the mouse is still the linear mouse; the
# camera is Epic's lazy one (after a flick it goes on moving toward its place behind the view, then rests); the
# rebuilt script says which camera it uses
scen_lazy() {
  start lazy
  place Zp lazy-placed
  cat <<SCEN
check lazy-camera-announced any('UCPlayerInput: PC mouse look (rebuilt UCGameplay.u)' in l and 'lazy camera under the mouse' in l for l in __import__('__main__').log_lines)
probe Na
chan mouse 500 0
probe Nb
check lazy-mouse-is-still-linear abs(yawdiff('Na','Nb') - 500*K) <= 2 and P['Nb'].mouselook == 'True'
sleep 1
probe Da
chan mouse 400 0
probe Db
sleep 2
probe Dc
pyexec save('lazy-camera-after-flick', round(cam_moved('Db','Dc'),2))
check lazy-camera-moves-on-after-the-flick cam_moved('Db','Dc') > 5 and P['Db'].rot == P['Dc'].rot
sleep 1
probe Dd
check lazy-camera-comes-to-rest cam_moved('Dc','Dd') < 0.5
SCEN
}

# accel [warp]: no mouse acceleration. Real X pointer motion (XTEST, the path m10-controls.sh uses),
# with the X server's pointer acceleration turned up to 4x above a threshold of 1 (XChangePointerControl on the test's
# Xvfb): 200 counts slowly (100 events of 2) and fast (5 events of 40), both axes; the view must turn the same per
# count at both speeds (SDL's relative mode reads raw motion). "warp" is the control run with the environment's
# SDL_MOUSE_RELATIVE_MODE_WARP=1 (SDL then reads the warped, accelerated system cursor): it shows the test can see
# acceleration; evidence, not counted.
scen_accel() {
  local p=accel${1:+-$1}
  start $p
  cat <<SCEN
pyexec import __main__ as MI; MI.x.XChangePointerControl(MI.d, 1, 1, 4, 1, 1); MI.x.XFlush(MI.d)
pyexec import ctypes; MI.pc = [ctypes.c_int(), ctypes.c_int(), ctypes.c_int()]; MI.x.XGetPointerControl(MI.d, *[ctypes.byref(v) for v in MI.pc]); save('$p-x-pointer-control', [v.value for v in MI.pc])
sleep 0.5
probe Aa
rmove 200 0 100
sleep 0.5
probe Ab
rmove -200 0 5
sleep 0.5
probe Ac
rmove 0 -100 50
sleep 0.5
probe Ad
rmove 0 100 5
sleep 0.5
probe Ae
pyexec M['ys']=save('$p-yaw-per-count-slow', round(yawdiff('Aa','Ab')/200.0,4)); M['yf']=save('$p-yaw-per-count-fast', round(-yawdiff('Ab','Ac')/200.0,4))
pyexec M['ps']=save('$p-pitch-per-count-slow', round(pitchdiff('Ac','Ad')/100.0,4)); M['pf']=save('$p-pitch-per-count-fast', round(-pitchdiff('Ad','Ae')/100.0,4))
check $p-x-acceleration-is-on MI.pc[0].value == 4 and MI.pc[2].value == 1
check $p-same-turn-per-count-slow-and-fast abs(M['yf']/M['ys']-1) < 0.01 and abs(M['pf']/M['ps']-1) < 0.02
check $p-n-counts-in-n-counts-out abs(M['ys']/K-1) < 0.01 and abs(M['yf']/K-1) < 0.01
SCEN
}

# walk: does UC2 walk? The steady ground speed (0.6-1.3 s after the start) of W, of the pad's left stick at 1.0,
# 0.5 and 0.35 deflection, and of W with Shift held, from the same open spot (TELEPORT, jit-trace.sh's spot): numbers
# for "Shift = walk" (on both packages: UnIn.cpp scales the stick on PC for both)
scen_walk() {
  start walk
  local spot="1977.8 583.7 -34.1 33000 0"
  echo "pad attach"
  echo "wait SDL2 pads: pad 0 connected 10"
  local k
  for k in w:keydown_w pad1.0:32767 pad0.5:16384 pad0.35:11469 shiftw:shift; do
    local tag=${k%%:*} how=${k#*:}
    echo "chan exec TELEPORT $spot"
    echo "sleep 1"
    echo "chan exec TELEPORT $spot"
    echo "sleep 1"
    echo "chan watch 20000 $tag"
    case $how in
      keydown_w) printf 'keydown w\nsleep 1.5\nkeyup w\n';;
      shift) printf 'keydown Shift_L\nkeydown w\nsleep 1.5\nkeyup w\nkeyup Shift_L\n';;
      *) printf 'pad axis lefty -%s\nsleep 1.5\npad axis lefty 0\n' "$how";;
    esac
    echo "sleep 0.5"
    echo "chan log WALK-$tag-END"
    echo "watch $tag test: log WALK-$tag-END 30"
    echo "pyexec M['$tag']=save('walk-speed-$tag', steady_speed('$tag'))"
  done
  cat <<SCEN
check walk-keys-run M['w'] > 300
check walk-pad-full-runs M['pad1.0'] > 300
SCEN
}

# cheat: WHEREAMI and TELEPORT in a standalone game (allowed; "Cheat:" lines; WHEREAMI's output pasted
# into TELEPORT gives the same place and view), then the same process's map reloaded as a listen server: the host is
# allowed too (the refusals of clients and dedicated servers: tools/dev/net-test.sh cheats)
scen_cheat() {
  start cheat
  cat <<SCEN
chan exec WHEREAMI
expect cheat-whereami-logged ^Cheat: WHEREAMI -?[0-9.]+ -?[0-9.]+ -?[0-9.]+ [0-9]+ -?[0-9]+\s*$ 10
chan exec TELEPORT 1977.8 583.7 -34.1 33000 -1400
expect cheat-teleport-logged ^Cheat: TELEPORT 1977.8 583.7 -34.1 33000 -1400 10
expect cheat-teleport-ok ^Cheat: TELEPORT ok: 1977.8 583.7 -3[0-9].[0-9] 33000 -1400 10
sleep 0.5
probe ta
check cheat-teleport-moved-the-pawn abs(P['ta'].loc[0]-1977.8) < 2 and abs(P['ta'].loc[1]-583.7) < 2 and P['ta'].rot[1] == 33000
chan exec WHEREAMI
expect cheat-whereami-gives-the-place ^Cheat: WHEREAMI 1977.8 583.7 -3[0-9].[0-9] 33000 -1400 10
chan exec TELEPORT 1500 600 -34.1
expect cheat-teleport-without-view-keeps-it ^Cheat: TELEPORT ok: 1500.0 600.0 -3[0-9].[0-9] 33000 -1400 10
chan exec NETPROBE standalone
expect cheat-netprobe-allowed-standalone ^Cheat: NETPROBE standalone 10
expect cheat-netprobe-runs-standalone ^standalone summary net=Standalone 10
mark
chan exec TELEPORT
sleep 1
absent cheat-bare-teleport-is-epics-and-not-logged ^Cheat: TELEPORT
chan exec open DM-Deadbolt?Listen
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 4
focus
key Alt_L 0.3
sleep 3
tryseek 6 P['_'].pawn == 1 ;; key Alt_L 0.3
chan exec WHEREAMI
expect cheat-whereami-allowed-for-a-listen-host ^Cheat: WHEREAMI -?[0-9.]+ -?[0-9.]+ -?[0-9.]+ [0-9]+ -?[0-9]+\s*$ 10
chan exec TELEPORT 1977.8 583.7 -34.1 33000 0
expect cheat-teleport-allowed-for-a-listen-host ^Cheat: TELEPORT ok: 1977.8 583.7 -3[0-9].[0-9] 33000 0 10
chan exec NETPLACE LocalPlayer 1500 600 -34.1 33000
expect cheat-netplace-allowed-for-a-listen-host ^place LocalPlayer ok 10
SCEN
}

# vanilla: Epic's shipped scripts with this repo's DefUser.ini: the mouse is Epic's PC mouse (into the stick's
# variables, through the pad's clamp and curve): it turns the view, right turns right, up looks up; capped (200
# counts in one tick turn less than 3x what 20 do); the Right arrow strafes (never turns), PageUp looks up
scen_vanilla() {
  start vanilla
  echo "probe Sa"; mlines 20 0 1; echo "probe Sb"
  echo "probe Fa"; echo "chan mouse 20 0"; echo "probe Fb"
  echo "probe Ga"; echo "chan mouse 200 0"; echo "probe Gb"
  echo "probe Ha"; mlines 0 -20 1; echo "probe Hb"; mlines 0 20 1; echo "probe Hc"
  cat <<SCEN
pyexec save('vanilla-yaw-20-counts-1-per-tick', yawdiff('Sa','Sb')); save('vanilla-yaw-20-counts-in-1-tick', yawdiff('Fa','Fb')); save('vanilla-yaw-200-counts-in-1-tick', yawdiff('Ga','Gb'))
pyexec save('vanilla-pitch-20-counts-up-1-per-tick', pitchdiff('Ha','Hb'))
check vanilla-mouse-right-turns-right yawdiff('Sa','Sb') > 500 and yawdiff('Fa','Fb') > 0
check vanilla-mouse-up-looks-up pitchdiff('Ha','Hb') > 500
check vanilla-mouse-is-epics-capped-path abs(yawdiff('Ga','Gb')) < 3 * abs(yawdiff('Fa','Fb'))
SCEN
  place Kp vanilla-placed-for-the-keys
  cat <<SCEN
probe Ka
keydown Right
sleep 1.0
keyup Right
sleep 0.3
probe Kb
check vanilla-right-arrow-strafes-and-never-turns side('Ka','Kb') > 100 and yawdiff('Ka','Kb') == 0
probe Kc
keydown Prior
sleep 0.5
keyup Prior
sleep 0.3
probe Kd
check vanilla-pageup-looks-up pitchdiff('Kc','Kd') > 500
SCEN
}

# dodge: [UCGameplay.UCPlayerInput] bDoubleTapDodge=True (rebuilt UCGameplay.u). PlayersOnly first (the bots
# stand still: no hits push the pawn). From the open spot (TELEPORT, jit-trace.sh's), a double tap of A against A held
# for the same 0.6 s: the double tap dodges (the pawn rises, goes further to the left, faster); Space + A still dodges;
# then against a wall on the left (the place where A held from the open spot stops: WHEREAMI 1938.2 1125.8 -34.3), a
# jump, and in the air a double tap of D, away from it: a wall dodge (against the same jump with D held); then two
# bots placed in front: Lock + one press of D still switches target; with the Lock button held a double tap of A
# switches back and does not dodge; locked on (button released) a double tap of D dodges and keeps the target. `dodge off`: the same taps with the key off (the default): no dodge, Space + A
# still dodges. Measurements: hop() in d6-checks.py (rise, sideways displacement, top speed from a watch trace).
scen_dodge() {
  local p=dodge spot="1977.8 583.7 -34.1 33000 0" wall="1938.2 1125.8 -34.3 33000 0"
  [ "${1:-}" = off ] && p=dodge-off
  start $p
  cat <<SCEN
chan exec PlayersOnly
wait test: exec PlayersOnly 10
SCEN
  local part
  for part in held tap space; do
    printf 'chan exec TELEPORT %s\nsleep 1\nchan exec TELEPORT %s\nsleep 1\n' "$spot" "$spot"
    echo "chan watch 20000 $part"
    case $part in
      held) printf 'keydown a\nsleep 0.6\nkeyup a\n';;
      tap) printf 'keydown a\nsleep 0.06\nkeyup a\nsleep 0.06\nkeydown a\nsleep 0.48\nkeyup a\n';;
      space) printf 'keydown a\nsleep 0.1\nkey space 0.1\nsleep 0.4\nkeyup a\n';;
    esac
    printf 'sleep 0.8\nchan log DODGE-%s-END\nwatch %s test: log DODGE-%s-END 30\n' "$part" "$part" "$part"
    echo "pyexec M['$part']=save('$p-$part', hop('$part'))"
  done
  echo "check $p-held-a-runs-flat M['held']['rise'] < 3 and M['held']['side'] < -50"
  if [ $p = dodge ]; then
    cat <<SCEN
check dodge-double-tap-a-dodges M['tap']['rise'] > 15 and M['tap']['side'] < M['held']['side'] - 30 and M['tap']['peak'] > 1.2 * M['held']['peak']
check dodge-space-a-still-dodges M['space']['rise'] > 15 and M['space']['side'] < M['held']['side'] - 30
SCEN
    # the wall: on the left of the place (TELEPORT); a jump (Space, no direction), and in the air D held (the
    # control) or a double tap of D (away from the wall: UCPawn.CanWallDodge traces behind the dodge direction)
    for part in walljump walldodge; do
      printf 'chan exec TELEPORT %s\nsleep 1\nchan exec TELEPORT %s\nsleep 1\n' "$wall" "$wall"
      echo "probe $part-at"
      echo "chan watch 20000 $part"
      echo "key space 0.1"
      echo "sleep 0.1"
      case $part in
        walljump) printf 'keydown d\nsleep 0.4\nkeyup d\n';;
        walldodge) printf 'keydown d\nsleep 0.05\nkeyup d\nsleep 0.05\nkeydown d\nsleep 0.3\nkeyup d\n';;
      esac
      printf 'sleep 1.0\nchan log DODGE-%s-END\nwatch %s test: log DODGE-%s-END 30\n' "$part" "$part" "$part"
      echo "pyexec M['$part']=save('dodge-$part', hop('$part'))"
    done
    cat <<SCEN
check dodge-at-the-wall abs(P['walldodge-at'].loc[1]-1125.8) < 3 and abs(P['walljump-at'].loc[1]-1125.8) < 3
check dodge-double-tap-in-the-air-wall-dodges M['walldodge']['peak'] > 1.3 * M['walljump']['peak'] and M['walldodge']['rise'] > M['walljump']['rise'] + 15
chan exec TELEPORT $spot
sleep 1
pyexec chan('place 0 400 0'); chan('place 1 400 30')
sleep 0.5
tryseek 4 len(P['_'].others) >= 2 and P['_'].others[0]['los'] == 1 and P['_'].others[1]['los'] == 1 ;; pyexec chan('mouse 818 0'); chan('place 0 400 0'); chan('place 1 400 30')
probe pa
check dodge-lock-two-targets-in-sight len(P['pa'].others) >= 2 and P['pa'].others[0]['los'] == 1 and P['pa'].others[1]['los'] == 1
chan aim 0
sleep 0.3
chan exec Lock 1
sleep 0.3
chan exec Lock 0
sleep 0.5
probe la
check dodge-lock-acquired P['la'].locked and P['la'].enemy in [o['name'] for o in P['pa'].others]
chan exec Lock 1
sleep 0.2
keydown d
sleep 0.6
keyup d
chan exec Lock 0
sleep 0.5
probe le
check dodge-lock-and-one-press-of-d-switches-target P['le'].locked and P['le'].enemy != P['la'].enemy
chan exec Lock 1
sleep 0.2
chan watch 20000 lockheld
keydown a
sleep 0.06
keyup a
sleep 0.06
keydown a
sleep 0.08
keyup a
sleep 0.4
chan exec Lock 0
sleep 0.5
chan log DODGE-lockheld-END
watch lockheld test: log DODGE-lockheld-END 30
probe lf
pyexec M['lockheld']=save('dodge-lock-held-double-tap-a', hop('lockheld'))
check dodge-lock-held-double-tap-switches-and-does-not-dodge P['lf'].locked and P['lf'].enemy == P['la'].enemy and M['lockheld']['rise'] < 3
chan watch 20000 lockedon
keydown d
sleep 0.06
keyup d
sleep 0.06
keydown d
sleep 0.3
keyup d
sleep 0.8
chan log DODGE-lockedon-END
watch lockedon test: log DODGE-lockedon-END 30
probe lg
pyexec M['lockedon']=save('dodge-locked-on-double-tap-d', hop('lockedon'))
check dodge-locked-on-double-tap-dodges-and-keeps-the-target P['lg'].locked and P['lg'].enemy == P['lf'].enemy and M['lockedon']['rise'] > 15
SCEN
  else
    cat <<SCEN
check dodge-off-double-tap-a-does-not-dodge M['tap']['rise'] < 3 and M['tap']['peak'] < 1.1 * M['held']['peak'] and abs(M['tap']['side'] - M['held']['side']) < 80
check dodge-off-space-a-still-dodges M['space']['rise'] > 15 and M['space']['side'] < M['held']['side'] - 30
SCEN
  fi
}

fails=0
passes=0
count() {
  local f=$1
  [ -f "$f" ] || { fails=$((fails+1)); return; }
  passes=$((passes + $(grep -c '^PASS' "$f"))); fails=$((fails + $(grep -c '^FAIL' "$f")))
}

for s in ${@:-look ini lazy pad lock before}; do
  case $s in
    look)
      echo "== look ($tree, our DefUser.ini)"
      scen_look > "$out/look.scen"; prep "$tree"; run look "$tree" 0 600 "-ini:System:[Engine.Engine]:FrameRateCap=60"; count "$out/look.result";;
    pad)
      echo "== pad ($tree, ours) and pad-shipped ($ship, the shipped scripts; same DefUser.ini)"
      scen_pad > "$out/pad.scen"; scen_pad shipped > "$out/pad-shipped.scen"
      # at 30 frames a second (the Xbox's): Epic's view rotation is whole units per tick, so the slow end of the
      # curve depends on the frame rate; both runs at the same rate
      prep "$tree"; run pad "$tree" 0 300 "-ini:System:[Engine.Engine]:FrameRateCap=30"; count "$out/pad.result"
      prep "$ship"; run pad-shipped "$ship" 0 300 "-ini:System:[Engine.Engine]:FrameRateCap=30" | sed 's/^/(shipped) /'
      python3 - "$out/pad.json" "$out/pad-shipped.json" <<'PY' | tee "$out/pad-compare.result" | sed 's/\t/ /g'
import json, sys
def load(p):
    try:
        return {r['key']: r['value'] for r in map(json.loads, open(p))}
    except FileNotFoundError:
        return {}
ours, ship = load(sys.argv[1]), load(sys.argv[2])
keys = [k for k in ship if k.startswith('pad-') and isinstance(ship[k], dict)]
if not keys:
    print('FAIL\tpad-curves-match-shipped\tno shipped measurements'); sys.exit()
# The walk is a distance after the stick is held for 1.0 s of wall clock: 30 or 31 frames at 30 fps, so the measure's
# resolution is one frame of running, 430/30 = 14.3 units (12.2 backwards), 3.5% of the 341-unit half-stick segment
# (D-4: 353.0 vs 340.9 on two runs of the same build). A walk passes within 3% or one frame, whichever is larger.
FRAME_MOVE = 430.0 / 30
for k in sorted(keys):
    a, b = ours.get(k, {}), ship[k]
    worst = 0.0; ok = True
    for m, vb in b.items():
        va = a.get(m)
        if va is None:
            worst = 1.0; ok = False; continue
        d = abs(va - vb)
        worst = max(worst, d / max(abs(vb), 300.0))
        lim = max(0.03 * max(abs(vb), 300.0), FRAME_MOVE + 0.5) if k == 'pad-left-stick-walk' else 0.03 * max(abs(vb), 300.0)
        ok = ok and d < lim
    note = 'limit 3% or one frame of running' if k == 'pad-left-stick-walk' else 'limit 3%'
    print(f"{'PASS' if ok else 'FAIL'}\tpad-matches-shipped-{k[4:]}\tworst {worst*100:.2f}% of the shipped value ({note}); ours {a} shipped {b}")
PY
      count "$out/pad-compare.result";;
    ini)
      echo "== ini ($tree, -ini:User:[UCGameplay.UCPlayerInput]:MouseLookSensitivity=1.0 and bInvertMouseLook=True)"
      scen_ini > "$out/ini.scen"; prep "$tree"
      run ini "$tree" 0 200 "-ini:User:[UCGameplay.UCPlayerInput]:MouseLookSensitivity=1.0" "-ini:User:[UCGameplay.UCPlayerInput]:bInvertMouseLook=True"
      count "$out/ini.result";;
    lazy)
      echo "== lazy ($tree, -ini:User:[UCGameplay.UCPlayerInput]:bLazyMouseCamera=True)"
      scen_lazy > "$out/lazy.scen"; prep "$tree"
      run lazy "$tree" 0 200 "-ini:System:[Engine.Engine]:FrameRateCap=60" "-ini:User:[UCGameplay.UCPlayerInput]:bLazyMouseCamera=True"
      count "$out/lazy.result";;
    walk)
      echo "== walk ($tree and $ship: steady speeds of W, Shift+W and the left stick at 1.0/0.5/0.35)"
      scen_walk > "$out/walk.scen"
      cp "$out/walk.scen" "$out/walk-shipped.scen"
      prep "$tree"; run walk "$tree" 0 300 "-ini:System:[Engine.Engine]:FrameRateCap=60"; count "$out/walk.result"
      prep "$ship"; run walk-shipped "$ship" 0 300 "-ini:System:[Engine.Engine]:FrameRateCap=60" | sed 's/^/(shipped) /'; count "$out/walk-shipped.result";;
    cheat)
      echo "== cheat ($tree: WHEREAMI/TELEPORT standalone, then as the host of a listen server)"
      scen_cheat > "$out/cheat.scen"; prep "$tree"; run cheat "$tree" 0 300 "-ini:System:[Engine.Engine]:FrameRateCap=60"; count "$out/cheat.result";;
    accel)
      echo "== accel ($tree, real X motion with X pointer acceleration 4x; then the SDL_MOUSE_RELATIVE_MODE_WARP=1 control)"
      scen_accel > "$out/accel.scen"; prep "$tree"; run accel "$tree" 0 200 "-ini:System:[Engine.Engine]:FrameRateCap=60"; count "$out/accel.result"
      scen_accel warp > "$out/accel-warp.scen"; prep "$tree"
      SDL_MOUSE_RELATIVE_MODE_WARP=1 run accel-warp "$tree" 0 200 "-ini:System:[Engine.Engine]:FrameRateCap=60" | sed 's/^/(control) /';;
    lock)
      echo "== lock ($tree, two bots frozen)"
      scen_lock > "$out/lock.scen"; prep "$tree"; run lock "$tree" 2 400 "-ini:System:[Engine.Engine]:FrameRateCap=60"; count "$out/lock.result";;
    before)
      echo "== before ($ship, shipped scripts, the DefUser.ini before the mouse-look work; expected to FAIL drift/symmetry)"
      git -C "$repo" show c138c39:Engine/Config/DefUser.ini > "$out/DefUser-main.ini"
      scen_look before > "$out/before.scen"; prep "$ship" "$out/DefUser-main.ini"; D6_KVAL=52.4 run before "$ship" 0 600 "-ini:System:[Engine.Engine]:FrameRateCap=60" | sed 's/^/(before) /';;
    epic)
      echo "== epic ($ship, shipped scripts, Epic's DefUser.ini as the first web build had it; evidence)"
      git -C "$repo" show a573d61:Engine/Config/web/DefUser.ini > "$out/DefUser-epic.ini"
      scen_epic > "$out/epic.scen"; prep "$ship" "$out/DefUser-epic.ini"; run epic "$ship" 0 300 "-ini:System:[Engine.Engine]:FrameRateCap=60" | sed 's/^/(epic) /';;
    vanilla)
      echo "== vanilla ($ship, Epic's shipped scripts, OUR DefUser.ini: Epic's PC mouse path)"
      scen_vanilla > "$out/vanilla.scen"; prep "$ship"; run vanilla "$ship" 0 300 "-ini:System:[Engine.Engine]:FrameRateCap=60"; count "$out/vanilla.result";;
    dodge)
      echo "== dodge ($tree, -ini:User:[UCGameplay.UCPlayerInput]:bDoubleTapDodge=True; then dodge-off, the key's default)"
      scen_dodge > "$out/dodge.scen"; prep "$tree"
      run dodge "$tree" 2 400 "-ini:System:[Engine.Engine]:FrameRateCap=60" "-ini:User:[UCGameplay.UCPlayerInput]:bDoubleTapDodge=True"
      if grep -aq 'UCPlayerInput: double-tap dodge on (bDoubleTapDodge), DoubleClickTime=0.25' "$out/dodge.out"; then r=PASS; else r=FAIL; fi
      printf '%s\tdodge-key-on-logged\tUCPlayerInput: double-tap dodge on\n' $r | tee -a "$out/dodge.result" | sed 's/\t/ /; s/\t/: /'
      count "$out/dodge.result"
      scen_dodge off > "$out/dodge-off.scen"; prep "$tree"
      run dodge-off "$tree" 0 300 "-ini:System:[Engine.Engine]:FrameRateCap=60"
      if grep -aq 'UCPlayerInput: double-tap dodge on' "$out/dodge-off.out"; then r=FAIL; else r=PASS; fi
      printf '%s\tdodge-off-key-off-by-default\tno "double-tap dodge on" line\n' $r | tee -a "$out/dodge-off.result" | sed 's/\t/ /; s/\t/: /'
      count "$out/dodge-off.result";;
    *) echo "unknown scenario $s (look ini lazy pad lock before epic vanilla accel walk cheat dodge)" >&2;;
  esac
done
echo "d6-mouse: $passes PASS, $fails FAIL (scenarios: ${*:-look ini lazy pad lock before}; 'before' and 'epic' are evidence and not counted)"
