#!/usr/bin/env python3
"""A static mesh's RawTriangles (the editor's source triangles) rebuilt from its render data, for the PC packages
the disc cook writes. Used by tools/uc2xmesh.py.

    import uc2xrawtri
    count, blob, stats = uc2xrawtri.raw_triangles(vertices, colors, uvs, triangles, shows, use_strips)
    # vertices   PC vertex records (FVector Position, FVector Normal; 24 bytes each)
    # colors     ColorStream elements (4 bytes each, serialised order)
    # uvs        one bytes per UV stream (FStaticMeshUV, 8 bytes per vertex)
    # triangles  [(section, (a, b, c))]: the drawn triangles, strips unrolled as UStaticMesh::Build reads them
    # shows      per section, whether its material shows draw order (tools/uc2xmat.py); use_strips: UseTriStrips
    # blob       count FStaticMeshTriangle records (UnStaticMesh.cpp operator<<, version 112 on); stats: a dict

UStaticMesh::Build (Engine/Src/UnStaticMeshBuild.cpp) makes the render data from RawTriangles: a vertex per
triangle corner, welded with an earlier one of the same position (within THRESH_POINTS_ARE_SAME * 4 per axis),
normal, colour and UVs; the corner's normal is the sum of the face normals of the triangles that touch its position
and share a smoothing group bit with it (and its own), normalised. The disc carries the result, not the source, so
the source is written back from it:

  positions, UVs, colours   the corner's vertex, bit for bit; NumUVs is the number of UV streams
  MaterialIndex             the section (Build makes one section per Materials entry, in order)
  SmoothingMask             from the normals. At a position, the triangles whose vertices there have one normal
                            (one original normal packs to the same bits) are smoothed together when their face
                            normals sum to it; where they do not (a triangle in two smoothing groups), the other
                            triangles there whose face normals complete the sum join them, chosen so that every
                            triangle of the position agrees on who joins whom, the corners of one vertex come to one
                            normal and the positions agree with each other. A different normal parts two triangles,
                            and so does a pair of vertices that only a normal keeps from welding. Bits are then given
                            so that every pair smoothed together shares one and no pair kept apart does (a bit is
                            reused where triangles do not touch); a triangle smoothed with none gets 0
  order and corners         the vertices come back in the cooked order: Build numbers them as it first meets them,
                            so a triangle that brings new vertices comes when they are the next numbers, its
                            corners turned to meet them in order; otherwise the triangles keep their drawn order.
                            A section whose material shows draw order gets an order under which Build draws its
                            triangles as the disc does, where one is found (tools/uc2xorder.py)

Build then gives back the cooked vertex buffer (positions, UVs, colours, order), the same triangles in each section
and normals that differ from the cooked ones by their packing on the disc. It cannot where the disc lost what told
two vertices apart (normals that differed by less than their packing weld into one vertex) or where the rounding of
positions turned a face (on large meshes a face normal moves by up to a few degrees). The index order of each
section's strip is xbstrip's (Engine/Src/xbstrip.cpp), whose tie-breaks follow the triangle order the meshes were
first built from; the disc does not keep that order. Where the material shows draw order, the rebuild draws the
triangles in the disc's order in the sections tools/uc2xorder.py finds an order for.

stats: unexplained (corners no set of triangles there explains: their own face normals are kept), undecided
(positions whose triangles found no agreeing sets), conflicts (pairs one position joins and another parts),
unresolved (pairs smoothed together that no free bit could join), unplaced (triangles left after the numbering
order: only an unused vertex leaves any), draw order ({section: uc2xorder's result} for the sections that show it).
"""
import heapq
import itertools
import math
import struct

import uc2xorder

