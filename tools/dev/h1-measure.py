#!/usr/bin/env python3
"""Bloom measurements on screenshots. Coordinates are given in the 1280x720 frame
and mapped to each picture (AutoFOV off: the horizontal field of view is fixed, so a picture W wide shows the 1280
frame scaled by W/1280, centred vertically), so one set of numbers serves every resolution of the same view. Pure
Python + PIL.

    h1-measure.py profile <with.png> <without.png> x y dir [band]
        The glow = with - without (mean of R, G, B), along the row y (averaged over +-band rows of the 720 frame,
        default 2) from x in direction dir (-1 left, +1 right), out to 150 px of the 720 frame. Prints the peak, and
        the distances from x where the glow last falls below 50%, 25%, 10% and 5% of the peak, in the picture's own
        pixels and in pixels of the 720 frame (so equal numbers = the same area on screen).

    h1-measure.py rough <with.png> <without.png> x0 y0 x1 y1
        How blocky the glow is inside the box (720 frame): the glow image g = with - without (mean RGB), its second
        differences along x and y (g[i-1] - 2 g[i] + g[i+1]) at the picture's own pixels, RMS over the box, times
        (W/1280)^2, over the RMS of g. A glow that is the same smooth picture at every resolution gives the same number
        at every resolution; the corners of a bilinear magnification add to it. Also prints the largest such
        second difference, likewise normalised.

    h1-measure.py resid <with.png> <without.png> x0 y0 x1 y1 [sigma]
        Blockiness as departure from smoothness, inside a box that holds glow only (no light source): the glow g
        (mean RGB), minus g blurred by a Gaussian of `sigma` px of the 720 frame (default 3, scaled to the
        picture), RMS over the box, as a share of the glow's mean in the box (percent). Corners and flat facets of a
        coarse bilinear magnification leave a residual at their own scale; a smooth glow leaves only the 8-bit
        rounding.

    h1-measure.py check <tol_r> <tol_peak> <with.png> <off.png> [...] -- <with.png> <off.png> [...]
        Part 2 (PostProcessTargetScale): the radii of the DM-Deadbolt view of tools/dev/h1/*.scen, set A (pictures
        before the --, e.g. scale 1) against set B (e.g. scale 2): the white lamp (943,280 leftwards) and the blue
        emissive box (545,180 leftwards), peak, r50 and r10 in px of the 720 frame (the last pixel at or above
        50% / 10% of the peak before the profile first falls below half of that: robust against a dip inside a
        lamp and against another light further out), per picture, then the mean and
        spread of each set and one PASS/FAIL line per quantity: |mean B - mean A| <= tol_r (radii) or tol_peak
        (peaks). A last line "check: PASS|FAIL n of 6".

    h1-measure.py sheet <out.png> x0 y0 x1 y1 scale gain <label> <with.png> <off.png|-> [...]
        Part 2: as zoom, one tile per triple: the picture itself (off "-") or its glow (with - off) times gain.

    h1-measure.py zoom <out.png> x0 y0 x1 y1 scale <label> <a.png> [<label> <b.png> ...] [--gain g --base <off.png>]
        The same box (720 frame) of each picture at its own resolution, magnified (nearest) so that the 720 frame's
        box is shown `scale` times; with --base, the glow (picture - base) times g is shown instead.
"""
import sys

from PIL import Image, ImageDraw


def load(path):
    im = Image.open(path).convert('RGB')
    return im, im.load(), im.size


def frame(W, H):
    """Scale and vertical offset from the 1280x720 frame to a W x H picture (AutoFOV off)."""
    s = W / 1280.0
    return s, (H - 720 * s) / 2.0


def lum(px, x, y):
    r, g, b = px[x, y]
    return (r + g + b) / 3.0


def cmd_profile(a):
    wpath, opath, x, y, d = a[0], a[1], float(a[2]), float(a[3]), int(a[4])
    band = float(a[5]) if len(a) > 5 else 2.0
    wi, wp, (W, H) = load(wpath)
    oi, op, _ = load(opath)
    s, oy = frame(W, H)
    cx, cy = int(round(x * s)), int(round(y * s + oy))
    nb = max(1, int(round(band * s)))
    prof = []
    for i in range(int(150 * s)):
        xx = cx + d * i
        if xx < 0 or xx >= W:
            break
        v = 0.0
        for yy in range(cy - nb, cy + nb + 1):
            v += lum(wp, xx, yy) - lum(op, xx, yy)
        prof.append(v / (2 * nb + 1))
    pk = max(prof)
    ipk = prof.index(pk)
    out = ['peak=%.2f at %d px' % (pk, ipk)]
    for f in (.5, .25, .1, .05):
        last = None
        for i in range(ipk, len(prof)):
            if prof[i] >= f * pk:
                last = i
        out.append('r%d=%d px (%.1f @720)' % (int(f * 100), last, last / s))
    print(' '.join(out))


