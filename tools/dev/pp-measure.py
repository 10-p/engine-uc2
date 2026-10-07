#!/usr/bin/env python3
"""Measurements of the post-process blurs on screenshots, for tools/dev/pp-scale.sh (docs/porting/analysis/pp-scale.md).

Every picture is first brought to the 480-line scale of the Xbox's picture (an area average: PIL's BOX filter; a
4:3 picture becomes 640x480), so numbers from different resolutions compare directly: a blur that covers the same
share of the picture gives the same number. Coordinates are in that 480-line picture.

    pp-measure.py glow <off.png> <on.png> [x y half]
        The glow the post-processing adds around a bright thing (the coordinator's measure of 2026-10-07): the
        profile of (on - off), mean of R, G, B, down the column band x-half .. x+half-1, averaged across it; the
        peak within 15 rows of y, and the run of rows around the peak above 10% of it, as a share of the height.
        Default: the DM-Deadbolt ceiling tube from the pinned first PlayerStart, x=411 y=138 half=10.
        Prints: peak=<levels> width=<percent of the height> rows=<first>..<last>

    pp-measure.py sigma <base.png> <test.png> x0 y0 x1 y1 [xy|x|y]
        The size of the blur that turns base into test inside the box: test - base is fitted, by least squares,
        with a * (G(base) - base) + b, where G is a Gaussian blur of standard deviation s (both axes, or one),
        for s from 0.5 to 12 in steps of 0.5 (then refined to 0.05); a takes the share the blur is blended
        with (the depth of field's alpha), b a constant (glow added on top). Luminance (0.30, 0.59, 0.11).
        Prints: sigma=<s in 480-line pixels> alpha=<a> fit=<share of the variance of test - base explained>

    pp-measure.py diff <a.png> <b.png> [x0 y0 x1 y1]
        Native resolution, no scaling: the number of pixels that differ at all, the largest difference and the
        mean, inside the box (default: everything).
        Prints: differing=<n> of <total> max=<m> mean=<m>

    pp-measure.py sheet <out.png> <label> <a.png> [<label> <b.png> ...]
        Pictures side by side at the 480-line scale (640x480 for 4:3), each with its label above.

Pure Python and PIL (no numpy on this machine).
"""
import math
import sys

from PIL import Image, ImageDraw


def scaled(path):
    im = Image.open(path).convert('RGB')
    w, h = im.size
    if h != 480:
        im = im.resize((max(1, round(w * 480 / h)), 480), Image.BOX)
    return im


def lum_rows(im, box, margin):
    """Luminance as a list of rows, for the box grown by margin (clamped at the picture's edges)."""
    w, h = im.size
    px = im.load()
    x0, y0, x1, y1 = box
    rows = []
    for y in range(y0 - margin, y1 + margin):
        yy = min(max(y, 0), h - 1)
        row = []
        for x in range(x0 - margin, x1 + margin):
            xx = min(max(x, 0), w - 1)
            r, g, b = px[xx, yy]
            row.append(0.30 * r + 0.59 * g + 0.11 * b)
        rows.append(row)
    return rows


def kernel(s):
    if s <= 0:
        return [1.0]
    r = int(math.ceil(3 * s))
    k = [math.exp(-0.5 * (i / s) ** 2) for i in range(-r, r + 1)]
    t = sum(k)
    return [v / t for v in k]


def blur_inner(rows, s, axis, margin, size):
    """G(rows) on the inner box only (rows carry `margin` extra pixels on every side, margin >= 3s)."""
    bw, bh = size
    k = kernel(s); r = len(k) // 2
    if 'y' in axis:
        ylo, yhi = margin - r, margin + bh + r
    else:
        ylo, yhi = margin, margin + bh
    if 'x' in axis:
        tmp = [[sum(k[j] * rows[y][x + j - r] for j in range(len(k))) for x in range(margin, margin + bw)] for y in range(ylo, yhi)]
    else:
        tmp = [rows[y][margin:margin + bw] for y in range(ylo, yhi)]
    if 'y' in axis:
        return [[sum(k[j] * tmp[y + j][x] for j in range(len(k))) for x in range(bw)] for y in range(bh)]
    return tmp


def fit(base, test, s, axis, margin, size):
    bw, bh = size
    g = blur_inner(base, s, axis, margin, size)
    xs, ys = [], []
    for y in range(bh):
        for x in range(bw):
            xs.append(g[y][x] - base[y + margin][x + margin])
            ys.append(test[y + margin][x + margin] - base[y + margin][x + margin])
    n = len(xs)
    mx = sum(xs) / n; my = sum(ys) / n
    sxx = sum((v - mx) ** 2 for v in xs)
    sxy = sum((a - mx) * (b - my) for a, b in zip(xs, ys))
    syy = sum((v - my) ** 2 for v in ys)
    a = sxy / sxx if sxx > 0 else 0.0
    resid = syy - a * sxy
    return resid, a, (1 - resid / syy) if syy > 0 else 0.0


