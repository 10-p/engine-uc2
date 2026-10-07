#!/usr/bin/env python3
"""D-7 (console) and D-8 (AutoFOV) measurements on what tools/dev/d78.sh leaves. One PASS/FAIL line per check.

    python3 tools/dev/d78-measure.py <WorkUC2>/logs/d78/run

AutoFOV keeps the vertical field of view of the 4:3 picture and widens the horizontal one (Hor+). At the same
window height the 4:3 picture (1440x1080) must therefore be the centre of the 16:9 (1920x1080) and 21:9
(2560x1080) pictures of the same frozen scene, column for column: that is the main check (world, letterbox,
zoom), with AUTOFOV 0 as the control that must fail it. The vertical scale of the scene against the 4:3 one is
measured too (1.000 with AutoFOV; 1.333 and 1.778 without). The first-person weapon is measured on black
(the level not drawn): its size and place in the 4:3 frame. The lock-on reticle (WorldToScreen) is measured
against the target's own figure. 640x480 and 1024x768 are compared with main's binary (pinned view, static
part, tools/dev/pinned-compare.py). Only PIL (no numpy on this machine).
"""
import sys, os, re, math, subprocess
from PIL import Image, ImageChops

run = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
fails = 0


def result(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'} {name}: {detail}", flush=True)


def pic(sub, name):
    p = os.path.join(run, sub, name + '.png')
    return Image.open(p).convert('RGB') if os.path.exists(p) else None


def centre(im, w=1440):
    """The centre w columns of a picture (the 4:3 frame of the same height)."""
    off = (im.size[0] - w) // 2
    return im.crop((off, 0, off + w, im.size[1]))


def maxdiff(a, b):
    d = ImageChops.difference(a, b)
    r, g, bl = d.split()
    return ImageChops.lighter(ImageChops.lighter(r, g), bl)


def within(a, b, tol=2, region=None):
    """Percentage of pixels whose largest channel difference is <= tol (inside region, if given)."""
    m = maxdiff(a, b)
    if region:
        m = m.crop(region)
    h = m.histogram()
    return 100.0 * sum(h[:tol + 1]) / sum(h)


def row_profile(im, xs):
    """Mean grey level per row over the columns xs (a list of x ranges)."""
    g = im.convert('L')
    W, H = g.size
    px = g.load()
    prof = []
    for y in range(H):
        s = n = 0
        for x0, x1 in xs:
            for x in range(x0, x1):
                s += px[x, y]; n += 1
        prof.append(s / n)
    return prof


def vscale(ref, other):
    """The zoom z (about the centre row) that best maps the 4:3 picture's rows onto the other picture's rows,
    measured on two strips 60-100 px left and right of the centre column (away from the crosshair)."""
    H = ref.size[1]
    cr, co = ref.size[0] // 2, other.size[0] // 2
    pr = row_profile(ref, [(cr - 100, cr - 60), (cr + 60, cr + 100)])
    po = row_profile(other, [(co - 100, co - 60), (co + 60, co + 100)])
    c = (H - 1) / 2.0
    best = None
    z = 0.80
    while z <= 2.0001:
        err = n = 0
        for y in range(0, H, 2):
            yo = c + (y - c) * z
            if 0 <= yo < H - 1:
                i = int(yo); f = yo - i
                v = po[i] * (1 - f) + po[i + 1] * f
                err += abs(v - pr[y]); n += 1
        if n > H // 4:
            e = err / n
            if best is None or e < best[1]:
                best = (z, e)
        z += 0.002
    return best


def blob(im, x0, x1, y0, thr=8):
    """Non-black pixels of a picture on black in x0..x1, y >= y0: (count, centroid, bbox)."""
    g = im.convert('L').crop((x0, y0, x1, im.size[1])).point(lambda v: 255 if v > thr else 0)
    bb = g.getbbox()
    if bb is None:
        return None
    px = g.load(); W, H = g.size
    n = sx = sy = 0
    for y in range(bb[1], bb[3]):
        for x in range(bb[0], bb[2]):
            if px[x, y]:
                n += 1; sx += x; sy += y
    return n, (x0 + sx / n, y0 + sy / n), (bb[0] + x0, bb[1] + y0, bb[2] + x0, bb[3] + y0)


def log_text(sub, name):
    p = os.path.join(run, sub, name + '.out')
    return open(p, encoding='latin-1').read() if os.path.exists(p) else ''


def hp(base, aspect, ref=4.0 / 3.0):
    return 2 * math.degrees(math.atan(math.tan(math.radians(base / 2)) * aspect / ref))


# ------------------------------------------------------------------------- 1. 4:3 identical to main's binary
for res in ('640x480', '1024x768'):
    a = os.path.join(run, 'ident', res, 'main-pinned-hud.png')
    b = os.path.join(run, 'ident', res, 'pinned-hud.png')
    if os.path.exists(a) and os.path.exists(b):
        o = subprocess.run([sys.executable, os.path.join(here, 'pinned-compare.py'), a, b,
                            os.path.join(run, 'ident', res, 'diff.png')], capture_output=True, text=True).stdout.split()
        lg = log_text(os.path.join('ident', res), 'pinned')
        w, h = res.split('x')
        m = re.search(r'\[AutoFOV\] viewport=%sx%s aspect=\S+ base=([\d.]+) effective=([\d.]+)' % (w, h), lg)
        same = m is not None and m.group(1) == m.group(2)
        ok = len(o) >= 2 and o[0] == 'OK' and float(o[1]) >= 99.99 and same
        result(f'identical-to-main-{res}', ok,
               f'pinned DM-Deadbolt, -nopostprocess, static part within 2 levels {o[1] if len(o) > 1 else "?"}% '
               f'(whole frame bit-identical {o[2] if len(o) > 2 else "?"}%, PSNR {o[3] if len(o) > 3 else "?"} dB); '
               f'AutoFOV logged ' + (f'base {m.group(1)} effective {m.group(2)}' if m else 'nothing'))
    else:
        result(f'identical-to-main-{res}', False, 'missing pictures')

# ------------------------------------------------------------------------- 2. the numbers in the log
lg = log_text('fov', 'fov')
expect = [('1920x1080', 90.0, 16 / 9.0), ('2560x1080', 90.0, 2560 / 1080.0), ('1920x1080', 40.0, 16 / 9.0),
          ('2560x1080', 40.0, 2560 / 1080.0), ('1440x1080', 90.0, 4 / 3.0), ('1440x1080', 40.0, 4 / 3.0)]
bad = []
for size, base, asp in expect:
    w, h = size.split('x')
    m = re.search(r'\[AutoFOV\] viewport=%sx%s aspect=\S+ base=%.6f effective=([\d.]+)' % (w, h, base), lg)
    want = base if abs(asp - 4 / 3.0) < 1e-6 else hp(base, asp)
    if not m or abs(float(m.group(1)) - want) > 0.001:
        bad.append(f'{size} base {base}: {m.group(1) if m else "no line"} (want {want:.4f})')
for size, asp in (('1440x1080', 4 / 3.0), ('1920x1080', 16 / 9.0), ('2560x1080', 2560 / 1080.0)):
    w, h = size.split('x')
    m = re.search(r'\[AutoFOV\] weapon viewport=%sx%s base=60\.000000 drawFov=([\d.]+)' % (w, h), lg)
    want = 60.0 if size == '1440x1080' else hp(60.0, asp)
    if not m or abs(float(m.group(1)) - want) > 0.001:
        bad.append(f'weapon {size}: {m.group(1) if m else "no line"} (want {want:.4f})')
result('fov-numbers', not bad and '[AutoFOV] mode=2 referenceAspect=1.333333' in lg,
       'effective FOV = 2*atan(tan(base/2)*aspect/(4/3)) for the world (90, zoom 40) and the weapon (60) at '
       '16:9 and 21:9, unchanged at 4:3' + ('' if not bad else '; wrong: ' + '; '.join(bad)))
res = os.path.join(run, 'fov', 'fov.result')
rtext = open(res).read() if os.path.exists(res) else ''
result('fov-scenario', 'FAIL' not in rtext and rtext.count('PASS') >= 5,
       'fov.scen checks: ' + ', '.join(l.split('\t')[1] + '=' + l.split('\t')[0] for l in rtext.splitlines() if '\t' in l)
       + ' (sizes, the player\'s own FovAngle untouched outside the frame, zoom 40 held)')

# ------------------------------------------------------------------------- 3. the 4:3 picture is the centre
def centre_check(name, kind, thr, region=None, wides=(1920, 2560), ctl=1920):
    ref = pic('fov', f'fov-{kind}-1440x1080')
    if ref is None:
        result(name, False, f'missing fov-{kind}-1440x1080'); return
    parts = []; ok = True
    for w in wides:
        im = pic('fov', f'fov-{kind}-{w}x1080')
        if im is None:
            ok = False; parts.append(f'{w}: missing'); continue
        p = within(ref, centre(im), 2, region)
        t = thr[w] if isinstance(thr, dict) else thr
        ok &= p >= t
        parts.append(f'{w}x1080 centre {p:.2f}% within 2 levels (>= {t})')
    off = pic('fov', f'fov-{kind}-{ctl}x1080-off')
    if off is not None:
        p = within(ref, centre(off), 2, region)
        ok &= p < 50
        parts.append(f'control AUTOFOV 0 at {ctl}x1080: {p:.2f}% (< 50)')
    result(name, ok, f'{kind}: 1440x1080 vs the centre 1440 columns of ' + '; '.join(parts)
           + (f'; region {region}' if region else ''))


centre_check('world-third-person', 'tp', {1920: 98.5, 2560: 96.0})
centre_check('letterbox', 'cin', {1920: 98.5, 2560: 96.0})
# first person: above the weapon (y < 560; the weapon and the two animated pickups are below)
centre_check('world-first-person', 'fp', {1920: 98.5, 2560: 96.0}, region=(0, 0, 1440, 560))
ref = pic('fov', 'fov-zoom-1440x1080')
if ref is not None:
    parts = []; ok = True
    for w, t in ((1920, 98.0), (2560, 96.0)):
        im = pic('fov', f'fov-zoom-{w}x1080')
        p = within(ref, centre(im), 2, (0, 0, 1440, 560)) if im else 0.0
        ok &= p >= t
        parts.append(f'{w}x1080 centre {p:.2f}% within 2 levels above the weapon (>= {t})')
    result('zoom-40', ok, 'FOV 40 (the sniper rifle sets DesiredFOV/FovAngle this way): 1440x1080 vs ' + '; '.join(parts))
else:
    result('zoom-40', False, 'missing pictures')

# vertical scale of the scene against the 4:3 picture (1.000 = the same vertical field of view)
ref = pic('fov', 'fov-tp-1440x1080')
parts = []; ok = ref is not None
for name, want, tol in (('tp-1920x1080', 1.0, 0.006), ('tp-2560x1080', 1.0, 0.006), ('zoom-1920x1080', 1.0, 0.006),
                        ('tp-1920x1080-off', 4 / 3.0, 0.05), ('tp-2560x1080-off', (2560 / 1080.0) / (4 / 3.0), 0.06)):
    r = pic('fov', 'fov-zoom-1440x1080') if name.startswith('zoom') else ref
    im = pic('fov', 'fov-' + name)
    if r is None or im is None:
        ok = False; parts.append(f'{name}: missing'); continue
    z, e = vscale(r, im)
    ok &= abs(z - want) <= tol
    parts.append(f'{name} {z:.3f} (want {want:.3f})')
result('vertical-extent', ok, 'vertical scale of the scene against the 4:3 picture, best fit of the rows near the centre '
       'column (the HUD, which does not scale, pulls the AUTOFOV 0 controls a little toward 1): ' + ', '.join(parts)
       + '; with AutoFOV within 0.006 of 1, the controls within 0.05/0.06 of the stock zoom')

# more scene left and right: the columns outside the 4:3 frame are scene, not empty
im = pic('fov', 'fov-tp-1920x1080')
if im is not None:
    from PIL import ImageStat
    L = ImageStat.Stat(im.crop((0, 0, 240, 1080)).convert('L')).stddev[0]
    R = ImageStat.Stat(im.crop((1680, 0, 1920, 1080)).convert('L')).stddev[0]
    result('wider-horizontally', L > 6 and R > 6, f'16:9: the 240 columns left and right of the 4:3 frame hold scene '
           f'(grey level spread {L:.1f} and {R:.1f}, > 6); the centre is the 4:3 picture (world-third-person)')

# ------------------------------------------------------------------------- 4. the first-person weapon
ref = pic('fov', 'fov-fp-1440x1080-lo')
ref2 = pic('fov', 'fov-fp-1440x1080-lo2')
if ref is not None:
    b0 = blob(ref, 880, 1440, 560)
    rows = []; ok = b0 is not None
    drift = None
    if ref2 is not None and b0:
        b2 = blob(ref2, 880, 1440, 560)
        drift = (abs(b2[0] / b0[0] - 1) * 100, math.dist(b2[1], b0[1]))
    for name in ('fp-1920x1080-lo', 'fp-2560x1080-lo', 'fp-1920x1080-lo-off'):
        im = pic('fov', 'fov-' + name)
        if im is None or b0 is None:
            ok = False; rows.append(f'{name}: missing'); continue
        b = blob(centre(im), 880, 1440, 560)
        da = (b[0] / b0[0] - 1) * 100; dc = math.dist(b[1], b0[1])
        if name.endswith('-off'):
            ok &= abs(da) > 4 or dc > 8
            rows.append(f'control {name}: area {da:+.1f}%, centroid moved {dc:.1f} px')
        else:
            ok &= abs(da) <= 3.0 and dc <= 4.0
            rows.append(f'{name}: area {da:+.1f}%, centroid moved {dc:.1f} px, bbox {b[2]}')
    result('weapon-size-place', ok, 'first-person weapon (right hand, on black) against the 4:3 frame: 1440x1080 area '
           f'{b0[0] if b0 else "?"} px, bbox {b0[2] if b0 else "?"}; ' + '; '.join(rows) + ' (<= 3% and <= 4 px)'
           + (f'; the same 1440x1080 picture taken again later: area {drift[0]:.1f}%, centroid {drift[1]:.1f} px '
              f'(the idle animation in the frozen world)' if drift else ''))
else:
    result('weapon-size-place', False, 'missing pictures')

# ------------------------------------------------------------------------- 5. the lock-on reticle on its target
def reticle(lo):
    """The lock-on reticle (Element_TargetReticle, orange; placed with PlayerController.WorldToScreen) on black:
    the centroid of its pixels and their mean distance from it (its ring of arcs turns, which moves its bounding
    box but not these). y >= 300 (the HUD's top bars); colour orange/brown (the HUD's other parts are teal,
    green, white)."""
    W, H = lo.size
    r, g, b = lo.split()
    rp, gp, bp = r.load(), g.load(), b.load()
    pts = [(x, y) for y in range(300, H) for x in range(0, W, 1)
           if rp[x, y] > 70 and rp[x, y] >= gp[x, y] + 25 and gp[x, y] >= bp[x, y] + 10]
    if len(pts) < 200:
        return None
    cx = sum(p[0] for p in pts) / len(pts); cy = sum(p[1] for p in pts) / len(pts)
    rad = sum(math.hypot(x - cx, y - cy) for x, y in pts) / len(pts)
    return (cx, cy), rad, len(pts)


def figure(im, c, rad):
    """The target's figure (the summoned Liandri's purple) inside the reticle's ring (0.6 of its mean radius
    either side of its centre; the whole ring's height)."""
    W, H = im.size
    x0, x1 = max(0, int(c[0] - 0.6 * rad)), min(W, int(c[0] + 0.6 * rad))
    y0, y1 = max(300, int(c[1] - 1.3 * rad)), min(H, int(c[1] + 1.3 * rad))
    sub = im.crop((x0, y0, x1, y1))
    r, g, b = sub.split()
    m = ImageChops.multiply(ImageChops.multiply(r.point(lambda v: 255 if v > 120 else 0), g.point(lambda v: 255 if v < 100 else 0)),
                            b.point(lambda v: 255 if v > 140 else 0))
    bb = m.getbbox()
    if bb is None or m.histogram()[255] < 300:
        return None
    return (x0 + bb[0], y0 + bb[1], x0 + bb[2], y0 + bb[3])


ms = []
for name in ('lock-1440x1080', 'lock-1920x1080', 'lock-2560x1080', 'lock-1920x1080-off'):
    im = pic('fov', 'fov-' + name)
    lo = pic('fov', 'fov-' + name.replace('-off', '') + ('-lo-off' if name.endswith('-off') else '-lo'))
    if im is None or lo is None:
        ms.append((name, None)); continue
    rt = reticle(lo)
    fg = figure(im, rt[0], rt[1]) if rt else None
    if rt is None or fg is None:
        ms.append((name, None)); continue
    fc = ((fg[0] + fg[2]) / 2, (fg[1] + fg[3]) / 2)
    ms.append((name, (rt, fg, (rt[0][0] - fc[0], rt[0][1] - fc[1]))))
if all(m is not None for _, m in ms):
    on = [(n, m) for n, m in ms if not n.endswith('-off')]
    offs = [m[2] for _, m in on]
    spread = max(max(o[0] for o in offs) - min(o[0] for o in offs), max(o[1] for o in offs) - min(o[1] for o in offs))
    rads = [m[0][1] for _, m in on]
    rs = (max(rads) / min(rads) - 1) * 100
    # where the 4:3 reticle lands in the wider frames (+ (W-1440)/2) against where it is
    base = on[0][1][0][0]
    shift = max(math.dist((base[0] + (w - 1440) / 2, base[1]), m[0][0]) for (n, m), w in zip(on, (1440, 1920, 2560)))
    offm = ms[3][1]
    on_fig = all(m[1][0] <= m[0][0][0] <= m[1][2] and m[1][1] <= m[0][0][1] <= m[1][3] for _, m in ms)
    result('marker-on-actor', shift <= 2.0 and rs <= 1.0 and on_fig and spread <= 10,
           'lock-on reticle (WorldToScreen; its centre and mean radius on black) against the target\'s figure (the same '
           'frozen frame with the level): ' +
           '; '.join(f'{n} reticle centre ({m[0][0][0]:.1f},{m[0][0][1]:.1f}) radius {m[0][1]:.1f} figure {m[1]} '
                     f'offset ({m[2][0]:+.1f},{m[2][1]:+.1f})' for n, m in ms) +
           f'; the 4:3 reticle shifted by the side columns lands within {shift:.1f} px (<= 2) of the 16:9 and 21:9 ones and '
           f'their radius differs by {rs:.2f}% (<= 1): the marker is where it is at 4:3 in a picture whose centre is the 4:3 '
           f'picture (world-third-person); its centre is on the figure in every picture, and its offset from the figure\'s '
           f'box spreads {spread:.1f} px (<= 10; the purple box is a coarse measure) (AUTOFOV 0: radius {offm[0][1]:.1f}, '
           f'the view zoomed)')
else:
    result('marker-on-actor', False, 'no lock-on reticle / target found in: ' + ', '.join(n for n, m in ms if m is None))

# ------------------------------------------------------------------------- 6. the console
for name, n in (('console-game', 12), ('console-menu', 8)):
    p = os.path.join(run, 'console', name + '.result')
    t = open(p).read() if os.path.exists(p) else ''
    passes = t.count('PASS\t'); fl = [l.split('\t')[1] for l in t.splitlines() if l.startswith('FAIL')]
    result(name, passes >= n and not fl, f'{passes} checks passed' + (f', failed: {", ".join(fl)}' if fl else '')
           + f' ({os.path.join("console", name + ".result")})')

# ------------------------------------------------------------------------- 7. logs
bad = []
nlogs = 0
for root, dirs, files in os.walk(run):
    for f in files:
        if f.endswith('.out'):
            nlogs += 1
            t = open(os.path.join(root, f), encoding='latin-1').read()
            c = t.count('Critical'); g = len(re.findall(r'GL error 0x', t))
            if c or g:
                bad.append(f'{os.path.relpath(os.path.join(root, f), run)}: {c} Critical, {g} GL errors')
result('logs', not bad and nlogs > 0, f'{nlogs} logs, ' + ('0 Critical, 0 GL errors' if not bad else '; '.join(bad)))

# ------------------------------------------------------------------------- pictures for the owner
def sheet(kind, fn, sub):
    """One picture per row: 4:3 (1440x1080), 16:9 (1920x1080), 21:9 (2560x1080), 16:9 with AUTOFOV 0 (before),
    each 360 px high, centred, labelled."""
    from PIL import ImageDraw
    rows = []
    for suffix, label in (('1440x1080', '4:3 (1440x1080)'), ('1920x1080', '16:9 (1920x1080), AutoFOV'),
                          ('2560x1080', '21:9 (2560x1080), AutoFOV'), ('1920x1080-off', '16:9, AUTOFOV 0 (before)')):
        im = pic(sub, f'fov-{kind}-{suffix}')
        if im is not None:
            rows.append((im.resize((im.size[0] * 360 // im.size[1], 360)), label))
    if not rows:
        return None
    W = max(r[0].size[0] for r in rows)
    S = Image.new('RGB', (W, sum(r[0].size[1] + 24 for r in rows)), (40, 40, 40))
    d = ImageDraw.Draw(S); y = 0
    for im, label in rows:
        d.text((8, y + 6), label, fill=(255, 255, 255))
        S.paste(im, ((W - im.size[0]) // 2, y + 24)); y += im.size[1] + 24
    out = os.path.join(run, fn)
    S.save(out)
    return out


made = []
for kind, what in (('tp', 'third person'), ('fp', 'first person'), ('zoom', 'zoom 40'),
                   ('cin', 'letterbox'), ('lock', 'lock-on')):
    for sub in ('fov-pp', 'fov'):
        o = sheet(kind, f'owner-{kind}-{sub}.png', sub)
        if o:
            made.append(o)
print('pictures for the owner (rows 4:3, 16:9, 21:9, 16:9 before; fov-pp = with post-processing): '
      + ', '.join(os.path.relpath(m, run) for m in made))

print('ALL PASS' if fails == 0 else f'{fails} FAILED')
sys.exit(1 if fails else 0)