# PointsEqual's THRESH_POINTS_ARE_SAME * 4.0f, FindVertexIndex's UV tolerance, SafeNormal's SMALL_NUMBER
PT = struct.unpack('<f', struct.pack('<f', struct.unpack('<f', struct.pack('<f', 0.002))[0] * 4.0))[0]
UV_TOLERANCE = 1.0 / 1024.0
SMALL_NUMBER = 1e-8
# the grid cell of the position search: two cells hold every point within PT
CELL = 2.0 * PT
# a normal is the sum of its group's face normals within a degree (the disc's packing and position rounding move
# it less); face normals within a hundredth of a degree make a flat group, whose split leaves its normal as it is
COS_EXPLAINED = math.cos(math.radians(1.0))
COS_FLAT = math.cos(math.radians(0.01))
# a set of triangles whose face normals sum within this of a normal is tried before the others that explain it
COS_CLOSE = math.cos(math.radians(0.3))
MAX_BITS = 32
# a corner's smoothing set is searched among all subsets of up to this many triangles with other normals there
MAX_SEARCH = 10
MAX_SETS = 64
MAX_STEPS = 20000
# two corners of one vertex need normals this close (Build's NormalsEqual welds within 0.00008 per axis)
NORMAL_SAME = 0.00004


def _points_equal(a, b):
    return abs(a[0] - b[0]) <= PT and abs(a[1] - b[1]) <= PT and abs(a[2] - b[2]) <= PT


def _face_normal(p0, p1, p2):
    """FPlane(V2, V1, V0)'s normal: ((V1 - V2) ^ (V0 - V2)).SafeNormal()."""
    ax, ay, az = p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2]
    bx, by, bz = p0[0] - p2[0], p0[1] - p2[1], p0[2] - p2[2]
    x, y, z = ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx
    s = x * x + y * y + z * z
    if s < SMALL_NUMBER:
        return (0.0, 0.0, 0.0)
    k = 1.0 / math.sqrt(s)
    return (x * k, y * k, z * k)


def _positions(pos):
    """Distinct positions, the position id of each vertex, and per position id the ids Build counts as the same
    point (PointsEqual), itself included."""
    ids, uniq, of_vertex = {}, [], []
    for p in pos:
        k = ids.get(p)
        if k is None:
            k = ids[p] = len(uniq)
            uniq.append(p)
        of_vertex.append(k)
    grid = {}
    for k, p in enumerate(uniq):
        grid.setdefault((math.floor(p[0] / CELL), math.floor(p[1] / CELL), math.floor(p[2] / CELL)), []).append(k)
    near = []
    for p in uniq:
        cx, cy, cz = math.floor(p[0] / CELL), math.floor(p[1] / CELL), math.floor(p[2] / CELL)
        out = []
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for q in grid.get((cx + dx, cy + dy, cz + dz), ()):
                        if _points_equal(p, uniq[q]):
                            out.append(q)
        near.append(out)
    return of_vertex, near


def _cos(s, want):
    size = math.sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2])
    if size == 0.0:
        return -2.0
    return (s[0] * want[0] + s[1] * want[1] + s[2] * want[2]) / size


def _unit(s):
    k = 1.0 / math.sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2])
    return (s[0] * k, s[1] * k, s[2] * k)


def _fitting_sets(group, others, fn, want, every=False):
    """The sets of others that join group in a smoothing set whose face normals sum to want within COS_EXPLAINED,
    closest first (at most MAX_SETS), each as (set, whether it comes within COS_CLOSE, the normalised sum): only
    the empty one when group's sum is that close, unless every; beyond MAX_SEARCH others, the set that joins one
    by one while the sum comes closer. Empty: none comes that close."""
    base = [0.0, 0.0, 0.0]
    for u in group:
        base[0] += fn[u][0]
        base[1] += fn[u][1]
        base[2] += fn[u][2]
    alone = _cos(base, want)
    if (alone >= COS_EXPLAINED and not every) or not others:
        return [(frozenset(), True, _unit(base))] if alone >= COS_EXPLAINED else []
    if len(others) > MAX_SEARCH:
        s, best, chosen, rest = list(base), alone, [], list(others)
        while rest and best < COS_EXPLAINED:
            c, u = max((_cos((s[0] + fn[u][0], s[1] + fn[u][1], s[2] + fn[u][2]), want), u) for u in rest)
            if c <= best:
                break
            best = c
            chosen.append(u)
            rest.remove(u)
            s = [s[0] + fn[u][0], s[1] + fn[u][1], s[2] + fn[u][2]]
        return [(frozenset(chosen), True, _unit(s))] if best >= COS_EXPLAINED else []
    fits = []
    for size in range(len(others) + 1):
        for combo in itertools.combinations(others, size):
            s = list(base)
            for u in combo:
                s[0] += fn[u][0]
                s[1] += fn[u][1]
                s[2] += fn[u][2]
            c = _cos(s, want)
            if c >= COS_EXPLAINED:
                fits.append((-c, size, combo, s))
    fits.sort(key=lambda f: f[:2])
    return [(frozenset(combo), -c >= COS_CLOSE, _unit(s)) for c, _n, combo, s in fits[:MAX_SETS]]


