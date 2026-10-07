#!/usr/bin/env bash
# M13: the 64-bit build checked end to end, headless and repeatable. One PASS/FAIL line per check.
#
#   tools/dev/m13-64bit.sh                     every check, against the 64-bit tree run/m13 (and run/m13-32)
#   tools/dev/m13-64bit.sh verify census       only these checks (names below)
#   M13_TREE=m13-uchar M13_BUILD=build-native64-uchar tools/dev/m13-64bit.sh     the -funsigned-char build
#
# Environment (defaults in brackets):
#   M13_TREE      the 64-bit run tree under <WorkUC2>/run [m13]; rebuilt from M13_BUILD unless M13_NO_MKRUN=1
#   M13_BUILD     its build directory [build-native64]
#   M13_REF       the 32-bit reference tree [m13-32], rebuilt from M13_REFBUILD [build-native] if missing
#   M13_SOAK      soak length in seconds [1200]
#   M13_FX        the m7-effects.sh effects to run [all]
# Output: <WorkUC2>/logs/m13 (the m13 tree) or <WorkUC2>/logs/m13/<tree> (others; <WorkUC2>/logs/<tree> is then
# a link there, because the suites write to logs/<tree>), stable names, PASS/FAIL lines also in m13-summary.txt.
#
# Every program runs inside <WorkUC2>/bin/capped (CLAUDE.md "Running anything"): the suites keep their own
# ceilings (2G per game run: a 64-bit game measured below that, docs/porting/analysis/64bit.md), ucc-bin
# verifyclasses 3G, the census 8G for its 4 servers, the soak 3G. Nothing is left running (checked last).
#
# Checks (existing suites reused; nothing is re-implemented here but the comparisons):
#   verify     ucc-bin verifyclasses (64-bit mode): exit 0, all layouts match, 0 link-time mismatches, 0 bytecode
#              references to placeholders; tools/dev/m13-layout-check.py against the binary's DWARF: the same
#              mismatch lines as the 32-bit reference build (pre-existing ones), nothing new
#   census     tools/dev/map-census.sh on the 64-bit and the 32-bit build: 58/58 and the same result per map
#   controls   tools/dev/m10-controls.sh, all five scenarios
#   audio      tools/dev/audio-regression.sh: check, self-test 39/0, the menu music level, four captures
#   movies     tools/dev/movie-selftest.sh 96/0 and tools/dev/m9-movies.py
#   glselftest tools/dev/m7-effects.sh selftest (the -glselftest primitives) 10/10
#   effects    tools/dev/m7-effects.sh: every effect critical=0 glerrors=0 programs_failed=0+0
#   pinned     the pinned DM-Deadbolt view (tools/dev/m12/deadbolt-pinned.scen) on the 64-bit and the 32-bit
#              tree, post-processing off: same position/rotation, static part >= 99.9% within 2 levels
#              (tools/dev/pinned-compare.py, as m12-wine-smoke.sh's pinned check)
#   psh2glsl   python3 tools/psh2glsl.py --selftest
#   saves      `ucc-bin profiletest`: a profile saved by each build is byte-identical to the other's, and each
#              build loads the other's file back to the values it was saved with (ULoadSaveManager, the game's path)
#   soak       M13_SOAK s: menu -> DM-Deadbolt with 4 bots -> menu -> DM-Azure with 4 bots; 0 Critical, the
#              resident size sampled every 30 s does not grow over the second map (flat RSS)
#   base32     (not in the default list) the 32-bit build after M13, tree M13_REF: verifyclasses as before,
#              controls keyboard+pad, audio self-test 39/0, movie self-test 96/0, glselftest 10/10, and the pinned
#              picture against main's 32-bit build (M13_BASEBUILD, default <WorkUC2>/engine-uc2/build-native, read
#              only, copied into run tree M13_BASE=m13-main32): same view, static part >= 99.9% within 2 levels
#   asan       (not in the default list) the native64-asan build (tree m13-asan): verifyclasses, a server with
#              bots, the menu and a map with bots; 0 AddressSanitizer reports; UBSan sites listed
#   leftovers  no uc2.bin/ucc-bin/Xvfb of the m13 trees running
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
capped=$work/bin/capped
tree=${M13_TREE:-m13}
build=${M13_BUILD:-build-native64}
ref=${M13_REF:-m13-32}
refbuild=${M13_REFBUILD:-build-native}
soak=${M13_SOAK:-1200}
case "$tree" in m13*) ;; *) echo "M13_TREE must start with m13" >&2; exit 2;; esac
if [ "$tree" = m13 ]; then out=$work/logs/m13; else out=$work/logs/m13/$tree; fi
mkdir -p "$out"
# The suites write to <WorkUC2>/logs/<tree>: for other trees that is a link into logs/m13.
if [ "$tree" != m13 ]; then
  [ -e "$work/logs/$tree" ] && [ ! -L "$work/logs/$tree" ] && { echo "$work/logs/$tree exists and is not a link" >&2; exit 2; }
  ln -sfn "$out" "$work/logs/$tree"
