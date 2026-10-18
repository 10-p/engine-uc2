#!/usr/bin/env bash
# Run the frontend's uc2 WebRTC net spec (packages/e2e/uc2-wasm-net.spec.js) against this worktree's
# wasm-perf artifact, under the memory rules of CLAUDE.md ("Running anything").
#
#   tools/dev/e2-web-net.sh <tag> [spec] [extra playwright args...]
#     <tag>   names the logs: <repo>/../logs/e2/e2e-<tag>.txt, .trace, -proof.txt, -pages/<page>-console.txt
#     spec    default uc2-wasm-net.spec.js
#
# What it does, in order (and refuses to start when it cannot do it safely):
#   1. refuses if another e2e-mem-guard runs or :8080/:8081/:9000 are taken (one harness run machine-wide);
#   2. reassembles the frontend's serve-web (<repo>/../unreal-frontend-arch-private, UFRONTEND=<dir>) with this worktree's build-wasm-perf artifact (yarn build, capped 4G)
#      and records the sha256 of serve-web/SDLLaunch.wasm against the artifact's, before and after the run;
#   3. loads a null PulseAudio sink and removes it by its module index afterwards (not by module name: that
#      removes every null sink on the machine);
#   4. runs bin/capped 8G 1800 -- node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=16 -- --target uc2 <spec>.
# Exit status: the run's.
set -u
HERE=$(cd "$(dirname "$0")/../.." && pwd)
W=$(cd "$HERE/.." && pwd)
FE=${UFRONTEND:-$W/unreal-frontend-arch-private}
ART=$HERE/build-wasm-perf/Source/SDLLaunch
OUT=$W/logs/e2
tag=${1:?usage: e2-web-net.sh <tag> [spec] [playwright args]}
spec=${2:-uc2-wasm-net.spec.js}
shift; [ $# -gt 0 ] && shift
mkdir -p "$OUT"
log=$OUT/e2e-$tag.txt; proof=$OUT/e2e-$tag-proof.txt

if pgrep -a node | grep -a -q e2e-mem-guard; then echo "another e2e-mem-guard is running: not starting" >&2; exit 2; fi
for p in 8080 8081 9000; do
  if ss -ltn 2>/dev/null | grep -q ":$p "; then echo "port $p is in use: not starting" >&2; exit 2; fi
done
[ -f "$ART/SDLLaunch.wasm" ] || { echo "no artifact at $ART" >&2; exit 2; }

source $W/env.sh
cd "$FE"
$W/bin/capped 4G 900 -- yarn build --target uc2 --engine-artifact "$ART" > "$OUT/fe-build-$tag.txt" 2>&1 || { echo "yarn build failed (see $OUT/fe-build-$tag.txt)" >&2; exit 2; }
want=$(sha256sum "$ART/SDLLaunch.wasm" | cut -d' ' -f1)
{ echo "artifact $ART/SDLLaunch.wasm $want"; echo "before: $(sha256sum serve-web/SDLLaunch.wasm)"; } > "$proof"
[ "$(sha256sum serve-web/SDLLaunch.wasm | cut -d' ' -f1)" = "$want" ] || { echo "serve-web does not hold this artifact" >&2; exit 2; }

idx=$(pactl load-module module-null-sink sink_name=uc2e2 sink_properties=device.description=uc2e2 2>/dev/null) || idx=
echo "null sink module index: ${idx:-none}" >> "$proof"
echo "machine before: $(free -m | awk '/^Mem:/{print $3" MiB used, "$7" MiB available"}'), load $(cut -d' ' -f1-3 /proc/loadavg)" >> "$proof"
PULSE_SINK=uc2e2 UFRONT_E2E_TRACE=$OUT/e2e-$tag.trace UC2_E2E_LOG_DIR=$OUT/e2e-$tag-pages \
  $W/bin/capped 8G 1800 -- node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=16 --log="$OUT/e2e-$tag-guard.log" -- \
  --target uc2 "$spec" "$@" > "$log" 2>&1
rc=$?
[ -n "$idx" ] && pactl unload-module "$idx"
echo "after:  $(sha256sum serve-web/SDLLaunch.wasm)" >> "$proof"
echo "null sinks named uc2e2 left: $(pactl list short sinks 2>/dev/null | grep -c uc2e2)" >> "$proof"
echo "rc=$rc" >> "$proof"
cat "$proof"
exit $rc
