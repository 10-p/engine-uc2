#!/usr/bin/env python3
"""uc2xwb.py: read the Xbox XACT wave banks (.xwb) that Unreal Championship 2 ships, and decode their waves.

The format is the XDK's (XDK 5849 `xbox/include/xactwb.h`, version 3; every UC2 bank is version 3):

  WAVEBANKHEADER   0x00 'WBND'  u32 version (3)  4 x WAVEBANKREGION {u32 offset, u32 length}:
                   bank data, entry meta-data, entry names (empty in UC2), entry wave data
  WAVEBANKDATA     u32 flags (0x1 streaming, 0x10000 entry names, 0x20000 compact)  u32 entry count
                   char name[16]  u32 meta-data element size  u32 name element size  u32 alignment
                   WAVEBANKMINIWAVEFORMAT compact format
  WAVEBANKENTRY    (full layout, 24 bytes) u32 flags, mini format, play region {offset, length},
                   loop region {offset, length}; offsets relative to the wave data segment
  WAVEBANKENTRYCOMPACT (4 bytes) offset:21 in units of the alignment, length deviation:11 in bytes; the length
                   is the distance to the next entry (or the segment end) minus the deviation; every entry has
                   the bank's compact format and no loop region of its own
  WAVEBANKMINIWAVEFORMAT  tag:2 (0 PCM, 1 Xbox ADPCM, 2 WMA)  channels:3  rate:26  bits:1 (PCM: 0 8-bit, 1 16-bit)

Xbox ADPCM (WAVE_FORMAT_XBOX_ADPCM 0x0069) is IMA ADPCM in blocks of 36 bytes per channel: a 4-byte header
(s16 first sample, u8 step index, u8 0) and 32 bytes of 4-bit codes (low nibble first). Channels are interleaved
in 4-byte words: the headers of all channels, then 4 bytes (8 codes) of channel 0, 4 of channel 1, and so on.
A block is 64 sample frames (the XDK's and the cook's unit): the header sample and the first 63 codes; the 64th
code is not played (it predicts the next block's header sample). This is checked against source WAVs by
`--verify` (see docs/porting/analysis/audio-f6.md).

No dependency beyond the standard library. Usage:

  tools/uc2xwb.py list <bank.xwb>                         entries, formats, lengths
  tools/uc2xwb.py extract <bank.xwb> <index> <out.wav>    one entry as 16-bit PCM WAV (WMA: not supported)
  tools/uc2xwb.py verify <bank.xwb> <index> <source.wav>  decode and compare with a source WAV (SNR, offset)
"""

import struct
import sys

FLAG_STREAMING = 0x1
FLAG_ENTRYNAMES = 0x10000
FLAG_COMPACT = 0x20000
TAG_PCM, TAG_ADPCM, TAG_WMA = 0, 1, 2
TAG_NAMES = {TAG_PCM: 'PCM', TAG_ADPCM: 'XboxADPCM', TAG_WMA: 'WMA'}
ADPCM_BLOCK = 36            # bytes per channel per block
ADPCM_FRAMES = 64           # sample frames per block

IMA_INDEX = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)
IMA_STEP = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767)


class XwbError(Exception):
    pass


def mini_format(v):
    return dict(tag=v & 3, channels=(v >> 2) & 7, rate=(v >> 5) & ((1 << 26) - 1), bits=16 if v >> 31 else 8)


