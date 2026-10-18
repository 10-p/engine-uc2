#!/usr/bin/env bash
# pp-scale: do the post-process blurs cover the same share of the picture at every resolution, as on the Xbox at
# 640x480? One PASS/FAIL line per check (INFO lines decide nothing).
#
#   tools/dev/pp-scale.sh [<tree>] [check ...]        default tree: pp; default: every check except perf
#   tools/dev/pp-scale.sh pp glow dof
#   tools/dev/pp-scale.sh list
#
#   selftest  the driver's numeric self-test (-ini:System:[UC2GLDrv.UC2GLRenderDevice]:SelfTest=True's POSTSELFTEST) at 1440x1080 and 2880x2160: 15 of 15
#             (pp-sizes: the pooled blur targets are the reference picture's fractions; ref-copy, ref-taps,
#             bb-copy-ref with 3x3 and 5x5 samples per reference pixel)
#   glow      DM-Deadbolt, pinned first PlayerStart, the ceiling tube: the glow post-processing adds (on - off),
#             measured on the 480-line picture (pp-measure.py glow): width above 10% of its peak and the peak, at
#             640x480, 1440x1080, 2880x2160 (all 4:3, same framing), with the reference height 480 and 0
#   dof       DM-Azure, pinned: the always-on depth of field's blur in the distance (DOF on against DOF off, same
#             frozen frame), as a Gaussian sigma in 480-line pixels (pp-measure.py sigma), same resolutions
#   blur      DM-Deadbolt: the concussion blur (BlurMe) on the poster, sigma, same resolutions
#   radial    DM-Deadbolt: the radial blur held at distance 30, as one zoomed copy (pp-measure.py zoom): its inset
#             in 640x480 pixels, same resolutions
#   identity  640x480, post-processing on, a frame frozen from the start: this binary against main's (the tree's
#             System/uc2-base.bin), twice each; every pixel outside the two spinning pickups must be equal
#   aspect    1920x1080 with AutoFOV on (the normal view): the tube's glow (the 4:3 middle of the 16:9
#             picture is the 4:3 picture), this binary and main's; sheets
#   sheets    picture sheets from the pictures above (also logs/pp2): logs/<tree>/sheets
#   perf      (not in the default list) frames per second at 1920x1080 and 3840x2160 on the GPU (UC2_GPU=1 of
#             tools/dev/fx-owner.sh, offscreen, no window), reference height 480 then 0 in one run, and main's
#             binary
#
# The pictures are frozen in time (set LevelInfo TimeDilation 0.0001) once the match runs, so the shots of one run
# differ only in what the run switched (post-processing, PPREFERENCE, an effect); the dynamic gamma (the bloom's
# threshold follows it) converges over frames, so each shot waits long enough at every resolution (the log's
# frame numbers are printed). Every run goes through tools/dev/run-uc2-shot.sh (<repo>/../bin/capped 2G; 3G at
# 2880x2160 through UC2_CAP), headless, AA off, AutoFOV off and Epic's 1:1 UI unless the check says otherwise.
# Pictures, logs and sheets: <repo>/../logs/<tree>/pp-*.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=pp
if [ $# -gt 0 ] && [ -d "$work/run/$1" ]; then tree=$1; shift; fi
out=$work/logs/$tree
shot=$repo/tools/dev/run-uc2-shot.sh
M="python3 $repo/tools/dev/pp-measure.py"
PIN="set UCDeathmatch NumPlayers 0"
ALL="selftest glow dof blur radial identity aspect"
if [ "${1:-}" = list ]; then sed -n '/^#   selftest/,/^#             binary$/p' "$0"; exit 0; fi
want=${*:-$ALL}
mkdir -p "$out"
summary=$out/pp-scale-summary.txt
: > "$summary"
result() { echo "$1 pp-$2: $3" | tee -a "$summary"; }
info() { echo "INFO pp-$1: $2" | tee -a "$summary"; }
num() { sed -E "s/.*$1=([-0-9.]+).*/\\1/" <<<"$2"; }
# within <a> <b> <tolerance>: |a/b - 1| <= tolerance
within() { awk -v a="$1" -v b="$2" -v t="$3" 'BEGIN{ if (b == 0) exit 1; r = a / b; exit !(r >= 1 - t && r <= 1 + t) }'; }
ratio() { awk -v a="$1" -v b="$2" 'BEGIN{ printf "%.2f", (b == 0 ? 0 : a / b) }'; }
runok() {  # runok <log>: "critical=N glerrors=N" and 0 if both 0
  local c e
  c=$(grep -a -c "Critical" "$1" 2>/dev/null)
  e=$(grep -a -o "[0-9]* GL errors so far\|UC2GLDrv: [0-9]* GL errors in [0-9]* frames" "$1" 2>/dev/null | tail -1 | grep -o "[0-9]* GL errors" | grep -o "^[0-9]*")
  echo "critical=${c:-?} glerrors=${e:-?}"
  [ "${c:-1}" = 0 ] && [ "${e:-1}" = 0 ]
}
frames() {  # frames <log>: "shot@T=frame ..." from the engine's own lines
  grep -a -o -- "-glshot at [0-9.]* s (frame [0-9]*)" "$1" 2>/dev/null | sed -E 's/-glshot at ([0-9.]+) s \(frame ([0-9]+)\)/\1s:f\2/' | tr '\n' ' '
}
# Waits per resolution, in seconds after post-processing goes on (W1) and after a later switch (W2): enough
# frames for the dynamic gamma to settle (it moves 5% of the way per frame) at the software-GL frame rates here.
waits() {
  case $1 in
    640x480)   W1=20; W2=12; G=2; CAP=2G ;;
    1440x1080) W1=70; W2=35; G=4; CAP=2G ;;
    *)         W1=${PP_WAIT_4K:-180}; W2=$(( ${PP_WAIT_4K:-180} / 2 )); G=12; CAP=3G ;;
  esac
}
runshot() {  # runshot <name> <res> <secs> <fire> <map> <args...>: one run, pinned, pictures <name>-N.png
  local name=$1 res=$2 secs=$3 fire=$4 map=$5; shift 5
  rm -f "$out/$name"-*.png
  UC2_CAP=$CAP UC2_RES=$res UC2_PRECMD="$PIN" "$shot" "$tree" "$name" "$secs" "$fire" "$map" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True "$@" > "$out/$name.report" 2>&1
}
RESOLUTIONS=${PP_RESOLUTIONS:-"640x480 1440x1080 2880x2160"}