def measure(wpath, opath, x, y, d, band=2.0):
    """peak (mean RGB levels) and r50, r10 in px of the 720 frame, as cmd_profile."""
    wi, wp, (W, H) = load(wpath)
    oi, op, _ = load(opath)
    s, oy = frame(W, H)
    cx, cy = int(round(x * s)), int(round(y * s + oy))
    nb = max(1, int(round(band * s)))
    prof = []
    for i in range(int(150 * s)):
        xx = cx + d * i
        if xx < 0 or xx >= W:
            break
        prof.append(sum(lum(wp, xx, yy) - lum(op, xx, yy) for yy in range(cy - nb, cy + nb + 1)) / (2 * nb + 1))
    pk = max(prof)
    ipk = prof.index(pk)
    out = {'peak': pk}
    for f in (.5, .1):
        # the glow's own radius: the last pixel at or above f x peak before the profile first falls below half of
        # that (cmd_profile takes the last one anywhere out to 150 px, which another light further out can catch;
        # stopping at the first fall below f x peak catches a dip between the bright parts of a lamp)
        last = ipk
        for i in range(ipk, len(prof)):
            if prof[i] < f * pk / 2:
                break
            if prof[i] >= f * pk:
                last = i
        out['r%d' % int(f * 100)] = last / s
    return out


POINTS = (('lamp', 943, 280, -1), ('blue', 545, 180, -1))


def cmd_check(a):
    tol_r, tol_p = float(a[0]), float(a[1])
    rest = a[2:]
    i = rest.index('--')
    sets = (rest[:i], rest[i + 1:])
    means = []
    for name, files in zip('AB', sets):
        vals = {}
        for j in range(0, len(files), 2):
            row = []
            for (pname, x, y, d) in POINTS:
                m = measure(files[j], files[j + 1], x, y, d)
                for q in ('peak', 'r50', 'r10'):
                    vals.setdefault((pname, q), []).append(m[q])
                row.append('%s %.1f/%.1f/%.1f' % (pname, m['peak'], m['r50'], m['r10']))
            print('%s %s: %s (peak/r50/r10)' % (name, files[j].split('/')[-1], ' | '.join(row)))
        means.append(vals)
    ok = 0
    for (pname, x, y, d) in POINTS:
        for q in ('peak', 'r50', 'r10'):
            A, B = means[0][(pname, q)], means[1][(pname, q)]
            ma, mb = sum(A) / len(A), sum(B) / len(B)
            tol = tol_p if q == 'peak' else tol_r
            good = abs(mb - ma) <= tol
            ok += good
            print('%s %s %s: A %.1f (spread %.1f, n %d)  B %.1f (spread %.1f, n %d)  B-A %+.1f  tolerance %.1f' % (
                'PASS' if good else 'FAIL', pname, q, ma, max(A) - min(A), len(A), mb, max(B) - min(B), len(B), mb - ma, tol))
    print('check: %s %d of 6' % ('PASS' if ok == 6 else 'FAIL', ok))


def cmd_rough(a):
    wpath, opath = a[0], a[1]
    x0, y0, x1, y1 = (float(v) for v in a[2:6])
    wi, wp, (W, H) = load(wpath)
    oi, op, _ = load(opath)
    s, oy = frame(W, H)
    X0, X1 = int(round(x0 * s)), int(round(x1 * s))
    Y0, Y1 = int(round(y0 * s + oy)), int(round(y1 * s + oy))
    g = [[lum(wp, x, y) - lum(op, x, y) for x in range(X0 - 1, X1 + 1)] for y in range(Y0 - 1, Y1 + 1)]
    n = 0; ss = 0.0; gg = 0.0; mx = 0.0
    for j in range(1, len(g) - 1):
        for i in range(1, len(g[0]) - 1):
            dx = g[j][i - 1] - 2 * g[j][i] + g[j][i + 1]
            dy = g[j - 1][i] - 2 * g[j][i] + g[j + 1][i]
            ss += dx * dx + dy * dy
            gg += g[j][i] * g[j][i]
            mx = max(mx, abs(dx), abs(dy))
            n += 1
    rms = (ss / (2 * n)) ** .5
    grms = (gg / n) ** .5
    print('rough=%.4f max2nd=%.4f glow_rms=%.2f (box %dx%d px, scale %.3f)' % (rms * s * s / grms, mx * s * s / grms, grms, X1 - X0, Y1 - Y0, s))


