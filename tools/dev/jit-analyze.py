#!/usr/bin/env python3
"""Jitter of the player's pawn on the screen, from the engine's FRAMETRACE lines ("frametrace <tag> <i> ...") (docs/porting/analysis/jitter.md).

    jit-analyze.py <engine-log> [--json out.json] [--tags run,back,...] [--strip <tag>:<first>:<count>]

For every trace tag in the log: the frames of steady running (walking physics, horizontal speed within 3% of the
tag's highest, the camera's rotation the same as the previous frame's, the velocity's heading within 2 degrees of
the previous frame's) and, over them:
  dt          the frame times (mean, min, max)
  scr         the pawn's projected screen position (viewport pixels): standard deviation in x and y, and
  jitter      the RMS and the largest frame-to-frame change of that position (0 = the pawn stands still on the
              screen while it runs with the camera following, which is what a following camera should show)
  judder      the RMS and the largest distance of each frame's position from the straight line between its two
              neighbours at their times: a smooth movement on the screen (the camera's walk bob, a slow drift while a
              lazy camera settles) gives ~0 at any frame time, a position that depends on the frame's own length
              does not
  nobob       jitter and judder of the point pawn + WalkBob (the camera's target includes WalkBob: with a rigid
              camera that point stands still on the screen unless the view is from another moment than the pawn)
  ahead       the pawn's (+ WalkBob's) distance from the camera along its own velocity, against v x dt of that frame: the
              least-squares slope `ahead = a + b (v dt)`. b = 1: the camera of the frame was computed before the
              pawn's move of that frame (each frame the pawn is drawn its own move ahead of its rest place);
              b = 0: the pawn's place in front of the camera does not depend on the frame time.
              The bob MUST be in it (D-4, 2026-10-10): the camera's target is pawn + WalkBob, and on a strafe the
              bob's sideways sway (+-3.4 units at 430 units/s, about 1.3 Hz) lies along the velocity, so the fit of
              the pawn alone takes that sway for a frame-time dependence wherever a 1.3 s segment's frame times
              happen to correlate with its phase: the browser spec's b = 0.217-0.247 on strafe right (a19) was
              exactly that (the same trace: 0.000 with the bob, 0.217 without; forward and back are unaffected,
              their bob is across the velocity). `ahead_nobob` reports the fit without the bob for comparison only.
  root        the same jitter for the mesh's root bone as posed for the frame (rscr)
  bob         the largest |WalkBob| (the camera's target includes it)
"""
import json, math, re, sys

LINE = re.compile(r'frametrace (\S+) (\d+) t=([-\d.]+) dt=([-\d.]+) cam=\(([^)]*)\) camrot=\(([^)]*)\) fov=([-\d.]+) '
                  r'view=(\d+)x(\d+) pawn=\(([^)]*)\) vel=\(([^)]*)\) bob=\(([^)]*)\) root=\(([^)]*)\) '
                  r'scr=\(([^)]*)\) rscr=\(([^)]*)\) phys=(\d+) look=(\S+)')


def vec(s):
    return [float(v) for v in s.split(',')]


def parse(path):
    tags = {}
    with open(path, 'r', encoding='latin-1', errors='replace') as f:
        for line in f:
            m = LINE.search(line)
            if not m:
                continue
            g = m.groups()
            fr = dict(tag=g[0], i=int(g[1]), t=float(g[2]), dt=float(g[3]), cam=vec(g[4]),
                      camrot=[int(v) for v in g[5].split(',')], fov=float(g[6]), w=int(g[7]), h=int(g[8]),
                      pawn=vec(g[9]), vel=vec(g[10]), bob=vec(g[11]), root=vec(g[12]), scr=vec(g[13]),
                      rscr=vec(g[14]), phys=int(g[15]), look=g[16])
            tags.setdefault(fr['tag'], []).append(fr)
    return tags


def speed2d(fr):
    return math.hypot(fr['vel'][0], fr['vel'][1])


def heading(fr):
    return math.degrees(math.atan2(fr['vel'][1], fr['vel'][0]))


