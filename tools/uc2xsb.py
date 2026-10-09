#!/usr/bin/env python3
"""uc2xsb.py: read the simple parts of a shipped Xbox XACT sound bank (.xsb, format version 11).

The XDK documents no .xsb layout (xact.h only has the runtime API). What follows was decoded by comparing the 78
shipped banks of the project with Sounds/Avaris.xap (tools/dev/audio-xsb-verify.py checks it; the evidence is in
docs/porting/analysis/audio-f6.md, step 2). Only what a bank made of plain sounds needs is read; anything else
raises XsbUnsupported, so a bank is cooked either faithfully or not at all.

  0x00 'SDBK'  u16 version (11)  u16 checksum
  0x08 u32 wave bank name table (16-byte names)   0x0C u32 track/event area   0x10 u32 3D parameter table
  0x14 u32 RPC area (or the end of the file)
  0x18 u16 0  u16 hash size  u16 sounds  u16 cues  u16 ?  u16 wave banks      0x28 char name[16]
  0x38 cues, 20 bytes: u16 flags, u16 sound, u32 name offset, u32 0xFFFFFFFF (one sound; else a variation table),
       u16 0, u16 hash link, u32 0x07FFFFFF
  then sounds, 20 bytes:
       +0  u32  flags & 8: u16 wave entry, u16 wave bank index; else the offset of the track table
       +4  s16  volume: floor((-8192 - (sound volume + group volume in mB)) / 16)
       +6  s16  pitch: trunc(cents * 4096 / 1200)
       +8  u8   tracks        +9  u8 layer (0xFF none)      +10 u8 category (0xFF none)
       +11 u8   flags: 1 3D, 2 headroom boost, 4 parametric EQ, 8 one wave without events, 0x10 one play event
                with a variation table (not read)
       +12 u16  3D parameter set: index into the 3D table (40-byte records, shared by identical sounds)
       +14 u8   priority (0..255; the project's "no Priority" is stored as 255)
       +15 u8   1 if the sound has an I3DL2 (reverb) level (the project's -300 mB on 733 of 734 such sounds)
       +16 u32  RPC (parameter control) reference, 0 if none (not read)
  track table: u32 per track = event count (low byte) | event list offset << 8
  event type 0 (u32 header 0 = type 0 at time 0): u16 flags (4 = one wave), u16 loop count (0xFFFF infinite),
       u16 wave entry, u16 wave bank index; 12 bytes
  3D record: u16 inside cone angle, u16 outside cone angle, s32 cone outside volume (mB), f32 minimum distance,
       f32 maximum distance, f32 distance factor, f32 rolloff factor, f32 doppler factor, 12 bytes 0

  tools/uc2xsb.py <bank.xsb>          print what is read
"""

import struct
import sys


class XsbError(Exception):
    pass


class XsbUnsupported(XsbError):
    pass


def volume_from_stored(stored):
    """The stored volume keeps 1/16 of the mB value, floored: return the round value the designer most likely typed
    (a multiple of 100, else of 50, else the top of the interval), and the interval (lo exclusive, hi inclusive)."""
    hi = -8192 - 16 * stored
    lo = hi - 16
    for step in (100, 50, 10):
        v = hi - (hi % step)
        if lo < v <= hi:
            return v, (lo, hi)
    return hi, (lo, hi)


def pitch_from_stored(stored):
    return int(round(stored * 1200 / 4096.0))


def cstr(d, off):
    end = d.index(b'\0', off)
    return d[off:end].decode('latin-1')


def read_xsb(path, categories=('Music', 'FX', 'Voice'), layers=(), strict=True):
    """Returns dict(name, version, wavebanks, sounds, cues). Each sound: dict(name (of its first cue), priority,
    volume, volume_range, pitch, category, layer, headroomboost, i3dl2_flag, is3d, d3 (dict or None),
    tracks=[[dict(loops, bank, entry)]]). Raises XsbUnsupported for anything outside that shape; with strict=False
    such a cue or sound is dict(unsupported=<reason>) instead (used by the verifier on the project's banks)."""
    with open(path, 'rb') as f:
        d = f.read()
    magic, ver = struct.unpack_from('<4sH', d, 0)
    if magic != b'SDBK':
        raise XsbError('%s: not an SDBK file' % path)
    if ver != 11:
        raise XsbUnsupported('%s: version %d (11 is read)' % (path, ver))
    wbtab, _, t3d, _ = struct.unpack_from('<4I', d, 8)
    _, _, nsounds, ncues, _, nwb = struct.unpack_from('<6H', d, 0x18)
    name = d[0x28:0x38].split(b'\0')[0].decode('latin-1')
    wavebanks = [d[wbtab + 16 * i:wbtab + 16 * i + 16].split(b'\0')[0].decode('latin-1') for i in range(nwb)]
    cues = []
    for i in range(ncues):
        fl, si, noff, var, _, _, _ = struct.unpack_from('<HHIIHHI', d, 0x38 + 20 * i)
        c = dict(name=cstr(d, noff), sound=si)
        if fl != 0 or var != 0xFFFFFFFF:
            if strict:
                raise XsbUnsupported('cue %d: flags 0x%x / variation table 0x%x (one plain sound is read)' % (
                    i, fl, var))
            c['unsupported'] = 'cue flags 0x%x / variation table 0x%x' % (fl, var)
        elif si == 0xFFFF:
            if strict:
                raise XsbUnsupported('cue %d: no sound' % i)
            c['unsupported'] = 'cue without a sound'
        elif si >= nsounds:
            raise XsbError('cue %d: sound %d of %d' % (i, si, nsounds))
        cues.append(c)
    base = 0x38 + 20 * ncues
    sounds = []
    for i in range(nsounds):
        try:
            sounds.append(read_sound(d, base, i, t3d, wavebanks, categories, layers))
        except XsbUnsupported as ex:
            if strict:
                raise
            sounds.append(dict(unsupported=str(ex)))
    for c in cues:
        if 'unsupported' not in c:
            sounds[c['sound']].setdefault('name', c['name'])
    for i, s in enumerate(sounds):
        s.setdefault('name', '%s\\Sound%d' % (name, i))
    return dict(name=name, version=ver, wavebanks=wavebanks, sounds=sounds, cues=cues)


