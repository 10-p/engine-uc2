#!/usr/bin/env bash
# The generated C++ headers (Source/<Pkg>/Inc/<Pkg>Classes.h) are pure output of the script compiler's header
# exporter (`ucc-bin make -ForceHeads`, Editor's UClassExporterH, 64-bit-correct through Core's placeholder table).
# This checks that they are up to date with the script sources: it compiles the scripts in a scratch run tree,
# exports the headers there and compares each with the committed one, byte for byte (the exporter writes CRLF, the
# repo keeps LF: CRs are dropped first). Generated = <Pkg>/Inc/<Pkg>Classes.h (IpDrv's GameSpyClasses.h is
# hand-written). The repository holds no script sources: the scripts are the build's patched tree; Source/ is only where the
# headers are committed.
#
#   tools/dev/check-headers.sh [options]      exit 0 when every header is current, 1 when stale (diff printed),
#                                             2 when the check could not run (no ucc-bin with Editor, make failed)
#   tools/dev/regen-headers.sh [options]      the same export, then the exported headers are copied over
#                                             Source/<Pkg>/Inc/<Pkg>Classes.h (= check-headers.sh --write)
#     --build <dir>          build directory with Source/UCC/ucc-bin linked with the Editor package (the native
#                            32-bit preset; default build-native)
#     --classes-root <dir>   the script sources: <dir>/<Pkg>/Classes (default <build>/Scripts/tree, the build's
#                            patched tree, made by its `scripts` target). A patched tree (only the packages the
#                            patch sets rebuild, decompiled from the shipped .u + patches/; no Engine/) is checked
#                            package by package: tools/scripts/uc-build.sh --headers compiles those packages against
#                            the shipped ones and exports their headers; only those packages' generated headers are
#                            compared (the others come from shipped packages nothing here changes), and a class's
#                            cpptext and `struct native` blocks are not compared (the shipped packages do not carry
#                            them; tools/dev/check-headers-cpptext.py takes the committed header's). A FULL tree (all
#                            17 packages, e.g. Epic's leaked text on the dev machine, <repo>/../epic-sources) is
#                            compiled whole with tools/dev/script-build.sh (dev only; its headers lack our patches)
#     --tree <name>          scratch run tree under <repo>/../run (default hdr-<this checkout's directory name>)
#     --logs <dir>           logs (default <repo>/../logs/hdr)
#     --write                write the exported headers into Source/<Pkg>/Inc (what regen-headers.sh does). On a
#                            patched tree: the committed header with the export's changes (check-headers-cpptext.py
#                            --merge: its cpptext and native structs stay as committed)
#
# `ucc-bin make` runs inside bin/capped (3G): uc-build.sh on a patched tree (about 15 s), script-build.sh on a full one
# (about 30 s). A patched tree takes the shipped packages and the game folder from <build>/Scripts/build-info.txt
# (UC2_SHIPPED_SYSTEM / UC2_ASSETS_DIR override); a full tree needs them like script-build.sh.
set -uo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
build=build-native
classes_root=""
tree=hdr-$(basename "$repo")
logs=$work/logs/hdr
write=0
while [ $# -gt 0 ]; do
  case "$1" in
    --build) build=$2; shift 2;;
    --classes-root) classes_root=$2; shift 2;;
    --tree) tree=$2; shift 2;;
    --logs) logs=$2; shift 2;;
    --write) write=1; shift;;
    *) echo "unknown argument: $1" >&2; exit 2;;
  esac
done
case "$build" in /*) ;; *) build=$repo/$build;; esac
case "$tree" in hdr*) ;; *) echo "tree name must start with hdr: $tree" >&2; exit 2;; esac
if [ ! -x "$build/Source/UCC/ucc-bin" ] || [ ! -d "$build/Source/Editor" ]; then
  echo "check-headers: no $build/Source/UCC/ucc-bin with the Editor package (build the native preset first)" >&2
  exit 2
fi
if [ -z "$classes_root" ]; then
  classes_root=$build/Scripts/tree
  [ -d "$classes_root" ] || { echo "check-headers: no patched script tree at $classes_root: build the scripts first (cmake --build $build --target scripts; configured with UE_SCRIPT_PATCHES ON), or pass --classes-root <dir> (<dir>/<Pkg>/Classes)" >&2; exit 2; }
fi
[ -d "$classes_root" ] || { echo "check-headers: no script sources at $classes_root" >&2; exit 2; }
classes_root=$(cd "$classes_root" && pwd)
mkdir -p "$logs"

# Generated headers are <Pkg>/Inc/<Pkg>Classes.h; IpDrv's GameSpyClasses.h is hand-written and not one of them.
generated() { local d=${1%/Inc/*}; d=${d##*/}; [ "${1##*/}" = "${d}Classes.h" ]; }

