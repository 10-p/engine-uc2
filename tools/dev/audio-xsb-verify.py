#!/usr/bin/env python3
"""audio-xsb-verify.py: check tools/uc2xsb.py against the project.

Reads every shipped .xsb that the project (Sounds/Avaris.xap) also describes, with strict=False, and compares each
sound the reader understands with the project's sound: priority (the project's "no Priority" = 255 in the binary),
volume (sound + group, against the stored interval and against the reader's round-number guess), pitch, category,
layer, headroom boost, 3D flag and parameters, I3DL2 flag, and the tracks (one plain play event each: loop count,
wave bank, wave entry; no variation, no other event). Also every plain cue's sound index.

  tools/dev/audio-xsb-verify.py [--epic <epic-sources>]
  tools/dev/audio-xsb-verify.py --full [--cooked <cooked Sounds dir>]

--full checks uc2xsb.read_xsb_full, the whole format, against the cooked cue tables (the oracle: what the cook
wrote from the project) in every field UC2ALAudio plays: every cue (name, queue, sounds), every sound (priority,
volume + group, pitch, category, headroom boost, I3DL2, 3D flag, min/max distance, rolloff, RPC curves, the length
from the shipped .xwb), every track and event (play: time, delay, loops, new variation, playlist type, pitch and
volume ranges, waves and weights; set volume/pitch: time, mode, relative, steps, duration, low, high; the other
events by kind). Prints the differences by field with every instance.
"""

import argparse
import collections
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import uc2xsb  # noqa: E402

spec = importlib.util.spec_from_file_location('cook', os.path.join(HERE, '..', 'cook-audio.py'))
cook = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cook)


def compare(x, s, groups):
    """List of field names that differ between reader sound x and project sound s."""
    bad = []
    if x['priority'] != (s['priority'] if s['priority'] or s.get('_has_priority') else 255):
        bad.append('priority')
    v = s['volume'] + groups.get(s['group'], 0)
    if not (x['volume_range'][0] < v <= x['volume_range'][1]):
        bad.append('volume-interval')
    if x['pitch'] != s['pitch']:
        bad.append('pitch')
    if x['category'] != s['category']:
        bad.append('category')
    if x['layer'] != s['layer']:
        bad.append('layer')
    if x['headroomboost'] != s['headroomboost']:
        bad.append('headroomboost')
    if x['is3d'] != s['is3d']:
        bad.append('3d')
    if x['i3dl2_flag'] != (1 if s['i3dl2'] else 0):
        bad.append('i3dl2')
    if x['d3'] and s['3d']:
        for k in ('cone_in', 'cone_out', 'cone_vol', 'min', 'max', 'distfactor', 'rolloff', 'doppler'):
            if abs(x['d3'][k] - s['3d'][k]) > 1e-4 * max(1.0, abs(s['3d'][k])):
                bad.append('3d.' + k)
        if s['3d']['mode'] != 0:
            bad.append('3d.mode')
    if len(x['tracks']) != len(s['tracks']):
        bad.append('tracks')
    else:
        for xt, st in zip(x['tracks'], s['tracks']):
            if len(st['events']) != 1 or st['events'][0]['kind'] != 'play':
                bad.append('events')
                continue
            e = st['events'][0]
            if len(e['waves']) != 1 or e['pitch'] != (0, 0) or e['vol'] != (0, 0) or e['time'] or e['delay']:
                bad.append('variation')
                continue
            if xt[0]['loops'] != e['loops']:
                bad.append('loops')
            if (xt[0]['bank'], xt[0]['entry']) != (e['waves'][0]['bank'], e['waves'][0]['entry']):
                bad.append('wave')
    if s['eqparams'] or s['rpcs']:
        bad.append('eq/rpc')
    return bad