def _choose(items, soft):
    """One set per corner of a position (items: (triangle, the triangles sharing its vertex there, others,
    candidates from _fitting_sets)) such that two triangles agree on whether they are smoothed together there and
    the corners of one vertex come to one normal; each corner's sets tried closest first, the close ones alone
    first; soft(t, u) is what other positions said of the pair (None: nothing), followed where it can be. Where
    nothing agrees, each corner in turn takes the set that disagrees least with those before it, and the second
    value is False."""
    index = {t: k for k, (t, _s, _o, _c) in enumerate(items)}
    order = sorted(range(len(items)), key=lambda k: len(items[k][3]))

    def misses(k, cand, chosen, use_soft):
        t, same, others, _c = items[k]
        x, _close, unit = cand
        bad = 0
        for u in same:
            j = index.get(u)
            if u != t and j is not None and chosen[j] is not None:
                o = chosen[j][2]
                bad += max(abs(o[0] - unit[0]), abs(o[1] - unit[1]), abs(o[2] - unit[2])) > NORMAL_SAME
        for u in others:
            inside = u in x
            j = index.get(u)
            if j is not None and chosen[j] is not None:
                bad += (t in chosen[j][0]) != inside
            if use_soft:
                d = soft(t, u)
                bad += d is not None and d != inside
        return bad

    for close, use_soft in ((True, True), (True, False), (False, True), (False, False)):
        cands = [[c for c in it[3] if c[1] or not close] for it in items]
        chosen = [None] * len(items)
        steps = 0
        stack = [0]
        # depth-first over the corners in order, each trying its sets in turn
        while stack:
            i = len(stack) - 1
            if i == len(order):
                return [c[0] for c in chosen], True
            steps += 1
            if steps > MAX_STEPS:
                break
            k = order[i]
            sets = cands[k]
            chosen[k] = None
            j = stack[-1]
            while j < len(sets) and misses(k, sets[j], chosen, use_soft):
                j += 1
            if j == len(sets):
                stack.pop()
                if stack:
                    stack[-1] += 1
                continue
            stack[-1] = j
            chosen[k] = sets[j]
            stack.append(0)
    chosen = [None] * len(items)
    for k in order:
        chosen[k] = min(items[k][3], key=lambda c: misses(k, c, chosen, False))
    return [c[0] for c in chosen], False


TOGETHER, FLAT, APART, GUESSED, WELD = 1, 2, 4, 8, 16


