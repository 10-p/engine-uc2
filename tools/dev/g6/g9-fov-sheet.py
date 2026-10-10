#!/usr/bin/env python3
"""G-9: the split-screen FOV A/B sheets from tools/dev/g6-splitscreen.sh's `fov` part.

    g9-fov-sheet.py <out-dir>

Reads, for the runs g9-fov (default ini) and g9-fovclamp ([ufront.AutoFOV] SplitClampToFullScreen=True):
  <run>.out               the engine log: "test: log g9fov <tag>" markers and the "[AutoFOV] shown viewport=<i>
                          rect=X,Y WxH mode=M base=B effective=E scaleFOV=S horizontal=H vertical=V" lines
  <run>-<tag>.png/.json   the picture and the viewports (vprobe) of each tag n<k>-on / n<k>-epic
Writes <out>/fov-<k>players.png (k = 2, 3, 4): four panels - the single-player reference (AutoFOV on), AutoFOV on
(today's default), Epic's maths (AutoFOV=0), and the proposal (Hor+ clamped to the full-screen FOV) - with every
viewport's horizontal x vertical FOV written in it, read from the engine's line in force when the shot was taken.
Prints the table and compares every logged value with the FOV computed here from the rectangle alone (Hor+ maths of
D-8, ufrontSplitFovScale of G-6, Epic's ReduceFOV of SplitScreenMgr.uc): a check line per variant.
"""
import json, math, os, re, sys
from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1]
SHOWN = re.compile(r'\[AutoFOV\] shown viewport=(\d+) rect=(-?\d+),(-?\d+) (\d+)x(\d+) mode=(\d) base=([-\d.]+) '
                   r'effective=([-\d.]+) scaleFOV=([-\d.]+) horizontal=([-\d.]+) vertical=([-\d.]+)')
MARK = re.compile(r'test: log g9fov (\S+)')
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
REF = 4.0 / 3.0
REDUCE = {1: 1.0, 2: 1.0, 3: 0.5, 4: 0.5}   # SplitScreenMgr.uc SSDetailLevels(n-1).ReduceFOV


def snapshots(run):
    """tag -> {rect: (horizontal, vertical, effective, scaleFOV, mode)} as in force at the tag's marker."""
    snaps, cur = {}, {}
    path = os.path.join(out, run + '.out')
    if not os.path.exists(path):
        return snaps
    for line in open(path, encoding='latin-1'):
        m = SHOWN.search(line)
        if m:
            g = m.groups()
            cur[int(g[0])] = (tuple(int(v) for v in g[1:5]), float(g[9]), float(g[10]), float(g[7]), float(g[8]), int(g[5]))
            continue
        m = MARK.search(line)
        if m:
            snaps[m.group(1)] = {v[0]: v[1:] for v in cur.values()}
    return snaps


def views(run, tag):
    p = os.path.join(out, f'{run}-{tag}.json')
    if not os.path.exists(p):
        return []
    j = json.load(open(p))
    rows = j['V'][tag]
    split = sorted([r for r in rows if r['split'] >= 0], key=lambda r: r['split'])
    return [tuple(r['rect']) for r in split] or [tuple(r['rect']) for r in rows if r['scene'] == 1][:1]


def horplus(base, w, h):
    k = (w / h) / REF
    if abs(k - 1) < 1e-4 or k < 1:          # Adaptive: never narrower than the base
        return base
    return math.degrees(2 * math.atan(math.tan(math.radians(base / 2)) * k))


def predicted(variant, n, rect, W, H, base=90.0):
    """(horizontal, vertical) a viewport shows, from its rectangle alone."""
    x, y, w, h = rect
    sx = w / W
    if variant == 'epic':
        half = math.radians(base / 2) * (REDUCE[n] if n > 1 else 1.0)
        return (math.degrees(2 * math.atan(math.tan(half) / sx)), math.degrees(2 * math.atan(math.tan(half) * h / (w * sx))))
    wc = w
    if variant == 'clamp' and n > 1 and w / h > W / H + 1e-6:
        wc = round(h * W / H)
    eff = horplus(base, wc, h)
    return eff, math.degrees(2 * math.atan(math.tan(math.radians(eff / 2)) * h / w))


