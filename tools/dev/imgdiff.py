#!/usr/bin/env python3
"""Compare two pictures of the same size (M11: desktop driver against the GL4ES twin or the browser).

  tools/dev/imgdiff.py A.png B.png [--box x0,y0,x1,y1] [--mask x0,y0,x1,y1 ...] [--side out.png] [--diff out.png]

Prints one line: mean absolute difference over the three channels (of 255), the largest difference, and the
share of pixels whose largest channel difference is above 2, 8 and 32 levels. --box restricts the comparison
to a rectangle, --mask blanks rectangles in both pictures first (animated things), --side writes A and B
side by side at half size, --diff writes the difference times 8.
Only Pillow is needed (no numpy on this machine).
"""
import sys
from PIL import Image, ImageChops, ImageStat, ImageDraw

def main():
    args = sys.argv[1:]
    files = []; box = None; masks = []; side = None; diff = None
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--box': box = tuple(int(v) for v in args[i+1].split(',')); i += 2
        elif a == '--mask': masks.append(tuple(int(v) for v in args[i+1].split(','))); i += 2
        elif a == '--side': side = args[i+1]; i += 2
        elif a == '--diff': diff = args[i+1]; i += 2
        else: files.append(a); i += 1
    a = Image.open(files[0]).convert('RGB'); b = Image.open(files[1]).convert('RGB')
    if a.size != b.size:
        print('size differs: %s vs %s' % (a.size, b.size)); return 2
    if side:
        w = Image.new('RGB', (a.size[0]*2, a.size[1])); w.paste(a, (0, 0)); w.paste(b, (a.size[0], 0))
        w.resize((a.size[0], a.size[1]//2)).save(side)
    for m in masks:
        for im in (a, b):
            ImageDraw.Draw(im).rectangle(m, fill=(0, 0, 0))
    if box:
        a = a.crop(box); b = b.crop(box)
    d = ImageChops.difference(a, b)
    if diff:
        d.point(lambda v: min(255, v*8)).save(diff)
    r, g, bl = d.split()
    m = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    hist = m.histogram(); n = float(a.size[0]*a.size[1])
    above = lambda t: 100.0*sum(hist[t+1:])/n
    mx = max(i for i, c in enumerate(hist) if c)
    print('mean=%.3f max=%d above2=%.3f%% above8=%.3f%% above32=%.3f%%' % (sum(ImageStat.Stat(d).mean)/3, mx, above(2), above(8), above(32)))
    return 0

sys.exit(main())
