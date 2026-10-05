#!/usr/bin/env bash
# M9 / V3: the movie player's numeric self-test, headless (no window, no audio device).
#
#   tools/dev/movie-selftest.sh [<run-tree-name>]        (default m9; the tree from tools/dev/mkrun-uc2.sh)
#
# 1. ffmpeg (inside bin/capped) writes the references of a sample of cooked movies into
#    <WorkUC2>/logs/<tree>/selftest/: per-picture Adler-32 of the decoded Y'CbCr (framecrc, with its frame
#    number), the packet count, two pictures converted to BGRA by ffmpeg's swscale (BT.601, limited range,
#    chroma replicated per 2x2 as the player does: neighbor to 4:4:4 first), and the sound decoded to 16-bit
#    PCM. Every ffmpeg decode runs with -threads 1: ffmpeg's frame-threaded Theora decoder is not
#    deterministic (TVDisplaySelket2: two runs, two different framecrc lists; see docs/porting/analysis/video.md).
# 2. `ucc-bin UC2Movie.UC2MovieSelfTest REF=<dir>` decodes the same files with the game's player and prints one
#    PASS/FAIL line per check (summary in <dir>/selftest.out).
set -uo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:-m9}
run=$work/run/$tree
ref=$work/logs/$tree/selftest
capped=$work/bin/capped
[ -x "$run/System/ucc-bin" ] || { echo "no $run/System/ucc-bin (tools/dev/mkrun-uc2.sh build-native $tree)" >&2; exit 2; }
rm -rf "$ref"; mkdir -p "$ref"

# case  video (relative to System/)  sound file or -  language of the sound  kind
cat > "$ref/cases.txt" <<'EOF'
logos ../Movies/UC2_Logos.ogv ../Movies/UC2_Logos.ogv int movie
asc3deu ../Movies/03_AscPre3.ogv ../Movies/03_AscPre3_deu.oga deu movie
loading ../Textures/Movies/UI_LoadingMovie.ogv - - clip
deadbolt ../Textures/Movies/Portrait_Arena_Deadbolt.ogv - - clip
anubis ../Textures/Movies/Portrait_Char_Anubis.ogv - - clip
reactor ../Textures/Movies/Reactor_01.ogv - - clip
tvselket ../Textures/Movies/TVDisplaySelket2.ogv - - clip
EOF

while read -r name video audio lang kind; do
  v=$run/System/$video
  # every decoded picture: stream, dts, pts, duration, size, adler32 of the raw yuv420p frame
  "$capped" 2G 600 -- ffmpeg -nostdin -v error -threads 1 -i "$v" -map 0:v:0 -f framecrc - > "$ref/$name.vcrc"
  "$capped" 2G 600 -- ffprobe -v error -count_packets -select_streams v:0 -show_entries stream=nb_read_packets \
    -of csv=p=0 "$v" > "$ref/$name.packets"
  # two pictures through swscale: the first, and the decoded picture at the middle of the list
  mid=$(awk -F', *' '!/^#/{n++; p[n]=$3} END{print p[int(n/2)]}' "$ref/$name.vcrc")
  for f in 0 "$mid"; do
    "$capped" 2G 600 -- ffmpeg -nostdin -v error -threads 1 -i "$v" -map 0:v:0 \
      -vf "select=eq(pts\\,$f),scale=flags=neighbor+bitexact,format=yuv444p,scale=in_color_matrix=bt601:in_range=tv:out_range=pc:flags=neighbor+accurate_rnd+bitexact,format=bgra" \
      -frames:v 1 -f rawvideo "$ref/$name.bgra$f"
  done
  echo "$mid" > "$ref/$name.mid"
  if [ "$audio" != - ]; then
    "$capped" 2G 600 -- ffmpeg -nostdin -v error -threads 1 -i "$run/System/$audio" -map 0:a:0 -f s16le "$ref/$name.s16"
    # the sound's length as the cook measured it in the XMV (movie-manifest.json, samples_in of that output)
    python3 -c 'import json,sys,os; m=json.load(open(sys.argv[1])); b=os.path.basename(sys.argv[2])
print([o["samples_in"] for r in m["movies"] for o in r["outputs"] if os.path.basename(o["file"])==b][0])' \
      "$run/Movies/movie-manifest.json" "$audio" > "$ref/$name.samples"
  fi
done < "$ref/cases.txt"

cd "$run/System" && "$capped" 2G 900 -- ./ucc-bin UC2Movie.UC2MovieSelfTest "REF=$ref" 2>&1 | tee "$ref/selftest.out" \
  | grep -a "PASS\|FAIL\|UC2MovieSelfTest"
grep -aq "UC2MovieSelfTest: .* 0 failed" "$ref/selftest.out"
