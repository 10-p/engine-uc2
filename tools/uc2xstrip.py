#!/usr/bin/env python3
"""xbstrip.cpp's Stripify (Engine/Src/xbstrip.cpp, OPTIMIZE_FOR_INDICES) as UStaticMesh::Build runs it, for the disc
cook to tell how an editor rebuild orders a section's triangles. Used by tools/uc2xrawtri.py.

    import uc2xstrip
    indices = uc2xstrip.build_order(triangle_list_indices, use_strips)

Exact to the C++ including its tie-breaks: BuildStrips takes the start of least neighbour count, then least swap
ratio, and of equal ones the first in the input's order, or the last where the float the ratio is stored in is
above its exact value (the x87 build compares the new ratio in extended precision with the stored float);
CreateLongStrip joins the strips as FindBestStrip picks them by vertex cache hits. Computed with neighbour-count
buckets, CreateStrip results cached until a triangle they read is used, and a cache-hit filter in FindBestStrip.
"""
import heapq
import struct

CACHE_SIZE = 18
# triangles walked and indices cached so far: a count of the work done, for budgets that do not depend on the clock
WORK = [0]


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


class Stripper:
    """One section's triangles (vertex triples, in the input's order) and their neighbours as InitTriangleInfo
    links them: each edge to the first later triangle with the reversed edge that is not linked on all three."""

    def __init__(self, tris):
        self.tris = tris
        n = self.n = len(tris)
        ntri = self.ntri = [[-1, -1, -1] for _ in range(n)]
        nedge = self.nedge = [[0, 0, 0] for _ in range(n)]
        used = [0] * n
        by_edge = {}
        for t in range(n):
            a, b, c = tris[t]
            for k, e in enumerate(((a, b), (b, c), (c, a))):
                by_edge.setdefault(e, []).append((t, k))
        for tri in range(n):
            tv = tris[tri]
            for vert in range(3):
                if used[tri] & (1 << vert):
                    continue
                v1, v2 = tv[(vert + 1) % 3], tv[vert]
                for itri, ivert in by_edge.get((v1, v2), ()):
                    if itri <= tri or used[itri] == 7:
                        continue
                    ntri[tri][vert] = itri
                    nedge[tri][vert] = ivert
                    used[tri] |= 1 << vert
                    ntri[itri][ivert] = tri
                    nedge[itri][ivert] = vert
                    used[itri] |= 1 << ivert
                    break

    def _strip(self, used, tri, vert, maxlen, lookahead, nonseq, cw, reads):
        """CreateStrip: (triangles, swaps); reads collects the triangles whose used flag it read."""
        ntri, nedge = self.ntri, self.nedge
        out = [tri]
        used[tri] = 1
        swaps = 0
        cw = not cw
        edge = (vert + 2 if cw else vert + 1) % 3
        nexttri = ntri[tri][edge]
        nextvert = nedge[tri][edge]
        stripcount = 3

        def count(x):
            c = 0
            for y in ntri[x]:
                if y != -1:
                    reads.add(y)
                    if not used[y]:
                        c += 1
            return c
        while stripcount < maxlen:
            if nexttri == -1:
                break
            reads.add(nexttri)
            if used[nexttri]:
                break
            tri, vert = nexttri, nextvert
            cw = not cw
            edge = (vert + 2 if cw else vert + 1) % 3
            nexttri = ntri[tri][edge]
            nextvert = nedge[tri][edge]
            swap = False
            if nexttri != -1:
                reads.add(nexttri)
            if nexttri == -1 or used[nexttri]:
                swap = True
            elif lookahead:
                edgeswap = (vert + 1 if cw else vert + 2) % 3
                nts = ntri[tri][edgeswap]
                nvs = nedge[tri][edgeswap]
                if nts != -1:
                    reads.add(nts)
                if nts != -1 and not used[nts]:
                    a, b = count(nts), count(nexttri)
                    if a < b:
                        swap = True
                    elif a == b:
                        e2 = (nvs + 2 if cw else nvs + 1) % 3
                        nts2 = ntri[nts][e2]
                        e1 = (nextvert + 1 if cw else nextvert + 2) % 3
                        nt1 = ntri[nexttri][e1]
                        if nt1 != -1:
                            reads.add(nt1)
                        if nts2 != -1:
                            reads.add(nts2)
                        if nt1 == -1 or used[nt1]:
                            swap = True
                        elif nts2 != -1 and not used[nts2]:
                            if count(nts2) < count(nt1):
                                swap = True
            if swap and nonseq:
                edgeswap = (vert + 1 if cw else vert + 2) % 3
                nexttri = ntri[tri][edgeswap]
                nextvert = nedge[tri][edgeswap]
                if nexttri != -1:
                    reads.add(nexttri)
                if nexttri != -1 and not used[nexttri]:
                    stripcount += 1
                    swaps += 1
                    cw = not cw
            out.append(tri)
            used[tri] = 1
            stripcount += 1
        for t in out:
            used[t] = 0
        reads.update(out)
        WORK[0] += len(out)
        return out, swaps

    def _indices(self, cw, ptris):
        """CreateIndices: a strip's triangles as strip indices."""
        tris, ntri, nedge = self.tris, self.ntri, self.nedge
        out = []
        in_edge = -1
        n = len(ptris)
        for i in range(n):
            tri = ptris[i]
            if i < n - 1:
                nt = ptris[i + 1]
                nb = ntri[tri]
                out_edge = 0 if nb[0] == nt else 1 if nb[1] == nt else 2 if nb[2] == nt else 3
            else:
                out_edge = (in_edge + 1 if cw else in_edge + 2) % 3
            tv = tris[tri]
            if i == 0:
                if cw:
                    out += [tv[(out_edge + 2) % 3], tv[out_edge % 3], tv[(out_edge + 1) % 3]]
                else:
                    out += [tv[(out_edge + 2) % 3], tv[(out_edge + 1) % 3], tv[out_edge % 3]]
            else:
                if out_edge == (in_edge + 1 if cw else in_edge + 2) % 3:
                    out.append(tv[(in_edge + 2) % 3])
                else:
                    if cw:
                        out += [tv[in_edge % 3], tv[(in_edge + 2) % 3]]
                    else:
                        out += [tv[(in_edge + 1) % 3], tv[(in_edge + 2) % 3]]
                    cw = not cw
            in_edge = nedge[tri][out_edge] if out_edge < 3 else None
            cw = not cw
        return out

    def build_strips(self, maxlen, lookahead, nonseq, swap_orientation=True):
        """BuildStrips. The start is the triangle and corner of least neighbour count, then least swap ratio; of
        equal ones the first in order, or the last where the ratio stored as a float is greater than the new one
        (x87: the new ratio is compared in extended precision with the stored float)."""
        n = self.n
        ntri = self.ntri
        used = [0] * n
        nc = [sum(1 for y in ntri[t] if y != -1) for t in range(n)]
        back = [[] for _ in range(n)]
        for y in range(n):
            for x in ntri[y]:
                if x != -1:
                    back[x].append(y)
        bucket_of = [nc[t] or 4 for t in range(n)]
        members = {1: set(), 2: set(), 3: set(), 4: set()}
        for t in range(n):
            members[bucket_of[t]].add(t)
        # per orientation: the triangles whose ratios need computing, by bucket; heaps of (ratio, tri, vert, version)
        dirty = {cw: {k: set(members[k]) for k in (1, 2, 3, 4)} for cw in (True, False)}
        heaps = {cw: {1: [], 2: [], 3: [], 4: []} for cw in (True, False)}
        version = {True: [0] * n, False: [0] * n}
        rev = {}
        strips = []
        start_cw = True
        while True:
            c = next((k for k in (1, 2, 3, 4) if members[k]), None)
            if c is None:
                break
            heap = heaps[start_cw][c]
            ver_cw = version[start_cw]
            for tri in dirty[start_cw][c]:
                ver_cw[tri] += 1
                for vert in range(3):
                    reads = set()
                    ptris, swaps = self._strip(used, tri, vert, maxlen, lookahead, nonseq, start_cw, reads)
                    for r in reads:
                        rev.setdefault(r, set()).add(tri)
                    ln = 2 + len(ptris) + swaps
                    r = 1.0 if ln == 3 else swaps / ln
                    heapq.heappush(heap, (r, tri, vert, ver_cw[tri]))
            dirty[start_cw][c].clear()
            while True:
                r, tri, vert, ver = heap[0]
                if ver == ver_cw[tri] and not used[tri] and bucket_of[tri] == c:
                    break
                heapq.heappop(heap)
            if f32(r) > r:
                # equal ratios: the last in order
                same = []
                while heap and heap[0][0] == r:
                    e = heapq.heappop(heap)
                    if e[3] == ver_cw[e[1]] and not used[e[1]] and bucket_of[e[1]] == c:
                        same.append(e)
                for e in same:
                    heapq.heappush(heap, e)
                _r, tri, vert, _v = max(same, key=lambda e: (e[1], e[2]))
            ptris, _ = self._strip(used, tri, vert, maxlen, lookahead, nonseq, start_cw, set())
            touched = set()
            for t in ptris:
                used[t] = 1
                members[bucket_of[t]].discard(t)
                for cw in (True, False):
                    dirty[cw][bucket_of[t]].discard(t)
            for t in ptris:
                for y in back[t]:
                    if not used[y]:
                        b0 = bucket_of[y]
                        members[b0].discard(y)
                        for cw in (True, False):
                            dirty[cw][b0].discard(y)
                        nc[y] -= 1
                        bucket_of[y] = nc[y] or 4
                        members[bucket_of[y]].add(y)
                        touched.add(y)
                touched.update(rev.pop(t, ()))
            for y in touched:
                if not used[y]:
                    for cw in (True, False):
                        dirty[cw][bucket_of[y]].add(y)
                        version[cw][y] += 1
            idx = self._indices(start_cw, ptris)
            strips.append([start_cw, ptris, idx])
            if swap_orientation and (len(idx) & 1):
                start_cw = not start_cw
        return strips


