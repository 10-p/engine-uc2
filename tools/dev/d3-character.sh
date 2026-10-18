#!/usr/bin/env bash
# Character check (native): a launch straight into a map plays the active profile's character; [DefaultPlayer] Class only
# without a profile. Also the web preview's flat profile files (Profiles/<name>.PRO) moved into Epic's folder.
#
#   tools/dev/d3-character.sh
#
# Needs the native run tree <repo>/../run/g7c (tools/dev/mkrun-uc2.sh build-native g7c; D3C_TREE=<name> to change).
# Every run is one client under Xvfb inside <repo>/../bin/capped 2G (through tools/dev/m10-controls.sh --file); every
# run after c0 starts a new process on the same tree (M10_KEEP=1), i.e. a restart. "Direct" = the map is the game's
# first argument, as the web launcher's Play form starts it (an absolute travel: no menu builds the URL).
# Output in <repo>/../logs/g7/character/ (D3C_OUT=<dir>): c<N>.out (engine log), c<N>.result, User.ini after each run;
# one PASS/FAIL line per check on stdout.
#
#   c0   fresh tree, no profile, direct DM-Deadbolt with -ini:User:[DefaultPlayer]:Class=<Z> (Malcolm): plays Z.
#   c1   menus: create profile "Alpha" (on-screen keyboard), Instant Action, two Rights on the character page (X), play.
#   c1m  direct: plays X (Alpha's).                       User.ini [DefaultPlayer] Class stays Z after c1 and c1m.
#   c2   menus: starts in Alpha; create "Bravo", Instant Action, its character page left as it is (Y: a new
#        profile's default, Anubis; a fresh profile has two characters unlocked, Anubis and Sapphire, and Right
#        stops at Sapphire: run 2), play.
#   c2m  direct: plays Y (Bravo's).
#   c3   menus: starts in Bravo; select Alpha on the Profiles page; quit without playing (the next direct launch
#        must not take [DefaultPlayer] Class, the class of the last map played = Bravo's).
#   c3m  direct: plays X (Alpha's).
#   c4m  Profiles/ removed (no profile at all): direct, plays Z (the ini's class, kept through the profile sessions).
#   c5m  the web preview's flat files: Profiles/Alpha.PRO alone (moved into Profiles/Alpha/, then loaded: plays X),
#        and Profiles/Bravo.PRO beside Profiles/Bravo/ (an older copy: deleted, the folder untouched).
# Evidence: `getall PlayerController PawnClass` in the match, the engine's "Profiles: ..." lines (UC2Profiles.cpp).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${D3C_TREE:-g7c}
root=$work/run/$tree
sys=$root/System
out=${D3C_OUT:-$work/logs/g7/character}
m10=$repo/tools/dev/m10-controls.sh
Z=UCCharacters.UCPawn_Human_Malcolm
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }

fails=0
result() { echo "$1 $2: $3"; [ "$1" = PASS ] || fails=$((fails+1)); }
check() { local n=$1 d; shift; if d=$("$@" 2>&1); then result PASS "$n" "$d"; else result FAIL "$n" "$d"; fi; }
ini() { awk -F= -v sec="[$1]" -v key="$2" '/^\[/{s=($0==sec)} s&&$1==key{v=substr($0,length(key)+2)} END{print v}' "$sys/User.ini" 2>/dev/null | tr -d '\r'; }
# pawnclass <run>: the human's PawnClass in that run's last answer (Class'<pkg>.<name>' -> <pkg>.<name>).
pawnclass() { grep -a "PlayerController .*\.PawnClass = " "$out/$1.out" | tail -1 | sed "s/.* = //; s/^Class'//; s/'.*//" | tr -d '\r'; }
profchar() { grep -a "PlayerProfile Package.PlayerProfile.CharacterClassName = " "$out/$1.out" | tail -1 | sed 's/.* = //' | tr -d '\r'; }
active_first() { grep -a "PlayerProfile Package.PlayerProfile.ProfileName = " "$out/$1.out" | head -1 | sed 's/.* = //' | tr -d '\r'; }

