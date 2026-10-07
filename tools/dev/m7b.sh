#!/usr/bin/env bash
# M7b verification: the Xbox shadow textures, the Xbox static projector batching and the three-layer terrain pass,
# each against the PC path, on real content, with one PASS/FAIL line per check. Notebook:
# docs/porting/analysis/projectors.md.
#
#   tools/dev/m7b.sh [<tree>] [check ...]      default tree m7b (made from build-native when missing); default: all
#   tools/dev/m7b.sh list
#   M7B_ARGS="-gl4es" tools/dev/m7b.sh m7b-twin shadow projector-agree projector-differ terrain twin-match
#                       the same checks on the GL4ES / OpenGL ES 2 twin (the browser's code path), then its pictures
#                       against the desktop's of the tree M7B_REF (default m7b; run that first)
#
# Every game run is tools/dev/fx-owner.sh's "custom" case (bin/capped 2G, Xvfb, Mesa llvmpipe, 1280x720, -glnoaa
# via the engine defaults of that runner, AutoFOV off, pre-D-2 UI), the pawn placed with NETPLACE, time frozen
# (TimeDilation 0.0001) before any picture. Pictures and logs: <WorkUC2>/logs/<tree>/fx-m7b-<case>*.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=m7b
if [ $# -gt 0 ] && { [ -d "$work/run/$1" ] || [ "$1" = m7b ]; }; then tree=$1; shift; fi
out=$work/logs/$tree
imgdiff=$repo/tools/dev/imgdiff.py
CHECKS="census shadow projector-agree projector-differ projector-draws terrain leftovers"
ref=$work/logs/${M7B_REF:-m7b}
if [ "${1:-}" = list ]; then
  cat <<LIST
census            GLSCENECENSUS in DM-DeepCore and CTF-Recoil: every level-static projector batched at load (Xbox)
shadow            DM-DeepCore, player shadow on a lit floor: GLSHADOWCHECK recomputes both shadow textures from their
                  caster targets (PC and Xbox filters), and the PC/Xbox pictures differ where the shadow is
projector-agree   DM-DeepCore wire-mesh light (modulate, no material blending, clipped, gradient): PC path and Xbox batch
                  agree on the floor (Epic's formulas are the same there)
projector-differ  CTF-Recoil wire-mesh light (MaterialBlendingOp=modulate, ignored by the Xbox batch): the pictures differ
projector-draws   draw calls per frame (GLSTATS) for both paths at the DM-DeepCore and CTF-Recoil cameras
terrain           CTF-Judgment and DM-Ascension: one layer per pass against three (TERRAIN3LAYER) on the same frame
twin-match        (not in the default list) this tree's Xbox-path pictures against the same pictures of M7B_REF's tree
leftovers         nothing of these runs still running
LIST
  exit 0
fi
want=${*:-$CHECKS}
mkdir -p "$out"
[ -d "$work/run/$tree/System" ] || "$work/bin/capped" 2G 600 -- "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" "$tree" > /dev/null
cp -f "$repo/build-native/Source/SDLLaunch/uc2.bin" "$work/run/$tree/System/uc2.bin"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
pass=0; fail=0
line() {  # line <PASS|FAIL> <check> <text>
  echo "m7b $2: $1 $3"
  [ "$1" = PASS ] && pass=$((pass+1)) || fail=$((fail+1))
}
# The start of every scenario after fx-owner's own (which clicks Fire): Fire again through the virtual pad and the
# mouse (a click can be lost while software GL renders a slow frame), invulnerable, view pitch held.
start() {
  printf 'sleep 2\npad attach\nsleep 1\npadtrigger righttrigger\nsleep 1\npadtrigger righttrigger\nsleep 1\nfocus\nmove 640 360\nclick 1\nsleep 3\nchan exec God\nchan exec SetAutoCenterPitch 0\nsleep 1\n'
}
place() {  # place "<x y z yaw pitch>": twice (the pitch holds once the pawn stands), then time frozen
  printf 'chan exec NETPLACE FxCam %s\nsleep 4\nchan exec NETPLACE FxCam %s\nsleep 5\nchan exec set LevelInfo TimeDilation 0.0001\nsleep 2\n' "$1" "$1"
}
run() {  # run <name> <map> <scenario file> [uc2.bin arguments]
  local name=$1 map=$2 scen=$3; shift 3
  FX_ARGS="$* ${M7B_ARGS:-}" FX_SCENARIO=$scen FX_MAP=$map FX_NAME=m7b-$name "$repo/tools/dev/fx-owner.sh" "$tree" custom > "$tmp/$name.fx" 2>&1
  cat "$tmp/$name.fx" | tail -1
}
diffline() {  # diffline <a.png> <b.png> [imgdiff options]: "mean=.. max=.. above2=.." of the pair
  python3 "$imgdiff" "$@" 2>&1 | tail -1
}
mean_of() { sed -E 's/.*mean=([0-9.]+).*/\1/' <<< "$1"; }
max_of() { sed -E 's/.* max=([0-9]+).*/\1/' <<< "$1"; }
DEEPCORE="-1791 1021 6900 32768 -12500"
RECOIL="-201 1000 -2150 16384 -9000"
# pawn, crosshair and HUD of the DM-DeepCore camera, masked out of floor comparisons
DCMASK="--mask 450,380,830,720 --mask 250,90,400,160 --mask 0,0,700,110 --mask 0,280,200,430"

for c in $want; do
  case $c in
  census)
    { start; place "$DEEPCORE"; echo 'chan exec GLSCENECENSUS'; echo 'sleep 1'; } > "$tmp/c1.scen"
    { start; place "$RECOIL"; echo 'chan exec GLSCENECENSUS'; echo 'sleep 1'; } > "$tmp/c2.scen"
    run census-deepcore DM-DeepCore "$tmp/c1.scen"; run census-recoil CTF-Recoil "$tmp/c2.scen"
    for m in deepcore recoil; do
      s=$(grep -a 'scene summary' "$out/fx-m7b-census-$m.out" | tail -1)
      st=$(sed -E 's/.*\(static ([0-9]+),.*/\1/' <<< "$s"); bt=$(sed -E 's/.*Xbox-batched ([0-9]+)\).*/\1/' <<< "$s")
      drawn=$(grep -a -c 'UC2GLDrv: projector material ' "$out/fx-m7b-census-$m.out")
      if [ -n "$st" ] && [ "$st" = "$bt" ] && [ "$st" -gt 0 ]; then r=PASS; else r=FAIL; fi
      line $r census "$m: $st level-static projectors, $bt batched at load ($(grep -a 'ProjSecs' "$out/fx-m7b-census-$m.out" | tail -1 | sed 's/^.*ProjSecs/ProjSecs/')), $drawn drawn with the Xbox projector material in this view"
    done
    ;;
  shadow)
    { start; place "$DEEPCORE"
      printf 'chan exec XBOXSHADOWS 0\nsleep 1\nshot pc\nsleep 0.5\nshot pc2\nchan exec GLSHADOWCHECK\nsleep 1\nchan exec XBOXSHADOWS 1\nsleep 1\nshot xbox\nsleep 0.5\nshot xbox2\nchan exec GLSHADOWCHECK\nsleep 1\n'; } > "$tmp/s.scen"
    run shadow DM-DeepCore "$tmp/s.scen"
    f=$out/fx-m7b-shadow.out
    pcl=$(grep -a 'shadowcheck Shadow.*branch=pc' "$f" | head -1); xbl=$(grep -a 'shadowcheck Shadow.*branch=xbox' "$f" | head -1)
    pce=$(sed -E 's/.*largest error ([0-9.]+).*/\1/' <<< "$pcl"); xbe=$(sed -E 's/.*largest error ([0-9.]+).*/\1/' <<< "$xbl")
    pcm=$(sed -E 's/.*shadow min=([0-9]+).*/\1/' <<< "$pcl"); xbm=$(sed -E 's/.*shadow min=([0-9]+).*/\1/' <<< "$xbl")
    if [ -n "$pcl" ] && [ -n "$xbl" ] && python3 -c "import sys; sys.exit(0 if $pce<=2 and $xbe<=2 and abs($pcm-27)<=2 and abs($xbm-55)<=2 else 1)"; then r=PASS; else r=FAIL; fi
    line $r shadow-filter "recomputed from the caster target: PC branch largest error $pce/255 (core $pcm, expected 27), Xbox branch $xbe/255 (core $xbm, expected 255-200=55)"
    d=$(diffline "$out/fx-m7b-shadow-pc.png" "$out/fx-m7b-shadow-xbox.png" $DCMASK); n1=$(diffline "$out/fx-m7b-shadow-pc.png" "$out/fx-m7b-shadow-pc2.png" $DCMASK)
    if python3 -c "import sys; sys.exit(0 if $(mean_of "$d") > 3*max($(mean_of "$n1"),0.01) else 1)"; then r=PASS; else r=FAIL; fi
    line $r shadow-picture "PC vs Xbox shadow (pawn masked): $d; two PC frames: $n1"
    ;;
  projector-agree|projector-differ|projector-draws)
    # Both projector checks and the draw counts come from the same two pairs of runs (made once).
    if [ ! -f "$tmp/proj.done" ]; then
      for cam in deepcore recoil; do
        p=$DEEPCORE; m=DM-DeepCore; [ $cam = recoil ] && { p=$RECOIL; m=CTF-Recoil; }
        { start; place "$p"; printf 'chan exec GLSTATS RESET\nsleep 4\nchan exec GLSTATS\nshot a\nsleep 0.5\nshot b\n'; } > "$tmp/p-$cam.scen"
        run proj-$cam-pc $m "$tmp/p-$cam.scen" -noxboxprojectors; run proj-$cam-xbox $m "$tmp/p-$cam.scen"
      done
      touch "$tmp/proj.done"
    fi
    if [ $c = projector-agree ]; then
      d=$(diffline "$out/fx-m7b-proj-deepcore-pc-a.png" "$out/fx-m7b-proj-deepcore-xbox-a.png" $DCMASK); n=$(diffline "$out/fx-m7b-proj-deepcore-pc-a.png" "$out/fx-m7b-proj-deepcore-pc-b.png" $DCMASK)
      if python3 -c "import sys; sys.exit(0 if $(mean_of "$d") <= 0.5 and $(max_of "$d") <= 8 else 1)"; then r=PASS; else r=FAIL; fi
      line $r projector-agree "DM-DeepCore floor under the wire-mesh light, PC path vs Xbox batch (separate runs, pawn masked): $d; two frames of one run: $n"
    elif [ $c = projector-differ ]; then
      d=$(diffline "$out/fx-m7b-proj-recoil-pc-a.png" "$out/fx-m7b-proj-recoil-xbox-a.png" --box 380,250,920,470); n=$(diffline "$out/fx-m7b-proj-recoil-pc-a.png" "$out/fx-m7b-proj-recoil-pc-b.png" --box 380,250,920,470)
      if python3 -c "import sys; sys.exit(0 if $(mean_of "$d") >= 3 else 1)"; then r=PASS; else r=FAIL; fi
      line $r projector-differ "CTF-Recoil grid (MaterialBlendingOp=modulate), PC vs Xbox: $d; two frames of one run: $n"
    else
      for cam in deepcore recoil; do
        a=$(grep -a 'UC2GLDrv: stats:' "$out/fx-m7b-proj-$cam-pc.out" | tail -1 | sed -E 's/.*per frame: ([0-9.]+) draw calls.*/\1/')
        b=$(grep -a 'UC2GLDrv: stats:' "$out/fx-m7b-proj-$cam-xbox.out" | tail -1 | sed -E 's/.*per frame: ([0-9.]+) draw calls.*/\1/')
        if [ -n "$a" ] && [ -n "$b" ]; then r=PASS; else r=FAIL; fi
        line $r projector-draws "$cam camera: draw calls per frame PC path $a, Xbox batches $b"
      done
    fi
    ;;
  terrain)
    for m in CTF-Judgment DM-Ascension; do
      { start; printf 'sleep 3\nchan exec set LevelInfo TimeDilation 0.0001\nsleep 2\nchan exec TERRAIN3LAYER 0\nsleep 1\nchan exec GLSTATS RESET\nsleep 3\nchan exec GLSTATS\nshot one\nsleep 0.5\nshot one2\nchan exec TERRAIN3LAYER 1\nsleep 1\nchan exec GLSTATS RESET\nsleep 3\nchan exec GLSTATS\nshot three\nchan exec GLSCENECENSUS\nsleep 1\n'; } > "$tmp/t.scen"
      run terrain-$m $m "$tmp/t.scen"
      f=$out/fx-m7b-terrain-$m
      d=$(diffline "$f-one.png" "$f-three.png" --mask 500,440,760,720); n=$(diffline "$f-one.png" "$f-one2.png" --mask 500,440,760,720)
      comb=$(grep -a 'scene summary' "$f.out" | tail -1 | sed -E 's/.*sectors with a three-layer pass ([0-9]+).*/\1/')
      dc=$(grep -a 'UC2GLDrv: stats:' "$f.out" | sed -E 's/.*per frame: ([0-9.]+) draw calls.*/\1/' | tr '\n' ' ')
      if [ "${comb:-0}" -gt 0 ] && python3 -c "import sys; sys.exit(0 if $(mean_of "$d") <= 0.5 else 1)"; then r=PASS; else r=FAIL; fi
      line $r terrain "$m: $comb sectors drawn with PS_Terrain3Layer; one vs three layers per pass (pawn masked): $d; two frames: $n; draw calls per frame one/three: $dc"
    done
    ;;
  twin-match)
    for pic in shadow-xbox:dc proj-deepcore-xbox-a:dc proj-recoil-xbox-a:full terrain-DM-Ascension-three:pawn; do
      name=${pic%%:*}; how=${pic##*:}
      case $how in dc) m=$DCMASK;; pawn) m="--mask 500,440,760,720";; *) m="";; esac
      a=$ref/fx-m7b-$name.png; b=$out/fx-m7b-$name.png
      if [ -f "$a" ] && [ -f "$b" ]; then
        d=$(diffline "$a" "$b" $m)
        python3 -c "import sys; sys.exit(0 if $(mean_of "$d") <= 3 else 1)" && r=PASS || r=FAIL
        line $r twin-match "$name: $(basename "$ref") vs $tree: $d"
      else
        line FAIL twin-match "$name: picture missing ($a or $b)"
      fi
    done
    ;;
  leftovers)
    left=0
    for p in $(pgrep -x uc2.bin) $(pgrep -x Xvfb); do
      case "$(readlink /proc/$p/cwd 2>/dev/null)" in "$work/run/$tree/System"*) left=$((left+1));; esac
    done
    [ $left = 0 ] && r=PASS || r=FAIL
    line $r leftovers "$left processes of run tree $tree still running"
    ;;
  esac
done
echo "m7b summary: $pass PASS, $fail FAIL"
