#!/usr/bin/env bash
# uc-patches-proof.sh: the bytecode proofs of the script patch pipeline (T1; docs/porting/analysis/
# script-patches.md, step 3). Each proof prints one PASS/FAIL line; the exit status is the number of FAILs.
#
#   tools/scripts/uc-patches-proof.sh [p1] [p2] [p3] [options]     (no proof named: all three)
#     --work <dir>   scratch tree (default <WorkUC2>/run/t1-proof); everything is written below it
#     --ref <dir>    P2's reference: the System folder of a build of the leak's sources with our changes
#                    (`tools/dev/script-build.sh --tree t1-ref`, default <WorkUC2>/run/t1-ref/System)
#     --shipped <dir> the shipped .u (default <WorkUC2>/epic-sources/System); other options go to uc-build.sh
#     --engine-textures <dir>  P1 only, dev only: Epic's Engine source folder (default <WorkUC2>/epic-sources/
#                    Engine). Engine.u holds 68 textures that `#exec TEXTURE IMPORT` made from image files the disc
#                    does not have and the decompiler cannot recreate; with this folder P1 puts those lines (from
#                    its Classes/*.uc) back before `defaultproperties` and links its Textures/, so the rest of
#                    Engine can be proved. Without it (or `none`) P1 leaves Engine out.
#
#   P1  base + epic-fixes, compiled and stripped, == the shipped .u + the load-time patcher (ScriptPatches.h), for
#       every package the patch table touches: tools/dev/script-diff.py against the shipped files with the patch
#       table, `problems 0` on each package line, and its patch table (every patched function's bytecode against
#       the patcher's).
#   P2  base + the default sets, compiled and stripped, == the reference build for the packages the default build
#       rebuilds: uc-patches.py compare (bytecode of every function and state, every default, every declaration;
#       names, imports and exports as sets).
#   P3  determinism: decompile twice (two Python hash seeds), apply twice, build twice: identical bytes.
set -uo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
W=$work/run/t1-proof
ref=$work/run/t1-ref/System
shipped=$work/epic-sources/System
content=$work/assets/uc2
capped=$work/bin/capped
SP=$repo/Source/Engine/Inc/ScriptPatches.h
engine_tex=$work/epic-sources/Engine
proofs=() build_args=()
while [ $# -gt 0 ]; do
  case "$1" in
    p1|p2|p3) proofs+=("$1"); shift;;
    --work) W=$2; shift 2;;
    --ref) ref=$2; shift 2;;
    --engine-textures) engine_tex=$2; shift 2;;
    --shipped) shipped=$2; build_args+=(--shipped "$2"); shift 2;;
    --assets) content=$2; build_args+=(--assets "$2"); shift 2;;
    --ucc|--retail-inis|--mem|--capped) build_args+=("$1" "$2"); shift 2;;
    *) echo "unknown argument $1" >&2; exit 2;;
  esac
