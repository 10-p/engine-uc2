#!/usr/bin/env bash
# Player name check (native): whose name is the player's? The launcher's without a profile, the profile's with one.
#
#   tools/dev/d4-names.sh
#
# Needs a native run tree with the rebuilt script packages (GUI.u carries the name change):
#   UC2_SCRIPTS=<repo>/../run/d3-make/System tools/dev/mkrun-uc2.sh build-native d3s
# (D4_TREE=<name> to change; run on a tree with the shipped packages it shows Epic's behaviour: n1 fails where the
# character page renames the player.) Every run is one client under Xvfb inside <repo>/../bin/capped 2G (through
# tools/dev/m10-controls.sh --file); n2-n4 restart on the same tree (M10_KEEP=1). Each run is launched the way the
# web launcher launches the game: the name as a global option first (`Name=FrontName`, or a map URL with
# `?Name=FrontName`) and as `-ini:User:[DefaultPlayer]:Name=FrontName`.
# Output in <repo>/../logs/d3/names/ (D4_OUT=<dir>); one PASS/FAIL line per check on stdout.
#
#   n1  fresh (no profile): the menu, the character page after a character change, the match URL and the match
#       all use FrontName (Epic: the character's name from the character page on); User.ini keeps FrontName.
#   n2  restart: a profile "Saver" is created; the player is "Saver" at once; at exit User.ini [DefaultPlayer] Name
#       is still FrontName (the profile's name is not the player's own name) and [Profiles] LastProfile=Saver.
#   n3  restart straight into DM-Deadbolt?Name=FrontName: the profile is active and names the player, in spite of
#       the URL's Name (the scoreboard draws the PlayerReplicationInfo's name, which is what is read).
#   n4  restart: the profile is deleted on the Profiles page: the player is FrontName again, LastProfile is empty.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${D4_TREE:-d3s}
sys=$work/run/$tree/System
out=${D4_OUT:-$work/logs/d3/names}
m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (UC2_SCRIPTS=... tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }
front=${D4_NAME:-FrontName}
ini="-ini:User:[DefaultPlayer]:Name=$front"

fails=0
result() { echo "$1 $2: $3"; [ "$1" = PASS ] || fails=$((fails+1)); }
check() { local n=$1 d; shift; if d=$("$@" 2>&1); then result PASS "$n" "$d"; else result FAIL "$n" "$d"; fi; }
inikey() { awk -F= -v sec="[$1]" -v key="$2" '/^\[/{s=($0==sec)} s&&$1==key{v=substr($0,length(key)+2)} END{print v}' "$sys/User.ini" 2>/dev/null | tr -d '\r'; }

run() {  # run <name> <seconds> <first argument>
  echo "== $1 (launched with: $3 $ini)"
  M10_KEEP=1 M10_TREE=$tree M10_OUT=$out M10_SECS=$2 M10_URL="$3" "$m10" --file "$out/$1.scen" "$1" "$ini" | sed 's/^/    /'
  cp -f "$sys/User.ini" "$out/$1-User.ini" 2>/dev/null
  check "$1-scenario-checks" bash -c "! grep -a -q '^FAIL' '$out/$1.result' && grep -a -c '^PASS' '$out/$1.result'"
}

# The names of the PlayerReplicationInfos (what scoreboards draw): the menu level has only the player's; in a
# match the bots have their own names, so the expected name appearing at all is the player's.
pri_name() {  # pri_name <check> <expected>
  cat <<SCEN
chan exec getall PlayerReplicationInfo PlayerName
expect $1 PlayerReplicationInfo.*PlayerName = $2\s*$ 10
SCEN
}

