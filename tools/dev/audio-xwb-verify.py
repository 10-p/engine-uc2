#!/usr/bin/env python3
"""audio-xwb-verify.py: check tools/uc2xwb.py's decoder against the project's source WAVs.

For every entry of the named wave banks that has a source WAV in SoundData/, decode the shipped .xwb entry and
compare it with the source: length (the decode must be the source length rounded up to a 64-frame ADPCM block),
signal-to-noise ratio at shift 0 (a wrong block layout drifts by a frame per block and gives ~0 dB or less), and
samples at full scale in the decode and in the source (does the decoder clip where the source does not?).

  tools/dev/audio-xwb-verify.py [--epic <epic-sources>] A_Combo A_Gameplay ...

Run under the ceiling: <WorkUC2>/bin/capped 2G 1800 -- python3 tools/dev/audio-xwb-verify.py ...
"""

import argparse
import importlib.util
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import uc2xwb  # noqa: E402

spec = importlib.util.spec_from_file_location('cook', os.path.join(HERE, '..', 'cook-audio.py'))
cook = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cook)


def main():
    work = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
    ap = argparse.ArgumentParser()
    ap.add_argument('--epic', default=os.path.join(work, 'epic-sources'))
    ap.add_argument('banks', nargs='+')
    args = ap.parse_args()
    root = cook.parse_xap(os.path.join(args.epic, 'Sounds', 'Avaris.xap'))
    idx = cook.SourceIndex(args.epic)
    total = bad = 0
    for wb in (cook.WaveBank(n) for n in root.kids('Wave Bank')):
        if wb.stem not in args.banks:
            continue
        bank = uc2xwb.read_xwb(os.path.join(args.epic, 'Sounds', wb.stem + '.xwb'))
        snrs, extra, fs_dec, fs_src, n = [], [], 0, 0, 0
        for e in wb.entries:
            rel = os.path.normpath(os.path.join('Sounds', e.file.replace('\\', '/'))).replace(os.sep, '/')
            path = idx.find(rel)
            if not path:
                continue
            try:
                _, sch, ref = uc2xwb.read_wav_pcm16(path)
            except uc2xwb.XwbError:
                continue            # AIFF or 8-bit sources: not compared
            rate, ch, dec = uc2xwb.decode_entry(bank, e.index)
            if ch != sch:
                print('  %s entry %d: %d ch decoded, source %d ch' % (wb.stem, e.index, ch, sch))
                bad += 1
                continue
            m = min(len(dec), len(ref))
            sig = sum(x * x for x in ref[:m])
            err = sum((a - b) * (a - b) for a, b in zip(dec[:m], ref[:m]))
            snr = 10 * math.log10(sig / err) if err and sig else 99.0
            pad = (len(dec) - len(ref)) // ch
            snrs.append(snr)
            extra.append(pad)
            fs_dec += sum(1 for x in dec[:m] if x >= 32767 or x <= -32768)
            fs_src += sum(1 for x in ref[:m] if x >= 32767 or x <= -32768)
            n += 1
            if not (0 <= pad < 64) or snr < 8:
                bad += 1
                print('  %s entry %d %s: SNR %.1f dB, decode - source = %d frames' % (wb.stem, e.index, e.name, snr,
                                                                                         pad))
        total += n
        snrs.sort()
        if n:
            print('%s: %d entries compared; SNR min %.1f median %.1f max %.1f dB; decode - source %d..%d frames; '
                  'samples at full scale: decode %d, source %d' % (wb.stem, n, snrs[0], snrs[n // 2], snrs[-1],
                                                                  min(extra), max(extra), fs_dec, fs_src))
    print('compared %d entries, %d outside (length 0..63 frames longer, SNR >= 8 dB)' % (total, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
