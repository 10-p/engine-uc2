#!/usr/bin/env bash
# M11 / G2: every renderer test against the GL4ES/GLES2 twin of the driver, one PASS/FAIL line per check.
#
#   tools/dev/m11-twin.sh [check ...]          default: every check, in this order
#   tools/dev/m11-twin.sh list
#
# The twin is the same uc2.bin with -gl4es (render device UC2GL4ESDrv.UC2GL4ESRenderDevice: an OpenGL ES 2
# context behind gl4es, the code path the browser runs). Reference is always the desktop binding of the same
# binary, run in the same call from the same pinned views. Two run trees are rebuilt from build-native:
# <WorkUC2>/run/m11 (desktop) and run/m11t (twin); logs and pictures go to logs/m11 and logs/m11t. Every run
# is inside <WorkUC2>/bin/capped (2G) through tools/dev/run-uc2-shot.sh; nothing is left running.
#
#   shaders    tools/psh2glsl.py --selftest: the 100 pixel shaders, interpreter against Epic's compiled words
#   gputest    tools/psh2glsl.py --gputest: the GLSL of every pixel shader against the interpreter (desktop GL)
#   boot       twin: menu for 40 s; device class, 97 fragment + 405 vertex shaders through gl4es, 0 GL errors
#              (84 until M7b, which compiles the 12 projector shaders and PS_Terrain3Layer too)
#   selftest   -ini:System:[UC2GLDrv.UC2GLRenderDevice]:SelfTest=True numeric checks on both bindings: 15 of 15 on each (M7's ten + fixed-function fog + pp-scale's
#              pp-sizes, ref-copy, ref-taps, bb-copy-ref, which run at 1280x720), same tolerances
#   progtest   485 complete programs (97 pixel shaders x 5 vertex shader enums; 420 = 84 x 5 before M7b) drawn on both, compared
#   pinned     DM-Deadbolt (first PlayerStart) and the menu, desktop against twin: post-processing off (static
#              part of the frame) and on
#   effects    tools/dev/m7-effects.sh on both; the desktop's B pictures are the twin's fx-<effect>-b.ref.png
#   m6         the M6 pixel-shader pictures (pixel shaders on / off) on both
#   fallbacks  the twin with each capability masked: -ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=s3tc (engine decodes DXT), -ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=maxlevel on the
#              desktop (mip chains completed by the driver), -ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=fragdepth (depth test in the distortion
#              passes' shaders), -ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=uintindex (32-bit indices stored as 16-bit); -ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=depthtexture is run
#              and reported, its fallback is NOT implemented (the line says what happens instead)
#   movies     tools/dev/m9-movies.py scenarios logos, logos-skip, menu-movie on the twin
#
# M11_SERIAL=1 runs the two bindings one after the other instead of side by side (one game run at a time).
# Thresholds are in the check functions, each with its reason; docs/porting/analysis/web-renderer.md has the
# measurements they come from. A line starting with "INFO" is a measurement that decides nothing.
set -u
# The twin's OpenGL ES context is version 2.0, not the 3.2 Mesa gives by default: ES-3-only leniency would
# hide what WebGL 1 rejects (web-renderer.md, "A stricter twin"). MESA_DEBUG=1 makes Mesa print every error it
# raises, including those gl4es' own glGetError calls swallow; the boot check counts those lines. Neither
# affects a desktop OpenGL context.
export MESA_GLES_VERSION_OVERRIDE=${MESA_GLES_VERSION_OVERRIDE:-2.0}
export MESA_DEBUG=${MESA_DEBUG:-1}
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
capped=$work/bin/capped
shot=$repo/tools/dev/run-uc2-shot.sh
imgdiff=$repo/tools/dev/imgdiff.py
# run trees: desktop binding, twin (M11_DESKTOP_TREE / M11_TWIN_TREE: other names, so that a branch can run
# the whole script without touching the trees of main)
D=${M11_DESKTOP_TREE:-m11}; T=${M11_TWIN_TREE:-m11t}
LD=$work/logs/$D; LT=$work/logs/$T
ALL="shaders gputest boot selftest progtest pinned effects m6 fallbacks movies"
if [ "${1:-}" = list ]; then sed -n '/^#   shaders/,/^#   movies/p' "$0"; exit 0; fi
want=${*:-$ALL}
summary=$LT/m11-twin-summary.txt
mkdir -p "$LD" "$LT"; : > "$summary"

