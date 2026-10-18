#!/usr/bin/env python3
"""Flicker measure over consecutive frames.

  f5-flicker.py <prefix> [--frames N] [--box x0,y0,x1,y1] [--thr T] [--map out.png] [--strip out.png]

<prefix>-NN.png are consecutive frames (tools/dev/f5-flicker.sh). A pixel *flickers* at frame i when its luminance
goes one way from frame i-1 to i and back from i to i+1, both steps larger than T levels (default 24): an up-down-up
toggle. A smooth camera move or the slow sway of the leaf shadows changes pixels monotonically from frame to frame
and does not count; z-fighting, a projection that is drawn every other frame, a shadow that jumps between two
directions do. Printed per frame: the share of flickering pixels in the box and the mean absolute change; at the
end the totals. --map writes where it happened (white = flickered in many frames), --strip a strip of the box.
Python standard library + Pillow only.
"""
import argparse
import os
from PIL import Image, ImageChops


def lum(path, box):
    im = Image.open(path).convert('L')
    return im.crop(box) if box else im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prefix')
    ap.add_argument('--frames', type=int, default=0)
    ap.add_argument('--box')
    ap.add_argument('--thr', type=int, default=24)
    ap.add_argument('--map')
    ap.add_argument('--strip')
    a = ap.parse_args()
    box = tuple(int(v) for v in a.box.split(',')) if a.box else None
    paths = []
    i = 0
    while (not a.frames or i < a.frames) and os.path.exists('%s-%02d.png' % (a.prefix, i)):
        paths.append('%s-%02d.png' % (a.prefix, i))
        i += 1
    if len(paths) < 3:
        raise SystemExit('need at least 3 frames at %s-NN.png' % a.prefix)
    frames = [lum(p, box) for p in paths]
    w, h = frames[0].size
    n = w * h
    count = bytearray(n)
    shares = []
    changes = []
    for k in range(1, len(frames) - 1):
        p, c, q = (f.tobytes() for f in frames[k - 1:k + 2])
        hits = 0
        for j in range(n):
            d1 = c[j] - p[j]
            d2 = q[j] - c[j]
            if (d1 > a.thr and d2 < -a.thr) or (d1 < -a.thr and d2 > a.thr):
                hits += 1
                if count[j] < 255:
                    count[j] += 1
        shares.append(hits / n)
        diff = ImageChops.difference(frames[k], frames[k + 1]).tobytes()
        changes.append(sum(diff) / n)
        print('frame %02d: flicker %.4f%%  mean change %.2f' % (k, 100 * hits / n, changes[-1]))
    print('total: %d frames, box %s, threshold %d: flicker mean %.4f%% max %.4f%%, mean change %.2f, pixels that '
          'flickered at least twice %.4f%%' % (len(frames), box or 'whole frame', a.thr, 100 * sum(shares) / len(shares),
                                               100 * max(shares), sum(changes) / len(changes),
                                               100 * sum(1 for v in count if v >= 2) / n))
    if a.map:
        scale = max(1, 255 // max(1, len(frames) - 2))
        Image.frombytes('L', (w, h), bytes(min(255, v * scale) for v in count)).save(a.map)
    if a.strip:
        cols = min(6, len(paths))
        rows = (len(paths) + cols - 1) // cols
        tw, th = (w // 2, h // 2) if w > 640 else (w, h)
        out = Image.new('RGB', (cols * tw, rows * th))
        for k, p in enumerate(paths):
            im = Image.open(p).convert('RGB')
            im = im.crop(box) if box else im
            out.paste(im.resize((tw, th)), ((k % cols) * tw, (k // cols) * th))
        out.save(a.strip)


if __name__ == '__main__':
    main()
