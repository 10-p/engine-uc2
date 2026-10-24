#!/usr/bin/env bash
# Language check: the language setting ([Engine.Engine] Language, seeded by the launcher with -ini:) reaches the menu text, the
# localised voice banks, the cinematic sound track and the loading screen. Native, headless, repeatable.
#
#   tools/dev/g4-language.sh [<tree>] [case ...]      default tree: g4; default: every case
#   tools/dev/g4-language.sh g4 menu-deu match-deu
#   UC2_GPU=1 tools/dev/g4-language.sh g4 menu-deu   render on this machine's GPU (SDL's offscreen video driver, EGL,
#                                                    no window); the pictures get the suffix "-gpu"
#
# Cases: menu-<lang>  the menu level: the main menu's focused caption (input test channel probe) and a picture;
#        match-<lang> from the menu, open DM-Deadbolt?StartCinematic=04a_AscPre5: the loading screen's message, the
#                     cinematic's sound track, a localised class default (GET UCDeathmatch GameDescription), Fire (the
#                     match starts) and the announcer's wave pack (A_Ann_Female[_<lang>].ogp).
#        <lang> = int, deu, fra, ita, spa (the languages the cooked assets carry).
# Every run: <repo>/../bin/capped 2G, xvfb-run (software GL unless UC2_GPU), 1280x720, -log, -UFRONTLOG,
# -ini:System:[Engine.Engine]:Language=<lang>, -ini:System:[UC2Movie]:StartupMovies=False, the input test channel.
# Output in <repo>/../logs/g4/native/: <case>[-gpu].out (console), .log (System/Avaris.log), .inject, .result, <case>[-gpu]-<shot>.png and one
# summary line per case.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=g4
if [ $# -gt 0 ] && [ -d "$work/run/$1" ]; then tree=$1; shift; fi
sys=$work/run/$tree/System
out=$work/logs/g4/native
inject=$repo/tools/dev/m10-inject.py
[ -x "$sys/uc2.bin" ] || { echo "no run tree at $sys (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }
mkdir -p "$out"
suffix=""; video=""
[ -n "${UC2_GPU:-}" ] && { video="SDL_VIDEODRIVER=offscreen"; suffix="-gpu"; }

cases=${*:-menu-int menu-deu menu-fra menu-ita menu-spa match-deu}
for c in $cases; do
  kind=${c%%-*}; lang=${c#*-}
  tag=$c$suffix
  scen=$(mktemp)
  case $kind in
  menu)
    url=""; secs=150
    cat > "$scen" <<'SCEN'
wait OpenMenu \[UCMenu.UC2MainMenu\] 120
sleep 4
probe main
chan exec GET GameInfo DefaultPlayerName
shot main-menu
sleep 2.5
shot main-menu-later
sleep 1
SCEN
    ;;
  match)
    # A map on the command line loads before the window exists (no movie can show there): the match is opened
    # from the menu, as the game's own ladder travels (tools/dev/m9-movies.py does the same with EXEC=).
    url=""; secs=240
    {
      echo 'wait OpenMenu \[UCMenu.UC2MainMenu\] 120'
      echo 'sleep 2'
      echo 'chan exec open DM-Deadbolt?StartCinematic=04a_AscPre5?Name=G4'
      echo 'wait UC2Movie: loading screen for DM-Deadbolt 60'
      echo 'wait Bringing Level DM-Deadbolt.MyLevel up for play 120'
      echo 'wait UC2Movie: opened \S+04a_AscPre5 60'
      echo 'wait UC2Movie: \S+04a_AscPre5.xmv ended 60'
      echo 'sleep 4'
      echo 'chan exec GET UCDeathmatch GameDescription'
      # Fire through the virtual pad (the input test channel): no window focus needed, software GL or GPU alike.
      printf 'pad attach\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\n'
      echo 'wait read wave pack A_Ann_Female 60'
      echo 'sleep 2'
      echo 'shot match'
      echo 'sleep 1'
    } > "$scen"
    ;;
  *) echo "unknown case $c" >&2; rm -f "$scen"; continue;;
  esac
  cp -f "$repo"/Engine/Config/*.ini "$sys/"
  sed -i -E "s/^(WindowedViewportX|FullscreenViewportX)=.*/\\1=1280/; s/^(WindowedViewportY|FullscreenViewportY)=.*/\\1=720/" "$sys/Default.ini"
  # The GUI's own log lines (OpenMenu ...) are what the scenario waits on: unsuppressed in this run tree only.
  sed -i '/^Suppress=DevUI\r\?$/d; /^Suppress=DevGUI\r\?$/d' "$sys/Default.ini"
  rm -f "$sys/Avaris.ini" "$sys/User.ini" "$sys"/Shot*.bmp "$out/$tag"-*.png "$out/$tag".result*
  chan=$out/$tag.channel; : > "$chan"
  inner=$(mktemp)
  cat > "$inner" <<INNER
