#!/usr/bin/env python3
"""Stipple and flicker of a projected shadow over consecutive frames (B-1, docs/porting/analysis/shadow-flicker.md).

  b1-stipple.py <prefix> [--box x0,y0,x1,y1] [--thr T] [--frames N] [--diff out.png] [--strip out.png]

<prefix>-NN.png: consecutive frames (tools/dev/f5-flicker.sh). Per frame, in the box:

- stipple: the share of pixels that are a 1-pixel spike, i.e. whose luminance differs by more than T levels (default
  12) from BOTH neighbours above and below, or from both neighbours left and right, in the same direction: the
  pixel-fine crosshatch of a depth test that passes and fails on alternate pixels (z-fighting). Smooth shading,
  texture-free surfaces (RMODE 7) and soft shadow edges have none; the HUD's thin lines do (keep them out of the box).
- nyquist: the mean |[1,-2,1]| response along columns and rows (the energy at the highest frequency), in levels.
- toggle (between frames, as f5-flicker.py): the share of pixels whose luminance goes more than 24 levels one way and
  back over three frames.
- change: mean |frame i+1 - frame i|.

--diff writes |frame 1 - frame 0| x 4 of the box, --strip the frames of the box side by side (6 per row).
Python standard library + Pillow only.
"""
import argparse
import os
from PIL import Image, ImageChops


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prefix')
    ap.add_argument('--box')
    ap.add_argument('--thr', type=int, default=12)
    ap.add_argument('--frames', type=int, default=0)
    ap.add_argument('--diff')
    ap.add_argument('--strip')
    a = ap.parse_args()
    paths = []
    while (not a.frames or len(paths) < a.frames) and os.path.exists('%s-%02d.png' % (a.prefix, len(paths))):
        paths.append('%s-%02d.png' % (a.prefix, len(paths)))
    if not paths:
        raise SystemExit('no frames at %s-NN.png' % a.prefix)
    box = tuple(int(v) for v in a.box.split(',')) if a.box else None
    frames = []
    for p in paths:
        im = Image.open(p).convert('L')
        frames.append(im.crop(box) if box else im)
    w, h = frames[0].size
    n = (w - 2) * (h - 2)
    T = a.thr
    stip, nyq = [], []
    for f in frames:
        L = f.tobytes()
        s = 0
        e = 0
        for y in range(1, h - 1):
            r = y * w
            for x in range(1, w - 1):
                c = L[r + x]
                u, d, l, rr = L[r - w + x], L[r + w + x], L[r + x - 1], L[r + x + 1]
                if (c - u > T and c - d > T) or (u - c > T and d - c > T) or \
                   (c - l > T and c - rr > T) or (l - c > T and rr - c > T):
                    s += 1
                e += abs(u + d - 2 * c) + abs(l + rr - 2 * c)
        stip.append(s / n)
        nyq.append(e / (2 * n))
    tog, chg = [], []
    for k in range(1, len(frames) - 1):
        p, c, q = (f.tobytes() for f in frames[k - 1:k + 2])
        t = 0
        for j in range(w * h):
            d1 = c[j] - p[j]
            d2 = q[j] - c[j]
            if (d1 > 24 and d2 < -24) or (d1 < -24 and d2 > 24):
                t += 1
        tog.append(t / (w * h))
    for k in range(len(frames) - 1):
        chg.append(sum(ImageChops.difference(frames[k], frames[k + 1]).tobytes()) / (w * h))
    for k in range(len(frames)):
        print('frame %02d: stipple %.4f%%  nyquist %.3f%s%s' % (
            k, 100 * stip[k], nyq[k],
            '  toggle %.4f%%' % (100 * tog[k - 1]) if 0 < k < len(frames) - 1 else '',
            '  change %.2f' % chg[k] if k < len(chg) else ''))
    print('total: %d frames, box %s: stipple mean %.4f%% max %.4f%%, nyquist mean %.3f, toggle mean %.4f%% max %.4f%%, '
          'change mean %.2f' % (len(frames), box or 'whole frame', 100 * sum(stip) / len(stip), 100 * max(stip),
                                sum(nyq) / len(nyq), 100 * sum(tog) / max(1, len(tog)),
                                100 * max(tog) if tog else 0, sum(chg) / max(1, len(chg))))
    if a.diff and len(frames) > 1:
        d = ImageChops.difference(frames[0], frames[1]).point(lambda v: min(255, 4 * v))
        d.save(a.diff)
    if a.strip:
        cols = min(6, len(paths))
        rows = (len(paths) + cols - 1) // cols
        out = Image.new('RGB', (cols * w, rows * h))
        for k, p in enumerate(paths):
            im = Image.open(p).convert('RGB')
            im = im.crop(box) if box else im
            out.paste(im, ((k % cols) * w, (k // cols) * h))
        out.save(a.strip)


if __name__ == '__main__':
    main()