def parse_cooked(path):
    """The cooked cue table, in read_xsb_full's shape (group volume added; ignored events by kind only)."""
    groups, sounds, cues, snd, trk = {}, [], [], None, None
    unesc = lambda v: re.sub('%([0-9A-Fa-f]{2})', lambda m: chr(int(m.group(1), 16)), v)
    for line in open(path, encoding='latin-1'):
        p = line.split()
        if not p:
            continue
        kw, kv = p[0], dict((x.split('=', 1)[0], unesc(x.split('=', 1)[1])) for x in p[1:] if '=' in x)
        pair = lambda k: tuple(int(x) for x in kv[k].split(','))
        if kw == 'group':
            groups[kv['name']] = int(kv['volume'])
        elif kw == 'sound':
            snd = dict(name=kv['name'], priority=int(kv['priority']), volume=int(kv['volume']) + groups.get(
                kv['group'], 0), pitch=int(kv['pitch']), category=kv['category'], headroomboost=int(kv['headroomboost']),
                i3dl2=int(kv['i3dl2']), is3d=int(kv['3d']), length=int(kv['length']), loops=int(kv['loops']), d3=None,
                tracks=[], rpcs=[])
            sounds.append(snd)
        elif kw == '3d':
            snd['d3'] = dict(min=float(kv['min']), max=float(kv['max']), rolloff=float(kv['rolloff']))
        elif kw == 'rpc':
            snd['rpcs'].append(dict(name=kv['name'], track=int(kv['track']), points=[
                tuple(int(y) for y in x.split(':')) for x in kv['points'].split(',')]))
        elif kw == 'track':
            trk = []
            snd['tracks'].append(trk)
        elif kw == 'play':
            trk.append(dict(kind='play', time=int(kv['time']), delay=int(kv['delay']), loops=int(kv['loops']),
                            newvar=int(kv['newvar']), playlist=int(kv['playlist']), pitch=pair('pitch'),
                            vol=pair('volume'), waves=[]))
        elif kw == 'wave':
            trk[-1]['waves'].append(dict(bank=kv['bank'], entry=int(kv['entry']), weight=int(kv['weight'])))
        elif kw in ('setvol', 'setpitch'):
            trk.append(dict(kind=kw, time=int(kv['time']), mode=kv['mode'], relative=int(kv['relative']),
                            steps=int(kv['steps']), dur=int(kv['dur']), lo=int(kv['lo']), hi=int(kv['hi'])))
        elif kw in ('lpf', 'lfo', 'pitchlfo', 'eg', 'mixbins', 'loopstart'):
            trk.append(dict(kind=kw, time=int(kv['time'])))
            if kw == 'loopstart':
                trk[-1]['count'] = int(kv['count'])
        elif kw == 'cue':
            cues.append(dict(name=kv['name'], queue=int(kv['queue']), sounds=[] if kv['sounds'] == '-' else [
                tuple(int(y) for y in x.split(':')) for x in kv['sounds'].split(',')]))
    return dict(sounds=sounds, cues=cues)


def length_ms(sound, frames):
    """cook-audio.py's sound_length_ms with the wave lengths of the shipped .xwb (frames(bank, entry) -> (n, rate))."""
    longest = 0
    for t in sound['tracks']:
        tlen = 0
        for e in t:
            if e['kind'] != 'play':
                tlen = max(tlen, e['time'] + e.get('dur', 0))
                continue
            if e['loops'] == 65535:
                return -1
            wl = 0
            for w in e['waves']:
                fr = frames(w['bank'], w['entry'])
                if fr and fr[0] and fr[1]:
                    wl = max(wl, 1000.0 * fr[0] / fr[1])
            tlen = max(tlen, e['time'] + e['delay'] + wl * (e['loops'] + 1))
        if any(e['kind'] == 'loopstart' and e['count'] == 65535 for e in t):
            return -1
        longest = max(longest, tlen)
    return int(round(longest))


