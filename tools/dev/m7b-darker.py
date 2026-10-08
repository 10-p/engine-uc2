#!/usr/bin/env python3
"""How much darker picture A is than picture B, pixel by pixel (M7b: the PC shadow against the Xbox one).

    tools/dev/m7b-darker.py A.png B.png A2.png

Counts the pixels whose luminance (0.30 R + 0.59 G + 0.11 B) in A is below 0.9 and below 0.8 of B's, where B is
not black (luminance > 20) and A does not change between A and A2 (a second frame of A's run: the animated pawn
and particles drop out). Prints "darker10=<n> darker20=<n> median-ratio=<r> of <pixels> pixels compared".
Only Pillow is needed.
"""
import sys
from PIL import Image

def lum(p):
    return 0.30 * p[0] + 0.59 * p[1] + 0.11 * p[2]

a = Image.open(sys.argv[1]).convert('RGB'); b = Image.open(sys.argv[2]).convert('RGB'); a2 = Image.open(sys.argv[3]).convert('RGB')
if a.size != b.size or a.size != a2.size:
    print('size differs'); sys.exit(2)
pa, pb, pa2 = a.load(), b.load(), a2.load()
w, h = a.size
d10 = d20 = n = 0
ratios = []
for y in range(h):
    for x in range(w):
        la, lb = lum(pa[x, y]), lum(pb[x, y])
        if lb <= 20 or abs(la - lum(pa2[x, y])) >= 2:
            continue
        n += 1
        r = la / lb
        if r < 0.9:
            d10 += 1
        if r < 0.8:
            d20 += 1
        if n % 16 == 0:
            ratios.append(r)
ratios.sort()
print('darker10=%d darker20=%d median-ratio=%.3f of %d pixels compared' % (d10, d20, ratios[len(ratios) // 2] if ratios else 0, n))