if [ ! -d "$classes_root/Engine/Classes" ]; then
  # A patched tree (the packages the patch sets rebuild). Inputs as the build had them: <build>/Scripts/build-info.txt.
  info=$build/Scripts/build-info.txt
  shipped=${UC2_SHIPPED_SYSTEM:-$(sed -n 's/^shipped=//p' "$info" 2>/dev/null)}
  assets=${UC2_ASSETS_DIR:-$(sed -n 's/^assets=//p' "$info" 2>/dev/null)}
  [ -n "$shipped" ] && [ -n "$assets" ] || { echo "check-headers: $classes_root is a patched tree, and no $info names the shipped packages and the game folder (configure the build with the script patches, or set UC2_SHIPPED_SYSTEM and UC2_ASSETS_DIR)" >&2; exit 2; }
  pk=""
  for d in "$classes_root"/*/Classes; do [ -d "$d" ] && pk="$pk,$(basename "$(dirname "$d")")"; done
  pk=${pk#,}
  [ -n "$pk" ] || { echo "check-headers: no <Pkg>/Classes under $classes_root" >&2; exit 2; }
  out=$build/Scripts/headers
  capped=none; [ -x "$work/bin/capped" ] && capped=$work/bin/capped
  if ! "$repo/tools/scripts/uc-build.sh" --headers --classes "$classes_root" --out "$out" --packages "$pk" \
       --shipped "$shipped" --assets "$assets" --ucc "$build/Source/UCC/ucc-bin" --capped "$capped" > "$logs/$tree-make.out" 2>&1; then
    echo "check-headers: compiling the patched tree failed ($logs/$tree-make.out)" >&2
    tail -5 "$logs/$tree-make.out" >&2
    exit 2
  fi
  report=$logs/$tree-check.diff
  : > "$report"
  stale=0 n=0
  IFS=, read -r -a pks <<< "$pk"
  for p in "${pks[@]}"; do
    src=$repo/Source/$p/Inc/${p}Classes.h
    f=$out/$p/Inc/${p}Classes.h
    if [ ! -f "$f" ]; then
      if [ -f "$src" ]; then
        if [ $write -eq 1 ]; then echo "  $p/Inc/${p}Classes.h: not exported any more (left in place: delete it by hand if the package has no exported class)"
        else echo "  $p/Inc/${p}Classes.h: STALE (committed, but the exporter writes no such header)"; stale=1; fi
      fi
      continue
    fi
    n=$((n+1))
    if [ $write -eq 1 ]; then
      if [ ! -f "$src" ]; then
        mkdir -p "$(dirname "$src")"; tr -d '\r' < "$f" > "$src"; echo "  $p/Inc/${p}Classes.h: written (new)"
      elif python3 "$repo/tools/dev/check-headers-cpptext.py" --merge "$src" "$f" "$src.tmp" && ! cmp -s "$src.tmp" "$src"; then
        mv -f "$src.tmp" "$src"; echo "  $p/Inc/${p}Classes.h: written (cpptext and native structs kept from the committed header)"
      else
        [ -f "$src.tmp" ] && { rm -f "$src.tmp"; echo "  $p/Inc/${p}Classes.h: unchanged"; } || { echo "  $p/Inc/${p}Classes.h: merge FAILED" >&2; stale=1; }
      fi
      continue
    fi
    [ -f "$src" ] || { echo "  $p/Inc/${p}Classes.h: STALE (exported, but there is no Source/$p/Inc/${p}Classes.h)"; stale=1; continue; }
    # (cpptext: the shipped packages do not have it; the comparison takes the committed header's there)
    if python3 "$repo/tools/dev/check-headers-cpptext.py" "$src" "$f" "$p/Inc/${p}Classes.h" >> "$report"; then
      echo "  $p/Inc/${p}Classes.h: current (taken from the committed header, not in the shipped packages: $(python3 "$repo/tools/dev/check-headers-cpptext.py" --count "$src" "$f"))"
    else
      echo "  $p/Inc/${p}Classes.h: STALE"; stale=1
    fi
  done
  if [ $write -eq 1 ]; then
    [ $stale -eq 0 ] || exit 1
    echo "regen-headers: $n headers exported from $classes_root (patched packages $pk); review with git diff -- 'Source/*/Inc/*Classes.h'"
    exit 0
  fi
  if [ $stale -ne 0 ]; then
    [ -s "$report" ] && cat "$report"
    echo "check-headers: STALE generated headers (diff above, also $report)."
    echo "  The headers follow the patched scripts: tools/dev/regen-headers.sh --build $build --classes-root $classes_root"
    echo "  writes them into Source/<Pkg>/Inc; commit them with the patch."
    exit 1
  fi
  [ $n -gt 0 ] || { echo "check-headers: the patched packages ($pk) export no generated header"; exit 0; }
  echo "check-headers: all $n generated headers of the patched packages ($pk) are current (scripts: $classes_root)"
  exit 0
fi
run=$work/run/$tree
sb=("$repo/tools/dev/script-build.sh" --build "$build" --tree "$tree" --logs "$logs" --classes-root "$classes_root")

"${sb[@]}" assemble > "$logs/$tree-assemble.out" 2>&1 || { echo "check-headers: assemble failed ($logs/$tree-assemble.out)" >&2; exit 2; }
# The tree's <Pkg>/Inc holds copies of Source/<Pkg>/Inc (the exporter looks there for per-class headers). The
# copies of the generated headers go: a package that no longer exports anything must not leave a current-looking
# copy behind.
rm -f "$run"/*/Inc/*Classes.h
"${sb[@]}" --headers make > "$logs/$tree-make.out" 2>&1
log=$logs/$tree-make.log
verdict=$(grep -a -o '\(Success\|Failure\) - [0-9]* error(s), [0-9]* warning(s)' "$log" 2>/dev/null | tail -1)
case "$verdict" in
  "Success - 0 error(s)"*) ;;
  *) echo "check-headers: ucc-bin make did not succeed (${verdict:-no verdict}); log $log" >&2
     grep -a "^Error\|) : Error," "$log" 2>/dev/null | head -20 >&2
     exit 2;;
esac

stale=0 n=0
report=$logs/$tree-check.diff
: > "$report"
declare -A exported=()
for f in "$run"/*/Inc/*Classes.h; do
  [ -f "$f" ] && generated "$f" || continue
  rel=${f#$run/}
  exported[$rel]=1
  n=$((n+1))
  src=$repo/Source/$rel
  if [ $write -eq 1 ]; then
    if [ -f "$src" ] && cmp -s <(tr -d '\r' < "$f") "$src"; then
      echo "  $rel: unchanged"
    else
      tr -d '\r' < "$f" > "$src.tmp" && mv -f "$src.tmp" "$src"
      echo "  $rel: written"
    fi
    continue
  fi
  if [ ! -f "$src" ]; then
    echo "  $rel: STALE (exported, but there is no Source/$rel)"
    stale=1; continue
  fi
  if cmp -s <(tr -d '\r' < "$f") "$src"; then
    echo "  $rel: current"
  else
    echo "  $rel: STALE"
    diff -u --label "Source/$rel (committed)" --label "Source/$rel (exported from the scripts)" "$src" <(tr -d '\r' < "$f") >> "$report"
    stale=1
  fi
done
# A committed generated header whose package exports nothing any more.
for s in "$repo"/Source/*/Inc/*Classes.h; do
  generated "$s" || continue
  rel=${s#$repo/Source/}
  if [ -z "${exported[$rel]:-}" ]; then
    if [ $write -eq 1 ]; then
      echo "  $rel: not exported any more (left in place: delete it by hand if the package has no exported class)"
    else
      echo "  $rel: STALE (committed, but the exporter writes no such header)"; stale=1
    fi
  fi
done
if [ $n -eq 0 ]; then echo "check-headers: the exporter wrote no header ($log)" >&2; exit 2; fi
if [ $write -eq 1 ]; then
  echo "regen-headers: $n headers exported from $classes_root; review with git diff -- 'Source/*/Inc/*Classes.h'"
  exit 0
fi
if [ $stale -ne 0 ]; then
  [ -s "$report" ] && cat "$report"
  echo "check-headers: STALE generated headers (diff above, also $report)."
  echo "  Regenerate with tools/dev/regen-headers.sh --classes-root $classes_root and commit the result with the script change."
  exit 1
fi
echo "check-headers: all $n generated headers are current (scripts: $classes_root)"
exit 0
