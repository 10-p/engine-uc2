#!/usr/bin/env python3
"""High-frequency energy of a picture region (F-3, texture LOD bias: is a far texture sharper?).

  tools/dev/hf-energy.py A.png [B.png ...] --box x0,y0,x1,y1 [--crop out.png --scale N]

For each picture prints the mean squared 4-neighbour Laplacian of the luminance (0..255) inside the box: the
energy of detail at the finest scale, which a texture sampled from a finer mip level (negative LOD bias) has
more of. One number per picture; compare pictures of the same view. --crop writes the boxes of all pictures
side by side, enlarged N times (nearest neighbour), for looking at. Only Pillow is needed (no numpy here).
"""
import sys
from PIL import Image

def energy(img, box):
    x0, y0, x1, y1 = box
    g = img.convert('L')
    w, h = g.size
    px = g.load()
    total = 0.0; n = 0
    for y in range(max(y0, 1), min(y1, h - 1)):
        for x in range(max(x0, 1), min(x1, w - 1)):
            lap = 4 * px[x, y] - px[x - 1, y] - px[x + 1, y] - px[x, y - 1] - px[x, y + 1]
            total += lap * lap; n += 1
    return total / max(n, 1)

def main():
    args = sys.argv[1:]; files = []; box = None; crop = None; scale = 3
    i = 0
    while i < len(args):
        if args[i] == '--box': box = tuple(int(v) for v in args[i + 1].split(',')); i += 2
        elif args[i] == '--crop': crop = args[i + 1]; i += 2
        elif args[i] == '--scale': scale = int(args[i + 1]); i += 2
        else: files.append(args[i]); i += 1
    if not files or not box:
        print(__doc__); return 2
    crops = []
    for f in files:
        img = Image.open(f).convert('RGB')
        print('%s: hf-energy=%.2f box=%s' % (f, energy(img, box), ','.join(map(str, box))))
        crops.append(img.crop(box))
    if crop:
        cw, ch = crops[0].size
        out = Image.new('RGB', (cw * scale * len(crops) + 4 * (len(crops) - 1), ch * scale), (255, 0, 255))
        for k, c in enumerate(crops):
            out.paste(c.resize((cw * scale, ch * scale), Image.NEAREST), (k * (cw * scale + 4), 0))
        out.save(crop)
    return 0

if __name__ == '__main__':
    sys.exit(main())