check_selftest() {
  local res ok=1 txt=""
  for res in 1440x1080 2880x2160; do
    waits $res
    runshot pp-selftest-$res $res $((80+W1+W1/2)) 34 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=$((44+W1)):POSTSELFTEST@$((50+W1+W1/2)):exit
    local o=$out/pp-selftest-$res.out s
    s=$(grep -a -o "selftest summary: [0-9]* passed, [0-9]* failed" "$o")
    grep -a "UC2GLDrv: selftest \(pp-sizes\|ref-\|bb-copy-ref\)" "$o" | sed -E "s/^UC2GLDrv: //; s/ values compared//; s/^/INFO pp-selftest $res: /" | cut -c1-400 | tee -a "$summary"
    [ "$s" = "selftest summary: 15 passed, 0 failed" ] && runok "$o" > /dev/null || ok=0
    txt="$txt $res: ${s:-no summary}, $(runok "$o");"
  done
  [ $ok = 1 ] && result PASS selftest "$txt" || result FAIL selftest "$txt"
}

# glow: shots 0 = off (before the freeze), 1 = on, reference 480, 2 = on, reference 0.
check_glow() {
  local res g1 g2 base_w="" base_p="" fails=0
  for res in $RESOLUTIONS; do
    waits $res
    # G: seconds between a shot and the next switch, a few frames at that resolution's frame rate (a shot is
    # taken at the end of the first frame after its time; a switch made in between would be in it).
    local on=$((44+G)); local t1=$((on+W1)); local t2=$((t1+G+W2))
    runshot pp-glow-$res $res $((t2+25)) 34 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:PostProcessToggleTimes=$on -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44,$t1,$t2 \
      -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=$((on+3)):set+LevelInfo+TimeDilation+0.0001@$((t1+G)):PPREFERENCE+0
    local o=$out/pp-glow-$res
    if [ ! -f "$o-2.png" ]; then result FAIL glow-$res "no pictures ($(runok "$o.out"); see $o.out)"; fails=$((fails+1)); continue; fi
    g1=$($M glow "$o-0.png" "$o-1.png"); g2=$($M glow "$o-0.png" "$o-2.png")
    # Frames from post-processing on to the reference-480 shot: the dynamic gamma (the bloom's threshold) has
    # 0.95^n of its start offset left; below 60 frames (5%) the peak is not comparable.
    local pon f1 settled
    pon=$(grep -a -o 'post-processing on (frame [0-9]*)' "$o.out" | head -1 | grep -o '[0-9]*')
    f1=$(frames "$o.out" | awk '{print $2}' | sed 's/.*:f//')
    settled=$(( ${f1:-0} - ${pon:-0} ))
    info glow-$res "reference 480: $g1; reference 0 (as before): $g2; $(runok "$o.out"); shots $(frames "$o.out"); $settled frames from post-processing on to the reference-480 shot"
    [ "$settled" -ge 60 ] || { result FAIL glow-$res "only $settled frames between post-processing on and the shot: the dynamic gamma has not settled (more time: waits())"; fails=$((fails+1)); continue; }
    if [ "$res" = 640x480 ]; then base_w=$(num width "$g1"); base_p=$(num peak "$g1"); fi
    [ -n "$base_w" ] || continue
    local w1 p1 w2; w1=$(num width "$g1"); p1=$(num peak "$g1"); w2=$(num width "$g2")
    # With the reference: width and peak as at 640x480 within 10%. Without it: the width shrinks with the
    # resolution (expected 480/height of it, i.e. 0.44 at 1080 lines, 0.22 at 2160), or stays at 640x480.
    if within "$w1" "$base_w" 0.10 && within "$p1" "$base_p" 0.10 && runok "$o.out" > /dev/null; then
      result PASS glow-$res "reference 480: width $w1% of the height (640x480: $base_w%, ratio $(ratio $w1 $base_w)), peak $p1 (640x480: $base_p, ratio $(ratio $p1 $base_p)); reference 0: width $w2% (ratio $(ratio $w2 $base_w), the old behaviour)"
    else
      result FAIL glow-$res "reference 480: width $w1% (640x480: $base_w%), peak $p1 (640x480: $base_p); reference 0: width $w2%; $(runok "$o.out")"; fails=$((fails+1))
    fi
  done
  [ -f "$out/pp-glow-640x480-1.png" ] && [ -f "$out/pp-glow-1440x1080-2.png" ] && \
    $M sheet "$out/pp-sheet-glow.png" "640x480" "$out/pp-glow-640x480-1.png" "1440x1080 old (reference 0)" "$out/pp-glow-1440x1080-2.png" "1440x1080 new (reference 480)" "$out/pp-glow-1440x1080-1.png" > /dev/null
  [ -f "$out/pp-glow-2880x2160-1.png" ] && \
    $M sheet "$out/pp-sheet-glow-2160.png" "640x480" "$out/pp-glow-640x480-1.png" "2880x2160 old (reference 0)" "$out/pp-glow-2880x2160-2.png" "2880x2160 new (reference 480)" "$out/pp-glow-2880x2160-1.png" > /dev/null
}

