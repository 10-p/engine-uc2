#!/usr/bin/env python3
"""The PC packages' tables: the GUID, generations, names, imports and exports of each package of the PC game folder
(Epic's PC packages, which the game is served with), and the disc's packages rewritten into them. A server on the
disc's packages and a client on the PC folder then see the same packages: a client loads a package only with the
server's GUID, and the network refers to objects and names by their index in the tables (Core/Src/UnCoreNet.cpp,
UPackageMap::Compute, which counts the last generation's exports and names).

    import uc2xtables
    tables = uc2xtables.load('tools/uc2-pc-tables.json.gz')     # {'Maps/DM-Legacy.unr': {...}, ...}
    pkg, stats = uc2xtables.apply(disc_pkg, tables['Maps/DM-Legacy.unr'], types, warn)
    entry = uc2xtables.make(pc_pkg, disc_pkg, types)             # tools/dev/make-pc-tables.py

The disc's packages hold the same objects in another order: the textures, meshes and animations packages the same
objects, and the maps the same objects plus their copies of the content packages' assets, merged into the map as
MyLevel.cla<N> exports, where the PC map imports them. apply() takes the PC package's tables as they are and finds
each export's data on the disc by its path and class; each object reference (uc2xrefs.py) goes to the PC table's
entry of the same path, a merged copy to the import it stands for, and a name to the PC name of the same text. What
the PC tables lack goes after them (beyond the last generation's counts, so the network never sees it): the names
and imports the disc's data needs, the disc-only exports something references. Disc-only exports nothing
references are left out; PC exports the disc does not have are written without data, flagged for neither client nor
server.

The merged copies are matched once, by make(), from the PC map and the disc's: both maps' common exports, walked
side by side (their references and names aligned where the two differ), say which import each copy's references
stand for. copy_identities() extends that to the copies only other copies refer to (a shader's texture), and
MapCopies gathers what every map's copies stand for: the content packages take the pixels and the geometry the
disc holds only in a map's copy from the copies of exactly that object.
"""
import collections
import difflib
import gzip
import io
import json
import struct

import uc2xrefs
from uc2xpkg import Export, FormatError, Package, RF_HasStack, RF_LoadRuntime

FORMAT = 1


# ---------------------------------------------------------------------------------------------------------------
# The tables file
# ---------------------------------------------------------------------------------------------------------------
def load(path):
    """{package path under the game folder: its tables}"""
    with gzip.open(path, 'rt', encoding='utf-8') as f:
        doc = json.load(f)
    if doc.get('format') != FORMAT:
        raise ValueError('%s: format %r, not %d' % (path, doc.get('format'), FORMAT))
    return doc['packages']


def save(path, packages):
    # no name and mtime 0 in the gzip header: the same tables give the same file
    with open(path, 'wb') as out, gzip.GzipFile('', 'wb', 9, out, mtime=0) as raw, \
            io.TextIOWrapper(raw, encoding='utf-8') as f:
        json.dump({'format': FORMAT, 'packages': packages}, f, separators=(',', ':'), sort_keys=True)


def find(tables, rel):
    """A package's tables by its path under the game folder, case-insensitively: (path as the PC names it, tables)."""
    if rel in tables:
        return rel, tables[rel]
    low = rel.lower()
    for k, v in tables.items():
        if k.lower() == low:
            return k, v
    return None, None


def tables_of(pkg):
    return {
        'guid': pkg.guid.hex(), 'flags': pkg.flags,
        'gens': [list(g) for g in (pkg.gens or [(len(pkg.exports), len(pkg.names))])],
        'names': [[s, fl] for s, fl in pkg.names],
        'imports': [list(im) for im in pkg.imports],
        'exports': [[e.cls, e.sup, e.pkg, e.name, e.flags] for e in pkg.exports],
    }


def package_of(t):
    """A package with the tables t and no export data, in the PC format of version 151."""
    p = Package()
    p.version, p.licensee, p.flags = 151, 0, t['flags']
    p.guid = bytes.fromhex(t['guid'])
    p.gens = [tuple(g) for g in t['gens']]
    p.names = [(s, fl) for s, fl in t['names']]
    p.imports = [tuple(im) for im in t['imports']]
    p.exports = [Export(c, s, k, n, fl, 0, 0, b'') for c, s, k, n, fl in t['exports']]
    return p


