#!/usr/bin/env python3
"""X-1 (examination): candidate 3P / 4P cursor tags for the character page, made from the game's own 1P tag.

    tools/dev/make-player-tags.py --src <dir> --out <dir> [--page <png> --rect X,Y,SIZE]

<dir> holds the shipped cursors as the game's tool exports them (from YOUR T_UI_2.utx, nothing of Epic's art is in the
repository):
    cd <run tree>/System && ./ucc-bin batchexport T_UI_2.utx Texture dds <dir>
    -> <dir>/UI_Char_1PSelector.dds, UI_Char_2PSelector.dds   (64x64 DXT3, one mip)

What the script does (docs/porting/analysis/x1-players34.md, "The artwork"):
  1. the "1P" label of the 1P tag is cleaned: in each label row the tag's own fill is interpolated across the glyph
     pixels, up to the tag's dark diagonal edge, which is kept;
  2. 3P = the cleaned 1P flipped top-to-bottom about the hexagon's own centre row (y' = 64 - y: the outline lands on
     the 1P outline's rows exactly, so the four cursors stack on one portrait); its tag is now bottom-left;
     4P = 3P flipped left-to-right as Epic flipped 1P into 2P (x' = 63 - x: the outline lands on 2P's columns);
  3. the label is written upright in the flipped tag: a 3-level pixel font transcribed from the "1" and "P" of the 1P
     tag (dark core, two anti-alias levels, applied as a multiplier on the fill, so the fill's gradient and hue stay),
     with new "3" and "4" in the same strokes; placed on the rows mirrored from the 1P label, as close to the tag's
     outer corner as the edge allows with one pixel to spare (the tag narrows towards the hexagon's middle);
  4. the whole cursor is hue-rotated (HSV, saturation and value kept) to each candidate colour.
Writes <out>/UI_Char_{3P,4P}Selector-<colour>.tga (64x64 RGBA, run-length coded, what a patch set would import into
UCMenu.u), <out>/player-tags-sheet.png (the owner's sheet) and <out>/player-tags-report.txt (the numeric checks).
With --page (a 1280x720 shot of the character page) and --rect (the cursor's rectangle in that shot) the sheet also
shows all four cursors on a portrait of the real page: a mock-up composited here, NOT the engine's rendering.
"""
import argparse
import colorsys
import os
from PIL import Image, ImageDraw, ImageFont

FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

# The label font: rows y9..y16 of the 1P tag (8 rows). '#' dark core, '+' and '-' the two anti-alias levels measured
# on the 1P label (luminance / fill: core 0.03, '+' 0.35, '-' 0.65), ' ' the fill. "1" and "P" transcribed from
# Epic's 1P (DXT block noise rounded to the three levels); "2" from 2P's shape; "3" and "4" drawn in the same strokes.
LEVEL = {'#': 0.03, '+': 0.35, '-': 0.65, ' ': 1.0, '.': 1.0}
GLYPHS = {
    '1': [' ----',
          ' +##+',
          ' --#+',
          '   #+',
          '   #+',
          '   #+',
          '   #+',
          '   --'],
    '3': [' --- ',
          '-###+',
          '   #+',
          '   #+',
          ' -##+',
          '  -#+',
          '-###+',
          ' ----'],
    '4': ['-  - ',
          '#+ #+',
          '#+ #+',
          '#+ #+',
          '####+',
          '---#+',
          '   #+',
          '   --'],
    'P': [' ------- ',
          '-#######+',
          '-#-    +#',
          '-#+----##',
          '-#######-',
          '-#+------',
          '-#+      ',
          ' --      '],
}
LABEL_ROWS = (9, 16)          # the 1P label's rows (anti-alias rows included)
TAG_W = 20                    # the tag lies in x 0..19 (1P)
CANDIDATES = [                # name, hue in degrees (1P is about 52, 2P about 190)
    ('green', 120.0), ('red', 0.0), ('magenta', 300.0), ('orange', 28.0), ('violet', 270.0),
]
PAIRS = [('green', 'magenta'), ('red', 'green'), ('magenta', 'orange')]


def lum(p):
    return (p[0] + p[1] + p[2]) / 3.0


def load(path):
    return Image.open(path).convert('RGBA')


