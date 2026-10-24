#!/usr/bin/env bash
# Saves check (native): which files does the game write, and does a saved profile survive a restart?
#
#   tools/dev/c1-saves.sh
#
# Needs the native run tree <repo>/../run/c1 (tools/dev/mkrun-uc2.sh build-native c1; C1_TREE=<name> to change).
# Everything runs inside <repo>/../bin/capped (the client under Xvfb through m10-controls.sh, 2G each).
# Output in <repo>/../logs/c1/saves/ (C1_OUT=<dir>): before.tsv / after1.tsv / after2.tsv (every file of the
# tree: path, size, mtime, link count), written1.txt / written2.txt (what the run added or changed), the
# runs' logs and screenshots, and one PASS/FAIL line per check on stdout.
#
# Run 1 starts from a fresh configuration (our tracked inis, no Avaris.ini/User.ini, no profiles, no logs),
# creates a profile on the on-screen keyboard page (UC2ProfileKeyboard, typed on the real keyboard: m10-controls.sh's
# `text` scenario), leaves the profile page, plays a deathmatch on DM-Deadbolt for a few seconds and is stopped
# with SIGTERM (the normal way out here). Run 2 starts from the same tree (M10_KEEP=1: nothing reset), opens the
# profile page, expects the saved profile in the list, selects it, and asks the engine which profile the
# viewport holds (`getall PlayerProfile ProfileName`). The listings diff is the file inventory.
# The restarted game is in the saved profile from the start (Engine/Src/UC2Profiles.cpp); run 2 checks
# that first (tools/dev/d3-profiles.sh is the full restart test).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${C1_TREE:-c1}
root=$work/run/$tree
sys=$root/System
out=${C1_OUT:-$work/logs/c1/saves}
m10=$repo/tools/dev/m10-controls.sh
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }
name=${C1_PROFILE:-Saver}

fails=0
result() { echo "$1 $2: $3"; [ "$1" = PASS ] || fails=$((fails+1)); }
check() { local n=$1 d; shift; if d=$("$@" 2>&1); then result PASS "$n" "$d"; else result FAIL "$n" "$d"; fi; }

# listing <file>: every file in the tree, sorted: path <TAB> size <TAB> mtime (epoch seconds) <TAB> link count.
listing() { ( cd "$root" && find . -type f -printf '%p\t%s\t%T@\t%n\n' | sed 's/\.[0-9]*\t\([0-9]*\)$/\t\1/' | sort ) > "$1"; }
# written <before> <after> <out>: files that are new, changed (size or mtime), or gone.
written() {
  awk -F'\t' 'NR==FNR { sz[$1]=$2; mt[$1]=int($3); next }
              { seen[$1]=1; if (!($1 in sz)) printf "new      %s\t%s bytes\n", $1, $2;
                else if (sz[$1]!=$2 || mt[$1]!=int($3)) printf "changed  %s\t%s -> %s bytes\n", $1, sz[$1], $2 }
              END { for (p in sz) if (!(p in seen)) printf "removed  %s\n", p }' "$1" "$2" | sort -k2 > "$3"
}