cd "$sys" || exit 1
SDL_AUDIODRIVER=dummy $video ./uc2.bin $url -log -UFRONTLOG -ini:System:[Engine.Engine]:Language=$lang -ini:System:[UC2Movie]:StartupMovies=False -ini:System:[SDL2Drv.SDL2Client]:InputTestFile="$chan" > "$out/$tag.out" 2>&1 &
pid=\$!
python3 "$inject" "$scen" "$out/$tag.out" "$chan" "$out/$tag.result" > "$out/$tag.inject" 2>&1
kill -TERM \$pid 2>/dev/null
for i in \$(seq 20); do kill -0 \$pid 2>/dev/null || break; sleep 0.5; done
kill -KILL \$pid 2>/dev/null
wait \$pid 2>/dev/null
exit 0
INNER
  "$work/bin/capped" 2G "$secs" -- xvfb-run -a -s "-screen 0 1280x720x24" bash "$inner"
  rc=$?
  rm -f "$inner" "$scen"
  # The engine's own log file has the Init lines that come before the console echo (-log) starts.
  cp -f "$sys/Avaris.log" "$out/$tag.log" 2>/dev/null
  shots=""
  if [ -f "$out/$tag.result.shots" ]; then
    i=0
    while read -r shot; do
      bmp=$(printf "%s/Shot%05d.bmp" "$sys" "$i")
      [ -f "$bmp" ] && python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$bmp" "$out/$tag-$shot.png" && shots="$shots $shot"
      i=$((i+1))
    done < "$out/$tag.result.shots"
    rm -f "$sys"/Shot*.bmp
  fi
  export LC_ALL=C   # byte-wise greps over the Latin-1 log
  # What the log says about the language (latin-1 text: the localised strings are the game's own bytes)
  said=$(grep -a -h -E "^(Init: )?Language: " "$out/$tag.log" "$out/$tag.out" 2>/dev/null | head -1 | tr -d '\r')
  caption=$(grep -a -o "caption='[^']*'" "$out/$tag.inject" | head -1)
  movie=$(grep -a -o "language [a-z]*: [^)]*" "$out/$tag.out" | head -1)
  loading=$(grep -a -o 'UC2Movie: loading screen for [^,]*, "[^"]*"' "$out/$tag.out" | head -1 | sed 's/.*, //')
  pack=$(grep -a -o "read wave pack A_Ann_Female[A-Za-z_]*\.ogp" "$out/$tag.out" | head -1 | sed 's/read wave pack //')
  falsch=$(grep -a -o "bEnableStatLogging [A-Za-z]*" "$out/$tag.out" | head -1)
  desc=$(grep -a -A1 "GET UCDeathmatch GameDescription" "$out/$tag.out" | tail -1 | tr -d '\r')
  scenario=$(grep -a -c '^PASS\|^FAIL' "$out/$tag.result" 2>/dev/null)
  failed=$(grep -a '^FAIL' "$out/$tag.result" 2>/dev/null | head -1)
  echo "g4 $tag: [$said] [$falsch] $caption movie=[$movie] loading=[$loading] ${desc:+get=[$desc]} announcer=[$pack] shots=[${shots# }] capped_exit=$rc ${failed:+FAILED: $failed}" \
    | iconv -f latin1 -t utf-8
done
