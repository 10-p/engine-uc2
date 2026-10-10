#!/usr/bin/env bash
# The audio regression suite of M8, for later milestones to rerun: no ears and no Xbox needed.
#
#   tools/dev/audio-regression.sh <tree>        (a run tree made by tools/dev/mkrun-uc2.sh, e.g. m8)
#
#  1. ucc-bin UC2ALAudio.UC2AudioCheck      every cooked wave decodes to its length, every cue resolves (M16: also
#                                           every shipped .xwb entry and .xsb, on a tree made with UC2_XBOX_SOUNDS)
#  2. ucc-bin UC2ALAudio.UC2AudioSelfTest   gain/pan/pitch/loops/playlists/reverb against the XACT numbers
#  3. captures through OpenAL Soft's wave writer (tools/dev/run-uc2-audio.sh), fixed names:
#       audio-menu          the main menu, 60 s
#       audio-deadbolt      DM-Deadbolt, 3 bots, first PlayerStart, Fire at 28 s, walk/jump/fire, 85 s
#       audio-praxis-efx    DM-Praxis (has reverb zones), same inputs plus "Ghost" after the spawn (the walk crosses
#                           zones: Underwater and back), EFX reverb on
#       audio-praxis-dry    the same with UseEFX=False (set with -ini:)
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
# (M16: on a tree with the shipped Xbox banks, the .xwb/.xsb lines of the check too.)
echo "SUMMARY audio-check: $(grep -a -E '^(Xbox )?wave banks:' "$out/audio-check.out" | sed 's/^wave banks: //' | tr '\n' ';' | sed 's/;$//'); $(grep -a '^Xbox sound banks:' "$out/audio-check.out" | sed 's/$/; /')$(grep -a '^cues \[int\]' "$out/audio-check.out"); $(grep -a '^UC2AudioCheck:' "$out/audio-check.out")" | tee -a "$summary"

( cd "$sys" && "$work/bin/capped" 2G 900 -- ./ucc-bin UC2ALAudio.UC2AudioSelfTest > "$out/audio-selftest.out" 2>&1 )
echo "SUMMARY audio-selftest: $(grep -a '^UC2AudioSelfTest:' "$out/audio-selftest.out" | tail -1)" | tee -a "$summary"
grep -a '^FAIL' "$out/audio-selftest.out" | sort -u | sed 's/^/  /' | tee -a "$summary"

run() { "$repo/tools/dev/run-uc2-audio.sh" "$@" > /dev/null 2>&1; }
run "$tree" audio-menu 60 0 ''
UC2_PRECMD="set UCDeathmatch NumPlayers 0" run "$tree" audio-deadbolt 85 28 'DM-Deadbolt?NumBots=3'
UC2_PRECMD="set UCDeathmatch NumPlayers 0" UC2_POSTCMD="Ghost" run "$tree" audio-praxis-efx 85 28 'DM-Praxis?NumBots=3'
UC2_PRECMD="set UCDeathmatch NumPlayers 0" UC2_POSTCMD="Ghost" run "$tree" audio-praxis-dry 85 28 'DM-Praxis?NumBots=3' "-ini:System:[UC2ALAudio.UC2ALAudioSubsystem]:UseEFX=False"
# The menu music is NuBattle at -900 mB (A_Music) - 600 mB (voice headroom) + 1000*log10(0.40) (the default
# profile's MusicVolume, XACT's category formula) = -19.0 dB; another track as control.
python3 "$repo/tools/dev/audio-music-check.py" "$out/audio-menu.wav" "$work/epic-sources/Music/NuBattle.wav" -19.0 \
  "$work/epic-sources/Music/Recoil.wav" | sed 's/^CHECK/SUMMARY audio-menu-music:/' | tee -a "$summary"
for n in audio-menu audio-deadbolt audio-praxis-efx audio-praxis-dry; do
  cat "$out/$n.summary.txt" 2>/dev/null || echo "SUMMARY $n: no result"
done | tee -a "$summary"
for p in $(pgrep -x uc2.bin) $(pgrep -x ucc-bin) $(pgrep -x Xvfb); do
  case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$sys"*) echo "WARNING: process $p of this run tree still running";; esac
done
