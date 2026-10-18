#!/usr/bin/env bash
# Anti-aliasing and texture LOD bias A/B pictures, from one pinned view of DM-Deadbolt
# (first PlayerStart, NumPlayers 0, post-processing off so that nothing animates in the compared box), on the
# GL4ES twin and the desktop, one game run at a time, each inside bin/capped through tools/dev/run-uc2-shot.sh.
#
#   tools/dev/f3-ab.sh aa [<fxaa-binary>]   anti-aliasing: twin none | twin FXAA (the binary before FXAA went,
#                                           on the OpenGL ES 2 path where it ran) | twin 4x MSAA (OpenGL ES 3) |
#                                           desktop 4x MSAA; pictures after the present pass (ShotPresented)
#   tools/dev/f3-ab.sh lodbias              texture LOD bias: desktop with / without its -0.5 bias, twin with /
#                                           without (NoLODBias = what GL4ES did before our fork applied it);
#                                           high-frequency energy of the far floor (tools/dev/hf-energy.py)
#
# Trees: F3_DESKTOP_TREE (default f3d), F3_TWIN_TREE (f3t), made from build-native unless F3_NO_MKRUN is set.
# Output: <repo>/../logs/f3/ab-*.png and ab-*.txt. The twin runs an OpenGL ES 3.0 context
# (MESA_GLES_VERSION_OVERRIDE=3.0 = WebGL 2) unless a case says 2.0.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
shot=$repo/tools/dev/run-uc2-shot.sh
imgdiff=$repo/tools/dev/imgdiff.py
hf=$repo/tools/dev/hf-energy.py
D=${F3_DESKTOP_TREE:-f3d}; T=${F3_TWIN_TREE:-f3t}
out=$work/logs/f3; mkdir -p "$out"
PIN='set UCDeathmatch NumPlayers 0'
ES2="-ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:GLESVersion=2"
if [ -z "${F3_NO_MKRUN:-}" ]; then
  "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" $D > /dev/null || exit 1
  "$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" $T > /dev/null || exit 1
fi
glini=$repo/tools/dev/glini
run() {  # run <tree> <name> <mesa-es-version> <args...>: one pinned picture at 44 s (settings and test keys: -ini:)
  local tree=$1 name=$2 es=$3; shift 3
  MESA_GLES_VERSION_OVERRIDE=$es UC2_RES=1280x720 UC2_PRECMD="$PIN" "$shot" $tree "$name" 75 34 DM-Deadbolt $("$glini" CheckGLErrors=True UsePostProcess=False ShotTimes=44) "$@" > "$work/logs/$tree/$name.report" 2>&1
  cp -f "$work/logs/$tree/$name-0.png" "$out/ab-$name.png" 2>/dev/null || echo "no picture for $name (see $work/logs/$tree/$name.report)"
  grep -a -o "anti-aliasing: .*\|UC2GLDrv: main framebuffer .*\|binding: [^)]*" "$work/logs/$tree/$name.out" | sed "s/^/  $name: /"
}

case "${1:-}" in
aa)
  fx=${2:-}
  SP=$("$glini" ShotPresented=True)
  run $T aa-none 3.0 -gl4es $SP
  if [ -n "$fx" ]; then
    cp -f "$fx" "$work/run/$T/System/uc2-fxaa.bin"
    # (an older FXAA binary reads -gl* switches, not the test keys)
    UC2_BIN=uc2-fxaa.bin UC2_AA=1 run $T aa-fxaa 2.0 -gl4es -glerrors -nopostprocess -glshot=44 -glshotpresented $ES2
  fi
  UC2_AA=1 run $T aa-msaa 3.0 -gl4es $SP
  UC2_AA=1 run $D aa-desktop-msaa 3.0 -opengl $SP
  {
    for n in aa-fxaa aa-msaa aa-desktop-msaa; do
      [ -f "$out/ab-$n.png" ] && echo "$n vs aa-none (static part x>=760): $(python3 "$imgdiff" "$out/ab-aa-none.png" "$out/ab-$n.png" --box 760,0,1280,720)"
    done
    [ -f "$out/ab-aa-msaa.png" ] && echo "aa-msaa (twin) vs aa-desktop-msaa (static part): $(python3 "$imgdiff" "$out/ab-aa-desktop-msaa.png" "$out/ab-aa-msaa.png" --box 760,0,1280,720)"
  } | tee "$out/ab-aa.txt"
  # Crops of the same edges, 3x: none | FXAA | MSAA (twin) | MSAA (desktop): the lamp, the poster frame, the pipes.
  python3 - "$out" <<'PY'
import sys, os
from PIL import Image
out = sys.argv[1]; names = ['aa-none', 'aa-fxaa', 'aa-msaa', 'aa-desktop-msaa']
imgs = [Image.open(os.path.join(out, 'ab-%s.png' % n)).convert('RGB') for n in names if os.path.exists(os.path.join(out, 'ab-%s.png' % n))]
box = (740, 100, 1060, 380); s = 3
w, h = box[2] - box[0], box[3] - box[1]
sheet = Image.new('RGB', (w * s * len(imgs) + 4 * (len(imgs) - 1), h * s), (255, 0, 255))
for k, im in enumerate(imgs):
    sheet.paste(im.crop(box).resize((w * s, h * s), Image.NEAREST), (k * (w * s + 4), 0))
sheet.save(os.path.join(out, 'ab-aa-crops.png'))
print('crops: ' + ' | '.join(n for n in names if os.path.exists(os.path.join(out, 'ab-%s.png' % n))) + ' -> ab-aa-crops.png')
PY
  ;;
lodbias)
  run $D lod-desktop 3.0 -opengl
  run $D lod-desktop-nobias 3.0 -opengl $("$glini" NoLODBias=True)
  run $T lod-twin 3.0 -gl4es
  run $T lod-twin-nobias 3.0 -gl4es $("$glini" NoLODBias=True)
  # The far floor grating on the right (grazing angle: the mip level decides how much of it survives).
  box=760,470,1200,620
  {
    python3 "$hf" "$out/ab-lod-desktop.png" "$out/ab-lod-desktop-nobias.png" "$out/ab-lod-twin.png" "$out/ab-lod-twin-nobias.png" --box $box --crop "$out/ab-lod-crops.png" --scale 2
    echo "twin vs desktop, far floor: $(python3 "$imgdiff" "$out/ab-lod-desktop.png" "$out/ab-lod-twin.png" --box $box)"
    echo "twin without bias vs desktop, far floor: $(python3 "$imgdiff" "$out/ab-lod-desktop.png" "$out/ab-lod-twin-nobias.png" --box $box)"
    echo "twin vs twin without bias, far floor: $(python3 "$imgdiff" "$out/ab-lod-twin-nobias.png" "$out/ab-lod-twin.png" --box $box --diff "$out/ab-lod-twin-diff.png")"
  } | tee "$out/ab-lodbias.txt"
  ;;
*) sed -n '2,15p' "$0"; exit 2;;
esac