run() {  # run <name> <seconds> [uc2.bin args...]  (scenario in $out/<name>.scen)
  local n=$1 s=$2; shift 2
  echo "== $n"
  M10_KEEP=1 M10_TREE=$tree M10_OUT=$out M10_SECS=$s "$m10" --file "$out/$n.scen" "$n" "$@" | sed 's/^/    /'
  cp -f "$sys/User.ini" "$out/$n-User.ini" 2>/dev/null
  check "$n-scenario-checks" bash -c "! grep -a -q '^FAIL' '$out/$n.result' && grep -a -c '^PASS' '$out/$n.result'"
}

# A direct start into DM-Deadbolt: wait for the match, ask who plays whom.
direct_scen() {  # direct_scen <run>
  cat <<SCEN
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
focus
chan exec getall PlayerProfile ProfileName
wait PlayerProfile Package.PlayerProfile.ProfileName = 10
key Alt_L 0.2
sleep 4
probe spawn
check $1-match-started P['spawn'].pawn==1 and P['spawn'].level=='DM-Deadbolt'
chan exec getall PlayerController PawnClass
wait PawnClass = 10
chan exec getall PlayerReplicationInfo PlayerName
wait PlayerName = 10
SCEN
}
direct() {  # direct <run> [uc2.bin args...]
  local n=$1; shift
  direct_scen "$n" > "$out/$n.scen"
  M10_URL=DM-Deadbolt run "$n" 200 "$@"
}

at_start() {  # main menu up, nothing pressed yet
  cat <<SCEN
expect $1-main-menu-up OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
chan exec getall PlayerProfile ProfileName
wait PlayerProfile Package.PlayerProfile.ProfileName = 10
SCEN
}
to_profiles() {  # main menu (focus on the first item) -> Profiles page
  cat <<SCEN
key Down
key Down
key Down
sleep 0.5
key Return
expect $1-profile-page-opens OpenMenu \[UCMenu.UC2Profile\] 15
SCEN
}
# Profiles page (just left with Escape: focus on PROFILES) -> Instant Action -> DeathMatch -> DM-Deadbolt -> the
# character page, <rights> times Right, then the armory and launch; asks the profile's character in the match.
play_with() {  # play_with <prefix> <rights>   (main menu: 1 Instant Action, 2 Network, 3 Profiles)
  cat <<SCEN
key Up
key Up
sleep 0.5
key Return
expect $1-instant-action-opens OpenMenu \[UCMenu.UC2Arena_Main\] 15
sleep 3
seek 10 P['_'].item.startswith('DeathMatch|') ;; key Down
key Return
sleep 2
key Return
sleep 2
seek 40 'DM-Deadbolt' in P['_'].item ;; key Down
key Return
expect $1-character-page-opens OpenMenu \[UCMenu.UC2Arena_Character\] 15
sleep 2
SCEN
  for _ in $(seq "$2"); do printf 'key Right\nsleep 2\n'; done
  cat <<SCEN
key Return
expect $1-armory-opens OpenMenu \[UCMenu.UC2Arena_Armory\] 15
sleep 2
key Return
sleep 2
key Return
expect $1-launch-confirm-opens OpenMenu \[UCMenu.ConfirmLaunchMenu\] 15
sleep 3
key Return
expect $1-deadbolt-up Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
key Alt_L 0.2
sleep 4
chan exec getall PlayerProfile CharacterClassName
wait PlayerProfile Package.PlayerProfile.CharacterClassName = 10
chan exec getall PlayerController PawnClass
wait PawnClass = 10
SCEN
}
# New profile <name> on the keyboard page; <first>: no profile exists yet (the "create a profile?" box).
new_profile() {  # new_profile <prefix> <name> <first:1|0>
  if [ "$3" = 1 ]; then
    printf 'wait OpenMenu \\[GUI.GUIMessageBox\\] 20\nsleep 1.5\nkey Return\n'
  else
    printf 'sleep 3\nkey e\n'
  fi
  cat <<SCEN
expect $1-keyboard-opens OpenMenu \[UCMenu.UC2ProfileKeyboard\] 20
sleep 3
type $2
sleep 1
key Return
expect $1-keyboard-closes CloseMenu \[UC2ProfileKeyboard\] 10
expect $1-engine-remembers-$2 Profiles: remembered profile "$2" 15
sleep 3
key Escape
expect $1-profile-page-closes CloseMenu \[UC2Profile\] 10
sleep 3
SCEN
}

