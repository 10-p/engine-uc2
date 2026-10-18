#!/usr/bin/env bash
# Profile check (native): the player starts in the profile they last played with, with its settings.
#
#   tools/dev/d3-profiles.sh
#
# Needs the native run tree <repo>/../run/d3 (tools/dev/mkrun-uc2.sh build-native d3; D3_TREE=<name> to change).
# Every run is one client under Xvfb inside <repo>/../bin/capped 2G (through tools/dev/m10-controls.sh --file);
# runs 2-5 start a new process on the same tree (M10_KEEP=1: nothing reset), i.e. restarts.
# Output in <repo>/../logs/d3/profiles/ (D3_OUT=<dir>): r<N>.out (engine log), r<N>.result (the run's own
# checks), screenshots, User.ini after each run; one PASS/FAIL line per check on stdout.
#
#   r1  fresh configuration (no profiles, no derived inis): create profile "Saver" on the on-screen keyboard
#       (real keyboard), Settings -> Controller -> invert the look, Instant Action with another character, a
#       few seconds of a deathmatch, normal exit (SIGTERM).
#   r2m restart straight into DM-Deadbolt (as the web launcher's Play form does): "Saver" is active, its invert
#       setting is applied to the player's input (UCPlayerInput: the menu level's input class ignores it), and
#       the player is named after it.
#   r2  restart: "Saver" is the active profile WITHOUT any menu action (engine probe), its invert setting and
#       its character are back, the player is named after it; then a second profile "Second" is created.
#   r3  restart: "Second" (the last one used) is active; the player selects "Saver" on the Profiles page.
#   r4  restart: "Saver" is active again (the last one USED, not the last one made); the player deletes it.
#   r5  restart: whatever was active when r4 ended is active now, and "Saver" is not.
# The engine logs "Profiles: started with the last profile ..." / "Profiles: remembered profile ..." (Engine/Src/
# UC2Profiles.cpp); User.ini [Profiles] LastProfile is read after each run.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${D3_TREE:-d3}
root=$work/run/$tree
sys=$root/System
out=${D3_OUT:-$work/logs/d3/profiles}
m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }

fails=0
result() { echo "$1 $2: $3"; [ "$1" = PASS ] || fails=$((fails+1)); }
check() { local n=$1 d; shift; if d=$("$@" 2>&1); then result PASS "$n" "$d"; else result FAIL "$n" "$d"; fi; }
# lastprofile: the value of User.ini [Profiles] LastProfile ("" when absent).
lastprofile() { awk -F= '/^\[/{s=($0=="[Profiles]")} s&&$1=="LastProfile"{v=$2} END{print v}' "$sys/User.ini" 2>/dev/null | tr -d '\r'; }
# active <run>: the viewport's profile name in that run's last `getall PlayerProfile ProfileName` answer.
active_last() { grep -a "PlayerProfile Package.PlayerProfile.ProfileName = " "$out/$1.out" | tail -1 | sed 's/.* = //' | tr -d '\r'; }
active_first() { grep -a "PlayerProfile Package.PlayerProfile.ProfileName = " "$out/$1.out" | head -1 | sed 's/.* = //' | tr -d '\r'; }

run() {  # run <name> <seconds>  (scenario in $out/<name>.scen)
  echo "== $1"
  M10_KEEP=1 M10_TREE=$tree M10_OUT=$out M10_SECS=$2 "$m10" --file "$out/$1.scen" "$1" | sed 's/^/    /'
  cp -f "$sys/User.ini" "$out/$1-User.ini" 2>/dev/null
  check "$1-scenario-checks" bash -c "! grep -a -q '^FAIL' '$out/$1.result' && grep -a -c '^PASS' '$out/$1.result'"
}

# The start of every restart: the main menu is up, nothing has been pressed; ask the engine.
at_start() {  # at_start <prefix>
  cat <<SCEN
expect $1-main-menu-up OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
chan exec getall PlayerProfile ProfileName
wait PlayerProfile Package.PlayerProfile.ProfileName = 10
chan exec getall PlayerProfile bInvertY
wait PlayerProfile Package.PlayerProfile.bInvertY = 10
chan exec getall PlayerProfile CharacterClassName
wait PlayerProfile Package.PlayerProfile.CharacterClassName = 10
chan exec getall PlayerReplicationInfo PlayerName
wait PlayerName = 10
shot start
SCEN
}

# Main menu (focus on the first item) -> Profiles page. The "create a profile?" box comes up only when none exists.
to_profiles() {  # to_profiles <prefix>
  cat <<SCEN
key Down
key Down
key Down
sleep 0.5
key Return
expect $1-profile-page-opens OpenMenu \[UCMenu.UC2Profile\] 15
SCEN
}