def edge_start(px, y, xs):
    """The first column of the tag's dark diagonal edge in row y, scanning xs (from the open side inwards)."""
    run = None
    for x in xs:
        a = px[x, y][3]
        if run is None and a < 128:
            continue
        if run is None:
            run = x
            continue
        break
    # run = the first opaque pixel from the open side: the edge's dark pixel; extend while dark
    if run is None:
        return None
    fill = max(lum(px[x, y]) for x in xs if px[x, y][3] >= 200)
    x = run
    step = 1 if xs[1] > xs[0] else -1
    while 0 <= x + step < 64 and lum(px[x + step, y]) < 0.6 * fill:
        x += step
    return x  # the inner-most edge pixel


def clean_label(img, log):
    """1P: interpolate the tag's fill over the label's pixels, row by row, up to the dark edge."""
    out = img.copy()
    px = img.load()
    po = out.load()
    for y in range(LABEL_ROWS[0], LABEL_ROWS[1] + 1):
        e = edge_start(px, y, list(range(TAG_W - 1, -1, -1)))  # edge seen from the right (the open side)
        inner = e - 1                                               # last interior column
        fills = [lum(px[x, y]) for x in range(0, inner + 1) if px[x, y][3] >= 200]
        ref = sorted(fills)[len(fills) * 3 // 4]
        glyph = [x for x in range(0, inner + 1) if lum(px[x, y]) < 0.9 * ref]
        if not glyph:
            continue
        good = [x for x in range(0, inner + 1) if x not in glyph]
        for x in glyph:
            left = [g for g in good if g < x]
            right = [g for g in good if g > x]
            if left and right:
                l, r = left[-1], right[0]
                t = (x - l) / float(r - l)
                c = tuple(int(round(px[l, y][k] * (1 - t) + px[r, y][k] * t)) for k in range(4))
            else:
                c = px[(left or right)[-1 if left else 0], y]
            po[x, y] = c
        log.append(f'  clean row {y}: interior x0..{inner} (edge from x{e}), {len(glyph)} label pixels replaced')
    return out


def flip(img, vertical, horizontal):
    """y' = 64 - y (about the hexagon's centre row 32), x' = 63 - x (Epic's 1P -> 2P mirror)."""
    src = img.load()
    out = Image.new('RGBA', img.size, (0, 0, 0, 0))
    po = out.load()
    for y in range(64):
        for x in range(64):
            sy = 64 - y if vertical else y
            sx = 63 - x if horizontal else x
            if 0 <= sy < 64:
                po[x, y] = src[sx, sy]
    return out


def text_mask(text, gap=-1):
    """The label as rows of multipliers. gap -1: the glyphs abut, the P's left anti-alias column merging with the
    digit's last (the new labels: the flipped tag is narrower where the label's top is); gap n: n fill columns."""
    rows = [[] for _ in range(8)]
    for i, ch in enumerate(text):
        g = GLYPHS[ch]
        w = len(g[0])
        for r in range(8):
            vals = [LEVEL[c] for c in g[r].ljust(w)]
            if i and gap < 0:
                rows[r][-1] = min(rows[r][-1], vals[0])
                vals = vals[1:]
            elif i:
                vals = [1.0] * gap + vals
            rows[r].extend(vals)
    return rows


def interior(px, y, from_left):
    """The tag's interior columns in row y: from the texture border to the column before the dark edge."""
    xs = list(range(0, 64)) if from_left else list(range(63, -1, -1))
    fill = max(lum(px[x, y]) for x in xs[:TAG_W] if px[x, y][3] >= 200)
    cols = []
    for x in xs[:TAG_W + 2]:
        p = px[x, y]
        if p[3] < 200 or lum(p) < 0.6 * fill:
            break
        cols.append(x)
    return sorted(cols)


def write_label(img, text, from_left, top, log, gap=-1, force_x0=None):
    """Multiply the label onto the fill. Placement: one fill column at the texture border if every row then keeps at
    least one fill column before the dark edge, else none at the border; per row, since the tag narrows."""
    out = img.copy()
    po = out.load()
    mask = text_mask(text, gap)
    w = len(mask[0])
    spans = [interior(po, top + r, from_left) for r in range(8)]
    used = [[c for c in range(w) if mask[r][c] < 1.0] for r in range(8)]
    allused = sorted(set(c for u in used for c in u))

    def clearance(x0):
        """columns of fill between the label and the edge (min over rows), and at the border"""
        edge = min((s[-1] - (x0 + u[-1])) if from_left else ((x0 + u[0]) - s[0]) for s, u in zip(spans, used) if u)
        border = (x0 + allused[0]) if from_left else (63 - (x0 + allused[-1]))
        return edge, border

    if force_x0 is not None:
        x0 = force_x0
    else:
        cands = []
        for b in (1, 0):
            x0 = b - allused[0] if from_left else (63 - b) - allused[-1]
            cands.append((clearance(x0)[0] >= 1, b, x0))
        ok = [c for c in cands if c[0]]
        x0 = (ok[0] if ok else cands[-1])[2]
    edge, border = clearance(x0)
    for r in range(8):
        for c in used[r]:
            m = mask[r][c]
            x, y = x0 + c, top + r
            p = po[x, y]
            po[x, y] = (int(round(p[0] * m)), int(round(p[1] * m)), int(round(p[2] * m)), p[3])
    log.append(f'  label "{text}" rows {top}..{top + 7}, columns {x0 + allused[0]}..{x0 + allused[-1]}: fill columns '
               f'left before the dark edge (narrowest row) {edge}, at the texture border {border}')
    return out, edge


def hue_of(img):
    px = img.load()
    hs = []
    for y in range(64):
        for x in range(64):
            r, g, b, a = px[x, y]
            if a > 200 and max(r, g, b) > 120:
                h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
                if s > 0.4:
                    hs.append(h)
    hs.sort()
    return hs[len(hs) // 2] * 360.0


def rehue(img, target, base):
    out = img.copy()
    po = out.load()
    d = (target - base) / 360.0
    for y in range(64):
        for x in range(64):
            r, g, b, a = po[x, y]
            if a == 0:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
            r2, g2, b2 = colorsys.hsv_to_rgb((h + d) % 1.0, s, v)
            po[x, y] = (int(round(r2 * 255)), int(round(g2 * 255)), int(round(b2 * 255)), a)
    return out


def alpha_iou(a, b, skip=None):
    pa, pb = a.load(), b.load()
    i = u = 0
    for y in range(64):
        for x in range(64):
            if skip and skip(x, y):
                continue
            ia, ib = pa[x, y][3] > 128, pb[x, y][3] > 128
            i += ia and ib
            u += ia or ib
    return i / float(u) if u else 1.0


def dark_left(img, rows, cols):
    px = img.load()
    n = 0
    for y in rows:
        fill = max(lum(px[x, y]) for x in cols if px[x, y][3] >= 200)
        n += sum(1 for x in cols if px[x, y][3] >= 200 and lum(px[x, y]) < 0.6 * fill)
    return n


def on_dark(img, scale, bg=(28, 30, 40, 255)):
    big = img.resize((64 * scale, 64 * scale), Image.NEAREST)
    base = Image.new('RGBA', big.size, bg)
    return Image.alpha_composite(base, big)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--page', help='a 1280x720 shot of the character page (one player)')
    ap.add_argument('--rect', help='X,Y,SIZE of the 1P cursor in that shot (screen pixels)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    log = []
    p1 = load(os.path.join(a.src, 'UI_Char_1PSelector.dds'))
    p2 = load(os.path.join(a.src, 'UI_Char_2PSelector.dds'))
    base_hue = hue_of(p1)
    log.append(f'1P: {p1.size}, hue {base_hue:.1f}; 2P hue {hue_of(p2):.1f}')

    # Epic's 2P against 1P mirrored with x' = 63 - x (outside both labels): the method Epic used
    in_tags = lambda x, y: (y < 20 and (x < 20 or x > 43))
    log.append(f'check: 2P outline = 1P mirrored left-right (x\' = 63 - x): alpha IoU {alpha_iou(flip(p1, False, True), p2, in_tags):.3f} '
               f'(outside the tags; 1.000 = identical)')

    clean = clean_label(p1, log)
    n = dark_left(clean, range(LABEL_ROWS[0], LABEL_ROWS[1] + 1), range(0, 15))
    log.append(f'check: dark pixels left in the cleaned label area (x0..14, rows {LABEL_ROWS[0]}..{LABEL_ROWS[1]}): {n} (want 0)')

    t3 = flip(clean, True, False)
    t4 = flip(clean, True, True)
    hex_only = lambda x, y: (y < 20 or y > 43)
    log.append(f'check: 3P outline vs 1P, rows 20..43 (the hexagon\'s sides): alpha IoU {alpha_iou(t3, p1, hex_only):.3f}')
    log.append(f'check: 4P outline vs 2P, rows 20..43: alpha IoU {alpha_iou(t4, p2, hex_only):.3f}')
    t3full = flip(clean, True, False)
    log.append(f'check: 3P whole outline vs 1P flipped (top/bottom edges land on 1P\'s rows): rows of the top edge at '
               f'x=32: {[y for y in range(64) if t3full.load()[32, y][3] > 128]}, 1P: {[y for y in range(64) if p1.load()[32, y][3] > 128]}')
    top = 64 - LABEL_ROWS[1]          # the 1P label's rows, mirrored (48..55)
    t3, m3 = write_label(t3, '3P', True, top, log)
    t4, m4 = write_label(t4, '4P', False, top, log)
    # self-check of the font: "1P" written on the cleaned 1P at the original place, against Epic's 1P
    one, _ = write_label(clean, '1P', True, LABEL_ROWS[0], [], gap=1, force_x0=1)
    d = [abs(lum(one.load()[x, y]) - lum(p1.load()[x, y])) for y in range(LABEL_ROWS[0], LABEL_ROWS[1] + 1) for x in range(16)]
    log.append(f'check: the transcribed font\'s "1P" written back on the cleaned 1P vs Epic\'s 1P label (x0..15, rows '
               f'{LABEL_ROWS[0]}..{LABEL_ROWS[1]}): mean |luminance difference| {sum(d) / len(d):.1f} of 255, max {max(d):.0f} '
               f'(at Epic\'s place: "1" from x2, one fill column, "P" from x7)')

    tags = {}
    for name, h in CANDIDATES:
        tags[('3P', name)] = rehue(t3, h, base_hue)
        tags[('4P', name)] = rehue(t4, h, base_hue)
        for k in ('3P', '4P'):
            tags[(k, name)].save(os.path.join(a.out, f'UI_Char_{k}Selector-{name}.tga'), compression='tga_rle')
    one.save(os.path.join(a.out, 'check-1P-rewritten.png'))

    # the sheet
    S = 4
    cell = 64 * S
    font = ImageFont.truetype(FONT, 18)
    small = ImageFont.truetype(FONT, 14)
    cols = 2 + len(CANDIDATES) * 2
    W = 24 + cols * (cell + 12)
    page = None
    if a.page and a.rect:
        page = Image.open(a.page).convert('RGBA')
        rx, ry, rs = [float(v) for v in a.rect.split(',')]
    ctx_h = 0
    if page:
        ctx_w, ctx_h = 300, 300
    H = 60 + (cell + 40) + 40 + (ctx_h + 60 if page else 0) + 30
    sheet = Image.new('RGBA', (max(W, 24 + (len(PAIRS) + 1) * 320), H), (16, 17, 22, 255))
    dr = ImageDraw.Draw(sheet)
    dr.text((24, 16), 'X-1: candidate 3P / 4P cursors (4x, nearest), from the shipped 1P/2P (your T_UI_2.utx). '
                      'Originals first.', font=font, fill=(230, 230, 230, 255))
    x = 24
    y = 60
    items = [('1P (Epic)', p1), ('2P (Epic)', p2)]
    for name, _ in CANDIDATES:
        items.append((f'3P {name}', tags[('3P', name)]))
        items.append((f'4P {name}', tags[('4P', name)]))
    for label, im in items:
        sheet.alpha_composite(on_dark(im, S), (x, y))
        dr.text((x, y + cell + 6), label, font=small, fill=(220, 220, 220, 255))
        x += cell + 12
    y += cell + 40
    if page:
        dr.text((24, y), 'In place: the four cursors on one portrait of the real page (screen 1280x720, cursor '
                         f'{rs:.0f} px): a mock-up composited by this script, not the engine\'s drawing', font=font,
                fill=(230, 230, 230, 255))
        y += 34
        x = 24
        cx0, cy0 = int(rx - 60), int(ry - 50)
        for pair in [None] + PAIRS:
            crop = page.crop((cx0, cy0, cx0 + 240, cy0 + 220)).copy()
            layers = [p1, p2] + ([tags[('3P', pair[0])], tags[('4P', pair[1])]] if pair else [])
            for im in layers:
                big = im.resize((int(round(rs)), int(round(rs))), Image.BILINEAR)
                crop.alpha_composite(big, (int(round(rx - cx0)), int(round(ry - cy0))))
            crop = crop.resize((300, 275), Image.BILINEAR)
            sheet.alpha_composite(crop, (x, y))
            dr.text((x, y + 280), 'shipped 1P + 2P' if not pair else f'3P {pair[0]} + 4P {pair[1]}', font=small,
                    fill=(220, 220, 220, 255))
            x += 320
    sheet.save(os.path.join(a.out, 'player-tags-sheet.png'))
    with open(os.path.join(a.out, 'player-tags-report.txt'), 'w') as f:
        f.write('\n'.join(log) + '\n')
    print('\n'.join(log))
    print('wrote', os.path.join(a.out, 'player-tags-sheet.png'))


if __name__ == '__main__':
    main()