done
[ ${#proofs[@]} -eq 0 ] && proofs=(p1 p2 p3)
mkdir -p "$W"
fails=0
py() { PYTHONHASHSEED=${SEED:-0} "$capped" 2G 1800 -- python3 "$@"; }
dec() { py "$repo/tools/scripts/uc-decompile.py" "$@" --content "$content"; }
up() { py "$repo/tools/scripts/uc-patches.py" "$@"; }
# base <dir> <Pkg...>: the base tree of these packages
base() {
  local d=$1; shift
  rm -rf "$d"
  for p in "$@"; do
    if [ "$p" = UCMenu ]; then dec "$shipped/$p.u" --out "$d/$p/Classes" --shipped-text
    else dec "$shipped/$p.u" --out "$d/$p/Classes"; fi || return 1
  done
}
# build <tree> <out> <targets,...> [uc-build options]: compiles the targets and what must be compiled first
build() {
  local tree=$1 out=$2 pk=$3; shift 3
  local all; all=$(up packages --sets "$CUR_SETS" ${WITH_EPIC:+--with-epic-only} --compile-list "$shipped" | awk '{print $1}' | paste -sd,)
  "$repo/tools/scripts/uc-build.sh" --classes "$tree" --out "$out" --packages "$all" --keep "$pk" --shipped "$shipped" "${build_args[@]}" "$@"
}
# compile_list <sets> [--with-epic-only]: every package to decompile (targets and the ones compiled first)
compile_list() { up packages --sets "$1" ${2:-} --compile-list "$shipped" | awk '{print $1}'; }
t() { local t0=$SECONDS; "$@"; local rc=$?; echo "  ($(( SECONDS - t0 )) s: $*)" | cut -c1-200 >> "$W/times.txt"; return $rc; }
: > "$W/times.txt"

for pr in "${proofs[@]}"; do
  case $pr in
  p1)
    CUR_SETS=epic-fixes; WITH_EPIC=1
    mapfile -t pk < <(up packages --sets epic-fixes --with-epic-only)
    mapfile -t dpk < <(compile_list epic-fixes --with-epic-only)
    links=()
    if [ "$engine_tex" = none ] || [ ! -d "$engine_tex/Classes" ]; then
      pk=("${pk[@]/Engine}"); mapfile -t pk < <(printf '%s\n' "${pk[@]}" | grep .)
      dpk=("${dpk[@]/Engine}"); mapfile -t dpk < <(printf '%s\n' "${dpk[@]}" | grep .)
      echo "P1: Engine left out (no --engine-textures folder: its #exec-imported textures cannot be recreated from the disc)"
    fi
    list=$(IFS=,; echo "${pk[*]}")
    echo "P1: packages ${list}"
    t base "$W/p1-base" "${dpk[@]}" > "$W/p1-decompile.txt" 2>&1 || { echo "FAIL P1: decompile (see $W/p1-decompile.txt)"; fails=$((fails+1)); continue; }
    t up apply --base "$W/p1-base" --sets epic-fixes --out "$W/p1-tree" || { echo "FAIL P1: apply"; fails=$((fails+1)); continue; }
    if [ -d "$W/p1-tree/Engine" ]; then
      # dev aid (see --engine-textures): Epic's texture imports back before defaultproperties (no line above moves)
      python3 - "$engine_tex/Classes" "$W/p1-tree/Engine/Classes" <<'EOP' || { echo "FAIL P1: Engine texture lines"; fails=$((fails+1)); continue; }
import os, re, sys
src, dst = sys.argv[1], sys.argv[2]
n = 0
for fn in sorted(os.listdir(src)):
    if not fn.endswith('.uc') or not os.path.exists(os.path.join(dst, fn)):
        continue
    ex = [l.rstrip(b'\r') for l in open(os.path.join(src, fn), 'rb').read().split(b'\n')
          if re.match(rb'\s*#exec\s+texture\s+import', l, re.I)]
    if ex:
        d = open(os.path.join(dst, fn), 'rb').read().split(b'\n')
        i = d.index(b'defaultproperties')
        d[i:i] = ex
        open(os.path.join(dst, fn), 'wb').write(b'\n'.join(d))
        n += len(ex)
print('P1: Engine: %d #exec TEXTURE IMPORT lines from %s (dev aid)' % (n, src))
EOP
      links=(--link "Engine/Textures=$engine_tex/Textures")
    fi
    t build "$W/p1-tree" "$W/p1" "$list" "${links[@]}" > "$W/p1-build.txt" 2>&1 || { echo "FAIL P1: build (see $W/p1-build.txt)"; tail -5 "$W/p1-build.txt"; fails=$((fails+1)); continue; }
    t py "$repo/tools/dev/script-diff.py" "$shipped" "$W/p1/System" --packages "$list" --patches "$SP" \
      --details "$W/p1-compare.txt" --patch-table "$W/p1-patches.txt" --functions "$W/p1-functions.txt" > "$W/p1-summary.txt"
    cat "$W/p1-summary.txt"
    echo "  patch table ($W/p1-patches.txt): $(grep -c 'identical' "$W/p1-patches.txt") identical, $(grep -vc 'identical' "$W/p1-patches.txt") other"
    grep -v 'identical' "$W/p1-patches.txt" | sed 's/^/    /'
    if grep -q 'problems  *[1-9]\|MISSING' "$W/p1-summary.txt"; then echo "FAIL P1"; fails=$((fails+1)); else echo "PASS P1: problems 0 in ${#pk[@]} packages"; fi
    ;;
  p2)
    CUR_SETS=$(up sets --default); WITH_EPIC=
    mapfile -t pk < <(up packages --default)
    mapfile -t dpk < <(up packages --default --compile-list "$shipped" | awk '{print $1}')
    list=$(IFS=,; echo "${pk[*]}")
    echo "P2: packages ${list} (reference $ref)"
    t base "$W/p2-base" "${dpk[@]}" > "$W/p2-decompile.txt" 2>&1 || { echo "FAIL P2: decompile"; fails=$((fails+1)); continue; }
    t up apply --base "$W/p2-base" --default --out "$W/p2-tree" || { echo "FAIL P2: apply"; fails=$((fails+1)); continue; }
    t build "$W/p2-tree" "$W/p2" "$list" > "$W/p2-build.txt" 2>&1 || { echo "FAIL P2: build (see $W/p2-build.txt)"; tail -5 "$W/p2-build.txt"; fails=$((fails+1)); continue; }
    bad=0
    for p in "${pk[@]}"; do
      t up compare "$W/p2/System/$p.u" "$ref/$p.u" --report "$W/p2-compare-$p.txt" | head -12 || bad=1
    done
    if [ $bad = 0 ]; then echo "PASS P2: ${#pk[@]} packages equal to the reference build"; else echo "FAIL P2"; fails=$((fails+1)); fi
    ;;
  p3)
    CUR_SETS=$(up sets --default); WITH_EPIC=
    mapfile -t pk < <(up packages --default)
    mapfile -t dpk < <(up packages --default --compile-list "$shipped" | awk '{print $1}')
    list=$(IFS=,; echo "${pk[*]}")
    ok=1
    SEED=1 base "$W/p3-base1" "${dpk[@]}" > /dev/null 2>&1 && SEED=2 base "$W/p3-base2" "${dpk[@]}" > /dev/null 2>&1 || ok=0
    diff -rq "$W/p3-base1" "$W/p3-base2" > "$W/p3-decompile.diff" && n1="decompile twice: identical ($(find "$W/p3-base1" -name '*.uc' | wc -l) files)" || { ok=0; n1="decompile twice: DIFFERENT ($W/p3-decompile.diff)"; }
    up apply --base "$W/p3-base1" --default --out "$W/p3-tree1" > /dev/null && up apply --base "$W/p3-base1" --default --out "$W/p3-tree2" > /dev/null || ok=0
    diff -rq "$W/p3-tree1" "$W/p3-tree2" > "$W/p3-apply.diff" && n2="apply twice: identical" || { ok=0; n2="apply twice: DIFFERENT"; }
    t build "$W/p3-tree1" "$W/p3-b1" "$list" > "$W/p3-build1.txt" 2>&1 && t build "$W/p3-tree2" "$W/p3-b2" "$list" > "$W/p3-build2.txt" 2>&1 || ok=0
    n3="build twice:"
    for p in "${pk[@]}"; do
      if cmp -s "$W/p3-b1/System/$p.u" "$W/p3-b2/System/$p.u"; then n3="$n3 $p.u identical ($(stat -c %s "$W/p3-b1/System/$p.u") bytes);"
      else ok=0; n3="$n3 $p.u DIFFERENT;"; fi
    done
    echo "  $n1"; echo "  $n2"; echo "  $n3"
    if [ $ok = 1 ]; then echo "PASS P3: decompile, apply and build are deterministic"; else echo "FAIL P3"; fails=$((fails+1)); fi
    ;;
  esac
done
echo "times: $W/times.txt"
exit $fails