def read_sound(d, base, i, t3d, wavebanks, categories, layers):
    """One 20-byte sound record (see the module comment)."""
    nwb = len(wavebanks)
    w0, vol, pit, ntr, lay, cat, fl, i3d, pri, rev, rpc = struct.unpack_from('<IhhBBBBHBBI', d, base + 20 * i)
    if fl & ~0x0B:
        raise XsbUnsupported('sound %d: flags 0x%x (EQ or a variation table)' % (i, fl))
    if rpc:
        raise XsbUnsupported('sound %d: parameter control 0x%x' % (i, rpc))
    if rev > 1:
        raise XsbUnsupported('sound %d: I3DL2 byte %d' % (i, rev))
    v, vr = volume_from_stored(vol)
    s = dict(priority=pri, volume=v, volume_range=vr, pitch=pitch_from_stored(pit),
             category=categories[cat] if cat < len(categories) else ('-' if cat == 0xFF else None),
             layer=layers[lay] if lay < len(layers) else ('-' if lay == 0xFF else None),
             headroomboost=1 if fl & 2 else 0, i3dl2_flag=rev, is3d=fl & 1, d3=None, tracks=[])
    if s['category'] is None or s['layer'] is None:
        raise XsbUnsupported('sound %d: category %d / layer %d unknown' % (i, cat, lay))
    if fl & 1:
        ci, co, cv, mn, mx, df, ro, dp = struct.unpack_from('<HHifffff', d, t3d + 40 * i3d)
        if d[t3d + 40 * i3d + 28:t3d + 40 * i3d + 40].strip(b'\0'):
            raise XsbUnsupported('sound %d: 3D record %d has data after the doppler factor' % (i, i3d))
        s['d3'] = dict(mode=0, cone_in=ci, cone_out=co, cone_vol=cv, min=mn, max=mx, distfactor=df, rolloff=ro,
                       doppler=dp)
    if fl & 8:
        if ntr != 1:
            raise XsbUnsupported('sound %d: one-wave sound with %d tracks' % (i, ntr))
        s['tracks'].append([dict(loops=0, entry=w0 & 0xFFFF, bank=w0 >> 16)])
    else:
        for t in range(ntr):
            v32 = struct.unpack_from('<I', d, w0 + 4 * t)[0]
            nev, off = v32 & 0xFF, v32 >> 8
            evs = []
            for k in range(nev):
                hdr, efl, loops, entry, bank = struct.unpack_from('<IHHHH', d, off + 12 * k)
                if hdr != 0 or efl != 4:
                    raise XsbUnsupported('sound %d track %d event %d: header 0x%x flags 0x%x (a plain play event '
                                         'at time 0 is read)' % (i, t, k, hdr, efl))
                evs.append(dict(loops=loops, entry=entry, bank=bank))
            if len(evs) != 1:
                raise XsbUnsupported('sound %d track %d: %d events' % (i, t, len(evs)))
            s['tracks'].append(evs)
    for tr in s['tracks']:
        for ev in tr:
            if ev['bank'] >= nwb:
                raise XsbError('sound %d: wave bank %d of %d' % (i, ev['bank'], nwb))
            ev['bank'] = wavebanks[ev['bank']]
    return s


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    b = read_xsb(argv[1])
    print('bank %s version %d wave banks %s sounds %d cues %d' % (b['name'], b['version'], ','.join(b['wavebanks']),
                                                                  len(b['sounds']), len(b['cues'])))
    for i, s in enumerate(b['sounds']):
        print('sound %d %s priority %d volume %d (%d..%d] pitch %d %s 3d %d tracks %s' % (
            i, s['name'], s['priority'], s['volume'], s['volume_range'][0], s['volume_range'][1], s['pitch'],
            s['category'], s['is3d'], s['tracks']))
        if s['d3']:
            print('  3d %s' % s['d3'])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