result() { echo "$1 m11-$2: $3" | tee -a "$summary"; }
num() { sed -E "s/.*$1=([0-9.]+).*/\\1/" <<<"$2"; }
le() { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a+0<=b+0)}'; }
crit() { grep -a -c "Critical" "$1" 2>/dev/null; }
glerr() { grep -a -o "UC2GLDrv: [0-9]* GL errors in [0-9]* frames\|, [0-9]* GL errors so far" "$1" 2>/dev/null | tail -1 | grep -o "[0-9]* GL errors" | grep -o "^[0-9]*"; }

if [ -z "${M11_NO_MKRUN:-}" ]; then
  "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" $D > /dev/null || exit 1
  "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" $T > /dev/null || exit 1
fi
PIN='set UCDeathmatch NumPlayers 0'

# Both bindings at once (two trees): desktop in the background, twin in the foreground. M11_SERIAL=1: one after the
# other (a loaded machine: one game run at a time; the coordinator's rule of 2026-10-08).
both() {  # both <name> <secs> <fire> <map> <args...>
  local name=$1 secs=$2 fire=$3 map=$4; shift 4
  if [ -n "${M11_SERIAL:-}" ]; then
    UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $D "$name" "$secs" "$fire" "$map" -opengl -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True "$@" > "$LD/$name.report" 2>&1
  else
    UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $D "$name" "$secs" "$fire" "$map" -opengl -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True "$@" > "$LD/$name.report" 2>&1 &
  fi
  local pid=${!:-}
  UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $T "$name" "$secs" "$fire" "$map" -gl4es -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True "$@" > "$LT/$name.report" 2>&1
  [ -z "$pid" ] || wait $pid
}

check_shaders() {
  local line; line=$("$capped" 1G 300 -- python3 "$repo/tools/psh2glsl.py" --selftest 2>&1 | tail -1)
  if [[ "$line" == *" 0 failed"* ]] && [[ "$line" == *"100 shaders"* ]]; then result PASS shaders "$line"; else result FAIL shaders "$line"; fi
}

check_gputest() {
  local line; line=$("$capped" 2G 900 -- xvfb-run -a -s "-screen 0 640x480x24" python3 "$repo/tools/psh2glsl.py" --gputest 2>&1 | tail -1)
  if [[ "$line" == *"0 shaders failed"* ]] && [[ "$line" == *", 0 outside the tolerance"* ]]; then result PASS gputest "$line"; else result FAIL gputest "$line"; fi
}

check_boot() {
  UC2_RES=1280x720 "$shot" $T boot 45 0 '' -gl4es -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=30:exit > "$LT/boot.report" 2>&1
  local o=$LT/boot.out cls fs vs e
  cls=$(grep -a -o "Initializing UC2GLDrv (class [^;]*; binding: [^)]*)" "$o")
  fs=$(grep -a -o "pixel shaders on: [0-9]* fragment shaders compiled, [0-9]* failed" "$o")
  vs=$(grep -a -o "[0-9]* vertex shaders compiled ([0-9]* failed)" "$o" | head -1)
  e=$(grep -a -o "UC2GLDrv: [0-9]* GL errors in [0-9]* frames ([0-9]* frames with errors)" "$o")
  if [[ "$cls" == *"UC2GL4ESDrv.UC2GL4ESRenderDevice"* ]] && [[ "$fs" == *"97 fragment shaders compiled, 0 failed" ]] && [[ "$vs" == "405 vertex shaders compiled (0 failed)" ]] \
     && [[ "$e" == "UC2GLDrv: 0 GL errors in "* ]] && [ "$(crit "$o")" = 0 ] && grep -a -q "Bringing Level Menu.MyLevel up for play" "$o" \
     && [ "$(grep -a -c '^Mesa: error' "$o")" = 0 ]; then
    result PASS boot "$cls; $fs; $vs; ${e#UC2GLDrv: }; Mesa errors 0; $(grep -a -o 'GLES hardware: .*' "$o"); $(grep -a -o 'capabilities: .*' "$o")"
  else
    result FAIL boot "class '$cls' fragment '$fs' vertex '$vs' errors '$e' Mesa errors $(grep -a -c '^Mesa: error' "$o") critical $(crit "$o") (see $o)"
  fi
}

