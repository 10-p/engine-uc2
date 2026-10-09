#!/usr/bin/env python3
"""audio-xsb-verify.py: check tools/uc2xsb.py against the project.

Reads every shipped .xsb that the project (Sounds/Avaris.xap) also describes, with strict=False, and compares each
sound the reader understands with the project's sound: priority (the project's "no Priority" = 255 in the binary),
volume (sound + group, against the stored interval and against the reader's round-number guess), pitch, category,
layer, headroom boost, 3D flag and parameters, I3DL2 flag, and the tracks (one plain play event each: loop count,
wave bank, wave entry; no variation, no other event). Also every plain cue's sound index.

  tools/dev/audio-xsb-verify.py [--epic <epic-sources>]
"""

import argparse
import collections
import importlib.util
import os
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


def main():
    work = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
    ap = argparse.ArgumentParser()
    ap.add_argument('--epic', default=os.path.join(work, 'epic-sources'))
    args = ap.parse_args()
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
