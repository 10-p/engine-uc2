#!/usr/bin/env python3
"""The order the disc cook writes a static mesh's RawTriangles in, for the sections whose material shows draw order
(tools/uc2xmat.py): one under which an editor rebuild draws the section's triangles in the disc's order. Used by
tools/uc2xrawtri.py.

    import uc2xorder
    order, results = uc2xorder.solve(pos, uv, colors, tris, bits, order, shows, use_strips)
    # order: [(triangle, corners)] as tools/uc2xrawtri.py's _order gives it; shows: per section
    # results: per section that shows order, 'drawn as on the disc', 'reordered (<how>)' or 'not found'

UStaticMesh::Build welds the RawTriangles' corners into vertices (numbered as it first meets them), then strips
each section twice with xbstrip (tools/uc2xstrip.py): the first pass for the wireframe, the second on the first's
output. Both passes break ties by the order of their input, so the order and corner turn of a section's
triangles decide which strips come out and in which order. For a section the disc draws otherwise, orders are
tried, each kept to the rule that a triangle bringing new vertices comes when they are the next numbers (so the
rebuild numbers the vertices as the disc does): the cook's own order, orders by the triangles' vertex numbers, then
short searches from random keys and turns, changing those near the first triangle where the rebuild's draw order
leaves the disc's. No search runs where Build splits a vertex the disc's strip runs across (its corners get
normals that do not weld): no strip can run from one side to the other there. The searches are seeded and their
budget counts uc2xstrip's work, not time, so a cook gives the same order every time. The other sections keep
their order and turns; the sections are then interleaved again by their vertex numbers.
"""
import heapq
import math
import random
import struct

import uc2xstrip

# PointsEqual's and NormalsEqual's tolerances (THRESH_POINTS_ARE_SAME * 4, THRESH_NORMALS_ARE_SAME * 4), the UV
# tolerance of FindVertexIndex, SafeNormal's SMALL_NUMBER
_f = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]  # noqa: E731
PT = _f(_f(0.002) * 4.0)
NT = _f(_f(0.00002) * 4.0)
UVT = 1.0 / 1024.0
SMALL_NUMBER = 1e-8
CELL = 2.0 * PT
# the search: tries of TRY_RESTARTS random orders and TRY_LOCAL changes from the best of them, each try seeded by the
# section's size and its number, until SEARCH_WORK of uc2xstrip's work (about a minute of it) is spent
SEARCH_WORK = 45000000
TRY_RESTARTS = 40
TRY_LOCAL = 300
MAX_TRIES = 1000
KEY_STEPS = (2.0, 10.0, 50.0, 200.0)
RESTART_SCALES = (3.0, 10.0, 30.0, 100.0)
NEAR = 8


def _turn(t):
    k = t.index(min(t))
    return tuple(t[k:]) + tuple(t[:k])


def _cell(p):
    return (math.floor(p[0] / CELL), math.floor(p[1] / CELL), math.floor(p[2] / CELL))


def _near(grid, p):
    cx, cy, cz = _cell(p)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for q, item in grid.get((cx + dx, cy + dy, cz + dz), ()):
                    if abs(p[0] - q[0]) <= PT and abs(p[1] - q[1]) <= PT and abs(p[2] - q[2]) <= PT:
                        yield item


def _face_normal(a, b, c):
    """FPlane(V2, V1, V0)'s normal."""
    ax, ay, az = b[0] - c[0], b[1] - c[1], b[2] - c[2]
    bx, by, bz = a[0] - c[0], a[1] - c[1], a[2] - c[2]
    x, y, z = ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx
    s = x * x + y * y + z * z
    if s < SMALL_NUMBER:
        return (0.0, 0.0, 0.0)
    k = 1.0 / math.sqrt(s)
    return (x * k, y * k, z * k)


