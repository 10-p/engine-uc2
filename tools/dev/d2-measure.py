#!/usr/bin/env python3
"""D-2 (UI scaling) measurements on the pictures tools/dev/d2-ui-scale.sh takes. One PASS/FAIL line per check.

    python3 tools/dev/d2-measure.py <WorkUC2>/logs/d2/run 640x480 1280x720 ...

Pictures per resolution (<run>/<WxH>/): menu-<shot>.png (tools/dev/d2/menu.scen: main, main-box,
main-box-levelOff, main-levelOff and the same for arena) and hud-<shot>.png (tools/dev/d2/deadbolt.scen: hud,
hud-levelOff). "levelOff" = TOGGLERENDERLEVEL: the UI on a black frame. The box of a W x H window is the one
UCanvas::ApplyUIBox computes: s = min(W/640, H/480), corner (floor((W-640s)/2), floor((H-480s)/2)).

Only PIL (no numpy on this machine). Also writes <run>/contact-sheet.png (one row per resolution, the owner's
pictures) and <run>/before-after-1920x1080.png.
"""
import sys, os, re, math, glob, subprocess
from PIL import Image, ImageChops, ImageStat, ImageDraw

run = sys.argv[1]
resl = sys.argv[2:] or ['640x480']
here = os.path.dirname(os.path.abspath(__file__))
fails = 0


def result(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'} {name}: {detail}", flush=True)


def wh(res):
    w, h = res.split('x')
    return int(w), int(h)


def box(W, H):
    s = min(W / 640.0, H / 480.0)
    return s, math.floor((W - 640 * s) * 0.5), math.floor((H - 480 * s) * 0.5)


def pic(res, name):
    p = os.path.join(run, res, name + '.png')
    return Image.open(p).convert('RGB') if os.path.exists(p) else None


def maxch(im):
    r, g, b = im.split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b)


def bbox(im, t=8):
    return maxch(im).point(lambda v: 255 if v > t else 0).getbbox()


def mad(a, b):
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3.0


def same_pct(a, b):
    h = maxch(ImageChops.difference(a, b)).histogram()
    return 100.0 * h[0] / sum(h), max(i for i, c in enumerate(h) if c)


def design_crop(im, res, x0, y0, x1, y1, dx=0, dy=0):
    """The design rect (x0,y0)-(x1,y1) of a W x H picture, shifted by (dx,dy) window pixels, back to design size."""
    W, H = wh(res)
    s, bx, by = box(W, H)
    c = im.crop((round(bx + x0 * s + dx), round(by + y0 * s + dy), round(bx + x1 * s + dx), round(by + y1 * s + dy)))
    return c.resize((x1 - x0, y1 - y0), Image.BOX if s > 1 else Image.NEAREST)


def align(im, res, ref, x0, y0, x1, y1, r=3):
    """Best (dx,dy) in window pixels (|d| <= r) for the design rect against the 640x480 picture, and its MAD."""
    tmpl = ref.crop((x0, y0, x1, y1))
    best = None
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            m = mad(design_crop(im, res, x0, y0, x1, y1, dx, dy), tmpl)
            if best is None or m < best[0]:
                best = (m, dx, dy)
    return best


base = {}
for name in ('menu-main-box-levelOff', 'menu-arena-box-levelOff', 'hud-hud-levelOff'):
    base[name] = pic('640x480', name)

