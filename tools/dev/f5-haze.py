#!/usr/bin/env python3
"""DM-Azure sky haze A/B: numbers and a sheet from the frames
tools/dev/f5-flicker.sh made of one NETPLACE view on three renderers.

  f5-haze.py <dir> <out.png> [<desktop suffix>]      e.g. "-fix": the desktop frames of another executable

Reads <dir>/{desk-gpu,twin-mesa,desk-mesa}[-nopp]-frozen-01.png (desktop device on the GPU, GLES twin on Mesa
llvmpipe, desktop device on Mesa llvmpipe; post-processing on, and off with "-nopp"). Prints, per pair, the mean and
largest difference (0-255, mean of R, G, B) in the sky box and the far-mountain box, and writes a sheet: rows
post-processing on/off; columns desktop GPU, twin, desktop Mesa, |desktop GPU - twin| x4, |desktop Mesa - twin| x4.
"""
import sys
from PIL import Image, ImageChops, ImageDraw, ImageStat

SKY = (590, 10, 740, 230)        # open sky between the buildings, 1280x720
FAR = (612, 345, 732, 412)       # the far mountains under the bridge


def stats(a, b, box):
    d = ImageChops.difference(a.crop(box), b.crop(box))
    mean = sum(ImageStat.Stat(d).mean) / 3
    mx = max(hi for lo, hi in d.getextrema())
    return mean, mx


def main():
    src, out = sys.argv[1], sys.argv[2]
    dsuf = sys.argv[3] if len(sys.argv) > 3 else ''
    names = ['desk-gpu', 'twin-mesa', 'desk-mesa']
    files = {'desk-gpu': 'desk-gpu' + dsuf, 'twin-mesa': 'twin-mesa', 'desk-mesa': 'desk-mesa' + dsuf}
    rows = []
    for suffix, label in (('', 'post-processing on'), ('-nopp', 'post-processing off')):
        ims = {}
        for n in names:
            f = files[n] if n == 'twin-mesa' else (files[n][:len(n)] + suffix + files[n][len(n):])
            ims[n] = Image.open('%s/%s%s-frozen-01.png' % (src, f, '' if n != 'twin-mesa' else suffix)).convert('RGB')
        for n in names:
            m = ImageStat.Stat(ims[n].crop(SKY)).mean
            print('%-20s %-10s sky mean RGB (%.1f, %.1f, %.1f)' % (label, n, m[0], m[1], m[2]))
        for a, b in (('desk-gpu', 'twin-mesa'), ('desk-mesa', 'twin-mesa'), ('desk-gpu', 'desk-mesa')):
            s, f = stats(ims[a], ims[b], SKY), stats(ims[a], ims[b], FAR)
            print('%-20s %s vs %s: sky mean %.2f max %d; far mountains mean %.2f max %d'
                  % (label, a, b, s[0], s[1], f[0], f[1]))
        rows.append((label, ims))
    w, h = 384, 216
    sheet = Image.new('RGB', (5 * w, 2 * (h + 18)), (0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    titles = ['desktop, GPU (NVIDIA)', 'GLES twin (GL4ES, Mesa)', 'desktop, Mesa llvmpipe', '|GPU - twin| x4',
              '|Mesa desktop - twin| x4']
    for r, (label, ims) in enumerate(rows):
        y = r * (h + 18)
        cells = [ims['desk-gpu'], ims['twin-mesa'], ims['desk-mesa'],
                 ImageChops.difference(ims['desk-gpu'], ims['twin-mesa']).point(lambda v: min(255, 4 * v)),
                 ImageChops.difference(ims['desk-mesa'], ims['twin-mesa']).point(lambda v: min(255, 4 * v))]
        for c, im in enumerate(cells):
            sheet.paste(im.resize((w, h)), (c * w, y + 18))
            draw.text((c * w + 4, y + 3), '%s; %s' % (titles[c], label), fill=(255, 255, 255))
            for box, col in ((SKY, (255, 64, 64)), (FAR, (64, 160, 255))):
                draw.rectangle([c * w + box[0] * w // 1280, y + 18 + box[1] * h // 720,
                                c * w + box[2] * w // 1280, y + 18 + box[3] * h // 720], outline=col)
    sheet.save(out)


if __name__ == '__main__':
    main()
