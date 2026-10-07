#!/usr/bin/env bash
# O-9: fixed-function materials on NVIDIA as on Mesa and as Epic's D3D path specifies. One PASS/FAIL line per
# check; notebook docs/porting/analysis/o9-nvidia.md.
#
#   tools/dev/o9-nvidia.sh [check ...]          default: every check, in this order
#   tools/dev/o9-nvidia.sh list
#
#   combiner-mesa   -glcombinertest on Mesa llvmpipe (Xvfb): fragment programs; the texture environment with
#                   combine3; with combine4 only (-glnocombine3); and "before" = main's NVIDIA path emulated
#                   (texture environment, no combine3/combine4, 4 units): the three-operand cases must fail
#                   there and only there
#   combiner-gpu    the same on this machine's NVIDIA GPU, headless (SDL offscreen video driver, EGL; nothing on
#                   the display): programs; texture environment (combine4); "before" (-glnocombine4: main's path)
#   combiner-twin   the same test on the GL4ES / OpenGL ES 2 twin (the browser's code path; gl4es emulates the
#                   texture environment, combine3 included, in its own shaders)
#   caps            the material compiler gets the same stage count and the same texture-op caps on NVIDIA and Mesa
#   pictures-other  DM-AcidRain and DM-Tempest from a PlayerStart (tools/dev/o9/acidrain.scen, tempest.scen):
#                   views where a fifth of the drawn materials depend on the three-operand caps (-glmatcensus);
#                   whole frame without the pawn, NVIDIA before/after against Mesa after
#   pictures        DM-Azure, wooden pillar and cobra statue (tools/dev/o9/azure.scen through fx-owner.sh, time
#                   frozen, -glnoaa): NVIDIA before (main's binary) and after, Mesa before and after; crops of the
#                   shots without post-processing compared (the dynamic gamma depends on the frame rate)
#
# Run tree <WorkUC2>/run/o9 (made from build-native unless O9_NO_MKRUN=1; main's executable must be there as
# System/uc2-before.bin for the "before" pictures: build main and copy its uc2.bin). Logs and pictures in
# <WorkUC2>/logs/o9. Every run inside <WorkUC2>/bin/capped 2G; GPU runs one at a time; nothing left running.
set -u
export MESA_GLES_VERSION_OVERRIDE=${MESA_GLES_VERSION_OVERRIDE:-2.0}   # the twin: a strict ES 2.0 context (m11-twin.sh)
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
capped=$work/bin/capped
imgdiff=$repo/tools/dev/imgdiff.py
T=${O9_TREE:-o9}; sys=$work/run/$T/System; out=$work/logs/$T   # O9_TREE: another run tree / log folder (M7b)
ALL="combiner-mesa combiner-gpu combiner-twin caps pictures pictures-other"
if [ "${1:-}" = list ]; then sed -n '/^#   combiner-mesa/,/^#                   frozen/p' "$0"; exit 0; fi
want=${*:-$ALL}
mkdir -p "$out"
summary=$out/o9-nvidia-summary.txt; : > "$summary"
result() { echo "$1 o9-$2: $3" | tee -a "$summary"; }
info() { echo "INFO o9-$1: $2" | tee -a "$summary"; }

if [ -z "${O9_NO_MKRUN:-}" ]; then
  before=""; [ -f "$sys/uc2-before.bin" ] && before=$(mktemp) && cp "$sys/uc2-before.bin" "$before"
  "$capped" 2G 600 -- "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" $T > /dev/null || exit 1
  [ -n "$before" ] && cp "$before" "$sys/uc2-before.bin" && chmod 755 "$sys/uc2-before.bin" && rm -f "$before"