def _export_key(pkg, i):
    return pkg.export_path(i).lower(), pkg.export_class(i).lower()


def _import_key(pkg, k):
    return pkg.ref_path(-k - 1).lower(), pkg.name(pkg.imports[k][1]).lower()


def _is_merged(pkg, i):
    """A map's copy of a content package's asset (MyLevel.cla<N>)."""
    name = pkg.name(pkg.exports[i].name)
    return name[:3].lower() == 'cla' and name[3:].isdigit()


def _i32(data, at):
    return struct.unpack_from('<i', data, at)[0]


# ---------------------------------------------------------------------------------------------------------------
# Making the tables (from the PC package and the disc's, converted with its own tables)
# ---------------------------------------------------------------------------------------------------------------
def _tokens(pkg, data, refs):
    """What two packages' references can be compared by: a name's text, a reference's class."""
    out = []
    for at, kind in refs:
        v = _i32(data, at)
        out.append(('n', pkg.name(v).lower()) if kind == 'name' else ('r', (pkg.ref_class(v) or '').lower()))
    return out


def _blocks(a, b):
    """Matching runs of two token lists: their common head and tail, and the matches between."""
    p = 0
    while p < min(len(a), len(b)) and a[p] == b[p]:
        p += 1
    q = 0
    while q < min(len(a), len(b)) - p and a[-1 - q] == b[-1 - q]:
        q += 1
    out = [(0, 0, p)]
    ma, mb = a[p:len(a) - q], b[p:len(b) - q]
    if ma and mb and len(ma) * len(mb) < 4000000:
        out += [(p + x, p + y, n) for x, y, n in difflib.SequenceMatcher(None, ma, mb, autojunk=False)
                .get_matching_blocks() if n]
    out.append((len(a) - q, len(b) - q, q))
    return out


def make(pc, disc, types):
    """The tables of PC package pc, and for disc (the disc's package, converted with its own tables) what its
    merged copies and its exports of the same path stand for. Returns (tables, stats)."""
    t = tables_of(pc)
    stats = collections.Counter()
    pc_exp = collections.defaultdict(list)
    for j in range(len(pc.exports)):
        pc_exp[_export_key(pc, j)].append(j)
    disc_exp = {}
    for i in range(len(disc.exports)):
        disc_exp.setdefault(_export_key(disc, i), i)
    votes = collections.defaultdict(collections.Counter)    # disc reference -> Counter(PC reference)
    if pc.version == 151:
        for key, js in pc_exp.items():
            i = disc_exp.get(key)
            if i is None or not disc.exports[i].data:
                continue
            rd = uc2xrefs.references(disc, types, i)
            td = _tokens(disc, disc.exports[i].data, rd)
            for j in js:
                if not pc.exports[j].data:
                    continue
                rp = uc2xrefs.references(pc, types, j)
                tp = _tokens(pc, pc.exports[j].data, rp)
                blocks = [(0, 0, len(td))] if td == tp else _blocks(td, tp)
                stats['exports aligned' if td == tp else 'exports partly aligned'] += 1
                for a, b, n in blocks:
                    for q in range(n):
                        (ad, kind), (ap, _) = rd[a + q], rp[b + q]
                        if kind == 'ref':
                            votes[_i32(disc.exports[i].data, ad)][_i32(pc.exports[j].data, ap)] += 1
    merged, prefer = {}, {}
    for i in range(len(disc.exports)):
        c = votes.get(i + 1)
        key = _export_key(disc, i)
        js = pc_exp.get(key)
        if js:
            if c and len(js) > 1:
                # the PC package has this path twice: the entry the PC's references use
                best = c.most_common(1)[0][0]
                if best > 0 and best - 1 in js and best - 1 != js[0]:
                    prefer['%s|%s' % key] = best - 1
            continue
        if _is_merged(disc, i) and c:
            best, n = c.most_common(1)[0]
            if best < 0 and pc.name(pc.imports[-best - 1][1]).lower() == disc.export_class(i).lower():
                merged[disc.name(disc.exports[i].name).lower()] = best
                stats['merged copies matched'] += 1
                if len(c) > 1:
                    stats['merged copies with other votes'] += 1
            else:
                stats['merged copies not matched'] += 1
    if merged:
        t['merged'] = merged
    if prefer:
        t['prefer'] = prefer
    return t, stats


