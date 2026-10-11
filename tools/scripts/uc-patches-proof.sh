#!/usr/bin/env bash
# uc-patches-proof.sh: the bytecode proofs of the script patch pipeline (T1; docs/porting/analysis/
# script-patches.md, step 3). Each proof prints one PASS/FAIL line; the exit status is the number of FAILs.
#
#   tools/scripts/uc-patches-proof.sh [p1] [p2] [p3] [options]     (no proof named: all three)
#     --work <dir>   scratch tree (default <WorkUC2>/run/t1-proof); everything is written below it
#     --reference <file>  P2's regression reference (default patches/reference.sha256)
#     --write-reference   P2 writes the reference from its build instead of checking it (after a set changed on
#                    purpose: commit the new file with the patch)
#     --ref <dir>    dev machine only: P2 also compares its build, bytecode by bytecode, with the System folder of
#                    another build (T1's reference, <WorkUC2>/run/t1-ref/System: `tools/dev/script-build.sh --tree
#                    t1-ref` of the leak's sources with our changes, before the sources left the repo; it matches
#                    the sets as they were at 4cbe12f)
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
#   P2  regression: base + the default sets, compiled and stripped, gives exactly the packages of
#       patches/reference.sha256 (sha256 and size of each package the default build rebuilds; the build is
#       deterministic, P3). Until T4 (2026-10-11) P2 compared with a build of the repo's script sources (Epic's leaked
#       text with our changes); those sources are gone, the patches are the only form of our changes, so the
#       reference is the packages they produced when P2 last passed that comparison. With --ref <dir> the old
#       comparison runs too: uc-patches.py compare (bytecode of every function and state, every default, every
#       declaration; names, imports and exports as sets).
#   P3  determinism: decompile twice (two Python hash seeds), apply twice, build twice: identical bytes.
#
#   tools/scripts/uc-patches-proof.sh rebase --old-decompiler <file> [--new-decompiler <file>] [--patches-out <dir>]
#       when uc-decompile.py changes: the sets (patches/) were made against the base of the OLD decompiler (e.g.
#       `git show <rev>:tools/scripts/uc-decompile.py > old.py`); for every set k the old base + sets 1..k is built
#       and decompiled with the NEW one (default: tools/scripts/uc-decompile.py), `uc-patches.py rebase` re-makes
#       each set against the new base into --patches-out (default: the repo's patches/), and the proof builds
#       new base + rebased sets and compares it with the old chain's build (uc-patches.py compare: equal).
set -uo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
W=$work/run/t1-proof
ref=""
reference=$repo/patches/reference.sha256
write_ref=0
shipped=$work/epic-sources/System
content=$work/assets/uc2
# WorkUnreal (the home since 2026-10-11): the retail disc's System/ holds the 17 shipped .u; the cooked tree the content.
[ -d "$work/assets/uc2-retail-assets/System" ] && shipped=$work/assets/uc2-retail-assets/System
[ -d "$work/assets/uc2-assets/System" ] && content=$work/assets/uc2-assets
capped=$work/bin/capped
[ -x "$capped" ] || capped=none
SP=$repo/Source/Engine/Inc/ScriptPatches.h
engine_tex=$work/epic-sources/Engine
proofs=() build_args=()
old_dec="" new_dec=$repo/tools/scripts/uc-decompile.py pout=$repo/patches
while [ $# -gt 0 ]; do
  case "$1" in
    p1|p2|p3|rebase) proofs+=("$1"); shift;;
    --old-decompiler) old_dec=$2; shift 2;;
    --new-decompiler) new_dec=$2; shift 2;;
    --patches-out) pout=$2; shift 2;;
    --work) W=$2; shift 2;;
    --ref) ref=$2; shift 2;;
    --reference) reference=$2; shift 2;;
    --write-reference) write_ref=1; shift;;
    --engine-textures) engine_tex=$2; shift 2;;
    --shipped) shipped=$2; build_args+=(--shipped "$2"); shift 2;;
    --assets) content=$2; build_args+=(--assets "$2"); shift 2;;
    --capped) capped=$2; build_args+=("$1" "$2"); shift 2;;
    --ucc|--retail-inis|--mem) build_args+=("$1" "$2"); shift 2;;
    *) echo "unknown argument $1" >&2; exit 2;;
  esac
