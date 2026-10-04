#!/usr/bin/env python3
"""Which audio track of a cinematic is the English one? Evidence from the production stems, no ears needed.

  tools/dev/movie-lang-evidence.py [--cooked <dir>]

For the cinematics whose English voice stem is in <epic-sources>/TestMaps (*-voice.wav), correlate the stem's
50 ms level envelope (dB) with the envelope of every audio track of the shipped XMV (and of the cooked files:
<Name>.ogv = track 0, <Name>_<lang>.oga = tracks 1-4), over lags of -3..+3 s. The sfx stem (*-sfx.wav), shared
by every language, is the control: it should correlate about equally with all five tracks, while the English
voice stem should correlate best with the English track. Every decode runs inside <WorkUC2>/bin/capped.
"""
import argparse, math, os, struct, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
work = os.path.abspath(os.path.join(here, '..', '..', '..'))
capped = os.path.join(work, 'bin', 'capped')
epic = os.path.join(work, 'epic-sources')

# cinematic XMV -> production stem prefix in TestMaps (matched by duration: see docs/porting/analysis/video.md)
PAIRS = [
    ('03_AscPre3', 'AscPre3_comp2'),
    ('04_AscPre6', 'AscPre6_Road_to_Capitol'),
    ('05_AscPre7', 'AscPre7_UTT_Final'),
    ('07_Asc02', 'Asc02_Eternal'),
]
LANGS = ('int', 'deu', 'fra', 'ita', 'spa')


def envelope(path, stream=None, seconds=200):
    with tempfile.NamedTemporaryFile(suffix='.f32') as t:
        cmd = [capped, '1G', '300', '--', 'ffmpeg', '-v', 'error', '-y', '-i', path]
        if stream is not None:
            cmd += ['-map', '0:a:%d' % stream]
        cmd += ['-t', str(seconds), '-ac', '1', '-ar', '8000', '-f', 'f32le', t.name]
        subprocess.run(cmd, check=True)
        d = open(t.name, 'rb').read()
    x = struct.unpack('<%df' % (len(d) // 4), d)
    w = 400
    return [10 * math.log10(sum(v * v for v in x[i:i + w]) / w + 1e-10) for i in range(0, len(x) - w, w)]


def corr(a, b, maxlag=60):
    best = (-2.0, 0)
    for lag in range(-maxlag, maxlag + 1):
        if lag >= 0:
            x, y = a[lag:], b
        else:
            x, y = a, b[-lag:]
        n = min(len(x), len(y))
        if n < 200:
            continue
        x, y = x[:n], y[:n]
        mx, my = sum(x) / n, sum(y) / n
        den = math.sqrt(sum((p - mx) ** 2 for p in x) * sum((q - my) ** 2 for q in y))
        c = sum((p - mx) * (q - my) for p, q in zip(x, y)) / den if den else 0.0
        if c > best[0]:
            best = (c, lag)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cooked', default=os.path.join(work, 'assets', 'uc2-cooked', 'Movies'))
    args = ap.parse_args()
    ok = True
    for xmv, stem in PAIRS:
        voice = os.path.join(epic, 'TestMaps', stem + '-voice.wav')
        sfx = os.path.join(epic, 'TestMaps', stem + '-sfx.wav')
        if not os.path.exists(voice):
            continue
        ev, es = envelope(voice), envelope(sfx)
        src = os.path.join(epic, 'Movies', xmv + '.xmv')
        rows = []
        for k, lang in enumerate(LANGS):
            et = envelope(src, k)
            cooked = os.path.join(args.cooked, xmv + ('.ogv' if k == 0 else '_%s.oga' % lang))
            ec = envelope(cooked) if os.path.exists(cooked) else None
            rows.append((lang, corr(et, ev), corr(et, es), corr(ec, ev) if ec else None))
        best = max(range(len(rows)), key=lambda i: rows[i][1][0])
        for i, (lang, cv, cs, cc) in enumerate(rows):
            print('%-11s track %d (%s): English voice stem r=%.3f (lag %+.2f s)  sfx stem r=%.3f  cooked file vs voice '
                  'r=%s%s' % (xmv, i, lang, cv[0], cv[1] * 0.05, cs[0],
                              '%.3f' % cc[0] if cc else '-', '   <- best' if i == best else ''))
        verdict = best == 0
        ok = ok and verdict
        print('EVIDENCE %s: the English voice stem correlates best with track %d (%s): %s' % (
            xmv, best, LANGS[best], 'track 0 is English, as XMVHelper.cpp:160-189 says' if verdict else 'MISMATCH'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