# ---------------------------------------------------------------------------------------------------------------
# What the maps' merged copies stand for
# ---------------------------------------------------------------------------------------------------------------
def copy_identities(disc, t, types, original):
    """{merged copy name (lower case): path of the content package object it stands for} of the disc's map disc
    (Xbox format): the copies the PC map imports, from its tables t; then each copy that identified copies refer
    to, from the object their originals refer to at the same place, where a copy's tag stream and its original's
    line up. original(package name) is the disc's content package of that name, or None. A copy two referrers
    disagree on is left out."""
    pc = package_of(t)
    found = {cla: pc.ref_path(ref) for cla, ref in t.get('merged', {}).items()}
    copies = {disc.name(disc.exports[i].name).lower(): i for i in range(len(disc.exports))
              if _is_merged(disc, i) and disc.exports[i].data}
    paths = {}
    disputed = set()
    queue = list(found)
    while queue:
        cla = queue.pop()
        i = copies.get(cla)
        pkg_name, _, rest = found[cla].partition('.')
        orig = original(pkg_name) if i is not None else None
        if orig is None:
            continue
        if orig.path not in paths:
            paths[orig.path] = {orig.export_path(j).lower(): j for j in range(len(orig.exports))}
        j = paths[orig.path].get(rest.lower())
        if j is None or orig.export_class(j) != disc.export_class(i) or not orig.exports[j].data:
            continue
        try:
            rd, ro = uc2xrefs.tag_references(disc, types, i), uc2xrefs.tag_references(orig, types, j)
        except (FormatError, IndexError, KeyError, struct.error):
            continue
        dd, do = disc.exports[i].data, orig.exports[j].data
        if _tokens(disc, dd, rd) != _tokens(orig, do, ro):
            continue
        for (ad, kind), (ao, _) in zip(rd, ro):
            v, w = _i32(dd, ad), _i32(do, ao)
            if kind != 'ref' or v <= 0 or not _is_merged(disc, v - 1) or w == 0:
                continue
            target = orig.ref_path(w) if w < 0 else '%s.%s' % (pkg_name, orig.export_path(w - 1))
            name = disc.name(disc.exports[v - 1].name).lower()
            if name not in found:
                found[name] = target
                queue.append(name)
            elif found[name].lower() != target.lower():
                disputed.add(name)
    return {k: v for k, v in found.items() if k not in disputed}


class MapCopies:
    """The maps' merged copies by what they stand for (copy_identities), for the content packages whose own data
    the disc holds only in a map's copy:

        named = uc2xtables.MapCopies()
        named.add('dm-legacy', uc2xtables.copy_identities(map_pkg, tables, types, original))
        named.find('T_NK_Tech.Walls.Wall01')         # [('dm-legacy', 'cla12'), ...]
        named.stands_for('dm-legacy', 'cla12')       # 'T_NK_Tech.Walls.Wall01' (lower case), or None
    """

    def __init__(self):
        self.by_path = {}
        self.by_copy = {}

    def add(self, map_name, identities):
        for cla, path in identities.items():
            self.by_copy[(map_name.lower(), cla.lower())] = path.lower()
            self.by_path.setdefault(path.lower(), []).append((map_name.lower(), cla.lower()))

    def merge(self, other):
        for (m, cla), path in other.by_copy.items():
            self.add(m, {cla: path})

    def find(self, path):
        return self.by_path.get(path.lower(), [])

    def stands_for(self, map_name, cla):
        return self.by_copy.get((map_name.lower(), cla.lower()))