done
[ ${#proofs[@]} -eq 0 ] && proofs=(p1 p2 p3)
mkdir -p "$W"
fails=0
py() { if [ "$capped" = none ]; then PYTHONHASHSEED=${SEED:-0} python3 "$@"; else PYTHONHASHSEED=${SEED:-0} "$capped" 2G 1800 -- python3 "$@"; fi; }
DEC=$repo/tools/scripts/uc-decompile.py
dec() { py "$DEC" "$@" --content "$content"; }
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
    echo "P2: packages ${list} (reference $reference${ref:+; and the build $ref})"
    t base "$W/p2-base" "${dpk[@]}" > "$W/p2-decompile.txt" 2>&1 || { echo "FAIL P2: decompile"; fails=$((fails+1)); continue; }
    t up apply --base "$W/p2-base" --default --out "$W/p2-tree" || { echo "FAIL P2: apply"; fails=$((fails+1)); continue; }
    t build "$W/p2-tree" "$W/p2" "$list" > "$W/p2-build.txt" 2>&1 || { echo "FAIL P2: build (see $W/p2-build.txt)"; tail -5 "$W/p2-build.txt"; fails=$((fails+1)); continue; }
    if [ $write_ref = 1 ]; then
      {
        echo "# patches/reference.sha256: the script packages the DEFAULT build makes (the sets marked on in sets.txt), from"
        echo "# the shipped .u (shipped.sha256) and the retail disc's per-class inis (UC2_SCRIPT_RETAIL_INIS_DIR), with this"
        echo "# repo's decompiler, patches and ucc. A regression reference (the build is deterministic): scripts-proof P2"
        echo "# (tools/scripts/uc-patches-proof.sh p2) builds base + default sets and checks every package against it;"
        echo "# \`sha256sum -c\` in build-native/Scripts/System checks a build by hand. When a set (or the decompiler, or"
        echo "# the compiler) changes on purpose, regenerate it and commit it with the change:"
        echo "#   tools/scripts/uc-patches-proof.sh p2 --write-reference      (patches/README.md, \"Changing a script\")"
        for p in "${pk[@]}"; do
          echo "# $p.u $(stat -c %s "$W/p2/System/$p.u") bytes"
          (cd "$W/p2/System" && sha256sum "$p.u")
        done
      } > "$reference.tmp" && mv -f "$reference.tmp" "$reference"
      echo "P2: wrote $reference ($(grep -c '^[0-9a-f]\{64\}  ' "$reference") packages)"
    fi
    bad=0
    declare -A rsha=() rsize=()
    if [ -f "$reference" ]; then
      while read -r a b c d; do
        case "$a" in
          '#') [ "$d" = bytes ] && rsize[$b]=$c;;
          [0-9a-f]*) [ ${#a} = 64 ] && rsha[$b]=$a;;
        esac
      done < "$reference"
    else
      echo "  no $reference"; bad=1
    fi
    for p in "${pk[@]}"; do
      f=$W/p2/System/$p.u
      sha=$(sha256sum "$f" | awk '{print $1}'); size=$(stat -c %s "$f")
      if [ "${rsha[$p.u]:-}" = "$sha" ] && [ "${rsize[$p.u]:-}" = "$size" ]; then
        echo "  $p.u: $size bytes, sha256 $sha: as the reference"
      else
        echo "  $p.u: $size bytes, sha256 $sha: DIFFERENT (reference: ${rsize[$p.u]:-no} bytes, ${rsha[$p.u]:-no sha256})"; bad=1
      fi
    done
    for k in "${!rsha[@]}"; do
      case ",$list," in *",${k%.u},"*) ;; *) echo "  $k: in the reference, not built by the default sets"; bad=1;; esac
    done
    unset rsha rsize
    if [ -n "$ref" ]; then
      for p in "${pk[@]}"; do
        t up compare "$W/p2/System/$p.u" "$ref/$p.u" --report "$W/p2-compare-$p.txt" | head -12 || bad=1
      done
    fi
    if [ $bad = 0 ]; then echo "PASS P2: ${#pk[@]} packages equal to the reference ($(basename "$reference")${ref:+ and the build $ref})"
    else echo "FAIL P2 (a set changed on purpose? regenerate: $0 p2 --write-reference)"; fails=$((fails+1)); fi
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
  rebase)
    [ -n "$old_dec" ] || { echo "FAIL rebase: --old-decompiler <file> is required"; fails=$((fails+1)); continue; }
    R=$W/rebase; rm -rf "$R"; mkdir -p "$R/stages"
    CUR_SETS=$(up sets --default); WITH_EPIC=
    mapfile -t sets < <(up sets --default | tr , '\n')
    mapfile -t pk < <(up packages --default)
    mapfile -t dpk < <(up packages --default --compile-list "$shipped" | awk '{print $1}')
    list=$(IFS=,; echo "${pk[*]}")
    DEC=$old_dec base "$R/old-base" "${dpk[@]}" > "$R/decompile.txt" 2>&1 || { echo "FAIL rebase: old decompile"; fails=$((fails+1)); continue; }
    DEC=$new_dec base "$R/new-base" "${dpk[@]}" >> "$R/decompile.txt" 2>&1 || { echo "FAIL rebase: new decompile"; fails=$((fails+1)); continue; }
    done_sets=""; ok=1
    for k in "${sets[@]}"; do
      done_sets=${done_sets:+$done_sets,}$k
      st=$R/stages/${k//\//_}
      up apply --base "$R/old-base" --sets "$done_sets" --out "$R/tree-$k" > /dev/null || { ok=0; break; }
      build "$R/tree-$k" "$R/build-$k" "$list" > "$R/build-$k.txt" 2>&1 || { echo "  build of old base + $done_sets failed"; ok=0; break; }
      for p in "${dpk[@]}"; do
        # (uc-build.sh delivers UCMenu.u unstripped, as it shipped: its text is the stage's text)
        if [ "$p" = UCMenu ]; then DEC=$new_dec dec "$R/build-$k/System/UCMenu.u" --out "$st/$p/Classes" --shipped-text
        else DEC=$new_dec dec "$R/build-$k/System/$p.u" --out "$st/$p/Classes"; fi > /dev/null 2>&1 || { echo "  decompile of stage $k/$p failed"; ok=0; }
      done
    done
    [ $ok = 1 ] || { echo "FAIL rebase: stages"; fails=$((fails+1)); continue; }
    if [ "$(cd "$pout" 2>/dev/null && pwd)" != "$(cd "$repo/patches" && pwd)" ]; then rm -rf "$pout"; cp -a "$repo/patches" "$pout"; fi
    up rebase --new-base "$R/new-base" --stages "$R/stages" --sets "$CUR_SETS" --patches "$pout" || { echo "FAIL rebase: re-make"; fails=$((fails+1)); continue; }
    up apply --base "$R/new-base" --default --out "$R/new-tree" --patches "$pout" || { echo "FAIL rebase: apply"; fails=$((fails+1)); continue; }
    build "$R/new-tree" "$R/new-build" "$list" > "$R/new-build.txt" 2>&1 || { echo "FAIL rebase: build"; fails=$((fails+1)); continue; }
    last=${sets[${#sets[@]}-1]}
    bad=0
    for p in "${pk[@]}"; do up compare "$R/new-build/System/$p.u" "$R/build-$last/System/$p.u" --report "$R/compare-$p.txt" | head -6 || bad=1; done
    if [ $bad = 0 ]; then echo "PASS rebase: sets re-made in $pout; new base + sets builds the same as old base + sets"; else echo "FAIL rebase"; fails=$((fails+1)); fi
    ;;
  esac
done
echo "times: $W/times.txt"
exit $fails
