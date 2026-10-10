#!/usr/bin/env python3
"""G-6: measure the pictures of tools/dev/g6-splitscreen.sh's render part, per split-screen viewport.

    g6-measure.py <out-dir>          reads g6-render-<tag>[-nopp|-hud|-nohud|-nohud-nopp].png and g6-render-<tag>.json

For every layout picture (n2a n2b n3 n4 n4d; n1 is the single-player reference):
  rendered   each split rectangle: mean luminance and the share of pixels above luminance 12 (not black)
  distinct   mean absolute difference (0..255) between every two rectangles, both resized to 160x90 grey
  hud        the HUD alone (TOGGLERENDERLEVEL: the level not drawn): its bounding box in each rectangle and the
             margin to the rectangle's edges (a HUD cut by the edge touches it: margin 0), and its pixel count
  same-view  the viewport whose player stands at spot A (the single-player picture's spot and view) against the
             single-player picture, without the HUD: with AutoFOV per viewport aspect (Hor+) a viewport at least
             4:3 wide shows the single view's vertical extent, so its centre (height x the window's aspect) is the
             single picture scaled to its height. Mean absolute difference with post-processing on (pp) and off
             (nopp); post-processing that is right per viewport adds no difference of its own (pp - nopp small),
             and its strength in the viewport (|pp - nopp| of the viewport) matches the single picture's.
Prints one line per measurement and a PASS/FAIL line per check; exit 1 on a failed check.
"""
import json, os, sys
from PIL import Image, ImageChops, ImageStat

out = sys.argv[1]
SPOT_A = {'n2a': 0, 'n2b': 1, 'n3': 0, 'n4': 0, 'n4d': 3}
fails = 0


