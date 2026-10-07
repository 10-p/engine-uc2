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

    pp-measure.py shift <base.png> <test.png> x0 y0 x1 y1
        The horizontal displacement that best explains test from base in the box (test = a * base(x + s) + b),
        in 480-line pixels: for the radial blur, whose zoomed copies pull the picture towards the middle.
        Prints: shift=<s> gain=<a> fit=<share of the variance explained>

    pp-measure.py diff <a.png> <b.png> [x0 y0 x1 y1]
        Native resolution, no scaling: the number of pixels that differ at all, the largest difference and the
        mean, inside the box (default: everything).
        Prints: differing=<n> of <total> max=<m> mean=<m>

    pp-measure.py sheet <out.png> <label> <a.png> [<label> <b.png> ...]
        Pictures side by side at the 480-line scale (640x480 for 4:3), each with its label above.

    pp-measure.py crop <out.png> x0 y0 x1 y1 <scale> <label> <a.png> [<label> <b.png> ...]
        The same part of each picture (box on the 480-line picture), cut at the picture's own resolution and
        brought to scale times the box's size: as the pictures look on screens of the same size.

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
    smax = float(__import__("os").environ.get("PP_SMAX", "12"))
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


def cmd_shift(args):
    """shift <base.png> <test.png> x0 y0 x1 y1: the horizontal displacement s (480-line pixels, + = content from
    further right) for which a * base(x + s) + b fits test best inside the box (linear interpolation; -40..40 in
    0.25, refined to 0.05). For the radial blur, whose zoomed copies pull the picture towards the middle."""
    base_path, test_path = args[0], args[1]
    x0, y0, x1, y1 = (int(v) for v in args[2:6])
    margin = 42
    base = lum_rows(scaled(base_path), (x0, y0, x1, y1), margin)
    test = lum_rows(scaled(test_path), (x0, y0, x1, y1), margin)
    bw, bh = x1 - x0, y1 - y0

    def resid(s):
        xs, ys = [], []
        for y in range(bh):
            rb, rt = base[y + margin], test[y + margin]
            for x in range(bw):
                p = x + margin + s
                i = int(math.floor(p)); f = p - i
                xs.append(rb[i] * (1 - f) + rb[i + 1] * f)
                ys.append(rt[x + margin])
        n = len(xs); mx = sum(xs) / n; my = sum(ys) / n
        sxx = sum((v - mx) ** 2 for v in xs); sxy = sum((a - mx) * (b - my) for a, b in zip(xs, ys))
        syy = sum((v - my) ** 2 for v in ys)
        a = sxy / sxx if sxx > 0 else 0.0
        return syy - a * sxy, a, (1 - (syy - a * sxy) / syy) if syy > 0 else 0.0

    best = None
    for step, span in ((0.25, None), (0.05, 0.25)):
        grid = [step * i for i in range(-160, 161)] if span is None else [best[0] + step * i for i in range(-5, 6)]
        for s in grid:
            r = resid(s)
            if best is None or r[0] < best[1][0]:
                best = (s, r)
    print('shift=%.2f gain=%.3f fit=%.3f' % (best[0], best[1][1], best[1][2]))


