#!/usr/bin/env bash
# D-6 check (native): mouse look like a PC shooter, the game pad as on the Xbox, measured. One PASS/FAIL line per
# check on stdout, then a summary line.
#
#   tools/dev/d6-mouse.sh [scenario...]        (default: look pad lock before)
#
#   look    linear mouse look: N counts turn N x k at three speeds and three distances, the same k in both axes, a
#           circle of mouse motion gives a circle of view directions, no camera motion after the mouse stops (a
#           flick, the circle), the sensitivity, FOV and invert settings (mouse invert apart from the pad's, both
#           saved/applied), auto-centring only under the pad, the look keys, mouse after stick
#   pad     the right stick's turn and look-up curves (rotation reached 0.1-1.0 s after the stick is pushed to
#           0.35/0.5/0.75/1.0) on our scripts and on the SHIPPED scripts with the same ini: must match; the stick
#           dead zone 0.3; the left stick walks
#   lock    lock-on with a mouse (two bots frozen by PlayersOnly and placed in front): acquire, hold through mouse
#           motion, switch target with Lock + D, break away by mouse motion past the clamp (degrees), not by a
#           smaller one; the stick's break-away still works; aim adhesion follows a target under the pad, not under
#           the mouse
#   before  the look scenario as the owner played it (shipped scripts, main's DefUser.ini at 47b1b18): evidence,
#           its drift/symmetry checks are expected to FAIL there (labelled "before-")
#
# Trees (<WorkUC2>/run/<name>, made by tools/dev/mkrun-uc2.sh): D6_TREE (rebuilt packages, default d6-run),
# D6_SHIP_TREE (shipped packages, default d6-ship). Every game run is tools/dev/m10-controls.sh --file (Xvfb inside
# <WorkUC2>/bin/capped 2G), -nullrenderer (the camera's lag shows at real frame rates; software GL runs at a few
# frames a second, where a lazy camera arrives in one frame), mouse motion as exact counts through the engine's test
# channel (`mouse <dx> <dy>`: an SDL motion event; Source/SDL2Drv/Src/SDL2InputTest.h). Output: D6_OUT (default
# <WorkUC2>/logs/d6/mouse): <scenario>.out (engine log), .result, .inject, .json (measurements).
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
# Turn per count at FOV 90 with the default MouseLookSensitivity 2.5: 0.022 deg x 2.5 x 65536/360
K=10.012444