def read_xwb(path):
    """Returns dict(version, flags, name, alignment, entries=[dict(index, flags, tag, channels, rate, bits,
    offset (absolute file offset), length (bytes), loop=(offset, length) bytes or None, frames)])."""
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 40:
        raise XwbError('%s: too short for a wave bank header' % path)
    sig, ver = struct.unpack_from('<4sI', d, 0)
    if sig != b'WBND':
        raise XwbError('%s: not a WBND file' % path)
    if ver != 3:
        raise XwbError('%s: wave bank version %d (only 3 is known)' % (path, ver))
    segs = [struct.unpack_from('<II', d, 8 + 8 * i) for i in range(4)]
    for i, (o, n) in enumerate(segs):
        if o + n > len(d):
            raise XwbError('%s: segment %d (%d+%d) beyond the file (%d bytes)' % (path, i, o, n, len(d)))
    bd = segs[0][0]
    flags, count = struct.unpack_from('<II', d, bd)
    name = d[bd + 8:bd + 24].split(b'\0')[0].decode('latin-1')
    mdsize, nmsize, align, cfmt = struct.unpack_from('<IIII', d, bd + 24)
    wave0, wavelen = segs[3]
    ents = []
    if flags & FLAG_COMPACT:
        if mdsize != 4:
            raise XwbError('%s: compact bank with %d-byte entries' % (path, mdsize))
        raw = []
        for i in range(count):
            v = struct.unpack_from('<I', d, segs[1][0] + 4 * i)[0]
            raw.append(((v & ((1 << 21) - 1)) * align, v >> 21))
        for i, (o, dev) in enumerate(raw):
            nxt = raw[i + 1][0] if i + 1 < count else wavelen
            e = dict(index=i, flags=0, offset=wave0 + o, length=nxt - o - dev, loop=None)
            e.update(mini_format(cfmt))
            ents.append(e)
    else:
        if mdsize < 24:
            raise XwbError('%s: %d-byte entry meta-data (24 expected)' % (path, mdsize))
        for i in range(count):
            ef, fv, po, pl, lo, ll = struct.unpack_from('<6I', d, segs[1][0] + mdsize * i)
            e = dict(index=i, flags=ef, offset=wave0 + po, length=pl, loop=(lo, ll))
            e.update(mini_format(fv))
            ents.append(e)
    for e in ents:
        if e['offset'] + e['length'] > len(d) or e['length'] < 0:
            raise XwbError('%s: entry %d (%d+%d) beyond the file' % (path, e['index'], e['offset'], e['length']))
        ch = max(1, e['channels'])
        if e['tag'] == TAG_ADPCM:
            e['frames'] = e['length'] // (ADPCM_BLOCK * ch) * ADPCM_FRAMES
        elif e['tag'] == TAG_PCM:
            e['frames'] = e['length'] // (e['bits'] // 8 * ch)
        else:
            e['frames'] = None
    return dict(path=path, version=ver, flags=flags, name=name, alignment=align, entries=ents, data=d)


def decode_adpcm(data, channels):
    """Xbox ADPCM -> list of interleaved s16 samples, 64 frames per block."""
    blk = ADPCM_BLOCK * channels
    nblocks = len(data) // blk
    out = [0] * (nblocks * ADPCM_FRAMES * channels)
    idx_tab, step_tab = IMA_INDEX, IMA_STEP
    for b in range(nblocks):
        base = b * blk
        for c in range(channels):
            pred, index = struct.unpack_from('<hB', data, base + 4 * c)
            if index > 88:
                index = 88
            o = (b * ADPCM_FRAMES) * channels + c
            out[o] = pred
            # codes of this channel: 8 words of 4 bytes, word w at base + 4*channels + (w*channels + c)*4
            n = 1
            for w in range(8):
                wo = base + 4 * channels + (w * channels + c) * 4
                for byte in data[wo:wo + 4]:
                    for code in (byte & 15, byte >> 4):
                        if n >= ADPCM_FRAMES:
                            break       # the 64th code predicts the next block's header: not played
                        step = step_tab[index]
                        diff = step >> 3
                        if code & 1:
                            diff += step >> 2
                        if code & 2:
                            diff += step >> 1
                        if code & 4:
                            diff += step
                        if code & 8:
                            pred -= diff
                            if pred < -32768:
                                pred = -32768
                        else:
                            pred += diff
                            if pred > 32767:
                                pred = 32767
                        index += idx_tab[code]
                        if index < 0:
                            index = 0
                        elif index > 88:
                            index = 88
                        out[o + n * channels] = pred
                        n += 1
    return out


def decode_entry(bank, index):
    """Returns (rate, channels, samples) with samples a list of interleaved s16."""
    e = bank['entries'][index]
    raw = bank['data'][e['offset']:e['offset'] + e['length']]
    ch = e['channels']
    if e['tag'] == TAG_ADPCM:
        return e['rate'], ch, decode_adpcm(raw, ch)
    if e['tag'] == TAG_PCM:
        if e['bits'] == 16:
            n = len(raw) // 2
            return e['rate'], ch, list(struct.unpack('<%dh' % n, raw[:2 * n]))
        return e['rate'], ch, [(b - 128) << 8 for b in raw]
    raise XwbError('%s entry %d: %s is not decoded by this tool' % (bank['name'], index, TAG_NAMES.get(e['tag'])))