# ------------------------------------------------------------------------------------- per resolution
TITLE = (225, 38, 600, 72)       # "UNREAL CHAMPIONSHIP 2" (main menu)
ITEM = (55, 170, 300, 200)       # "INSTANT ACTION" (main menu)
for res in resl:
    W, H = wh(res)
    s, bx, by = box(W, H)
    want = (bx, by, bx + round(640 * s), by + round(480 * s))
    for page in ('main', 'arena'):
        im = pic(res, f'menu-{page}-box-levelOff')
        if im is None:
            result(f'{page}-box-{res}', False, 'no picture')
            continue
        bb = bbox(im)
        ok = bb is not None and all(abs(a - b) <= 2 for a, b in zip(bb, want))
        result(f'{page}-box-{res}', ok, f'UI bounding box {bb}, computed box {want} (s={s:g})')
    im = pic(res, 'menu-main-box-levelOff')
    ref = base['menu-main-box-levelOff']
    if im is not None and ref is not None:
        m, dx, dy = align(im, res, ref, 0, 0, 640, 480, r=2)
        result(f'menu-scale-{res}', m <= 6 and abs(dx) <= 1 and abs(dy) <= 1,
               f'the box scaled back to 640x480 vs the 640x480 picture: mean difference {m:.2f} levels, best offset ({dx},{dy}) px')
        for tag, rect in (('title', TITLE), ('item', ITEM)):
            m, dx, dy = align(im, res, ref, *rect, r=6)
            W0, H0 = rect[0] * s + bx, rect[1] * s + by
            result(f'menu-{tag}-{res}', abs(dx) <= 2 and abs(dy) <= 2 and m <= 12,
                   f'design ({rect[0]},{rect[1]}) expected at ({W0:.1f},{H0:.1f}), found at offset ({dx},{dy}) px, mean difference {m:.2f}')
    fill, boxed = pic(res, 'menu-main-levelOff'), pic(res, 'menu-main-box-levelOff')
    if (W, H) != (640, 480) and fill is not None and boxed is not None and (bx > 0 or by > 0):
        # the area outside the box: left/right bars, or top/bottom bars
        regions = [(0, 0, bx, H), (W - bx, 0, W, H)] if bx > 0 else [(0, 0, W, by), (0, H - by, W, H)]
        fm = sum(ImageStat.Stat(maxch(fill.crop(r))).mean[0] for r in regions) / 2
        bm = sum(ImageStat.Stat(maxch(boxed.crop(r))).mean[0] for r in regions) / 2
        result(f'menu-fill-{res}', fm >= 3 and bm <= 1,
               f'outside the box, mean level: Fill {fm:.1f} (the page background drawn there), Box {bm:.2f} (black)')
    hud = pic(res, 'hud-hud-levelOff')
    href = base['hud-hud-levelOff']
    if hud is None or href is None:
        result(f'hud-box-{res}', False, 'no picture')
        continue
    # threshold 24: the level-off frame keeps a faint ghost of lit things (post-processing's lagged back-buffer
    # copy; a glowing pickup reached 9-20 levels at 2560x1440); the HUD's own pixels are far above it
    bb, b0 = bbox(hud, 24), bbox(href, 24)
    exp = (bx + b0[0] * s, by + b0[1] * s, bx + b0[2] * s, by + b0[3] * s)
    tol = max(2, math.ceil(s))
    inside = bb[0] >= want[0] - 1 and bb[1] >= want[1] - 1 and bb[2] <= want[2] + 1 and bb[3] <= want[3] + 1
    ok = inside and all(abs(a - b) <= tol for a, b in zip(bb, exp))
    result(f'hud-box-{res}', ok, f'HUD bounding box {bb}, the 640x480 one {b0} scaled = ({exp[0]:.1f},{exp[1]:.1f},{exp[2]:.1f},{exp[3]:.1f}) '
           f'(tolerance {tol}), inside the box {want}: {inside}')
    m, dx, dy = align(hud, res, href, 0, 0, 640, 480, r=2)
    result(f'hud-scale-{res}', m <= 3 and abs(dx) <= 1 and abs(dy) <= 1,
           f'the box scaled back to 640x480 vs the 640x480 HUD: mean difference {m:.2f}, best offset ({dx},{dy}) px')
    # the reticle: the drawn shape in a window around the centre. The crosshair's parts are laid out around
    # x=62.5 of a 128-wide element anchored at its centre (Crosshair_NakhtiRW: 28/96, 23.5/101.5), so the art
    # is off-centre by itself and the 640x480 picture has it too; the check is that the reticle is where the
    # 640x480 one maps (+-max(2, s/2+1) px) and the literal distance to the screen centre is reported.
    def reticle(img, Wi, Hi, sc):
        rr = 40 * sc
        x0, y0 = round(Wi / 2 - rr), round(Hi / 2 - rr)
        rb = bbox(img.crop((x0, y0, round(Wi / 2 + rr), round(Hi / 2 + rr))), 24)
        return None if rb is None else (x0 + (rb[0] + rb[2]) / 2.0, y0 + (rb[1] + rb[3]) / 2.0, rb[2] - rb[0], rb[3] - rb[1])
    rt, r0 = reticle(hud, W, H, s), reticle(href, 640, 480, 1.0)
    if rt is None or r0 is None:
        result(f'reticle-{res}', False, 'no reticle near the centre')
    else:
        cx, cy, rw, rh = rt
        ex, ey = bx + s * r0[0], by + s * r0[1]
        tol = max(2.0, s / 2 + 1)
        lit = abs(cx - W / 2) <= 2 and abs(cy - H / 2) <= 2
        result(f'reticle-{res}', abs(cx - ex) <= tol and abs(cy - ey) <= tol,
               f'reticle centre ({cx:.1f},{cy:.1f}) size {rw}x{rh}; the 640x480 one ({r0[0]:.1f},{r0[1]:.1f}) maps to ({ex:.1f},{ey:.1f}) '
               f'(tolerance {tol:.1f}); from the screen centre ({W/2:.0f},{H/2:.0f}): ({cx-W/2:+.1f},{cy-H/2:+.1f}) px, '
               f'{"within" if lit else "NOT within"} 2 px')

