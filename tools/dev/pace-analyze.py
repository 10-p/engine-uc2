#!/usr/bin/env python3
"""Analyse the pace trace (PACETRACE, Source/Engine/Src/UC2PaceTrace.cpp) in a game log: frame times, the game-time
steps the engine ticked with, the swaps against the display's refresh, repeated and skipped frames.

    tools/dev/pace-analyze.py [--hz 59.96] [--tag <tag>] <Avaris.log or stdout> [...]

--hz is the display's refresh rate (xrandr's current rate on Linux); without it a vsync trace's median step is taken
as one refresh, and 60 Hz otherwise. Each trace (tag) in the files is analysed on its own. Per trace:
  dt        the game-time step each frame was ticked with (the launcher's DeltaTime)
  step      the time between two frames' swap returns (when each frame was handed to the display)
  swap      how long the swap call took (it waits there for the display when vsync blocks)
  limiter   the frame limiter's requested sleep and its oversleep (how much longer it took)
  refresh   each frame placed on the display's refresh grid by the timestamp that sits on it most tightly (the
            swap's return when the swap waits for the display; the grid's phase is estimated from the frames): with
            vsync the frames lock to the grid, and a step of 2 refreshes is a frame shown twice (a repeat), 0 a frame
            never shown (a skip); the spread says how tightly each timestamp sits on the grid
  mismatch  a frame's dt minus the time from the picture before to its own on the display (refresh steps x the
            period when locked, else the step); a frame's picture goes out with the next frame's swap, as
            UGameEngine::Draw presents the last frame before it draws the next. A steady frame rate that stutters has
            a large mismatch
The last line, 'verdict: ...', sums it up.
"""
import argparse
import math
import re
import sys

FRAME = re.compile(r'pacetrace (\S+) (\d+) start=(\S+) dt=(\S+) tick=(\S+) swap0=(\S+) swap1=(\S+) end=(\S+) '
                   r'want=(\S+) slept=(\S+) swaps=(\d+)(?: vbl=(-?\d+))?')
BEGIN = re.compile(r'pacetrace (\S+) begin (\d+) frames device=(\S+) vsync=(\S+) cap=(\S+) view=(\S+)(?: hz=(\S+))?')


def stats(v):
    if not v:
        return 'none'
    s = sorted(v)
    n = len(s)
    avg = sum(s) / n
    sd = math.sqrt(sum((x - avg) ** 2 for x in s) / n)
    pick = lambda q: s[min(n - 1, int(n * q))]
    return 'avg %.3f p50 %.3f p1 %.3f p99 %.3f min %.3f max %.3f sd %.3f (n %d)' % (
        avg, pick(0.5), pick(0.01), pick(0.99), s[0], s[-1], sd, n)


def hist(v, width):
    """Counts per bin of <width> ms, the bins that hold anything."""
    b = {}
    for x in v:
        k = int(math.floor(x / width))
        b[k] = b.get(k, 0) + 1
    return ', '.join('%g-%g: %d' % (k * width, (k + 1) * width, b[k]) for k in sorted(b))


