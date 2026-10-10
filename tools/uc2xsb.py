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

Everything else of the format, decoded in M16 (docs/porting/analysis/xbox-audio-runtime.md; read_xsb_full reads it and
tools/dev/audio-xsb-verify.py --full compares it with the cooked cue tables, the C++ reader in Source/UC2ALAudio is the
same layout):
  0x20 u16 at 0x20 (the "?" above): the number of RPC records
  sound +4: bits 0-8 attenuation in 16 mB steps (the volume), bits 9-15 the LFE level in -50 mB steps (0x7F = none);
       the "5 overflowed volumes" of F-6 are the 5 sounds with an LFE level, read correctly this way
  sound flags 0x10: +0 is the offset of a variation table (one play event at time 0, no pitch/volume variation)
  sound +15: 1 = the sound sends to I3DL2 (the level itself is not stored: the project's -300 mB is taken)
  sound +16: parametric EQ parameters when flag 4 (not read); RPCs are not referenced from the sound
  event header u32: type (low byte) | timestamp in ms << 8. Types and sizes:
       0 play (12): u16 flags (4 one wave, 0x400 variation table, 0x4000 new variation on loop), u16 loop count,
         u32 wave (entry | bank << 16) or variation table offset
       1 play with pitch/volume variation (24): as 0, then s16 pitch low/high (4096/1200 units), s16 volume low/high
         (mB), u16 variable delay (ms), u16 not used
       4 set pitch / 5 set volume (16): u8 8, u8 mode (0x10 set, 0x04 random, 0x20 fade), u16 fade steps, s16 low,
         s16 high (pitch: 4096/1200 units, volume: mB), u8 relative, u16 duration (ms, unaligned), u8 0
       7 low-pass filter (20; u16 duration at +13), 8 pitch LFO (12), 9 multi-function LFO (16), 11 envelope (24),
       12 loop start (8; u16 loop count at +6), 15 play without a wave (8; u16 loop count at +6),
       16 mix bins (8 + the byte count at +4)
  variation table: u16 count | playlist type << 13, u16 2*count, then per wave u16 entry, u16 wave bank,
       u16 low, u16 high: cumulative weight ranges over 0..10000 (equal weights = equal ranges)
  RPC record (24 bytes, at the RPC area, 0x14): u16 sound, u8 track, u8 1, u32 curve offset, char name[16];
       curve: 4 bytes 0, 4 points of (u16 x 0..65535, s16 y mB), 4 bytes 0 (every UC2 curve has 4 points)
  cue flags 1: queue

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


EVENT_SIZE = {0: 12, 1: 24, 4: 16, 5: 16, 7: 20, 8: 12, 9: 16, 11: 24, 12: 8, 15: 8}
IGNORED_EVENTS = {7: 'lpf', 8: 'pitchlfo', 9: 'lfo', 11: 'eg', 16: 'mixbins'}


def _p4096(v):
    return int(round(v * 1200 / 4096.0))


def read_xsb_full(path, categories=('Music', 'FX', 'Voice')):
    """Every field UC2ALAudio plays, in the shape of the cooked cue table (cook-audio.py's write_cues): dict(name,
    wavebanks, sounds, cues). Sound: name, priority, volume (mB, the group's included), lfe, pitch (cents), category,
    headroomboost, i3dl2 (mB), is3d, d3 (min, max, rolloff, ...) or None, rpcs [dict(name, track, points)], tracks
    [[event...]]; event dicts as the cook's (kind play/setvol/setpitch/lpf/lfo/pitchlfo/eg/mixbins/loopstart).
    Weights: -1 when the table's ranges are equal (within rounding), else the range width (out of 10000)."""
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != b'SDBK':
        raise XsbError('%s: not an SDBK file' % path)
    if struct.unpack_from('<H', d, 4)[0] != 11:
        raise XsbUnsupported('%s: version %d (11 is read)' % (path, struct.unpack_from('<H', d, 4)[0]))
    wbtab, _, t3d, rpcarea = struct.unpack_from('<4I', d, 8)
    _, _, nsounds, ncues, nrpc, nwb = struct.unpack_from('<6H', d, 0x18)
    name = d[0x28:0x38].split(b'\0')[0].decode('latin-1')
    wbs = [d[wbtab + 16 * i:wbtab + 16 * i + 16].split(b'\0')[0].decode('latin-1') for i in range(nwb)]

    def table(off):
        a = struct.unpack_from('<H', d, off)[0]
        n, playlist = a & 0x1FFF, a >> 13
        ents = [struct.unpack_from('<HHHH', d, off + 4 + 8 * j) for j in range(n)]
        widths = [hi - lo + (0 if j == n - 1 else 1) for j, (_, _, lo, hi) in enumerate(ents)]
        uniform = n <= 1 or max(widths) - min(widths) <= n
        return playlist, [dict(bank=wbs[bk], entry=e, weight=-1 if uniform else w)
                          for (e, bk, _, _), w in zip(ents, widths)]

    def play(time=0, loops=0, playlist=0, waves=(), newvar=0):
        return dict(kind='play', time=time, delay=0, loops=loops, newvar=newvar, playlist=playlist, pitch=(0, 0),
                    vol=(0, 0), waves=list(waves))

    cues = []
    for i in range(ncues):
        fl, si, noff, var = struct.unpack_from('<HHII', d, 0x38 + 20 * i)
        if var != 0xFFFFFFFF:
            raise XsbUnsupported('cue %d: a sound variation table' % i)
        cues.append(dict(name=cstr(d, noff), queue=fl & 1, sounds=[] if si == 0xFFFF else [(si, -1)]))
    base = 0x38 + 20 * ncues
    sounds = []
    for i in range(nsounds):
        w0, vraw, pit, ntr, lay, cat, fl, i3d, pri, rev, _ = struct.unpack_from('<IHhBBBBHBBI', d, base + 20 * i)
        v, _ = volume_from_stored((vraw & 0x1FF) - 512)
        s = dict(priority=pri, volume=v, lfe=0 if vraw >> 9 == 0x7F else -50 * (vraw >> 9), pitch=_p4096(pit),
                 category=categories[cat] if cat < len(categories) else '-', headroomboost=1 if fl & 2 else 0,
                 i3dl2=-300 if rev else 0, is3d=fl & 1, d3=None, rpcs=[], tracks=[])
        if fl & 1:
            ci, co, cv, mn, mx, df, ro, dp = struct.unpack_from('<HHifffff', d, t3d + 40 * i3d)
            s['d3'] = dict(mode=0, cone_in=ci, cone_out=co, cone_vol=cv, min=mn, max=mx, distfactor=df, rolloff=ro,
                           doppler=dp)
        if fl & 8:
            s['tracks'].append([play(waves=[dict(bank=wbs[w0 >> 16], entry=w0 & 0xFFFF, weight=-1)])])
        elif fl & 0x10:
            pl, waves = table(w0)
            s['tracks'].append([play(playlist=pl, waves=waves)])
        else:
            for t in range(ntr):
                v32 = struct.unpack_from('<I', d, w0 + 4 * t)[0]
                o, evs = v32 >> 8, []
                for _ in range(v32 & 0xFF):
                    h = struct.unpack_from('<I', d, o)[0]
                    ty, tm = h & 0xFF, h >> 8
                    if ty == 16:
                        size = 8 + struct.unpack_from('<I', d, o + 4)[0]
                    elif ty in EVENT_SIZE:
                        size = EVENT_SIZE[ty]
                    else:
                        raise XsbUnsupported('sound %d track %d: event type %d' % (i, t, ty))
                    if ty in (0, 1):
                        efl, loops, wv = struct.unpack_from('<HHI', d, o + 4)
                        e = play(tm, loops, newvar=1 if efl & 0x4000 else 0)
                        if efl & 0x400:
                            e['playlist'], e['waves'] = table(wv)
                        else:
                            e['waves'] = [dict(bank=wbs[wv >> 16], entry=wv & 0xFFFF, weight=-1)]
                        if ty == 1:
                            pl_, ph, vl, vh, dl = struct.unpack_from('<hhhhH', d, o + 12)
                            e.update(pitch=(_p4096(pl_), _p4096(ph)), vol=(vl, vh), delay=dl)
                    elif ty == 15:
                        e = play(tm, struct.unpack_from('<H', d, o + 6)[0])
                    elif ty in (4, 5):
                        _, mode, steps, lo, hi, rel, dur = struct.unpack_from('<BBHhhBH', d, o + 4)
                        if ty == 4:
                            lo, hi = _p4096(lo), _p4096(hi)
                        e = dict(kind='setpitch' if ty == 4 else 'setvol', time=tm,
                                 mode='random' if mode & 4 else ('fade' if mode & 0x20 else 'set'), relative=rel,
                                 steps=steps, dur=dur, lo=lo, hi=hi)
                    elif ty == 12:
                        e = dict(kind='loopstart', time=tm, count=struct.unpack_from('<H', d, o + 6)[0])
                    elif ty == 7:
                        e = dict(kind='lpf', time=tm, dur=struct.unpack_from('<H', d, o + 13)[0])
                    else:
                        e = dict(kind=IGNORED_EVENTS[ty], time=tm)
                    evs.append(e)
                    o += size
                s['tracks'].append(evs)
        sounds.append(s)
    for k in range(nrpc):
        o = rpcarea + 24 * k
        si, trk, _, off = struct.unpack_from('<HBBI', d, o)
        pts = [struct.unpack_from('<Hh', d, off + 4 + 4 * j) for j in range(4)]
        sounds[si]['rpcs'].append(dict(name=d[o + 8:o + 24].split(b'\0')[0].decode('latin-1'), track=trk,
                                       points=pts))
    for c in cues:
        for si, _ in c['sounds']:
            sounds[si].setdefault('name', c['name'])
    for i, s in enumerate(sounds):
        s.setdefault('name', '%s\\Sound%d' % (name, i))
    return dict(name=name, wavebanks=wbs, sounds=sounds, cues=cues)


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
