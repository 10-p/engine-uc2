#!/usr/bin/env bash
# The audio regression suite of M8, for later milestones to rerun: no ears and no Xbox needed.
#
#   tools/dev/audio-regression.sh <tree>        (a run tree made by tools/dev/mkrun-uc2.sh, e.g. m8)
#
#  1. ucc-bin UC2ALAudio.UC2AudioCheck      every cooked wave decodes to its length, every cue resolves
#  2. ucc-bin UC2ALAudio.UC2AudioSelfTest   gain/pan/pitch/loops/playlists/reverb against the XACT numbers
#  3. captures through OpenAL Soft's wave writer (tools/dev/run-uc2-audio.sh), fixed names:
#       audio-menu          the main menu, 60 s
#       audio-deadbolt      DM-Deadbolt, 3 bots, first PlayerStart, Fire at 28 s, walk/jump/fire, 85 s
#       audio-praxis-efx    DM-Praxis (has reverb zones), same inputs plus "Ghost" after the spawn (the walk crosses
#                           zones: Underwater and back), EFX reverb on
#       audio-praxis-dry    the same with -NOEFX
# Output: <WorkUC2>/logs/<tree>/audio-*, and one line per item in <WorkUC2>/logs/<tree>/audio-summary.txt.
# Everything runs inside <WorkUC2>/bin/capped (2G each, one at a time).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?tree}
sys=$work/run/$tree/System
out=$work/logs/$tree
mkdir -p "$out"
summary=$out/audio-summary.txt
: > "$summary"

( cd "$sys" && "$work/bin/capped" 2G 600 -- ./ucc-bin UC2ALAudio.UC2AudioCheck > "$out/audio-check.out" 2>&1 )
echo "SUMMARY audio-check: $(grep -a '^wave banks:' "$out/audio-check.out" | sed 's/^wave banks: //'); $(grep -a '^cues \[int\]' "$out/audio-check.out"); $(grep -a '^UC2AudioCheck:' "$out/audio-check.out")" | tee -a "$summary"

( cd "$sys" && "$work/bin/capped" 2G 900 -- ./ucc-bin UC2ALAudio.UC2AudioSelfTest > "$out/audio-selftest.out" 2>&1 )
echo "SUMMARY audio-selftest: $(grep -a '^UC2AudioSelfTest:' "$out/audio-selftest.out" | tail -1)" | tee -a "$summary"
grep -a '^FAIL' "$out/audio-selftest.out" | sort -u | sed 's/^/  /' | tee -a "$summary"

run() { "$repo/tools/dev/run-uc2-audio.sh" "$@" > /dev/null 2>&1; }
run "$tree" audio-menu 60 0 ''
UC2_PRECMD="set UCDeathmatch NumPlayers 0" run "$tree" audio-deadbolt 85 28 'DM-Deadbolt?NumBots=3'
UC2_PRECMD="set UCDeathmatch NumPlayers 0" UC2_POSTCMD="Ghost" run "$tree" audio-praxis-efx 85 28 'DM-Praxis?NumBots=3'
UC2_PRECMD="set UCDeathmatch NumPlayers 0" UC2_POSTCMD="Ghost" run "$tree" audio-praxis-dry 85 28 'DM-Praxis?NumBots=3' -NOEFX
for n in audio-menu audio-deadbolt audio-praxis-efx audio-praxis-dry; do
  cat "$out/$n.summary.txt" 2>/dev/null || echo "SUMMARY $n: no result"
done | tee -a "$summary"
for p in $(pgrep -x uc2.bin) $(pgrep -x ucc-bin) $(pgrep -x Xvfb); do
  case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"*) echo "WARNING: process $p of this run tree still running";; esac
done