# sigma check, shared by dof, blur, radial: pairs (base shot, effect shot) for reference 480 and reference 0.
#   sigmacheck <check> <res> <box> <axis|zoom> <ref480-base> <ref480-effect> <ref0-base> <ref0-effect>
declare -A SIG480 SIG0
sigmacheck() {
  local check=$1 res=$2 box=$3 axis=$4 o=$out/pp-$1-$2
  local s1 s2 key=sigma
  if [ "$axis" = zoom ]; then
    key=inset
    s1=$($M zoom "$o-$5.png" "$o-$6.png" $box); s2=$($M zoom "$o-$7.png" "$o-$8.png" $box)
  else
    s1=$($M sigma "$o-$5.png" "$o-$6.png" $box $axis); s2=$($M sigma "$o-$7.png" "$o-$8.png" $box $axis)
  fi
  SIG480[$check-$res]=$(num $key "$s1"); SIG0[$check-$res]=$(num $key "$s2")
  # Every shot at least 3 frames after the one before (else a switch may have landed in the wrong picture).
  local fr gaps prev="" ok=1
  for fr in $(frames "$o.out" | grep -o 'f[0-9]*' | tr -d f); do
    [ -n "$prev" ] && [ $((fr - prev)) -lt 3 ] && ok=0
    prev=$fr
  done
  [ $ok = 1 ] || { SIG480[$check-$res]=""; info $check-$res "shots fewer than 3 frames apart ($(frames "$o.out")): not judged; rerun on a quieter machine"; }
  info $check-$res "box $box ($axis) on the 480-line picture: reference 480: $s1; reference 0 (as before): $s2; $(runok "$o.out"); shots $(frames "$o.out")"
}
# judge <check> <tolerance> <expect-old>: the reference-480 sigma at every resolution within tolerance of 640x480's;
# expect-old=shrinks: without the reference the sigma falls with the resolution; =same: the effect never depended
# on it (reported, not judged).
judge() {
  local check=$1 tol=$2 old=$3 res b=${SIG480[$1-640x480]:-} fails=0 txt=""
  [ -n "$b" ] || { result FAIL $check "no 640x480 measurement"; return; }
  for res in $RESOLUTIONS; do
    [ "$res" = 640x480 ] && continue
    local s1=${SIG480[$check-$res]:-} s0=${SIG0[$check-$res]:-}
    if [ -z "$s1" ]; then fails=$((fails+1)); txt="$txt $res: no measurement;"; continue; fi
    within "$s1" "$b" "$tol" || fails=$((fails+1))
    txt="$txt $res: $s1 (ratio $(ratio $s1 $b)), reference 0: ${s0:-?} (ratio $(ratio ${s0:-0} $b));"
  done
  [ $fails = 0 ] && result PASS $check "640x480: $b 480-line pixels;$txt within ${tol} with the reference 480" || result FAIL $check "640x480: $b;$txt (tolerance $tol)"
}