def cmd_resid(a):
    import math
    wpath, opath = a[0], a[1]
    x0, y0, x1, y1 = (float(v) for v in a[2:6])
    sig720 = float(a[6]) if len(a) > 6 else 3.0
    wi, wp, (W, H) = load(wpath)
    oi, op, _ = load(opath)
    s, oy = frame(W, H)
    sig = sig720 * s
    r = int(math.ceil(3 * sig))
    k = [math.exp(-0.5 * (i / sig) ** 2) for i in range(-r, r + 1)]
    t = sum(k); k = [v / t for v in k]
    X0, X1 = int(round(x0 * s)), int(round(x1 * s))
    Y0, Y1 = int(round(y0 * s + oy)), int(round(y1 * s + oy))
    g = [[lum(wp, x, y) - lum(op, x, y) for x in range(X0 - r, X1 + r)] for y in range(Y0 - r, Y1 + r)]
    h = [[sum(k[j] * row[x + j] for j in range(len(k))) for x in range(X1 - X0)] for row in g]
    n = 0; ss = 0.0; m = 0.0
    for y in range(Y1 - Y0):
        for x in range(X1 - X0):
            b = sum(k[j] * h[y + j][x] for j in range(len(k)))
            v = g[y + r][x + r]
            ss += (v - b) ** 2; m += v; n += 1
    print('resid=%.2f%% glow_mean=%.2f (box %dx%d px, sigma %.1f px)' % (100 * (ss / n) ** .5 / (m / n), m / n, X1 - X0, Y1 - Y0, sig))


def cmd_zoom(a):
    out = a[0]
    x0, y0, x1, y1 = (float(v) for v in a[1:5])
    scale = float(a[5])
    rest = a[6:]
    gain = None; base = None
    if '--gain' in rest:
        i = rest.index('--gain'); gain = float(rest[i + 1]); rest = rest[:i] + rest[i + 2:]
    if '--base' in rest:
        i = rest.index('--base'); base = rest[i + 1]; rest = rest[:i] + rest[i + 2:]
    pairs = [(rest[i], rest[i + 1]) for i in range(0, len(rest), 2)]
    tiles = []
    bw, bh = int((x1 - x0) * scale), int((y1 - y0) * scale)
    for label, path in pairs:
        im = Image.open(path).convert('RGB')
        s, oy = frame(*im.size)
        box = (int(round(x0 * s)), int(round(y0 * s + oy)), int(round(x1 * s)), int(round(y1 * s + oy)))
        c = im.crop(box)
        if base:
            # a base of the same resolution as this picture: the name with the label's resolution
            b = Image.open(base.replace('RES', '%dx%d' % im.size)).convert('RGB').crop(box)
            cp = c.load(); bp = b.load()
            for yy in range(c.size[1]):
                for xx in range(c.size[0]):
                    cp[xx, yy] = tuple(max(0, min(255, int((cp[xx, yy][k] - bp[xx, yy][k]) * (gain or 1)))) for k in range(3))
        tiles.append((label, c.resize((bw, bh), Image.NEAREST)))
    sheet = Image.new('RGB', (len(tiles) * (bw + 6), bh + 18), (255, 0, 255))
    d = ImageDraw.Draw(sheet)
    for i, (label, t) in enumerate(tiles):
        sheet.paste(t, (i * (bw + 6), 18))
        d.rectangle((i * (bw + 6), 0, i * (bw + 6) + bw, 17), fill=(0, 0, 0))
        d.text((i * (bw + 6) + 3, 3), label, fill=(255, 255, 255))
    sheet.save(out)
    print('wrote %s (%d tiles of %dx%d)' % (out, len(tiles), bw, bh))


def cmd_sheet(a):
    out = a[0]
    x0, y0, x1, y1 = (float(v) for v in a[1:5])
    scale, gain = float(a[5]), float(a[6])
    rest = a[7:]
    bw, bh = int((x1 - x0) * scale), int((y1 - y0) * scale)
    tiles = []
    for i in range(0, len(rest), 3):
        label, path, off = rest[i:i + 3]
        im = Image.open(path).convert('RGB')
        s, oy = frame(*im.size)
        box = (int(round(x0 * s)), int(round(y0 * s + oy)), int(round(x1 * s)), int(round(y1 * s + oy)))
        c = im.crop(box)
        if off != '-':
            b = Image.open(off).convert('RGB').crop(box)
            cp = c.load(); bp = b.load()
            for yy in range(c.size[1]):
                for xx in range(c.size[0]):
                    cp[xx, yy] = tuple(max(0, min(255, int((cp[xx, yy][k] - bp[xx, yy][k]) * gain))) for k in range(3))
        tiles.append((label, c.resize((bw, bh), Image.NEAREST)))
    sheet = Image.new('RGB', (len(tiles) * (bw + 6), bh + 18), (255, 0, 255))
    d = ImageDraw.Draw(sheet)
    for i, (label, t) in enumerate(tiles):
        sheet.paste(t, (i * (bw + 6), 18))
        d.rectangle((i * (bw + 6), 0, i * (bw + 6) + bw, 17), fill=(0, 0, 0))
        d.text((i * (bw + 6) + 3, 3), label, fill=(255, 255, 255))
    sheet.save(out)
    print('wrote %s (%d tiles of %dx%d)' % (out, len(tiles), bw, bh))


if __name__ == '__main__':
    {'profile': cmd_profile, 'check': cmd_check, 'sheet': cmd_sheet, 'rough': cmd_rough, 'resid': cmd_resid, 'zoom': cmd_zoom}[sys.argv[1]](sys.argv[2:])
