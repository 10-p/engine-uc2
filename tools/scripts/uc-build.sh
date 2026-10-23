#!/usr/bin/env bash
# uc-build.sh: compile script packages from a patched class tree, against the shipped packages, the way Epic's
# build machine compiled them (make, then stripsource), one package at a time. The CMake build's script step
# (cmake/UC2Scripts.cmake) runs it.
#
#   tools/scripts/uc-build.sh --classes <tree> --out <dir> --packages A,B,... [options]
#
#   --classes <tree>   <tree>/<Pkg>/Classes/*.uc for every package to build (uc-patches.py apply's output)
#   --packages A,B     the packages to compile (any order; compiled in EditPackages order): the targets and the
#                      packages that must be compiled first (`uc-patches.py packages ... --compile-list <shipped>`)
#   --keep A,B         the packages to deliver in <out>/System (default: all of --packages); the others compiled
#                      only so the targets could be, and System/ gets the shipped file back for them
#   --out <dir>        the work tree; the result is <dir>/System/<Pkg>.u (with the shipped .u of every other
#                      package beside it, so the folder is a complete System for checking)
#   --shipped <dir>    the shipped script packages (default <repo>/../assets/uc2-assets/System: the game folder's .u
#                      are the shipped ones, patches/shipped.sha256 checks them)
#   --assets <dir>     the game tree: its content folders (Textures, StaticMeshes, ...) are linked into the work tree
#                      and its System/ provides the .int files and Epic's inis (default <repo>/../assets/uc2-assets)
#   --retail-inis <d>  a System/ whose Characters.ini, Gametypes.ini and Maplists.ini the strip loads instead of the
#                      asset tree's (default `none`: the asset tree's, the inis the game runs with; the pipeline proof
#                      passes the retail disc's, which the shipped defaults carry)
#   --ucc <file>       the script compiler: ucc-bin (Linux) or ucc.exe (a Windows host; MSYS2 bash); default
#                      <repo>/build-native/Source/UCC/ucc-bin. Copied into the work tree under its own name.
#   --mem <size>       memory ceiling of each ucc run (default 3G) when it runs under the capped wrapper
#   --capped <file>    the wrapper every ucc run goes through (<repo>/../bin/capped: memory ceiling and time limit);
#                      default <repo>/../bin/capped when it exists, else none; `none`: run ucc directly (a machine
#                      without that wrapper; the CMake build passes it explicitly)
#   --keep-text        do not strip (all packages keep their script text; for debugging)
#   --headers          also export the C++ headers (`make -ForceHeads` instead of `-noh`) into <out>/<Pkg>/Inc, which
#                      starts as a copy of <repo>/Source/<Pkg>/Inc (the exporter includes a per-class header when it
#                      finds one there); implies --keep-text. tools/dev/check-headers.sh uses it
#   --link <rel>=<dir> also link <dir> as <work tree>/<rel> (e.g. Engine/Textures for `#exec TEXTURE IMPORT` files;
#                      none of GUI, UCGameplay, UCMenu, UCMutators needs one)
#
# Stages (each a `ucc-bin` run under bin/capped):
#   make <Pkg>   for each package to build, in EditPackages order: System/ holds only the packages before it (shipped,
#                or built by an earlier stage), Avaris.ini = DefaultXbox.ini, User.ini = DefUserXbox.ini (the inis
#                Epic compiled with, Source/BuildProcess/sync_avaris.pl), and `ucc-bin make -nobind -noh
#                -CONFORMNOCONFIG endpkg=<Pkg>` compiles exactly that package, conformed to the shipped file
#                (../GUIRes/<Pkg>.u). Hiding the later packages matters: Epic's build compiled from scratch, so a
#                default that names a class of a later package (UCGameplay -> UCEffects) did not resolve.
#   strip        all 17 back in System/, PC inis (Default.ini/DefUser.ini) and the retail per-class inis, `ucc-bin
#                editor.stripsource -CONFORM` (every .u but Editor.u re-saved with " " text); the built packages
#                are taken from it, UCMenu.u from before the strip (it shipped with its text).
# Bash only (coreutils, cp, ln); no Python. Nothing is written outside --out. Linux, or MSYS2 bash on a Windows host
# (there `ln -s` needs MSYS=winsymlinks:nativestrict and Windows symlink rights, or it copies; not tested there).
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
classes="" out="" packages="" keep="" mem=3G keep_text=0 headers=0 links=()
# Defaults: the game folder beside the repo (its System/ holds the 17 shipped .u and the inis the game runs with).
assets=$work/assets/uc2-assets
shipped=$assets/System retail=none
ucc=$repo/build-native/Source/UCC/ucc-bin
capped=$work/bin/capped
[ -x "$capped" ] || capped=none
while [ $# -gt 0 ]; do
  case "$1" in
    --classes) classes=$2; shift 2;;
    --out) out=$2; shift 2;;
    --packages) packages=$2; shift 2;;
    --keep) keep=$2; shift 2;;
    --shipped) shipped=$2; shift 2;;
    --assets) assets=$2; shift 2;;
    --retail-inis) retail=$2; shift 2;;
    --ucc) ucc=$2; shift 2;;
    --mem) mem=$2; shift 2;;
    --capped) capped=$2; shift 2;;
    --keep-text) keep_text=1; shift;;
    --headers) headers=1; keep_text=1; shift;;
    --link) links+=("$2"); shift 2;;
    *) echo "uc-build: unknown argument $1" >&2; exit 2;;
  esac
