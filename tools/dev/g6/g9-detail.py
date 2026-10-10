#!/usr/bin/env python3
"""G-9: split-screen detail, full (SplitScreenFullDetail=True) against the Xbox's (False), from two runs of
tools/dev/g6-splitscreen.sh's render part.

    g9-detail.py <full-run-dir> <xbox-run-dir> <out.png>

For every layout with player one at spot A (n2a n3 n4; n4d = player four there): the viewport at spot A against the
single-player picture of the full run (single player is the same in both) (g6-measure.py's comparison: the centre of the viewport, the single picture
scaled to it, no HUD), without and with post-processing, for both runs. Full detail must not be further from the
single player than the Xbox detail. The engine's "Split screen: N views, detail ..." lines say what each run set.
The sheet: per layout, the single picture's centre | full | Xbox | |full - Xbox| x4, so what the reductions removed
(coronas, real shadows, particles, decals, projectors) shows as the bright part of the last column.
"""
import json, os, re, sys
from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageStat

PRAXIS = len(sys.argv) > 1 and sys.argv[1] == '--praxis'
if PRAXIS:
    # g9-detail.py --praxis <out.png> <label>=<run dir> ...: the `detail` part on DM-Praxis. A label starting with
    # "full" expects SplitScreenFullDetail (player one's quadrant shows its coronas), "xbox" the Xbox's 4-player detail
    # (no corona anywhere); every run: no quadrant shows more corona than its single player at the same spot.
    dst = sys.argv[2]
    runs = [a.split('=', 1) for a in sys.argv[3:]]
    full_dir = xbox_dir = None
else:
    full_dir, xbox_dir, dst = sys.argv[1:4]