def analyse(tag, frames, begin, hz):
    print('== trace %s: %s' % (tag, begin or 'no begin line'))
    f = [fr for fr in frames]
    n = len(f)
    if n < 3:
        print('too few frames: %d' % n)
        print('verdict: %s: too few frames' % tag)
        return
    if not hz:
        # No rate given: a vsync trace's median step is one refresh; otherwise 60 Hz.
        steps = sorted(f[i]['swap1'] - f[i - 1]['swap1'] for i in range(1, n) if f[i]['swaps'] and f[i - 1]['swaps'])
        hz = 1000.0 / steps[len(steps) // 2] if begin and 'vsync=True' in begin and steps else 60.0
        print('display rate not given: %.3f Hz (%s)' % (hz, 'the median step' if hz != 60.0 else 'assumed'))
    P = 1000.0 / hz
    dt = [x['tick'] for x in f[1:]]
    secs = (f[-1]['start'] - f[0]['start']) / 1000.0
    print('frames %d over %.2f s: %.2f fps; display %.3f Hz (period %.3f ms)' % (n, secs, (n - 1) / secs if secs else 0, hz, P))
    print('dt (game step)   ms: ' + stats(dt))
    print('dt histogram, 1 ms bins: ' + hist(dt, 1.0))
    sw = [x for x in f if x['swaps'] > 0]
    step = [f[i]['swap1'] - f[i - 1]['swap1'] for i in range(1, n) if f[i]['swaps'] and f[i - 1]['swaps']]
    print('step (swap to swap) ms: ' + stats(step))
    print('swap wait        ms: ' + stats([x['swap1'] - x['swap0'] for x in sw]))
    print('work (start to swap) ms: ' + stats([x['swap0'] - x['start'] for x in sw]))
    lim = [x for x in f if x['want'] >= 0]
    if lim:
        print('limiter want     ms: ' + stats([x['want'] for x in lim]))
        print('limiter oversleep ms: ' + stats([x['slept'] - x['want'] for x in lim]))
    else:
        print('limiter: did not run')
    # The refresh grid: for each timestamp of the frame, its phase on the refresh grid (circular mean) and its spread
    # around it. Where the driver makes the game wait for the display, that timestamp sits on the grid; the most
    # tightly placed one is the display's reference (the swap's return when the swap itself waits).
    def grid(key):
        ang = [2 * math.pi * ((x[key] % P) / P) for x in sw]
        cs, sn = sum(math.cos(a) for a in ang) / len(ang), sum(math.sin(a) for a in ang) / len(ang)
        R = math.hypot(cs, sn)
        ph = (math.atan2(sn, cs) / (2 * math.pi)) % 1.0 * P
        sp = P * math.sqrt(-2 * math.log(R)) / (2 * math.pi) if 0 < R < 1 else (0.0 if R >= 1 else P)
        return ph, sp
    grids = {k: grid(k) for k in ('start', 'swap0', 'swap1', 'end')}
    print('refresh grid spread ms: ' + ', '.join('%s %.3f' % (k, grids[k][1]) for k in ('start', 'swap0', 'swap1', 'end')))
    ref = min(grids, key=lambda k: grids[k][1])
    phase, spread = grids[ref]
    locked = spread < 0.1 * P
    print('refresh grid: %s at phase %.3f ms, spread %.3f ms (%s)' % (
        ref, phase, spread, 'locked to the refresh' if locked else 'not locked: nothing in the frame waits for the display'))
    idx = {}
    if all(x['vbl'] >= 0 for x in sw):
        # The platform's own count of the display's refreshes at each swap's return (Windows: the compositor's);
        # with vsync on, the frames are on the display's refreshes whether or not the timestamps show it.
        ref = 'swap1'
        locked = locked or bool(begin and 'vsync=True' in begin)
        print('refresh count: the display clock of the platform (vbl), at the swap returns')
        for i, x in enumerate(f):
            if x['swaps']:
                idx[i] = x['vbl']
    else:
        for i, x in enumerate(f):
            if x['swaps']:
                idx[i] = int(math.floor((x[ref] - phase) / P + 0.5))
    d = [idx[i] - idx[i - 1] for i in range(1, n) if i in idx and i - 1 in idx]
    dh = {}
    for k in d:
        dh[k] = dh.get(k, 0) + 1
    print('refreshes between swaps: ' + ', '.join('%d: %d' % (k, dh[k]) for k in sorted(dh)))
    repeats = sum(1 for k in d if k >= 2)
    skips = sum(1 for k in d if k <= 0)
    # Mismatch: a frame's game step against the time between its picture and the one before on the display.
    # UGameEngine::Draw presents the last frame before it draws the next, so frame j's picture goes out with frame
    # j+1's swap (and with frame j+2's start when that is the timestamp on the grid: it follows frame j+1's swap and
    # render).
    off = 2 if ref == 'start' else 1
    mis = []
    for j in range(1, n - off):
        a, b = j + off - 1, j + off
        if a in idx and b in idx:
            shown = (idx[b] - idx[a]) * P if locked else f[b][ref] - f[a][ref]
            mis.append(f[j]['tick'] - shown)
    rms = math.sqrt(sum(m * m for m in mis) / len(mis)) if mis else 0.0
    over2 = sum(1 for m in mis if abs(m) > 2.0)
    overq = sum(1 for m in mis if abs(m) > P / 4)
    print('mismatch (dt - shown) ms: ' + stats(mis))
    print('mismatch: rms %.3f ms; over 2 ms in %d of %d frames; over a quarter refresh (%.2f ms) in %d' % (
        rms, over2, len(mis), P / 4, overq))
    med = sorted(dt)[len(dt) // 2]
    longf = sum(1 for x in dt if x > 1.5 * med)
    print('long frames (dt over 1.5 x median %.3f ms): %d' % (med, longf))
    if locked:
        bad = repeats + skips + overq
        what = 'steady' if bad == 0 and over2 <= len(mis) // 100 else 'STUTTER'
        print('verdict: %s: %s, vsync-locked %.2f fps; %d repeats, %d skips; game step off the display by over 2 ms in '
              '%d of %d frames (rms %.3f ms), over a quarter refresh in %d' % (
                  tag, what, (n - 1) / secs if secs else 0, repeats, skips, over2, len(mis), rms, overq))
    else:
        sd = math.sqrt(sum((x - sum(dt) / len(dt)) ** 2 for x in dt) / len(dt))
        what = 'even' if longf == 0 and sd < 1.0 else 'UNEVEN'
        print('verdict: %s: %s, not locked to the refresh, %.2f fps; dt sd %.3f ms, %d long frames; per refresh: %s' % (
            tag, what, (n - 1) / secs if secs else 0, sd, longf, ', '.join('%d: %d' % (k, dh[k]) for k in sorted(dh))))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--hz', type=float, default=None, help='display refresh (default: a vsync trace\'s median step, else 60)')
    ap.add_argument('--tag')
    ap.add_argument('logs', nargs='+')
    a = ap.parse_args()
    traces, begins, rates, order = {}, {}, {}, []
    for path in a.logs:
        with open(path, errors='replace') as fh:
            for line in fh:
                m = BEGIN.search(line)
                if m:
                    begins[m.group(1)] = 'vsync=%s cap=%s view=%s device=%s' % (m.group(4), m.group(5), m.group(6), m.group(3))
                    if m.group(7) and float(m.group(7)) > 0:
                        rates[m.group(1)] = float(m.group(7))
                    continue
                m = FRAME.search(line)
                if not m:
                    continue
                t = m.group(1)
                if a.tag and t != a.tag:
                    continue
                if t not in traces:
                    traces[t] = []
                    order.append(t)
                v = [float(x) for x in m.groups()[2:10]]
                traces[t].append(dict(i=int(m.group(2)), start=v[0], dt=v[1], tick=v[2], swap0=v[3], swap1=v[4],
                                      end=v[5], want=v[6], slept=v[7], swaps=int(m.group(11)),
                                      vbl=int(m.group(12)) if m.group(12) is not None else -1))
    if not order:
        print('verdict: no pace trace in ' + ' '.join(a.logs))
        return 1
    for t in order:
        analyse(t, traces[t], begins.get(t), a.hz or rates.get(t))
    return 0


if __name__ == '__main__':
    sys.exit(main())