check_selftest() {   # also produces the logs check_progtest compares
  both fx-selftest 220 34 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=44:POSTSELFTEST@170:exit
  local t ok=1 txt=""
  for t in $D $T; do
    local o=$work/logs/$t/fx-selftest.out s e
    s=$(grep -a -o "selftest summary: [0-9]* passed, [0-9]* failed" "$o")
    e=$(grep -a -o "[0-9]* GL errors in [0-9]* frames" "$o" | tail -1)
    [ "$s" = "selftest summary: 15 passed, 0 failed" ] && [[ "$e" == "0 GL errors in "* ]] && [ "$(crit "$o")" = 0 ] || ok=0
    txt="$txt $([ $t = $D ] && echo desktop || echo twin): ${s:-no summary}, ${e:-no error count};"
    grep -a "UC2GLDrv: selftest [a-z-]*: " "$o" | sed -E "s/^UC2GLDrv: //; s/;.*//; s/^/INFO m11-selftest $([ $t = $D ] && echo desktop || echo twin): /" | tee -a "$summary"
  done
  [ $ok = 1 ] && result PASS selftest "$txt" || result FAIL selftest "$txt"
}

check_progtest() {
  [ -f "$LD/fx-selftest.out" ] && [ -f "$LT/fx-selftest.out" ] && [ -z "${ran_selftest:-}" ] && : # logs of this call or an earlier one
  local line; line=$(python3 "$repo/tools/dev/m11-progcmp.py" "$LD/fx-selftest.out" "$LT/fx-selftest.out" 2 2>&1 | tail -1)
  # Tolerance 2 of 255: both sides compute in 32-bit floats on the same rasteriser; the last step is a
  # rounding to 8 bits (measured: largest difference 1).
  if [[ "$line" == "progtest: 485 programs on side A, 485 on side B"*", 0 programs outside the tolerance or missing" ]]; then result PASS progtest "$line"; else result FAIL progtest "$line (run 'selftest' first: it makes the two logs)"; fi
}

check_pinned() {
  # DM-Deadbolt, first PlayerStart, picture 0 with post-processing off, picture 1 with it on.
  both pinned-deadbolt 100 34 DM-Deadbolt -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44,56 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:PostProcessToggleTimes=48
  # The desktop once more without texture LOD bias: OpenGL ES has none on the fixed-function path, so this
  # is the picture the twin is expected to equal (web-renderer.md, "Texture LOD bias").
  UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $D pinned-deadbolt-nb 75 34 DM-Deadbolt -opengl -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:NoLODBias=True -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44 > "$LD/pinned-deadbolt-nb.report" 2>&1
  local a b c ok=1 e
  # Static part of this view: x >= 760 (left of it: the pawn, a pickup, the HUD, which animate).
  a=$("$imgdiff" "$LD/pinned-deadbolt-0.png" "$LT/pinned-deadbolt-0.png" --box 760,0,1280,720 --side "$LT/pinned-deadbolt-off-side.png" --diff "$LT/pinned-deadbolt-off-diff.png")
  b=$("$imgdiff" "$LD/pinned-deadbolt-nb-0.png" "$LT/pinned-deadbolt-0.png" --box 760,0,1280,720 --diff "$LT/pinned-deadbolt-off-nolodbias-diff.png")
  c=$("$imgdiff" "$LD/pinned-deadbolt-1.png" "$LT/pinned-deadbolt-1.png" --box 760,0,1280,720 --side "$LT/pinned-deadbolt-on-side.png" --diff "$LT/pinned-deadbolt-on-diff.png")
  e=$(glerr "$LT/pinned-deadbolt.out")
  echo "INFO m11-pinned deadbolt, post-processing off, static part, desktop vs twin: $a" | tee -a "$summary"
  echo "INFO m11-pinned deadbolt, post-processing off, static part, desktop without LOD bias vs twin: $b" | tee -a "$summary"
  echo "INFO m11-pinned deadbolt, post-processing on, static part, desktop vs twin: $c" | tee -a "$summary"
  # Thresholds (static part): against the desktop without LOD bias no pixel may be off by more than 32
  # levels and at most 0.5% by more than 8 (measured 0.20%: the unexplained residue, see the notebook);
  # with the bias and with post-processing on (glow spreads every difference) the mean stays below 3.
  le "$(num above32 "$b")" 0 && le "$(num above8 "$b")" 0.5 && le "$(num mean "$b")" 0.8 || ok=0
  le "$(num mean "$a")" 3 && le "$(num mean "$c")" 3 || ok=0
  [ "$e" = 0 ] && [ "$(crit "$LT/pinned-deadbolt.out")" = 0 ] || ok=0
  [ $ok = 1 ] && result PASS pinned "deadbolt: off, no LOD bias: $b; twin GL errors ${e:-?}" || result FAIL pinned "deadbolt: off '$a' / no LOD bias '$b' / on '$c'; twin GL errors ${e:-?}"
}