def praxis():
    def img(d, tag):
        p = os.path.join(d, f'g9-detail-{tag}.png')
        return Image.open(p).convert('RGB') if os.path.exists(p) else None

    def lit(on, off):
        """Pixels a corona lights: brighter with the client's coronas on than off (luminance, > 40)."""
        m = ImageChops.subtract(on.convert('L'), off.convert('L')).point(lambda v: 255 if v > 40 else 0)
        return m.histogram()[255], m

    fails = 0
    font = ImageFont.truetype(FONT, 16)
    tiles = []
    for label, d in runs:
        q, qnc = img(d, 'q'), img(d, 'qnc')
        if q is None or qnc is None:
            print(f'{label}: q/qnc missing'); fails += 1; continue
        W, H = q.size
        quads = [(0, 0), (W // 2, 0), (0, H // 2), (W // 2, H // 2)]
        line = []
        leak = []
        for k, (x, y) in enumerate(quads):
            box = (x, y, x + W // 2, y + H // 2)
            nq, _ = lit(q.crop(box), qnc.crop(box))
            s1, s1nc = img(d, f's{k + 1}'), img(d, f's{k + 1}nc')
            ns = lit(s1.resize((W // 2, H // 2), Image.LANCZOS), s1nc.resize((W // 2, H // 2), Image.LANCZOS))[0] if s1 and s1nc else -1
            line.append(f'P{k + 1}: quadrant {nq} px, single player {ns} px')
            leak.append((k, nq, ns))
        print(f'{label}: corona pixels ' + '; '.join(line))
        for k, nq, ns in leak:
            ok = nq <= ns * 1.5 + 60
            fails += not ok
            print(f"{'PASS' if ok else 'FAIL'} {label}-P{k + 1}-no-corona-from-another-view: {nq} px in the quadrant, {ns} px single player (limit 1.5 x + 60)")
        k, nq, ns = leak[0]
        if label.startswith('full'):
            ok = ns > 200 and nq > 0.5 * ns
            fails += not ok
            print(f"{'PASS' if ok else 'FAIL'} {label}-player-one-quadrant-shows-its-coronas: {nq} px, single player {ns} px")
        elif label.startswith('xbox'):
            ok = all(n < 60 for _, n, _ in leak)
            fails += not ok
            print(f"{'PASS' if ok else 'FAIL'} {label}-xbox-four-player-detail-has-no-coronas: {[n for _, n, _ in leak]} px")
        qd, sd = img(d, 'qd'), img(d, 'sd')
        if qd is not None and sd is not None:
            qq = qd.crop((0, 0, W // 2, H // 2))
            print(f'{label}: player one at PD after three shots: |quadrant - single player| {mad(qq, sd.resize(qq.size, Image.LANCZOS)):.2f}')
        tiles.append((f'{label}: 4 players, coronas on', q))
        tiles.append((f'{label}: coronas off', qnc))
        if qd is not None:
            tiles.append((f'{label}: P1 at PD after 3 shots', qd.crop((0, 0, W // 2, H // 2))))
        if sd is not None:
            tiles.append((f'{label}: 1 player at PD after 3 shots', sd))
    first = runs[0][1] if runs else None
    if first:
        for k in range(4):
            s1 = img(first, f's{k + 1}')
            if s1 is not None:
                tiles.append((f'1 player at P{k + 1} ({runs[0][0]})', s1))
    cw, ch = 480, 270
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (cw + 6), rows * (ch + 28)), (20, 20, 24))
    dr = ImageDraw.Draw(sheet)
    for k, (title, im) in enumerate(tiles):
        x, y = (k % cols) * (cw + 6), (k // cols) * (ch + 28)
        dr.text((x + 4, y + 4), title, fill=(240, 240, 240), font=font)
        sheet.paste(im.resize((cw, ch), Image.LANCZOS), (x, y + 24))
    sheet.save(dst)
    print(f'sheet {dst}')
    print(f"{'FAIL' if fails else 'PASS'} g9-detail-praxis: {fails} failed")
    return 1 if fails else 0
SPOT_A = {'n2a': 0, 'n3': 0, 'n4': 0, 'n4d': 3}
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'


def load(d, tag, kind=''):
    p = os.path.join(d, f'g6-render-{tag}{kind}.png')
    return Image.open(p).convert('RGB') if os.path.exists(p) else None


def rects(d, tag):
    v = json.load(open(os.path.join(d, f'g6-render-{tag}.json')))['V'][tag]
    return [tuple(r['rect']) for r in sorted([r for r in v if r['split'] >= 0], key=lambda r: r['split'])]


def mad(a, b):
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3.0


def centre(view, ww, wh):
    w, h = view.size
    cw = min(w, round(h * ww / wh))
    x0 = (w - cw) // 2
    return view.crop((x0, 0, x0 + cw, h))


def spot_view(d, tag, kind):
    # The single-player reference is the FULL run's for both runs: single player is the same in both (one view always
    # gets SSDetailLevels(0)), and a run's own single picture can miss spot A (the third-person camera frozen where it
    # was: seen once in the Xbox run, G-9 step 2; G-6 saw it for a freshly joined player, n2b).
    im, single = load(d, tag, kind), load(full_dir, 'n1', kind)
    if im is None or single is None:
        return None, None
    x, y, w, h = rects(d, tag)[SPOT_A[tag]]
    v = centre(im.crop((x, y, x + w, y + h)), *single.size)
    return v, single.resize(v.size, Image.LANCZOS)


def detail_lines(d):
    out = []
    p = os.path.join(d, 'g6-render.out')
    if os.path.exists(p):
        for line in open(p, encoding='latin-1'):
            m = re.search(r'Split screen: (\d+) views, detail (.*)', line)
            if m:
                out.append(m.group(1) + ': ' + m.group(2).strip())
    return out


if PRAXIS:
    sys.exit(praxis())

fails = 0
for name, d in (('full', full_dir), ('xbox', xbox_dir)):
    seen = []
    for l in detail_lines(d):
        if l not in seen:
            seen.append(l)
    print(f'{name}: ' + ' | '.join(seen))
font = ImageFont.truetype(FONT, 16)
rows = []
for tag in SPOT_A:
    res = {}
    for name, d in (('full', full_dir), ('xbox', xbox_dir)):
        for kind in ('-nohud-nopp', '-nohud'):
            v, s = spot_view(d, tag, kind)
            if v is not None:
                res[(name, kind)] = (mad(v, s), v, s)
    if len(res) < 4:
        print(f'{tag}: pictures missing')
        continue
    fn, xn = res[('full', '-nohud-nopp')][0], res[('xbox', '-nohud-nopp')][0]
    fp, xp = res[('full', '-nohud')][0], res[('xbox', '-nohud')][0]
    ok = fn <= xn + 0.5 and fp <= xp + 0.5
    fails += not ok
    print(f"{'PASS' if ok else 'FAIL'} {tag}-full-detail-no-further-from-single-player: |view at spot A - single|"
          f" without post-processing full {fn:.2f} / Xbox {xn:.2f}; with it full {fp:.2f} / Xbox {xp:.2f}")
    vf, sf = res[('full', '-nohud')][1], res[('full', '-nohud')][2]
    vx = res[('xbox', '-nohud')][1]
    diff = ImageChops.difference(vf, vx).point(lambda p: min(255, p * 4))
    rows.append((tag, [sf, vf, vx, diff], (fp, xp)))

if rows:
    cw = 320
    tiles = []
    for tag, ims, (fp, xp) in rows:
        ims = [im.resize((cw, round(im.size[1] * cw / im.size[0])), Image.LANCZOS) for im in ims]
        tiles.append((tag, ims, fp, xp))
    th = max(t[1][0].size[1] for t in tiles)
    cap = 24
    sheet = Image.new('RGB', (4 * cw + 3 * 6, len(tiles) * (th + cap + 6) + cap), (20, 20, 24))
    dr = ImageDraw.Draw(sheet)
    for k, head in enumerate(['single player (reference)', 'full detail (new default)', 'Xbox detail (False)', '|full - Xbox| x4']):
        dr.text((k * (cw + 6) + 4, 3), head, fill=(240, 240, 240), font=font)
    for r, (tag, ims, fp, xp) in enumerate(tiles):
        y = cap + r * (th + cap + 6)
        dr.text((4, y + 3), f'{tag}: view at spot A; |diff to single| full {fp:.2f}, Xbox {xp:.2f}', fill=(120, 255, 160), font=font)
        for k, im in enumerate(ims):
            sheet.paste(im, (k * (cw + 6), y + cap))
    sheet.save(dst)
    print(f'sheet {dst}')
print(f"{'FAIL' if fails else 'PASS'} g9-detail: {fails} failed")
sys.exit(1 if fails else 0)
