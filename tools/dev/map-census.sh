#!/usr/bin/env bash
# Load every map on a headless dedicated server and record what happened.
#
#   tools/dev/map-census.sh <build-dir> [seconds-per-map] [workers]
#
# Each worker gets its own run tree (<WorkUC2>/run/census-N) and its own port, so logs and inis do not collide.
# A map PASSES when its level is brought up for play, nothing Critical is logged, and the server is still
# ticking when the timeout stops it. Result table: <WorkUC2>/logs/census/summary.tsv (+ one log per map).
set -uo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
build=${1:?usage: map-census.sh <build-dir> [seconds-per-map] [workers]}
secs=${2:-15}
workers=${3:-4}
out=$work/logs/census
mkdir -p "$out"
rm -f "$out"/*.log "$out"/part-*.tsv

mapfile -t maps < <(cd "${UC2_ASSETS_DIR:-$work/assets/uc2}/Maps" && ls -1 *.unr | sed 's/\.unr$//')

run_worker() {
  local w=$1
  "$repo/tools/dev/mkrun-uc2.sh" "$build" "census-$w" >/dev/null
  cd "$work/run/census-$w/System" || exit 1
  local i=0 map status rc warn crit
  for map in "${maps[@]}"; do
    if [ $(( i % workers )) -eq "$w" ]; then
      rm -f ucc.log
      timeout -s TERM "$secs" ./ucc-bin Engine.ServerCommandlet "$map" "PORT=$((7780 + w))" -log >"$out/$map.stdout.log" 2>&1
      rc=$?
      cp -f ucc.log "$out/$map.log" 2>/dev/null
      # grep -c prints the count and still exits 1 when it is 0, so no `|| echo 0` here.
      warn=$(grep -a -c '^Warning:' "$out/$map.log" 2>/dev/null); warn=${warn:-0}
      crit=$(grep -a -c '^Critical:' "$out/$map.log" 2>/dev/null); crit=${crit:-0}
      if grep -a -q "Bringing Level .*\.MyLevel up for play" "$out/$map.log" 2>/dev/null && [ "$crit" -eq 0 ] && [ "$rc" -eq 124 ]; then
        status=PASS
      else
        status=FAIL
      fi
      printf '%s\t%s\texit=%s\twarnings=%s\tcritical=%s\t%s\n' "$status" "$map" "$rc" "$warn" "$crit" \
        "$(grep -a -m1 '^Critical:' "$out/$map.log" 2>/dev/null | grep -a -v 'appError called' | cut -c1-120)" >>"$out/part-$w.tsv"
    fi
    i=$((i + 1))
  done
}

for w in $(seq 0 $((workers - 1))); do run_worker "$w" & done
wait
cat "$out"/part-*.tsv | LC_ALL=C sort -k2 >"$out/summary.tsv"
rm -f "$out"/part-*.tsv
echo "maps: ${#maps[@]}  PASS: $(grep -c '^PASS' "$out/summary.tsv")  FAIL: $(grep -c '^FAIL' "$out/summary.tsv")   -> $out/summary.tsv"