class Cache:
    """FindBestStrip's vertex cache (CACHE_SIZE entries, first in first out), counting hits on vertices another
    strip put there."""
    __slots__ = ('cache', 'strip', 'ptr', 'hits')

    def __init__(self):
        self.cache = [0xFFFF] * CACHE_SIZE
        self.strip = [None] * CACHE_SIZE
        self.ptr = 0
        self.hits = 0

    def copy(self):
        c = Cache.__new__(Cache)
        c.cache = self.cache[:]
        c.strip = self.strip[:]
        c.ptr = self.ptr
        c.hits = self.hits
        return c

    def add(self, strip, v):
        cache = self.cache
        try:
            i = cache.index(v)
        except ValueError:
            p = self.ptr
            cache[p] = v
            self.strip[p] = strip
            self.ptr = (p + 1) % CACHE_SIZE
            return
        if strip != self.strip[i]:
            self.hits += 1
            self.strip[i] = strip


def _hits(cache, idx):
    """The hits a strip's indices would score on the cache."""
    c = cache.copy()
    c.hits = 0
    for v in idx:
        c.add(2, v)
    WORK[0] += len(idx)
    return c.hits


def long_strip(strips):
    """CreateLongStrip with FindBestStrip (strips: [cw, triangles, indices]). FindBestStrip takes, among the strips of
    the head's orientation and length parity, the first in list order with the most cache hits; a strip that shares
    no vertex with the cache has none, so with no hits anywhere the head itself is taken. Taking strip i moves the
    head to i's place."""
    k = len(strips)
    cw = [s[0] for s in strips]
    idx = [s[2] for s in strips]
    key = list(range(k))
    alive = [True] * k
    by_vertex = {}
    for i in range(k):
        for v in set(idx[i]):
            by_vertex.setdefault(v, set()).add(i)
    order = [(i, i) for i in range(k)]
    heapq.heapify(order)

    def remove(i):
        alive[i] = False
        for v in set(idx[i]):
            by_vertex[v].discard(i)

    def head():
        while True:
            kk, i = order[0]
            if alive[i] and key[i] == kk:
                return i
            heapq.heappop(order)
    out = []
    cache = Cache()
    h = head()
    for v in idx[h]:
        out.append(v)
        cache.add(1, v)
    remove(h)
    remaining = k - 1
    while remaining:
        h = head()
        hcw, hpar = cw[h], len(idx[h]) & 1
        cand = set()
        for v in cache.cache:
            cand |= by_vertex.get(v, set())
        best, best_hits, best_flip = h, 0, False
        for i in sorted(cand, key=lambda i: key[i]):
            if cw[i] != hcw or (len(idx[i]) & 1) != hpar:
                continue
            hits = _hits(cache, idx[i])
            flipped = False
            if not (len(idx[i]) & 1):
                fh = _hits(cache, idx[i][::-1])
                if fh > hits:
                    hits, flipped = fh, True
            if hits > best_hits:
                best, best_hits, best_flip = i, hits, flipped
        if best != h:
            key[h] = key[best]
            heapq.heappush(order, (key[h], h))
        seq = idx[best][::-1] if best_flip else idx[best]
        last = out[-1]
        first = seq[0]
        if first != last:
            out.append(last)
            out.append(first)
        if cw[best] != (not (len(out) & 1)):
            out.append(first)
        for v in seq:
            out.append(v)
            cache.add(1, v)
        remove(best)
        remaining -= 1
    return out


