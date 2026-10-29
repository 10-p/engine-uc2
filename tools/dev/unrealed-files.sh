#!/usr/bin/env bash
# Copy the editor's files (Source/UnrealEd/release-files.txt) from a windows build, this repository and Epic's drop
# into a game folder or a release's staging folder; stop on any file that is missing.
#
#   tools/dev/unrealed-files.sh <windows build dir> <dest>
#   tools/dev/unrealed-files.sh build-windows ../run/m12     ->  ../run/m12/System/UnrealEd.exe, .../EditorRes/*.bmp
#   tools/dev/unrealed-files.sh --list                       the list's "<source> <path>" lines, resolved
#
#   UC2_EPIC_SOURCES=<dir>   Epic's drop (default <repo>/../epic-sources/uc2-avaris-2226)
#
# bin/package-release.sh (uc2 <version> unrealed), tools/dev/package-windows.sh and tools/dev/mkrun-uc2.sh call it.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
list=$repo/Source/UnrealEd/release-files.txt
epic=${UC2_EPIC_SOURCES:-$repo/../epic-sources/uc2-avaris-2226}
if [ "${1:-}" = --list ]; then
  build='<build>'; dest=''
else
  build=$(cd "${1:?usage: unrealed-files.sh <windows build dir> <dest> | --list}" && pwd)
  dest=${2:?usage: unrealed-files.sh <windows build dir> <dest>}
fi

missing=0
while read -r from src to; do
  case "$from" in
    ''|'#'*) continue ;;
    build) base=$build ;;
    repo)  base=$repo ;;
    epic)  base=$epic ;;
    *) echo "$list: unknown source '$from'" >&2; exit 1 ;;
  esac
  if [ -z "$dest" ]; then
    echo "$base/$src $to"
    continue
  fi
  if [ ! -f "$base/$src" ]; then
    echo "missing: $base/$src (for $to)" >&2
    missing=1
    continue
  fi
  # Programs executable, everything else read-write for its owner (a read-only file stays read-only on Windows).
  case "$to" in *.exe) mode=755 ;; *) mode=644 ;; esac
  install -D -m "$mode" "$base/$src" "$dest/$to"
done < "$list"
exit $missing