# --- fresh configuration (as m10-controls.sh's prep; skipped there with M10_KEEP) ------------------------------
cp -f "$repo"/Engine/Config/*.ini "$sys/"
sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$sys"/*.log
rm -rf "$sys/Profiles"

# --- n1: no profile ---------------------------------------------------------------------------------------------
{
cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
SCEN
pri_name n1-menu-uses-the-launchers-name "$front"
cat <<'SCEN'
key Down
sleep 0.5
key Return
expect n1-instant-action-opens OpenMenu \[UCMenu.UC2Arena_Main\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
key Escape
expect n1-no-profile-box-answered-no CloseMenu \[GUIMessageBox\] 10
sleep 1.5
seek 10 P['_'].item.startswith('DeathMatch|') ;; key Down
key Return
sleep 2
key Return
sleep 2
seek 40 'DM-Deadbolt' in P['_'].item ;; key Down
key Return
expect n1-character-page-opens OpenMenu \[UCMenu.UC2Arena_Character\] 15
sleep 2
key Right
sleep 2
key Right
sleep 2
shot character
SCEN
pri_name n1-character-change-keeps-the-launchers-name "$front"
cat <<'SCEN'
key Return
expect n1-armory-opens OpenMenu \[UCMenu.UC2Arena_Armory\] 15
sleep 2
key Return
sleep 2
key Return
expect n1-launch-confirm-opens OpenMenu \[UCMenu.ConfirmLaunchMenu\] 15
sleep 1.5
key Return
SCEN
cat <<SCEN
expect n1-match-url-carries-the-launchers-name LoadMap: DM-Deadbolt\?.*Name=$front 120
expect n1-deadbolt-up Bringing Level DM-Deadbolt.MyLevel up for play 60
sleep 6
key Alt_L 0.2
sleep 4
probe spawn
check n1-match-started P['spawn'].pawn==1
SCEN
pri_name n1-match-player-has-the-launchers-name "$front"
echo "shot match"
} > "$out/n1.scen"
run n1 420 "Name=$front"
check n1-user-ini-keeps-the-launchers-name bash -c "[ '$(inikey DefaultPlayer Name)' = '$front' ] && echo '[DefaultPlayer] Name=$(inikey DefaultPlayer Name)'"
check n1-no-profile-remembered bash -c "[ -z '$(inikey Profiles LastProfile)' ] && echo 'LastProfile=(empty)'"

# --- n2: a profile is created -----------------------------------------------------------------------------------
{
cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
SCEN
pri_name n2-menu-uses-the-launchers-name "$front"
cat <<'SCEN'
key Down
key Down
key Down
sleep 0.5
key Return
expect n2-profile-page-opens OpenMenu \[UCMenu.UC2Profile\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
key Return
expect n2-keyboard-opens OpenMenu \[UCMenu.UC2ProfileKeyboard\] 15
sleep 3
type Saver
sleep 1
key Return
expect n2-keyboard-closes CloseMenu \[UC2ProfileKeyboard\] 10
expect n2-profile-remembered Profiles: remembered profile "Saver" 15
sleep 3
key Escape
expect n2-profile-page-closes CloseMenu \[UC2Profile\] 10
sleep 2
SCEN
pri_name n2-the-profile-names-the-player Saver
} > "$out/n2.scen"
run n2 240 "Name=$front"
check n2-user-ini-still-has-the-launchers-name bash -c "[ '$(inikey DefaultPlayer Name)' = '$front' ] && echo '[DefaultPlayer] Name=$(inikey DefaultPlayer Name)'"
check n2-profile-remembered bash -c "[ '$(inikey Profiles LastProfile)' = Saver ] && echo 'LastProfile=Saver'"

# --- n3: straight into a map, with the launcher's ?Name= ------------------------------------------------------------
{
cat <<'SCEN'
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
focus
chan exec getall PlayerProfile ProfileName
expect n3-profile-active PlayerProfile Package.PlayerProfile.ProfileName = Saver\s*$ 10
key Alt_L 0.2
sleep 4
probe spawn
check n3-match-started P['spawn'].pawn==1
SCEN
pri_name n3-the-profile-names-the-player-despite-the-url Saver
echo "shot match"
} > "$out/n3.scen"
run n3 240 "DM-Deadbolt?Name=$front"
check n3-url-had-the-launchers-name grep -a -m1 "LoadMap: DM-Deadbolt?.*Name=$front" "$out/n3.out"
check n3-user-ini-still-has-the-launchers-name bash -c "[ '$(inikey DefaultPlayer Name)' = '$front' ] && echo '[DefaultPlayer] Name=$(inikey DefaultPlayer Name)'"

# --- n4: the profile is deleted -------------------------------------------------------------------------------------
{
cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
SCEN
pri_name n4-the-profile-names-the-player-at-start Saver
cat <<'SCEN'
key Down
key Down
key Down
sleep 0.5
key Return
expect n4-profile-page-opens OpenMenu \[UCMenu.UC2Profile\] 15
sleep 3
key x
wait OpenMenu \[GUI.GUIMessageBox\] 15
sleep 1.5
key Return
expect n4-profile-forgotten Profiles: remembered profile "" 15
sleep 3
seek 3 P['_'].menu == 'UC2MainMenu' ;; key Escape
sleep 2
SCEN
pri_name n4-the-launchers-name-is-back "$front"
} > "$out/n4.scen"
run n4 240 "Name=$front"
check n4-profile-deleted bash -c "[ ! -e '$sys/Profiles/Saver' ] && echo 'Profiles/: '\$(ls '$sys/Profiles' | tr '\n' ' ')"
check n4-nothing-remembered bash -c "[ -z '$(inikey Profiles LastProfile)' ] && echo 'LastProfile=(empty)'"

echo "d4-names: $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (logs in $out)"
[ "$fails" = 0 ]
