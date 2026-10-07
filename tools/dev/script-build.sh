#!/usr/bin/env bash
# Build every UnrealScript package from source with our ucc-bin (`ucc-bin make`), the way Epic's build machine
# did (Source/BuildProcess/sync_avaris.pl), into a tree of its own; then (stage `compare`) compare the result
# with the shipped packages. M14; docs/porting/analysis/scripts.md.
#
#   tools/dev/script-build.sh [options] [stage...]
#     stages: assemble make strip compare (default: all four, in that order)
#     --build <dir>     build directory with Source/UCC/ucc-bin (default build-native)
#     --tree <name>     output tree <WorkUC2>/run/<name>; must start with m14 (default m14-make)
#     --mem <size>      memory ceiling of the `ucc-bin make` run (default 3G)
#     --headers         also export the C++ headers (into <tree>/<Pkg>/Inc, a scratch copy, never Source/)
#     --extra "<args>"  more `ucc-bin make` arguments
#
# The tree: content folders hard-linked from the asset tree (never written), System/ a real copy of the asset
# tree's with the 17 EditPackages .u removed (they are what gets built), GUIRes/ the shipped .u (UMakeCommandlet
# conforms each new package against ../GUIRes/<Pkg>.u, as Epic's tree had it), and for each package
# <Pkg>/Classes -> Source/<Pkg>/Classes and <Pkg>/<resource dir> -> epic-sources/<Pkg>/<resource dir> (the
# #exec imports read textures and models from there; the repo holds text files only). The compile inis are
# Epic's: Avaris.ini = DefaultXbox.ini, User.ini = DefUserXbox.ini (sync_avaris.pl copies them so).
# Nothing is written outside the tree and <WorkUC2>/logs/m14.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
assets=${UC2_ASSETS_DIR:-$work/assets/uc2}
epic=${UC2_EPIC_SOURCES:-$work/epic-sources}
capped=$work/bin/capped
build=build-native
tree=m14-make
mem=3G
headers=0
extra=""
stages=()
while [ $# -gt 0 ]; do
  case "$1" in
    --build) build=$2; shift 2;;
    --tree) tree=$2; shift 2;;
    --mem) mem=$2; shift 2;;
    --headers) headers=1; shift;;
    --extra) extra=$2; shift 2;;
    assemble|make|strip|compare) stages+=("$1"); shift;;
    *) echo "unknown argument: $1" >&2; exit 2;;
  esac
done
[ ${#stages[@]} -eq 0 ] && stages=(assemble make strip compare)
case "$tree" in m14*) ;; *) echo "tree name must start with m14: $tree" >&2; exit 2;; esac
case "$tree" in */*|*..*) echo "bad tree name: $tree" >&2; exit 2;; esac
case "$build" in /*) ;; *) build=$repo/$build;; esac
run=$work/run/$tree
logs=$work/logs/m14
mkdir -p "$logs"

# EditPackages, in order ([Editor.EditorEngine] of DefaultXbox.ini), and the source folder of each.
packages=(Core Engine UCAnnouncements Editor UnrealEd IpDrv UWeb GamePlay UnrealGame UCElements UCGameplay
          UCEffects UCCharacters UCInventory UCMutators GUI UCMenu)
srcdir() { case "$1" in GamePlay) echo Gameplay;; *) echo "$1";; esac; }

assemble() {
  [ -x "$build/Source/UCC/ucc-bin" ] || { echo "no $build/Source/UCC/ucc-bin" >&2; exit 1; }
  rm -rf "$run"
  mkdir -p "$run"
  for d in "$assets"/*/; do
    d=$(basename "$d")
    [ "$d" = System ] && continue
    cp -al "$assets/$d" "$run/$d"
  done
  cp -a "$assets/System" "$run/System"
  chmod -R u+w "$run/System"
  mkdir -p "$run/GUIRes"
  for p in "${packages[@]}"; do
    cp -a "$assets/System/$p.u" "$run/GUIRes/$p.u"
    rm -f "$run/System/$p.u"
  done
  chmod u+w "$run/GUIRes"/*.u
  # Epic's own inis (the asset tree's System/ has them unchanged), not Engine/Config's: defaults of config
  # properties are read from them during the build.
  cp -f "$assets/System/DefaultXbox.ini" "$run/System/Avaris.ini"
  cp -f "$assets/System/DefUserXbox.ini" "$run/System/User.ini"
  cp -f "$build/Source/UCC/ucc-bin" "$run/System/"
  for p in "${packages[@]}"; do
    s=$(srcdir "$p")
    mkdir -p "$run/$s"
    ln -s "$repo/Source/$s/Classes" "$run/$s/Classes"
    if [ -d "$repo/Source/$s/Inc" ]; then
      mkdir -p "$run/$s/Inc"
      cp -a "$repo/Source/$s/Inc/"*Classes.h "$run/$s/Inc/" 2>/dev/null || true
    fi
    for r in "$epic/$s"/*/; do
      r=$(basename "$r")
      case "$r" in Classes|Inc|Src|Lib) continue;; esac
      ln -s "$epic/$s/$r" "$run/$s/$r"
    done
  done
  mkdir -p "$run/Save" "$run/Cache"
  echo "assembled $run"
}