def check(name, ok, detail):
    global fails
    print(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")
    if not ok:
        fails += 1


def load(tag, kind=''):
    p = os.path.join(out, f'g6-render-{tag}{kind}.png')
    return Image.open(p).convert('RGB') if os.path.exists(p) else None


def rects(tag):
    j = json.load(open(os.path.join(out, f'g6-render-{tag}.json')))
    v = j['V'][tag]
    return [tuple(r['rect']) for r in sorted([r for r in v if r['split'] >= 0], key=lambda r: r['split'])]


def crop(im, r):
    x, y, w, h = r
    return im.crop((x, y, x + w, y + h))


def lum(im):
    return im.convert('L')


def mad(a, b):
    """Mean absolute difference of two equal-size RGB images, 0..255."""
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3.0


def centre_of_viewport(view, ww, wh):
    """The centre of a viewport that is the single view at the viewport's height (Hor+): width h*ww/wh."""
    w, h = view.size
    cw = min(w, round(h * ww / wh))
    x0 = (w - cw) // 2
    return view.crop((x0, 0, x0 + cw, h))


single = load('n1')
if single is None:
    print('no single-player picture g6-render-n1.png'); sys.exit(1)
WW, WH = single.size
single_nohud, single_nohud_nopp = load('n1', '-nohud'), load('n1', '-nohud-nopp')
pp_single = mad(single_nohud, single_nohud_nopp) if single_nohud and single_nohud_nopp else None
print(f"single-player: {WW}x{WH}, post-processing strength |pp - nopp| = {pp_single:.2f}")

for tag in ['n2a', 'n2b', 'n3', 'n4', 'n4d']:
    im = load(tag)
    if im is None:
        continue
    rs = rects(tag)
    print(f"--- {tag}: {len(rs)} split views {rs}")
    # rendered
    for i, r in enumerate(rs):
        L = lum(crop(im, r))
        st = ImageStat.Stat(L)
        lit = sum(L.histogram()[13:]) / float(r[2] * r[3])
        check(f'{tag}-view{i}-rendered', st.mean[0] > 10 and lit > 0.5, f'mean luminance {st.mean[0]:.1f}, {100*lit:.1f}% of pixels above 12')
    # distinct
    small = [lum(crop(im, r)).resize((160, 90)) for r in rs]
    for i in range(len(rs)):
        for k in range(i + 1, len(rs)):
            d = sum(ImageStat.Stat(ImageChops.difference(small[i], small[k])).mean)
            check(f'{tag}-views{i}{k}-distinct', d > 8, f'mean |difference| {d:.1f}')
    # hud
    hud = load(tag, '-hud')
    if hud is not None:
        for i, r in enumerate(rs):
            L = lum(crop(hud, r)).point(lambda v: 255 if v > 40 else 0)
            bb = L.getbbox()
            n = L.histogram()[255]
            if bb is None:
                check(f'{tag}-view{i}-hud-inside', False, 'no HUD pixels')
                continue
            margin = min(bb[0], bb[1], r[2] - bb[2], r[3] - bb[3])
            check(f'{tag}-view{i}-hud-inside', margin >= 1 and n > 200,
                  f'HUD box {bb} in {r[2]}x{r[3]}, margins l{bb[0]} t{bb[1]} r{r[2]-bb[2]} b{r[3]-bb[3]}, {n} pixels')
    # same view as single player (spot A)
    k = SPOT_A.get(tag)
    pp, nopp = load(tag, '-nohud'), load(tag, '-nohud-nopp')
    if k is not None and k < len(rs) and pp is not None and nopp is not None:
        r = rs[k]
        if r[2] * WH < r[3] * WW * 0.75 - 1:
            print(f"{tag}: view {k} narrower than 4:3, no same-view comparison")
            continue
        vp, vn = centre_of_viewport(crop(pp, r), WW, WH), centre_of_viewport(crop(nopp, r), WW, WH)
        sp, sn = single_nohud.resize(vp.size, Image.LANCZOS), single_nohud_nopp.resize(vn.size, Image.LANCZOS)
        d_pp, d_nopp = mad(vp, sp), mad(vn, sn)
        strength = mad(vp, vn)
        strength_single = mad(sp, sn)
        check(f'{tag}-view{k}-same-view-as-single', d_nopp < 12,
              f'centre {vp.size} vs single picture scaled: |diff| without post-processing {d_nopp:.2f}')
        check(f'{tag}-view{k}-post-processing-per-viewport', d_pp < d_nopp + 3 and 0.6 * strength_single <= strength <= 1.6 * strength_single + 1,
              f'|diff| with post-processing {d_pp:.2f} (without {d_nopp:.2f}); post-processing strength in the viewport {strength:.2f}, in the single picture scaled {strength_single:.2f}')

# Dynamic gamma per viewport: the driver's log line of every viewport's first ramp (then every 200th) names the
# viewport's rectangle. Every split rectangle of every layout must have had a ramp of its own.
import re
ramps = {}
log = os.path.join(out, 'g6-render.out')
if os.path.exists(log):
    for line in open(log, encoding='latin-1'):
        m = re.search(r'dynamic gamma ramp \d+ \(viewport (\d+) at (\d+),(\d+) (\d+)x(\d+)\): luminance min ([\d.]+) max ([\d.]+) average ([\d.]+)', line)
        if m:
            ramps.setdefault(tuple(int(v) for v in m.groups()[1:5]), []).append(tuple(float(v) for v in m.groups()[5:8]))
print(f"dynamic gamma ramps per split rectangle (min, max, average luminance): " + '; '.join(f'{k}: {v[-1]}' for k, v in sorted(ramps.items())))
for tag in ['n2a', 'n3', 'n4']:
    if os.path.exists(os.path.join(out, f'g6-render-{tag}.json')):
        rs = rects(tag)
        missing = [r for r in rs if r not in ramps]
        check(f'{tag}-dynamic-gamma-per-viewport', not missing, f'{len(rs) - len(missing)} of {len(rs)} rectangles had a ramp of their own' + (f'; none for {missing}' if missing else ''))

print(f"{'FAIL' if fails else 'PASS'} g6-measure: {fails} failed")
sys.exit(1 if fails else 0)
