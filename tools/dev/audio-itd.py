#!/usr/bin/env python3
"""audio-itd.py: interaural differences in a stereo capture (F-6, HRTF on/off).

Plain stereo panning (OpenAL Soft without HRTF) only changes the level between the channels; HRTF also delays the
far ear by a fraction of a millisecond. This cuts the capture into windows, keeps the clearly lateral ones (one
channel at least --ild dB louder than the other, above a level floor), takes the --top most lateral, and finds in
each the lag (within +-1 ms) that maximises the normalised cross-correlation of the two channels' first
differences (a pre-emphasis: game sounds are bass-heavy, and the plain correlation of low frequencies is nearly flat
over +-1 ms, so its maximum would wander). Positive "near ear leads" =
the louder channel is also the earlier one.

  tools/dev/audio-itd.py <capture.wav> [--window 1024] [--ild 3] [--top 150]

Prints a histogram and one SUMMARY line. Standard library only (a 1-3 minute capture takes some seconds).
"""

import argparse
import math
import struct
import sys


def read_wav(path):
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != b'RIFF' or d[8:12] != b'WAVE':
        raise SystemExit('%s: not RIFF/WAVE' % path)
    i, fmt, data = 12, None, None
    while i + 8 <= len(d):
        cid, ln = d[i:i + 4], struct.unpack_from('<I', d, i + 4)[0]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', d, i + 8)
            if fmt[0] == 0xFFFE:
                sub = struct.unpack_from('<H', d, i + 8 + 24)[0]
                fmt = (sub,) + fmt[1:]
        elif cid == b'data':
            data = d[i + 8:i + 8 + min(ln, len(d) - i - 8)] if ln != 0xFFFFFFFF and ln else d[i + 8:]
        i += 8 + ln + (ln & 1) if ln not in (0, 0xFFFFFFFF) else len(d)
    tag, ch, rate, _, align, bits = fmt
    if ch != 2:
        raise SystemExit('%s: %d channels (stereo expected)' % (path, ch))
    n = len(data) // (bits // 8)
    if tag == 3 and bits == 32:
        s = struct.unpack('<%df' % n, data[:4 * n])
    elif tag == 1 and bits == 16:
        s = [v / 32768.0 for v in struct.unpack('<%dh' % n, data[:2 * n])]
    else:
        raise SystemExit('%s: format tag %d, %d bits not read' % (path, tag, bits))
    return rate, s[0::2], s[1::2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('--window', type=int, default=1024)
    ap.add_argument('--ild', type=float, default=3.0)
    ap.add_argument('--top', type=int, default=150)
    ap.add_argument('--floor', type=float, default=-60.0, help='window level floor, dBFS')
    args = ap.parse_args()
    rate, L, R = read_wav(args.wav)
    dL = [0.0] + [L[i] - L[i - 1] for i in range(1, len(L))]
    dR = [0.0] + [R[i] - R[i - 1] for i in range(1, len(R))]
    maxlag = rate // 1000
    W = args.window
    cands = []
    for w in range(maxlag, len(L) - W - maxlag, W):
        el = sum(v * v for v in L[w:w + W])
        er = sum(v * v for v in R[w:w + W])
        if el + er <= 0:
            continue
        lvl = 10 * math.log10((el + er) / (2 * W))
        if lvl < args.floor or min(el, er) <= 0:
            continue
        ild = 10 * math.log10(er / el)
        if abs(ild) >= args.ild:
            cands.append((abs(ild), w, ild))
    cands.sort(reverse=True)
    cands = cands[:args.top]
    hist = {'|lag|<=2': 0, 'near ear leads 3..7': 0, 'near ear leads 8..%d' % maxlag: 0, 'other': 0}
    leads = []
    for _, w, ild in cands:
        best, lag = -1e30, 0
        a = dL[w:w + W]
        ea = sum(x * x for x in a)
        for k in range(-maxlag, maxlag + 1):
            b = dR[w + k:w + k + W]
            eb = sum(x * x for x in b)
            c = sum(x * y for x, y in zip(a, b)) / math.sqrt(ea * eb) if ea > 0 and eb > 0 else -1.0
            if c > best:
                best, lag = c, k
        near = -lag if ild > 0 else lag        # right louder: right earlier = left late = negative lag
        leads.append(near)
        if abs(lag) <= 2:
            hist['|lag|<=2'] += 1
        elif 3 <= near <= 7:
            hist['near ear leads 3..7'] += 1
        elif 8 <= near <= maxlag:
            hist['near ear leads 8..%d' % maxlag] += 1
        else:
            hist['other'] += 1
    leads.sort()
    med = leads[len(leads) // 2] if leads else 0
    print('%s: %d Hz, %.1f s, %d lateral windows of %d frames (|ILD| >= %.1f dB, level >= %.0f dBFS), %d analysed' % (
        args.wav, rate, len(L) / float(rate), len(cands) if len(cands) < args.top else args.top, W, args.ild,
        args.floor, len(leads)))
    for k, v in hist.items():
        print('  %-24s %4d' % (k, v))
    n = max(1, len(leads))
    print('SUMMARY itd %s: %d windows, near ear leads by >= 8 frames in %.0f%%, |lag| <= 2 in %.0f%%, median lead %d '
          'frames (%.3f ms)' % (args.wav.rsplit('/', 1)[-1], len(leads), 100.0 * hist['near ear leads 8..%d' % maxlag] / n,
                                100.0 * hist['|lag|<=2'] / n, med, 1000.0 * med / rate))


if __name__ == '__main__':
    sys.exit(main())
