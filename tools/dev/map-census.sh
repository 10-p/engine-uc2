#!/usr/bin/env bash
# Load every map on a headless dedicated server and record what happened.
#
#   tools/dev/map-census.sh <build-dir> [seconds-per-map] [workers]
#
# Each worker gets its own run tree (<WorkUC2>/run/census-N) and its own port, so logs and inis do not collide.
# A map PASSES when its level is brought up for play, nothing Critical is logged, the server is still ticking when
# the timeout sends it SIGTERM, and it then exits cleanly: within CENSUS_GRACE seconds (default 30; after that
# `timeout -k` KILLs it and the map FAILs as a hang), without the crash handler ("Aborting." on stdout, a
# "Developer Backtrace" in the log) and with its log closed ("Log file closed"). Result table:
# <WorkUC2>/logs/census/summary.tsv (+ one log per map): `exit=` is the server's own exit status (137 = KILLed
# after the grace), `shutdown=` the seconds from the SIGTERM to the end of the process.
# CENSUS_NAME (default census) names the run trees (<name>-N) and the log folder; CENSUS_PORT (default 7780)
# the first port: a second census (M13's 64-bit one: CENSUS_NAME=m13-census) does not collide with this one.
# CENSUS_NODE=1: <build-dir> is an Emscripten build and each map runs as `node ucc-bin.js` (the wasm32 objects of the
# browser build, under Node). Node's socket emulation cannot listen, so the server logs "can't listen ... the level
# runs without network" (a warning on the web, F-4a) and keeps ticking. A map FAILS there also when the module
# aborts ("Aborted(" or "RuntimeError" on stdout). Wrap the whole census in <WorkUC2>/bin/capped (about 1.5G per worker).
set -uo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
build=${1:?usage: map-census.sh <build-dir> [seconds-per-map] [workers]}
secs=${2:-15}
grace=${CENSUS_GRACE:-30}
workers=${3:-4}
name=${CENSUS_NAME:-census}
port=${CENSUS_PORT:-7780}
out=$work/logs/$name
mkdir -p "$out"
rm -f "$out"/*.log "$out"/part-*.tsv

mapfile -t maps < <(cd "${UC2_ASSETS_DIR:-$work/assets/uc2}/Maps" && ls -1 *.unr | sed 's/\.unr$//')

run_worker() {
  local w=$1
  "$repo/tools/dev/mkrun-uc2.sh" "$build" "$name-$w" >/dev/null
  cd "$work/run/$name-$w/System" || exit 1
  local i=0 map status rc warn crit t0 t1 down exitok note asan asank asanfirst
  for map in "${maps[@]}"; do
    if [ $(( i % workers )) -eq "$w" ]; then
      rm -f ucc.log
      t0=$(date +%s.%N)
      if [ -n "${CENSUS_NODE:-}" ]; then
        # Node with its pthread workers does not end on SIGTERM: KILL at the time limit, and read 137 as "still running".
        timeout -s KILL "$secs" node ucc-bin.js Engine.ServerCommandlet "$map" "PORT=$((port + w))" -log >"$out/$map.stdout.log" 2>&1
        rc=$?; [ "$rc" -eq 137 ] && rc=124
      else
        # --preserve-status: the server's own status, so a crash on the way out is not hidden behind timeout's 124;
        # -k: a server that has not ended $grace s after its TERM is KILLed (137) instead of blocking the census.
        timeout --preserve-status -k "$grace" -s TERM "$secs" ./ucc-bin Engine.ServerCommandlet "$map" "PORT=$((port + w))" -log >"$out/$map.stdout.log" 2>&1
        rc=$?
      fi
      t1=$(date +%s.%N)
      down=$(awk -v a="$t0" -v b="$t1" -v s="$secs" 'BEGIN { printf "%.1f", b - a - s }')
      cp -f ucc.log "$out/$map.log" 2>/dev/null
      # grep -c prints the count and still exits 1 when it is 0, so no `|| echo 0` here.
      warn=$(grep -a -c '^Warning:' "$out/$map.log" 2>/dev/null); warn=${warn:-0}
      crit=$(grep -a -c '^Critical:' "$out/$map.log" 2>/dev/null); crit=${crit:-0}
      abort=$(grep -a -c 'Aborted(\|RuntimeError\|TypeError\|exception catching is not enabled' "$out/$map.stdout.log" 2>/dev/null); abort=${abort:-0}
      note=$(grep -a -m1 '^Critical:' "$out/$map.log" 2>/dev/null | grep -a -v 'appError called' | cut -c1-120)
      # A sanitizer build (preset native64-asan; built with -fsanitize-recover=address and run with halt_on_error=0 it
      # reports every error and goes on) reports on stderr: an AddressSanitizer report fails the map, unless its first
      # frame matches CENSUS_ASAN_KNOWN (an extended regex of known, recorded reports, e.g. 'UnCorSc\.cpp:5481'); the
      # known ones are counted in asan_known=. Each report: "ERROR: AddressSanitizer: <what>" ... "#0 <pc> in <frame>".
      read -r asan asank asanfirst < <(awk -v known="${CENSUS_ASAN_KNOWN:-}" '
        /ERROR: AddressSanitizer/ { want = 1; what = $0; sub(/^.*AddressSanitizer: /, "", what); sub(/ on address.*/, "", what); next }
        want && /^ *#0 / { want = 0; if (known != "" && $0 ~ known) k++; else { n++; if (first == "") { f = $0; sub(/^.* in /, "", f); first = what "@" f } } }
        END { printf "%d %d %s\n", n, k, (first == "" ? "-" : first) }' "$out/$map.stdout.log" 2>/dev/null)
      asan=${asan:-0}; asank=${asank:-0}
      [ "$asan" -gt 0 ] && note="ASan: $(echo "$asanfirst" | cut -c1-160) $note"
      if [ -n "${CENSUS_NODE:-}" ]; then
        down=-; exitok=0; [ "$rc" -eq 124 ] && exitok=1
      else
        # Still running at the TERM (it did not end on its own before), then a clean exit: not KILLed, no crash handler,
        # log closed. The exit status itself is 0 or 1 (UCC's ErrorLevel counts warnings), so it is reported, not judged.
        exitok=1
        awk -v d="$down" 'BEGIN { exit !(d >= 0) }' || { exitok=0; note="ended before the TERM $note"; }
        [ "$rc" -eq 137 ] && { exitok=0; note="hang: KILLed ${grace}s after the TERM $note"; }
        if grep -a -q '^Aborting\.' "$out/$map.stdout.log" 2>/dev/null || grep -a -q 'Developer Backtrace' "$out/$map.log" 2>/dev/null; then
          exitok=0; note="crash handler after $(grep -a -c '^Signal:' "$out/$map.stdout.log") signal(s), last $(grep -a '^Signal:' "$out/$map.stdout.log" | tail -1) $note"
        fi
        grep -a -q 'Log file closed' "$out/$map.log" 2>/dev/null || { exitok=0; note="log not closed $note"; }
      fi
      if grep -a -q "Bringing Level .*\.MyLevel up for play" "$out/$map.log" 2>/dev/null && [ "$crit" -eq 0 ] && [ "$abort" -eq 0 ] && [ "$asan" -eq 0 ] && [ "$exitok" -eq 1 ]; then
        status=PASS
      else
        status=FAIL
      fi
      [ "$asan$asank" != 00 ] && note="asan=$asan asan_known=$asank $note"
      printf '%s\t%s\texit=%s\tshutdown=%s\twarnings=%s\tcritical=%s\taborts=%s\t%s\n' "$status" "$map" "$rc" "$down" "$warn" "$crit" "$abort" \
        "$note" >>"$out/part-$w.tsv"
    fi
    i=$((i + 1))
  done
}

for w in $(seq 0 $((workers - 1))); do run_worker "$w" & done
wait
cat "$out"/part-*.tsv | LC_ALL=C sort -k2 >"$out/summary.tsv"
rm -f "$out"/part-*.tsv
echo "maps: ${#maps[@]}  PASS: $(grep -c '^PASS' "$out/summary.tsv")  FAIL: $(grep -c '^FAIL' "$out/summary.tsv")   -> $out/summary.tsv"