# --- fresh configuration (as m10-controls.sh's prep; skipped there with M10_KEEP) ---------------------------------
cp -f "$repo"/Engine/Config/*.ini "$sys/"
sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$sys"/*.log
rm -rf "$sys/Profiles"

# --- r1 -------------------------------------------------------------------------------------------------------------
{
cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
chan exec getall PlayerProfile ProfileName
expect r1-fresh-start-has-no-profile PlayerProfile Package.PlayerProfile.ProfileName = \$\$Default\$\$ 10
SCEN
to_profiles r1
cat <<'SCEN'
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
key Return
expect r1-keyboard-opens OpenMenu \[UCMenu.UC2ProfileKeyboard\] 15
sleep 3
type Saver
sleep 1
probe k1
check r1-typed-the-name P['k1'].text == 'Saver'
key Return
expect r1-keyboard-closes CloseMenu \[UC2ProfileKeyboard\] 10
expect r1-engine-remembers-the-new-profile Profiles: remembered profile "Saver" 15
sleep 3
key Escape
expect r1-profile-page-closes CloseMenu \[UC2Profile\] 10
sleep 3
key Down
sleep 0.5
key Return
expect r1-settings-opens OpenMenu \[UCMenu.UC2Settings_Main\] 15
sleep 3
key Down
sleep 0.5
key Return
expect r1-controller-settings-open OpenMenu \[UCMenu.UC2Settings_Controller\] 15
sleep 3
# the page opens on the layout button; the first check box below it is Invert
seek 4 'CheckBox' in P['_'].focus ;; key Down
key Return
sleep 1
shot inverted
key Escape
expect r1-controller-settings-close CloseMenu \[UC2Settings_Controller\] 10
sleep 2
chan exec getall PlayerProfile bInvertY
expect r1-invert-is-in-the-profile PlayerProfile Package.PlayerProfile.bInvertY = True 10
key Escape
expect r1-settings-close CloseMenu \[UC2Settings_Main\] 10
sleep 3
key Up
key Up
key Up
sleep 0.5
mark
key Return
expect r1-instant-action-opens OpenMenu \[UCMenu.UC2Arena_Main\] 15
sleep 3
absent r1-no-no-profile-box-with-a-profile OpenMenu \[GUI.GUIMessageBox\]
seek 10 P['_'].item.startswith('DeathMatch|') ;; key Down
key Return
sleep 2
key Return
sleep 2
seek 40 'DM-Deadbolt' in P['_'].item ;; key Down
key Return
expect r1-character-page-opens OpenMenu \[UCMenu.UC2Arena_Character\] 15
sleep 2
key Right
sleep 2
key Return
expect r1-armory-opens OpenMenu \[UCMenu.UC2Arena_Armory\] 15
sleep 2
key Return
sleep 2
key Return
expect r1-launch-confirm-opens OpenMenu \[UCMenu.ConfirmLaunchMenu\] 15
sleep 1.5
key Return
expect r1-deadbolt-up Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
key Alt_L 0.2
sleep 4
probe spawn
check r1-match-started P['spawn'].pawn==1 and P['spawn'].level=='DM-Deadbolt'
chan exec getall PlayerProfile CharacterClassName
wait PlayerProfile Package.PlayerProfile.CharacterClassName = 10
chan exec getall PlayerProfile ProfileName
wait PlayerProfile Package.PlayerProfile.ProfileName = 10
SCEN
} > "$out/r1.scen"
run r1 420
r1_char=$(grep -a "PlayerProfile Package.PlayerProfile.CharacterClassName = " "$out/r1.out" | tail -1 | sed 's/.* = //' | tr -d '\r')
check r1-profile-saved test -s "$sys/Profiles/Saver/AVARIS_PROFILE.PRO"
check r1-user-ini-remembers-saver bash -c "[ '$(lastprofile)' = Saver ] && echo 'LastProfile=$(lastprofile)'"
check r1-character-changed-from-the-default bash -c "[ -n '$r1_char' ] && [ '$r1_char' != UCCharacters.UCPawn_Nakhti_Anubis ] && echo '$r1_char'"

# --- r2m: restart straight into a map (what the web launcher does when a map is chosen) --------------------------
cat > "$out/r2m.scen" <<'SCEN'
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
focus
chan exec getall PlayerProfile ProfileName
wait PlayerProfile Package.PlayerProfile.ProfileName = 10
chan exec getall UCPlayerInput bInvertMouse
wait bInvertMouse = 10
chan exec getall PlayerReplicationInfo PlayerName
wait PlayerName = 10
key Alt_L 0.2
sleep 4
probe spawn
check r2m-match-started P['spawn'].pawn==1
chan exec getall PlayerReplicationInfo PlayerName
wait PlayerName = 10
shot match
SCEN
M10_URL=DM-Deadbolt run r2m 240
check r2m-saver-active-in-a-map-started-directly bash -c "[ '$(active_first r2m)' = Saver ] && echo 'first answer: $(active_first r2m)'"
check r2m-invert-applied-to-the-players-input grep -a -m1 "UCPlayerInput.*bInvertMouse = True" "$out/r2m.out"
check r2m-player-named-after-the-profile grep -a -m1 "PlayerName = Saver" "$out/r2m.out"

# --- r2 -------------------------------------------------------------------------------------------------------------
{
at_start r2
echo mark
to_profiles r2
cat <<'SCEN'
sleep 3
absent r2-no-create-box-when-profiles-exist OpenMenu \[GUI.GUIMessageBox\]
key e
expect r2-new-opens-the-keyboard OpenMenu \[UCMenu.UC2ProfileKeyboard\] 20
sleep 3
probe k0
type Second
sleep 1
probe k2
check r2-typed-the-second-name P['k2'].text == 'Second'
key Return
expect r2-keyboard-closes CloseMenu \[UC2ProfileKeyboard\] 10
expect r2-engine-remembers-the-second-profile Profiles: remembered profile "Second" 15
sleep 3
key Escape
expect r2-profile-page-closes CloseMenu \[UC2Profile\] 10
sleep 2
chan exec getall PlayerProfile ProfileName
expect r2-second-is-active PlayerProfile Package.PlayerProfile.ProfileName = Second\s*$ 10
SCEN
} > "$out/r2.scen"
run r2 240
check r2-engine-started-with-saver grep -a -m1 'Profiles: started with the last profile "Saver"' "$out/r2.out"
check r2-saver-active-before-any-menu-action bash -c "[ '$(active_first r2)' = Saver ] && echo 'first answer: $(active_first r2)'"
check r2-invert-back-in-the-profile grep -a -m1 "PlayerProfile Package.PlayerProfile.bInvertY = True" "$out/r2.out"
check r2-character-back bash -c "grep -a -m1 'PlayerProfile Package.PlayerProfile.CharacterClassName = $r1_char' '$out/r2.out'"
check r2-player-named-after-the-profile grep -a -m1 "PlayerName = Saver" "$out/r2.out"
check r2-no-load-failure bash -c "! grep -a -q 'Unable to load profile\|did not load\|does not exist; starting' '$out/r2.out'"
check r2-user-ini-remembers-second bash -c "[ '$(lastprofile)' = Second ] && echo 'LastProfile=$(lastprofile)'"
check r2-both-profiles-on-disk bash -c "ls '$sys/Profiles' | tr '\n' ' '; [ -s '$sys/Profiles/Saver/AVARIS_PROFILE.PRO' ] && [ -s '$sys/Profiles/Second/AVARIS_PROFILE.PRO' ]"

# --- r3 -------------------------------------------------------------------------------------------------------------
{
at_start r3
to_profiles r3
cat <<'SCEN'
sleep 3
# The list (alphabetical: Saver, Second) opens on the active profile, Second; its rows have no text the probe
# can read (the names are check boxes beside it), so one Up and the result decides (r3-saver-is-active below).
key Up
sleep 0.5
key Return
sleep 2
shot selected
key Escape
expect r3-profile-page-closes CloseMenu \[UC2Profile\] 10
expect r3-engine-remembers-saver-again Profiles: remembered profile "Saver" 15
sleep 2
chan exec getall PlayerProfile ProfileName
expect r3-saver-is-active PlayerProfile Package.PlayerProfile.ProfileName = Saver\s*$ 10
SCEN
} > "$out/r3.scen"
run r3 240
check r3-second-active-at-start bash -c "[ '$(active_first r3)' = Second ] && echo 'first answer: $(active_first r3)'"
check r3-user-ini-remembers-saver bash -c "[ '$(lastprofile)' = Saver ] && echo 'LastProfile=$(lastprofile)'"

# --- r4 -------------------------------------------------------------------------------------------------------------
{
at_start r4
to_profiles r4
cat <<'SCEN'
sleep 3
# The list opens on the active profile, Saver: X deletes it.
key x
wait OpenMenu \[GUI.GUIMessageBox\] 15
sleep 1.5
shot delete-question
key Return
sleep 3
probe d1
shot deleted
key Escape
sleep 3
probe d2
seek 3 P['_'].menu == 'UC2MainMenu' ;; key Escape
chan exec getall PlayerProfile ProfileName
wait PlayerProfile Package.PlayerProfile.ProfileName = 10
SCEN
} > "$out/r4.scen"
run r4 240
check r4-saver-active-at-start bash -c "[ '$(active_first r4)' = Saver ] && echo 'first answer: $(active_first r4)'"
check r4-saver-deleted bash -c "[ ! -e '$sys/Profiles/Saver' ] && ls '$sys/Profiles' | tr '\n' ' '"
r4_end=$(active_last r4)
r4_ini=$(lastprofile)
check r4-user-ini-follows-the-active-profile bash -c "exp='$r4_end'; [ \"\$exp\" = '\$\$Default\$\$' ] && exp=''; [ '$r4_ini' = \"\$exp\" ] && echo 'active at exit: $r4_end, LastProfile=$r4_ini'"

# --- r5 -------------------------------------------------------------------------------------------------------------
at_start r5 > "$out/r5.scen"
run r5 180
check r5-starts-with-what-r4-ended-with bash -c "[ '$(active_first r5)' = '$r4_end' ] && echo 'r4 ended with: $r4_end, r5 starts with: $(active_first r5)'"
check r5-saver-not-active bash -c "[ '$(active_first r5)' != Saver ]"

echo "d3-profiles: $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (logs in $out)"
[ "$fails" = 0 ]