check_effects() {
  local names="bloom blur berserk liandri pause radial dyngamma dof-alpha azure octane decay undertow recoil praxis-panes"
  if [ -n "${M11_SERIAL:-}" ]; then
    "$repo/tools/dev/m7-effects.sh" $D $names > "$LD/fx-lines.txt" 2>&1
  else
    ( "$repo/tools/dev/m7-effects.sh" $D $names > "$LD/fx-lines.txt" 2>&1 ) &
  fi
  local pid=${!:-}
  rm -f "$LT"/fx-*-b.ref.png
  UC2_GLARGS=-gl4es "$repo/tools/dev/m7-effects.sh" $T $names > "$LT/fx-lines.txt" 2>&1
  [ -z "$pid" ] || wait $pid
  local n line dline st ok fails=0
  for n in $names; do
    line=$(grep -a "^fx $n:" "$LT/fx-lines.txt"); dline=$(grep -a "^fx $n:" "$LD/fx-lines.txt")
    ok=1; st=""
    [[ "$line" == *"critical=0 glerrors=0 programs_failed=0+0"* ]] && [[ "$dline" == *"critical=0 glerrors=0 programs_failed=0+0"* ]] || ok=0
    if [ -f "$LD/fx-$n-b.png" ] && [ -f "$LT/fx-$n-b.png" ]; then
      cp -f "$LD/fx-$n-b.png" "$LT/fx-$n-b.ref.png"
      st=$("$imgdiff" "$LD/fx-$n-b.png" "$LT/fx-$n-b.png" --side "$LT/fx-$n-b-desktop-twin.png" --diff "$LT/fx-$n-b-desktop-twin-diff.png")
      # Threshold: mean difference of the whole B picture to the desktop's below 6 of 255. Two desktop runs
      # of the same effect differ by up to about 2 (animated pawn, pickups, particles, accumulated glow);
      # a wrong effect differs by 8 to 80 (the A/B differences in docs/porting/analysis/postprocess.md).
      le "$(num mean "$st")" 6 || ok=0
    else
      ok=0; st="no pair of B pictures"
    fi
    [ $ok = 1 ] || fails=$((fails+1))
    echo "$([ $ok = 1 ] && echo PASS || echo FAIL) m11-effect-$n: B desktop vs twin $st | twin: ${line#fx $n: } | desktop: $(sed -E 's/.*(critical=.*)/\1/' <<<"$dline")" | cut -c1-900 | tee -a "$summary"
  done
  [ $fails = 0 ] && result PASS effects "14 effects, every B picture within the threshold of the desktop's, critical=0 glerrors=0 programs_failed=0 on both" || result FAIL effects "$fails of 14 effects failed (lines above)"
}

