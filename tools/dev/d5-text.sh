#!/usr/bin/env bash
# Text entry check (native): every text-entry page takes the real keyboard; Enter accepts, Escape cancels.
#
#   tools/dev/d5-text.sh [k1 k2 k3 k4 k5]
#
# Needs a native run tree (default <repo>/../run/d5: tools/dev/mkrun-uc2.sh build-native d5; D5_TREE=<name>).
# Every run is one client under Xvfb inside <repo>/../bin/capped 2G (through tools/dev/m10-controls.sh --file);
# k2-k5 run on the tree k1 left (M10_KEEP=1). Output in <repo>/../logs/d3/text/ (D5_OUT=<dir>).
#
# The text pages: UCMenu.UC2ProfileKeyboard (new profile, copy, rename) and
# UCMenu.UC2Keyboard (a server's name when hosting), both an on-screen keyboard whose field never has the focus;
# the engine lets the real keyboard type into it ([GUI.EmulatedJoypad] `/Type` codes, UnGUICore.cpp) with Enter = Start
# (done) and Escape = B (cancel). In a match, chat (T = Talk) is Engine.Console's Typing state.
#   k1  new profile: typed text arrives exactly (W/a/S/d/X/q are pad keys elsewhere), Backspace edits, Escape
#       cancels (no profile is made); New again, Enter makes "Typer".
#   k2  rename (Profiles -> Y Edit -> Rename): Escape keeps the old name; Enter renames (the engine follows: the
#       remembered profile is the new name).
#   k3  copy (Profiles -> Q = White): Enter makes the copy.
#   k4  host -> server name (UC2Keyboard): Escape keeps the old name, Enter sets the typed one.
#   k5  chat in a match (T): typed text arrives, W/A/S/D do not move the player while typing, Enter sends it,
#       Escape cancels.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${D5_TREE:-d5}
sys=$work/run/$tree/System
out=${D5_OUT:-$work/logs/d3/text}
m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }

fails=0
result() { echo "$1 $2: $3"; [ "$1" = PASS ] || fails=$((fails+1)); }
check() { local n=$1 d; shift; if d=$("$@" 2>&1); then result PASS "$n" "$d"; else result FAIL "$n" "$d"; fi; }
inikey() { awk -F= -v sec="[$1]" -v key="$2" '/^\[/{s=($0==sec)} s&&$1==key{v=substr($0,length(key)+2)} END{print v}' "$sys/User.ini" 2>/dev/null | tr -d '\r'; }
run() {  # run <name> <seconds> [first argument]
  echo "== $1"
  M10_KEEP=1 M10_TREE=$tree M10_OUT=$out M10_SECS=$2 M10_URL="${3:-}" "$m10" --file "$out/$1.scen" "$1" | sed 's/^/    /'
  check "$1-scenario-checks" bash -c "! grep -a -q '^FAIL' '$out/$1.result' && grep -a -c '^PASS' '$out/$1.result'"
}
menu_up='wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus'
to_profiles='key Down
key Down
key Down
sleep 0.5
key Return'
wanted=" ${*:-k1 k2 k3 k4 k5} "