def steady(frames):
    if not frames:
        return []
    top = max(speed2d(f) for f in frames)
    if top < 50:
        return []
    out = []
    for a, b in zip(frames, frames[1:]):
        if b['phys'] != 1 or speed2d(b) < 0.97 * top or speed2d(a) < 0.97 * top:
            continue
        if b['camrot'] != a['camrot']:
            continue
        dh = abs((heading(b) - heading(a) + 180) % 360 - 180)
        if dh > 2:
            continue
        out.append(b)
    return out


def stats(xs):
    n = len(xs)
    if not n:
        return dict(n=0)
    m = sum(xs) / n
    sd = math.sqrt(sum((x - m) ** 2 for x in xs) / n)
    return dict(n=n, mean=m, sd=sd, min=min(xs), max=max(xs))


def runs(frames):
    """Consecutive frame indices as separate runs (a frame-to-frame change is only taken inside one)."""
    out, cur = [], []
    for f in frames:
        if cur and f['i'] != cur[-1]['i'] + 1:
            out.append(cur)
            cur = []
        cur.append(f)
    if cur:
        out.append(cur)
    return out


def jitter(frames, key):
    d = []
    for r in runs(frames):
        for a, b in zip(r, r[1:]):
            d.append(math.hypot(b[key][0] - a[key][0], b[key][1] - a[key][1]))
    if not d:
        return dict(n=0, rms=float('nan'), max=float('nan'))
    return dict(n=len(d), rms=math.sqrt(sum(x * x for x in d) / len(d)), max=max(d))


def project(f, p):
    """A world point in viewport pixels with the frame's camera, as UC2FrameTrace.cpp / FCameraSceneNode do."""
    pitch, yaw, roll = (a * math.pi / 32768.0 for a in f['camrot'])
    cp, sp, cy, sy, cr, sr = math.cos(pitch), math.sin(pitch), math.cos(yaw), math.sin(yaw), math.cos(roll), math.sin(roll)
    x = (cp * cy, cp * sy, sp)
    y = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    z = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    d = [p[i] - f['cam'][i] for i in range(3)]
    fwd = sum(d[i] * x[i] for i in range(3)); right = sum(d[i] * y[i] for i in range(3)); up = sum(d[i] * z[i] for i in range(3))
    t = math.tan(f['fov'] * math.pi / 360.0)
    return (0.5 * f['w'] * (1 + right / (fwd * t)), 0.5 * f['h'] * (1 - up * (f['w'] / f['h']) / (fwd * t)))


def judder(frames, key):
    d = []
    for r in runs(frames):
        for a, b, c in zip(r, r[1:], r[2:]):
            span = c['t'] - a['t']
            if span <= 0:
                continue
            k = (b['t'] - a['t']) / span
            px = a[key][0] + (c[key][0] - a[key][0]) * k
            py = a[key][1] + (c[key][1] - a[key][1]) * k
            d.append(math.hypot(b[key][0] - px, b[key][1] - py))
    if not d:
        return dict(n=0, rms=float('nan'), max=float('nan'))
    return dict(n=len(d), rms=math.sqrt(sum(x * x for x in d) / len(d)), max=max(d))


def ahead_fit(frames, bob=True):
    xs, ys = [], []
    for f in frames:
        v = f['vel'][:2]
        s = math.hypot(*v)
        u = (v[0] / s, v[1] / s)
        # pawn + WalkBob: the point the camera's target follows (a strafe's sideways bob is along the velocity)
        bx, by = (f['bob'][0], f['bob'][1]) if bob else (0.0, 0.0)
        rel = (f['pawn'][0] + bx - f['cam'][0], f['pawn'][1] + by - f['cam'][1])
        ys.append(rel[0] * u[0] + rel[1] * u[1])
        xs.append(s * f['dt'])
    n = len(xs)
    if n < 3:
        return dict(n=n)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    b = sxy / sxx if sxx > 1e-9 else float('nan')
    a = my - b * mx if sxx > 1e-9 else my
    resid = math.sqrt(sum((y - (a + b * x)) ** 2 for x, y in zip(xs, ys)) / n) if sxx > 1e-9 else float('nan')
    return dict(n=n, a=a, b=b, resid=resid, vdt=stats(xs), ahead=stats(ys))


