#!/usr/bin/env bash
# Y-1 check (native): the names a System Link HOST shows, driven through the game's own menus as a player drives them
# (main menu -> NETWORKED COMBAT -> HOST SYSTEM LINK GAME -> the host page -> the character page with the default
# character, no change -> armory -> Begin Match), then asked for its beacon over UDP as the System Link page asks
# (tools/dev/y1/y1-beacon.py: "REPORT?<nonce>" to the UdpBeacon, GameInfo.GetBeaconText() back).
#
#   tools/dev/y1-names.sh <tree> rebuilt|vanilla [h1] [h2] [h3] [h4]   (default: all four, in this order)
#
#   <tree>    a native run tree under <WorkUC2>/run (tools/dev/mkrun-uc2.sh build-native <tree>; for vanilla
#             UC2_SCRIPTS=none tools/dev/mkrun-uc2.sh build-native <tree>: Epic's shipped GUI.u/UCMenu.u)
#   h1  no profile, launched with the launcher's names (-ini:User:[DefaultPlayer]:Name=Coordinator,
#       -ini:System:[Engine.GameReplicationInfo]:ServerName=MyBox). rebuilt: the match URL, the player's
#       PlayerReplicationInfo and the beacon's player list say Coordinator, the beacon's server field MyBox.
#       vanilla (Epic's rule): Anubis (the default character) and Liandri Corporation (the profile default).
#   h2  fresh tree, the same launch, but a profile "Proffy" is created on the Profiles page first: the profile names
#       the player (Proffy) and the session (its server name, the profile default Liandri Corporation), in both
#       variants.
#   h3  restart after h2 with another launcher server name (OtherBox): the game starts in profile Proffy, whose own
#       names win (Proffy, Liandri Corporation), not the launcher's.
#   h4  restart after h3: the profile is deleted on the Profiles page, then a host without a profile: rebuilt
#       Coordinator / OtherBox (the launcher's again); vanilla Anubis / Liandri Corporation.
#
# Every game run is one client under Xvfb inside <WorkUC2>/bin/capped 2G (tools/dev/m10-controls.sh --file). The
# beacon and game ports are moved off the defaults (UDP Y1_BEACON_PORT, default 18777; game Y1_GAME_PORT, 17777) with
# -ini: so another run on the machine cannot answer. Output in <WorkUC2>/logs/y1/native/<tree>-<part>.*
# (Y1_OUT=<dir>); one PASS/FAIL line per check on stdout.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?usage: y1-names.sh <tree> rebuilt|vanilla [h1..h4]}
variant=${2:?usage: y1-names.sh <tree> rebuilt|vanilla [h1..h4]}
shift 2
parts=${*:-h1 h2 h3 h4}
sys=$work/run/$tree/System
out=${Y1_OUT:-$work/logs/y1/native}
m10=$repo/tools/dev/m10-controls.sh
helper=$repo/tools/dev/y1/y1-beacon.py
bport=${Y1_BEACON_PORT:-18777}
gport=${Y1_GAME_PORT:-17777}
player=Coordinator
server=MyBox
mkdir -p "$out"
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys" >&2; exit 2; }
case "$variant" in rebuilt|vanilla) ;; *) echo "variant must be rebuilt or vanilla" >&2; exit 2;; esac

fails=0
result() { echo "$1 $2: $3"; [ "$1" = PASS ] || fails=$((fails+1)); }

# The host flow from the main menu (focus on its first item, or on PROFILES after h2's profile page).
host_flow() {  # host_flow <nag: 1 = the "no profile loaded" box comes up on the host page>
  cat <<SCEN
seek 8 P['_'].caption == 'NETWORKED COMBAT' ;; key \$MOVE
key Return
wait OpenMenu \\[UCMenu.UC2NetworkMain\\] 15
sleep 2
seek 8 P['_'].caption == 'HOST SYSTEM LINK GAME' ;; key Down
key Return
expect host-page-opens OpenMenu \\[UCMenu.UC2Arena_ServerMain\\] 15
SCEN
  if [ "$1" = 1 ]; then
    cat <<'SCEN'
wait OpenMenu \[GUI.GUIMessageBox\] 20
wait GUIMessageBox Unblocking animation 30
sleep 0.5
key Escape
SCEN
  fi
  cat <<'SCEN'
sleep 2
key Return
wait handled XC_A 15
sleep 2
shot host-rules
key Return
wait handled XC_A 15
sleep 2
key Return
tryseek 10 P['_'].menu == 'UC2Arena_Character' ;; sleep 1
check character-page P['_'].menu == 'UC2Arena_Character'
sleep 2
shot character
key Return
sleep 2
key Return
sleep 2
key Return
sleep 2
key Return
SCEN
}

# What the host shows: its match URL, its player's PlayerReplicationInfo name, its beacon.
host_checks() {  # host_checks <player> <server>
  cat <<SCEN
expect match-url-names-the-player LoadMap: [A-Z]+-[A-Za-z]+\\?.*Name=$1\\?.*listen 120
expect level-up Bringing Level (?!Menu)[A-Z]+-[A-Za-z]+\\.MyLevel up for play 60
sleep 8
chan exec getall PlayerReplicationInfo PlayerName
expect player-pri-name PlayerReplicationInfo.*PlayerName = $1\\s*\$ 10
pyfile $helper
pyexec beacon($bport)
check beacon-answers len(BEACON) >= 10
check beacon-server-name BEACON[1] == '$2'
check beacon-player-names BEACON[8].split(';') == ['$1']
shot match
SCEN
}