# prep <tree> [defuser-file]: fresh inis (ours, or another DefUser.ini), DevUI unsuppressed, no profiles
prep() {
  local sys=$work/run/$1/System
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  [ -n "${2:-}" ] && cp -f "$2" "$sys/DefUser.ini"
  sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp
  rm -rf "$sys/Profiles" "$work/run/$1/Save"/*.pro 2>/dev/null
  mkdir -p "$sys/Profiles"
}

# run <name> <tree> <bots> <seconds>: one game run of $out/<name>.scen, DM-Deadbolt with <bots> bots
run() {
  local name=$1 t=$2 bots=$3 secs=$4
  rm -f "$out/$name.json"
  D6_RESULTS=$out/$name.json D6_SYS=$work/run/$t/System D6_K=${D6_KVAL:-$K} \
  M10_KEEP=1 M10_TREE=$t M10_OUT=$out M10_SECS=$secs M10_URL="DM-Deadbolt?NumBots=$bots" \
    "$m10" --file "$out/$name.scen" "$name" -nullrenderer | sed 's/^/    /'
  [ -f "$out/$name.result" ] || return 0
  sed -n 's/^\(PASS\|FAIL\)\t\([^\t]*\)\t\(.*\)$/\1 \2: \3/p' "$out/$name.result" | cut -c1-240
}

# The start of every scenario: the level, a spawn (Fire), God, the helpers.
start() {
  cat <<SCEN
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 4
focus
key Alt_L 0.3
sleep 3
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

# look [before]: $1 = "before" for the owner's configuration (reduced distances, no new settings)
scen_look() {
  local p=look sc=1
  [ "${1:-}" = before ] && { p=before; sc=5; }
  start $p
  # 1. linearity and speed: three distances x three speeds (counts per tick); alternate directions
  local sign=1 d s n
  for d in 100 500 2000; do
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
    for d in 100 500 2000; do
      for s in fast medium slow; do
        echo "check $p-linear-$d-counts-$s abs(M['$d$s']/K-1) < 0.002"
      done
    done
  fi
  for d in 100 500 2000; do
    echo "check $p-same-turn-at-every-speed-$d max(M['${d}fast'],M['${d}medium'],M['${d}slow'])*$(( d / sc )) - min(M['${d}fast'],M['${d}medium'],M['${d}slow'])*$(( d / sc )) <= 2"
  done
  echo "check $p-same-turn-at-every-distance max(M['100medium'],M['500medium'],M['2000medium'])/min(M['100medium'],M['500medium'],M['2000medium']) < 1.01"
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
  # 3. a circle (radius 300 counts, 72 steps a turn, two turns), then nothing: the circle and no drift
  python3 - $(( 300 / sc )) <<'PY'
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
pyexec C=save('$p-circle', circle_stats('circ', *moving_rows('circ')))
check $p-circle-is-a-circle C['dev'] < 0.03 and C['n'] >= 140
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
  # 5. the settings: sensitivity (and saved), FOV, mouse invert apart from the pad's (and saved)
  cat <<SCEN
check look-default-sensitivity abs(M['500medium'] - $K) < 0.02 and P['Db'].sens.startswith('2.5')
chan exec MouseLookSens 1.25
wait test: exec MouseLookSens 1.25 10
probe Ea
chan mouse 500 0
probe Eb
check look-sensitivity-setting abs(yawdiff('Ea','Eb') - 500*K/2) <= 2
check look-sensitivity-saved 'MouseLookSensitivity=1.25' in open(os.environ['D6_SYS']+'/User.ini',encoding='latin-1').read()
chan exec MouseLookSens 2.5
wait test: exec MouseLookSens 2.5 10
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
check look-mouse-invert-saved 'bInvertMouseLook=True' in open(os.environ['D6_SYS']+'/User.ini',encoding='latin-1').read()
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
check look-auto-centre-under-the-pad P['Ad'].mouselook == 'False' and abs(((P['Ae'].rot[0]+1400+32768)&65535)-32768) < abs(((P['Ad'].rot[0]+1400+32768)&65535)-32768) - 1000
SCEN
  # 7. the look keys, and the mouse right after the stick (no adhesion/friction/curve on the mouse)
  cat <<SCEN
probe Ka
keydown Right
sleep 1.0
keyup Right
sleep 0.3
probe Kb
pyexec save('look-keys-deg-per-s', round(yawdiff('Ka','Kb')*360/65536/1.0,1))
check look-right-key-turns-right 60*65536/360 < yawdiff('Ka','Kb') < 125*65536/360
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

# pad: the right stick's curves (rotation reached after the stick is pushed), dead zone, left stick
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
probe la
pad axis lefty -32767
sleep 1.0
pad axis lefty 0
sleep 0.5
probe lb
check pad-left-stick-walks fwd('la','lb') > 150
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
  cat <<SCEN
chan exec PlayersOnly
wait test: exec PlayersOnly 10
sleep 0.5
pyexec chan('place 0 400 0'); chan('place 1 400 30')
sleep 0.5
tryseek 4 len(P['_'].others) >= 2 and P['_'].others[0]['los'] == 1 and P['_'].others[1]['los'] == 1 ;; pyexec chan('mouse 1636 0'); chan('place 0 400 0'); chan('place 1 400 30')
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
    print('chan mouse %d 0' % (40 if i % 2 == 0 else -40))"
  cat <<SCEN
sleep 0.5
probe lc
check lock-held-through-mouse-motion P['lc'].locked and P['lc'].enemy == P['la'].enemy
chan mouse 300 0
probe ld0
sleep 1.0
probe ld
pyexec save('lock-offset-after-mouse-and-1s-later', [enemy_off('ld0'), enemy_off('ld')])
check lock-pulls-back-after-the-mouse-stops P['ld'].locked and enemy_off('ld') < enemy_off('ld0') - 300
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
  mlines -450 0 30
  cat <<SCEN
sleep 0.3
probe lg
check lock-kept-by-a-push-inside-the-clamp P['lg'].locked
SCEN
  mlines -2000 0 50
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
  # aim adhesion: a target in front, our pawn strafes; the view follows it under the pad only
  cat <<SCEN
chan place 0 500 0
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
check lock-no-aim-adhesion-under-the-mouse yawdiff('ma','mb') == 0 and P['mb'].mouselook == 'True'
chan place 0 500 0
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
check lock-aim-adhesion-under-the-pad abs(yawdiff('mc','md')) > 300 and P['md'].mouselook == 'False'
SCEN
}

fails=0
passes=0
count() {
  local f=$1
  [ -f "$f" ] || { fails=$((fails+1)); return; }
  passes=$((passes + $(grep -c '^PASS' "$f"))); fails=$((fails + $(grep -c '^FAIL' "$f")))
}

for s in ${@:-look pad lock before}; do
  case $s in
    look)
      echo "== look ($tree, our DefUser.ini)"
      scen_look > "$out/look.scen"; prep "$tree"; run look "$tree" 0 400; count "$out/look.result";;
    pad)
      echo "== pad ($tree, ours) and pad-shipped ($ship, the shipped scripts; same DefUser.ini)"
      scen_pad > "$out/pad.scen"; cp "$out/pad.scen" "$out/pad-shipped.scen"
      prep "$tree"; run pad "$tree" 0 300; count "$out/pad.result"
      prep "$ship"; run pad-shipped "$ship" 0 300
      python3 - "$out/pad.json" "$out/pad-shipped.json" <<'PY' | tee "$out/pad-compare.result" | sed 's/\t/ /g'
import json, sys
def load(p):
    try:
        return {r['key']: r['value'] for r in map(json.loads, open(p))}
    except FileNotFoundError:
        return {}
ours, ship = load(sys.argv[1]), load(sys.argv[2])
keys = [k for k in ship if k.startswith('pad-')]
if not keys:
    print('FAIL\tpad-curves-match-shipped\tno shipped measurements'); sys.exit()
for k in sorted(keys):
    a, b = ours.get(k, {}), ship[k]
    worst = 0.0
    for m, vb in b.items():
        va = a.get(m)
        if va is None:
            worst = 1.0; continue
        worst = max(worst, abs(va - vb) / max(abs(vb), 300.0))
    print(f"{'PASS' if worst < 0.03 else 'FAIL'}\tpad-matches-shipped-{k[4:]}\tworst {worst*100:.2f}% of the shipped value (limit 3%); ours {a} shipped {b}")
PY
      count "$out/pad-compare.result";;
    lock)
      echo "== lock ($tree, two bots frozen)"
      scen_lock > "$out/lock.scen"; prep "$tree"; run lock "$tree" 2 400; count "$out/lock.result";;
    before)
      echo "== before ($ship, shipped scripts, main's DefUser.ini: what the owner played; expected to FAIL drift/symmetry)"
      git -C "$repo" show 47b1b18:Engine/Config/DefUser.ini > "$out/DefUser-main.ini"
      scen_look before > "$out/before.scen"; prep "$ship" "$out/DefUser-main.ini"; D6_KVAL=52.4 run before "$ship" 0 400;;
    *) echo "unknown scenario $s (look pad lock before)" >&2;;
  esac
done
echo "d6-mouse: $passes PASS, $fails FAIL (scenarios: ${*:-look pad lock before}; 'before' is evidence and not counted)"