done
[ -n "$classes" ] && [ -n "$out" ] && [ -n "$packages" ] || { echo "uc-build: --classes, --out and --packages are required" >&2; exit 2; }
[ -f "$ucc" ] || { echo "uc-build: no script compiler at $ucc" >&2; exit 2; }
ucc_name=$(basename "$ucc")
if [ "$capped" = none ]; then
  run_ucc() { "./$ucc_name" "$@"; }
else
  [ -x "$capped" ] || { echo "uc-build: no $capped (--capped none runs ucc directly)" >&2; exit 2; }
  run_ucc() { "$capped" "$mem" 1800 -- "./$ucc_name" "$@"; }
fi
classes=$(cd "$classes" && pwd)

all=(Core Engine UCAnnouncements Editor UnrealEd IpDrv UWeb GamePlay UnrealGame UCElements UCGameplay
     UCEffects UCCharacters UCInventory UCMutators GUI UCMenu)
declare -A want=()
IFS=, read -r -a req <<< "$packages"
for p in "${req[@]}"; do
  ok=0; for q in "${all[@]}"; do [ "$q" = "$p" ] && ok=1; done
  [ $ok = 1 ] || { echo "uc-build: $p is not a script package" >&2; exit 2; }
  [ -d "$classes/$p/Classes" ] || { echo "uc-build: no $classes/$p/Classes" >&2; exit 2; }
  want[$p]=1
done
declare -A keepset=()
IFS=, read -r -a kreq <<< "${keep:-$packages}"
for p in "${kreq[@]}"; do
  [ -n "${want[$p]:-}" ] || { echo "uc-build: --keep $p is not in --packages" >&2; exit 2; }
  keepset[$p]=1
done
for p in "${all[@]}"; do [ -f "$shipped/$p.u" ] || { echo "uc-build: no $shipped/$p.u" >&2; exit 2; }; done
for f in DefaultXbox.ini DefUserXbox.ini Default.ini DefUser.ini; do
  [ -f "$assets/System/$f" ] || { echo "uc-build: no $assets/System/$f" >&2; exit 2; }
done

mkdir -p "$out"
out=$(cd "$out" && pwd)
case "$out" in /|"$work"|"$repo"|"$assets"*|"$shipped"*) echo "uc-build: refusing --out $out" >&2; exit 2;; esac
rm -rf "$out/System" "$out/GUIRes" "$out/Hidden" "$out/Unstripped"
for d in "$out"/*/; do [ -L "${d%/}" ] && rm -f "${d%/}"; done
for d in "$assets"/*/; do
  d=$(basename "$d"); [ "$d" = System ] && continue
  ln -sfn "$assets/$d" "$out/$d"
done
mkdir -p "$out/System" "$out/GUIRes" "$out/Hidden" "$out/Save" "$out/Cache"
# System: the asset tree's System/ (Epic's inis, Localization/ with the .int files that name the commandlets,
# Ladders/) without its .u, then the shipped .u and ucc-bin
cp -a "$assets/System/." "$out/System/"
rm -f "$out/System/"*.u
for p in "${all[@]}"; do cp -f "$shipped/$p.u" "$out/GUIRes/$p.u"; cp -f "$shipped/$p.u" "$out/System/$p.u"; done
chmod u+w "$out/System"/* "$out/GUIRes"/*
cp -f "$ucc" "$out/System/$ucc_name"
for p in "${all[@]}"; do
  s=$p; [ "$p" = GamePlay ] && s=Gameplay
  rm -rf "${out:?}/$s"
  if [ -n "${want[$p]:-}" ]; then
    mkdir -p "$out/$s"; ln -s "$classes/$p/Classes" "$out/$s/Classes"
    # a set's resources beside Classes (Textures/ ...: what its #exec lines import; uc-patches.py apply puts them there)
    for r in "$classes/$p"/*/; do
      r=$(basename "$r"); [ "$r" = Classes ] || [ ! -d "$classes/$p/$r" ] || ln -sfn "$classes/$p/$r" "$out/$s/$r"
    done
    if [ $headers = 1 ] && [ -d "$repo/Source/$s/Inc" ]; then
      mkdir -p "$out/$s/Inc"; cp -f "$repo/Source/$s/Inc/"*.h "$out/$s/Inc/" 2>/dev/null || true
    fi
  fi