make_scripts() {
  local log=$logs/$tree-make.log
  local args="-nobind"
  [ $headers -eq 1 ] || args="$args -noh"
  rm -f "$run/System/ucc.log"
  set +e
  (cd "$run/System" && "$capped" "$mem" 3600 -- ./ucc-bin make $args $extra -log=ucc.log) > "$log.out" 2>&1
  local rc=$?
  set -e
  cp -f "$run/System/ucc.log" "$log" 2>/dev/null || true
  echo "ucc-bin make $args $extra: exit $rc (output $log.out, log $log)"
  for p in "${packages[@]}"; do
    if [ -f "$run/System/$p.u" ]; then echo "  built $p.u $(stat -c %s "$run/System/$p.u") bytes"; else echo "  MISSING $p.u"; fi
  done
  return $rc
}

# Epic ran `ucc editor.stripsource` on the made packages before they shipped (docs/porting/analysis/scripts.md,
# step 2): it loads every .u but Editor.u, replaces script and C++ text with " " and saves. Same here, saving
# conformed to ../GUIRes (-CONFORM, our addition) so the result lines up with the shipped files. The made packages
# (with their text) stay in <tree>/Unstripped/.
strip_scripts() {
  local log=$logs/$tree-strip.log
  rm -rf "$run/Unstripped"
  mkdir -p "$run/Unstripped"
  for p in "${packages[@]}"; do cp -a "$run/System/$p.u" "$run/Unstripped/$p.u"; done
  rm -f "$run/System/ucc.log"
  # With the PC inis: the shipped defaults of config properties carry Default.ini's values where it and
  # DefaultXbox.ini differ (e.g. UWeb.WebServer DefaultApplication=0, against the class's -1), so the strip,
  # which loads every class's config again, ran after sync_avaris.pl had deleted the Xbox Avaris.ini/User.ini.
  cp -f "$assets/System/Default.ini" "$run/System/Avaris.ini"
  cp -f "$assets/System/DefUser.ini" "$run/System/User.ini"
  set +e
  (cd "$run/System" && "$capped" "$mem" 3600 -- ./ucc-bin editor.stripsource -CONFORM -log=ucc.log) > "$log.out" 2>&1
  local rc=$?
  set -e
  cp -f "$run/System/ucc.log" "$log" 2>/dev/null || true
  # The shipped UCMenu.u was not stripped (all 171 text buffers hold the full text, 111 classes lack the flag a
  # load sets), unlike the other 15: it ships as `make` wrote it. Editor.u is skipped by the commandlet itself.
  cp -f "$run/Unstripped/UCMenu.u" "$run/System/UCMenu.u"
  echo "ucc-bin editor.stripsource -CONFORM: exit $rc (output $log.out, log $log); UCMenu.u kept unstripped"
  return $rc
}

for s in "${stages[@]}"; do
  case "$s" in
    assemble) assemble;;
    make) make_scripts || true;;
    strip) strip_scripts || true;;
    compare) echo "compare: not written yet";;
  esac
done
