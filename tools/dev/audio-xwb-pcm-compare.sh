#!/usr/bin/env bash
# M16: the C++ Xbox ADPCM/PCM decoder of UC2ALAudio against tools/uc2xwb.py, byte for byte: every wave of the given
# banks decoded by `ucc-bin UC2ALAudio.UC2AudioCheck PCM=<dir> PCMBANKS=<a+b+c>` (16-bit little-endian) and by
# uc2xwb.decode_entry, compared as bytes.
#
#   tools/dev/audio-xwb-pcm-compare.sh <tree> <bank,bank,...>     e.g. m16-xbox A_Weapon,A_Announcer_spa,A_Character
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:?tree}; banks=${2:?banks}
sys=$work/run/$tree/System
out=$work/logs/$tree
pcm=$out/pcm
rm -rf "$pcm"; mkdir -p "$pcm"
( cd "$sys" && "$work/bin/capped" 2G 1200 -- ./ucc-bin UC2ALAudio.UC2AudioCheck "PCM=$pcm" "PCMBANKS=${banks//,/+}" > "$out/pcm-check.out" 2>&1 )
grep -a '^PCM:' "$out/pcm-check.out"
"$work/bin/capped" 2G 1800 -- env REPO="$repo" python3 - "$sys/../Sounds" "$pcm" "$banks" <<'PY'
import os, struct, sys
sys.path.insert(0, os.path.join(os.environ['REPO'], 'tools'))
import uc2xwb
sounds, pcm, banks = sys.argv[1], sys.argv[2], sys.argv[3].split(',')
total_w = total_b = bad = 0
for b in banks:
    bank = uc2xwb.read_xwb(os.path.join(sounds, b + '.xwb'))
    nw = nb = nbad = 0
    for e in bank['entries']:
        if e['tag'] == uc2xwb.TAG_WMA:
            continue
        rate, ch, s = uc2xwb.decode_entry(bank, e['index'])
        ref = struct.pack('<%dh' % len(s), *s)
        p = os.path.join(pcm, '%s.%d.pcm' % (b, e['index']))
        got = open(p, 'rb').read() if os.path.exists(p) else None
        nw += 1
        nb += len(ref)
        if got != ref:
            nbad += 1
            first = next((i for i in range(min(len(got or b''), len(ref))) if got[i] != ref[i]), None)
            print('DIFFER %s entry %d: C++ %s bytes, Python %d bytes, first differing byte %s' % (
                b, e['index'], len(got) if got is not None else 'no file', len(ref), first))
    print('PCM %s: %d waves, %d bytes compared, %d differ' % (b, nw, nb, nbad))
    total_w += nw; total_b += nb; bad += nbad
print('PCM RESULT: %d waves in %d banks, %d bytes, %d waves differ' % (total_w, len(banks), total_b, bad))
sys.exit(1 if bad else 0)
PY
