#!/usr/bin/env bash
# Dev-only proof tool. Build every UnrealScript package from Epic's leaked script sources with this build's ucc-bin
# (`ucc-bin make`), the way Epic's build machine did (Source/BuildProcess/sync_avaris.pl), into a tree of its own; then
# (stage `compare`) compare the result with the shipped packages.
#
# The repository holds no UnrealScript: the build makes the patched packages from the player's shipped .u and patches/
# (cmake/UC2Scripts.cmake, tools/scripts/). This tool checks the compiler against Epic's leaked text and needs that
# text, which only the dev machine
# has (<repo>/../epic-sources/uc2-avaris-2226/<Pkg>/Classes, read-only). It is never a build input.
#
#   tools/dev/script-build.sh [options] [stage...]
#     stages: assemble make strip compare (default: all four, in that order)
#     --build <dir>     build directory with Source/UCC/ucc-bin (default build-native). A Windows build directory
#                       (Source/UCC/ucc.exe and no ucc-bin, e.g. build-windows) switches to the Windows compiler:
#                       ucc.exe is copied into the tree and every ucc run (make, strip) goes through wine via
#                       tools/dev/m12-wine.sh, in the prefix $UC2_WINEPREFIX (default <repo>/../.cache/wine-scripts;
#                       created on first use, its wineserver is killed after each run). Everything else (assemble,
#                       the inis, compare) is the same.
#     --tree <name>     output tree <repo>/../run/<name>; must start with m14, d<N> (a D-phase task's prefix,
#                       e.g. d3-make), f<N> (an F-task's, e.g. f1-make), g<N> (a G-task's, e.g. g8-make), s<N>
#                       (a spike's, e.g. s2-make), t<N> (a T-task's, e.g. t2-hdr), hdr (tools/dev/check-headers.sh's,
#                       logs in <repo>/../logs/hdr) or jit (default m14-make)
#     --logs <dir>      where the logs go (default <repo>/../logs/m14; a d<N>/f<N>/g<N>/s<N>/t<N> tree: <repo>/../logs/<that
#                       prefix>; jit: logs/jit)
#     --mem <size>      memory ceiling of the `ucc-bin make` run (default 3G; also under wine)
#     --headers         also export the C++ headers (into <tree>/<Pkg>/Inc, a scratch copy, never Source/)
#     --classes-root <dir>  where the script sources are: <dir>/<Pkg>/Classes for all 17 packages (source folder
#                       names: Gameplay for GamePlay). Default: $UC2_EPIC_SOURCES (<repo>/../epic-sources/uc2-avaris-2226) when it
#                       has Core/Classes, Epic's leaked text; otherwise this option is required. Any full tree
#                       works (e.g. epic-sources with --classes overrides)
#     --extra "<args>"  more `ucc-bin make` arguments
#     --classes <Pkg>=<dir>  take <Pkg>'s classes from <dir> instead of <classes root>/<Pkg>/Classes (repeatable;
#                       e.g. decompiled text: the
#                       build's patched Scripts/tree/UCGameplay/Classes on top of Epic's others)
#
# The tree: content folders hard-linked from the asset tree (never written), System/ a real copy of the asset
# tree's with the 17 EditPackages .u removed (they are what gets built), GUIRes/ the shipped .u (UMakeCommandlet
# conforms each new package against ../GUIRes/<Pkg>.u, as Epic's tree had it), and for each package
# <Pkg>/Classes -> <classes root>/<Pkg>/Classes and <Pkg>/<resource dir> -> the Epic drop's <Pkg>/<resource dir> (the
# #exec imports read textures and models from there), <Pkg>/Inc a copy of Source/<Pkg>/Inc (the exporter looks there).
# The compile inis are Epic's: Avaris.ini = DefaultXbox.ini, User.ini = DefUserXbox.ini (sync_avaris.pl copies them so).
# Nothing is written outside the tree and the logs directory.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
assets=${UC2_ASSETS_DIR:-$work/assets/uc2-assets}
epic=${UC2_EPIC_SOURCES:-$work/epic-sources/uc2-avaris-2226}
# The shipped script packages, the baseline for conforming and comparing. Not the asset tree's System/: some of its
# packages may be rebuilt ones (System/SCRIPTS.txt), and conforming against
# those would chain generations and compare a build with itself.
shipped=${UC2_SHIPPED_SYSTEM:-$epic/System}
capped=$work/bin/capped
build=build-native
tree=m14-make
mem=3G
headers=0
extra=""
logs=""
classes_root=""
stages=()
declare -A classes_override=()
while [ $# -gt 0 ]; do
  case "$1" in
    --build) build=$2; shift 2;;
    --tree) tree=$2; shift 2;;
    --mem) mem=$2; shift 2;;
    --headers) headers=1; shift;;
    --extra) extra=$2; shift 2;;
    --logs) logs=$2; shift 2;;
    --classes-root) classes_root=$(cd "$2" && pwd); shift 2;;
    --classes) classes_override[${2%%=*}]=${2#*=}; shift 2;;
    assemble|make|strip|compare) stages+=("$1"); shift;;
    *) echo "unknown argument: $1" >&2; exit 2;;
  esac
