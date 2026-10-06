#!/usr/bin/env python3
"""Compare two engine screenshots of the pinned DM-Deadbolt view (tools/dev/m12/deadbolt-pinned.scen).

    python3 tools/dev/pinned-compare.py <reference.png> <other.png> <diff.png>

Prints "OK <static % within 2 levels> <whole frame % bit-identical> <PSNR dB>" (or "SIZE ..."). The player
model, the two pickups and the aiming reticle animate with the time since the spawn and are left out of the
static part. Used by m12-wine-smoke.sh (wine vs Linux) and m13-64bit.sh (64-bit vs 32-bit)."""
import sys, math
from PIL import Image, ImageChops
a = Image.open(sys.argv[1]).convert('RGB'); b = Image.open(sys.argv[2]).convert('RGB')
if a.size != b.size: print("SIZE %s %s" % (a.size, b.size)); sys.exit(0)
d = ImageChops.difference(a, b); r, g, bl = d.split()
mx = ImageChops.lighter(ImageChops.lighter(r, g), bl)
d.point(lambda v: min(255, 4 * v)).save(sys.argv[3])
w, h = a.size; px = mx.load()
# The player model and the two pickups of this pinned view (they animate with the time since the spawn,
# which differs between two runs): x 60..395, y >= 265 of the 640x480 frame.
static = moving = same_static = exact = 0; sq = 0
for y in range(h):
    for x in range(w):
        v = px[x, y]; sq += v * v; exact += (v == 0)
        if 60 * w // 640 <= x <= 395 * w // 640 and y >= 265 * h // 480: moving += 1; continue
        # ... and the aiming reticle in the middle of the screen, which animates too (x 280..360, y 210..265).
        if 280 * w // 640 <= x <= 360 * w // 640 and 210 * h // 480 <= y < 265 * h // 480: moving += 1; continue
        static += 1; same_static += (v <= 2)
mse = sq / float(w * h)
print("OK %.3f %.1f %.2f" % (100.0 * same_static / static, 100.0 * exact / (w * h), 99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse)))
