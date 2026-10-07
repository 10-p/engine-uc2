#!/usr/bin/env bash
# M14 S3: the game on script packages rebuilt from source (tools/dev/script-build.sh), headless and repeatable.
# One PASS/FAIL line per check; the existing suites are reused, nothing is re-implemented here.
#
#   tools/dev/m14-scripts-check.sh                    every check
#   tools/dev/m14-scripts-check.sh verify census      only these checks (names below)
#
# Environment (defaults in brackets):
#   M14_SCRIPTS   directory with the rebuilt .u [<WorkUC2>/run/m14-make/System]
#   M14_TREE      run tree with them [m14-run]; M14_SHIP the same build with the shipped .u [m14-ship]
#   M14_BUILD     build directory [build-native]
#   M14_NO_MKRUN  1: use the trees as they are
#   M14_PREFIX    the name every tree of this run starts with [m14]: a D-phase task uses its own (d3-m14 makes
#                 d3-m14-run, d3-m14-ship, d3-m14-census-*, d3-m14-movies, d3-m14-net); M14_OUT the summary's
#                 directory [<WorkUC2>/logs/m14/check]
# Output: <WorkUC2>/logs/m14/check/ (summary m14-summary.txt); the suites write to <WorkUC2>/logs/<tree>.
#
# Checks:
#   verify    ucc-bin verifyclasses on the rebuilt packages: exit 0, all layouts match (Editor's included)
#   patcher   the script patcher leaves every rebuilt package alone (one "patch not applied" line per package
#             it loads) and still patches the shipped ones in the reference tree
#   menu      the client boots to the menu (Menu level up for play, 0 Critical, 0 GL errors); screenshots of both
#             trees in logs/<tree>/menu-*.png
#   deadbolt  a DM-Deadbolt match: level up for play, Fire starts it, 0 Critical, 0 GL errors, both trees
#   controls  tools/dev/m10-controls.sh, all five scenarios (M10_TREE)
#   audio     tools/dev/audio-regression.sh <tree>
#   effects   tools/dev/m7-effects.sh selftest and the effect pairs in M14_FX [bloom blur berserk decay], on both
#             trees: every pair critical=0 glerrors=0 programs_failed=0; the B pictures compared between the trees
#   census    tools/dev/map-census.sh (trees m14-census-run-N / m14-census-ship-N, ports 7790+/7795+): every map on
#             a dedicated server with the rebuilt and with the shipped packages; same result per map
#   movies    tools/dev/m9-movies.py --tree m14-movies (rebuilt packages installed by mkrun's UC2_SCRIPTS)
#   net       NET_TREE=m14-net NET_SOAK_MIN=2 tools/dev/net-test.sh dedicated2 travel (rebuilt packages)
#   leftovers no uc2.bin/ucc-bin/Xvfb running in an m14 tree
# Memory: every program inside <WorkUC2>/bin/capped; the suites keep their own ceilings (2G per game run), the
# census 8G for its 4 servers per tree (the two trees one after the other), net-test 6G.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
capped=$work/bin/capped
prefix=${M14_PREFIX:-m14}
case "$prefix" in ""|*/*|*..*) echo "bad M14_PREFIX: $prefix" >&2; exit 2;; esac
scripts=${M14_SCRIPTS:-$work/run/m14-make/System}
tree=${M14_TREE:-$prefix-run}
ship=${M14_SHIP:-$prefix-ship}
build=${M14_BUILD:-build-native}
case "$tree" in "$prefix"*) ;; *) echo "M14_TREE must start with $prefix" >&2; exit 2;; esac
case "$ship" in "$prefix"*) ;; *) echo "M14_SHIP must start with $prefix" >&2; exit 2;; esac
out=${M14_OUT:-$work/logs/m14/check}
mkdir -p "$out"
summary=$out/m14-summary.txt
: > "$summary"
sys=$work/run/$tree/System
ssys=$work/run/$ship/System
result() { echo "$1 $2: $3" | tee -a "$summary"; }
crit() { local n; n=$(grep -a -c "^Critical:" "$1" 2>/dev/null); echo "${n:-0}"; }

[ -f "$scripts/UCMenu.u" ] || { echo "no rebuilt packages in $scripts (tools/dev/script-build.sh)" >&2; exit 2; }
if [ "${M14_NO_MKRUN:-0}" != 1 ]; then
  UC2_SCRIPTS=$scripts "$repo/tools/dev/mkrun-uc2.sh" "$repo/$build" "$tree" > /dev/null 2>&1 || exit 2
  "$repo/tools/dev/mkrun-uc2.sh" "$repo/$build" "$ship" > /dev/null 2>&1 || exit 2
fi
cmp -s "$sys/UCMenu.u" "$scripts/UCMenu.u" || { echo "$sys does not hold the rebuilt packages" >&2; exit 2; }

check_verify() {
  local rc
  rm -f "$sys/ucc.log"
  ( cd "$sys" && "$capped" 2G 600 -- ./ucc-bin verifyclasses > "$out/verify.out" 2>&1 ); rc=$?
  cp -f "$sys/ucc.log" "$out/verify.log" 2>/dev/null
  if [ $rc = 0 ] && grep -a -q "VERIFY: all native class layouts match" "$out/verify.log" \
     && grep -a -q "VERIFY: Editor class layouts match" "$out/verify.log"; then
    result PASS verify "exit 0, 'VERIFY: all native class layouts match', 'VERIFY: Editor class layouts match' (rebuilt packages)"
  else
    result FAIL verify "exit $rc; see $out/verify.log"
  fi
}

shot() {	# shot <tree> <name> <secs> <fire-at> <map>
  ( cd "$repo" && tools/dev/run-uc2-shot.sh "$1" "$2" "$3" "$4" "$5" -nostartupmovies -glerrors -glshot=$(( $4 > 0 ? $4 + 15 : 25 )),$(( $4 > 0 ? $4 + 30 : 35 )) ) > "$out/$1-$2.txt" 2>&1
}
glerrs() { grep -a -o "[0-9]* GL errors so far" "$1" | tail -1 | awk '{print $1}'; }

check_menu() {
  local t f ok=1 msg=""
  for t in "$tree" "$ship"; do
    shot "$t" menu 45 0 ''
    f=$work/logs/$t/menu.out
    if grep -a -q "Bringing Level Menu.MyLevel up for play" "$f" && [ "$(crit "$f")" = 0 ] && [ "$(glerrs "$f")" = 0 ] && [ -f "$work/logs/$t/menu-1.png" ]; then
      msg="$msg $t: menu up, 0 Critical, 0 GL errors;"
    else ok=0; msg="$msg $t: FAILED (see $f);"; fi
  done
  [ $ok = 1 ] && result PASS menu "$msg screenshots logs/<tree>/menu-*.png" || result FAIL menu "$msg"
}

check_patcher() {
  local f=$work/logs/$tree/menu.out g=$work/logs/$ship/menu.out d=$work/logs/$tree/deadbolt.out na ap
  [ -f "$f" ] || shot "$tree" menu 45 0 ''
  [ -f "$g" ] || shot "$ship" menu 45 0 ''
  na=$(cat "$f" "$d" 2>/dev/null | grep -a "^ScriptPatcher: .* patch not applied" | sed 's/^ScriptPatcher: \([A-Za-z]*\).*/\1/' | sort -u | tr '\n' ' ')
  ap=$(cat "$g" "$work/logs/$ship/deadbolt.out" 2>/dev/null | grep -a "^ScriptPatcher: [A-Za-z]* patched" | sed 's/^ScriptPatcher: \([A-Za-z]*\).*/\1/' | sort -u | tr '\n' ' ')
  if [ -n "$na" ] && ! cat "$f" "$d" 2>/dev/null | grep -a -q "^ScriptPatcher: [A-Za-z]* patched" && [ -n "$ap" ]; then
    result PASS patcher "rebuilt tree: not applied to $na(none patched); shipped tree: patched $ap"
  else
    result FAIL patcher "rebuilt: not applied '$na'; shipped: patched '$ap'"
  fi
}