# ------------------------------------------------------------------------------------- 640x480 = before
def identity(name, a, b, masks, why, base_a, base_b, levels=0):
    """a vs b (640x480): every pixel outside the masks identical (within `levels`); the masks are the parts
    that animate on their own (measured: two runs of one binary differ there and only there)."""
    A, B = pic('640x480', a), pic('640x480', b)
    if A is None or B is None:
        result(name, False, f'missing {a if A is None else b}')
        return
    def outside(X, Y):
        m = maxch(ImageChops.difference(X, Y)).point(lambda v: 255 if v > levels else 0)
        for r in masks:
            m.paste(0, r)
        h = m.histogram()
        n = 640 * 480 - sum((r[2] - r[0]) * (r[3] - r[1]) for r in masks)
        return h[255], n, m.getbbox()
    bad, n, bb = outside(A, B)
    detail = (f'{a} vs {b}: {n - bad} of {n} pixels outside {why} {"identical" if levels == 0 else f"within {levels} levels"}'
              f' ({100.0 * (n - bad) / n:.3f}%){"" if bb is None else f", differences in {bb}"}')
    BA, BB = pic('640x480', base_a), pic('640x480', base_b)
    if BA is not None and BB is not None:
        full = maxch(ImageChops.difference(BA, BB)).point(lambda v: 255 if v > levels else 0).getbbox()
        detail += f'; two runs of the reference binary ({base_a} vs {base_b}) differ in {full}'
    result(name, bad == 0, detail)

identity('identity-main-640x480', 'menu-main-box-levelOff', 'ref-menu-main-box-levelOff', [(55, 125, 300, 165)],
         'the focused item (its highlight pulses)', 'ref-menu-main-box-levelOff', 'ref3-menu-main-box-levelOff')
identity('identity-arena-640x480', 'menu-arena-box-levelOff', 'ref-menu-arena-box-levelOff', [(400, 80, 600, 298)],
         'the game type preview (a movie)', 'ref-menu-arena-box-levelOff', 'ref3-menu-arena-box-levelOff')
identity('identity-hud-640x480', 'hud-hud-levelOff', 'ref-hud-hud-levelOff', [(280, 210, 360, 265), (55, 45, 75, 58)],
         'the reticle and the adrenaline meter\'s end (both animate)', 'ref-hud-hud-levelOff', 'ref2-hud-hud-levelOff', levels=2)
for a, b, tag in (('hud-hud', 'main-hud-hud', 'pinned-hud-640x480'),
                  ('pinned-hud', 'main-pinned-hud', 'pinned-nopostprocess-640x480')):
    pa, pb = os.path.join(run, '640x480', a + '.png'), os.path.join(run, '640x480', b + '.png')
    if os.path.exists(pa) and os.path.exists(pb):
        o = subprocess.run([sys.executable, os.path.join(here, 'pinned-compare.py'), pb, pa,
                            os.path.join(run, '640x480', tag + '-diff.png')], capture_output=True, text=True).stdout.strip()
        f = o.split()
        ok = len(f) == 4 and f[0] == 'OK' and float(f[1]) >= 99.9
        result(tag, ok, f'pinned-compare.py {b} (main\'s binary) vs {a}: "{o}" (static part % within 2 levels >= 99.9)')
    else:
        result(tag, False, 'missing pictures')