check_m6() {
  # M6's pictures: picture 0 pixel shaders on, picture 1 off (docs/porting/analysis/shaders.md, S4), no post-processing.
  local fails=0 spec name map secs fire extra a b
  for spec in "deadbolt|DM-Deadbolt|100|34|" "octane|CTF-Octane|125|38|" "axon|DM-AxonCompressor|110|38|" "dualdiffuse|DM-AxonCompressor|110|38|-ini:System:[UC2GLDrv.UC2GLRenderDevice]:DualDiffuse=12"; do
    IFS='|' read -r name map secs fire extra <<<"$spec"
    both m6-$name "$secs" "$fire" "$map" -ini:System:[UC2GLDrv.UC2GLRenderDevice]:UsePostProcess=False -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:UsePostProcess=False -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=$((fire+10)),$((fire+20)) -ini:System:[UC2GLDrv.UC2GLRenderDevice]:PixelShaderToggleTimes=$((fire+15)) $extra
    a=$("$imgdiff" "$LD/m6-$name-0.png" "$LT/m6-$name-0.png" --side "$LT/m6-$name-on-side.png" --diff "$LT/m6-$name-on-diff.png" 2>&1)
    b=$("$imgdiff" "$LD/m6-$name-1.png" "$LT/m6-$name-1.png" 2>&1)
    # Threshold: whole frame (animated parts included), mean below 6, as for the effects.
    if le "$(num mean "$a")" 6 && le "$(num mean "$b")" 6 && [ "$(glerr "$LT/m6-$name.out")" = 0 ] && [ "$(crit "$LT/m6-$name.out")" = 0 ] \
       && grep -a "UC2GLDrv: programs:" "$LT/m6-$name.out" | tail -1 | grep -a -q "linked, 0 failed; fragment shaders [0-9]* compiled, 0 failed"; then
      echo "PASS m11-m6-$name: pixel shaders on: $a | off: $b | twin: $(grep -a -o 'programs: [0-9]* linked, [0-9]* failed' "$LT/m6-$name.out" | tail -1)" | tee -a "$summary"
    else
      fails=$((fails+1)); echo "FAIL m11-m6-$name: on '$a' off '$b' twin GL errors $(glerr "$LT/m6-$name.out")" | tee -a "$summary"
    fi
  done
  [ $fails = 0 ] && result PASS m6 "4 views" || result FAIL m6 "$fails of 4 views failed"
}

