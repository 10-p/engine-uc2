#!/usr/bin/env python3
"""H-1: a numeric model of the bloom + emissive chain of UCCameraEffects.cpp (UUCScreenEmissiveWithBloom), to see
what the halo of a small light looks like on screen at a given resolution, and what changes when the blur targets
get more texels (a post-process scale M) or the final upsampling gets smoother. Notebook:
docs/porting/analysis/h1-bloom.md.

Units: pixels of the 480-line reference picture ("ref px"); the screen has k = H/480 screen pixels per ref px. The
chain, as the port runs it today (pp-scale, PostProcessReferenceHeight 480), with M = 1:

  ref copy R (480 lines)  --select-->  S  (1/4 of R: one bilinear read at the middle of each 4x4 block)
  R  --emissive select (box, taps +-2 ref px)-->  E (1/4)   --feedback x2: V 1221 (E -> E/2 in y), H 1221 (-> /2 in x),
                                                             box up (+-1 texel) back into E, added on pass 2
  S += E ;  S --H 1221, /2 in x--> B0 --V 1221, /2 in y--> B1 (1/8 of R)  --bilinear--> screen (added, alpha 128)

FilterCopyRenderTarget's taps are offsets in SOURCE texels (1221: +-1 weight 2/6, +-3 weight 1/6; box: (+-1,+-1)
0.25 each); destination pixel (x,y) reads at ((x+.5)/w, (y+.5)/h) (UC2GLPostProcess.cpp, convention 4).

Variants (the prototype's choices):
  M        the blur targets have M times the texels per axis (M=1: today)
  taps     'texel': tap offsets stay in texels (the blur shrinks with M; what a higher PostProcessReferenceHeight does)
           'scaled': offsets x M texels (same distance on screen), each bilinear tap then covers 1/M of what it did
           'filled': as scaled, plus a fill pass before each target-to-target pass, M point taps at spacing 2 texels
                     along the pass's axis (2-D for the box filters), so every scaled tap covers the same 2 coarse
                     texels as at M=1 (the kernel of the Xbox at M times the sampling density)
  up       final upsampling to the screen: 'bilinear' (today), 'bspline' (cubic B-spline), 'bc' (cubic with
           B=0.5 C=0.25: same second moment as bilinear, i.e. the same added spread), 'catrom' (Catmull-Rom)

Measures, on the screen row through the light, for a light of one screen pixel at many sub-positions:
  peak, sum (energy), sigma (sqrt of the second moment), r10/r50 (radius where the mean halo falls to 10%/50% of
  its peak), all in screen pixels; 'jitter' = spread (max-min)/mean of the peak over the sub-positions (how much the
  halo changes as the light moves across the coarse grid: shimmer); 'kink' = largest |second difference| of the
  mean profile relative to the peak, times (sigma)^2 (dimensionless: a smooth Gaussian gives about 1, a profile made
  of straight segments with corners gives more: the blocks).

    h1-bloom-model.py table [H]        every variant at screen height H (default 1440)
    h1-bloom-model.py profile H M taps up [phase]   the mean profile (or one sub-position), one value per screen px
Pure Python (no numpy on this machine).
"""
import math
import sys

TAPS_1221_H = [(3, 0, 1 / 6), (1, 0, 2 / 6), (-1, 0, 2 / 6), (-3, 0, 1 / 6)]
TAPS_1221_V = [(0, o, w) for (o, _, w) in TAPS_1221_H]
TAPS_BOX = [(-1, -1, .25), (1, -1, .25), (-1, 1, .25), (1, 1, .25)]


def bil(T, x, y):
    """Bilinear read of texture T (rows of texels; texel (i,j) centre at (i+.5, j+.5)), clamped edges."""
    h = len(T); w = len(T[0])
    qx = x - .5; qy = y - .5
    i0 = math.floor(qx); j0 = math.floor(qy)
    fx = qx - i0; fy = qy - j0
    i1 = min(max(i0 + 1, 0), w - 1); i0 = min(max(i0, 0), w - 1)
    j1 = min(max(j0 + 1, 0), h - 1); j0 = min(max(j0, 0), h - 1)
    a = T[j0][i0] * (1 - fx) + T[j0][i1] * fx
    b = T[j1][i0] * (1 - fx) + T[j1][i1] * fx
    return a * (1 - fy) + b * fy


def fpass(src, ts_src, dw, dh, ts_dst, taps):
    """FilterCopyRenderTarget: dst (dw x dh texels of ts_dst ref px) from src (texels of ts_src ref px)."""
    out = []
    for j in range(dh):
        cy = (j + .5) * ts_dst / ts_src
        row = []
        for i in range(dw):
            cx = (i + .5) * ts_dst / ts_src
            row.append(sum(w * bil(src, cx + ox, cy + oy) for (ox, oy, w) in taps))
        out.append(row)
    return out


def scale_taps(taps, s):
    return [(ox * s, oy * s, w) for (ox, oy, w) in taps]