fi
summary=$out/m13-summary.txt
sys=$work/run/$tree/System
rsys=$work/run/$ref/System
: > "$summary"

result() { echo "$1 $2: $3" | tee -a "$summary"; }
crit() { grep -a -c "Critical" "$1" 2>/dev/null || true; }

if [ "${M13_NO_MKRUN:-0}" != 1 ]; then
  "$repo/tools/dev/mkrun-uc2.sh" "$repo/$build" "$tree" > /dev/null || exit 2
fi
[ -x "$rsys/ucc-bin" ] || "$repo/tools/dev/mkrun-uc2.sh" "$repo/$refbuild" "$ref" > /dev/null || exit 2
file "$sys/ucc-bin" | grep -q 'ELF 64-bit' || { echo "$sys/ucc-bin is not a 64-bit binary" >&2; exit 2; }

# verifyclasses DUMP= + DWARF comparison of one tree; writes <prefix>.{verify.out,log,layout.txt,dwarf.txt,check.txt}
layout_of() {	# layout_of <System dir> <ucc-bin path in the build> <prefix> <ceiling>
  local s=$1 bin=$2 p=$3 mem=$4 rc
  rm -f "$s/ucc.log"
  ( cd "$s" && "$capped" "$mem" 600 -- ./ucc-bin verifyclasses DUMP="$p.layout.txt" > "$p.verify.out" 2>&1 ); rc=$?
  cp -f "$s/ucc.log" "$p.log" 2>/dev/null
  "$capped" 4G 900 -- python3 "$repo/tools/dev/dwarf-layouts.py" "$bin" > "$p.dwarf.txt" 2>/dev/null
  python3 "$repo/tools/dev/m13-layout-check.py" "$p.layout.txt" "$p.dwarf.txt" > "$p.check.txt" 2>&1
  return $rc
}

check_verify() {
  local rc p=$out/verify64 r=$out/verify32 new
  layout_of "$sys" "$repo/$build/Source/UCC/ucc-bin" "$p" 3G; rc=$?
  layout_of "$rsys" "$repo/$refbuild/Source/UCC/ucc-bin" "$r" 2G > /dev/null
  # Mismatch lines without their numbers, 64-bit ones the 32-bit build does not have.
  new=$(LC_ALL=C comm -23 <(grep -a '^SIZE\|^OFFSET' "$p.check.txt" | sed -E 's/:.*//' | LC_ALL=C sort -u) \
                 <(grep -a '^SIZE\|^OFFSET' "$r.check.txt" | sed -E 's/:.*//' | LC_ALL=C sort -u))
  local nat scan ph m64 m32
  nat=$(grep -a -o "64-bit native size check: [0-9]* classes, [0-9]* mismatches while linking" "$p.log")
  scan=$(grep -a -o "bytecode scan: [0-9]* scripts read" "$p.log")
  ph=$(grep -a -c "VERIFY: placeholder .* offset" "$p.log")
  m64=$(grep -a -o "layout-check: [0-9]* mismatches" "$p.check.txt"); m32=$(grep -a -o "layout-check: [0-9]* mismatches" "$r.check.txt")
  if [ $rc = 0 ] && grep -a -q "VERIFY: all native class layouts match" "$p.log" && [[ "$nat" == *", 0 mismatches while linking" ]] \
     && ! grep -a -q "refers to native placeholder\|placeholder .* NOT FOUND" "$p.log" && [ -n "$scan" ] && [ -z "$new" ]; then
    result PASS verify "exit 0, 'VERIFY: all native class layouts match'; $nat; $ph placeholders linked at their C++ size; $scan, 0 placeholder references; DWARF $m64 at 64-bit, $m32 at 32-bit, none new"
  else
    result FAIL verify "exit $rc; $nat; $scan; new DWARF mismatches: $(echo $new); see $p.log $p.check.txt"
  fi
}