def panel(run, tag, title, scale, n, variant, snaps, table, font, small):
    p = os.path.join(out, f'{run}-{tag}.png')
    if not os.path.exists(p):
        im = Image.new('RGB', (1280, 720), (40, 0, 0))
        ImageDraw.Draw(im).text((20, 20), f'missing {run}-{tag}.png', fill=(255, 255, 255), font=font)
    else:
        im = Image.open(p).convert('RGB')
    W, H = im.size
    sw, sh = int(W * scale), int(H * scale)
    im = im.resize((sw, sh), Image.LANCZOS)
    cap = 40
    pan = Image.new('RGB', (sw, sh + cap), (24, 24, 28))
    pan.paste(im, (0, cap))
    d = ImageDraw.Draw(pan)
    d.text((10, 8), title, fill=(255, 230, 120) if 'Proposal' in title else (240, 240, 240), font=font)
    snap = snaps.get(tag, {})
    worst = 0.0
    for i, r in enumerate(views(run, tag)):
        got = snap.get(r)
        pr = predicted(variant, n, r, W, H)
        if got:
            hz, vt = got[0], got[1]
            worst = max(worst, abs(hz - pr[0]), abs(vt - pr[1]))
            label = f'{hz:.1f}° across  x  {vt:.1f}° up'
        else:
            hz = vt = None
            worst = float('inf')
            label = 'no [AutoFOV] shown line'
        table.append((n, variant, i, r, hz, vt, pr))
        x, y, w, h = r
        tx, ty = int(x * scale) + 8, cap + int(y * scale) + int(h * scale) - 30
        bb = d.textbbox((tx, ty), label, font=small)
        d.rectangle((bb[0] - 5, bb[1] - 4, bb[2] + 5, bb[3] + 4), fill=(0, 0, 0))
        d.text((tx, ty), label, fill=(120, 255, 160), font=small)
        d.rectangle((int(x * scale), cap + int(y * scale), int((x + w) * scale) - 1, cap + int((y + h) * scale) - 1), outline=(90, 90, 90))
    return pan, worst


def main():
    font = ImageFont.truetype(FONT, 20)
    small = ImageFont.truetype(FONT, 17)
    snaps = {'g9-fov': snapshots('g9-fov'), 'g9-fovclamp': snapshots('g9-fovclamp')}
    fails = 0
    rows = []
    for n in (2, 3, 4):
        table = []
        panels = [
            panel('g9-fov', 'n1-on', '1 player (reference), AutoFOV on', 0.5, 1, 'on', snaps['g9-fov'], table, font, small),
            panel('g9-fov', f'n{n}-on', f'{n} players: AutoFOV on, unclamped (Hor+)', 0.5, n, 'on', snaps['g9-fov'], table, font, small),
            panel('g9-fov', f'n{n}-epic', f'{n} players: Epic\'s maths (AutoFOV=0)', 0.5, n, 'epic', snaps['g9-fov'], table, font, small),
            panel('g9-fovclamp', f'n{n}-on', f'{n} players: DEFAULT, clamped to 1 player', 0.5, n, 'clamp', snaps['g9-fovclamp'], table, font, small),
        ]
        pw, ph = panels[0][0].size
        gap = 8
        sheet = Image.new('RGB', (2 * pw + gap, 2 * ph + gap), (0, 0, 0))
        for k, (pan, worst) in enumerate(panels):
            sheet.paste(pan, ((k % 2) * (pw + gap), (k // 2) * (ph + gap)))
        dst = os.path.join(out, f'fov-{n}players.png')
        sheet.save(dst)
        print(f'sheet {dst}')
        for (pan, worst), name in zip(panels, ('reference', 'on', 'epic', 'clamp')):
            ok = worst < 0.05
            fails += not ok
            print(f"{'PASS' if ok else 'FAIL'} g9-fov-{n}players-{name}-logged-equals-computed: worst |logged - computed| {worst:.4f} degrees")
        rows += table
    print()
    print('players variant   view  rect                 horizontal  vertical   (computed from the rectangle)')
    seen = set()
    for n, variant, i, r, hz, vt, pr in rows:
        key = (n if variant != 'on' or r != (0, 0, 1280, 720) else 1, variant, i, r)
        if key in seen:
            continue
        seen.add(key)
        hs = f'{hz:8.2f}' if hz is not None else '     n/a'
        vs = f'{vt:8.2f}' if vt is not None else '     n/a'
        print(f'{key[0]:>7} {variant:<9} {i:>4}  {str(r):<20} {hs}   {vs}   ({pr[0]:.2f} x {pr[1]:.2f})')
    print(f"{'FAIL' if fails else 'PASS'} g9-fov-sheet: {fails} failed")
    return 1 if fails else 0


sys.exit(main())
