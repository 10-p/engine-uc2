#!/usr/bin/env bash
# C-1 check: the `-ini:<File>:[<Section>]:<Key>=<Value>` command-line overrides (appApplyIniOverrides,
# Source/Core/Src/UnMisc.cpp), one PASS/FAIL line per check.
#
#   tools/dev/c1-ini-overrides.sh
#
# Needs a native run tree: tools/dev/mkrun-uc2.sh build-native c1   (override: C1_TREE=<name> under <WorkUC2>/run).
# Every game or ucc run is inside <WorkUC2>/bin/capped (CLAUDE.md): 2G for the client, 2G for ucc-bin.
# Output in <WorkUC2>/logs/c1/ini/ (C1_OUT=<dir>): a-*.out/.result (the client with good overrides), b-*.out/.result
# (the client with malformed ones), c-ucc.out (ucc-bin), and the inis each run left behind (a-Avaris.ini ...).
#
# How "takes effect" is proven: not from our own log line only. The client is started with the override on its
# command line, the engine's test channel (m10-controls.sh --file, -inputtest=) runs the console command
# `get <Class> <Property>`, which prints the class default the config loader produced, and the harness waits for
# that value in the engine's log. The same run is then stopped with SIGTERM (a normal exit: the config cache
# flushes) and the inis it left are read, which is how "written back to the ini on exit" is decided.
#
# Values used (Default.ini / DefUser.ini hold the originals):
#   System ini (Avaris.ini): [Engine.GameReplicationInfo] ShortName "Unreal Server" -> C1Short
#                            [Engine.GameReplicationInfo] AdminEmail -> "My C1 Server?mode=a=b" (spaces, ?, =; quoted)
#                            (not ServerName: the profile code copies its own ServerName over that class default at
#                            start-up, PlayerProfile.uc:764, so `get` would show the profile's, "Liandri Corporation")
#   User ini  (User.ini)   : [UCGameplay.UCPlayerInput] MouseSensitivity_X 1.0 -> 1.75
#                            [DefaultPlayer] Name -> C1Player (the launcher's player name)
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${C1_TREE:-c1}
sys=$work/run/$tree/System
out=${C1_OUT:-$work/logs/c1/ini}
capped=$work/bin/capped
m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] && [ -x "$sys/ucc-bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }

fails=0
result() {	# result PASS|FAIL <name> <detail>
  echo "$1 $2: $3"
  [ "$1" = PASS ] || fails=$((fails+1))
}
check() {	# check <name> <command...>: PASS when the command succeeds; the detail is its output
  local name=$1 detail; shift
  if detail=$("$@" 2>&1); then result PASS "$name" "$detail"; else result FAIL "$name" "$detail"; fi
}

# ini_get <file> <section> <key>: the value of the first Key= line in [Section], or nothing (exit 1).
ini_get() {
  awk -v sec="$2" -v key="$3" '
    { sub(/\r$/, "") }
    /^\[/ { cur = substr($0, 2, length($0) - 2); next }
    cur == sec { i = index($0, "="); if (i > 1 && substr($0, 1, i-1) == key) { print substr($0, i+1); found = 1; exit } }
    END { exit found ? 0 : 1 }' "$1"
}
expect_ini() {	# expect_ini <file> <section> <key> <value>
  local got; got=$(ini_get "$1" "$2" "$3") || { echo "[$2] $3 is not in $(basename "$1")"; return 1; }
  [ "$got" = "$4" ] && echo "[$2] $3=$got in $(basename "$1")" || { echo "[$2] $3 is '$got', expected '$4'"; return 1; }
}
logline() {	# logline <fixed text> <log...>: the first matching line of the first log that has one
  local text=$1 f l; shift
  for f in "$@"; do
    l=$(grep -a -F -m1 -- "$text" "$f" 2>/dev/null | tr -d '\r') && [ -n "$l" ] && { echo "$(basename "$f"): $l"; return 0; }
  done
  echo "no log line '$text' in $*"; return 1
}
count_is() {	# count_is <expected> <fixed text> <log>
  local n; n=$(grep -a -c -F -- "$2" "$3" || true)
  [ "$n" = "$1" ] && echo "$n x '$2'" || { echo "$n x '$2', expected $1"; return 1; }
}

run_client() {	# run_client <name> <scenario> <M10_URL or empty> <uc2.bin args...>
  local name=$1 scen=$2 url=$3; shift 3
  rm -f "$sys"/*.log "$sys"/*.log.*
  M10_STOPSIG=${STOPSIG:-TERM} M10_TREE=$tree M10_OUT=$out M10_URL=$url "$m10" --file "$scen" "$name" "$@" | sed 's/^/    /'
  # Lines logged inside appInit (the override lines) go to the FILE log only: -log's console echo starts later.
  cp -f "$sys/Avaris.log" "$out/$name-Avaris.log" 2>/dev/null
  cp -f "$sys/Avaris.ini" "$out/$name-Avaris.ini" 2>/dev/null
  cp -f "$sys/User.ini" "$out/$name-User.ini" 2>/dev/null
}

# ---------------------------------------------------------------------------------------------------------------
# A: good overrides. The first token on the command line is an override (no map): the engine must still boot
# to its default map, which is the main menu.
# ---------------------------------------------------------------------------------------------------------------
cat > "$out/a.scen" <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 2
chan exec get Engine.GameReplicationInfo ShortName
expect a-get-system-value ^C1Short\s*$ 10
chan exec get Engine.GameReplicationInfo AdminEmail
expect a-get-quoted-value-with-spaces-question-mark-and-equals ^My C1 Server\?mode=a=b\s*$ 10
chan exec get UCGameplay.UCPlayerInput MouseSensitivity_X
expect a-get-user-value ^1\.75 10
SCEN
echo "== A: good overrides (client, Xvfb)"
run_client a "$out/a.scen" '-ini:User:[DefaultPlayer]:Name=C1Player' \
  '-ini:System:[Engine.GameReplicationInfo]:ShortName=C1Short' \
  '"-ini:System:[Engine.GameReplicationInfo]:AdminEmail=My C1 Server?mode=a=b"' \
  '-ini:User:[UCGameplay.UCPlayerInput]:MouseSensitivity_X=1.75'
for c in a-get-system-value a-get-quoted-value-with-spaces-question-mark-and-equals a-get-user-value; do
  if grep -a -q -P "^PASS\t$c\t" "$out/a.result" 2>/dev/null; then result PASS "$c" "$(grep -a -P "^PASS\t$c\t" "$out/a.result" | cut -f3 | cut -c1-200)"
  else result FAIL "$c" "see $out/a.result"; fi
done
check a-first-token-override-still-boots-to-the-menu logline 'OpenMenu [UCMenu.UC2MainMenu]' "$out/a.out" 
check a-log-system-override logline 'Ini override: [Engine.GameReplicationInfo] ShortName=C1Short (System)' "$out/a-Avaris.log" "$out/a.out" 
check a-log-user-override logline 'Ini override: [DefaultPlayer] Name=C1Player (User)' "$out/a-Avaris.log" "$out/a.out" 
check a-log-value-with-spaces logline 'Ini override: [Engine.GameReplicationInfo] AdminEmail=My C1 Server?mode=a=b (System)' "$out/a-Avaris.log" "$out/a.out" 
check a-no-critical test "$(grep -a -c Critical "$out/a.out")" = 0
# Written back on exit (a SIGTERM exit flushes the config cache): the override is in the ini afterwards.
check a-system-value-written-to-Avaris.ini expect_ini "$out/a-Avaris.ini" Engine.GameReplicationInfo ShortName C1Short
check a-spaced-value-written-to-Avaris.ini expect_ini "$out/a-Avaris.ini" Engine.GameReplicationInfo AdminEmail 'My C1 Server?mode=a=b'
check a-user-value-written-to-User.ini expect_ini "$out/a-User.ini" UCGameplay.UCPlayerInput MouseSensitivity_X 1.75
check a-player-name-written-to-User.ini expect_ini "$out/a-User.ini" DefaultPlayer Name C1Player
check a-original-is-not-the-override test "$(ini_get "$repo/Engine/Config/Default.ini" Engine.GameReplicationInfo ShortName)" != C1Short

# ---------------------------------------------------------------------------------------------------------------
# B: malformed tokens, then one good one. Each malformed token is logged and ignored, the good one after them
# still applies, the engine boots and exits normally.
# ---------------------------------------------------------------------------------------------------------------
cat > "$out/b.scen" <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 2
chan exec get Engine.GameReplicationInfo ShortName
expect b-good-token-after-the-bad-ones-applies ^AfterBad\s*$ 10
SCEN
echo "== B: malformed tokens (client, Xvfb)"
run_client b "$out/b.scen" '' \
  '-ini:nonsense' \
  '-ini:User:[DefaultPlayer]Name=NoColonAfterBracket' \
  '-ini:User:[]:Key=EmptySection' \
  '-ini::[Section]:Key=EmptyFile' \
  '-ini:User:[Section]:=EmptyKey' \
  '-ini:User:[Section]:NoEquals' \
  '-ini:' \
  '-ini:System:[Engine.GameReplicationInfo]:ShortName=AfterBad'
if grep -a -q -P "^PASS\tb-good-token-after-the-bad-ones-applies\t" "$out/b.result" 2>/dev/null; then
  result PASS b-good-token-after-the-bad-ones-applies "$(grep -a -P '^PASS\tb-good' "$out/b.result" | cut -f3 | cut -c1-200)"
else result FAIL b-good-token-after-the-bad-ones-applies "see $out/b.result"; fi
check b-seven-malformed-tokens-reported count_is 7 'Ini override ignored (expected -ini:<File>:[<Section>]:<Key>=<Value>)' "$out/b-Avaris.log" 
check b-bad-tokens-name-the-token logline 'ignored (expected -ini:<File>:[<Section>]:<Key>=<Value>): -ini:User:[Section]:NoEquals' "$out/b-Avaris.log" 
check b-malformed-tokens-wrote-nothing test "$(grep -a -c -E 'Section|EmptyFile|NoColonAfterBracket' "$out/b-User.ini" "$out/b-Avaris.ini" 2>/dev/null | awk -F: '{s+=$NF} END{print s+0}')" = 0
check b-only-the-good-override-applied count_is 1 'Ini override: [' "$out/b-Avaris.log" 
check b-no-critical test "$(grep -a -c Critical "$out/b.out")" = 0
check b-reached-the-menu logline 'OpenMenu [UCMenu.UC2MainMenu]' "$out/b.out" 

# ---------------------------------------------------------------------------------------------------------------
# C: ucc-bin (Source/UCC) goes through the same appInit. verifyclasses is a short commandlet that loads the
# packages; the override is read back from Avaris.ini after the normal exit.
# ---------------------------------------------------------------------------------------------------------------
echo "== C: ucc-bin"
cp -f "$repo"/Engine/Config/*.ini "$sys/"
rm -f "$sys/Avaris.ini" "$sys/User.ini"
rm -f "$sys"/*.log "$sys"/*.log.*
( cd "$sys" && "$capped" 2G 180 -- ./ucc-bin verifyclasses '"-ini:System:[C1Test]:Key=ucc value"' '-ini:User:[C1Test]:UserKey=ucc-user' '-ini:bad' > "$out/c-ucc.out" 2>&1 ); rc=$?
cp -f "$sys"/*.log "$out/" 2>/dev/null; ls "$sys"/*.log > "$out/c-logs.txt" 2>/dev/null
cp -f "$sys/Avaris.ini" "$out/c-Avaris.ini" 2>/dev/null; cp -f "$sys/User.ini" "$out/c-User.ini" 2>/dev/null
check c-ucc-exits-normally test "$rc" = 0
check c-ucc-log-system-override logline 'Ini override: [C1Test] Key=ucc value (System)' "$out/c-ucc.out" "$sys"/*.log
check c-ucc-log-user-override logline 'Ini override: [C1Test] UserKey=ucc-user (User)' "$out/c-ucc.out" "$sys"/*.log
check c-ucc-reports-malformed logline 'Ini override ignored (expected -ini:<File>:[<Section>]:<Key>=<Value>): -ini:bad' "$out/c-ucc.out" "$sys"/*.log
check c-ucc-system-value-written-to-Avaris.ini expect_ini "$out/c-Avaris.ini" C1Test Key 'ucc value'
check c-ucc-user-value-written-to-User.ini expect_ini "$out/c-User.ini" C1Test UserKey ucc-user

# ---------------------------------------------------------------------------------------------------------------
# D: FLUSHCONFIG (Source/Engine/Src/UnEngine.cpp), the web host's "write the config now". A browser tab is closed,
# never shut down, so nothing may wait for the exit flush: the client is KILLED (no exit code runs), and the value
# is in the ini only if the engine wrote it when asked. E is the same without the command (informational: it says
# whether anything else flushed the cache on its own during the menu boot).
# ---------------------------------------------------------------------------------------------------------------
cat > "$out/d.scen" <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
chan exec flushconfig
expect d-flushconfig-answers ufront: flushed config to disk \(ok\) 10
sleep 1
SCEN
cat > "$out/e.scen" <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 4
SCEN
echo "== D: flushconfig, then SIGKILL (client, Xvfb)"
STOPSIG=KILL run_client d "$out/d.scen" '' '-ini:System:[C1Flush]:Key=flushed' '-ini:User:[C1Flush]:UserKey=flushed-user'
if grep -a -q -P "^PASS\td-flushconfig-answers\t" "$out/d.result" 2>/dev/null; then result PASS d-flushconfig-answers "$(grep -a -P '^PASS\td-' "$out/d.result" | cut -f3 | cut -c1-200)"
else result FAIL d-flushconfig-answers "see $out/d.result"; fi
check d-killed-process-has-the-system-value-in-Avaris.ini expect_ini "$out/d-Avaris.ini" C1Flush Key flushed
check d-killed-process-has-the-user-value-in-User.ini expect_ini "$out/d-User.ini" C1Flush UserKey flushed-user
echo "== E: no flushconfig, then SIGKILL (client, Xvfb; informational)"
STOPSIG=KILL run_client e "$out/e.scen" '' '-ini:System:[C1Flush]:Key=notflushed'
if ini_get "$out/e-Avaris.ini" C1Flush Key > /dev/null 2>&1; then
  result PASS e-info "without flushconfig a KILLed client DID have the value in Avaris.ini (something flushed during the menu boot)"
else
  result PASS e-info "without flushconfig a KILLed client had NO value in Avaris.ini: only the exit flush or flushconfig writes an override"
fi

echo "c1-ini-overrides: $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (logs in $out)"
[ "$fails" = 0 ]