# --- fresh configuration (as m10-controls.sh's prep; skipped there with M10_KEEP) ---------------------------------
cp -f "$repo"/Engine/Config/*.ini "$sys/"
sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$sys"/*.log
rm -rf "$sys/Profiles"

# --- c0: no profile, the ini's class -------------------------------------------------------------------------------
direct c0 "-ini:User:[DefaultPlayer]:Class=$Z"
check c0-no-profile bash -c "[ '$(active_first c0)' = '\$\$Default\$\$' ] && echo 'profile: $(active_first c0)'"
check c0-plays-the-ini-class bash -c "[ '$(pawnclass c0)' = '$Z' ] && echo 'PawnClass $(pawnclass c0)'"
check c0-no-profile-class-line bash -c "! grep -a -q 'logs in with' '$out/c0.out'"
check c0-user-ini-class bash -c "[ '$(ini DefaultPlayer Class)' = '$Z' ] && echo '[DefaultPlayer] Class=$(ini DefaultPlayer Class)'"

# --- c1: profile Alpha, character X ------------------------------------------------------------------------------
{ at_start c1; to_profiles c1; new_profile c1 Alpha 1; play_with c1 2; } > "$out/c1.scen"
run c1 420
X=$(profchar c1)
check c1-alpha-has-a-character-of-its-own bash -c "[ -n '$X' ] && [ '$X' != '$Z' ] && echo 'X=$X'"
check c1-menu-launch-plays-x bash -c "[ '$(pawnclass c1)' = '$X' ] && echo 'PawnClass $(pawnclass c1)'"
check c1-user-ini-class-kept bash -c "[ '$(ini DefaultPlayer Class)' = '$Z' ] && echo '[DefaultPlayer] Class=$(ini DefaultPlayer Class)'"

direct c1m
check c1m-alpha-active bash -c "[ '$(active_first c1m)' = Alpha ] && echo Alpha"
check c1m-plays-x bash -c "[ '$(pawnclass c1m)' = '$X' ] && echo 'PawnClass $(pawnclass c1m)'"
check c1m-engine-line grep -a -m1 "Profiles: profile \"Alpha\" logs in with Class=$X" "$out/c1m.out"
check c1m-user-ini-class-kept bash -c "[ '$(ini DefaultPlayer Class)' = '$Z' ] && echo '[DefaultPlayer] Class=$(ini DefaultPlayer Class)'"

# --- c2: profile Bravo, character Y ------------------------------------------------------------------------------
{ at_start c2; to_profiles c2; new_profile c2 Bravo 0; play_with c2 0; } > "$out/c2.scen"
run c2 420
Y=$(profchar c2)
check c2-started-in-alpha bash -c "[ '$(active_first c2)' = Alpha ] && echo Alpha"
check c2-bravo-has-another-character bash -c "[ -n '$Y' ] && [ '$Y' != '$X' ] && [ '$Y' != '$Z' ] && echo 'Y=$Y'"
check c2-menu-launch-plays-y bash -c "[ '$(pawnclass c2)' = '$Y' ] && echo 'PawnClass $(pawnclass c2)'"

direct c2m
check c2m-bravo-active bash -c "[ '$(active_first c2m)' = Bravo ] && echo Bravo"
check c2m-plays-y bash -c "[ '$(pawnclass c2m)' = '$Y' ] && echo 'PawnClass $(pawnclass c2m)'"

# --- c3: switch to Alpha at the menus, quit without playing; c3m: direct -----------------------------------------
{
at_start c3
to_profiles c3
cat <<'SCEN'
sleep 3
# The list (alphabetical: Alpha, Bravo) opens on the active profile, Bravo: one Up, select.
key Up
sleep 0.5
key Return
sleep 2
key Escape
expect c3-profile-page-closes CloseMenu \[UC2Profile\] 10
expect c3-engine-remembers-alpha Profiles: remembered profile "Alpha" 15
sleep 2
SCEN
} > "$out/c3.scen"
run c3 240
check c3-started-in-bravo bash -c "[ '$(active_first c3)' = Bravo ] && echo Bravo"
check c3-user-ini-alpha bash -c "[ '$(ini Profiles LastProfile)' = Alpha ] && echo 'LastProfile=Alpha, [DefaultPlayer] Class=$(ini DefaultPlayer Class)'"

direct c3m
check c3m-alpha-active bash -c "[ '$(active_first c3m)' = Alpha ] && echo Alpha"
check c3m-plays-x-not-y bash -c "[ '$(pawnclass c3m)' = '$X' ] && echo 'PawnClass $(pawnclass c3m) (X=$X, Y=$Y)'"

# --- c4m: no profile at all ----------------------------------------------------------------------------------------
cp -f "$sys/Profiles/Alpha/AVARIS_PROFILE.PRO" "$out/Alpha.PRO"
cp -f "$sys/Profiles/Bravo/AVARIS_PROFILE.PRO" "$out/Bravo.PRO"
rm -rf "$sys/Profiles"
direct c4m
check c4m-no-profile bash -c "[ '$(active_first c4m)' = '\$\$Default\$\$' ] && grep -a -m1 'does not exist; starting with no profile' '$out/c4m.out'"
check c4m-plays-the-ini-class bash -c "[ '$(pawnclass c4m)' = '$Z' ] && echo 'PawnClass $(pawnclass c4m)'"

# --- c5m: the web preview's flat files -----------------------------------------------------------------------------
mkdir -p "$sys/Profiles/Bravo"
cp -f "$out/Alpha.PRO" "$sys/Profiles/Alpha.PRO"
cp -f "$out/Bravo.PRO" "$sys/Profiles/Bravo/AVARIS_PROFILE.PRO"
cp -f "$out/Alpha.PRO" "$sys/Profiles/Bravo.PRO"	# an older flat copy beside an existing folder
sed -i 's/^LastProfile=.*/LastProfile=Alpha/' "$sys/User.ini"
grep -q '^LastProfile=Alpha' "$sys/User.ini" || printf '\n[Profiles]\nLastProfile=Alpha\n' >> "$sys/User.ini"
direct c5m
check c5m-alpha-moved grep -a -m1 "Profiles: flat profile file .*Alpha.PRO moved into Epic's folder\$" "$out/c5m.out"
check c5m-bravo-flat-deleted grep -a -m1 "Profiles: flat profile file .*Bravo.PRO deleted (Epic's folder exists)\$" "$out/c5m.out"
check c5m-started-in-alpha grep -a -m1 'Profiles: started with the last profile "Alpha"' "$out/c5m.out"
check c5m-plays-x bash -c "[ '$(pawnclass c5m)' = '$X' ] && echo 'PawnClass $(pawnclass c5m)'"
check c5m-files bash -c "cmp '$out/Alpha.PRO' '$sys/Profiles/Alpha/AVARIS_PROFILE.PRO' && cmp '$out/Bravo.PRO' '$sys/Profiles/Bravo/AVARIS_PROFILE.PRO' && [ ! -e '$sys/Profiles/Alpha.PRO' ] && [ ! -e '$sys/Profiles/Bravo.PRO' ] && ls -R '$sys/Profiles' | tr '\n' ' '"

echo "d3-character: $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (X=$X Y=$Y Z=$Z; logs in $out)"
[ "$fails" = 0 ]
