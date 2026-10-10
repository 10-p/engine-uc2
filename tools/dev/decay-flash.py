#!/usr/bin/env python3
"""B-2: luminance measure of the pictures of one tools/dev/decay-flash.sh run (DM-Decay white cloud).

    decay-flash.py <logs/tree/df-name>        every <prefix>-<shot>.png, in shot order of <prefix>.result.shots

Per picture: the mean luma (Rec. 601, 0..255) of the middle region (the central 50% x 50%), of the frame outside it,
their difference, the fraction of middle pixels at or above 235 ("white"), and the mean saturation of the middle
(a grey-white veil lowers it). The HUD is in both regions; it is the same in every picture of a run.
"""
import sys, os
from PIL import Image

prefix = sys.argv[1]
shots_file = prefix + '.result.shots'
names = [l.strip() for l in open(shots_file)] if os.path.exists(shots_file) else []
print('%-14s %7s %7s %7s %7s %7s' % ('shot', 'middle', 'outer', 'diff', 'white%', 'satmid'))
for n in names:
    p = '%s-%s.png' % (prefix, n)
    if not os.path.exists(p):
        print('%-14s missing' % n); continue
    im = Image.open(p).convert('RGB')
    w, h = im.size
    px = im.load()
    sm = so = 0.0; cm = co = 0; white = 0; sat = 0.0
    step = max(1, w // 320)
    for y in range(0, h, step):
        for x in range(0, w, step):
            r, g, b = px[x, y]
            l = 0.299 * r + 0.587 * g + 0.114 * b
            if w // 4 <= x < 3 * w // 4 and h // 4 <= y < 3 * h // 4:
                sm += l; cm += 1
                if l >= 235: white += 1
                mx = max(r, g, b); mn = min(r, g, b)
                sat += (mx - mn) / mx if mx else 0
            else:
                so += l; co += 1
    print('%-14s %7.1f %7.1f %7.1f %7.1f %7.3f' % (n, sm / cm, so / co, sm / cm - so / co, 100.0 * white / cm, sat / cm))
