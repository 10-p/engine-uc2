#!/usr/bin/env python3
"""Compare the program-test lines of two game logs (M11): the desktop GL binding against the GL4ES twin.

  tools/dev/m11-progcmp.py <desktop.out> <twin.out> [tolerance, default 2]

`POSTSELFTEST` / `-glselftest` makes the driver draw every usable pixel shader with five vertex-shader enums
and four sets of inputs each and log the centre pixel (`UC2GLDrv: progtest <n> <name> v<k>: rrggbbaa x4`).
Prints one summary line and every program whose largest channel difference is above the tolerance; exit 0
only if the two logs have the same programs and all are within it.
"""
import re, sys
def load(path):
    out = {}; summary = None
    for line in open(path, 'rb').read().decode('latin-1').splitlines():
        m = re.search(r'UC2GLDrv: progtest (\d+) (\S+) v(\d+):((?: [0-9a-f]{8})+|\s*NO PROGRAM)', line)
        if m:
            key = (int(m.group(1)), m.group(2), int(m.group(3)))
            out[key] = None if 'NO PROGRAM' in m.group(4) else [[int(v[i:i+2], 16) for i in (0, 2, 4, 6)] for v in m.group(4).split()]
        elif 'progtest summary' in line:
            summary = line[line.index('progtest summary'):]
    return out, summary
a, sa = load(sys.argv[1]); b, sb = load(sys.argv[2]); tol = int(sys.argv[3]) if len(sys.argv) > 3 else 2
bad = []; worst = 0; values = 0; exact = 0; discards = 0
for key in sorted(set(a) | set(b)):
    va, vb = a.get(key), b.get(key)
    if va is None or vb is None or len(va) != len(vb):
        bad.append('%s v%d: missing on one side' % (key[1], key[2])); continue
    d = max(abs(x - y) for ca, cb in zip(va, vb) for x, y in zip(ca, cb))
    values += sum(len(c) for c in va); exact += sum(1 for ca, cb in zip(va, vb) for x, y in zip(ca, cb) if x == y)
    discards += sum(1 for ca in va if ca == [128, 64, 192, 32])
    worst = max(worst, d)
    if d > tol:
        bad.append('%s v%d: largest difference %d: %s | %s' % (key[1], key[2], d, ' '.join('%02x%02x%02x%02x' % tuple(c) for c in va), ' '.join('%02x%02x%02x%02x' % tuple(c) for c in vb)))
for line in bad: print('  ' + line)
print('progtest: %d programs on side A, %d on side B, %d values compared, %d identical, largest difference %d of 255 (tolerance %d), %d cases discarded by the shader, %d programs outside the tolerance or missing'
      % (len(a), len(b), values, exact, worst, tol, discards, len(bad)))
sys.exit(0 if a and len(a) == len(b) and not bad else 1)