def strip_to_list(strip):
    """A strip's triangles as a list, odd ones flipped, degenerate ones left out."""
    out = []
    if len(strip) < 2:
        return out
    i1, i2 = strip[0], strip[1]
    for src in range(2, len(strip)):
        i0, i1, i2 = i1, i2, strip[src]
        if i0 != i1 and i1 != i2 and i2 != i0:
            if src & 1:
                out += [i1, i0, i2]
            else:
                out += [i0, i1, i2]
    return out


ARGMAP = ((1024, True, True), (1024, False, True), (1024, False, False))


def stripify(indices, tristrip):
    """FRawIndexBuffer::Stripify(0, tristrip): the argmap whose long strip is shortest (the first of equal ones), as a
    strip or as a list."""
    n = len(indices) // 3
    if not n:
        return []
    st = Stripper([tuple(indices[3 * i:3 * i + 3]) for i in range(n)])
    best, bestcost = None, None
    for maxlen, la, ns in ARGMAP:
        out = long_strip(st.build_strips(maxlen, la, ns))
        if bestcost is None or len(out) < bestcost:
            best, bestcost = out, len(out)
    return best if tristrip else strip_to_list(best)


def build_order(indices, use_strips=True):
    """UStaticMesh::Build's section indices: Stripify(0, 0) for the wireframe pass, then Stripify(0, UseTriStrips)."""
    return stripify(stripify(indices, False), use_strips)