# dof: post-processing on from the start; frozen; 0 = DOF on (ref 480), 1 = DOF off (ref 480), 2 = DOF off
# (ref 0), 3 = DOF on (ref 0).
check_dof() {
  local res
  for res in $RESOLUTIONS; do
    waits $res
    local t0=$((52+W1)); local t1=$((t0+2*G+8)); local t2=$((t1+G+W2)); local t3=$((t2+2*G+8))
    runshot pp-dof-$res $res $((t3+25)) 42 DM-Azure -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=$t0,$t1,$t2,$t3 \
      -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=50:set+LevelInfo+TimeDilation+0.0001@$((t0+G)):set+UCScreenDOF+Enabled+0@$((t1+G)):PPREFERENCE+0@$((t2+G)):set+UCScreenDOF+Enabled+1
    [ -f "$out/pp-dof-$res-3.png" ] || { info dof-$res "no pictures ($(runok "$out/pp-dof-$res.out"))"; continue; }
    sigmacheck dof $res "${PP_DOF_BOX:-310 186 428 230}" xy 1 0 2 3
  done
  judge dof 0.15 shrinks
  [ -f "$out/pp-dof-1440x1080-3.png" ] && $M sheet "$out/pp-sheet-dof.png" "640x480" "$out/pp-dof-640x480-0.png" "1440x1080 old (reference 0)" "$out/pp-dof-1440x1080-3.png" "1440x1080 new (reference 480)" "$out/pp-dof-1440x1080-0.png" > /dev/null
}

# blur (concussion, BlurMe): on from the start; frozen; 0 = none (ref 480), 1 = none (ref 0), 2 = BlurMe (ref 480),
# 3 = BlurMe (ref 0). The blur's oscillators stand still with the frozen clock.
check_blur() {
  local res
  for res in $RESOLUTIONS; do
    waits $res
    # (half waits between the switches: the pairs compared are 0/2 and 1/3, each at one reference height)
    local h=$((W2/2)); local t0=$((42+W1)); local t1=$((t0+G+h)); local t2=$((t1+2*G+h)); local t3=$((t2+G+h))
    runshot pp-blur-$res $res $((t3+25)) 34 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=$t0,$t1,$t2,$t3 \
      -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=40:set+LevelInfo+TimeDilation+0.0001@$((t0+G)):PPREFERENCE+0@$((t1+G)):PPREFERENCE+480@$((t1+G+1)):BlurMe@$((t2+G)):PPREFERENCE+0
    [ -f "$out/pp-blur-$res-3.png" ] || { info blur-$res "no pictures ($(runok "$out/pp-blur-$res.out"))"; continue; }
    sigmacheck blur $res "${PP_BLUR_BOX:-360 175 500 255}" xy 0 2 1 3
  done
  # The concussion blur's four reads are offset by oscillators of the material (fractions of the picture, 2-6 Hz):
  # the frozen frame catches them at a phase that differs from run to run, so its sigma moves between runs by
  # more than between resolutions. Judged: one run's reference-480 and reference-0 shots (same phase) agree,
  # and the resolutions agree within 30%.
  local r same=0 txt=""
  for r in $RESOLUTIONS; do
    [ -n "${SIG480[blur-$r]:-}" ] && [ -n "${SIG0[blur-$r]:-}" ] || continue
    within "${SIG480[blur-$r]}" "${SIG0[blur-$r]}" 0.10 || same=1
    txt="$txt $r: ${SIG480[blur-$r]} / ${SIG0[blur-$r]};"
  done
  [ $same = 0 ] && result PASS blur-setting "sigma with reference 480 / 0 in the same frozen frame:$txt the setting does not touch it (texture-coordinate offsets)" || result FAIL blur-setting "reference 480 / 0:$txt"
  judge blur 0.30 same
  [ -f "$out/pp-blur-1440x1080-3.png" ] && $M sheet "$out/pp-sheet-blur.png" "640x480" "$out/pp-blur-640x480-2.png" "1440x1080 old (reference 0)" "$out/pp-blur-1440x1080-3.png" "1440x1080 new (reference 480)" "$out/pp-blur-1440x1080-2.png" > /dev/null
}

