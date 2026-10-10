#!/usr/bin/env python3
"""G-6: per-cue table of UC2ALAudio cue logs (DevSound lines "UC2Audio: [t] <cue> on <actor>: played|culled|...").

    g6-cuetab.py <log>.out [<log>.out ...]     one column per log: count per (cue, outcome), and the totals
"""
import re, sys, collections
runs = sys.argv[1:]
tabs = {}
for r in runs:
    c = collections.Counter()
    for l in open(r, encoding='latin-1'):
        m = re.search(r'UC2Audio: \[[ \d.]+\] (\S+)(?: on (\S+?))?: (played|culled|failed|no voice)', l)
        if m:
            c[(m.group(1), m.group(3).split()[0])] += 1
    tabs[r] = c
keys = sorted(set(k for c in tabs.values() for k in c))
names = [r.split('/')[-2] + '/' + r.split('/')[-1].replace('.out', '') for r in runs]
print('%-45s %-7s ' % ('cue', 'outcome') + ' '.join('%14s' % n[-14:] for n in names))
for k in keys:
    print('%-45s %-7s ' % (k[0][-45:], k[1]) + ' '.join('%14d' % tabs[r][k] for r in runs))
print('%-53s ' % 'TOTAL' + ' '.join('%14d' % sum(tabs[r].values()) for r in runs))
