#!/usr/bin/env bash
# The generated C++ headers (Source/<Pkg>/Inc/<Pkg>Classes.h) are pure output of the script compiler's header
# exporter (`ucc-bin make -ForceHeads`, Editor's UClassExporterH, 64-bit-correct through Core's placeholder table).
# This checks that they are up to date with the script sources: it compiles the scripts in a scratch run tree,
# exports the headers there and compares each with the committed one, byte for byte (the exporter writes CRLF, the
# repo keeps LF: CRs are dropped first). Generated = <Pkg>/Inc/<Pkg>Classes.h (IpDrv's GameSpyClasses.h is
# hand-written). T2, docs/porting/analysis/headers.md.
#
#   tools/dev/check-headers.sh [options]      exit 0 when every header is current, 1 when stale (diff printed),
#                                             2 when the check could not run (no ucc-bin with Editor, make failed)
#   tools/dev/regen-headers.sh [options]      the same export, then the exported headers are copied over
#                                             Source/<Pkg>/Inc/<Pkg>Classes.h (= check-headers.sh --write)
#     --build <dir>          build directory with Source/UCC/ucc-bin linked with the Editor package (the native
#                            32-bit preset; default build-native)
#     --classes-root <dir>   the script sources: <dir>/<Pkg>/Classes (default <repo>/Source); the sources the build
#                            uses, wherever they are generated
#     --tree <name>          scratch run tree under <WorkUC2>/run (default hdr-<this checkout's directory name>)
#     --logs <dir>           logs (default <WorkUC2>/logs/hdr)
#     --write                copy the exported headers into Source/ (what regen-headers.sh does)
#
# Runs through tools/dev/script-build.sh (assemble + make; `ucc-bin make` inside bin/capped, 3G). About 30 s.
# Needs the asset tree (UC2_ASSETS_DIR) and the shipped packages (UC2_SHIPPED_SYSTEM), like script-build.sh.
set -uo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
build=build-native
classes_root=$repo/Source
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
[ -d "$classes_root/Engine/Classes" ] || { echo "check-headers: no script sources under $classes_root" >&2; exit 2; }
mkdir -p "$logs"
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
     grep -a "Error\|error" "$log" 2>/dev/null | head -20 >&2
     exit 2;;
esac

# Generated headers are <Pkg>/Inc/<Pkg>Classes.h; IpDrv's GameSpyClasses.h is hand-written and not one of them.
generated() { local d=${1%/Inc/*}; d=${d##*/}; [ "${1##*/}" = "${d}Classes.h" ]; }
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
  hint=""; [ "$classes_root" != "$repo/Source" ] && hint=" --classes-root $classes_root"
  echo "  Regenerate with tools/dev/regen-headers.sh$hint and commit the result with the script change."
  exit 1
fi
echo "check-headers: all $n generated headers are current (scripts: $classes_root)"
exit 0