# radial: on from the start; frozen; 0 = none (ref 480), 1 = radial 30 (ref 480), 2 = radial (ref 0), 3 = none (ref 0).
check_radial() {
  local res
  for res in $RESOLUTIONS; do
    waits $res
    local t0=$((42+W1)); local t1=$((t0+2*G+6)); local t2=$((t1+G+W2)); local t3=$((t2+2*G+6))
    runshot pp-radial-$res $res $((t3+25)) 34 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=$t0,$t1,$t2,$t3 \
      -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=38:set+UCPlayerController+RadialBlurSpeed+0@38.2:set+UCPlayerController+RadialBlurMinChange+-1@40:set+LevelInfo+TimeDilation+0.0001@$((t0+G)):set+UCScreenRadialBlur+RadialBlurDistance+30@$((t0+G)).2:set+UCScreenRadialBlur+Enabled+1@$((t1+G)):PPREFERENCE+0@$((t2+G)):set+UCScreenRadialBlur+Enabled+0
    [ -f "$out/pp-radial-$res-3.png" ] || { info radial-$res "no pictures ($(runok "$out/pp-radial-$res.out"))"; continue; }
    sigmacheck radial $res "${PP_RADIAL_BOX:-150 95 620 300}" zoom 0 1 3 2
  done
  judge radial 0.15 shrinks
  [ -f "$out/pp-radial-1440x1080-3.png" ] && $M sheet "$out/pp-sheet-radial.png" "640x480" "$out/pp-radial-640x480-1.png" "1440x1080 old (reference 0)" "$out/pp-radial-1440x1080-2.png" "1440x1080 new (reference 480)" "$out/pp-radial-1440x1080-1.png" > /dev/null
}

# identity: 640x480, post-processing on, the frame before the match (no Fire: the view behind the "press fire"
# board, every camera effect running), game time frozen from the first frame and the dynamic gamma stopped after
# its first update (DYNAMICGAMMA 0), so that two runs of one binary give the same picture except at the two
# spinning pickups and their glow (rows 220-320). Main's binary (System/uc2-base.bin) twice and this one twice;
# outside that band every pixel must be equal.
check_identity() {
  local b i
  [ -f "$work/run/$tree/System/uc2-base.bin" ] || { result FAIL identity "no System/uc2-base.bin in run/$tree (main's binary)"; return; }
  CAP=2G
  for b in base new; do
    for i in 1 2; do
      if [ $b = base ]; then UC2_BIN=uc2-base.bin runshot pp-id-$b$i 640x480 45 0 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=0:DYNAMICGAMMA+0@0.1:set+LevelInfo+TimeDilation+0.0001 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=25
      else runshot pp-id-$b$i 640x480 45 0 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=0:DYNAMICGAMMA+0@0.1:set+LevelInfo+TimeDilation+0.0001 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=25; fi
    done
  done
  local o=$out/pp-id p q t bo all txt="" bad=0
  for pq in "base1 base2" "new1 new2" "base1 new1" "base2 new2"; do
    set -- $pq
    t=$($M diff "$o-$1-0.png" "$o-$2-0.png" 0 0 640 220); bo=$($M diff "$o-$1-0.png" "$o-$2-0.png" 0 320 640 480)
    all=$($M diff "$o-$1-0.png" "$o-$2-0.png")
    [ "$(num differing "$t")" = 0 ] && [ "$(num differing "$bo")" = 0 ] || bad=1
    txt="$txt $1/$2: rows 0-219 $(num differing "$t"), rows 320-479 $(num differing "$bo") differing (whole frame: $all);"
  done
  grep -a -q "UCCameraEffects: UCScreenDOF target" "$o-new1.out" || bad=1
  [ $bad = 0 ] && result PASS identity "640x480, post-processing on (the camera effects' targets are in the log):$txt" || result FAIL identity "$txt"
}

