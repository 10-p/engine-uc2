#!/usr/bin/env python3
"""Where the split-screen HUD sits, per viewport.

    b3-hud-measure.py <out-dir> [<mode>]      mode: Fit43 (default) or Native640; reads b3-hud-<tag>-hud.png and
                                              b3-hud-<tag>.json (tags n1 n2 n3 n4) of tools/dev/g6-splitscreen.sh hud
    b3-hud-measure.py --json <out-dir> [<mode>]   the same, one JSON object on stdout (for b3-hud-compare.py)

The HUD-alone picture (TOGGLERENDERLEVEL: the level not drawn, black around the HUD) is cut into the client's
viewports (the vprobe's rectangles). Each viewport's pixels are mapped back into the design space its canvas gives the
HUD, as UCanvas::ApplyUIBox does: 640 x 480*ScaleY (640x240 for a half or a quarter), with Fit43 scaled by
s = min(w/640, h/DH) and centred (origin floored), with Native640 1:1 from the viewport's corner. With Fit43 on a
viewport wider than its box the HUD's side groups (patch set wide-hud) move out by the room beside the box in whole
design pixels (UCHUD.GetSideShifts): the left elements' regions move with them. Elements, each the bounding box of
a colour inside a design-space region (L, R: the side's shift applies):
  health    the health bar's green fill            (L, x 0..320, upper half of the design height)
  weapons   the weapon list (anything lit)         (L, x 0..215, lower half)
  score     the rank/score box (anything lit)      (x 250..420, top third)
  right     the right side's top group (anything lit: enemy info, leaderboard, compass)  (R, x 427..640, top half)
  reticle   the crosshair's bright pixels          (x 250..390, the design centre +-60)
plus the whole HUD's bounding box. In window pixels: the left elements' distance from the viewport's left edge, the
right one's from its right edge, the score box's and the reticle's centre against the viewport's centre.
Coordinates printed in design pixels (x0,y0)-(x1,y1), x1/y1 exclusive.
"""
import json, os, sys
from PIL import Image, ImageChops

TAGS = ['n1', 'n2', 'n3', 'n4']


def band_mask(im, lo=(0, 0, 0), hi=(255, 255, 255)):
    r, g, b = im.split()
    m = None
    for band, l, h in zip((r, g, b), lo, hi):
        bm = band.point(lambda v, l=l, h=h: 255 if l <= v <= h else 0)
        m = bm if m is None else ImageChops.multiply(m, bm)
    return m


def lum_mask(im, lo):
    return im.convert('L').point(lambda v: 255 if v >= lo else 0)


ELEMENTS = {
    # name: (mask function, side, design region as fractions/pixels: x0, x1, y0 (fraction of DH), y1 (fraction of DH))
    'health':  (lambda im: band_mask(im, (0, 150, 0), (140, 255, 140)), 'L', (0, 320, 0.0, 0.5)),
    'weapons': (lambda im: lum_mask(im, 40), 'L', (0, 215, 0.5, 1.0)),
    'score':   (lambda im: lum_mask(im, 40), 'C', (250, 420, 0.0, 0.34)),
    'right':   (lambda im: lum_mask(im, 40), 'R', (427, 640, 0.0, 0.5)),
    'reticle': (lambda im: lum_mask(im, 150), 'C', (250, 390, None, None)),
}