check_fallbacks() {
  # Reference for each mask: the same binding with the capability, same pinned view, so the comparison
  # isolates the fallback.
  if [ -n "${M11_SERIAL:-}" ]; then
    UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $T fb-ref 110 34 DM-Deadbolt -gl4es -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=50:POSTSELFTEST@100:exit > "$LT/fb-ref.report" 2>&1
  else
    UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $T fb-ref 110 34 DM-Deadbolt -gl4es -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=50:POSTSELFTEST@100:exit > "$LT/fb-ref.report" 2>&1 &
  fi
  local pid=${!:-}
  UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $D fb-ref 110 34 DM-Deadbolt -opengl -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=50:POSTSELFTEST@100:exit > "$LD/fb-ref.report" 2>&1
  [ -z "$pid" ] || wait $pid
  local spec sw tree arg fails=0 st s e L
  for spec in "glnos3tc|$T|-gl4es" "glnomaxlevel|$D|-opengl" "glnofragdepth|$T|-gl4es" "glnouintindex|$T|-gl4es"; do
    IFS='|' read -r sw tree arg <<<"$spec"; L=$work/logs/$tree
    UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $tree fb-$sw 110 34 DM-Deadbolt $arg -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=${sw#glno} -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=50:POSTSELFTEST@100:exit > "$L/fb-$sw.report" 2>&1
    st=$("$imgdiff" "$L/fb-ref-0.png" "$L/fb-$sw-0.png" --box 760,0,1280,720 --diff "$L/fb-$sw-diff.png" 2>&1)
    s=$(grep -a -o "selftest summary: [0-9]* passed, [0-9]* failed" "$L/fb-$sw.out"); e=$(glerr "$L/fb-$sw.out")
    # Threshold: the static part within a mean of 1 of 255 of the same binding with the capability (another
    # DXT decoder rounds differently; completed mip levels are only seen at extreme minification).
    # The self-test: every check passes (-ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=fragdepth replaces depth-copy by the check of its fallback,
    # -ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=uintindex adds index-narrow: 15 or 16 checks with pp-scale's four).
    if le "$(num mean "$st")" 1 && [[ "$s" =~ ^selftest\ summary:\ 1[56]\ passed,\ 0\ failed$ ]] && [ "$e" = 0 ] && [ "$(crit "$L/fb-$sw.out")" = 0 ]; then
      echo "PASS m11-fallback-$sw: $(grep -a -o 'capabilities: .*' "$L/fb-$sw.out" | cut -c1-220); static part vs the same binding with the capability: $st; $s; GL errors $e" | tee -a "$summary"
    else
      fails=$((fails+1)); echo "FAIL m11-fallback-$sw: picture '$st' selftest '${s:-none}' GL errors '${e:-?}' critical $(crit "$L/fb-$sw.out")" | tee -a "$summary"
    fi
  done
  # The mask whose fallback is not implemented: run, so that what happens is on record.
  for sw in glnodepthtexture; do
    UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $T fb-$sw 110 34 DM-Deadbolt -gl4es -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[UC2GLDrv.UC2GLRenderDevice]:MaskCaps=${sw#glno} -ini:System:[UC2GLDrv.UC2GLRenderDevice]:ShotTimes=44 -ini:System:[UC2GLDrv.UC2GLRenderDevice]:Exec=50:POSTSELFTEST@100:exit > "$LT/fb-$sw.report" 2>&1
    st=$("$imgdiff" "$LT/fb-ref-0.png" "$LT/fb-$sw-0.png" --box 760,0,1280,720 --diff "$LT/fb-$sw-diff.png" 2>&1)
    s=$(grep -a -o "selftest summary: [0-9]* passed, [0-9]* failed" "$LT/fb-$sw.out"); e=$(glerr "$LT/fb-$sw.out")
    fails=$((fails+1))
    echo "FAIL m11-fallback-$sw: FALLBACK NOT IMPLEMENTED. With the capability masked: critical $(crit "$LT/fb-$sw.out"), GL errors ${e:-?}, ${s:-no selftest summary}, static part vs the twin with the capability: $st; $(grep -a -o 'UC2GLDrv: no [a-zA-Z_ ]* on this context: [^(]*' "$LT/fb-$sw.out" | head -2 | tr '\n' ' ')" | tee -a "$summary"
  done
  result FAIL fallbacks "4 of 5 masks have a working fallback (S3TC, texture max level, gl_FragDepth, 32-bit indices); depth textures do not ($fails failing lines)"
}

check_movies() {
  local line; UC2_GLARGS=-gl4es python3 "$repo/tools/dev/m9-movies.py" --tree $T --no-mkrun --only logos,logos-skip,menu-movie > "$LT/m9-lines.txt" 2>&1
  sed 's/^/     /' "$LT/m9-lines.txt" | cut -c1-600 | tee -a "$summary"
  line=$(tail -1 "$LT/m9-lines.txt")
  if [[ "$line" == "m9-movies: 3 checks, 0 failed"* ]] || { [[ "$line" == *", 0 failed"* ]] && ! grep -q "^FAIL" "$LT/m9-lines.txt"; }; then result PASS movies "$line"; else result FAIL movies "$line"; fi
}

for c in $want; do
  case " $ALL " in *" $c "*) check_$c;; *) echo "unknown check: $c (try: $0 list)";; esac
done
left=$(( $(pgrep -f "$work/run/$D/" | wc -l) + $(pgrep -f "$work/run/$T/" | wc -l) ))
[ "$left" = 0 ] && result PASS leftovers "nothing of run/$D, run/$T is running" || result FAIL leftovers "$left processes of run/$D, run/$T still running"
echo "m11-twin: $(grep -c '^PASS' "$summary") PASS, $(grep -c '^FAIL' "$summary") FAIL lines ($summary)"
grep -q '^FAIL' "$summary" && exit 1 || exit 0