check_deadbolt() {
  local t f ok=1 msg=""
  for t in "$tree" "$ship"; do
    shot "$t" deadbolt 75 28 DM-Deadbolt
    f=$work/logs/$t/deadbolt.out
    if grep -a -q "Bringing Level DM-Deadbolt.MyLevel up for play" "$f" && [ "$(crit "$f")" = 0 ] && [ "$(glerrs "$f")" = 0 ] && [ -f "$work/logs/$t/deadbolt-1.png" ]; then
      msg="$msg $t: DM-Deadbolt up, Fire pressed at 28 s, 0 Critical, 0 GL errors;"
    else ok=0; msg="$msg $t: FAILED (see $f);"; fi
  done
  [ $ok = 1 ] && result PASS deadbolt "$msg screenshots logs/<tree>/deadbolt-*.png" || result FAIL deadbolt "$msg"
}

check_controls() {
  local rc
  ( cd "$repo" && M10_TREE=$tree M10_OUT=$out/controls tools/dev/m10-controls.sh ) > "$out/controls.txt" 2>&1; rc=$?
  local p f; p=$(grep -a -c "^PASS" "$out/controls.txt"); f=$(grep -a -c "^FAIL" "$out/controls.txt")
  [ $rc = 0 ] && [ "$f" = 0 ] && [ "$p" -ge 5 ] && result PASS controls "$(grep -a '^PASS' "$out/controls.txt" | cut -c1-60 | tr '\n' ';')" \
    || result FAIL controls "exit $rc, $p PASS, $f FAIL; see $out/controls.txt"
}