# Below: every pass works in texel units of its source (FilterCopyRenderTarget's convention), so a pass is given by
# its destination size and its taps; the halvings of the chain make x and y texel sizes differ.


def fill_taps(M, axes):
    """M point taps at spacing 2 along each axis in axes ('x', 'y' or 'xy'), equal weights (a box of 2M texels once
    combined with a bilinear tap on a texel boundary)."""
    offs = [2 * j - M + 1 for j in range(M)]
    xs = offs if 'x' in axes else [0]
    ys = offs if 'y' in axes else [0]
    n = len(xs) * len(ys)
    return [(ox, oy, 1.0 / n) for ox in xs for oy in ys]


def chain(R, M, mode):
    """Bloom + emissive of a light in the reference copy R (square, side n ref px). Returns the final target (the
    bloom's B1) and its texel size in ref px. The emissive selection is driven with the same light (an emissive
    lamp is both bright and emissive); the two parts are returned separately so they can be weighed."""
    n = len(R)
    ts_s = 4.0 / M                     # select / emissive target texel, ref px
    ns = int(round(n / ts_s))
    # bloom select: one bilinear read of R per texel (linear part above the threshold)
    S = fpass(R, 1.0, ns, ns, ts_s, [(0, 0, 1.0)])
    # emissive select: box taps +-2 ref px of R (a back-buffer source: taps in reference pixels, as today)
    E = fpass(R, 1.0, ns, ns, ts_s, scale_taps(TAPS_BOX, 2.0))
    F = [row[:] for row in E]
    for p in range(2):
        B1 = vpass(F, ns, M, mode)
        B2 = hpass(B1, M, mode)
        U = upbox(B2, ns, M, mode)
        if p == 0:
            F = U
        else:
            F = [[a + b for a, b in zip(r1, r2)] for r1, r2 in zip(F, U)]
    Sb = [[a for a in row] for row in S]
    B0 = hpass(Sb, M, mode)
    B1 = vpass2(B0, M, mode)
    Fe0 = hpass(F, M, mode)
    Fe1 = vpass2(Fe0, M, mode)
    return B1, Fe1


# Anisotropic passes: textures as rows; x texel size and y texel size differ after a halving. Coordinates in
# texel units of the source, so only the ratio of sizes matters: we work in source-texel units directly.

def gpass(src, dw, dh, taps):
    sw = len(src[0]); sh = len(src)
    out = []
    for j in range(dh):
        cy = (j + .5) * sh / dh
        row = []
        for i in range(dw):
            cx = (i + .5) * sw / dw
            row.append(sum(w * bil(src, cx + ox, cy + oy) for (ox, oy, w) in taps))
        out.append(row)
    return out


def prep(src, M, mode, axes):
    if mode == 'filled' and M > 1:
        return gpass(src, len(src[0]), len(src), fill_taps(M, axes))
    return src


def tscale(taps, M, mode):
    return scale_taps(taps, M) if (mode in ('scaled', 'filled') and M > 1) else taps


