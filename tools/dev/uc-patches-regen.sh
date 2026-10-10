#!/usr/bin/env bash
# uc-patches-regen.sh: re-make every set of patches/ from the leak's sources (dev only: it needs a build of Source/,
# i.e. Epic's leaked text with our changes on top, which users do not have). T1; docs/porting/analysis/
# script-patches.md, step 2.
#
#   tools/dev/uc-patches-regen.sh [--ref <dir>] [--work <dir>]
#     --ref   the System folder of `tools/dev/script-build.sh --tree t1-ref assemble make strip` (default
#             <WorkUC2>/run/t1-ref/System; its Unstripped/ sibling must hold the unstripped UCMenu.u)
#     --work  scratch folder for the trees (default <WorkUC2>/run/t1-regen)
#
# The chain (each tree is the previous one with some classes replaced; `uc-patches.py make <set>` diffs them):
#   base         uc-decompile of the shipped .u (UCMenu: --shipped-text) for the 7 packages the patch table touches
#   epic-fixes   uc-decompile --patches (the package as the load-time patcher leaves it) for the 6 decompiled ones;
#                UCMenu: the classes the patch table names, as the leak's text compiles (shipped-text of the
#                reference UCMenu.u: our ucc's preprocessing of the leak gives the retail text byte for byte)
#   epic-source  the classes where the leak differs from shipped + patcher in what the patcher could not carry
#                (UCGameplay: CTFFlag's Begin label, OverflowBall's default, UCWeap_Melee.BusyShieldOn.HitWall's
#                locals), from the reference build
#   mouse-look   every UCGameplay class from the reference build
#   player-name  every GUI class from the reference build
#   keys-menu    every UCMenu class from the reference build (shipped-text form); new classes: Source/UCMenu/Classes
# and checks at the end that base + all sets == the reference build's text (identity lines aside).
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
ref=$work/run/t1-ref/System
W=$work/run/t1-regen
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) ref=$2; shift 2;;
    --work) W=$2; shift 2;;
    *) echo "unknown argument $1" >&2; exit 2;;
  esac
done
capped=$work/bin/capped
shipped=$work/epic-sources/System
content=$work/assets/uc2
SP=$repo/Source/Engine/Inc/ScriptPatches.h
dec() { "$capped" 2G 900 -- python3 "$repo/tools/scripts/uc-decompile.py" "$@" --content "$content"; }
up() { "$capped" 2G 900 -- python3 "$repo/tools/scripts/uc-patches.py" "$@"; }
DECOMPILED=(Engine UnrealGame UCGameplay UCInventory UCMutators GUI)
case "$W" in "$work"/run/t1*) ;; *) echo "--work must be under $work/run/t1*" >&2; exit 2;; esac
rm -rf "$W"; mkdir -p "$W"

# base and the reference build's text
for p in "${DECOMPILED[@]}"; do dec "$shipped/$p.u" --out "$W/base/$p/Classes"; done
dec "$shipped/UCMenu.u" --out "$W/base/UCMenu/Classes" --shipped-text
for p in GUI UCGameplay; do dec "$ref/$p.u" --out "$W/ref/$p/Classes"; done
dec "$ref/../Unstripped/UCMenu.u" --out "$W/ref/UCMenu/Classes" --shipped-text

# take(<from tree> <to tree> <Pkg/Class> ...): copy classes
take() { local from=$1 to=$2; shift 2; for c in "$@"; do mkdir -p "$to/${c%%/*}/Classes"; cp -f "$from/${c%%/*}/Classes/${c#*/}.uc" "$to/${c%%/*}/Classes/"; done; }

# epic-fixes
cp -a "$W/base" "$W/c-epic-fixes"
for p in "${DECOMPILED[@]}"; do rm -rf "$W/c-epic-fixes/$p"; dec "$shipped/$p.u" --out "$W/c-epic-fixes/$p/Classes" --patches "$SP"; done
mapfile -t menu < <(python3 -c "
import sys; sys.path.insert(0, '$repo/tools/scripts'); import ucpkg
p = ucpkg.parse_patches('$SP')['UCMenu']
print('\n'.join(sorted({'UCMenu/' + d.split('.')[1] for d, b in p['scripts']})))")
echo "UCMenu classes in the patch table: ${menu[*]}"
take "$W/ref" "$W/c-epic-fixes" "${menu[@]}"
up make epic-fixes --base "$W/base" --changed "$W/c-epic-fixes"

# epic-source
cp -a "$W/c-epic-fixes" "$W/c-epic-source"
take "$W/ref" "$W/c-epic-source" UCGameplay/CTFFlag UCGameplay/OverflowBall UCGameplay/UCWeap_Melee
up make epic-source --base "$W/c-epic-fixes" --changed "$W/c-epic-source"

# ours
cp -a "$W/c-epic-source" "$W/c-mouse-look"; cp -f "$W/ref/UCGameplay/Classes/"*.uc "$W/c-mouse-look/UCGameplay/Classes/"
up make mouse-look --base "$W/c-epic-source" --changed "$W/c-mouse-look"
cp -a "$W/c-mouse-look" "$W/c-player-name"; cp -f "$W/ref/GUI/Classes/"*.uc "$W/c-player-name/GUI/Classes/"
up make player-name --base "$W/c-mouse-look" --changed "$W/c-player-name"
cp -a "$W/c-player-name" "$W/c-keys-menu"
for f in "$W/ref/UCMenu/Classes/"*.uc; do
  c=$(basename "$f")
  if [ -f "$W/base/UCMenu/Classes/$c" ]; then cp -f "$f" "$W/c-keys-menu/UCMenu/Classes/$c"
  else cp -f "$repo/Source/UCMenu/Classes/$c" "$W/c-keys-menu/UCMenu/Classes/$c"; echo "new class (our text): UCMenu/$c"; fi
done
up make keys-menu --base "$W/c-player-name" --changed "$W/c-keys-menu"

# check: base + all sets == the reference text
up apply --base "$W/base" --default --out "$W/final"
bad=0
for p in GUI UCGameplay UCMenu; do
  for f in "$W/c-keys-menu/$p/Classes/"*.uc; do
    c=$(basename "$f")
    cmp -s <(tail -n +2 "$f") <(tail -n +2 "$W/final/$p/Classes/$c") || { echo "MISMATCH $p/$c"; bad=1; }
  done
done
[ $bad = 0 ] && echo "regen: apply(base, default sets) == the chained trees, byte for byte below line 1 (GUI, UCGameplay, UCMenu)" || exit 1