check_audio() {
  local rc sm=$work/logs/$tree/audio-summary.txt bad=""
  ( cd "$repo" && tools/dev/audio-regression.sh "$tree" ) > "$out/audio.txt" 2>&1; rc=$?
  grep -a -q "UC2AudioCheck: .* 0 failures" "$sm" || bad="$bad check"
  grep -a -q "UC2AudioSelfTest: 39 checks, 0 failed" "$sm" || bad="$bad selftest"
  grep -a "SUMMARY audio-menu-music" "$sm" | grep -a -q "PASS" || bad="$bad menu-music"
  for c in menu deadbolt praxis-efx praxis-dry; do
    grep -a "SUMMARY audio-$c:" "$sm" | grep -a -q "silent seconds 0;.*failed 0 (0 distinct); Critical 0" || bad="$bad $c"
  done
  [ $rc = 0 ] && [ -z "$bad" ] && result PASS audio "$(grep -a 'audio-selftest\|audio-menu-music' "$sm" | cut -c9-140 | tr '\n' ';') captures menu/deadbolt/praxis-efx/praxis-dry: 0 silent s, 0 failed cues, 0 Critical; $sm" \
    || result FAIL audio "exit $rc; failed:$bad; see $sm"
}

check_effects() {
  local t rc bad="" msg=""
  for t in "$tree" "$ship"; do
    ( cd "$repo" && tools/dev/m7-effects.sh "$t" selftest ${M14_FX:-bloom blur berserk decay} ) > "$out/effects-$t.txt" 2>&1; rc=$?
    [ $rc = 0 ] || bad="$bad $t:exit$rc"
    grep -a "^fx " "$out/effects-$t.txt" | grep -a -v "^fx selftest" | grep -a -v "critical=0 glerrors=0 programs_failed=0+0" | grep -a -q . && bad="$bad $t:fx"
    grep -a -i "selftest" "$out/effects-$t.txt" | grep -a -q " 0 failed" || bad="$bad $t:selftest"
    msg="$msg $t: $(grep -a '^fx ' "$out/effects-$t.txt" | grep -a -v -c '^fx selftest') pairs critical=0 glerrors=0 programs_failed=0+0, selftest $(grep -a -i -o '[0-9]* passed, [0-9]* failed' "$out/effects-$t.txt" | head -1);"
  done
  # The B pictures (effect on) of the two trees, mean difference per effect (animated things differ anyway).
  for f in ${M14_FX:-bloom blur berserk decay}; do
    msg="$msg $f B rebuilt-vs-shipped: $(python3 -c "
import sys
from PIL import Image, ImageChops, ImageStat
a=Image.open(sys.argv[1]).convert('RGB'); b=Image.open(sys.argv[2]).convert('RGB')
print('mean %.2f' % (sum(ImageStat.Stat(ImageChops.difference(a,b)).mean)/3))" "$work/logs/$tree/fx-$f-b.png" "$work/logs/$ship/fx-$f-b.png" 2>/dev/null);"
  done
  [ -z "$bad" ] && result PASS effects "$msg" || result FAIL effects "$bad;$msg see $out/effects-*.txt"
}

