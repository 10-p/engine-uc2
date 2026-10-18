#!/usr/bin/env python3
"""audio-xsb-compare.py: compare, bank by bank, the tables UC2ALAudio builds from a shipped .xsb with the ones it
builds from the cooked .uc2cues (the oracle), as dumped by `ucc-bin UC2ALAudio.UC2AudioCheck DUMP=<dir>`
(<dir>/<Bank>.cooked.txt and <dir>/<Bank>.xsb.txt; tools/dev/audio-xsb-compare.sh runs it all).

Every field the device plays is compared: cue name and sounds; sound priority, volume, pitch, category, headroom
boost, I3DL2 level, 3D flag, min/max distance, rolloff, length, loop count, ignored events, RPC curves; per track
the play event (time, delay, loops, new variation, playlist type, pitch and volume ranges), the waves (bank, entry,
weight) and the set volume / set pitch events. A difference is KNOWN when it is one of the documented inferences
(KNOWN_SOUND and KNOWN_WEIGHTS below), else NEW. Exit status 1 if anything is NEW.

  tools/dev/audio-xsb-compare.py <dump dir>
"""
import collections
import glob
import os
import sys

# The documented inferences: (field, bank\\sound or None for any) -> why.
KNOWN_SOUND = {
    ('i3dl2', 'A_Character\\JG_Gorge_Death'): 'I3DL2 level not stored in the .xsb (only "sends"); -300 mB taken, project -333',
}
KNOWN_WEIGHTS = {
    'A_WeaponMelee\\NCSW_HeavyAttack': 'equal explicit weights (4000 x 3) read as unset: the same draw',
    'A_WeaponMelee\\JG_Swing': 'equal explicit weights (2000 x 3) read as unset: the same draw',
    'A_WeaponMelee\\NCSTF_JumpChargeAttack': 'one wave with weight 6000: no choice either way',
    'A_WeaponRace\\NK_FreezeImpactPlayer': '9878:4146 kept as normalised ranges 7043:2957 (the same ratio)',
}


def parse(path):
    bank, sounds, cues = None, [], []
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\n')
        if not line:
            continue
        depth = len(line) - len(line.lstrip(' '))
        p = line.split()
        kv = {}
        for x in p[2:]:
            if '=' in x:
                k, v = x.split('=', 1)
                kv[k] = v
        if p[0] == 'bank':
            bank = p[1]
        elif p[0] == 'sound':
            sounds.append(dict(kv, tracks=[], rpcs=[]))
        elif p[0] == 'rpc':
            sounds[-1]['rpcs'].append(' '.join(p[1:]))
        elif p[0] == 'track':
            sounds[-1]['tracks'].append(dict(kv, waves=[], events=[]))
        elif p[0] == 'wave':
            sounds[-1]['tracks'][-1]['waves'].append(dict((x.split('=', 1)) for x in p[1:]))
        elif p[0] == 'event':
            sounds[-1]['tracks'][-1]['events'].append(dict((x.split('=', 1)) for x in p[1:]))
        elif p[0] == 'cue':
            cues.append(kv)
    return bank, sounds, cues


def main():
    d = sys.argv[1]
    diffs = collections.defaultdict(list)       # field -> [(known?, text)]
    banks = sounds = cues = 0
    missing = []
    for cooked in sorted(glob.glob(os.path.join(d, '*.cooked.txt'))):
        stem = os.path.basename(cooked)[:-len('.cooked.txt')]
        xsb = os.path.join(d, stem + '.xsb.txt')
        if not os.path.exists(xsb):
            missing.append(stem)
            continue
        _, cs, cc = parse(cooked)
        _, xs, xc = parse(xsb)
        banks += 1

        def add(field, tag, a, b, why=None):
            diffs[field].append((why, '%s: cooked %s / xsb %s' % (tag, a, b)))
        if len(cs) != len(xs) or len(cc) != len(xc):
            add('bank.counts', stem, (len(cs), len(cc)), (len(xs), len(xc)))
            continue
        for a, b in zip(cc, xc):
            cues += 1
            for k in ('name', 'sounds'):
                if a.get(k) != b.get(k):
                    add('cue.' + k, a.get('name'), a.get(k), b.get(k))
        for i, (a, b) in enumerate(zip(cs, xs)):
            sounds += 1
            name = a['name']
            for k in ('name', 'priority', 'volume', 'pitch', 'category', 'headroomboost', 'i3dl2', '3d', 'min', 'max',
                      'rolloff', 'length', 'loops', 'ignored'):
                if a.get(k) == b.get(k):
                    continue
                why = KNOWN_SOUND.get((k, name))
                if k == 'name':
                    why = 'sound names are not in the .xsb: named after the first cue'
                elif k == 'volume' and abs(int(a[k]) - int(b[k])) < 16:
                    why = 'the .xsb keeps 16 mB steps; round-number guess %s for %s (%d mB)' % (b[k], a[k], int(b[k]) - int(a[k]))
                elif k == 'length' and 0 < int(b[k]) - int(a[k]) <= 5:
                    why = 'Xbox ADPCM pads waves to 64-frame blocks: %+d ms' % (int(b[k]) - int(a[k]))
                elif k in ('min', 'max', 'rolloff') and abs(float(a[k]) - float(b[k])) <= 1e-4 * max(1.0, abs(float(a[k]))):
                    continue
                add('sound.' + k, '%s sound %d %s' % (stem, i, name), a.get(k), b.get(k), why)
            if a['rpcs'] != b['rpcs']:
                add('sound.rpcs', '%s %s' % (stem, name), a['rpcs'], b['rpcs'])
            if len(a['tracks']) != len(b['tracks']):
                add('sound.tracks', '%s %s' % (stem, name), len(a['tracks']), len(b['tracks']))
                continue
            for t, (ta, tb) in enumerate(zip(a['tracks'], b['tracks'])):
                for k in ('play', 'time', 'delay', 'loops', 'newvar', 'playlist', 'pitch', 'volume'):
                    if ta.get(k) != tb.get(k):
                        add('track.' + k, '%s %s track %d' % (stem, name, t), ta.get(k), tb.get(k))
                ra = [(w['bank'], w['entry']) for w in ta['waves']]
                rb = [(w['bank'], w['entry']) for w in tb['waves']]
                if ra != rb:
                    add('track.waves', '%s %s track %d' % (stem, name, t), ra, rb)
                elif [w['weight'] for w in ta['waves']] != [w['weight'] for w in tb['waves']]:
                    add('track.weights', '%s %s track %d' % (stem, name, t), [w['weight'] for w in ta['waves']],
                        [w['weight'] for w in tb['waves']], KNOWN_WEIGHTS.get(name))
                if ta['events'] != tb['events']:
                    add('track.events', '%s %s track %d' % (stem, name, t), ta['events'], tb['events'])
    print('compared: %d banks, %d sounds, %d cues (the device\'s tables from the cooked .uc2cues and from the .xsb)' % (
        banks, sounds, cues))
    if missing:
        print('no .xsb dump for: %s' % ' '.join(missing))
    new = 0
    for field in sorted(diffs):
        items = diffs[field]
        known = sum(1 for w, _ in items if w)
        print('DIFF %-16s %5d (%d known, %d NEW)' % (field, len(items), known, len(items) - known))
        for w, text in items:
            print('    %s %s%s' % ('known' if w else 'NEW  ', text, ' -- ' + w if w else ''))
        new += len(items) - known
    print('RESULT: %d differences, %d of them NEW (not a documented inference)' % (
        sum(len(v) for v in diffs.values()), new))
    return 1 if new or missing else 0


if __name__ == '__main__':
    sys.exit(main())