done

for l in "${links[@]}"; do
  mkdir -p "$(dirname "$out/${l%%=*}")"; ln -sfn "$(cd "${l#*=}" && pwd)" "$out/${l%%=*}"
done

log=$out/uc-build.log
: > "$log"
t_all=$SECONDS
for p in "${all[@]}"; do
  [ -n "${want[$p]:-}" ] || continue
  # the packages before p are in System/, p and every later one hidden
  hide=0
  for q in "${all[@]}"; do
    [ "$q" = "$p" ] && hide=1
    if [ $hide = 1 ] && [ -f "$out/System/$q.u" ]; then mv -f "$out/System/$q.u" "$out/Hidden/$q.u"; fi
  done
  rm -f "$out/Hidden/$p.u"
  cp -f "$out/System/DefaultXbox.ini" "$out/System/Avaris.ini"
  cp -f "$out/System/DefUserXbox.ini" "$out/System/User.ini"
  rm -f "$out/System/ucc.log"
  t0=$SECONDS
  set +e
  hflag=-noh; [ $headers = 1 ] && hflag=-ForceHeads
  (cd "$out/System" && run_ucc make -nobind $hflag -CONFORMNOCONFIG "endpkg=$p" -log=ucc.log) > "$out/make-$p.out" 2>&1
  rc=$?
  set -e
  cp -f "$out/System/ucc.log" "$out/make-$p.log" 2>/dev/null || true
  verdict=$(grep -a -o '\(Success\|Failure\) - [0-9]* error(s), [0-9]* warning(s)' "$out/make-$p.log" | tail -1 || true)
  echo "make $p: exit $rc in $((SECONDS - t0)) s, $verdict" | tee -a "$log"
  # Epic's ucc exits 1 on warnings; the verdict line and the file decide
  if [ ! -f "$out/System/$p.u" ] || ! grep -aq 'Success - 0 error(s)' "$out/make-$p.log"; then
    grep -a 'Error' "$out/make-$p.log" | head -20 >&2 || true
    echo "uc-build: make $p failed (log $out/make-$p.log)" >&2
    exit 1
  fi
  for q in "${all[@]}"; do [ -f "$out/Hidden/$q.u" ] && mv -f "$out/Hidden/$q.u" "$out/System/$q.u"; done
done
mkdir -p "$out/Unstripped"
for p in "${all[@]}"; do [ -n "${want[$p]:-}" ] && cp -f "$out/System/$p.u" "$out/Unstripped/$p.u"; done
if [ $keep_text = 0 ]; then
  # stripsource re-saves every .u in System/; only the built ones are kept from it
  mkdir -p "$out/Strip"
  cp -f "$out/System/Default.ini" "$out/System/Avaris.ini"
  cp -f "$out/System/DefUser.ini" "$out/System/User.ini"
  if [ "$retail" != none ]; then
    [ -d "$retail" ] || { echo "uc-build: no --retail-inis folder $retail" >&2; exit 2; }
    for f in Characters.ini Gametypes.ini Maplists.ini; do cp -f "$retail/$f" "$out/System/$f"; done
  fi
  rm -f "$out/System/ucc.log"
  t0=$SECONDS
  set +e
  (cd "$out/System" && run_ucc editor.stripsource -CONFORM -log=ucc.log) > "$out/strip.out" 2>&1
  rc=$?
  set -e
  cp -f "$out/System/ucc.log" "$out/strip.log" 2>/dev/null || true
  echo "strip: exit $rc in $((SECONDS - t0)) s" | tee -a "$log"
  [ $rc = 0 ] || { echo "uc-build: stripsource failed (log $out/strip.log)" >&2; exit 1; }
  [ -n "${want[UCMenu]:-}" ] && cp -f "$out/Unstripped/UCMenu.u" "$out/System/UCMenu.u"
fi
for p in "${all[@]}"; do
  # the re-saved shipped ones and the packages compiled only to build the others are not delivered
  [ -n "${keepset[$p]:-}" ] || cp -f "$shipped/$p.u" "$out/System/$p.u"
  # ucc saves 0600; a delivered package is served and installed like any game file
  if [ -n "${keepset[$p]:-}" ]; then chmod 644 "$out/System/$p.u"; fi
done
rm -f "$out/System/$ucc_name" "$out/System/ucc.log"
echo "built ${kreq[*]} in $((SECONDS - t_all)) s: $(for p in "${all[@]}"; do [ -n "${keepset[$p]:-}" ] && printf '%s ' "$out/System/$p.u"; done)" | tee -a "$log"