def write_wav(path, rate, channels, samples):
    pcm = struct.pack('<%dh' % len(samples), *samples)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * channels * 2, channels * 2, 16))
        f.write(b'data' + struct.pack('<I', len(pcm)) + pcm)


def read_wav_pcm16(path):
    """(rate, channels, samples) of a 16-bit PCM RIFF WAV."""
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != b'RIFF' or d[8:12] != b'WAVE':
        raise XwbError('%s: not RIFF/WAVE' % path)
    i, fmt, data = 12, None, None
    while i + 8 <= len(d):
        cid, ln = d[i:i + 4], struct.unpack_from('<I', d, i + 4)[0]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', d, i + 8)
        elif cid == b'data':
            data = d[i + 8:i + 8 + ln]
        i += 8 + ln + (ln & 1)
    if not fmt or data is None or fmt[0] != 1 or fmt[5] != 16:
        raise XwbError('%s: not 16-bit PCM' % path)
    n = len(data) // 2
    return fmt[2], fmt[1], list(struct.unpack('<%dh' % n, data[:2 * n]))


def stats(samples):
    """peak, number of samples at full scale (clipped), RMS dBFS."""
    if not samples:
        return 0, 0, -999.0
    import math
    peak = max(abs(s) for s in samples)
    clipped = sum(1 for s in samples if s >= 32767 or s <= -32768)
    rms = math.sqrt(sum(s * s for s in samples) / len(samples))
    return peak, clipped, (20 * math.log10(rms / 32768.0) if rms > 0 else -999.0)


def compare(a, b, channels, max_shift=4):
    """Best SNR (dB) of decoded a against reference b over sample-frame shifts -max_shift..max_shift."""
    import math
    best = (-999.0, 0)
    n = min(len(a), len(b)) // channels
    for sh in range(-max_shift, max_shift + 1):
        sig = err = 0
        for i in range(max(0, -sh), min(n, n - sh)):
            for c in range(channels):
                r = b[i * channels + c]
                x = a[(i + sh) * channels + c]
                sig += r * r
                err += (x - r) * (x - r)
        snr = 10 * math.log10(sig / err) if err and sig else (999.0 if sig else -999.0)
        if snr > best[0]:
            best = (snr, sh)
    return best


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, path = argv[1], argv[2]
    bank = read_xwb(path)
    if cmd == 'list':
        print('bank %s version %d flags 0x%x (%s%s) alignment %d entries %d' % (
            bank['name'], bank['version'], bank['flags'], 'streaming' if bank['flags'] & FLAG_STREAMING else 'buffer',
            ', compact' if bank['flags'] & FLAG_COMPACT else '', bank['alignment'], len(bank['entries'])))
        for e in bank['entries']:
            print('entry %d %s %d ch %d Hz offset %d length %d frames %s loop %s flags 0x%x' % (
                e['index'], TAG_NAMES.get(e['tag'], '?') + ('%d' % e['bits'] if e['tag'] == TAG_PCM else ''),
                e['channels'], e['rate'], e['offset'],
                e['length'], e['frames'], '%d+%d' % e['loop'] if e['loop'] else '-', e['flags']))
        return 0
    if cmd == 'extract' and len(argv) == 5:
        rate, ch, s = decode_entry(bank, int(argv[3]))
        write_wav(argv[4], rate, ch, s)
        pk, cl, rms = stats(s)
        print('%s entry %s: %d ch %d Hz %d frames, peak %d, %d samples at full scale, RMS %.1f dBFS -> %s' % (
            bank['name'], argv[3], ch, rate, len(s) // ch, pk, cl, rms, argv[4]))
        return 0
    if cmd == 'verify' and len(argv) == 5:
        rate, ch, s = decode_entry(bank, int(argv[3]))
        r_rate, r_ch, ref = read_wav_pcm16(argv[4])
        snr, sh = compare(s, ref, ch)
        print('%s entry %s: decoded %d frames, source %d frames (%d ch %d Hz vs %d ch %d Hz): SNR %.2f dB at a '
              'shift of %d frames' % (bank['name'], argv[3], len(s) // ch, len(ref) // r_ch, ch, rate, r_ch, r_rate,
                                      snr, sh))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