def vpass(F, ns, M, mode):          # E-sized -> half height, vertical 1221
    s = prep(F, M, mode, 'y')
    return gpass(s, len(F[0]), len(F) // 2, tscale(TAPS_1221_V, M, mode))


def hpass(B, M, mode):              # -> half width, horizontal 1221
    s = prep(B, M, mode, 'x')
    return gpass(s, len(B[0]) // 2, len(B), tscale(TAPS_1221_H, M, mode))


def vpass2(B, M, mode):             # -> half height, vertical 1221
    s = prep(B, M, mode, 'y')
    return gpass(s, len(B[0]), len(B) // 2, tscale(TAPS_1221_V, M, mode))


def upbox(B2, ns, M, mode):         # box up into the E-sized target, taps +-1 source texel
    s = prep(B2, M, mode, 'xy')
    return gpass(s, ns, ns, tscale(TAPS_BOX, M, mode))


def cubic_w(t, B, C):
    t = abs(t)
    if t < 1:
        return ((12 - 9 * B - 6 * C) * t ** 3 + (-18 + 12 * B + 6 * C) * t ** 2 + (6 - 2 * B)) / 6
    if t < 2:
        return ((-B - 6 * C) * t ** 3 + (6 * B + 30 * C) * t ** 2 + (-12 * B - 48 * C) * t + (8 * B + 24 * C)) / 6
    return 0.0


UPS = {'bspline': (1.0, 0.0), 'bc': (0.5, 0.25), 'catrom': (0.0, 0.5)}


def sample_row(T, x, y, up):
    if up == 'bilinear':
        return bil(T, x, y)
    B, C = UPS[up]
    h = len(T); w = len(T[0])
    qx = x - .5; qy = y - .5
    i0 = math.floor(qx); j0 = math.floor(qy)
    s = 0.0
    for dj in range(-1, 3):
        wy = cubic_w(qy - (j0 + dj), B, C)
        if wy == 0:
            continue
        jj = min(max(j0 + dj, 0), h - 1)
        for di in range(-1, 3):
            wx = cubic_w(qx - (i0 + di), B, C)
            ii = min(max(i0 + di, 0), w - 1)
            s += wx * wy * T[jj][ii]
    return s


def light(n, x0, y0, size):
    """Reference copy of a square light of brightness 1, side `size` ref px, top-left corner at (x0, y0) ref px:
    each ref px holds the share of it the light covers (the reference copy is an area average)."""
    R = [[0.0] * n for _ in range(n)]
    for j in range(int(y0) - 1, int(y0 + size) + 2):
        oy = max(0.0, min(j + 1, y0 + size) - max(j, y0))
        for i in range(int(x0) - 1, int(x0 + size) + 2):
            ox = max(0.0, min(i + 1, x0 + size) - max(i, x0))
            R[j][i] = ox * oy
    return R


def run(H, M, mode, up, size=2.0, phases=3, n=144):
    """The halo of a size x size ref-px light, for phases x phases positions of the light across one 8-ref-px cell
    of the final target; the screen row through the light's middle, cut to +-60 ref px around it."""
    k = H / 480.0
    profiles = []
    peaks = []
    kinks = []
    for py in range(phases):
        for px in range(phases):
            x0 = n / 2 + 8.0 * px / phases
            y0 = n / 2 + 8.0 * py / phases
            R = light(n, x0, y0, size)
            B1, Fe1 = chain(R, M, mode)
            T = [[a + b for a, b in zip(r1, r2)] for r1, r2 in zip(B1, Fe1)]
            tsx = n / len(T[0]); tsy = n / len(T)
            yref = y0 + size / 2
            cx = (x0 + size / 2) * k
            prof = []
            for sx in range(int(cx - 60 * k), int(cx + 60 * k) + 1):
                xref = (sx + .5) / k
                prof.append(sample_row(T, xref / tsx, yref / tsy, up) * 128 / 255)
            profiles.append(prof)
            pk = max(prof)
            peaks.append(pk)
            kinks.append(max(abs(prof[i - 1] - 2 * prof[i] + prof[i + 1]) for i in range(1, len(prof) - 1)) / pk if pk > 0 else 0)
    L = min(len(p) for p in profiles)
    mean = [sum(p[i] for p in profiles) / len(profiles) for i in range(L)]
    c = L // 2
    pk = max(mean)
    tot = sum(mean)
    sig = math.sqrt(sum(v * (i - c) ** 2 for i, v in enumerate(mean)) / tot)

    def radius(frac):
        lo = c
        while lo > 0 and mean[lo] >= frac * pk:
            lo -= 1
        hi = c
        while hi < L - 1 and mean[hi] >= frac * pk:
            hi += 1
        return (hi - lo) / 2.0

    kink = sum(kinks) / len(kinks) * sig * sig
    jitter = (max(peaks) - min(peaks)) / (sum(peaks) / len(peaks))
    return dict(peak=pk, sum=tot, sigma=sig, r10=radius(.1), r50=radius(.5), kink=kink, jitter=jitter, mean=mean)


def cmd_table(args):
    H = int(args[0]) if args else 1440
    rows = [(1, 'texel', 'bilinear'), (1, 'texel', 'bspline'), (1, 'texel', 'bc'), (1, 'texel', 'catrom'),
            (2, 'texel', 'bilinear'), (2, 'scaled', 'bilinear'), (2, 'filled', 'bilinear'), (2, 'filled', 'bc'),
            (3, 'filled', 'bilinear'), (4, 'filled', 'bilinear')]
    if len(args) > 1:
        rows = [r for r in rows if r[0] <= int(args[1])]
    print('H=%d (k=%.2f screen px per ref px); halo of a %.0fx%.0f ref-px light, screen px' % (H, H / 480.0, SIZE, SIZE))
    print('%-3s %-7s %-9s %8s %8s %7s %6s %6s %6s %7s' % ('M', 'taps', 'up', 'peak', 'sum', 'sigma', 'r10', 'r50', 'kink', 'jitter'))
    for (M, mode, up) in rows:
        r = run(H, M, mode, up, SIZE)
        print('%-3d %-7s %-9s %8.5f %8.4f %7.2f %6.1f %6.1f %6.2f %7.3f' % (M, mode, up, r['peak'], r['sum'], r['sigma'], r['r10'], r['r50'], r['kink'], r['jitter']))
        sys.stdout.flush()


def cmd_profile(args):
    H, M, mode, up = int(args[0]), int(args[1]), args[2], args[3]
    r = run(H, M, mode, up)
    print(' '.join('%.5f' % v for v in r['mean']))


SIZE = float(__import__('os').environ.get('H1_LIGHT', '2'))

if __name__ == '__main__':
    {'table': cmd_table, 'profile': cmd_profile}[sys.argv[1]](sys.argv[2:])