census_of() {	# census_of <name> <port> <scripts dir or ''>
  local name=$1 port=$2 scr=$3 w
  # A server that crashes while exiting after its TERM hangs (BLOCKERS.md); kill such leftovers of these trees
  # after 75 s, as m13-64bit.sh does.
  ( while sleep 10; do
      for p in $(pgrep -x ucc-bin); do
        case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$work"/run/$name-*) ;; *) continue;; esac
        age=$(ps -o etimes= -p $p 2>/dev/null | tr -d ' ')
        [ -n "$age" ] && [ "$age" -gt 75 ] && { echo "$(readlink /proc/$p/cwd)" >> "$out/census-hung.txt"; kill -KILL $p; }
      done
    done ) & w=$!
  ( cd "$repo" && UC2_SCRIPTS=$scr CENSUS_NAME=$name CENSUS_PORT=$port "$capped" 8G 1800 -- tools/dev/map-census.sh "$repo/$build" 15 4 ) > "$out/$name.out" 2>&1
  kill $w 2>/dev/null; wait $w 2>/dev/null
}

check_census() {
  local a=$prefix-census-run b=$prefix-census-ship sa sb n p diffs
  : > "$out/census-hung.txt"
  census_of $a 7790 "$scripts"
  census_of $b 7795 ""
  sa=$work/logs/$a/summary.tsv; sb=$work/logs/$b/summary.tsv
  n=$(wc -l < "$sa" 2>/dev/null); p=$(grep -c '^PASS' "$sa" 2>/dev/null)
  diffs=$(diff <(cut -f1,2 "$sa" 2>/dev/null) <(cut -f1,2 "$sb" 2>/dev/null) | grep '^[<>]' | tr '\n' ' ')
  if [ "${n:-0}" -ge 58 ] && [ "$p" = "$n" ] && [ -z "$diffs" ]; then
    result PASS census "$n maps, $p PASS with the rebuilt packages, the same per-map result as the shipped ones ($(grep -c '^PASS' "$sb") PASS); hung servers killed after exit: $(grep -c . "$out/census-hung.txt"); $sa"
  else
    result FAIL census "maps ${n:-0}, PASS ${p:-0}; differences to the shipped packages: ${diffs:-none}; $sa"
  fi
}

check_movies() {
  local rc
  ( cd "$repo" && UC2_SCRIPTS=$scripts python3 tools/dev/m9-movies.py --tree $prefix-movies ) > "$out/movies.txt" 2>&1; rc=$?
  local f p; f=$(grep -a -c "^FAIL" "$out/movies.txt"); p=$(grep -a -c "^PASS" "$out/movies.txt")
  cmp -s "$work/run/$prefix-movies/System/UCMenu.u" "$scripts/UCMenu.u" || { result FAIL movies "the $prefix-movies tree does not hold the rebuilt packages"; return; }
  [ $rc = 0 ] && [ "$f" = 0 ] && [ "$p" -gt 0 ] && result PASS movies "$p PASS, 0 FAIL (rebuilt packages); $out/movies.txt" \
    || result FAIL movies "exit $rc, $p PASS, $f FAIL; see $out/movies.txt"
}

check_net() {
  local rc
  ( cd "$repo" && UC2_SCRIPTS=$scripts NET_TREE=$prefix-net NET_SOAK_MIN=2 NET_PORT=7860 tools/dev/net-test.sh dedicated2 travel ) > "$out/net.txt" 2>&1; rc=$?
  local f p; f=$(grep -a -c "^FAIL" "$out/net.txt"); p=$(grep -a -c "^PASS" "$out/net.txt")
  cmp -s "$work/run/$prefix-net/System/UCMenu.u" "$scripts/UCMenu.u" || { result FAIL net "the $prefix-net tree does not hold the rebuilt packages"; return; }
  [ $rc = 0 ] && [ "$f" = 0 ] && result PASS net "$p PASS, 0 FAIL (dedicated2 travel, rebuilt packages); $work/logs/$prefix-net/net-summary.txt" \
    || result FAIL net "exit $rc, $p PASS, $f FAIL; see $out/net.txt"
}

check_leftovers() {
  local left="" p
  for p in $(pgrep -x uc2.bin; pgrep -x ucc-bin; pgrep -x Xvfb); do
    case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$work"/run/"$prefix"*) left="$left $p";; esac
  done
  [ -z "$left" ] && result PASS leftovers "no uc2.bin/ucc-bin/Xvfb running in a $prefix tree" || result FAIL leftovers "still running:$left"
}

checks=${*:-verify menu deadbolt patcher controls audio effects census movies net leftovers}
for c in $checks; do "check_$c"; done
echo "summary: $summary"
