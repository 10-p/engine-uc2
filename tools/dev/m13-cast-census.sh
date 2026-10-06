#!/usr/bin/env bash
# Pointer-width cast census of a build log (M13): every distinct source line where GCC reports a pointer
# narrowed to a smaller integer ("loses precision", an error that -fpermissive turns into a warning) or an
# integer widened into a pointer (-Wint-to-pointer-cast). Fetched libraries (_deps) are not ours and skipped.
#
#   tools/dev/m13-cast-census.sh <build.log>          # prints "<kind> <file>:<line>" lines, then the counts
# A build log only has the diagnostics of the files it compiled: census a full (clean) build.
log=${1:?usage: m13-cast-census.sh <build.log>}
grep -a -E 'warning: (cast from .* loses precision|cast to pointer from integer of different size)|error: cast (from .* loses precision|to pointer from integer of different size)' "$log" \
 | grep -a -v '/_deps/' \
 | sed -E 's#^.*/engine-uc2[^/]*/##; s#^([^:]+:[0-9]+):[0-9]+: (warning|error): (cast from .* loses precision).*#ptr-to-int \1#; s#^([^:]+:[0-9]+):[0-9]+: (warning|error): (cast to pointer from integer).*#int-to-ptr \1#' \
 | sort -u > "${TMPDIR:-/tmp}/m13-cast-census.$$"
cat "${TMPDIR:-/tmp}/m13-cast-census.$$"
echo "ptr-to-int sites: $(grep -c '^ptr-to-int' "${TMPDIR:-/tmp}/m13-cast-census.$$")  int-to-ptr sites: $(grep -c '^int-to-ptr' "${TMPDIR:-/tmp}/m13-cast-census.$$")"
rm -f "${TMPDIR:-/tmp}/m13-cast-census.$$"