run_part() {  # run_part <part> <player> <server> <scenario file>   (env: SRV = the launcher's server name, KEEP=1)
  local name=$tree-$1
  echo "== $name ($variant): launched with Name=$player ServerName=$SRV; expect player '$2', server '$3'"
  M10_KEEP=${KEEP:-} M10_TREE=$tree M10_OUT=$out M10_SECS=420 "$m10" --file "$4" "$name" \
    "-ini:User:[DefaultPlayer]:Name=$player" "-ini:System:[Engine.GameReplicationInfo]:ServerName=$SRV" \
    "-ini:System:[IpDrv.UdpBeacon]:ServerBeaconPort=$bport" "-ini:System:[URL]:Port=$gport" | sed 's/^/    /'
  while read -r l; do
    case "$l" in PASS*) result PASS "$1-${l#PASS }" "";; FAIL*) result FAIL "$1-${l#FAIL }" "";; esac
  done < <(grep -a -E '^(PASS|FAIL)' "$out/$name.result" 2>/dev/null | sed -E 's/^(PASS|FAIL)[[:space:]]+([^[:space:]]+).*/\1 \2/')
  [ -f "$out/$name.result" ] || result FAIL "$1-result" "no result file"
  grep -a -m1 "beacon reply" "$out/$name.inject" 2>/dev/null | sed 's/^ */    /'
  local crit; # (the Entry level's minimal PlayerController reads its PlayerReplicationInfo before it has one on every start,
  # with any packages: not counted)
  crit=$(grep -a "^Critical\|Signal: SIG[^T]\|Accessed None\|Accessed array" "$out/$name.out" 2>/dev/null \
    | grep -a -v -c "Entry.UCMinimalPlayerController0 (Function Engine.PlayerController.PostBeginPlay" || true)
  [ "${crit:-0}" = 0 ] && result PASS "$1-no-critical" "0 Critical/signal/Accessed None lines (but the Entry one)" || result FAIL "$1-no-critical" "$crit"
  grep -a "SetProfileServerName\|Profiles: remembered\|Profiles: viewport" "$out/$name.out" 2>/dev/null | head -4 | sed 's/^/    log: /'
  return 0
}

main_menu() {
  cat <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 90
sleep 3
focus
SCEN
}
profiles_page() {  # the main menu's PROFILES item (the fourth)
  cat <<'SCEN'
key Down
key Down
key Down
sleep 0.5
key Return
expect profile-page-opens OpenMenu \[UCMenu.UC2Profile\] 15
SCEN
}

# Epic's rule (vanilla) or Y-1's (rebuilt) for a player and a session without a profile
nopro_player() { [ "$variant" = rebuilt ] && echo "$player" || echo Anubis; }
nopro_server() { [ "$variant" = rebuilt ] && echo "$1" || echo "Liandri Corporation"; }

for part in $parts; do
  case $part in
  h1)  # fresh tree, no profile
    SRV=$server; KEEP=
    { main_menu; host_flow 1 | sed 's/\$MOVE/Down/'; host_checks "$(nopro_player)" "$(nopro_server "$SRV")"; } > "$out/$tree-h1.scen"
    run_part h1 "$(nopro_player)" "$(nopro_server "$SRV")" "$out/$tree-h1.scen"
    ;;
  h2)  # fresh tree, a profile is created first: its names win (its name; its server name, the profile default)
    SRV=$server; KEEP=
    { main_menu; profiles_page; cat <<'SCEN'
wait OpenMenu \[GUI.GUIMessageBox\] 20
sleep 1.5
key Return
expect keyboard-opens OpenMenu \[UCMenu.UC2ProfileKeyboard\] 15
sleep 3
type Proffy
sleep 1
key Return
expect keyboard-closes CloseMenu \[UC2ProfileKeyboard\] 10
sleep 3
key Escape
expect profile-page-closes CloseMenu \[UC2Profile\] 10
sleep 2
SCEN
      host_flow 0 | sed 's/\$MOVE/Up/'; host_checks Proffy "Liandri Corporation"; } > "$out/$tree-h2.scen"
    run_part h2 Proffy "Liandri Corporation" "$out/$tree-h2.scen"
    ;;
  h3)  # restart after h2 with ANOTHER launcher server name: the game starts in profile Proffy, whose names win
    SRV=OtherBox; KEEP=1
    { main_menu; host_flow 0 | sed 's/\$MOVE/Down/'; host_checks Proffy "Liandri Corporation"; } > "$out/$tree-h3.scen"
    run_part h3 Proffy "Liandri Corporation" "$out/$tree-h3.scen"
    ;;
  h4)  # restart after h3: the profile is deleted on the Profiles page, then a host without a profile again
    SRV=OtherBox; KEEP=1
    { main_menu; profiles_page; cat <<'SCEN'
sleep 3
key x
wait OpenMenu \[GUI.GUIMessageBox\] 15
sleep 1.5
key Return
expect profile-forgotten Profiles: remembered profile "" 15
sleep 3
seek 3 P['_'].menu == 'UC2MainMenu' ;; key Escape
sleep 2
SCEN
      host_flow 1 | sed 's/\$MOVE/Up/'; host_checks "$(nopro_player)" "$(nopro_server "$SRV")"; } > "$out/$tree-h4.scen"
    run_part h4 "$(nopro_player)" "$(nopro_server "$SRV")" "$out/$tree-h4.scen"
    ;;
  *) echo "unknown part $part" >&2; fails=$((fails+1));;
  esac
done

echo "y1-names $tree ($variant): $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (logs in $out)"
[ "$fails" = 0 ]
