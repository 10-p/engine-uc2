#!/usr/bin/env python3
"""Compose the pictures of tools/dev/jit-strip.sh into one image: a row per run (e.g. before / after), consecutive
frames left to right, each cropped around the pawn, with the frame's length, the pawn's screen position from the
trace, and a fixed cross where the pawn stands on average (docs/porting/analysis/jitter.md); under them a chart of
the pawn's vertical screen offset per frame for every row. Also <out>.gif: the rows side by side as an animation
(150 ms a frame, i.e. slowed down; the fixed cross stays put, the pawn moves against it).

    jit-strip.py <out.png> <dir> <name>[:<label>] [<name>[:<label>] ...]
"""
import math, os, re, sys
from PIL import Image, ImageDraw

LINE = re.compile(r'^frametrace strip (\d+) t=([-\d.]+) dt=([-\d.]+) .* scr=\(([-\d.]+),([-\d.]+)\)')


def frames_of_shots(log):
    """For every `test: exec shot` line, the trace line before it (the frame the shot reads)."""
    last = None
    shots = []
    with open(log, encoding='latin-1', errors='replace') as f:
        for line in f:
            m = LINE.match(line)
            if m:
                last = dict(i=int(m.group(1)), t=float(m.group(2)), dt=float(m.group(3)),
                            x=float(m.group(4)), y=float(m.group(5)))
            elif line.startswith('test: exec shot'):
                shots.append(last)
    return shots


def main():
    out, d = sys.argv[1], sys.argv[2]
    rows = []
    for spec in sys.argv[3:]:
        name, _, label = spec.partition(':')
        shots = frames_of_shots(os.path.join(d, name + '.out'))
        pics = sorted(p for p in os.listdir(d) if re.fullmatch(re.escape(name) + r'-\d\d\.png', p))
        rows.append((label or name, shots, [os.path.join(d, p) for p in pics]))
    W, H = 220, 300          # crop, in the 1280x720 frame
    ncol = min(min(len(r[2]) for r in rows), min(len(r[1]) for r in rows))
    pad, head = 6, 34
    CH = 200                 # chart height
    img = Image.new('RGB', (110 + ncol * (W + pad), len(rows) * (H + head + pad) + 10 + CH + 30), (24, 24, 24))
    crops = [[] for _ in rows]
    colours = [(255, 120, 60), (80, 200, 255), (200, 200, 80), (200, 120, 255)]
    dr = ImageDraw.Draw(img)
    for r, (label, shots, pics) in enumerate(rows):
        sel = [s for s in shots[:ncol] if s]
        cx = sum(s['x'] for s in sel) / len(sel)
        cy = sum(s['y'] for s in sel) / len(sel)
        y0 = 5 + r * (H + head + pad)
        dr.text((6, y0 + head + H // 2 - 6), label, fill=(255, 255, 255))
        for c in range(ncol):
            s = shots[c]
            pic = Image.open(pics[c]).convert('RGB')
            sx = pic.width / 1280.0
            box = (int(cx * sx - W / 2), int(cy * sx - H * 0.62), int(cx * sx + W / 2), int(cy * sx + H * 0.38))
            crop = pic.crop(box)
            x0 = 110 + c * (W + pad)
            img.paste(crop, (x0, y0 + head))
            cd = ImageDraw.Draw(crop)
            cd.line([(W / 2 - 14, H * 0.62), (W / 2 + 14, H * 0.62)], fill=(255, 40, 40), width=2)
            cd.line([(W / 2, H * 0.62 - 14), (W / 2, H * 0.62 + 14)], fill=(255, 40, 40), width=2)
            cd.text((4, 4), f"{label}", fill=(255, 255, 255))
            if s:
                cd.text((4, 18), f"{1000 * s['dt']:.0f} ms frame", fill=(230, 230, 230))
            crops[r].append(crop)
            # the fixed cross: where the pawn's centre is on average over these frames
            mx, my = W / 2, H * 0.62
            dr.line([(x0 + mx - 12, y0 + head + my), (x0 + mx + 12, y0 + head + my)], fill=(255, 40, 40), width=1)
            dr.line([(x0 + mx, y0 + head + my - 12), (x0 + mx, y0 + head + my + 12)], fill=(255, 40, 40), width=1)
            if s:
                # the pawn's centre this frame (from the trace)
                px, py = x0 + mx + (s['x'] - cx) * sx, y0 + head + my + (s['y'] - cy) * sx
                dr.ellipse([(px - 3, py - 3), (px + 3, py + 3)], outline=(60, 255, 60))
                dr.text((x0 + 2, y0 + 2), f"frame {s['i']}  {1000 * s['dt']:.1f} ms", fill=(230, 230, 230))
                dr.text((x0 + 2, y0 + 16), f"off {s['x'] - cx:+.1f},{s['y'] - cy:+.1f} px", fill=(230, 230, 230))
    # chart: vertical screen offset of the pawn (from the trace) per frame, one line per row
    cy0 = len(rows) * (H + head + pad) + 10
    x0, x1 = 110, 110 + ncol * (W + pad) - pad
    mid = cy0 + CH / 2
    dr.line([(x0, mid), (x1, mid)], fill=(90, 90, 90))
    dr.text((6, cy0 + 4), 'pawn higher (+)', fill=(200, 200, 200))
    dr.text((6, cy0 + 18), 'or lower (-) on', fill=(200, 200, 200))
    dr.text((6, cy0 + 32), 'the screen, px', fill=(200, 200, 200))
    dr.text((6, mid - 6), '0', fill=(200, 200, 200))
    scale = (CH / 2 - 8) / 15.0     # +-15 px fills the chart
    for k in (-10, 10):
        dr.line([(x0, mid - k * scale), (x1, mid - k * scale)], fill=(50, 50, 50))
        dr.text((80, mid - k * scale - 6), f"{k:+d}", fill=(150, 150, 150))
    for r, (label, shots, pics) in enumerate(rows):
        sel = [s for s in shots[:ncol] if s]
        cy = sum(s['y'] for s in sel) / len(sel)
        pts = []
        for c in range(ncol):
            s = shots[c]
            if not s:
                continue
            pts.append((x0 + c * (W + pad) + W / 2, mid - (cy - s['y']) * scale))   # up on the chart = up on the screen
        dr.line(pts, fill=colours[r % len(colours)], width=3)
        for p in pts:
            dr.ellipse([(p[0] - 4, p[1] - 4), (p[0] + 4, p[1] + 4)], fill=colours[r % len(colours)])
        dr.text((x1 - 260, cy0 + 4 + 14 * r), label, fill=colours[r % len(colours)])
    img.save(out)
    # animation: rows side by side
    frames = []
    for c in range(ncol):
        f = Image.new('RGB', (len(rows) * (W + pad), H), (24, 24, 24))
        for r in range(len(rows)):
            f.paste(crops[r][c], (r * (W + pad), 0))
        frames.append(f)
    gif = os.path.splitext(out)[0] + '.gif'
    frames[0].save(gif, save_all=True, append_images=frames[1:], duration=150, loop=0)
    print(out)
    print(gif)


if __name__ == '__main__':
    main()