# ---------------------------------------------------------------------------------------------------------------
# Writing a disc package into the PC tables
# ---------------------------------------------------------------------------------------------------------------
class _Rewrite:
    def __init__(self, disc, t, types, warn):
        self.d, self.types, self.warn = disc, types, warn
        self.out = package_of(t)
        self.merged = t.get('merged', {})
        self.prefer = t.get('prefer', {})
        self.stats = collections.Counter()
        out = self.out
        self.names = {}
        for k, (s, _) in enumerate(out.names):
            self.names.setdefault(s.lower(), k)
        self.pc_exp = {}
        for j in range(len(out.exports)):
            self.pc_exp.setdefault(_export_key(out, j), j)
        self.pc_imp = {}
        for k in range(len(out.imports)):
            self.pc_imp.setdefault(_import_key(out, k), k)
        self.refs = {0: 0}
        self.queue = []         # (output export, disc export) whose data to write

    def name(self, di):
        s, fl = self.d.names[di]
        k = self.names.get(s.lower())
        if k is None:
            self.out.names.append((s, fl))
            k = self.names[s.lower()] = len(self.out.names) - 1
            self.stats['names added'] += 1
        return k

    def ref(self, dv):
        v = self.refs.get(dv)
        if v is not None:
            return v
        d = self.d
        if dv > 0:
            i = dv - 1
            key = _export_key(d, i)
            j = self.prefer.get('%s|%s' % key, self.pc_exp.get(key))
            if j is not None:
                v = j + 1
            elif _is_merged(d, i) and d.name(d.exports[i].name).lower() in self.merged:
                v = self.merged[d.name(d.exports[i].name).lower()]
                if self.out.name(self.out.imports[-v - 1][1]).lower() != key[1]:
                    self.warn('%s (%s): the PC tables match it to %s, of another class' % (
                        d.export_path(i), d.export_class(i), self.out.ref_path(v)))
                    v = self._add_export(i)
            elif not d.exports[i].data and not d.exports[i].runtime():
                v = 0           # left out of the conversion
            else:
                v = self._add_export(i)
        else:
            k = -dv - 1
            m = self.pc_imp.get(_import_key(d, k))
            v = -m - 1 if m is not None else self._add_import(k)
        self.refs[dv] = v
        return v

    def _add_import(self, k):
        out = self.out
        out.imports.append(None)
        v = self.refs[-k - 1] = -len(out.imports)
        cp, cn, pk, on = self.d.imports[k]
        out.imports[-v - 1] = (self.name(cp), self.name(cn), self.ref(pk), self.name(on))
        self.stats['imports added'] += 1
        return v

    def _add_export(self, i):
        out, e = self.out, self.d.exports[i]
        x = Export(0, 0, 0, 0, e.flags, 0, 0, b'')
        out.exports.append(x)
        v = self.refs[i + 1] = len(out.exports)
        x.cls, x.sup, x.pkg, x.name = self.ref(e.cls), self.ref(e.sup), self.ref(e.pkg), self.name(e.name)
        self.queue.append((v - 1, i))
        self.stats['exports added'] += 1
        if _is_merged(self.d, i):
            self.warn('%s (%s): a merged copy the PC tables do not match, kept in the map' % (
                self.d.export_path(i), self.d.export_class(i)))
        return v

    def data(self, i):
        e = self.d.exports[i]
        buf = bytearray(e.data)
        for at, kind in uc2xrefs.references(self.d, self.types, i):
            v = _i32(buf, at)
            struct.pack_into('<i', buf, at, self.ref(v) if kind == 'ref' else self.name(v))
        return bytes(buf)

    def run(self):
        d, out = self.d, self.out
        disc_exp = {}
        for i in range(len(d.exports)):
            disc_exp.setdefault(_export_key(d, i), i)
        for j in range(len(out.exports)):
            i = disc_exp.get(_export_key(out, j))
            if i is None:
                if out.exports[j].flags & RF_LoadRuntime:
                    self.stats['PC exports not on the disc'] += 1
                out.exports[j].flags &= ~RF_LoadRuntime
                continue
            self.queue.append((j, i))
        for j, i in self.queue:         # grows while it runs, by the exports the data needs added
            x, e = out.exports[j], d.exports[i]
            if not e.data:
                if x.flags & RF_LoadRuntime and e.runtime():
                    self.warn('%s (%s): no data' % (d.export_path(i), d.export_class(i)))
                x.flags &= ~RF_LoadRuntime
                continue
            x.data, x.lazy = self.data(i), e.lazy
            x.flags = (x.flags & ~RF_HasStack) | (e.flags & RF_HasStack)
            self.stats['exports'] += 1
        written = {i for _, i in self.queue}
        self.stats['disc exports left out'] = sum(1 for i, e in enumerate(d.exports)
                                                  if e.data and i not in written and not _is_merged(d, i))
        return out, self.stats


def apply(disc, t, types, warn):
    """disc (a disc package converted with its own tables) in the PC tables t: (package, stats)."""
    return _Rewrite(disc, t, types, warn).run()