# ------------------------------------------------------------------------------------- the switch
A, B = pic('1920x1080', 'native-menu-main-box-levelOff'), pic('1920x1080', 'ref-menu-main-box-levelOff')
if A is not None and B is not None:
    pct, mx = same_pct(A, B)
    ba, bb_ = bbox(A), bbox(B)
    result('switch-native640-1920x1080', ba == bb_ and pct >= 90.0,
           f'UIScaleMode=Native640 vs main\'s binary, main menu on black: {pct:.3f}% identical (largest {mx}), '
           f'UI bounding box {ba} vs {bb_}')
else:
    result('switch-native640-1920x1080', False, 'missing pictures')

# ------------------------------------------------------------------------------------- a marker on an actor
def lock_marker(im):
    """The lock-on reticle (Element_TargetReticle, orange on an enemy; placed at WorldToScreen of the target) and
    the target's figure (the summoned Liandri's purple) inside it: their bounding boxes and the offset of the
    reticle's centre from the figure's centre. The search keeps to y >= 300 (the HUD's top bars, the poster)."""
    W, H = im.size
    def m3(fr, fg, fb):
        r, g, b = im.split()
        return ImageChops.multiply(ImageChops.multiply(r.point(fr), g.point(fg)), b.point(fb))
    o = m3(lambda v: 255 if v > 180 else 0, lambda v: 255 if 90 < v < 190 else 0, lambda v: 255 if v < 70 else 0)
    o.paste(0, (0, 0, W, 300))
    ob = o.getbbox()
    if ob is None or o.histogram()[255] < 100:
        return None
    p = m3(lambda v: 255 if v > 120 else 0, lambda v: 255 if v < 100 else 0, lambda v: 255 if v > 140 else 0).crop(ob)
    pb = p.getbbox()
    if pb is None:
        return None
    pb = (ob[0] + pb[0], ob[1] + pb[1], ob[0] + pb[2], ob[1] + pb[3])
    oc = ((ob[0] + ob[2]) / 2, (ob[1] + ob[3]) / 2); pc = ((pb[0] + pb[2]) / 2, (pb[1] + pb[3]) / 2)
    return ob, pb, (oc[0] - pc[0], oc[1] - pc[1])

shots = [(n, pic('1920x1080', 'lockon-lock-' + n)) for n in ('fit', 'native', 'fit2')]
if all(im is not None for _, im in shots):
    ms = [(n, lock_marker(im)) for n, im in shots]
    if all(m is not None for _, m in ms):
        offs = [m[2] for _, m in ms]
        whole = all(m[0][1] > 300 and m[0][3] < 1080 and m[0][0] > 0 and m[0][2] < 1920 for _, m in ms)
        spread = max(max(o[0] for o in offs) - min(o[0] for o in offs), max(o[1] for o in offs) - min(o[1] for o in offs))
        same = max(abs(a - b) for (_, m1), (_, m2) in zip(ms, ms[1:]) for a, b in zip(m1[0], m2[0]))
        result('marker-on-actor-1920x1080', spread <= 8 and whole and same <= 2,
               'lock-on reticle (WorldToScreen) relative to the target\'s figure: ' +
               '; '.join(f'{n} reticle {m[0]} figure {m[1]} offset ({m[2][0]:+.1f},{m[2][1]:+.1f})' for n, m in ms) +
               f'; the reticle\'s box moves {same} px between the pictures (<= 2) and its offset from the figure spreads '
               f'{spread:.1f} px (<= 8: the figure idles); boxed, unboxed (window pixels, as before D-2), boxed again'
               + ('' if whole else '; the reticle is cut by the window or the y>=300 search'))
    else:
        result('marker-on-actor-1920x1080', False, 'no lock-on reticle / target found: ' + ', '.join(n for n, m in ms if m is None))
else:
    result('marker-on-actor-1920x1080', False, 'missing pictures (lockon-lock-fit, -native, -fit2)')