fi
cp -f "$repo"/Engine/Config/*.ini "$sys/"

# The three-operand cases of the test: what a texture environment without combine3/combine4 cannot do.
THREE="addsmooth blendtexturealphapm chain-3 modulatealpha_addcolor modulatealpha_addcolor-2 modulatecolor_addalpha modulateinvalpha_addcolor modulateinvcolor_addalpha multiplyadd-alpha multiplyadd-diffuse multiplyadd-tfactor"

# One run of the combiner test from the menu. ctrun <name> <mesa|gpu|twin> <args...>
ctrun() {
  local name=$1 kind=$2; shift 2
  local env="SDL_AUDIODRIVER=dummy"
  [ $kind = gpu ] && env="$env SDL_VIDEODRIVER=offscreen"
  [ $kind = twin ] && set -- -gl4es "$@"
  "$capped" 2G 180 -- xvfb-run -a -s "-screen 0 1280x720x24" bash -c \
    "cd '$sys' && $env ./uc2.bin -log -nostartupmovies -glnoaa -glerrors -glcombinertest -glexec=50:exit $* > '$out/ct-$name.out' 2>&1"
}
# Judge one run: expect "all" (0 failed) or "three" (exactly the three-operand cases fail). ctcheck <check> <name> <expect> <what>
ctcheck() {
  local check=$1 name=$2 expect=$3 what=$4 o=$out/ct-$2.out s fails e
  s=$(grep -a -o "combinertest summary: .*" "$o" | head -1)
  fails=$(grep -a "UC2GLDrv: combinertest [a-z0-9_-]*: FAIL" "$o" | sed -E 's/.*combinertest ([a-z0-9_-]+): FAIL.*/\1/' | sort | tr '\n' ' ' | sed 's/ $//')
  e=$(grep -a -o "[0-9]* GL errors in [0-9]* frames" "$o" | tail -1)
  local worst; worst=$(grep -a "UC2GLDrv: combinertest [a-z0-9_-]*: PASS" "$o" | sed -E 's/.*largest error ([0-9.]+) .*/\1/' | sort -g | tail -1)
  local ok=0
  if [ "$expect" = all ]; then
    [[ "$s" == *" 0 failed"* ]] && [[ "$e" == "0 GL errors"* ]] && ok=1
  else
    [ "$fails" = "$(tr ' ' '\n' <<<"$THREE" | sort | tr '\n' ' ' | sed 's/ $//')" ] && [[ "$e" == "0 GL errors"* ]] && ok=1
  fi
  local txt="$what: ${s:-no summary}; largest error of the passing cases ${worst:-?}/255; ${e:-no GL error count}"
  [ "$expect" = three ] && txt="$txt; failing: ${fails:-none}"
  [ $ok = 1 ] && result PASS "$check" "$txt" || result FAIL "$check" "$txt (log $o)"
}

check_combiner_mesa() {
  ctrun mesa-prog mesa
  ctcheck combiner-mesa-programs mesa-prog all "Mesa, fragment programs"
  ctrun mesa-texenv mesa -glnoffprograms
  ctcheck combiner-mesa-texenv-combine3 mesa-texenv all "Mesa, texture environment with combine3"
  ctrun mesa-combine4 mesa -glnoffprograms -glnocombine3
  ctcheck combiner-mesa-texenv-combine4 mesa-combine4 all "Mesa, texture environment with combine4 only (-glnocombine3)"
  ctrun mesa-before mesa -glnoffprograms -glnocombine3 -glnocombine4 -glunits=4
  ctcheck combiner-mesa-before mesa-before three "Mesa as main's NVIDIA path (texture environment, no combine3/combine4, 4 units): the defect reproduced"
}

gpu_busy() { pgrep -a -x uc2.bin | while read -r p rest; do [ -n "$(tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -a '^SDL_VIDEODRIVER=offscreen')" ] && echo $p; done; }

check_combiner_gpu() {
  if [ -n "$(gpu_busy)" ]; then result FAIL combiner-gpu "another headless GPU run of uc2.bin is going on; not started (one GPU run at a time)"; return; fi
  ctrun gpu-prog gpu
  ctcheck combiner-gpu-programs gpu-prog all "NVIDIA $(grep -a -o 'GL_RENDERER *: .*' "$out/ct-gpu-prog.out" | sed 's/GL_RENDERER *: //'), fragment programs"
  ctrun gpu-texenv gpu -glnoffprograms
  ctcheck combiner-gpu-texenv-combine4 gpu-texenv all "NVIDIA, texture environment with combine4 (4 units: the 8-stage and stage-5 cases are skipped)"
  ctrun gpu-before gpu -glnoffprograms -glnocombine4
  ctcheck combiner-gpu-before gpu-before three "NVIDIA, main's path (texture environment without combine4): the defect reproduced"
}

