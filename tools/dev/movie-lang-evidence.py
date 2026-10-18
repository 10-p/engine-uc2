#!/usr/bin/env python3
"""Which audio track of a cinematic is the English one? Evidence from the production stems, no ears needed.

  tools/dev/movie-lang-evidence.py [--cooked <dir>]

For the cinematics whose English voice stem is in <epic-sources>/TestMaps (*-voice.wav): every audio track of
the shipped XMV (and of the cooked files: <Name>.ogv = track 0, <Name>_<lang>.oga = tracks 1-4) and the stems
are decoded to 8 kHz mono, band-limited to the voice band (200-3400 Hz). First the 50 ms level envelopes give a
coarse alignment (lags -3..+3 s); then the WAVEFORMS are cross-correlated (tools/dev/xcorr.c, lags +-100 ms
around it). Dubbing keeps the timing and loudness of the lines, so envelopes cannot tell the languages apart,
but only the English mix contains the English stem's waveform. The sfx stem (*-sfx.wav), shared by every
language, is the control: its waveform should correlate about equally with all five tracks.
Every decode runs inside <repo>/../bin/capped.
"""
import argparse, atexit, math, os, shutil, struct, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
work = os.path.abspath(os.path.join(here, '..', '..', '..'))
capped = os.path.join(work, 'bin', 'capped')
epic = os.environ.get('UC2_EPIC_SOURCES') or os.path.join(work, 'epic-sources', 'uc2-avaris-2226')

# cinematic XMV -> production stem prefix in TestMaps (matched by duration)
PAIRS = [
    ('03_AscPre3', 'AscPre3_comp2'),
    ('04_AscPre6', 'AscPre6_Road_to_Capitol'),
    ('05_AscPre7', 'AscPre7_UTT_Final'),
    ('07_Asc02', 'Asc02_Eternal'),
]
LANGS = ('int', 'deu', 'fra', 'ita', 'spa')


TMP = tempfile.mkdtemp(prefix='m9-lang-')
atexit.register(shutil.rmtree, TMP, True)
XCORR = os.path.join(TMP, 'xcorr')
subprocess.run(['gcc', '-O2', '-o', XCORR, os.path.join(here, 'xcorr.c'), '-lm'], check=True)
_n = [0]


def decode(path, stream=None, seconds=200):
    """8 kHz mono float32, voice band; returns (file, samples)."""
    _n[0] += 1
    out = os.path.join(TMP, 'a%d.f32' % _n[0])
    cmd = [capped, '1G', '300', '--', 'ffmpeg', '-v', 'error', '-y', '-i', path]
    if stream is not None:
        cmd += ['-map', '0:a:%d' % stream]
    cmd += ['-t', str(seconds), '-af', 'highpass=f=200,lowpass=f=3400', '-ac', '1', '-ar', '8000', '-f', 'f32le', out]
    subprocess.run(cmd, check=True)
    d = open(out, 'rb').read()
    return out, struct.unpack('<%df' % (len(d) // 4), d)


def envelope(x):
    w = 400
    return [10 * math.log10(sum(v * v for v in x[i:i + w]) / w + 1e-10) for i in range(0, len(x) - w, w)]


def wave_corr(fa, fb, coarse_lag_windows):
    lag = coarse_lag_windows * 400
    r = subprocess.run([XCORR, fa, fb, str(lag - 800), str(lag + 800)], stdout=subprocess.PIPE, check=True)
    v, l = r.stdout.split()
    return float(v), int(l) / 8000.0


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
    ap.add_argument('--cooked', default=os.path.join(work, 'assets', 'uc2-assets', 'Movies'))
    args = ap.parse_args()
    ok = True
    for xmv, stem in PAIRS:
        voice = os.path.join(epic, 'TestMaps', stem + '-voice.wav')
        sfx = os.path.join(epic, 'TestMaps', stem + '-sfx.wav')
        if not os.path.exists(voice):
            continue
        fv, xv = decode(voice)
        fs, xs = decode(sfx)
        ev, es = envelope(xv), envelope(xs)
        src = os.path.join(epic, 'Movies', xmv + '.xmv')
        rows = []
        for k, lang in enumerate(LANGS):
            ft, xt = decode(src, k)
            et = envelope(xt)
            cv, cs = corr(et, ev), corr(et, es)
            # waveform: the stem shifted against the track, around the envelope's lag (same for every track:
            # use track 0's so that every track is measured at the same place)
            if k == 0:
                lag_v, lag_s = cv[1], cs[1]
            wv, ws = wave_corr(ft, fv, lag_v), wave_corr(ft, fs, lag_s)
            cooked = os.path.join(args.cooked, xmv + ('.ogv' if k == 0 else '_%s.oga' % lang))
            wc = wave_corr(decode(cooked)[0], fv, lag_v) if os.path.exists(cooked) else None
            rows.append((lang, cv, wv, ws, wc))
        best = max(range(len(rows)), key=lambda i: rows[i][2][0])
        for i, (lang, cv, wv, ws, wc) in enumerate(rows):
            print('%-11s track %d (%s): envelope vs English voice r=%.3f | waveform vs English voice r=%.3f (lag %+.4f s)'
                  ' | waveform vs sfx stem r=%.3f | cooked file vs English voice r=%s%s' % (
                      xmv, i, lang, cv[0], wv[0], wv[1], ws[0], '%.3f' % wc[0] if wc else '-',
                      '   <- best' if i == best else ''))
        verdict = best == 0
        ok = ok and verdict
        print('EVIDENCE %s: the English voice stem correlates best with track %d (%s): %s' % (
            xmv, best, LANGS[best], 'track 0 is English, as XMVHelper.cpp:160-189 says' if verdict else 'MISMATCH'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