def _smoothing(pos, nrm, attr, tris, stats):
    """SmoothingMask per triangle. attr(v, w): whether Build would weld vertices v and w by their colour and UVs."""
    n = len(tris)
    of_vertex, near = _positions(pos)
    at = [[] for _ in near]
    for t, (_s, tv) in enumerate(tris):
        for c in range(3):
            at[of_vertex[tv[c]]].append((t, tv[c]))
    # positions Build counts as one point, joined: a corner's triangles are among its cluster's
    root = list(range(len(near)))

    def find(k):
        while root[k] != k:
            root[k] = root[root[k]]
            k = root[k]
        return k
    for k, ks in enumerate(near):
        for q in ks:
            a, b = find(k), find(q)
            if a != b:
                root[max(a, b)] = min(a, b)
    fn = [_face_normal(pos[tv[0]], pos[tv[1]], pos[tv[2]]) for _s, tv in tris]
    rel = [dict() for _ in range(n)]

    def mark(t, u, flag):
        rel[t][u] = rel[t].get(u, 0) | flag
        rel[u][t] = rel[u].get(t, 0) | flag
    corners = []
    open_keys = set()
    for t, (_s, tv) in enumerate(tris):
        for c in range(3):
            v = tv[c]
            inc = {}
            for q in near[of_vertex[v]]:
                for u, w in at[q]:
                    inc.setdefault(u, w)
            want = struct.unpack('<3f', nrm[v])
            group = [u for u, w in inc.items() if nrm[w] == nrm[v]]
            others = [u for u in inc if u not in group]
            # the triangles with this normal there, and the others whose face normals sum to it with them (a
            # triangle in two smoothing groups sums both)
            sets = _fitting_sets(group, others, fn, want)
            if not sets:
                stats['unexplained'] += 1
            ft = fn[t]
            flat = all(fn[u][0] * ft[0] + fn[u][1] * ft[1] + fn[u][2] * ft[2] >= COS_FLAT for u in group)
            for u in group:
                if u != t:
                    mark(t, u, WELD if inc[u] != v and attr(v, inc[u]) else (FLAT if flat else TOGETHER))
            key = find(of_vertex[v])
            corners.append((key, t, group, [u for u in group if inc[u] == v], others, want, sets))
            if sets and sets[0][0]:
                open_keys.add(key)
    # where the groups do not explain every normal of a position, one set per corner there, all agreeing
    clusters = {}
    for key, t, group, same, others, want, sets in corners:
        if key in open_keys:
            sets = _fitting_sets(group, others, fn, want, True) or sets
            clusters.setdefault(key, []).append((t, same, others, sets))
        elif sets:
            for u in others:
                mark(t, u, APART)
        else:
            for u in others:
                mark(t, u, GUESSED)
    # the most constrained positions first; a second pass lets each follow what the others decided
    votes = {}
    keys = sorted(clusters, key=lambda k: (sum(len(it[3]) for it in clusters[k]), k))
    result = {}
    for _sweep in range(2):
        for key in keys:
            def soft(t, u):
                said = {v for k, v in votes.get((t, u) if t < u else (u, t), {}).items() if k != key}
                if len(said) == 1:
                    return said.pop()
                f = rel[t].get(u, 0)
                if f & (APART | WELD):
                    return False
                if f & TOGETHER:
                    return True
                return None
            items = [it for it in clusters[key] if it[3]]
            chosen, agreed = _choose(items, soft)
            result[key] = (items, chosen, agreed)
            for (t, _same, others, _sets), x in zip(items, chosen):
                for u in others:
                    votes.setdefault((t, u) if t < u else (u, t), {})[key] = u in x
    for key in keys:
        items, chosen, agreed = result[key]
        if not agreed:
            stats['undecided'] += 1
        for (t, _same, others, _sets), x in zip(items, chosen):
            for u in others:
                mark(t, u, TOGETHER if u in x else APART)
        for t, _same, others, sets in clusters[key]:
            if not sets:
                for u in others:
                    mark(t, u, GUESSED)
    together, apart = [set() for _ in range(n)], [set() for _ in range(n)]
    for t in range(n):
        for u, f in rel[t].items():
            if u < t:
                continue
            # another normal where the group explains it parts them, so do two vertices only a normal keeps apart;
            # one normal on a curved surface joins them, as does one on a flat one when nothing parts them
            if f & (APART | WELD):
                joined = False
                if f & TOGETHER:
                    stats['conflicts'] += 1
            elif f & TOGETHER:
                joined = True
            elif f & FLAT:
                joined = not f & GUESSED
            else:
                joined = False
            (together if joined else apart)[t].add(u)
            (together if joined else apart)[u].add(t)
    bits = [0] * n
    for t in range(n):
        for u in sorted(together[t]):
            if u < t or bits[t] & bits[u]:
                continue
            got = False
            for a, b in ((t, u), (u, t)):
                free = bits[a] & ~bits[b]
                while free:
                    bit = free & -free
                    free &= free - 1
                    if not any(bits[w] & bit for w in apart[b]):
                        bits[b] |= bit
                        got = True
                        break
                if got:
                    break
            if got:
                continue
            taken = 0
            for w in apart[t] | apart[u]:
                taken |= bits[w]
            for k in range(MAX_BITS):
                bit = 1 << k
                if not taken & bit:
                    bits[t] |= bit
                    bits[u] |= bit
                    got = True
                    break
            if not got:
                stats['unresolved'] += 1
    return bits


