# G-6 helpers for tools/dev/m10-inject.py scenarios (loaded with `pyfile`; tools/dev/g6-splitscreen.sh).
# They read V/VS (the `vprobe` results) and compute what Scion's USplitScreenMgr::UpdateSizes
# (Source/Engine/Src/UnSplitScreenMgr.cpp) gives each split view, so a check compares the engine with the
# layout written down independently here.
import json


def layout(n, w, h):
    """Rectangles (x, y, w, h) of split views 0..n-1 on a w x h screen, BorderThickness 0."""
    hw, hh = w // 2, h // 2
    if n <= 1:
        return [(0, 0, w, h)]
    if n == 2:
        return [(0, 0, w, hh), (0, hh, w, hh)]
    if n == 3:
        return [(0, 0, w, hh), (0, hh, hw, hh), (hw, hh, hw, hh)]
    return [(0, 0, hw, hh), (hw, 0, hw, hh), (0, hh, hw, hh), (hw, hh, hw, hh)]


def layout_ok(tag):
    n, nsplit, (w, h) = VS[tag]
    rects = vrects(tag)
    if nsplit == 0:
        return n == 1 and V[tag][0]['rect'] == (0, 0, w, h) and V[tag][0]['menu'] == 1 and V[tag][0]['scene'] == 1
    over = [r for r in V[tag] if r['overlay']]
    return (rects == layout(nsplit, w, h) and n == nsplit + 1 and len(over) == 1
            and over[0]['rect'] == (0, 0, w, h) and over[0]['menu'] == 1 and over[0]['scene'] == 0
            and all(r['menu'] == 0 and r['scene'] == 1 and r['disabled'] == 0 for r in vsplit(tag)))


def pads_in_join_order(tag):
    return [r['pad'] for r in vsplit(tag)] == list(range(len(vsplit(tag))))


def all_split_huds(tag):
    return all(r['hud'].endswith('_Split') for r in vsplit(tag)) and all(r['pawn'] == 1 for r in vsplit(tag))


def moved(a, b):
    """Distance each split player's pawn moved between vprobes a and b (split index order)."""
    return [round(vdist(a, b, i), 1) for i in range(len(vsplit(a)))]


def only_moved(a, b, k, far=50.0, still=2.0):
    m = moved(a, b)
    return m[k] > far and all(d < still for i, d in enumerate(m) if i != k)


def only_moved2(a, b, c, k, far=50.0, still=2.0):
    """Forward then back (a wall in front of one of them does not decide the check)."""
    m1, m2 = moved(a, b), moved(b, c)
    return m1[k] + m2[k] > far and all(m1[i] + m2[i] < still for i in range(len(m1)) if i != k)


def fired(a, b):
    """Shots (weapon FlashCount changes) of each split player between vprobes a and b."""
    return [(vsplit(b)[i]['shots'] - vsplit(a)[i]['shots']) & 255 for i in range(len(vsplit(a)))]


def only_fired(a, b, k):
    f = fired(a, b)
    return f[k] > 0 and all(d == 0 for i, d in enumerate(f) if i != k)


def turned(a, b):
    return [vyaw(a, b, i) for i in range(len(vsplit(a)))]


def only_turned(a, b, k, far=500, still=30):
    t = turned(a, b)
    return abs(t[k]) > far and all(abs(d) < still for i, d in enumerate(t) if i != k)


def dump(path):
    with open(path, 'w') as f:
        json.dump({'V': V, 'VS': VS}, f, indent=1)
    return True


def spawn(k, tries=10):
    """Press pad k's fire until its split player has a pawn (no shots once it has one)."""
    for _ in range(tries):
        vprobe('_s')
        rows = [r for r in V['_s'] if r['pad'] == k and r['split'] >= 0]
        if rows and rows[0]['pawn']:
            print(f'  spawn: pad {k} has a pawn', flush=True)
            return True
        chan(f'pad {k} axis righttrigger 32767'); time.sleep(0.3)
        chan(f'pad {k} axis righttrigger 0'); time.sleep(1.5)
    return False


def fire_all(npads):
    """Every pad's fire once (a match with several local players starts when all of them are ready)."""
    for k in range(npads):
        chan(f'pad {k} axis righttrigger 32767')
    time.sleep(0.3)
    for k in range(npads):
        chan(f'pad {k} axis righttrigger 0')
    time.sleep(1.0)


def only_accel(tag, k):
    """During a key's hold: split player k's pawn accelerates, nobody else's does."""
    acc = [abs(r['acc'][0]) + abs(r['acc'][1]) for r in vsplit(tag)]
    return acc[k] > 100 and all(a == 0 for i, a in enumerate(acc) if i != k)


def only_moved4(tags, k, far=50.0, still=2.0):
    """Over several walks (W S A D): split player k moved, nobody else did."""
    tot = [0.0] * len(vsplit(tags[0]))
    for a, b in zip(tags, tags[1:]):
        for i, d in enumerate(moved(a, b)):
            tot[i] += d
    return tot[k] > far and all(t < still for i, t in enumerate(tot) if i != k)


# G-9/G-11 (PadsStartAtPlayerTwo=True): the keyboard and the mouse are player one on port 0, a pad joins on port 1..3, so the
# virtual pad v of the test channel is port v+1. The helpers below take both.
def spawn_port(port, vpad, tries=10):
    """Press virtual pad vpad's fire until the split player on port `port` has a pawn."""
    for _ in range(tries):
        vprobe('_s')
        rows = [r for r in V['_s'] if r['pad'] == port and r['split'] >= 0]
        if rows and rows[0]['pawn']:
            print(f'  spawn: port {port} has a pawn', flush=True)
            return True
        chan(f'pad {vpad} axis righttrigger 32767'); time.sleep(0.3)
        chan(f'pad {vpad} axis righttrigger 0'); time.sleep(1.5)
    return False


def ports(tag):
    """The pad port (GamepadID) of every split view in order, or of the one viewport when not split."""
    return [r['pad'] for r in vsplit(tag)] or [V[tag][0]['pad']]


def still_all(a, b, still=2.0):
    """Nobody's pawn moved between a and b (split views, or the one viewport)."""
    if vsplit(a):
        return all(d < still for d in moved(a, b))
    return dist_v0(a, b) < still


def dist_v0(a, b):
    la, lb = V[a][0]['loc'], V[b][0]['loc']
    return sum((p - q) ** 2 for p, q in zip(la, lb)) ** 0.5