# aspect: 1920x1080, AutoFOV on (Hor+), on, frozen; this binary (ref 480 then 0) and main's. The tube in the
# 853x480 picture is at x = 411 + (853 - 640) / 2 = 518.
check_aspect() {
  local g1 g2 gb res=1920x1080
  waits 1440x1080
  local t1=$((46+W1)) t2=$((48+W1+W2))
  UC2_UIBOX=1 UC2_AUTOFOV=1 runshot pp-aspect-new $res $((t2+25)) 34 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:PostProcessToggleTimes=46 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44,$t1,$t2 \
    -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=49:set+LevelInfo+TimeDilation+0.0001@$((t1+2)):PPREFERENCE+0
  UC2_UIBOX=1 UC2_AUTOFOV=1 UC2_BIN=uc2-base.bin runshot pp-aspect-base $res $((t1+25)) 34 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:PostProcessToggleTimes=46 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44,$t1 \
    -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=49:set+LevelInfo+TimeDilation+0.0001
  local o=$out/pp-aspect
  g1=$($M glow "$o-new-0.png" "$o-new-1.png" 518 138 10); g2=$($M glow "$o-new-0.png" "$o-new-2.png" 518 138 10)
  gb=$($M glow "$o-base-0.png" "$o-base-1.png" 518 138 10)
  $M sheet "$out/pp-sheet-aspect.png" "1920x1080 AutoFOV on, main" "$o-base-1.png" "1920x1080 AutoFOV on, new (reference 480)" "$o-new-1.png" > /dev/null
  local base_w; base_w=$(num width "$($M glow "$out/pp-glow-640x480-0.png" "$out/pp-glow-640x480-1.png" 2>/dev/null)")
  if [ -n "$base_w" ] && within "$(num width "$g1")" "$base_w" 0.10; then
    result PASS aspect "1920x1080 AutoFOV on, the tube: new $g1 (4:3 640x480: width $base_w%); main's binary $gb; new with reference 0 $g2; $(runok "$o-new.out")"
  else
    result FAIL aspect "1920x1080 AutoFOV on: new $g1, main $gb, reference 0 $g2 (640x480 width: ${base_w:-not measured: run glow first})"
  fi
}

