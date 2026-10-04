#!/usr/bin/env python3
"""Is the music in a capture the expected track, at the expected level? (No ears needed.)

  tools/dev/audio-music-check.py <capture.wav> <source-track.wav> <expected-gain-dB> [<control-track.wav>]

Both files are decoded with ffmpeg (inside <WorkUC2>/bin/capped) to 8 kHz mono; their 50 ms level envelopes
(dB) are correlated over lags of 0..10 s to find where the track starts in the capture; then the energy ratio
from 4 s after that start (past a 3 s fade-in) over 40 s gives the mix gain applied to the track, compared with
the expected one. A control track should correlate clearly worse. Prints one CHECK line; exit 1 on failure.
"""
import math, os, struct, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
capped = os.path.join(here, '..', '..', '..', 'bin', 'capped')


def envelope(path, seconds=70):
    with tempfile.NamedTemporaryFile(suffix='.f32') as t:
        subprocess.run([capped, '1G', '120', '--', 'ffmpeg', '-v', 'error', '-y', '-i', path, '-t', str(seconds),
                        '-ac', '1', '-ar', '8000', '-f', 'f32le', t.name], check=True)
        d = open(t.name, 'rb').read()
    x = struct.unpack('<%df' % (len(d) // 4), d)
    w = 400
    return [10 * math.log10(sum(v * v for v in x[i:i + w]) / w + 1e-12) for i in range(0, len(x) - w, w)]


def best_lag(cap, src):
    best = (-2.0, 0)
    for lag in range(0, 200):
        a = cap[lag:lag + 600]
        b = src[:len(a)]
        n = min(len(a), len(b))
        if n < 100:
            break
        a, b = a[:n], b[:n]
        ma, mb = sum(a) / n, sum(b) / n
        den = math.sqrt(sum((p - ma) ** 2 for p in a) * sum((q - mb) ** 2 for q in b))
        c = sum((p - ma) * (q - mb) for p, q in zip(a, b)) / den if den else 0.0
        if c > best[0]:
            best = (c, lag)
    return best


def main():
    cap_path, src_path, expected = sys.argv[1], sys.argv[2], float(sys.argv[3])
    cap = envelope(cap_path, 120)
    src = envelope(src_path)
    corr, lag = best_lag(cap, src)
    st = int(4 / 0.05)
    a = cap[lag + st:lag + st + 800]
    b = src[st:st + len(a)]
    n = min(len(a), len(b))
    level = 10 * math.log10(sum(10 ** (v / 10) for v in a[:n]) / sum(10 ** (v / 10) for v in b[:n]))
    control = ''
    ok = corr > 0.7 and abs(level - expected) < 0.5
    if len(sys.argv) > 4:
        cc, _ = best_lag(cap, envelope(sys.argv[4]))
        control = ', control track %s correlates %.3f' % (os.path.basename(sys.argv[4]), cc)
        ok = ok and cc < corr - 0.3
    print('CHECK music %s in %s: envelope correlation %.3f at %.2f s, mix gain %.2f dB (expected %.2f dB, error %+.2f dB)%s: %s'
          % (os.path.basename(src_path), os.path.basename(cap_path), corr, lag * 0.05, level, expected,
             level - expected, control, 'PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