def _order(tris):
    """(triangle, corners turned) in Build's numbering order: the first triangle in drawn order that can come next,
    a triangle that brings new vertices only when they are the next numbers, turned so its corners meet them in
    order."""
    heap = [(t, t) for t in range(len(tris))]
    heapq.heapify(heap)
    waiting = {}
    out = []
    nxt = 0
    while heap:
        key = heapq.heappop(heap)
        tv = tris[key[1]][1]
        if max(tv) < nxt:
            out.append((key[1], tuple(tv)))
            continue
        turned = None
        for r in range(3):
            rt = tuple(tv[r:]) + tuple(tv[:r])
            new = [v for v in rt if v >= nxt]
            if new == list(range(nxt, nxt + len(new))):
                turned = rt
                break
        if turned is None:
            new = min(v for v in tv if v >= nxt)
            # waits until the numbers below its new vertices are taken
            waiting.setdefault(new if new > nxt else nxt + 1, []).append(key)
            continue
        out.append((key[1], turned))
        top = max(tv) + 1
        for v in range(nxt + 1, top + 1):
            for k in waiting.pop(v, ()):
                heapq.heappush(heap, k)
        nxt = top
    # only a vertex no triangle uses leaves triangles waiting: they come last, as they are
    rest = sorted(k for keys in waiting.values() for k in keys)
    out += [(k[1], tuple(tris[k[1]][1])) for k in rest]
    return out, len(rest)


def raw_triangles(vertices, colors, uvs, tris, shows=None, use_strips=True):
    """(count, FStaticMeshTriangle records, stats) for a mesh's render data (see the module's header)."""
    nv = len(vertices) // 24
    recs = [vertices[24 * k:24 * k + 24] for k in range(nv)]
    pos = [struct.unpack('<3f', r[:12]) for r in recs]
    nrm = [r[12:] for r in recs]
    nuv = len(uvs)
    uv = [[struct.unpack_from('<2f', stream, 8 * k) for stream in uvs] for k in range(nv)]

    def weldable(v, w):
        if colors[4 * v:4 * v + 4] != colors[4 * w:4 * w + 4]:
            return False
        return all(abs(a[0] - b[0]) <= UV_TOLERANCE and abs(a[1] - b[1]) <= UV_TOLERANCE
                   for a, b in zip(uv[v], uv[w]))
    stats = {'unresolved': 0, 'unexplained': 0, 'conflicts': 0, 'undecided': 0}
    bits = _smoothing(pos, nrm, weldable, tris, stats)
    order, left = _order(tris)
    stats['draw order'] = {}
    if shows:
        order, stats['draw order'] = uc2xorder.solve(pos, uv, colors, tris, bits, order, shows, use_strips)
    out = bytearray()
    for t, tv in order:
        for v in tv:
            out += recs[v][:12]
        out += struct.pack('<i', nuv)
        for stream in uvs:
            for v in tv:
                out += stream[8 * v:8 * v + 8]
        for v in tv:
            out += colors[4 * v:4 * v + 4]
        out += struct.pack('<iI', tris[t][0], bits[t])
    stats['unplaced'] = left
    return len(order), bytes(out), stats
