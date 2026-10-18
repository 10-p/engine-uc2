#!/usr/bin/env bash
# The tables UC2ALAudio builds from the shipped .xsb against the ones it builds from the cooked .uc2cues, for
# every bank. Needs a run tree with both in Sounds/ (tools/dev/mkrun-uc2.sh with UC2_XBOX_SOUNDS=add).
#
#   tools/dev/audio-xsb-compare.sh <tree>        e.g. m16-both
#
# Runs `ucc-bin UC2ALAudio.UC2AudioCheck DUMP=...` (capped 2G) and tools/dev/audio-xsb-compare.py on the dumps.
# Output: <repo>/../logs/<tree>/xsb-compare.out (every difference, known or NEW), the dumps in xsb-dump/.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?tree}
sys=$work/run/$tree/System
out=$work/logs/$tree
mkdir -p "$out"
rm -rf "$out/xsb-dump"
( cd "$sys" && "$work/bin/capped" 2G 1200 -- ./ucc-bin UC2ALAudio.UC2AudioCheck "DUMP=$out/xsb-dump" > "$out/xsb-compare-check.out" 2>&1 )
echo "check: $(grep -a '^UC2AudioCheck:' "$out/xsb-compare-check.out")"
python3 "$repo/tools/dev/audio-xsb-compare.py" "$out/xsb-dump" > "$out/xsb-compare.out"
rc=$?
grep -a -E '^(compared|DIFF|RESULT|no )' "$out/xsb-compare.out"
exit $rc