def full(args):
    import glob
    import uc2xwb
    xwbs = {}

    def frames(bank, entry):
        if bank not in xwbs:
            p = os.path.join(args.epic, 'Sounds', bank + '.xwb')
            xwbs[bank] = uc2xwb.read_xwb(p) if os.path.exists(p) else None
        b = xwbs[bank]
        if not b or entry >= len(b['entries']):
            return None
        return b['entries'][entry]['frames'], b['entries'][entry]['rate']

    diffs, ex = collections.Counter(), collections.defaultdict(list)

    def note(k, msg):
        diffs[k] += 1
        ex[k].append(msg)
    nb = ns = nc = 0
    for cf in sorted(glob.glob(os.path.join(args.cooked, '*.uc2cues'))):
        bn = os.path.basename(cf)[:-len('.uc2cues')]
        c, x = parse_cooked(cf), uc2xsb.read_xsb_full(os.path.join(args.epic, 'Sounds', bn + '.xsb'))
        nb += 1
        if len(c['sounds']) != len(x['sounds']) or len(c['cues']) != len(x['cues']):
            note('bank.counts', bn)
            continue
        for a, b in zip(c['cues'], x['cues']):
            nc += 1
            for k in ('name', 'queue', 'sounds'):
                if a[k] != b[k]:
                    note('cue.' + k, '%s: %s / %s' % (a['name'], a[k], b[k]))
        for a, b in zip(c['sounds'], x['sounds']):
            ns += 1
            tag = '%s %s' % (bn, a['name'])
            for k in ('name', 'priority', 'volume', 'pitch', 'category', 'headroomboost', 'i3dl2', 'is3d', 'rpcs'):
                if a[k] != b[k]:
                    note('sound.' + k, '%s: %s / %s' % (tag, a[k], b[k]))
            if (a['d3'] is None) != (b['d3'] is None):
                note('sound.3d-block', tag)
            elif a['d3']:
                for k in ('min', 'max', 'rolloff'):
                    if abs(a['d3'][k] - b['d3'][k]) > 1e-4 * max(1.0, abs(a['d3'][k])):
                        note('sound.3d.' + k, '%s: %s / %s' % (tag, a['d3'][k], b['d3'][k]))
            ln = length_ms(b, frames)
            if ln != a['length']:
                note('sound.length (ms, xsb - cooked = %+d)' % (ln - a['length']) if 0 < ln - a['length'] <= 5
                     else 'sound.length', '%s: %s / %s' % (tag, a['length'], ln))
            if len(a['tracks']) != len(b['tracks']):
                note('sound.tracks', tag)
                continue
            for ta, tb in zip(a['tracks'], b['tracks']):
                if [e['kind'] for e in ta] != [e['kind'] for e in tb]:
                    note('track.events', '%s: %s / %s' % (tag, [e['kind'] for e in ta], [e['kind'] for e in tb]))
                    continue
                for ea, eb in zip(ta, tb):
                    for k in ea:
                        if k == 'waves':
                            ra = [(w['bank'], w['entry']) for w in ea['waves']]
                            if ra != [(w['bank'], w['entry']) for w in eb['waves']]:
                                note('play.waves', '%s: %s / %s' % (tag, ea['waves'], eb['waves']))
                            elif [w['weight'] for w in ea['waves']] != [w['weight'] for w in eb['waves']]:
                                note('play.weights', '%s: %s / %s' % (tag, [w['weight'] for w in ea['waves']],
                                                                     [w['weight'] for w in eb['waves']]))
                        elif ea[k] != eb.get(k):
                            note('%s.%s' % (ea['kind'], k), '%s: %s / %s' % (tag, ea[k], eb.get(k)))
    print('banks %d, sounds %d, cues %d compared (cooked / read from the .xsb)' % (nb, ns, nc))
    for k, v in sorted(diffs.items()):
        print('DIFF %s: %d' % (k, v))
        for m in ex[k]:
            print('    ' + m)
    print('fields that differ: %d kinds, %d instances' % (len(diffs), sum(diffs.values())))
    return 0


def main():
    work = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
    ap = argparse.ArgumentParser()
    ap.add_argument('--epic', default=os.path.join(work, 'epic-sources'))
    ap.add_argument('--full', action='store_true')
    ap.add_argument('--cooked', default=os.path.join(work, 'assets', 'uc2-cooked', 'Sounds'))
    args = ap.parse_args()
    if args.full:
        return full(args)
    root = cook.parse_xap(os.path.join(args.epic, 'Sounds', 'Avaris.xap'))
    groups = {g.get('Name'): cook.num(g.get('Volume')) for g in root.kids('Sound Group')}
    cats = tuple(c.get('Name') for c in root.kids('Sound Category'))
    lays = tuple(c.get('Name') for c in root.kids('Sound Layer'))
    read = ok = unsupported = guess_exact = cues_read = cues_ok = 0
    why = collections.Counter()
    diffs = collections.Counter()
    for node in root.kids('Sound Bank'):
        sb = cook.SoundBank(node, [])
        has_pri = [s.get('Priority') is not None for s in node.kids('Sound')]
        x = uc2xsb.read_xsb(os.path.join(args.epic, 'Sounds', os.path.splitext(sb.bank_file)[0] + '.xsb'),
                            cats, lays, strict=False)
        for i, (xs, s) in enumerate(zip(x['sounds'], sb.sounds)):
            if 'unsupported' in xs:
                unsupported += 1
                why[xs['unsupported'].split(':', 1)[1].split('(')[0].strip()] += 1
                continue
            read += 1
            s = dict(s, _has_priority=has_pri[i])
            bad = compare(xs, s, groups)
            if xs['volume'] == s['volume'] + groups.get(s['group'], 0):
                guess_exact += 1
            if bad:
                for b in bad:
                    diffs[b] += 1
                if sum(diffs.values()) <= 12:
                    print('  %s sound %d %s: %s' % (sb.name, i, s['name'], ', '.join(bad)))
            else:
                ok += 1
        for xc, c in zip(x['cues'], sb.cues):
            if 'unsupported' in xc:
                continue
            cues_read += 1
            if xc['name'] == c['name'] and [xc['sound']] == [si for si, _ in c['sounds']]:
                cues_ok += 1
    print('sounds read: %d, equal to the project in every field read: %d; volume guess = the project\'s sound + '
          'group volume: %d of %d' % (read, ok, guess_exact, read))
    print('fields that differ: %s' % (dict(diffs) or 'none'))
    print('sounds not read (outside the reader\'s shape): %d: %s' % (unsupported, dict(why.most_common())))
    print('plain cues read: %d, same name and sound as the project: %d' % (cues_read, cues_ok))
    return 0 if ok == read and cues_ok == cues_read else 1


if __name__ == '__main__':
    sys.exit(main())
