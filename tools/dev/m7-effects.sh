#!/usr/bin/env bash
# Regenerate the A/B pictures of M7 (render targets and post-processing) and print one line per effect.
#
#   tools/dev/m7-effects.sh [<tree>] [effect ...]        default tree: m7; default: every effect
#   tools/dev/m7-effects.sh m7 bloom praxis-panes
#   tools/dev/m7-effects.sh list
#
# Every effect is one run of the native client through tools/dev/run-uc2-shot.sh, i.e. inside
# <WorkUC2>/bin/capped 2G with a time limit, headless (Xvfb, software GL), 1280x720, the first PlayerStart
# pinned (`set UCDeathmatch NumPlayers 0`), Fire injected to start the match, two engine screenshots taken at
# fixed times after the first frame. Between the two screenshots either post-processing is switched on
# (-nopostprocess ... -glpptoggle: picture A = off = the M6 picture, picture B = on) or a console command is
# issued (-glexec). Nothing is left running. Output, stable names, in <WorkUC2>/logs/<tree>/:
#
#   fx-<effect>-a.png, fx-<effect>-b.png     the pair (what A and B are is in the table below)
#   fx-<effect>-ab.png                       both side by side, A left
#   fx-<effect>.out                          the game log of the run
#
# and one line per effect on stdout:
#
#   fx <effect>: A=<what> B=<what> critical=<n> glerrors=<n> programs_failed=<n> diff(A,B) mean=<m> changed=<p>% left_behind=<n> [ref: ...]
#
# `changed` is the share of pixels that differ at all between A and B. If fx-<effect>-b.ref.png exists (copy a
# picture you have looked at and accepted there), the line also gives the mean difference of B to it: a later
# change of the renderer (the GL4ES/WebGL twin at M11) shows up as a number, not only as a picture.
# Animated things (the pawn's idle pose, pickups, the HUD) differ between any two runs; expect a few percent.
#
# What the on side should look like is in docs/porting/analysis/postprocess.md (per-effect table). There is no
# Xbox reference picture for any of them.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=m7
if [ $# -gt 0 ] && [ -d "$work/run/$1" ]; then tree=$1; shift; fi
out=$work/logs/$tree
shot=$repo/tools/dev/run-uc2-shot.sh

# name | map | pinned spawn (1/0) | seconds | fire at | "A label" | "B label" | uc2.bin arguments
EFFECTS='
bloom|DM-Deadbolt|1|100|34|post-processing off (M6)|on: emissive glow and bloom on the lamps, always-on depth of field|-nopostprocess -glshot=44,56 -glpptoggle=48
blur|DM-Deadbolt|1|100|34|off|on + BlurMe (UUCScreenBlur, ConcussionBlur: displaced ghost copies)|-nopostprocess -glshot=44,56 -glpptoggle=48 -glexec=52:BlurMe
berserk|DM-Deadbolt|1|100|34|off|on + berserk colour remap (scene in the ramp colours, pawn excluded)|-nopostprocess -glshot=44,56 -glpptoggle=48 -glexec=40:set+UCScreenBerserk+Enabled+1
liandri|DM-Deadbolt|1|100|34|off|on + Liandri overlay (grey scale with noise)|-nopostprocess -glshot=44,56 -glpptoggle=48 -glexec=40:set+UCScreenLiandri+Enabled+1
pause|DM-Deadbolt|1|100|34|off|on + pause-menu colour remap|-nopostprocess -glshot=44,56 -glpptoggle=48 -glexec=40:set+UCScreenMenuPause+Enabled+1
radial|DM-Deadbolt|1|100|34|off|on + radial blur held at distance 30|-nopostprocess -glshot=44,56 -glpptoggle=48 -glexec=38:set+UCPlayerController+RadialBlurSpeed+0@38.2:set+UCPlayerController+RadialBlurMinChange+-1@38.4:set+UCScreenRadialBlur+RadialBlurDistance+30@40:set+UCScreenRadialBlur+Enabled+1
dyngamma|DM-Deadbolt|1|100|34|on, dynamic gamma ramp applied to the picture|on, DYNAMICGAMMA 0 (no ramp in the picture)|-glshotramp -glshot=50,60 -glexec=54:DYNAMICGAMMA+0
dof-alpha|DM-Azure|1|120|42|on, depth-of-field blur alpha as script sets it (third person: 100)|on, DOFSHIPPEDALPHA 1 (as the shipped binary: 65)|-glshot=56,68 -glexec=61:DOFSHIPPEDALPHA+1
azure|DM-Azure|1|120|42|off|on: depth of field on the distance, glow, water and torch distortion if in view|-nopostprocess -glshot=56,68 -glpptoggle=61
octane-glass|CTF-Octane|1|125|38|off|on: distortion glass (T_LI_Drill glass)|-nopostprocess -glshot=56,68 -glpptoggle=61
praxis-panes|DM-Praxis|0|115|40|off: flat periwinkle panes (the PC bug)|on: the sea seen through the panes, distorted|-nopostprocess -glshot=52,66 -glpptoggle=58
'

if [ "${1:-}" = list ]; then
  echo "$EFFECTS" | awk -F'|' 'NF>1 { printf "%-14s %-12s A: %s\n%27s B: %s\n", $1, $2, $6, "", $7 }'
  exit 0
fi
want=" $* "

stats() {  # stats <a.png> <b.png> [box] -> "mean=<m> changed=<p>%"
  python3 - "$@" <<'PY'
import sys
from PIL import Image, ImageChops, ImageStat
a=Image.open(sys.argv[1]).convert('RGB'); b=Image.open(sys.argv[2]).convert('RGB')
if a.size!=b.size:
    print('mean=nan changed=nan%'); sys.exit(0)
d=ImageChops.difference(a,b); r,g,bl=d.split()
anyd=ImageChops.lighter(ImageChops.lighter(r,g),bl).point(lambda v: 255 if v else 0)
print('mean=%.3f changed=%.2f%%' % (sum(ImageStat.Stat(d).mean)/3, 100*ImageStat.Stat(anyd).sum[0]/255/(a.size[0]*a.size[1])))
PY
}

echo "$EFFECTS" | while IFS='|' read -r name map pinned secs fire la lb args; do
  [ -n "$name" ] || continue
  if [ "$want" != "  " ] && [ "${want#* $name }" = "$want" ]; then continue; fi
  tries=1
  # DM-Praxis: the panes are not in view from the first PlayerStart. The start is then random; the run is
  # repeated (at most 6 times) until picture A shows the view of the wine reference logs/oracle/
  # DM-Praxis-windows-A.png (mean difference below 8 of 255; another start gives more than 20).
  [ "$name" = praxis-panes ] && tries=6
  try=0; ok=0
  while [ $try -lt $tries ] && [ $ok = 0 ]; do
    try=$((try+1))
    rm -f "$out/fx-$name-0.png" "$out/fx-$name-1.png" "$out/fx-$name-a.png" "$out/fx-$name-b.png" "$out/fx-$name-ab.png"
    if [ "$pinned" = 1 ]; then
      report=$(UC2_RES=1280x720 UC2_PRECMD="set UCDeathmatch NumPlayers 0" "$shot" "$tree" "fx-$name" "$secs" "$fire" "$map" -glerrors $args 2>&1)
    else
      report=$(UC2_RES=1280x720 "$shot" "$tree" "fx-$name" "$secs" "$fire" "$map" -glerrors $args 2>&1)
    fi
    ok=1
    if [ "$name" = praxis-panes ] && [ -f "$out/fx-$name-0.png" ] && [ -f "$work/logs/oracle/DM-Praxis-windows-A.png" ]; then
      m=$(stats "$out/fx-$name-0.png" "$work/logs/oracle/DM-Praxis-windows-A.png" | sed -E 's/mean=([0-9.]+).*/\1/')
      python3 -c "import sys; sys.exit(0 if float('$m') < 8 else 1)" || ok=0
    fi
  done
  log=$out/fx-$name.out
  crit=$(grep -a -c "Critical" "$log" 2>/dev/null)
  glerr=$(grep -a "GL errors so far" "$log" 2>/dev/null | tail -1 | sed -E 's/.*, ([0-9]+) GL errors.*/\1/')
  pfail=$(grep -a "UC2GLDrv: programs:" "$log" 2>/dev/null | tail -1 | sed -E 's/.*linked, ([0-9]+) failed; fragment shaders [0-9]+ compiled, ([0-9]+) failed.*/\1+\2/')
  left=$(echo "$report" | grep -c "WARNING")
  if [ -f "$out/fx-$name-0.png" ] && [ -f "$out/fx-$name-1.png" ]; then
    mv -f "$out/fx-$name-0.png" "$out/fx-$name-a.png"; mv -f "$out/fx-$name-1.png" "$out/fx-$name-b.png"
    python3 -c "
import sys
from PIL import Image
a=Image.open(sys.argv[1]).convert('RGB'); b=Image.open(sys.argv[2]).convert('RGB')
o=Image.new('RGB',(a.size[0]+b.size[0]+8,max(a.size[1],b.size[1])),(255,0,255)); o.paste(a,(0,0)); o.paste(b,(a.size[0]+8,0)); o.save(sys.argv[3])" "$out/fx-$name-a.png" "$out/fx-$name-b.png" "$out/fx-$name-ab.png"
    st=$(stats "$out/fx-$name-a.png" "$out/fx-$name-b.png")
    ref=""
    [ -f "$out/fx-$name-b.ref.png" ] && ref=" ref: B vs accepted $(stats "$out/fx-$name-b.png" "$out/fx-$name-b.ref.png")"
    echo "fx $name: A=[$la] B=[$lb] critical=${crit:-?} glerrors=${glerr:-?} programs_failed=${pfail:-?} diff(A,B) $st left_behind=$left tries=$try$ref"
  else
    echo "fx $name: FAILED, no pair of pictures (see $log) critical=${crit:-?} left_behind=$left tries=$try"
  fi
done