def analyse(frames):
    st = steady(frames)
    res = dict(frames=len(frames), steady=len(st))
    if not st:
        return res
    res['look'] = sorted(set(f['look'] for f in st))
    res['view'] = f"{st[0]['w']}x{st[0]['h']}"
    res['speed'] = stats([speed2d(f) for f in st])
    res['dt'] = stats([f['dt'] for f in st])
    res['fov'] = stats([f['fov'] for f in st])
    res['scr_x'] = stats([f['scr'][0] for f in st])
    res['scr_y'] = stats([f['scr'][1] for f in st])
    for f in st:
        f['nobob'] = project(f, [f['pawn'][i] + f['bob'][i] for i in range(3)])
        f['scr_check'] = project(f, f['pawn'])
    res['projection_check'] = max(math.hypot(f['scr_check'][0] - f['scr'][0], f['scr_check'][1] - f['scr'][1]) for f in st)
    res['jitter'] = jitter(st, 'scr')
    res['judder'] = judder(st, 'scr')
    res['nobob_jitter'] = jitter(st, 'nobob')
    res['nobob_judder'] = judder(st, 'nobob')
    res['root_jitter'] = jitter(st, 'rscr')
    res['root_judder'] = judder(st, 'rscr')
    res['ahead'] = ahead_fit(st)
    res['ahead_nobob'] = ahead_fit(st, bob=False)
    res['bob_max'] = max(math.sqrt(sum(c * c for c in f['bob'])) for f in st)
    return res


def fmt(tag, r):
    if not r.get('steady'):
        return f"{tag}: {r['frames']} frames, no steady running"
    a = r['ahead']
    return (f"{tag}: {r['steady']}/{r['frames']} steady frames, look={','.join(r['look'])}, view {r['view']}, "
            f"speed {r['speed']['mean']:.1f}, dt mean {1000*r['dt']['mean']:.2f} ms "
            f"[{1000*r['dt']['min']:.2f}..{1000*r['dt']['max']:.2f}], fov {r['fov']['mean']:.2f}; "
            f"screen x sd {r['scr_x']['sd']:.2f} px y sd {r['scr_y']['sd']:.2f} px; "
            f"jitter rms {r['jitter']['rms']:.2f} px max {r['jitter']['max']:.2f} px, "
            f"judder rms {r['judder']['rms']:.2f} max {r['judder']['max']:.2f}; "
            f"without the bob: jitter rms {r['nobob_jitter']['rms']:.2f} judder rms {r['nobob_judder']['rms']:.2f} max {r['nobob_judder']['max']:.2f}; "
            f"root bone judder rms {r['root_judder']['rms']:.2f}; "
            f"ahead = {a.get('a', float('nan')):.2f} + {a.get('b', float('nan')):.3f} x v*dt "
            f"(v*dt {a['vdt']['min']:.2f}..{a['vdt']['max']:.2f}, residual {a.get('resid', float('nan')):.2f}; "
            f"without the bob, for comparison only: b {r['ahead_nobob'].get('b', float('nan')):.3f}); "
            f"max |WalkBob| {r['bob_max']:.2f}")


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(2)
    path = args[0]
    out_json = None
    only = None
    strip = []
    i = 1
    while i < len(args):
        if args[i] == '--json':
            out_json = args[i + 1]; i += 2
        elif args[i] == '--tags':
            only = args[i + 1].split(','); i += 2
        elif args[i] == '--strip':
            strip.append(args[i + 1]); i += 2
        else:
            print('unknown argument ' + args[i], file=sys.stderr); sys.exit(2)
    tags = parse(path)
    result = {}
    for tag, frames in tags.items():
        if only and tag not in only:
            continue
        result[tag] = analyse(frames)
        print(fmt(tag, result[tag]))
    for s in strip:
        tag, first, count = s.split(':')
        fr = [f for f in tags.get(tag, []) if int(first) <= f['i'] < int(first) + int(count)]
        for f in fr:
            print(f"  {tag} {f['i']:4d} dt {1000*f['dt']:6.2f} ms  pawn screen ({f['scr'][0]:8.2f},{f['scr'][1]:8.2f})  "
                  f"speed {speed2d(f):6.1f}  pawn-cam {math.dist(f['pawn'][:2], f['cam'][:2]):7.2f}")
    if out_json:
        with open(out_json, 'w') as f:
            json.dump(result, f, indent=1)


if __name__ == '__main__':
    main()
