#!/usr/bin/env python3
"""B-3: one sheet per layout with the split HUD of several runs side by side (docs/porting/analysis/split-hud.md).

    b3-hud-sheet.py <out.png-prefix> <label>=<dir>[:<mode>] ...

For every layout tag (n1 n2 n3 n4) writes <prefix>-<tag>.png: one column per run, each run's picture (top) and its
HUD alone (bottom) scaled to a common height, with the viewports' rectangles (grey), their centres (magenta cross),
each viewport's UI box (dark yellow) and the measured elements (b3-hud-measure.py): health green, weapons cyan, score
yellow, reticle red, and the element's top-left in design pixels written next to it.
"""
import json, os, subprocess, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PANEL_H = 540
COLS = {'health': (0, 255, 0), 'weapons': (0, 220, 255), 'score': (255, 220, 0), 'reticle': (255, 40, 40)}


def measure(d, mode):
    r = subprocess.run([sys.executable, os.path.join(HERE, 'b3-hud-measure.py'), '--json', d, mode],
                       capture_output=True, text=True, check=True)
    return json.loads(r.stdout)


def panel(path, m, label, annotate):
    im = Image.open(path).convert('RGB')
    W, H = im.size
    k = PANEL_H / H
    pw = int(round(W * k))
    im = im.resize((pw, PANEL_H), Image.LANCZOS)
    dr = ImageDraw.Draw(im)
    for v in m['views']:
        x, y, w, h = v['rect']
        dr.rectangle([x * k, y * k, (x + w) * k - 1, (y + h) * k - 1], outline=(110, 110, 110))
        cx, cy = (x + w / 2) * k, (y + h / 2) * k
        dr.line([cx - 8, cy, cx + 8, cy], fill=(255, 0, 255))
        dr.line([cx, cy - 8, cx, cy + 8], fill=(255, 0, 255))
        bx, by, bw, bh = v['box']
        dr.rectangle([(x + bx) * k, (y + by) * k, (x + bx + bw) * k - 1, (y + by + bh) * k - 1], outline=(140, 120, 0))
        if not annotate:
            continue
        for e, col in COLS.items():
            b = v.get(e + '_px')
            if b:
                dr.rectangle([b[0] * k, b[1] * k, b[2] * k, b[3] * k], outline=col)
                d = v[e]
                dr.text((b[2] * k + 2, b[1] * k), f"{d[0]:.0f},{d[1]:.0f}", fill=col)
    bar = Image.new('RGB', (pw, 18), (30, 30, 30))
    ImageDraw.Draw(bar).text((4, 3), label, fill=(255, 255, 255))
    col = Image.new('RGB', (pw, PANEL_H + 18))
    col.paste(bar, (0, 0))
    col.paste(im, (0, 18))
    return col


def main():
    prefix = sys.argv[1]
    runs = []
    for spec in sys.argv[2:]:
        label, rest = spec.split('=', 1)
        d, _, mode = rest.partition(':')
        runs.append((label, d, measure(d, mode or 'Fit43')))
    for tag in ['n1', 'n2', 'n3', 'n4']:
        cols = []
        for label, d, m in runs:
            if tag not in m:
                continue
            full = os.path.join(d, f'b3-hud-{tag}.png')
            hud = os.path.join(d, f'b3-hud-{tag}-hud.png')
            if not (os.path.exists(full) and os.path.exists(hud)):
                continue
            w, h = m[tag]['window']
            a = panel(full, m[tag], f"{label}: {w}x{h} {m[tag]['mode']} - picture", False)
            b = panel(hud, m[tag], f"{label}: the HUD alone, measured (design px)", True)
            c = Image.new('RGB', (a.width, a.height + b.height + 6), (60, 60, 60))
            c.paste(a, (0, 0))
            c.paste(b, (0, a.height + 6))
            cols.append(c)
        if not cols:
            continue
        sheet = Image.new('RGB', (sum(c.width for c in cols) + 8 * (len(cols) - 1), max(c.height for c in cols)), (60, 60, 60))
        x = 0
        for c in cols:
            sheet.paste(c, (x, 0))
            x += c.width + 8
        sheet.save(f'{prefix}-{tag}.png')
        print(f'{prefix}-{tag}.png {sheet.size[0]}x{sheet.size[1]}')


if __name__ == '__main__':
    main()