done
[ ${#stages[@]} -eq 0 ] && stages=(assemble make strip compare)
if [ -z "$classes_root" ]; then
  if [ -d "$epic/Core/Classes" ]; then
    classes_root=$(cd "$epic" && pwd)
  else
    echo "script-build: no script sources: the repository holds none (docs/porting/analysis/scripts-removed.md) and" >&2
    echo "  there is no $epic/Core/Classes (UC2_EPIC_SOURCES). Pass --classes-root <dir> (<dir>/<Pkg>/Classes)." >&2
    exit 2
  fi
fi
case "$tree" in m14*|d[0-9]*|f[0-9]*|g[0-9]*|s[0-9]*|t[0-9]*|hdr*|jit*) ;; *) echo "tree name must start with m14, d<N>, f<N>, g<N>, s<N>, t<N>, hdr or jit: $tree" >&2; exit 2;; esac
case "$tree" in */*|*..*) echo "bad tree name: $tree" >&2; exit 2;; esac
case "$build" in /*) ;; *) build=$repo/$build;; esac
run=$work/run/$tree
case "$tree" in d[0-9]*|f[0-9]*|g[0-9]*|s[0-9]*|t[0-9]*) logs=${logs:-$work/logs/${tree%%-*}};; jit*) logs=${logs:-$work/logs/jit};; hdr*) logs=${logs:-$work/logs/hdr};; esac
logs=${logs:-$work/logs/m14}
mkdir -p "$logs"

# The compiler: Linux ucc-bin, or (a Windows build directory) ucc.exe run through wine in its own prefix.
if [ -x "$build/Source/UCC/ucc-bin" ]; then
  ucc_src=$build/Source/UCC/ucc-bin; ucc_name=ucc-bin; ucc_run=(./ucc-bin)
elif [ -f "$build/Source/UCC/ucc.exe" ]; then
  ucc_src=$build/Source/UCC/ucc.exe; ucc_name=ucc.exe
  export UC2_WINEPREFIX=${UC2_WINEPREFIX:-$work/.cache/wine-scripts}
  ucc_run=("$repo/tools/dev/m12-wine.sh" "$run/System" ucc.exe)
else
  ucc_src=""; ucc_name=ucc-bin; ucc_run=(./ucc-bin)
fi

# EditPackages, in order ([Editor.EditorEngine] of DefaultXbox.ini), and the source folder of each.
packages=(Core Engine UCAnnouncements Editor UnrealEd IpDrv UWeb GamePlay UnrealGame UCElements UCGameplay
          UCEffects UCCharacters UCInventory UCMutators GUI UCMenu)
srcdir() { case "$1" in GamePlay) echo Gameplay;; *) echo "$1";; esac; }

assemble() {
  [ -n "$ucc_src" ] || { echo "no $build/Source/UCC/ucc-bin (or ucc.exe)" >&2; exit 1; }
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
    cp -a "$shipped/$p.u" "$run/GUIRes/$p.u"
    rm -f "$run/System/$p.u"
  done
  chmod u+w "$run/GUIRes"/*.u
  # Epic's own inis (the asset tree's System/ has them unchanged), not Engine/Config's: defaults of config
  # properties are read from them during the build.
  cp -f "$assets/System/DefaultXbox.ini" "$run/System/Avaris.ini"
  cp -f "$assets/System/DefUserXbox.ini" "$run/System/User.ini"
  cp -f "$ucc_src" "$run/System/"
  for p in "${packages[@]}"; do
    s=$(srcdir "$p")
    mkdir -p "$run/$s"
    if [ -n "${classes_override[$p]:-}" ]; then
      ln -s "$(cd "${classes_override[$p]}" && pwd)" "$run/$s/Classes"
      echo "  $p: classes from ${classes_override[$p]}"
    else
      ln -s "$classes_root/$s/Classes" "$run/$s/Classes"
    fi
    if [ -d "$repo/Source/$s/Inc" ]; then
      mkdir -p "$run/$s/Inc"
      # every header: the exporter writes `#include "<Class>.h"` into a class body when that file exists here
      cp -a "$repo/Source/$s/Inc/"*.h "$run/$s/Inc/" 2>/dev/null || true
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
  local args="-nobind -CONFORMNOCONFIG"
  [ $headers -eq 1 ] && args="$args -ForceHeads" || args="$args -noh"
  rm -f "$run/System/ucc.log"
  set +e
  local t0=$SECONDS
  (cd "$run/System" && "$capped" "$mem" 3600 -- "${ucc_run[@]}" make $args $extra -log=ucc.log) > "$log.out" 2>&1
  local rc=$?
  local dt=$((SECONDS - t0))
  set -e
  cp -f "$run/System/ucc.log" "$log" 2>/dev/null || true
  # Epic's ucc exits 1 when there was any warning (UCC.cpp "scion jg"); the verdict is the last log line.
  echo "$ucc_name make $args $extra: exit $rc in ${dt} s, $(grep -a -o '\(Success\|Failure\) - [0-9]* error(s), [0-9]* warning(s)' "$log" | tail -1) (output $log.out, log $log)"
  for p in "${packages[@]}"; do
    if [ -f "$run/System/$p.u" ]; then echo "  built $p.u $(stat -c %s "$run/System/$p.u") bytes"; else echo "  MISSING $p.u"; fi
  done
  if [ $headers -eq 1 ]; then
    # The exported C++ headers (in the tree's scratch <Pkg>/Inc, never Source/) against ours, line endings aside.
    local hd=$logs/$tree-headers.diff f s
    : > "$hd"
    for f in "$run"/*/Inc/*Classes.h; do
      s=$repo/Source/${f#$run/}
      [ -f "$s" ] || { echo "  header ${f#$run/}: no counterpart in Source/"; continue; }
      if diff <(tr -d '\r' < "$s") <(tr -d '\r' < "$f") > /dev/null; then
        echo "  header ${f#$run/}: identical"
      else
        echo "  header ${f#$run/}: $(diff <(tr -d '\r' < "$s") <(tr -d '\r' < "$f") | grep -c '^[<>]') lines differ"
        { echo "=== ${f#$run/} (< ours in Source/, > exported from the recompiled scripts)"; diff <(tr -d '\r' < "$s") <(tr -d '\r' < "$f"); } >> "$hd"
      fi
    done
    echo "  header diff: $hd"
  fi
  return $rc
}

# Epic ran `ucc editor.stripsource` on the made packages before they shipped: it loads every .u but Editor.u,
# replaces script and C++ text with " " and saves. Same here, saving conformed to ../GUIRes (-CONFORM, this
# build's addition) so the result lines up with the shipped files. The made packages
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
  # The per-class inis (Characters, Gametypes, Maplists) are the run tree's own: the game folder's, which the
  # game runs with.
  set +e
  local t0=$SECONDS
  (cd "$run/System" && "$capped" "$mem" 3600 -- "${ucc_run[@]}" editor.stripsource -CONFORM -log=ucc.log) > "$log.out" 2>&1
  local rc=$?
  local dt=$((SECONDS - t0))
  set -e
  cp -f "$run/System/ucc.log" "$log" 2>/dev/null || true
  # The shipped UCMenu.u was not stripped (all 171 text buffers hold the full text, 111 classes lack the flag a
  # load sets), unlike the other 15: it ships as `make` wrote it. Editor.u is skipped by the commandlet itself.
  cp -f "$run/Unstripped/UCMenu.u" "$run/System/UCMenu.u"
  echo "$ucc_name editor.stripsource -CONFORM: exit $rc in ${dt} s (output $log.out, log $log); UCMenu.u kept unstripped"
  return $rc
}

for s in "${stages[@]}"; do
  case "$s" in
    assemble) assemble;;
    make) make_scripts || true;;
    strip) strip_scripts || true;;
    compare)
      set +e
      python3 "$repo/tools/dev/script-diff.py" "$shipped" "$run/System" \
        --patches "$repo/Source/Engine/Inc/ScriptPatches.h" \
        --details "$logs/$tree-compare.txt" --patch-table "$logs/$tree-patches.txt" --functions "$logs/$tree-functions.txt"
      echo "compare: exit $? (details $logs/$tree-compare.txt, per function $logs/$tree-functions.txt, patch table $logs/$tree-patches.txt)"
      set -e;;
  esac
done
