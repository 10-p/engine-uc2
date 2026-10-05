#!/usr/bin/env bash
# Network play tests of the native build: dedicated server (ucc-bin server) and listen server (uc2.bin
# <map>?Listen) with clients, all on 127.0.0.1 port 7850 (NET_PORT), inside ONE bin/capped scope.
#
#   tools/dev/net-test.sh [--keep-tree] [check ...]
#   checks: dedicated1 dedicated2 handshake-loss listen travel disconnect lag glshot soak   (default: all of them)
#           wine   (opt-in) the Linux client against this branch's ucc.exe server under wine; needs
#                  <WorkUC2>/run/netwin (tools/dev/mkrun-uc2.sh build-windows netwin); prefix .cache/wine-net
#
# Builds a fresh run tree <WorkUC2>/run/net from build-native (tools/dev/mkrun-uc2.sh) unless --keep-tree,
# then runs tools/dev/net-test.py. Results: one PASS/FAIL line per item, also in
# <WorkUC2>/logs/net/net-summary.txt; per-process logs <check>-<proc>.log/.out in the same folder.
# Memory: one 6G ceiling for the whole run (server + up to 3 clients, each well under 1G; CLAUDE.md "Running
# anything"). Nothing is left running or listening: capped kills the scope, and the script checks the port.
# Environment: NET_SOAK_MIN (soak minutes, default 10), NET_PORT (default 7850; keep it in 7850-7899).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
out=$work/logs/net
keep=0
[ "${1:-}" = "--keep-tree" ] && { keep=1; shift; }
checks=("$@")
[ ${#checks[@]} -eq 0 ] && checks=(dedicated1 dedicated2 handshake-loss listen travel disconnect lag glshot soak)
port=${NET_PORT:-7850}
case "$port" in 78[5-9][0-9]) ;; *) echo "NET_PORT must be in 7850-7899" >&2; exit 2;; esac
mkdir -p "$out"
if [ $keep = 0 ]; then
  "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" net > /dev/null || exit 2
fi
sys=$work/run/net/System
[ -x "$sys/uc2.bin" ] && [ -x "$sys/ucc-bin" ] || { echo "no run tree at $sys" >&2; exit 2; }
if ss -lun 2>/dev/null | grep -a -q ":$port "; then echo "port $port is busy" >&2; exit 2; fi
: > "$out/net-summary.txt"
echo "net-test $(date -Is) checks: ${checks[*]} commit $(git -C "$repo" rev-parse --short HEAD)" >> "$out/net-summary.txt"
secs=600
for c in "${checks[@]}"; do
  case "$c" in soak) secs=$(( secs + ${NET_SOAK_MIN:-10} * 60 + 240 ));; wine) secs=$(( secs + 600 ));; handshake-loss) secs=$(( secs + 1200 ));; glshot) secs=$(( secs + 600 ));; *) secs=$(( secs + 300 ));; esac
done
"$work/bin/capped" 6G "$secs" -- env NET_PORT="$port" UC2_WINEPREFIX="${UC2_WINEPREFIX:-$work/.cache/wine-net}" python3 "$repo/tools/dev/net-test.py" "$sys" "$out" "${checks[@]}"
rc=$?
# Screenshots to PNG (PIL), for looking at; the checks never read pictures.
for b in "$out"/*.bmp; do
  [ -f "$b" ] && python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" "$b" "${b%.bmp}.png" && rm -f "$b"
done
left=$(ss -lunp 2>/dev/null | grep -a ":$port " || true)
if [ -n "$left" ]; then echo "FAIL leftovers: something still listens on $port: $left" | tee -a "$out/net-summary.txt"; rc=1
else echo "PASS leftovers: nothing listens on UDP $port after the run" | tee -a "$out/net-summary.txt"; fi
[ $rc = 137 ] && echo "capped: memory ceiling hit (6G): do not rerun unchanged" | tee -a "$out/net-summary.txt"
exit $rc