def cmd_zoom(args):
    """zoom <base.png> <test.png> x0 y0 x1 y1: the radial blur as one zoomed copy. Epic's radial blur draws the
    picture again into a quad inset by d pixels of a 640x480 screen on every side (d, d/2, ..., d/6), i.e. zoomed
    by 640/(640-2d) across and 480/(480-2d) down about the middle. Fitted: test = a * Z_d(base) + c * base + b, for
    d from 0 to 40 in steps of 0.5, refined to 0.1; d is the inset of the one copy that explains the blend best,
    in 640x480 pixels (on a wider picture the across zoom is the same factor). Prints: inset=<d> a=<a> fit=<r2>"""
    base_im, test_im = scaled(args[0]), scaled(args[1])
    x0, y0, x1, y1 = (int(v) for v in args[2:6])
    W, H = base_im.size
    cx, cy = W / 2.0, H / 2.0
    full = lum_rows(base_im, (0, 0, W, H), 0)
    test = lum_rows(test_im, (x0, y0, x1, y1), 0)

    def sample(x, y):
        x = min(max(x - 0.5, 0.0), W - 1.001); y = min(max(y - 0.5, 0.0), H - 1.001)
        i, j = int(x), int(y); fx, fy = x - i, y - j
        r0, r1 = full[j], full[j + 1]
        return (r0[i] * (1 - fx) + r0[i + 1] * fx) * (1 - fy) + (r1[i] * (1 - fx) + r1[i + 1] * fx) * fy

    def resid(d):
        zx = 640.0 / (640.0 - 2 * d); zy = 480.0 / (480.0 - 2 * d)
        rows = []
        for y in range(y0, y1):
            for x in range(x0, x1):
                px, py = x + 0.5, y + 0.5
                rows.append((sample(cx + (px - cx) * zx, cy + (py - cy) * zy), full[y][x], test[y - y0][x - x0]))
        # least squares t = a z + c o + b (normal equations, 3x3)
        n = len(rows)
        sz = sum(r[0] for r in rows); so = sum(r[1] for r in rows); st = sum(r[2] for r in rows)
        szz = sum(r[0] * r[0] for r in rows); soo = sum(r[1] * r[1] for r in rows); szo = sum(r[0] * r[1] for r in rows)
        szt = sum(r[0] * r[2] for r in rows); sot = sum(r[1] * r[2] for r in rows); stt = sum(r[2] * r[2] for r in rows)
        A = [[szz, szo, sz], [szo, soo, so], [sz, so, n]]; B = [szt, sot, st]
        det = (A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
               + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]))
        if abs(det) < 1e-9:
            return float('inf'), 0, 0
        def solve(col):
            M = [row[:] for row in A]
            for k in range(3):
                M[k][col] = B[k]
            return (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
                    + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det
        a, c, b = solve(0), solve(1), solve(2)
        res = stt - a * szt - c * sot - b * st
        var = stt - st * st / n
        return res, a, (1 - res / var) if var > 0 else 0.0

    best = None
    for step, span in ((0.5, None), (0.1, 0.5)):
        grid = [step * i for i in range(0, 81)] if span is None else [max(0.0, best[0] + step * i) for i in range(-5, 6)]
        for d in grid:
            r = resid(d)
            if best is None or r[0] < best[1][0]:
                best = (d, r)
    print('inset=%.2f a=%.3f fit=%.3f' % (best[0], best[1][1], best[1][2]))


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


def cmd_crop(args):
    """crop <out.png> x0 y0 x1 y1 scale <label> <a.png> [<label> <b.png> ...]: the same part of each picture
    (coordinates on the 480-line picture), cut from the picture at its own resolution and brought to (x1-x0)*scale
    by (y1-y0)*scale pixels (Lanczos): as the pictures would look on screens of the same size."""
    out = args[0]
    x0, y0, x1, y1 = (int(v) for v in args[1:5])
    scale = float(args[5])
    pairs = list(zip(args[6::2], args[7::2]))
    w, h = int((x1 - x0) * scale), int((y1 - y0) * scale)
    lh = 18
    sheet = Image.new('RGB', (w * len(pairs) + 8 * (len(pairs) - 1), h + lh), (255, 0, 255))
    d = ImageDraw.Draw(sheet)
    for i, (label, path) in enumerate(pairs):
        im = Image.open(path).convert('RGB')
        k = im.size[1] / 480.0
        part = im.crop((round(x0 * k), round(y0 * k), round(x1 * k), round(y1 * k))).resize((w, h), Image.LANCZOS)
        x = i * (w + 8)
        d.rectangle([x, 0, x + w - 1, lh - 1], fill=(0, 0, 0))
        d.text((x + 4, 3), label, fill=(255, 255, 255))
        sheet.paste(part, (x, lh))
    sheet.save(out)
    print('crop sheet %s %dx%d' % (out, sheet.size[0], sheet.size[1]))


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
    cmds = {'glow': cmd_glow, 'sigma': cmd_sigma, 'diff': cmd_diff, 'sheet': cmd_sheet, 'crop': cmd_crop, 'shift': cmd_shift, 'zoom': cmd_zoom}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        print(__doc__); sys.exit(2)
    cmds[sys.argv[1]](sys.argv[2:])