# --- k1: new profile ------------------------------------------------------------------------------------------------
if [[ $wanted == *" k1 "* ]]; then
cp -f "$repo"/Engine/Config/*.ini "$sys/"
sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$sys"/*.log
rm -rf "$sys/Profiles"
cat > "$out/k1.scen" <<SCEN
$menu_up
$to_profiles
expect k1-profiles-opens OpenMenu \[UCMenu.UC2Profile\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
wait GUIMessageBox Unblocking animation 15
sleep 0.5
key Return
expect k1-keyboard-opens OpenMenu \[UCMenu.UC2ProfileKeyboard\] 15
sleep 3
probe a0
mark
type WaSdXq
sleep 1
probe a1
check k1-typed-exactly P['a1'].text == 'WaSdXq'
key BackSpace
sleep 0.5
probe a2
check k1-backspace-deletes-one P['a2'].text == 'WaSdX'
absent k1-typing-does-not-navigate handled XC_
key Escape
expect k1-escape-closes-the-keyboard CloseMenu \[UC2ProfileKeyboard\] 10
sleep 3
absent k1-escape-made-no-profile Profiles: remembered profile "WaSdX"
# With no profile at all, the Profiles page closes after a cancelled creation (UC2Profile.InternalOnReopened); the
# main menu keeps the focus on PROFILES.
seek 4 P['_'].menu == 'UC2MainMenu' ;; key Escape
key Return
expect k1-profiles-opens-again OpenMenu \[UCMenu.UC2Profile\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
# A box takes no key while it animates in (seen under load: A ignored, the letters typed next went to it).
wait GUIMessageBox Unblocking animation 15
sleep 0.5
key Return
expect k1-the-keyboard-opens-again OpenMenu \[UCMenu.UC2ProfileKeyboard\] 20
sleep 3
type Typerz
key BackSpace
sleep 0.5
probe b1
check k1-typed-the-name P['b1'].text == 'Typer'
key Return
expect k1-enter-closes-the-keyboard CloseMenu \[UC2ProfileKeyboard\] 10
expect k1-enter-made-the-profile Profiles: remembered profile "Typer" 15
sleep 3
key Escape
expect k1-profiles-closes CloseMenu \[UC2Profile\] 10
SCEN
run k1 240
check k1-no-cancelled-profile-on-disk bash -c "[ ! -e '$sys/Profiles/WaSdX' ] && [ ! -e '$sys/Profiles/WaSdXq' ] && ls '$sys/Profiles' | tr '\n' ' '"
check k1-typer-on-disk test -s "$sys/Profiles/Typer/AVARIS_PROFILE.PRO"
fi

# --- k2: rename -----------------------------------------------------------------------------------------------------
if [[ $wanted == *" k2 "* ]]; then
cat > "$out/k2.scen" <<SCEN
$menu_up
$to_profiles
expect k2-profiles-opens OpenMenu \[UCMenu.UC2Profile\] 15
sleep 3
key y
expect k2-edit-opens-settings OpenMenu \[UCMenu.UC2Settings_Main\] 20
sleep 3
key Return
expect k2-rename-opens-the-keyboard OpenMenu \[UCMenu.UC2ProfileKeyboard\] 20
sleep 3
probe r0
check k2-keyboard-shows-the-current-name P['r0'].text == 'Typer'
type Nope
sleep 0.5
probe r1
key Escape
expect k2-escape-closes-the-keyboard CloseMenu \[UC2ProfileKeyboard\] 10
sleep 3
chan exec getall PlayerProfile ProfileName
expect k2-escape-kept-the-name PlayerProfile Package.PlayerProfile.ProfileName = Typer\s*$ 10
probe r2
check k2-back-on-settings P['r2'].menu == 'UC2Settings_Main'
key Return
expect k2-rename-again OpenMenu \[UCMenu.UC2ProfileKeyboard\] 20
sleep 3
probe r3
check k2-the-cancelled-text-is-gone P['r3'].text == 'Typer'
key BackSpace
sleep 0.5
probe r4
check k2-backspace-clears-the-selected-text P['r4'].text == ''
type Renamed
sleep 0.5
probe r5
check k2-typed-the-new-name P['r5'].text == 'Renamed'
key Return
expect k2-enter-closes-the-keyboard CloseMenu \[UC2ProfileKeyboard\] 10
expect k2-the-engine-follows-the-rename Profiles: remembered profile "Renamed" 15
sleep 3
chan exec getall PlayerProfile ProfileName
expect k2-renamed PlayerProfile Package.PlayerProfile.ProfileName = Renamed\s*$ 10
seek 6 P['_'].menu == 'UC2MainMenu' ;; key Escape
SCEN
run k2 240
check k2-renamed-on-disk bash -c "[ -s '$sys/Profiles/Renamed/AVARIS_PROFILE.PRO' ] && [ ! -e '$sys/Profiles/Typer' ] && [ ! -e '$sys/Profiles/Nope' ] && ls '$sys/Profiles' | tr '\n' ' '"
check k2-user-ini-follows bash -c "[ '$(inikey Profiles LastProfile)' = Renamed ] && echo LastProfile=Renamed"
fi

# --- k3: copy -------------------------------------------------------------------------------------------------------
if [[ $wanted == *" k3 "* ]]; then
cat > "$out/k3.scen" <<SCEN
$menu_up
$to_profiles
expect k3-profiles-opens OpenMenu \[UCMenu.UC2Profile\] 15
sleep 3
key q
expect k3-copy-opens-the-keyboard OpenMenu \[UCMenu.UC2ProfileKeyboard\] 20
sleep 3
probe c0
key BackSpace
type Copied
sleep 0.5
probe c1
check k3-typed P['c1'].text == 'Copied'
key Return
expect k3-enter-closes-the-keyboard CloseMenu \[UC2ProfileKeyboard\] 10
sleep 3
seek 4 P['_'].menu == 'UC2MainMenu' ;; key Escape
SCEN
run k3 240
check k3-copy-on-disk bash -c "[ -s '$sys/Profiles/Copied/AVARIS_PROFILE.PRO' ] && [ -s '$sys/Profiles/Renamed/AVARIS_PROFILE.PRO' ] && ls '$sys/Profiles' | tr '\n' ' '"
fi

# --- k4: host -> server name ----------------------------------------------------------------------------------------
# Network -> Host -> the game type (DeathMatch) -> the rules list opens on CUSTOMIZE (NO); Right makes it YES (the
# session name is disabled otherwise: "You must enable the CUSTOMIZE option to modify this setting"); the last rule is
# SESSION NAME ("Liandri Corporation", the profile's server name); X opens UC2Keyboard on it. The probe cannot read
# the rule rows (their list items are empty), so the keyboard page's own field is the witness: it shows the rule's
# current value each time it opens.
if [[ $wanted == *" k4 "* ]]; then
downs=$(for i in $(seq 20); do printf 'key Down\nsleep 0.3\n'; done)
cat > "$out/k4.scen" <<SCEN
$menu_up
key Down
key Down
sleep 0.5
key Return
expect k4-network-opens OpenMenu \[UCMenu.UC2NetworkMain\] 15
sleep 3
key Down
sleep 0.5
key Return
expect k4-host-opens OpenMenu \[UCMenu.UC2Arena_ServerMain\] 20
sleep 4
seek 4 P['_'].menu == 'UC2Arena_ServerMain' ;; key Escape
seek 10 P['_'].item.startswith('DeathMatch|') ;; key Down
key Return
sleep 3
key Right
sleep 1
$downs
shot session-name
mark
key x
expect k4-x-opens-the-keyboard OpenMenu \[UCMenu.UC2Keyboard\] 20
sleep 3
absent k4-the-x-that-opened-it-is-not-typed Typed 'x'
probe s1
check k4-keyboard-shows-the-session-name P['s1'].text.startswith('Liandri')
key BackSpace
sleep 0.5
probe s2
check k4-backspace-clears-the-selected-text P['s2'].text == ''
type Nope
sleep 0.5
probe s3
check k4-typed P['s3'].text == 'Nope'
key Escape
expect k4-escape-closes-the-keyboard CloseMenu \[UC2Keyboard\] 10
sleep 3
key x
expect k4-x-opens-the-keyboard-again-on-the-old-name OpenMenu \[UCMenu.UC2Keyboard\] \(Liandri Corporation\) 20
sleep 3
probe s4
check k4-escape-kept-the-old-name P['s4'].text == P['s1'].text
key BackSpace
type Mine
sleep 0.5
probe s5
check k4-typed-again P['s5'].text == 'Mine'
key Return
expect k4-enter-closes-the-keyboard CloseMenu \[UC2Keyboard\] 10
sleep 3
key x
expect k4-x-opens-the-keyboard-on-the-new-name OpenMenu \[UCMenu.UC2Keyboard\] \(Mine\) 20
sleep 3
probe s6
check k4-enter-set-the-name P['s6'].text == 'Mine'
key Escape
sleep 2
shot server-name
SCEN
run k4 300
fi

# --- k5: chat in a match --------------------------------------------------------------------------------------------
if [[ $wanted == *" k5 "* ]]; then
cat > "$out/k5.scen" <<SCEN
wait Bringing Level DM-Deadbolt.MyLevel up for play 120
sleep 6
focus
key Alt_L 0.2
sleep 4
probe spawn
check k5-match-started P['spawn'].pawn==1
chan exec God
sleep 1
probe t0
key t
sleep 1
mark
type wasd Hi
sleep 1
probe t1
check k5-typing-does-not-move-the-player dist('t0','t1') < 5
chan exec getall Console TypedStr
expect k5-typed-exactly Console.*TypedStr = Say wasd Hi\s*$ 10
key Return
sleep 1
chan exec getall Console TypedStr
expect k5-enter-sent-and-closed Console.*TypedStr = \s*$ 10
chan exec getall Console History
expect k5-the-line-ran-as-a-command Console.*History.*Say wasd Hi 10
chan exec getall Console bTyping
expect k5-not-typing-after-enter Console.*bTyping = False 10
key t
sleep 1
type nope
sleep 0.5
key Escape
sleep 1
chan exec getall Console bTyping
expect k5-one-escape-leaves-the-chat Console.*bTyping = False 10
chan exec getall Console TypedStr
expect k5-escape-discarded-the-text Console.*TypedStr = \s*$ 10
absent k5-cancelled-text-not-said Say nope
probe t2
keydown w
sleep 1
keyup w
sleep 0.5
probe t3
check k5-keys-move-again-after-chat dist('t2','t3') > 100
SCEN
run k5 240 DM-Deadbolt
fi

echo "d5-text: $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (logs in $out)"
[ "$fails" = 0 ]