# --- a fresh configuration (what m10-controls.sh's prep does; it is skipped below with M10_KEEP) -------------
cp -f "$repo"/Engine/Config/*.ini "$sys/"
sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$sys"/*.log "$sys"/*.log.*
rm -rf "$sys/Profiles"
listing "$out/before.tsv"

# --- run 1: create a profile, play ----------------------------------------------------------------------------
cat > "$out/run1.scen" <<SCEN
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
focus
seek 8 P['_'].caption == 'PROFILES' ;; key Down
key Return
expect s1-profile-page-opens OpenMenu \[UCMenu.UC2Profile\] 15
wait OpenMenu \[GUI.GUIMessageBox\] 20
key Return
expect s1-keyboard-opens OpenMenu \[UCMenu.UC2ProfileKeyboard\] 15
type $name
tryseek 20 len(P['_'].text) >= len('$name') ;; sleep 0.5
probe k1
check s1-typed-the-name P['k1'].text == '$name'
key Return
expect s1-keyboard-closes CloseMenu \[UC2ProfileKeyboard\] 10
wait UC2Profile Unblocking animation 60
shot created
chan exec getall PlayerProfile ProfileName
expect s1-new-profile-is-the-viewports-profile ProfileName = $name\s*$ 10
key Escape
expect s1-profile-page-closes CloseMenu \[UC2Profile\] 10
wait UC2MainMenu Unblocking animation 60
seek 8 P['_'].caption == 'INSTANT ACTION' ;; key Up
key Return
expect s1-main-menu-opens-arena OpenMenu \[UCMenu.UC2Arena_Main\] 15
wait UC2Arena_Main Unblocking animation 60
seek 4 P['_'].menu != 'GUIMessageBox' ;; key Escape
seek 40 P['_'].menu == 'UC2Arena_Main' ;; sleep 0.5
seek 10 P['_'].item.startswith('DeathMatch|') ;; key Down
key Return
seek 40 P['_'].item == '' ;; sleep 0.5
seek 10 P['_'].item != '' ;; key Return
seek 40 '|' in P['_'].item and P['_'].item != 'NOT SET' ;; sleep 0.5
seek 40 'DM-Deadbolt' in P['_'].item ;; key Down
key Return
expect s1-map-chosen-opens-character OpenMenu \[UCMenu.UC2Arena_Character\] 15
chan exec getall PlayerProfile CharacterClassName
wait CharacterClassName = \S 10
key Right
wait handled XC_PadRight 30
shot character
key Return
expect s1-character-opens-armory OpenMenu \[UCMenu.UC2Arena_Armory\] 15
seek 40 '|' in P['_'].item ;; sleep 0.5
probe armory0
seek 10 P['_'].item != P['armory0'].item ;; key Return
seek 40 '|' in P['_'].item ;; sleep 0.5
seek 10 P['_'].menu == 'ConfirmLaunchMenu' ;; key Return
check s1-armory-opens-launch-confirm P['_'].menu == 'ConfirmLaunchMenu'
key Return
expect s1-travel-to-deathmatch Game class is 'UCDeathmatch' 120
expect s1-deadbolt-up Bringing Level DM-Deadbolt.MyLevel up for play 60
wait Completed Dynamic Load 300
tryseek 10 P['_'].pawn==1 ;; key Alt_L 1
probe spawn
check s1-match-started-pawn-spawned P['spawn'].pawn==1 and P['spawn'].level=='DM-Deadbolt'
chan exec getall PlayerProfile CharacterClassName
wait CharacterClassName = \S 10
keydown w
sleep 2
keyup w
sleep 0.5
shot match
SCEN
echo "== run 1: fresh tree, create profile '$name', play (client, Xvfb)"
M10_KEEP=1 M10_TREE=$tree M10_OUT=$out M10_SECS=420 "$m10" --file "$out/run1.scen" run1 | sed 's/^/    /'
listing "$out/after1.tsv"
written "$out/before.tsv" "$out/after1.tsv" "$out/written1.txt"
cp -f "$sys/Avaris.log" "$out/run1-Avaris.log" 2>/dev/null
echo "-- files the first run added or changed (written1.txt):"; sed 's/^/    /' "$out/written1.txt"

profile=$sys/Profiles/$name/AVARIS_PROFILE.PRO
check s1-profile-file-written test -s "$profile"
check s1-run1-checks-passed bash -c "! grep -a -q '^FAIL' '$out/run1.result' && grep -a -c '^PASS' '$out/run1.result'"
check s1-nothing-written-outside-System bash -c "! grep -a -v -E '^(new|changed|removed) +\./System/' '$out/written1.txt' | grep -a -q ."
check s1-profile-name-is-the-directory test -d "$sys/Profiles/$name"
check s1-profile-directory-holds-one-file bash -c "[ \$(ls '$sys/Profiles/$name' | wc -l) = 1 ] && ls '$sys/Profiles/$name'"
profile_sum=$(md5sum < "$profile" 2>/dev/null)

# --- run 2: the same tree, a new process ------------------------------------------------------------------------
cat > "$out/run2.scen" <<SCEN
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
focus
chan exec getall PlayerProfile ProfileName
expect s2-at-start-the-viewport-profile-is-the-saved-one ProfileName = $name\s*$ 10
seek 8 P['_'].caption == 'PROFILES' ;; key Down
mark
key Return
expect s2-profile-page-opens OpenMenu \[UCMenu.UC2Profile\] 15
wait UC2Profile Unblocking animation 60
absent s2-no-empty-list-dialog OpenMenu \[GUI.GUIMessageBox\]
shot list
key Return
wait handled XC_A 30
shot selected
key Escape
expect s2-leaving-the-page-loads-the-profile CloseMenu \[UC2Profile\] 10
wait UC2MainMenu Unblocking animation 60
chan exec getall PlayerProfile ProfileName
expect s2-the-viewport-profile-is-the-saved-one ProfileName = $name\s*$ 10
chan exec getall PlayerProfile CharacterClassName
wait CharacterClassName = \S 10
SCEN
echo "== run 2: the same tree after a restart (client, Xvfb)"
M10_KEEP=1 M10_TREE=$tree M10_OUT=$out M10_SECS=240 "$m10" --file "$out/run2.scen" run2 | sed 's/^/    /'
listing "$out/after2.tsv"
written "$out/after1.tsv" "$out/after2.tsv" "$out/written2.txt"
cp -f "$sys/Avaris.log" "$out/run2-Avaris.log" 2>/dev/null
echo "-- files the second run added or changed (written2.txt):"; sed 's/^/    /' "$out/written2.txt"
check s2-run2-checks-passed bash -c "! grep -a -q '^FAIL' '$out/run2.result' && grep -a -c '^PASS' '$out/run2.result'"
check s2-profile-survived-the-restart test -s "$profile"
check s2-no-load-failure-logged bash -c "! grep -a -q 'Unable to load profile' '$out/run2.out' '$out/run2-Avaris.log'"
# The value the character page changed in run 1 (the last getall of run 1 reads the changed one) is the value
# the loaded profile holds in run 2: the file, not the class defaults, is what the second process read.
c_before=$(grep -a "PlayerProfile Package.PlayerProfile.CharacterClassName" "$out/run1.out" | head -1 | sed 's/.* = //' | tr -d '\r')
c_after=$(grep -a "PlayerProfile Package.PlayerProfile.CharacterClassName" "$out/run1.out" | tail -1 | sed 's/.* = //' | tr -d '\r')
c_loaded=$(grep -a "PlayerProfile Package.PlayerProfile.CharacterClassName" "$out/run2.out" | tail -1 | sed 's/.* = //' | tr -d '\r')
check s1-character-page-changed-the-profile-value test -n "$c_after" -a "$c_before" != "$c_after"
check s2-restarted-process-holds-the-value-saved-in-run-1 bash -c "[ -n '$c_loaded' ] && [ '$c_loaded' = '$c_after' ] && echo 'CharacterClassName: default $c_before, saved $c_after, loaded after restart $c_loaded'"
check s2-profile-bytes-intact-or-rewritten-by-the-game bash -c "echo 'before run 2: $profile_sum'; echo 'after run 2:  '\$(md5sum < '$profile')"

echo "c1-saves: $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (logs in $out)"
[ "$fails" = 0 ]