def welded(pos, uv, colors, tris, bits, order):
    """Build's vertex per corner: {triangle: {its disc vertex: Build's vertex}} for the triangles written in order,
    a triangle Build drops left out."""
    grid = {}
    for k, (t, tv) in enumerate(order):
        for v in tv:
            grid.setdefault(_cell(pos[v]), []).append((pos[v], k))
    fn = [_face_normal(pos[tv[0]], pos[tv[1]], pos[tv[2]]) for t, tv in order]
    at = {}         # disc vertex: the triangles with a corner at its point (PointsEqual)
    smooth = {}     # (disc vertex, smoothing mask): the corner's normal
    verts = []
    vgrid = {}
    out = {}
    for k, (t, tv) in enumerate(order):
        p = [pos[v] for v in tv]
        if any(all(abs(p[a][x] - p[b][x]) <= PT for x in range(3)) for a, b in ((0, 1), (0, 2), (1, 2))):
            continue
        corner = {}
        for v in tv:
            # the face normals of the triangles there that share a smoothing bit with this one, and its own
            if bits[t]:
                n = smooth.get((v, bits[t]))
                if n is None:
                    if v not in at:
                        at[v] = sorted(set(_near(grid, pos[v])))
                    s = [0.0, 0.0, 0.0]
                    for u in at[v]:
                        if bits[order[u][0]] & bits[t]:
                            s[0] += fn[u][0]
                            s[1] += fn[u][1]
                            s[2] += fn[u][2]
                    ln = s[0] * s[0] + s[1] * s[1] + s[2] * s[2]
                    n = smooth[(v, bits[t])] = tuple(x / math.sqrt(ln) for x in s) if ln >= SMALL_NUMBER else tuple(s)
            else:
                n = fn[k]
            found = None
            for j in sorted(_near(vgrid, pos[v])):
                pv, nv, cv, uvv = verts[j]
                if any(abs(n[x] - nv[x]) > NT for x in range(3)) or cv != colors[4 * v:4 * v + 4]:
                    continue
                if all(abs(a[0] - b[0]) <= UVT and abs(a[1] - b[1]) <= UVT for a, b in zip(uvv, uv[v])):
                    found = j
                    break
            if found is None:
                found = len(verts)
                verts.append((pos[v], n, colors[4 * v:4 * v + 4], uv[v]))
                vgrid.setdefault(_cell(pos[v]), []).append((pos[v], found))
            corner[v] = found
        if len(set(corner[v] for v in tv)) == 3:
            out[t] = corner
    return out