# ------------------------------------------------------------------------------------- mouse, logs
for res in ('640x480', '1920x1080'):
    p = os.path.join(run, res, 'mouse.result')
    if os.path.exists(p):
        rows = [l.split('\t') for l in open(p) if l.strip()]
        np_ = sum(1 for r in rows if r[0] == 'PASS'); nf = sum(1 for r in rows if r[0] != 'PASS')
        result(f'mouse-{res}', nf == 0 and np_ > 0, f'M10 mouse scenario (positions mapped into the box): {np_} passed, {nf} failed')
    else:
        result(f'mouse-{res}', False, 'no mouse.result')

crit = gl = 0; nlogs = 0; layers = set()
for res in resl:
    for f in glob.glob(os.path.join(run, res, '*.out')):
        if os.path.basename(f).startswith(('ref', 'main-')):
            continue
        nlogs += 1
        t = open(f, 'rb').read().decode('latin-1')
        crit += t.count('Critical')
        for m in re.finditer(r'UC2GLDrv: (\d+) GL errors in', t):
            gl += int(m.group(1))
        for m in re.finditer(r'UIScale: full-screen tile (.*)', t):
            layers.add(m.group(1).strip())
result('logs', crit == 0 and gl == 0 and nlogs > 0, f'{nlogs} game logs of the build under test: {crit} Critical, {gl} GL errors')
for l in sorted(layers):
    print(f'      full-screen layer seen: {l}')

# ------------------------------------------------------------------------------------- pictures for the owner
def label(im, text):
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 8 + 6 * len(text), 14), fill=(0, 0, 0))
    d.text((4, 2), text, fill=(255, 255, 0))
    return im


def thumb(res, name, h=270):
    im = pic(res, name)
    if im is None:
        return None
    return label(im.resize((max(1, round(im.width * h / im.height)), h), Image.LANCZOS), f'{res} {name}')

rows = []
for res in resl:
    ims = [t for t in (thumb(res, 'menu-main'), thumb(res, 'menu-arena'), thumb(res, 'hud-hud')) if t]
    if ims:
        rows.append(ims)
if rows:
    gap = 6
    width = max(sum(i.width for i in r) + gap * (len(r) - 1) for r in rows)
    sheet = Image.new('RGB', (width, sum(r[0].height for r in rows) + gap * (len(rows) - 1)), (40, 40, 40))
    y = 0
    for r in rows:
        x = 0
        for i in r:
            sheet.paste(i, (x, y)); x += i.width + gap
        y += r[0].height + gap
    sheet.save(os.path.join(run, 'contact-sheet.png'))
    print(f'      contact sheet: {os.path.join(run, "contact-sheet.png")} ({sheet.width}x{sheet.height})')
ba = [t for t in (thumb('1920x1080', 'ref-menu-main', 405), thumb('1920x1080', 'menu-main', 405),
                  thumb('1920x1080', 'ref-hud-hud', 405), thumb('1920x1080', 'hud-hud', 405),
                  thumb('1920x1080', 'menu-arena-box', 405), thumb('1920x1080', 'menu-arena', 405)) if t]
if ba:
    sheet = Image.new('RGB', (2 * ba[0].width + 6, ((len(ba) + 1) // 2) * (ba[0].height + 6)), (40, 40, 40))
    for i, t in enumerate(ba):
        sheet.paste(t, ((i % 2) * (ba[0].width + 6), (i // 2) * (ba[0].height + 6)))
    sheet.save(os.path.join(run, 'before-after-1920x1080.png'))
    print(f'      before/after (left: main\'s binary / Box; right: this build / Fill): {os.path.join(run, "before-after-1920x1080.png")}')

ig = [t for t in (thumb('1920x1080', 'pause-scoreboard', 405), thumb('1920x1080', 'pause-pause', 405),
                  thumb('1920x1080', 'lockon-lock-native', 405), thumb('1920x1080', 'lockon-lock-fit', 405)) if t]
if ig:
    sheet = Image.new('RGB', (2 * ig[0].width + 6, ((len(ig) + 1) // 2) * (ig[0].height + 6)), (40, 40, 40))
    for i, t in enumerate(ig):
        sheet.paste(t, ((i % 2) * (ig[0].width + 6), (i // 2) * (ig[0].height + 6)))
    sheet.save(os.path.join(run, 'ingame-1920x1080.png'))
    print(f'      in game (scoreboard, pause menu; lock-on unboxed / boxed): {os.path.join(run, "ingame-1920x1080.png")}')

print(f'{"ALL PASS" if fails == 0 else f"{fails} FAILED"}')
