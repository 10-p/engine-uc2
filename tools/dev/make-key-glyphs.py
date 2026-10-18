#!/usr/bin/env python3
"""The key glyph sheet (key caps and mouse icons) and the engine's table of its cells.

    tools/dev/make-key-glyphs.py [--preview <png>]

writes
    patches/key-glyphs/new/GUI/Textures/KeyGlyphSheet.tga   512x512 RGBA, run-length coded (the set's GUI.KeyGlyphs
                                                            class imports it into GUI.u: GUI.KeyGlyphSheet)
    Source/Engine/Src/UnKeyGlyphsSheet.h                   the cell of each key (included by UnKeyGlyphs.cpp)
Both are generated: change this script and run it again, never the outputs.

The sheet is an 11x11 grid of 46-pixel cells, each holding a 44x44 glyph (one pixel of transparent gutter all round so
the mip maps do not mix neighbours). The look follows the button font T_Fonts2.Controller_Buttons_LG (its stick and
Back/Start buttons): a dark glossy body, a light rim, a white legend with a dark edge. Keys are rounded caps; the mouse
buttons and the wheel are a mouse with the part to press lit in amber. Legends are DejaVu Sans Bold (Bitstream Vera
licence: free to embed), drawn at 4x and reduced. Keys with no cell of their own use the blank cap (BLANK) with the
key's text label drawn on it by the engine.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
TGA = os.path.join(REPO, 'patches', 'key-glyphs', 'new', 'GUI', 'Textures', 'KeyGlyphSheet.tga')
HDR = os.path.join(REPO, 'Source', 'Engine', 'Src', 'UnKeyGlyphsSheet.h')
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

SHEET, COLS, STRIDE, GLYPH = 512, 11, 46, 44
SS = 4                      # supersampling
S = GLYPH * SS

RIM = (214, 219, 226, 255)
BODY_TOP = (78, 84, 92, 255)
BODY_BOTTOM = (22, 24, 28, 255)
FACE_TOP = (64, 69, 76, 255)
FACE_BOTTOM = (30, 33, 38, 255)
LEGEND = (255, 255, 255, 255)
EDGE = (0, 0, 0, 200)
LIT = (240, 168, 40, 255)


def font(px):
    return ImageFont.truetype(FONT, px)


def vgrad(size, top, bottom):
    g = Image.new('RGBA', size)
    d = ImageDraw.Draw(g)
    w, h = size
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(4)))
    return g


def rounded_mask(size, box, radius):
    m = Image.new('L', size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=radius, fill=255)
    return m


def keycap():
    """A blank key cap: rim, body, an inset top face (the key seen from above and a little in front)."""
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    m = 2 * SS
    outer = (m, m, S - 1 - m, S - 1 - m)
    r = int(S * 0.2)
    im.paste(Image.new('RGBA', (S, S), RIM), (0, 0), rounded_mask((S, S), outer, r))
    rim = int(2.2 * SS)
    body = (outer[0] + rim, outer[1] + rim, outer[2] - rim, outer[3] - rim)
    im.paste(vgrad((S, S), BODY_TOP, BODY_BOTTOM), (0, 0), rounded_mask((S, S), body, r - rim))
    fi = int(3 * SS)
    face = (body[0] + fi, body[1] + int(1.5 * SS), body[2] - fi, body[3] - int(4.5 * SS))
    im.paste(vgrad((S, S), FACE_TOP, FACE_BOTTOM), (0, 0), rounded_mask((S, S), face, r - rim - fi // 2))
    return im, face


def legend(im, box, text, max_px=None):
    """White text with a dark edge, as big as fits the box."""
    d = ImageDraw.Draw(im)
    bw, bh = box[2] - box[0], box[3] - box[1]
    px = max_px or int(bh * 0.72)
    while px > 6:
        f = font(px)
        l, t, r, b = d.textbbox((0, 0), text, font=f, stroke_width=SS)
        if r - l <= bw * 0.9 and b - t <= bh * 0.86:
            break
        px -= 2
    f = font(px)
    l, t, r, b = d.textbbox((0, 0), text, font=f, stroke_width=SS)
    x = box[0] + (bw - (r - l)) / 2 - l
    y = box[1] + (bh - (b - t)) / 2 - t
    d.text((x, y), text, font=f, fill=LEGEND, stroke_width=SS, stroke_fill=EDGE)


def poly(im, pts, fill=LEGEND, edge=True):
    d = ImageDraw.Draw(im)
    if edge:
        d.polygon(pts, fill=EDGE, outline=EDGE, width=2 * SS)
    d.polygon(pts, fill=fill)


def arrow(im, box, direction, scale=1.0):
    """A filled arrow (head and stem) pointing up/down/left/right, centred in box."""
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    s = min(box[2] - box[0], box[3] - box[1]) * 0.42 * scale
    pts = [(0, -1), (0.75, -0.1), (0.28, -0.1), (0.28, 0.9), (-0.28, 0.9), (-0.28, -0.1), (-0.75, -0.1)]
    rot = {'up': lambda x, y: (x, y), 'down': lambda x, y: (x, -y),
           'left': lambda x, y: (y, x), 'right': lambda x, y: (-y, x)}[direction]
    poly(im, [(cx + rot(x, y)[0] * s, cy + rot(x, y)[1] * s) for x, y in pts])


def line(im, pts, width):
    d = ImageDraw.Draw(im)
    d.line(pts, fill=EDGE, width=width + 2 * SS, joint='curve')
    d.line(pts, fill=LEGEND, width=width, joint='curve')


def icon_enter(im, b):
    w, h = b[2] - b[0], b[3] - b[1]
    x1, y0, y1 = b[0] + w * 0.78, b[1] + h * 0.18, b[1] + h * 0.62
    x0 = b[0] + w * 0.36
    line(im, [(x1, y0), (x1, y1), (x0, y1)], int(SS * 2.6))
    s = h * 0.2
    poly(im, [(b[0] + w * 0.16, y1), (x0 + 1, y1 - s), (x0 + 1, y1 + s)])


def icon_tab(im, b):
    w, h = b[2] - b[0], b[3] - b[1]
    y = b[1] + h * 0.5
    line(im, [(b[0] + w * 0.16, y), (b[0] + w * 0.6, y)], int(SS * 2.6))
    s = h * 0.2
    poly(im, [(b[0] + w * 0.76, y), (b[0] + w * 0.56, y - s), (b[0] + w * 0.56, y + s)])
    line(im, [(b[0] + w * 0.84, y - h * 0.26), (b[0] + w * 0.84, y + h * 0.26)], int(SS * 2.6))


def icon_shift(im, b):
    w, h = b[2] - b[0], b[3] - b[1]
    cx = b[0] + w / 2
    pts = [(cx, b[1] + h * 0.12), (b[0] + w * 0.86, b[1] + h * 0.52), (b[0] + w * 0.66, b[1] + h * 0.52),
           (b[0] + w * 0.66, b[1] + h * 0.88), (b[0] + w * 0.34, b[1] + h * 0.88), (b[0] + w * 0.34, b[1] + h * 0.52),
           (b[0] + w * 0.14, b[1] + h * 0.52)]
    d = ImageDraw.Draw(im)
    d.polygon(pts, outline=EDGE, width=int(SS * 3.6))
    d.polygon(pts, outline=LEGEND, width=int(SS * 2.2))


def icon_backspace(im, b):
    w, h = b[2] - b[0], b[3] - b[1]
    pts = [(b[0] + w * 0.1, b[1] + h * 0.5), (b[0] + w * 0.36, b[1] + h * 0.2), (b[0] + w * 0.9, b[1] + h * 0.2),
           (b[0] + w * 0.9, b[1] + h * 0.8), (b[0] + w * 0.36, b[1] + h * 0.8)]
    d = ImageDraw.Draw(im)
    d.polygon(pts, outline=EDGE, width=int(SS * 3.6))
    d.polygon(pts, outline=LEGEND, width=int(SS * 2.2))
    c = (b[0] + w * 0.62, b[1] + h * 0.5)
    k = h * 0.14
    line(im, [(c[0] - k, c[1] - k), (c[0] + k, c[1] + k)], int(SS * 2))
    line(im, [(c[0] - k, c[1] + k), (c[0] + k, c[1] - k)], int(SS * 2))


def icon_space(im, b):
    w, h = b[2] - b[0], b[3] - b[1]
    y0, y1 = b[1] + h * 0.42, b[1] + h * 0.66
    line(im, [(b[0] + w * 0.14, y0), (b[0] + w * 0.14, y1), (b[0] + w * 0.86, y1), (b[0] + w * 0.86, y0)], int(SS * 2.6))


def icon_arrows(im, b):
    w, h = b[2] - b[0], b[3] - b[1]
    for dirn, (fx, fy) in (('up', (0.5, 0.27)), ('down', (0.5, 0.73)), ('left', (0.25, 0.5)), ('right', (0.75, 0.5))):
        cx, cy = b[0] + w * fx, b[1] + h * fy
        q = w * 0.2
        arrow(im, (cx - q, cy - q, cx + q, cy + q), dirn, 1.05)


def icon_numpad(im, b):
    w, h = b[2] - b[0], b[3] - b[1]
    d = ImageDraw.Draw(im)
    q = w * 0.17
    for i in range(3):
        for j in range(3):
            x = b[0] + w * (0.2 + 0.3 * i)
            y = b[1] + h * (0.2 + 0.3 * j)
            lit = (i, j) in ((1, 0), (0, 1), (2, 1), (1, 2))
            d.rounded_rectangle((x - q / 2, y - q / 2, x + q / 2, y + q / 2), radius=SS * 2,
                                fill=LIT if lit else (150, 155, 162, 255), outline=EDGE, width=SS)


def mouse(lit=None, mark=None):
    """A mouse seen from above: body, the two buttons, the wheel; lit: 'left', 'right', 'wheel' or None."""
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = S * 0.24, S * 0.06, S * 0.76, S * 0.94
    r = (x1 - x0) * 0.48
    d.rounded_rectangle((x0, y0, x1, y1), radius=r, fill=RIM)
    rim = 2.2 * SS
    body = (x0 + rim, y0 + rim, x1 - rim, y1 - rim)
    im.paste(vgrad((S, S), BODY_TOP, BODY_BOTTOM), (0, 0), rounded_mask((S, S), body, r - rim))
    split = y0 + (y1 - y0) * 0.42
    cx = (x0 + x1) / 2
    if lit in ('left', 'right'):
        m = Image.new('L', (S, S), 0)
        md = ImageDraw.Draw(m)
        md.rounded_rectangle(body, radius=r - rim, fill=255)
        md.rectangle((0, split, S, S), fill=0)
        if lit == 'left':
            md.rectangle((cx, 0, S, S), fill=0)
        else:
            md.rectangle((0, 0, cx, S), fill=0)
        im.paste(Image.new('RGBA', (S, S), LIT), (0, 0), m)
    d.line([(body[0], split), (body[2], split)], fill=RIM, width=int(SS * 1.6))
    d.line([(cx, body[1]), (cx, split)], fill=RIM, width=int(SS * 1.6))
    ww, wh = S * 0.075, S * 0.17
    wy = y0 + (y1 - y0) * 0.2
    d.rounded_rectangle((cx - ww, wy - wh / 2, cx + ww, wy + wh / 2), radius=ww,
                        fill=LIT if lit == 'wheel' else (150, 155, 162, 255), outline=EDGE, width=SS)
    if mark:
        lb = (body[0], split + SS * 2, body[2], body[3] - SS * 2)
        if mark in ('up', 'down'):
            arrow(im, lb, mark, 0.9)
        else:
            legend(im, lb, mark)
    return im


def cap(text=None, draw=None):
    im, face = keycap()
    if draw:
        draw(im, face)
    elif text:
        legend(im, face, text)
    return im


# The cells, in order: (name in the table, image maker, the keys it stands for). Order = cell index.
def cells():
    out = []
    for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
        out.append((c, lambda c=c: cap(c), ['IK_' + c]))
    for c in '0123456789':
        out.append((c, lambda c=c: cap(c), ['IK_' + c]))
    for i in range(1, 13):
        out.append(('F%d' % i, lambda i=i: cap('F%d' % i), ['IK_F%d' % i]))
    out += [
        ('ENTER', lambda: cap(draw=icon_enter), ['IK_Enter']),
        ('ESC', lambda: cap('ESC'), ['IK_Escape']),
        ('SPACE', lambda: cap(draw=icon_space), ['IK_Space']),
        ('TAB', lambda: cap(draw=icon_tab), ['IK_Tab']),
        ('SHIFT', lambda: cap(draw=icon_shift), ['IK_Shift', 'IK_LShift', 'IK_RShift']),
        ('CTRL', lambda: cap('CTRL'), ['IK_Ctrl', 'IK_LControl', 'IK_RControl']),
        ('ALT', lambda: cap('ALT'), ['IK_Alt']),
        ('BKSP', lambda: cap(draw=icon_backspace), ['IK_Backspace']),
        ('DEL', lambda: cap('DEL'), ['IK_Delete']),
        ('INS', lambda: cap('INS'), ['IK_Insert']),
        ('HOME', lambda: cap('HOME'), ['IK_Home']),
        ('END', lambda: cap('END'), ['IK_End']),
        ('PGUP', lambda: cap('PG↑'), ['IK_PageUp']),
        ('PGDN', lambda: cap('PG↓'), ['IK_PageDown']),
        ('UP', lambda: cap(draw=lambda im, b: arrow(im, b, 'up')), ['IK_Up']),
        ('DOWN', lambda: cap(draw=lambda im, b: arrow(im, b, 'down')), ['IK_Down']),
        ('LEFT', lambda: cap(draw=lambda im, b: arrow(im, b, 'left')), ['IK_Left']),
        ('RIGHT', lambda: cap(draw=lambda im, b: arrow(im, b, 'right')), ['IK_Right']),
        ('LMB', lambda: mouse('left'), ['IK_LeftMouse']),
        ('RMB', lambda: mouse('right'), ['IK_RightMouse']),
        ('MMB', lambda: mouse('wheel'), ['IK_MiddleMouse']),
        ('M4', lambda: mouse(None, '4'), ['IK_Mouse4']),
        ('M5', lambda: mouse(None, '5'), ['IK_Mouse5']),
        ('WHLUP', lambda: mouse('wheel', 'up'), ['IK_MouseWheelUp']),
        ('WHLDN', lambda: mouse('wheel', 'down'), ['IK_MouseWheelDown']),
    ]
    for name, text in (('Semicolon', ';'), ('Equals', '='), ('Comma', ','), ('Minus', '-'), ('Period', '.'),
                       ('Slash', '/'), ('Tilde', '`'), ('LeftBracket', '['), ('Backslash', '\\'),
                       ('RightBracket', ']'), ('SingleQuote', "'")):
        out.append((name.upper(), lambda t=text: cap(t), ['IK_' + name]))
    for i in range(10):
        out.append(('NUM%d' % i, lambda i=i: numcap(str(i)), ['IK_NumPad%d' % i]))
    for name, text in (('GreyStar', '*'), ('GreyPlus', '+'), ('GreyMinus', '-'), ('GreySlash', '/'),
                       ('NumPadPeriod', '.')):
        out.append(('NUM' + text, lambda t=text: numcap(t), ['IK_' + name]))
    out += [
        ('CAPS', lambda: cap('CAPS'), ['IK_CapsLock']),
        ('PAUSE', lambda: cap('PAUSE'), ['IK_Pause']),
        ('ARROWS', lambda: cap(draw=icon_arrows), ['KGS_Arrows']),
        ('MOUSE', lambda: mouse(None), ['KGS_Mouse']),
        ('NUMPAD', lambda: cap(draw=icon_numpad), ['KGS_NumPad']),
        ('BLANK', lambda: cap(), []),
    ]
    return out


def numcap(text):
    im, face = keycap()
    w, h = face[2] - face[0], face[3] - face[1]
    legend(im, (face[0], face[1] + h * 0.3, face[2], face[3]), text)
    legend(im, (face[0], face[1], face[2], face[1] + h * 0.36), 'NUM', max_px=int(h * 0.3))
    return im


def main():
    preview = None
    if '--preview' in sys.argv:
        preview = sys.argv[sys.argv.index('--preview') + 1]
    cl = cells()
    if len(cl) > COLS * COLS:
        sys.exit('make-key-glyphs: %d cells do not fit %dx%d' % (len(cl), COLS, COLS))
    sheet = Image.new('RGBA', (SHEET, SHEET), (0, 0, 0, 0))
    rows = []
    blank = None
    for i, (name, make, keys) in enumerate(cl):
        g = make().resize((GLYPH, GLYPH), Image.LANCZOS)
        x, y = (i % COLS) * STRIDE + 1, (i // COLS) * STRIDE + 1
        sheet.paste(g, (x, y))
        for k in keys:
            rows.append('\t{ %-20s %3d },\t// %s' % (k + ',', i, name))
        if name == 'BLANK':
            blank = i
    os.makedirs(os.path.dirname(TGA), exist_ok=True)
    sheet.save(TGA, rle=True)
    with open(HDR, 'w', newline='\n') as f:
        f.write('/*=============================================================================\n'
                '\tUnKeyGlyphsSheet.h: GENERATED by tools/dev/make-key-glyphs.py: do not edit.\n\n'
                '\tThe cells of GUI.KeyGlyphSheet (patches/key-glyphs/new/GUI/Textures/KeyGlyphSheet.tga): an %dx%d\n'
                '\tgrid of %d-pixel cells, the glyph at +1,+1, %dx%d. H-5, UnKeyGlyphs.cpp.\n'
                '=============================================================================*/\n\n'
                % (COLS, COLS, STRIDE, GLYPH, GLYPH))
        f.write('enum { KEYGLYPH_SHEET=%d, KEYGLYPH_COLUMNS=%d, KEYGLYPH_STRIDE=%d, KEYGLYPH_SIZE=%d, KEYGLYPH_BLANK=%d };\n\n'
                % (SHEET, COLS, STRIDE, GLYPH, blank))
        f.write('static const struct { INT Key; INT Cell; } GKeyGlyphCells[] =\n{\n')
        f.write('\n'.join(rows) + '\n};\n')
    print('make-key-glyphs: %d cells -> %s (%d bytes), %s' % (len(cl), os.path.relpath(TGA, REPO), os.path.getsize(TGA),
                                                         os.path.relpath(HDR, REPO)))
    if preview:
        bg = Image.new('RGBA', sheet.size, (40, 40, 90, 255))
        bg.alpha_composite(sheet)
        bg.resize((SHEET * 2, SHEET * 2), Image.NEAREST).save(preview)


if __name__ == '__main__':
    main()