check_census() {
  local c64=m13-census-$tree c32=m13-census-$ref s64 s32 n64 p64 diffs
  # A server that crashes while exiting after its TERM hangs (BLOCKERS.md: exit-time crash) and map-census.sh's
  # timeout has no -k: kill such a server of these trees 60 s after its TERM and count it.
  : > "$out/census-hung.txt"
  ( while :; do
      for p in $(pgrep -x ucc-bin); do
        case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$work"/run/m13-census-*) ;; *) continue;; esac
        local age; age=$(ps -o etimes= -p $p 2>/dev/null | tr -d ' ')
        [ -n "$age" ] && [ "$age" -gt 75 ] && { echo "$(tr '\0' ' ' < /proc/$p/cmdline) in $(readlink /proc/$p/cwd)" >> "$out/census-hung.txt"; kill -KILL $p; }
      done; sleep 10; done ) &
  local watcher=$!
  for c in "$c64:$build:7880" "$c32:$refbuild:7890"; do
    local name=${c%%:*} rest=${c#*:}; local b=${rest%%:*} port=${rest#*:}
    mkdir -p "$out/$name"; [ -L "$work/logs/$name" ] || { rm -rf "$work/logs/$name"; ln -sfn "$out/$name" "$work/logs/$name"; }
    CENSUS_NAME=$name CENSUS_PORT=$port "$capped" 8G 1800 -- "$repo/tools/dev/map-census.sh" "$repo/$b" 15 4 > "$out/$name.out" 2>&1
  done
  kill $watcher 2>/dev/null; wait $watcher 2>/dev/null
  s64=$out/$c64/summary.tsv; s32=$out/$c32/summary.tsv
  n64=$(wc -l < "$s64" 2>/dev/null || echo 0); p64=$(grep -c '^PASS' "$s64" 2>/dev/null || echo 0)
  diffs=$(diff <(cut -f1,2 "$s64" 2>/dev/null) <(cut -f1,2 "$s32" 2>/dev/null) | grep '^[<>]' | tr '\n' ' ')
  if [ "$n64" = 58 ] && [ "$p64" = 58 ] && [ -z "$diffs" ]; then
    result PASS census "58 maps, 58 PASS at 64-bit, the same per-map result as the 32-bit build ($(grep -c '^PASS' "$s32") PASS); servers hung on exit and killed: $(grep -E -c "m13-census-$tree-[0-9]+/" "$out/census-hung.txt") at 64-bit, $(grep -E -c "m13-census-$ref-[0-9]+/" "$out/census-hung.txt") at 32-bit (BLOCKERS.md); $s64"
  else
    result FAIL census "maps $n64, PASS $p64; differences to 32-bit: ${diffs:-none}; $s64"
  fi
}

check_controls() {
  local lines
  lines=$(M10_TREE=$tree M10_OUT=$out/controls "$repo/tools/dev/m10-controls.sh" 2>&1 | grep -a '^PASS\|^FAIL\|WARNING')
  echo "$lines" > "$out/controls.txt"
  if [ "$(echo "$lines" | grep -c '^PASS')" = 5 ] && ! echo "$lines" | grep -q '^FAIL\|WARNING'; then
    result PASS controls "$(echo "$lines" | sed -E 's/ \(([0-9]+) checks.*/ \1/' | tr '\n' ',' | sed 's/,$//') (scenario: checks), 0 Critical"
  else
    result FAIL controls "$(echo "$lines" | tr '\n' ' ')"
  fi
}

check_audio() {
  "$repo/tools/dev/audio-regression.sh" "$tree" > "$out/audio-regression.out" 2>&1
  local s=$out/audio-summary.txt ok=1
  grep -a -q "UC2AudioCheck: .* 0 failures\|UC2AudioCheck:.*0 fail" "$s" || ok=0
  grep -a -q "UC2AudioSelfTest: 39 checks, 0 failed" "$s" || ok=0
  grep -a -q "^SUMMARY audio-menu-music: .*PASS" "$s" || ok=0
  for n in audio-menu audio-deadbolt audio-praxis-efx audio-praxis-dry; do
    grep -a "^SUMMARY $n:" "$s" | grep -a -q "silent seconds 0;.*failed 0 (0 distinct); Critical 0" || ok=0
  done
  grep -a -q "WARNING" "$out/audio-regression.out" && ok=0
  if [ $ok = 1 ]; then
    result PASS audio "$(grep -a -o 'UC2AudioSelfTest: [^;]*' "$s"); $(grep -a -o 'mix gain [-0-9.]* dB' "$s" | head -1) (expected -19.00); 4 captures: 0 silent seconds, 0 failed cues, 0 Critical; $s"
  else
    result FAIL audio "see $s"
  fi
}

check_movies() {
  "$repo/tools/dev/movie-selftest.sh" "$tree" > "$out/movie-selftest.out" 2>&1
  local st; st=$(grep -a "UC2MovieSelfTest:" "$out/movie-selftest.out" | tail -1)
  python3 "$repo/tools/dev/m9-movies.py" --build "$repo/$build" --tree "$tree" --no-mkrun > "$out/m9-movies.out" 2>&1
  local np nf; np=$(grep -a -c '^PASS' "$out/m9-movies.out"); nf=$(grep -a -c '^FAIL' "$out/m9-movies.out")
  if [[ "$st" == *"96 checks, 0 failed"* ]] && [ "$nf" = 0 ] && [ "$np" -gt 0 ]; then
    result PASS movies "$st; m9-movies.py $np PASS, 0 FAIL"
  else
    result FAIL movies "${st:-no self-test line}; m9-movies.py $np PASS, $nf FAIL ($(grep -a '^FAIL' "$out/m9-movies.out" | cut -c1-80 | tr '\n' ' '))"
  fi
}

check_glselftest() {
  local l; l=$("$repo/tools/dev/m7-effects.sh" "$tree" selftest 2>&1 | grep -a "selftest summary")
  if [[ "$l" == *"15 passed, 0 failed"* ]]; then result PASS glselftest "$l"; else result FAIL glselftest "${l:-no summary line}"; fi
}

check_effects() {
  local lines bad
  lines=$("$repo/tools/dev/m7-effects.sh" "$tree" ${M13_FX:-} 2>&1 | grep -a '^fx ' | grep -a -v 'selftest')
  echo "$lines" > "$out/effects.txt"
  bad=$(echo "$lines" | grep -a -v 'critical=0 glerrors=0 programs_failed=0+0' | cut -d: -f1 | tr '\n' ' ')
  if [ -n "$lines" ] && [ -z "$bad" ]; then
    result PASS effects "$(echo "$lines" | wc -l) effects, every one critical=0 glerrors=0 programs_failed=0+0 ($out/effects.txt)"
  else
    result FAIL effects "failing: ${bad:-no lines}"
  fi
}

check_pinned() {
  local t name lp wp vw vl cmp errs
  for t in "$tree" "$ref"; do
    rm -rf "$out/pinned-$t"
    M10_URL='DM-Deadbolt?NumBots=0' M10_TREE=$t M10_OUT=$out/pinned-$t \
      "$repo/tools/dev/m10-controls.sh" --file "$repo/tools/dev/m12/deadbolt-pinned.scen" pinned -glerrors -nopostprocess > /dev/null 2>&1
  done
  lp=$out/pinned-$ref/pinned-pinned.png; wp=$out/pinned-$tree/pinned-pinned.png
  vw=$(grep -a "^probe view" "$out/pinned-$tree/pinned.out" | grep -a -o "loc=([^)]*) rot=([^)]*)")
  vl=$(grep -a "^probe view" "$out/pinned-$ref/pinned.out" | grep -a -o "loc=([^)]*) rot=([^)]*)")
  errs=$(grep -a -o "UC2GLDrv: [0-9]* GL errors in [0-9]* frames" "$out/pinned-$tree/pinned.out" | tail -1)
  cmp=$(python3 "$repo/tools/dev/pinned-compare.py" "$lp" "$wp" "$out/pinned-diff.png" 2>&1)
  set -- $cmp
  if [ "${1-}" = OK ] && [ -n "$vw" ] && [ "$vw" = "$vl" ] && [[ "$errs" == "UC2GLDrv: 0 GL errors in "* ]] \
     && awk -v s="$2" 'BEGIN{exit !(s>=99.9)}' && [ "$(crit "$out/pinned-$tree/pinned.out")" = 0 ]; then
    result PASS pinned "64-bit and 32-bit at the same $vw; static part ${2}% within 2 levels, whole frame ${3}% bit-identical, PSNR ${4} dB (pinned-diff.png); 64-bit ${errs#UC2GLDrv: }, Critical 0"
  else
    result FAIL pinned "64 '$vw' vs 32 '$vl'; comparison: $cmp; '${errs:-no GL summary}'"
  fi
}

check_psh2glsl() {
  local l; l=$(python3 "$repo/tools/psh2glsl.py" --selftest 2>&1 | tail -1)
  if [[ "$l" == *" 0 failed"* ]]; then result PASS psh2glsl "$l"; else result FAIL psh2glsl "$l"; fi
}

check_soak() {
  local half=$(( (soak - 120) / 2 )) scen=$out/soak.scen rss=$out/soak-rss.tsv pid t0
  cat > "$scen" <<SCEN
# M13 soak (generated by tools/dev/m13-64bit.sh): menu, two maps with bots, back to the menu in between.
wait Bringing Level Menu.MyLevel up for play 120
sleep 10
chan exec open DM-Deadbolt?NumBots=4
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 8
focus
click 1 1.0
sleep 3
chan exec God
expect soak-map1-up test: exec God 10
sleep $half
chan exec disconnect
expect soak-menu-again Bringing Level Menu.MyLevel up for play 120
sleep 15
chan exec open DM-Azure?NumBots=4
expect soak-map2-up Bringing Level DM-Azure.MyLevel up for play 120
sleep 8
focus
click 1 1.0
sleep 3
chan exec God
wait test: exec God 10
sleep $half
probe end
check soak-alive P['end'].level=='DM-Azure'
SCEN
  : > "$rss"
  # Resident size of this tree's uc2.bin every 30 s, from outside the game.
  ( t0=$(date +%s); while :; do
      for p in $(pgrep -x uc2.bin); do
        [ "$(readlink /proc/$p/cwd 2>/dev/null)" = "$sys" ] || continue
        printf '%s\t%s\n' $(( $(date +%s) - t0 )) "$(awk '/^VmRSS/{print $2}' /proc/$p/status 2>/dev/null)" >> "$rss"
      done; sleep 30; done ) &
  pid=$!
  M10_TREE=$tree M10_OUT=$out/soak M10_MEM=3G M10_SECS=$((soak + 300)) "$repo/tools/dev/m10-controls.sh" --file "$scen" soak -glerrors > "$out/soak.line" 2>&1
  kill $pid 2>/dev/null; wait $pid 2>/dev/null
  local line; line=$(tail -1 "$out/soak.line")
  # Flat RSS: over the second map (from 3 min after it came up to the end) the growth stays under 5%.
  local verdict; verdict=$(python3 - "$rss" "$half" <<'PY'
import sys
rows = [tuple(map(int, l.split())) for l in open(sys.argv[1]) if len(l.split()) == 2]
half = int(sys.argv[2])
if len(rows) < 10: print("FAIL only %d samples" % len(rows)); sys.exit()
t_end = rows[-1][0]; start2 = t_end - half + 180
m2 = [r for r in rows if r[0] >= start2]
peak = max(r[1] for r in rows) / 1024.0
if len(m2) < 4: print("FAIL second map too short (%d samples)" % len(m2)); sys.exit()
a, b = m2[0][1], m2[-1][1]
g = 100.0 * (b - a) / a
print("%s RSS over the second map %.0f -> %.0f MiB (%+.1f%%), peak %.0f MiB, %d samples" % ("OK" if g < 5.0 else "FAIL", a / 1024.0, b / 1024.0, g, peak, len(rows)))
PY
)
  if [[ "$line" == PASS* ]] && [[ "$verdict" == OK* ]] && [ "$(crit "$out/soak/soak.out")" = 0 ]; then
    result PASS soak "${soak} s, two maps with 4 bots and the menu between, 0 Critical; ${verdict#OK }; $(grep -a -o 'UC2GLDrv: [0-9]* GL errors in [0-9]* frames' "$out/soak/soak.out" | tail -1)"
  else
    result FAIL soak "$line; ${verdict}; Critical $(crit "$out/soak/soak.out")"
  fi
}

check_saves() {	# a profile saved by the 64-bit build loads in the 32-bit one and the other way round
  local rc64 rc32 r a b c d f64 f32 kept
  rm -rf "$sys/Profiles/M13Save" "$rsys/Profiles/M13Save" "$sys/Profiles/M13From32" "$rsys/Profiles/M13From64"
  ( cd "$sys" && "$capped" 3G 120 -- ./ucc-bin profiletest save M13Save "$out/saves-64-saved.txt" > "$out/saves-64-save.out" 2>&1 ); rc64=$?
  ( cd "$rsys" && "$capped" 2G 120 -- ./ucc-bin profiletest save M13Save "$out/saves-32-saved.txt" > "$out/saves-32-save.out" 2>&1 ); rc32=$?
  f64=$sys/Profiles/M13Save/AVARIS_PROFILE.PRO; f32=$rsys/Profiles/M13Save/AVARIS_PROFILE.PRO
  mkdir -p "$rsys/Profiles/M13From64" "$sys/Profiles/M13From32"
  cp -f "$f64" "$rsys/Profiles/M13From64/" 2>/dev/null; cp -f "$f32" "$sys/Profiles/M13From32/" 2>/dev/null
  ( cd "$rsys" && "$capped" 2G 120 -- ./ucc-bin profiletest load M13From64 "$out/saves-32-loaded-64.txt" > "$out/saves-32-load.out" 2>&1 ); a=$?
  ( cd "$sys" && "$capped" 3G 120 -- ./ucc-bin profiletest load M13From32 "$out/saves-64-loaded-32.txt" > "$out/saves-64-load.out" 2>&1 ); b=$?
  ( cd "$sys" && "$capped" 3G 120 -- ./ucc-bin profiletest load M13Save "$out/saves-64-loaded-64.txt" > "$out/saves-64-load-own.out" 2>&1 ); c=$?
  ( cd "$rsys" && "$capped" 2G 120 -- ./ucc-bin profiletest load M13Save "$out/saves-32-loaded-32.txt" > "$out/saves-32-load-own.out" 2>&1 ); d=$?
  # What a load gives back: the same text whichever width wrote or reads the file. (Localized texts and the
  # profile/player names are not taken from the file - the loader sets them - so a load is compared with loads.)
  kept=$(( $(wc -l < "$out/saves-64-saved.txt") - $(diff "$out/saves-64-saved.txt" "$out/saves-64-loaded-64.txt" | grep -c '^<') ))
  r="64 save rc $rc64, 32 save rc $rc32, loads: 32<-64 rc $a, 64<-32 rc $b, 64<-64 rc $c, 32<-32 rc $d"
  if [ $rc64 = 0 ] && [ $rc32 = 0 ] && [ $a = 0 ] && [ $b = 0 ] && [ $c = 0 ] && [ $d = 0 ] && cmp -s "$f64" "$f32" \
     && cmp -s "$out/saves-64-saved.txt" "$out/saves-32-saved.txt" \
     && cmp -s "$out/saves-32-loaded-64.txt" "$out/saves-32-loaded-32.txt" && cmp -s "$out/saves-64-loaded-32.txt" "$out/saves-64-loaded-64.txt" \
     && cmp -s "$out/saves-32-loaded-64.txt" "$out/saves-64-loaded-64.txt"; then
    result PASS saves "profiles written by the 64-bit and the 32-bit build are byte-identical ($(stat -c %s "$f64") bytes); each build loads its own and the other's file to the same values ($kept of $(wc -l < "$out/saves-64-saved.txt") exported lines as saved; the rest are localized texts and names the loader sets)"
  else
    result FAIL saves "$r; profile files: $(cmp "$f64" "$f32" 2>&1 | head -1); see $out/saves-*"
  fi
}

check_base32() {	# gate (c): the 32-bit build after M13 behaves as before (tree M13_REF; main's build as M13_BASE)
  local o=$out/base32 base=${M13_BASE:-m13-main32} basebuild=${M13_BASEBUILD:-$work/engine-uc2/build-native} rc v ctl st mv gl cmp vb vr
  mkdir -p "$o"
  [ -x "$work/run/$base/System/uc2.bin" ] || "$repo/tools/dev/mkrun-uc2.sh" "$basebuild" "$base" > /dev/null
  ( cd "$rsys" && "$capped" 2G 300 -- ./ucc-bin verifyclasses > "$o/verify.out" 2>&1 ); rc=$?
  v=$(tail -1 "$o/verify.out")
  ctl=$(M10_TREE=$ref M10_OUT=$o/controls "$repo/tools/dev/m10-controls.sh" keyboard pad 2>&1 | grep -a '^PASS\|^FAIL\|WARNING' | tr '\n' ';')
  ( cd "$rsys" && "$capped" 2G 900 -- ./ucc-bin UC2ALAudio.UC2AudioSelfTest > "$o/audio-selftest.out" 2>&1 )
  st=$(grep -a '^UC2AudioSelfTest:' "$o/audio-selftest.out" | tail -1)
  "$repo/tools/dev/movie-selftest.sh" "$ref" > "$o/movie-selftest.out" 2>&1
  mv=$(grep -a 'UC2MovieSelfTest:' "$o/movie-selftest.out" | tail -1)
  gl=$("$repo/tools/dev/m7-effects.sh" "$ref" selftest 2>&1 | grep -a 'selftest summary')
  for t in "$ref" "$base"; do
    rm -rf "$o/pinned-$t"
    M10_URL='DM-Deadbolt?NumBots=0' M10_TREE=$t M10_OUT=$o/pinned-$t \
      "$repo/tools/dev/m10-controls.sh" --file "$repo/tools/dev/m12/deadbolt-pinned.scen" pinned -glerrors -nopostprocess > /dev/null 2>&1
  done
  vr=$(grep -a "^probe view" "$o/pinned-$ref/pinned.out" | grep -a -o "loc=([^)]*) rot=([^)]*)")
  vb=$(grep -a "^probe view" "$o/pinned-$base/pinned.out" | grep -a -o "loc=([^)]*) rot=([^)]*)")
  cmp=$(python3 "$repo/tools/dev/pinned-compare.py" "$o/pinned-$base/pinned-pinned.png" "$o/pinned-$ref/pinned-pinned.png" "$o/pinned-diff.png" 2>&1)
  set -- $cmp
  if [ $rc = 0 ] && [[ "$v" == *"All native class layouts match"* ]] && [ "$(echo "$ctl" | grep -o 'PASS' | wc -l)" = 2 ] \
     && [[ "$st" == *"39 checks, 0 failed"* ]] && [[ "$mv" == *"96 checks, 0 failed"* ]] && [[ "$gl" == *"15 passed, 0 failed"* ]] \
     && [ "${1-}" = OK ] && [ -n "$vr" ] && [ "$vr" = "$vb" ] && awk -v s="$2" 'BEGIN{exit !(s>=99.9)}'; then
    result PASS base32 "32-bit after M13: verifyclasses '$v'; controls $ctl $st; $mv; $gl; pinned vs main's build ($base) at the same $vr: static part ${2}% within 2 levels, whole frame ${3}% bit-identical"
  else
    result FAIL base32 "verify rc $rc '$v'; controls $ctl; '$st'; '$mv'; '$gl'; pinned '$vr' vs '$vb': $cmp"
  fi
}

check_asan() {	# not in the default list: the sanitizer build (preset native64-asan) on a menu and a map
  local t=${M13_ASAN_TREE:-m13-asan} b=${M13_ASAN_BUILD:-build-native64-asan} o=$out/asan s n asan ub
  s=$work/run/$t/System; mkdir -p "$o"
  "$repo/tools/dev/mkrun-uc2.sh" "$repo/$b" "$t" > /dev/null || { result FAIL asan "no tree from $b"; return; }
  export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:alloc_dealloc_mismatch=0:abort_on_error=0:halt_on_error=0}
  export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=0}
  ( cd "$s" && "$capped" 6G 900 -- ./ucc-bin verifyclasses > "$o/verify.out" 2>&1 )
  ( cd "$s" && "$capped" 6G 120 -- timeout -s INT 60 ./ucc-bin Engine.ServerCommandlet 'DM-Deadbolt?NumBots=4' -log > "$o/server.out" 2>&1 )
  ( cd "$s" && "$capped" 6G 200 -- xvfb-run -a -s "-screen 0 1280x720x24" timeout -s INT 90 ./uc2.bin -log -nostartupmovies -glerrors > "$o/menu.out" 2>&1 )
  ( cd "$s" && "$capped" 6G 300 -- xvfb-run -a -s "-screen 0 1280x720x24" timeout -s INT 150 ./uc2.bin 'DM-Deadbolt?NumBots=4' -log -nostartupmovies -glerrors > "$o/map.out" 2>&1 )
  asan=$(cat "$o"/*.out | grep -a -c 'ERROR: AddressSanitizer')
  ub=$(cat "$o"/*.out | grep -a 'runtime error:' | sed -E 's/^.*\/Source\///' | sort -u | tee "$o/ubsan-unique.txt" | wc -l)
  n=$(grep -a -h -c 'Bringing Level' "$o"/server.out "$o"/menu.out "$o"/map.out | paste -sd+ | bc)
  if [ "$asan" = 0 ] && grep -a -q 'all native class layouts match' "$o/verify.out" "$s/ucc.log" 2>/dev/null; then
    result PASS asan "ASan: 0 reports over verifyclasses, a 60 s server, the menu and DM-Deadbolt with bots ($n level loads); UBSan: $ub distinct runtime-error sites ($o/ubsan-unique.txt)"
  else
    result FAIL asan "ASan reports: $asan; UBSan sites: $ub; see $o"
  fi
}

check_leftovers() {
  local left="" p
  for p in $(pgrep -x uc2.bin) $(pgrep -x ucc-bin) $(pgrep -x Xvfb); do
    case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$work"/run/m13*) left="$left $p";; esac
  done
  if [ -z "$left" ]; then result PASS leftovers "no uc2.bin/ucc-bin/Xvfb of the m13 trees running"; else result FAIL leftovers "still running:$left"; fi
}

checks=${*:-verify census controls audio movies glselftest effects pinned psh2glsl saves soak leftovers}
for c in $checks; do
  type "check_$c" > /dev/null 2>&1 || { echo "unknown check: $c" >&2; continue; }
  "check_$c"
done