class Section:
    """One section's triangles, the disc's draw order of them in Build's vertices, and Build of an order."""

    def __init__(self, tris, si, wmap, today, use_strips):
        self.tris = tris
        self.mine = [t for t, (s, tv) in enumerate(tris) if s == si and t in wmap]
        self.wmap = wmap
        self.use_strips = use_strips
        self.today = [(t, tv) for t, tv in today if tris[t][0] == si and t in wmap]
        self.target = [_turn(tuple(wmap[t][v] for v in tris[t][1])) for t in self.mine]
        verts = sorted({v for t in self.mine for v in tris[t][1]})
        self.rank = {v: k for k, v in enumerate(verts)}
        self.evals = 0

    def split_steps(self):
        """How many steps of the disc's strip Build's split vertices break: two triangles drawn one after the
        other that share an edge on the disc but not in Build's vertices, so no strip runs from one to the other."""
        out = 0
        for a, b in zip(self.mine, self.mine[1:]):
            shared = set(self.tris[a][1]) & set(self.tris[b][1])
            if len(shared) == 2 and {self.wmap[a][v] for v in shared} != {self.wmap[b][v] for v in shared}:
                out += 1
        return out

    def draw_order(self, seq):
        self.evals += 1
        idx = [self.wmap[t][v] for t, tv in seq for v in tv]
        out = uc2xstrip.build_order(idx, self.use_strips)
        tl = uc2xstrip.strip_to_list(out) if self.use_strips else out
        return [_turn(tuple(tl[3 * k:3 * k + 3])) for k in range(len(tl) // 3)]

    def by_keys(self, key, rot):
        """The section's triangles by key; one that brings new vertices (of the section's own) only when they are
        the next ones, turned to meet them in order (rot picks among the turns that do; any turn for the others)."""
        heap = [(key[j], j) for j in range(len(self.mine))]
        heapq.heapify(heap)
        waiting = {}
        out = []
        nxt = 0
        while heap:
            kj = heapq.heappop(heap)
            t = self.mine[kj[1]]
            orig = self.tris[t][1]
            tv = [self.rank[v] for v in orig]
            if max(tv) < nxt:
                r = rot[kj[1]] % 3
                out.append((t, tuple(orig[r:]) + tuple(orig[:r])))
                continue
            good = [r for r in range(3) if [v for v in tv[r:] + tv[:r] if v >= nxt] ==
                    list(range(nxt, nxt + sum(1 for v in tv if v >= nxt)))]
            if not good:
                new = min(v for v in tv if v >= nxt)
                waiting.setdefault(new if new > nxt else nxt + 1, []).append(kj)
                continue
            r = good[rot[kj[1]] % len(good)]
            out.append((t, tuple(orig[r:]) + tuple(orig[:r])))
            top = max(tv) + 1
            for v in range(nxt + 1, top + 1):
                for kk in waiting.pop(v, ()):
                    heapq.heappush(heap, kk)
            nxt = top
        return out


def _prefix(a, b):
    n = min(len(a), len(b))
    k = 0
    while k < n and a[k] == b[k]:
        k += 1
    return k


def solve_section(sec):
    """(how, order) for a section the disc draws otherwise; (None, None) when no order is found."""
    n = len(sec.mine)
    tv = [sec.tris[t][1] for t in sec.mine]
    T = sec.target
    rot0 = [0] * n
    # orders by the triangles' vertex numbers: they follow the order the mesh's vertices were first numbered in
    schemes = (
        ('drawn order', [float(j) for j in range(n)]),
        ('vertex order', [max(v) * 1e6 + sorted(v)[1] * 1e3 + min(v) for v in tv]),
        ('vertex order, drawn order within', [max(v) * 1e4 + j for j, v in enumerate(tv)]),
        ('first-vertex order', [min(v) * 1e6 + sorted(v)[1] * 1e3 + max(v) for v in tv]),
    )
    for how, key in schemes:
        seq = sec.by_keys(key, rot0)
        if sec.draw_order(seq) == T:
            return how, seq
    if sec.split_steps():
        return 'split', None
    near_of = {}
    for j, t in enumerate(sec.mine):
        near_of.setdefault(_turn(tuple(sec.wmap[t][v] for v in sec.tris[t][1])), []).append(j)
    start = uc2xstrip.WORK[0]
    for attempt in range(MAX_TRIES):
        rng = random.Random(n * 1000 + attempt)
        best = None
        # random keys and turns
        for k in range(TRY_RESTARTS):
            sc = RESTART_SCALES[k % len(RESTART_SCALES)]
            key = [j + (rng.random() - 0.5) * sc for j in range(n)]
            rot = [int(rng.random() * 3) for _ in range(n)]
            seq = sec.by_keys(key, rot)
            got = sec.draw_order(seq)
            if got == T:
                return 'search', seq
            p = _prefix(got, T)
            if best is None or p > best[0]:
                best = (p, key, rot)
            if uc2xstrip.WORK[0] - start > SEARCH_WORK:
                return None, None
        # then from the best, changes near where the rebuild leaves the disc's order, kept while that point does
        # not move back
        p, key, rot = best
        for k in range(TRY_LOCAL):
            near = []
            for d in range(-NEAR, NEAR + 1):
                if 0 <= p + d < len(T):
                    near += near_of.get(T[p + d], [])
            j = rng.choice(near) if near and rng.random() > 0.1 else rng.randrange(n)
            old = (key[j], rot[j])
            mv = rng.random()
            if mv < 0.4:
                key[j] -= rng.random() * rng.choice(KEY_STEPS)
            elif mv < 0.8:
                key[j] += rng.random() * rng.choice(KEY_STEPS)
            else:
                rot[j] += 1
            seq = sec.by_keys(key, rot)
            got = sec.draw_order(seq)
            if got == T:
                return 'search', seq
            q = _prefix(got, T)
            if q >= p:
                p = q
            else:
                key[j], rot[j] = old
            if uc2xstrip.WORK[0] - start > SEARCH_WORK:
                return None, None
    return None, None


def merge(tris, seqs, today):
    """One order of all triangles from per-section orders, each kept as it is, vertex-valid: a triangle bringing new
    vertices comes when they are the next numbers; among the sections whose next triangle can come, the one that
    came first in today's order. None when none can."""
    at = {t: k for k, (t, tv) in enumerate(today)}
    heads = {s: 0 for s in seqs}
    out = []
    nxt = 0
    total = sum(len(q) for q in seqs.values())
    while len(out) < total:
        best = None
        for s, h in heads.items():
            if h >= len(seqs[s]):
                continue
            t, tv = seqs[s][h]
            new = [v for v in tv if v >= nxt]
            if new != list(range(nxt, nxt + len(new))):
                continue
            if best is None or at[t] < at[best[1]]:
                best = (s, t, tv, len(new))
        if best is None:
            return None
        s, t, tv, k = best
        out.append((t, tv))
        heads[s] += 1
        nxt += k
    return out


def _same_welds(a, b):
    """Do two welds join the same corners (Build's vertex numbers aside)?"""
    if a.keys() != b.keys():
        return False
    fwd, back = {}, {}
    for t, ca in a.items():
        cb = b[t]
        for v, x in ca.items():
            y = cb[v]
            if fwd.setdefault(x, y) != y or back.setdefault(y, x) != x:
                return False
    return True


def solve(pos, uv, colors, tris, bits, today, shows, use_strips):
    """(order, results): today's order with the sections that show draw order reordered where the disc draws them
    otherwise (see the module's header)."""
    want = sorted({s for s, tv in tris if s < len(shows) and shows[s]})
    if not want:
        return today, {}
    wmap = welded(pos, uv, colors, tris, bits, today)
    results = {}
    seqs = {}
    for s in sorted({s for s, tv in tris}):
        seqs[s] = [(t, tv) for t, tv in today if tris[t][0] == s]
    changed = False
    for s in want:
        sec = Section(tris, s, wmap, today, use_strips)
        if not sec.mine or len(sec.mine) != len(seqs[s]):
            results[s] = 'not found (triangles Build drops)'
            continue
        if sec.draw_order(sec.today) == sec.target:
            results[s] = 'drawn as on the disc'
            continue
        how, seq = solve_section(sec)
        if seq is None:
            results[s] = ('not found (Build splits vertices the disc\'s strip runs across)' if how == 'split'
                          else 'not found')
            continue
        seqs[s] = seq
        results[s] = 'reordered (%s)' % how
        changed = True
    if not changed:
        return today, results
    order = merge(tris, seqs, today)
    if order is None:
        return today, {s: ('not found (sections share vertices)' if r.startswith('reordered') else r)
                       for s, r in results.items()}
    # the welds and draw orders again from the order written: the same corners share vertices, and the reordered
    # sections draw as the disc does
    w2 = welded(pos, uv, colors, tris, bits, order)
    if not _same_welds(wmap, w2):
        return today, {s: ('not found (the merged order welds otherwise)' if r.startswith('reordered') else r)
                       for s, r in results.items()}
    for s, r in list(results.items()):
        if r.startswith('reordered'):
            sec = Section(tris, s, w2, order, use_strips)
            if sec.draw_order(sec.today) != sec.target:
                return today, {s2: ('not found (the merged order welds otherwise)' if r2.startswith('reordered')
                                    else r2) for s2, r2 in results.items()}
    return order, results