def ui_box(rect, win, mode):
    x, y, w, h = rect
    W, H = win
    dh = 480.0 * (h / H) if 0 < h < H else 480.0
    if mode == 'Native640':
        return 1.0, 0, 0, float(w), float(h), 0, 0   # the canvas is the viewport, 1:1 (Epic's PC path; the Xbox's screen at 640x480)
    s = min(w / 640.0, h / dh)
    bx = int((w - 640.0 * s) // 2)
    by = int((h - dh * s) // 2)
    return s, bx, by, 640.0, dh, -int(bx / s + 0.5), int((w - bx - 640.0 * s) / s + 0.5)


def measure(out, mode):
    res = {}
    for tag in TAGS:
        p = os.path.join(out, f'b3-hud-{tag}-hud.png')
        jp = os.path.join(out, f'b3-hud-{tag}.json')
        if not (os.path.exists(p) and os.path.exists(jp)):
            continue
        im = Image.open(p).convert('RGB')
        win = im.size
        rows = json.load(open(jp))['V'][tag]
        views = sorted([r for r in rows if r['split'] >= 0], key=lambda r: r['split'])
        if not views:
            views = [r for r in rows if r['i'] == 0]
        out_views = []
        for r in views:
            x, y, w, h = r['rect']
            s, bx, by, dw, dh, lsh, rsh = ui_box(r['rect'], win, mode)
            crop = im.crop((x, y, x + w, y + h))
            v = {'i': r['i'], 'split': r['split'], 'rect': [x, y, w, h], 'hud': r.get('hud'), 'player': r.get('player'),
                 'pawn': r.get('pawn'), 's': s, 'box': [bx, by, round(dw * s, 2), round(dh * s, 2)], 'design': [dw, dh],
                 'shifts': [lsh, rsh]}

            def to_design(px, py):
                return round((px - bx) / s, 1), round((py - by) / s, 1)

            def to_px(dx, dy):
                return int(round(bx + dx * s)), int(round(by + dy * s))

            whole = lum_mask(crop, 24).getbbox()
            v['all'] = list(to_design(*whole[:2]) + to_design(*whole[2:])) if whole else None
            for name, (fn, side, (rx0, rx1, ry0, ry1)) in ELEMENTS.items():
                sh = lsh if side == 'L' else rsh if side == 'R' else 0
                rx0, rx1 = rx0 + sh, rx1 + sh
                if ry0 is None:
                    ry0, ry1 = dh / 2 - 60, dh / 2 + 60
                else:
                    ry0, ry1 = dh * ry0, dh * ry1
                px0, py0 = to_px(rx0, ry0)
                px1, py1 = to_px(rx1, ry1)
                px0, py0 = max(px0, 0), max(py0, 0)
                px1, py1 = min(px1, w), min(py1, h)
                if px1 <= px0 or py1 <= py0:
                    v[name] = None
                    continue
                bb = fn(crop.crop((px0, py0, px1, py1))).getbbox()
                if not bb:
                    v[name] = None
                    continue
                bb = (bb[0] + px0, bb[1] + py0, bb[2] + px0, bb[3] + py0)
                v[name] = list(to_design(bb[0], bb[1]) + to_design(bb[2], bb[3]))
                v[name + '_px'] = [bb[0] + x, bb[1] + y, bb[2] + x, bb[3] + y]
                if side == 'L':
                    v[name + '_edge_px'] = bb[0]
                elif side == 'R':
                    v[name + '_edge_px'] = w - bb[2]
                else:
                    v[name + '_off_px'] = round((bb[0] + bb[2]) / 2.0 - w / 2.0, 1)
                if name == 'reticle':
                    cx, cy = (bb[0] + bb[2]) / 2.0, (bb[1] + bb[3]) / 2.0
                    v['reticle_centre_px'] = [round(cx + x, 1), round(cy + y, 1)]
                    v['reticle_off_px'] = [round(cx - w / 2.0, 1), round(cy - h / 2.0, 1)]
                    v['reticle_off_design'] = [round((cx - w / 2.0) / s, 2), round((cy - h / 2.0) / s, 2)]
            out_views.append(v)
        res[tag] = {'window': list(win), 'mode': mode, 'views': out_views}
    return res


def fmt(b):
    return '-' if not b else f"({b[0]:.1f},{b[1]:.1f})-({b[2]:.1f},{b[3]:.1f})"


def main():
    args = sys.argv[1:]
    as_json = False
    if args and args[0] == '--json':
        as_json = True
        args = args[1:]
    out = args[0]
    mode = args[1] if len(args) > 1 else 'Fit43'
    res = measure(out, mode)
    if as_json:
        print(json.dumps(res, indent=1))
        return
    for tag, r in res.items():
        print(f"{tag}: window {r['window'][0]}x{r['window'][1]} {mode}")
        for v in r['views']:
            print(f"  viewport i={v['i']} split={v['split']} rect={tuple(v['rect'])} hud={v['hud']} pawn={v['pawn']} "
                  f"design {v['design'][0]:.0f}x{v['design'][1]:.0f} s={v['s']:.4f} box at ({v['box'][0]},{v['box'][1]}) "
                  f"{v['box'][2]}x{v['box'][3]}, side shifts {tuple(v['shifts'])}")
            for name in ['all', 'health', 'weapons', 'score', 'right', 'reticle']:
                extra = ''
                if v.get(name + '_edge_px') is not None:
                    extra = f"  {v[name + '_edge_px']} px from the viewport's {'left' if name != 'right' else 'right'} edge"
                if name == 'score' and v.get('score_off_px') is not None:
                    extra = f"  centre {v['score_off_px']:+.1f} px from the viewport's centre"
                if name == 'reticle' and v.get('reticle'):
                    extra = (f"  centre {tuple(v['reticle_centre_px'])} px, from the viewport centre "
                             f"{tuple(v['reticle_off_px'])} px = {tuple(v['reticle_off_design'])} design")
                print(f"    {name:8s} {fmt(v.get(name))}{extra}")
    json.dump(res, open(os.path.join(out, 'b3-hud-measure.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