check_combiner_twin() {
  ctrun twin twin
  ctcheck combiner-twin twin all "GL4ES twin ($(grep -a -o 'GLES hardware: [^/]*' "$out/ct-twin.out" | head -1)), texture environment emulated by gl4es (LOD bias cases skipped: no LOD bias on OpenGL ES)"
}

check_caps() {
  local m g
  m=$(grep -a -o "fixed-function materials: .*" "$out/ct-mesa-prog.out" 2>/dev/null | head -1)
  g=$(grep -a -o "fixed-function materials: .*" "$out/ct-gpu-prog.out" 2>/dev/null | head -1)
  local ms gs; ms=$(grep -o "[0-9]* texture stages" <<<"$m"); gs=$(grep -o "[0-9]* texture stages" <<<"$g")
  if [ -n "$m" ] && [ "$ms" = "$gs" ] && [[ "$m" == *"operations exact"* ]] && [[ "$g" == *"operations exact"* ]]; then
    result PASS caps "the material compiler gets $ms and exact three-operand operations on both: Mesa '$m' | NVIDIA '$g'"
  else
    result FAIL caps "Mesa '$m' | NVIDIA '$g' (run combiner-mesa and combiner-gpu first)"
  fi
}

# Crops of the DM-Azure pictures (1280x720), third-person view (shots "pp", "nopp", "texenv"): the wooden pillar
# right of the cobra niche, and the right half of the cobra statue (its left half is behind the waterfall). The
# close first-person view of the cobra (shots "cobra*") is for people and for the same-run comparison only: its
# eye height settles differently at the GPU's and at llvmpipe's frame rate (a few pixels), the third-person
# camera does not.
PILLAR=730,0,920,480; COBRA=370,290,460,540; COBRACLOSE=600,170,790,540
check_pictures() {
  if [ ! -f "$sys/uc2-before.bin" ]; then result FAIL pictures "no $sys/uc2-before.bin (main's executable)"; return; fi
  local b
  # (After first: fx-owner.sh clears fx-azure-*.png, which includes the "-before" pictures.)
  for b in uc2.bin uc2-before.bin; do
    FX_ARGS=-glnoaa UC2_BIN=$b FX_SCENARIO="$repo/tools/dev/o9/azure.scen" FX_MAP=DM-Azure FX_NAME=azure "$repo/tools/dev/fx-owner.sh" $T custom | tee -a "$out/pictures-runs.txt"
    if [ -n "$(gpu_busy)" ]; then result FAIL pictures "another headless GPU run is going on"; return; fi
    FX_ARGS=-glnoaa UC2_GPU=1 UC2_BIN=$b FX_SCENARIO="$repo/tools/dev/o9/azure.scen" FX_MAP=DM-Azure FX_NAME=azure "$repo/tools/dev/fx-owner.sh" $T custom | tee -a "$out/pictures-runs.txt"
  done
  local p=$out/fx-azure ok=1 shot
  for f in "$p-pp.png" "$p-before-gpu-pp.png" "$p-gpu-pp.png" "$p-nopp.png" "$p-before-nopp.png" "$p-gpu-nopp.png" "$p-before-gpu-nopp.png" "$p-gpu-texenv.png" \
           "$p-cobra.png" "$p-gpu-cobra.png" "$p-before-gpu-cobra.png" "$p-cobra-nopp.png" "$p-gpu-cobra-nopp.png" "$p-before-gpu-cobra-nopp.png" "$p-gpu-cobra-texenv.png"; do
    [ -f "$f" ] || { result FAIL pictures "missing $f (see $out/pictures-runs.txt)"; return; }
  done
  # Numbers from the shots without post-processing (the dynamic gamma ramp adapts with the frame rate, which is
  # very different on the GPU and on llvmpipe); Mesa (after) is the reference: what Mesa drew before O-9 for
  # these materials (combine3, 2-3 stages).
  local after before mesa gpuenv cafter cbefore cgpuenv
  after=$("$imgdiff" "$p-gpu-nopp.png" "$p-nopp.png" --box $PILLAR)
  before=$("$imgdiff" "$p-before-gpu-nopp.png" "$p-nopp.png" --box $PILLAR)
  mesa=$("$imgdiff" "$p-before-nopp.png" "$p-nopp.png" --box $PILLAR)
  gpuenv=$("$imgdiff" "$p-gpu-texenv.png" "$p-gpu-nopp.png" --box $PILLAR)
  cafter=$("$imgdiff" "$p-gpu-nopp.png" "$p-nopp.png" --box $COBRA)
  cbefore=$("$imgdiff" "$p-before-gpu-nopp.png" "$p-nopp.png" --box $COBRA)
  cgpuenv=$("$imgdiff" "$p-gpu-cobra-texenv.png" "$p-gpu-cobra-nopp.png" --box $COBRACLOSE)
  info pictures-pillar "NVIDIA after vs Mesa after: $after"
  info pictures-pillar "NVIDIA before vs Mesa after: $before"
  info pictures-pillar "Mesa before vs Mesa after: $mesa"
  info pictures-pillar "NVIDIA, texture environment (combine4) vs fragment programs: $gpuenv"
  info pictures-cobra "NVIDIA after vs Mesa after: $cafter"
  info pictures-cobra "NVIDIA before vs Mesa after: $cbefore"
  info pictures-cobra "NVIDIA, texture environment (combine4) vs fragment programs (close view, same run): $cgpuenv"
  mean() { sed -E 's/.*mean=([0-9.]+).*/\1/' <<<"$1"; }
  awk -v a="$(mean "$after")" -v b="$(mean "$before")" 'BEGIN{exit !(a < 3 && b > 5*a)}' || ok=0
  awk -v a="$(mean "$cafter")" -v b="$(mean "$cbefore")" 'BEGIN{exit !(a < 3 && b > 2*a)}' || ok=0
  awk -v a="$(mean "$mesa")" -v c="$(mean "$gpuenv")" -v d="$(mean "$cgpuenv")" 'BEGIN{exit !(a < 1 && c < 1 && d < 1)}' || ok=0
  "$imgdiff" "$p-before-gpu-pp.png" "$p-gpu-pp.png" --side "$out/o9-azure-nvidia-before-after.png" > /dev/null
  "$imgdiff" "$p-gpu-pp.png" "$p-pp.png" --side "$out/o9-azure-nvidia-vs-mesa.png" > /dev/null
  "$imgdiff" "$p-before-gpu-cobra.png" "$p-gpu-cobra.png" --side "$out/o9-azure-cobra-nvidia-before-after.png" > /dev/null
  local txt="mean difference to Mesa (of 255), pillar: NVIDIA before $(mean "$before"), after $(mean "$after"); cobra: before $(mean "$cbefore"), after $(mean "$cafter"); Mesa before/after (pillar) $(mean "$mesa"); NVIDIA combine4 vs programs: pillar $(mean "$gpuenv"), cobra $(mean "$cgpuenv"); side by side: o9-azure-nvidia-before-after.png, o9-azure-nvidia-vs-mesa.png, o9-azure-cobra-nvidia-before-after.png"
  [ $ok = 1 ] && result PASS pictures "$txt" || result FAIL pictures "$txt"
}