def cmd_sigma(args):
    base_path, test_path = args[0], args[1]
    x0, y0, x1, y1 = (int(v) for v in args[2:6])
    axis = args[6] if len(args) > 6 else 'xy'
    smax = 12.0
    margin = int(math.ceil(3 * smax)) + 1
    base = lum_rows(scaled(base_path), (x0, y0, x1, y1), margin)
    test = lum_rows(scaled(test_path), (x0, y0, x1, y1), margin)
    size = (x1 - x0, y1 - y0)
    best = None
    for step, span in ((0.5, None), (0.1, 0.5), (0.05, 0.1)):
        if span is None:
            grid = [step * i for i in range(1, int(smax / step) + 1)]
        else:
            c = best[0]
            grid = [c + step * i for i in range(-int(round(span / step)), int(round(span / step)) + 1) if 0.05 <= c + step * i <= smax]
        for s in grid:
            r = fit(base, test, s, axis, margin, size)
            if best is None or r[0] < best[1][0]:
                best = (s, r)
    print('sigma=%.2f alpha=%.3f fit=%.3f' % (best[0], best[1][1], best[1][2]))


def cmd_glow(args):
    off, on = scaled(args[0]).load(), scaled(args[1]).load()
    x0 = int(args[2]) if len(args) > 2 else 411
    y0 = int(args[3]) if len(args) > 3 else 138
    half = int(args[4]) if len(args) > 4 else 10
    p = []
    for y in range(480):
        s = 0.0
        for x in range(x0 - half, x0 + half):
            a = on[x, y]; b = off[x, y]
            s += (a[0] - b[0] + a[1] - b[1] + a[2] - b[2]) / 3.0
        p.append(s / (2 * half))
    pk = max(range(y0 - 15, y0 + 16), key=lambda y: p[y])
    peak = p[pk]
    lo = pk
    while lo > 0 and p[lo - 1] > 0.1 * peak:
        lo -= 1
    hi = pk
    while hi < 479 and p[hi + 1] > 0.1 * peak:
        hi += 1
    print('peak=%.2f width=%.2f%% rows=%d..%d' % (peak, 100.0 * (hi - lo + 1) / 480, lo, hi))


def cmd_diff(args):
    a = Image.open(args[0]).convert('RGB'); b = Image.open(args[1]).convert('RGB')
    if a.size != b.size:
        print('differing=nan sizes %s %s' % (a.size, b.size)); return
    w, h = a.size
    x0, y0, x1, y1 = (int(v) for v in args[2:6]) if len(args) >= 6 else (0, 0, w, h)
    pa, pb = a.load(), b.load()
    n = mx = 0; tot = 0.0
    for y in range(y0, y1):
        for x in range(x0, x1):
            d = max(abs(pa[x, y][c] - pb[x, y][c]) for c in range(3))
            if d:
                n += 1
            mx = max(mx, d); tot += d
    print('differing=%d of %d max=%d mean=%.4f' % (n, (x1 - x0) * (y1 - y0), mx, tot / ((x1 - x0) * (y1 - y0))))


def cmd_sheet(args):
    out = args[0]
    pairs = list(zip(args[1::2], args[2::2]))
    ims = [(label, scaled(path)) for label, path in pairs]
    lh = 18
    w = sum(im.size[0] for _, im in ims) + 8 * (len(ims) - 1)
    h = max(im.size[1] for _, im in ims) + lh
    sheet = Image.new('RGB', (w, h), (255, 0, 255))
    d = ImageDraw.Draw(sheet)
    x = 0
    for label, im in ims:
        d.rectangle([x, 0, x + im.size[0] - 1, lh - 1], fill=(0, 0, 0))
        d.text((x + 4, 3), label, fill=(255, 255, 255))
        sheet.paste(im, (x, lh))
        x += im.size[0] + 8
    sheet.save(out)
    print('sheet %s %dx%d' % (out, w, h))


if __name__ == '__main__':
    cmds = {'glow': cmd_glow, 'sigma': cmd_sigma, 'diff': cmd_diff, 'sheet': cmd_sheet}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        print(__doc__); sys.exit(2)
    cmds[sys.argv[1]](sys.argv[2:])