# perf: GPU, offscreen (fx-owner.sh's UC2_GPU=1), DM-Deadbolt, the pinned first PlayerStart (no bots: NumPlayers 0
# before Fire) and game time frozen, so every frame is the same picture: frames counted between -glexec markers
# 15 s apart, reference 480, 0, 480 in one run; main's binary the same way. Reported, not judged (the machine is
# shared: the CPU side of the frame competes with other work).
check_perf() {
  local res scen=$(mktemp)
  # fx-owner.sh stops the game when the scenario ends: keep it running past the last marker (95 s after the
  # first frame; the common start takes about 35 s).
  echo 'sleep 95' > "$scen"
  for res in 1920x1080 3840x2160; do
    local X=${res%x*} Y=${res#*x} b
    for b in uc2.bin uc2-base.bin; do
      UC2_GPU=1 UC2_BIN=$b FX_SCENARIO=$scen FX_MAP=DM-Deadbolt FX_NAME=pp-perf-$res \
        FX_ARGS="-ini:System:[UC2GLDrv.UC2GLRenderDevice]:AntiAliasing=0 -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:AntiAliasing=0 -ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=$X -ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY=$Y -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=1:set+UCDeathmatch+NumPlayers+0@32:set+LevelInfo+TimeDilation+0.0001@40:GLINFO@55:GLINFO@56:PPREFERENCE+0@60:GLINFO@75:GLINFO@76:PPREFERENCE+480@80:GLINFO@95:GLINFO" \
        "$repo/tools/dev/fx-owner.sh" "$tree" custom > /dev/null 2>&1
      local suffix=""; [ $b = uc2-base.bin ] && suffix="-base"
      local log=$out/fx-pp-perf-$res$suffix-gpu.out
      local f; f=$(grep -a -o -- "-glexec at [0-9.]* s (frame [0-9]*)" "$log" | sed -E 's/.*frame ([0-9]+)\)/\1/' | tr '\n' ' ')
      # markers: 1 s pin, 32 s freeze, then 40/55 (480), 56 switch, 60/75 (0), 76 switch, 80/95 (480)
      read -r p1 p2 a1 a2 a3 a4 a5 a6 a7 a8 <<<"$f"
      if [ -n "${a8:-}" ]; then
        # Three 15 s windows: reference 480, 0, 480 again (main's binary ignores PPREFERENCE: its three windows
        # show how much the frame rate drifts on its own).
        info perf-$res$suffix "frames per second in three 15 s windows, reference 480 / 0 / 480: $(awk -v a=$a1 -v b=$a2 'BEGIN{printf "%.1f",(b-a)/15}') / $(awk -v a=$a4 -v b=$a5 'BEGIN{printf "%.1f",(b-a)/15}') / $(awk -v a=$a7 -v b=$a8 'BEGIN{printf "%.1f",(b-a)/15}') ($(grep -a -o 'UC2GLDrv: main framebuffer [0-9x]*' "$log" | head -1); $(grep -a -o 'NVIDIA[^/;]*' "$log" | head -1))"
      else
        info perf-$res$suffix "no frame markers (see $log)"
      fi
    done
  done
  rm -f "$scen"
}

# sheets: the picture sheets from the pictures the checks left (in this tree's logs and in PP_ALSO, default
# logs/pp2): per effect, 640x480 | 1440x1080 old | 1440x1080 new (and the same at 2880x2160), whole pictures at the
# 480-line scale, and a part of them enlarged twice from each picture's own resolution (pp-measure.py crop);
# the 16:9 AutoFOV view, main's binary against this one. Into <repo>/../logs/<tree>/sheets.
check_sheets() {
  local d=$out/sheets also=${PP_ALSO:-$work/logs/pp2} spec e on old new box res n=0
  mkdir -p "$d"
  pic() { if [ -f "$out/$1" ]; then echo "$out/$1"; elif [ -f "$also/$1" ]; then echo "$also/$1"; fi; }
  # effect | shot with the effect, reference 480 | the same, reference 0 | crop box on the 480-line picture
  for spec in "glow|1|2|330 90 500 200" "dof|0|3|280 150 460 260" "blur|2|3|340 150 520 280" "radial|1|2|440 60 640 330"; do
    IFS='|' read -r e on old box <<<"$spec"
    for res in 1440x1080 2880x2160; do
      local a b c; a=$(pic pp-$e-640x480-$on.png); b=$(pic pp-$e-$res-$old.png); c=$(pic pp-$e-$res-$on.png)
      [ -n "$a" ] && [ -n "$b" ] && [ -n "$c" ] || continue
      $M sheet "$d/$e-$res.png" "640x480 (the Xbox's picture)" "$a" "$res before (main)" "$b" "$res now (reference 480)" "$c" > /dev/null
      $M crop "$d/$e-$res-detail.png" $box 2 "640x480" "$a" "$res before" "$b" "$res now" "$c" > /dev/null
      n=$((n+2))
    done
  done
  local mb nw; mb=$(pic pp-aspect-base-1.png); nw=$(pic pp-aspect-new-1.png)
  if [ -n "$mb" ] && [ -n "$nw" ]; then
    $M sheet "$d/aspect-1920x1080-autofov.png" "1920x1080 AutoFOV on, before (main)" "$mb" "1920x1080 AutoFOV on, now" "$nw" > /dev/null
    $M crop "$d/aspect-1920x1080-autofov-detail.png" 436 90 606 200 3 "before (main)" "$mb" "now" "$nw" > /dev/null
    cp -f "$nw" "$d/aspect-1920x1080-autofov-now-full.png"
    n=$((n+3))
  fi
  info sheets "$n sheets in $d: $(cd "$d" && ls | tr '\n' ' ')"
}

for c in $want; do
  case $c in
    selftest|glow|dof|blur|radial|identity|aspect|perf|sheets) check_$c ;;
    *) echo "unknown check $c (tools/dev/pp-scale.sh list)" ;;
  esac
done
echo "summary: $(grep -c '^PASS' "$summary") PASS, $(grep -c '^FAIL' "$summary") FAIL ($summary)"