check_pictures_other() {
  local spec n map b ok=1 txt=""
  [ -f "$sys/uc2-before.bin" ] || { result FAIL pictures-other "no $sys/uc2-before.bin (main's executable)"; return; }
  for spec in acidrain:DM-AcidRain tempest:DM-Tempest; do
    n=${spec%%:*}; map=${spec#*:}
    for b in uc2.bin uc2-before.bin; do
      FX_ARGS="-glnoaa -glmatcensus" UC2_BIN=$b FX_SCENARIO="$repo/tools/dev/o9/$n.scen" FX_MAP=$map FX_NAME=$n "$repo/tools/dev/fx-owner.sh" $T custom | tee -a "$out/pictures-runs.txt"
      if [ -n "$(gpu_busy)" ]; then result FAIL pictures-other "another headless GPU run is going on"; return; fi
      FX_ARGS="-glnoaa -glmatcensus" UC2_GPU=1 UC2_BIN=$b FX_SCENARIO="$repo/tools/dev/o9/$n.scen" FX_MAP=$map FX_NAME=$n "$repo/tools/dev/fx-owner.sh" $T custom | tee -a "$out/pictures-runs.txt"
    done
    local p=$out/fx-$n f
    for f in "$p-nopp.png" "$p-gpu-nopp.png" "$p-before-gpu-nopp.png" "$p-before-nopp.png" "$p-gpu-texenv.png"; do
      [ -f "$f" ] || { result FAIL pictures-other "missing $f"; return; }
    done
    # The pawn (third person, animating) is masked; the rest of the frame is static (time frozen).
    local m="--mask 480,440,720,720" after before mesa env drawn dep
    after=$("$imgdiff" "$p-gpu-nopp.png" "$p-nopp.png" $m)
    before=$("$imgdiff" "$p-before-gpu-nopp.png" "$p-nopp.png" $m)
    mesa=$("$imgdiff" "$p-before-nopp.png" "$p-nopp.png" $m)
    env=$("$imgdiff" "$p-gpu-texenv.png" "$p-gpu-nopp.png" $m)
    drawn=$(grep -a "UC2GLDrv: census " "$p-gpu.out" | awk '{print $3}' | LC_ALL=C sort -u)
    dep=$(grep -a -h "UC2GLDrv: matcensus .*B!=C" "$work"/logs/o9c*/census-$map.out 2>/dev/null | awk '{print $5}' | LC_ALL=C sort -u | LC_ALL=C comm -12 - <(echo "$drawn") | wc -l)
    info pictures-$n "$map: $(echo "$drawn" | wc -l) materials drawn, $dep of them compile differently without the three-operand caps (census of tools/dev/o9-census.sh)"
    info pictures-$n "$map NVIDIA after vs Mesa after: $after"
    info pictures-$n "$map NVIDIA before vs Mesa after: $before"
    info pictures-$n "$map Mesa before vs Mesa after: $mesa"
    info pictures-$n "$map NVIDIA texture environment (combine4) vs fragment programs: $env"
    "$imgdiff" "$p-before-gpu-pp.png" "$p-gpu-pp.png" --side "$out/o9-$n-nvidia-before-after.png" > /dev/null
    "$imgdiff" "$p-gpu-pp.png" "$p-pp.png" --side "$out/o9-$n-nvidia-vs-mesa.png" > /dev/null
    local ma mb; ma=$(sed -E 's/.*mean=([0-9.]+).*/\1/' <<<"$after"); mb=$(sed -E 's/.*mean=([0-9.]+).*/\1/' <<<"$before")
    awk -v a="$ma" -v b="$mb" 'BEGIN{exit !(a < 3 && b > a)}' || ok=0
    txt="$txt $map: NVIDIA vs Mesa before $mb, after $ma;"
  done
  [ $ok = 1 ] && result PASS pictures-other "${txt# } pictures o9-acidrain-*.png, o9-tempest-*.png" || result FAIL pictures-other "${txt# }"
}

for c in $want; do
  case $c in
    combiner-mesa) check_combiner_mesa ;;
    combiner-gpu)  check_combiner_gpu ;;
    combiner-twin) check_combiner_twin ;;
    caps)          check_caps ;;
    pictures)      check_pictures ;;
    pictures-other) check_pictures_other ;;
    *) echo "unknown check $c"; exit 2 ;;
  esac
done
left=0
for p in $(pgrep -x uc2.bin) $(pgrep -x uc2-before.bin); do
  case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"|"$sys"/*) left=$((left+1));; esac
done
[ $left = 0 ] && result PASS leftovers "nothing of run/$T is running" || result FAIL leftovers "$left processes of run/$T still running"
echo "o9-nvidia: $(grep -c '^PASS' "$summary") PASS, $(grep -c '^FAIL' "$summary") FAIL lines ($summary)"
