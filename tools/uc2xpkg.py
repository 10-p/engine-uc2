#!/usr/bin/env python3
"""Unreal Championship 2 packages (version 151) in both formats: the Xbox format of the disc (licensee 1, maps as
.LIN linear-load streams) read, the PC format (licensee 0) written. Used by tools/cook-xbox-packages.py.

    from uc2xpkg import Package, read_lin
    pkg = Package.load('XboxTextures/T_FX.utx')
    pkg = read_lin('XboxMaps/DM-Legacy.LIN', foreign)       # foreign: ForeignIndex of the disc's other packages
    pkg.write('Textures/T_FX.utx')                           # licensee 0, the tables after the export data

The layout (docs/porting/analysis/xbox-packages.md):
  summary   tag, version 151, licensee, flags, the six table counts and offsets, GUID, generations
  names     i32 length (with NUL), bytes, u32 flags
  imports   i32 class package, i32 class name, i16 package, i32 object name
  exports   five i16 (class, super, super, package, package), i32 name, u32 flags, i32 size, i32 offset (size>0)
A .LIN is every byte the Xbox loader read while it loaded the map, in the order it read them: the summary and the
names, the import and export tables, then each export as the loader serialised it, with the reads from packages
that were already open (script packages, texture and mesh packages) between them. read_lin replays the loader's
order (ULinkerLoad::LoadAllObjects, then UObject::EndLoad, which does not sort on the Xbox) to cut it back into
exports. Exports flagged neither RF_LoadForClient nor RF_LoadForServer (brushes, cameras, their models) were never
loaded on the Xbox and are not in the stream: they come back with no data.
"""
import os
import struct

TAG = 0x9E2A83C1
RF_LoadForClient = 0x00010000
RF_LoadForServer = 0x00020000
RF_LoadForEdit = 0x00040000
RF_HasStack = 0x02000000
RF_LoadRuntime = RF_LoadForClient | RF_LoadForServer


def find_ci(root, rel):
    """The path of rel below root, matched case-insensitively (disc folders are upper case on some dumps), or None."""
    cur = root
    for part in [p for p in rel.split('/') if p]:
        try:
            match = [e for e in os.listdir(cur) if e.lower() == part.lower()]
        except OSError:
            return None
        if not match:
            return None
        cur = os.path.join(cur, sorted(match)[0])
    return cur


def files_in(root, rel, ext):
    """The files below root/rel (case-insensitive) whose names end in ext (lower case), sorted."""
    d = find_ci(root, rel)
    if not d or not os.path.isdir(d):
        return []
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.lower().endswith(ext))


class FormatError(Exception):
    pass


class Reader:
    """Little-endian cursor over bytes."""

    def __init__(self, data, pos=0, end=None):
        self.d = data
        self.p = pos
        self.end = len(data) if end is None else min(end, len(data))

    def take(self, n):
        if n < 0 or self.p + n > self.end:
            raise FormatError('read past end (%d+%d > %d)' % (self.p, n, self.end))
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self):
        return self.take(1)[0]

    def i16(self):
        return struct.unpack('<h', self.take(2))[0]

    def u16(self):
        return struct.unpack('<H', self.take(2))[0]

    def i32(self):
        return struct.unpack('<i', self.take(4))[0]

    def u32(self):
        return struct.unpack('<I', self.take(4))[0]

    def f32(self):
        return struct.unpack('<f', self.take(4))[0]

    def index(self):
        """FCompactIndex before version 145: sign and 6 bits, then 7 bits per byte while the high bit is set."""
        b0 = self.u8()
        v = b0 & 0x3F
        if b0 & 0x40:
            shift = 6
            while True:
                b = self.u8()
                v |= (b & 0x7F) << shift
                shift += 7
                if not b & 0x80:
                    break
        return -v if b0 & 0x80 else v

    def count(self, limit=1 << 26):
        n = self.i32()
        if n < 0 or n > limit:
            raise FormatError('bad array count %d at %d' % (n, self.p - 4))
        return n


class Export:
    __slots__ = ('cls', 'sup', 'pkg', 'name', 'flags', 'size', 'offset', 'data', 'lin', 'lazy')

    def __init__(self, cls, sup, pkg, name, flags, size, offset, data=None):
        self.cls, self.sup, self.pkg, self.name, self.flags = cls, sup, pkg, name, flags
        self.size, self.offset, self.data = size, offset, data
        self.lin = None         # from a .LIN with lazy arrays skipped: the bytes the stream holds
        self.lazy = ()          # offsets in data of lazy-array end fields, relative to the export's start

    def runtime(self):
        return bool(self.flags & RF_LoadRuntime)


class Package:
    """Names [(str, flags)], imports [(class package, class name, package index, name)] as name indices, exports."""

    def __init__(self):
        self.version, self.licensee, self.flags = 151, 0, 0
        self.guid = bytes(16)
        self.gens = None        # [(export count, name count)] per generation; None: one, of the whole tables
        self.names, self.imports, self.exports = [], [], []
        self.path = None

    # -- names and references
    def name(self, i):
        return self.names[i][0]

    def ref_path(self, ref):
        if ref == 0:
            return 'None'
        parts = []
        while ref:
            if ref > 0:
                e = self.exports[ref - 1]
                parts.append(self.name(e.name))
                ref = e.pkg
            else:
                im = self.imports[-ref - 1]
                parts.append(self.name(im[3]))
                ref = im[2]
        return '.'.join(reversed(parts))

    def ref_class(self, ref):
        """Class name of the object a reference points at."""
        if ref > 0:
            return self.export_class(ref - 1)
        if ref < 0:
            return self.name(self.imports[-ref - 1][1])
        return None

    def export_class(self, i):
        c = self.exports[i].cls
        if c == 0:
            return 'Class'
        if c < 0:
            return self.name(self.imports[-c - 1][3])
        return self.name(self.exports[c - 1].name)

    def export_path(self, i):
        return self.ref_path(i + 1)

    def none_index(self):
        """Index of the name 'None', -1 when the package has none."""
        if not hasattr(self, '_none'):
            i = self.find_name('None')
            self._none = -1 if i is None else i
        return self._none

    def find_name(self, s):
        for i, (n, _) in enumerate(self.names):
            if n.lower() == s.lower():
                return i
        return None

    def add_name(self, s, flags=0x00070010):
        i = self.find_name(s)
        if i is None:
            self.names.append((s, flags))
            i = len(self.names) - 1
            self.__dict__.pop('_none', None)
        return i

    # -- reading
    @staticmethod
    def _summary(r):
        tag, ver, lic, flags = struct.unpack('<IHHI', r.take(12))
        if tag != TAG:
            raise FormatError('not a package')
        counts = struct.unpack('<6i', r.take(24))
        guid = r.take(16)
        gens = [struct.unpack('<ii', r.take(8)) for _ in range(r.count(1000))]
        return ver, lic, flags, counts, guid, gens

    def _idx(self, r):
        return r.i32() if self.version >= 145 else r.index()

    def _read_names(self, r, n):
        for _ in range(n):
            ln = r.count(4096) if self.version >= 145 else r.index()
            s = r.take(ln)
            self.names.append((s[:-1].decode('latin-1'), r.u32()))

    def _read_imports(self, r, n):
        for _ in range(n):
            cp, cn = self._idx(r), self._idx(r)
            pk = r.i16() if self.version >= 150 else r.i32()
            on = self._idx(r)
            self.imports.append((cp, cn, pk, on))

    def _read_exports(self, r, n):
        for _ in range(n):
            if self.version >= 150:
                # three SWORD fields written as two 32-bit indices and a 16-bit one, overlapping
                h = struct.unpack('<5h', r.take(10))
                cls, sup, pkg = h[0], h[2], h[4]
            else:
                cls, sup, pkg = self._idx(r), self._idx(r), r.i32()
            nm, fl = self._idx(r), r.u32()
            sz = self._idx(r)
            off = self._idx(r) if sz else 0
            self.exports.append(Export(cls, sup, pkg, nm, fl, sz, off))

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            data = f.read()
        p = cls()
        p.path = path
        r = Reader(data)
        p.version, p.licensee, p.flags, counts, p.guid, p.gens = cls._summary(r)
        nc, no, ec, eo, ic, io = counts
        p._read_names(Reader(data, no), nc)
        p._read_imports(Reader(data, io), ic)
        p._read_exports(Reader(data, eo), ec)
        for e in p.exports:
            e.data = data[e.offset:e.offset + e.size] if e.size else b''
        return p

    # -- writing (PC layout: names, export data, imports, exports)
    def write(self, path, licensee=0):
        names = bytearray()
        for s, fl in self.names:
            b = s.encode('latin-1') + b'\0'
            names += struct.pack('<i', len(b)) + b + struct.pack('<I', fl)
        head = 12 + 24 + 16 + 4 + 8 * len(self.gens or [None])
        body = bytearray()
        offsets = []
        pos = head + len(names)
        for e in self.exports:
            d = e.data or b''
            if d and e.lazy:
                d = bytearray(d)
                for at in e.lazy:
                    struct.pack_into('<i', d, at, struct.unpack_from('<i', d, at)[0] + pos)
            offsets.append(pos if d else 0)
            body += d
            pos += len(d)
        imports = bytearray()
        for cp, cn, pk, on in self.imports:
            imports += struct.pack('<iihi', cp, cn, pk, on)
        import_off = pos
        exports = bytearray()
        for e, off in zip(self.exports, offsets):
            d = e.data or b''
            exports += struct.pack('<5h', e.cls, e.sup, e.sup, e.pkg, e.pkg)
            exports += struct.pack('<iIi', e.name, e.flags, len(d))
            if d:
                exports += struct.pack('<i', off)
        export_off = import_off + len(imports)
        summary = struct.pack('<IHHI', TAG, self.version, licensee, self.flags)
        summary += struct.pack('<6i', len(self.names), head, len(self.exports), export_off,
                               len(self.imports), import_off)
        gens = self.gens or [(len(self.exports), len(self.names))]
        summary += self.guid + struct.pack('<i', len(gens)) + b''.join(struct.pack('<ii', *g) for g in gens)
        with open(path, 'wb') as f:
            f.write(summary + names + body + imports + exports)


# ---------------------------------------------------------------------------------------------------------------
# Object layout helpers shared by the delineariser and the class converters
# ---------------------------------------------------------------------------------------------------------------
def skip_stack(r, flags):
    """UObject::Serialize's state frame (RF_HasStack): node, state node, probe mask, latent action, code offset."""
    if flags & RF_HasStack:
        node = r.i32()
        r.i32()
        r.take(8)
        r.take(2)
        if node:
            r.i32()


def skip_tagged(r, none=0):
    """FPropertyTag stream up to its terminator (UnClass.cpp, FPropertyTag<<): by name (ver 0), ended by the
    name 'None' (index none), or by reference (ver 1), ended by a null property."""
    while True:
        ver = r.u8()
        key = r.i32()
        if ver == 0 and key == none:
            return
        if ver != 0 and key == 0:
            return
        info = r.u8()
        if ver == 0 and info & 0x0F == 10:
            r.i32()
        sz = {0x00: 1, 0x10: 2, 0x20: 4, 0x30: 12, 0x40: 16}.get(info & 0x70)
        if sz is None:
            sz = {0x50: r.u8, 0x60: r.u16, 0x70: r.i32}[info & 0x70]()
        if info & 0x80 and info & 0x0F != 3:
            b = r.u8()
            if b & 0x80:
                r.take(1 if b & 0xC0 == 0x80 else 3)
        if info & 0x0F != 3:
            r.take(sz)


def drop_tags(data, flags, pkg, names):
    """The object's data without the tagged properties whose names are in names (lower case); the state frame
    and the native data stay as they are."""
    r = Reader(data)
    skip_stack(r, flags)
    out = bytearray(data[:r.p])
    none = pkg.none_index()
    while True:
        start = r.p
        ver = r.u8()
        key = r.i32()
        if (ver == 0 and key == none) or (ver != 0 and key == 0):
            out += data[start:]
            return bytes(out)
        name = pkg.name(key) if ver == 0 else pkg.ref_path(key).rsplit('.', 1)[-1]
        info = r.u8()
        if ver == 0 and info & 0x0F == 10:
            r.i32()
        sz = {0x00: 1, 0x10: 2, 0x20: 4, 0x30: 12, 0x40: 16}.get(info & 0x70)
        if sz is None:
            sz = {0x50: r.u8, 0x60: r.u16, 0x70: r.i32}[info & 0x70]()
        if info & 0x80 and info & 0x0F != 3:
            b = r.u8()
            if b & 0x80:
                r.take(1 if b & 0xC0 == 0x80 else 3)
        if info & 0x0F != 3:
            r.take(sz)
        if name.lower() not in names:
            out += data[start:r.p]


def object_head_end(data, flags, pkg=None, pos=0):
    """Offset where an object's native data starts: after the state frame and the tagged properties. pkg gives
    the index of the name 'None' (the by-name terminator; packages that store tags by reference have none)."""
    r = Reader(data, pos)
    skip_stack(r, flags)
    skip_tagged(r, pkg.none_index() if pkg is not None else 0)
    return r.p


# Model (Engine/Src/UnModel.cpp, UModel::Serialize) up to and with its Polys reference, Xbox layout
XBOX_BSP_NODE = 82      # FBspNode<< with LicenseeVer 1: 16-bit leaves, section, first vertex, light map


def model_polys_ref_end(data, flags, node_size, pkg=None):
    """Offset just after UModel's Polys reference (where UModel::Serialize preloads the Polys), and the reference."""
    r = Reader(data, object_head_end(data, flags, pkg))
    r.take(24 + 16)                         # UPrimitive: BoundingBox (FBox, no IsValid from 146), BoundingSphere
    r.take(12 * r.count())                  # Vectors
    r.take(12 * r.count())                  # Points
    r.take(node_size * r.count())           # Nodes
    r.take(52 * r.count())                  # Surfs
    r.take(8 * r.count())                   # Verts
    r.i32()                                 # NumSharedSides
    nz = r.i32()
    if nz < 0 or nz > 64:
        raise FormatError('bad zone count %d' % nz)
    r.take(4 * nz)                          # Zones: FZoneProperties (ZoneActor only from 141)
    ref = r.i32()
    return r.p, ref


# ---------------------------------------------------------------------------------------------------------------
# Linear-load streams
# ---------------------------------------------------------------------------------------------------------------
class ForeignIndex:
    """The files of packages that were open before a map's stream (the disc's script packages): the loader's reads
    from them inside a .LIN are runs of their bytes."""

    KEY = 16

    def __init__(self):
        self.blobs = []

    def add_file(self, path):
        with open(path, 'rb') as f:
            self.blobs.append(f.read())

    def contains(self, chunk):
        return any(blob.find(chunk) >= 0 for blob in self.blobs)

    def match(self, data, pos):
        """Length of the longest run of foreign bytes at pos (at least KEY bytes), else 0."""
        probe = data[pos:pos + self.KEY]
        if len(probe) < self.KEY:
            return 0
        best = 0
        for blob in self.blobs:
            k = blob.find(probe)
            while k >= 0:
                lo, hi = self.KEY, min(len(blob) - k, len(data) - pos)
                while lo < hi:
                    mid = (lo + hi + 1) // 2
                    if blob[k:k + mid] == data[pos:pos + mid]:
                        lo = mid
                    else:
                        hi = mid - 1
                best = max(best, lo)
                k = blob.find(probe, k + 1)
        return best


# classes that serialise native data after their tags (UObject::Serialize overrides that read on load)
NATIVE_CLASSES = {
    'Model', 'Polys', 'Level', 'StaticMesh', 'StaticMeshInstance', 'SkeletalMesh', 'MeshAnimation', 'VertMesh',
    'Animation', 'Texture', 'Cubemap', 'Palette', 'Font', 'TerrainInfo', 'TerrainSector', 'TerrainSectorBlock',
    'TerrainPrimitive', 'FluidSurfacePrimitive', 'ConvexVolume', 'StaticProjectorClippedPrim', 'Sound',
    'TextBuffer',
}


def _check_level(d, pos, end, pkg):
    """ULevel's native data: the actor list (count twice), the URL, the model, time, text blocks, travel info."""
    ne, ni = len(pkg.exports), len(pkg.imports)
    try:
        r = Reader(d, pos, end)
        n, mx = r.i32(), r.i32()
        if n != mx or not 0 < n < 65536:
            return False
        for v in struct.unpack('<%di' % n, r.take(4 * n)):
            if not 0 <= v <= ne:
                return False
        _skip_url(r)
        return -ni <= r.i32() <= ne
    except FormatError:
        return False


def _check_model(d, pos, end, pkg):
    try:
        r = Reader(d, pos, end)
        r.take(40)
        for size in (12, 12, XBOX_BSP_NODE, 52, 8):
            n = r.count(1 << 20)
            r.take(size * n)
        return True
    except FormatError:
        return False


def _check_polys(d, pos, end, pkg):
    try:
        r = Reader(d, pos, end)
        return r.i32() == r.i32()
    except FormatError:
        return False


NATIVE_CHECKS = {'Level': _check_level, 'Model': _check_model, 'Polys': _check_polys}

TAG_SIZES = {1: (1,), 2: (4,), 4: (4,), 5: (4,), 6: (4,), 8: (4,), 11: (12,), 12: (12,)}


def check_object_head(data, flags, pkg, pos=0, size=None):
    """Parse an object's state frame and tags at pos strictly against pkg's tables: their length, or None."""
    nn, ni, ne = len(pkg.names), len(pkg.imports), len(pkg.exports)
    none = pkg.none_index()
    r = Reader(data, pos, None if size is None else pos + size)
    try:
        if flags & RF_HasStack:
            node, state = r.i32(), r.i32()
            if not (-ni <= node <= ne and -ni <= state <= ne):
                return None
            r.take(10)
            if node:
                r.i32()
        while True:
            ver = r.u8()
            if ver not in (0, 1):
                return None
            key = r.i32()
            if ver == 0:
                if not 0 <= key < nn:
                    return None
                if key == none:
                    return r.p - pos
            else:
                if key == 0:
                    return r.p - pos
                if not -ni <= key < 0:
                    return None
            info = r.u8()
            typ = info & 0x0F
            if typ == 0 or typ > 15:
                return None
            if ver == 0 and typ == 10 and not 0 <= r.i32() < nn:
                return None
            sz = {0x00: 1, 0x10: 2, 0x20: 4, 0x30: 12, 0x40: 16}.get(info & 0x70)
            if sz is None:
                sz = {0x50: r.u8, 0x60: r.u16, 0x70: r.i32}[info & 0x70]()
            if typ in TAG_SIZES and sz not in TAG_SIZES[typ]:
                return None
            if info & 0x80 and typ != 3:
                b = r.u8()
                if b & 0x80:
                    r.take(1 if b & 0xC0 == 0x80 else 3)
            if typ == 3:
                continue
            v = r.take(sz)
            if typ in (5, 8) and not -ni <= struct.unpack('<i', v)[0] <= ne:
                return None
            if typ == 6 and not 0 <= struct.unpack('<i', v)[0] < nn:
                return None
    except FormatError:
        return None


def find_level(path):
    """Offset of the summary of the package with a Level export in a linear-load stream (the entry level in
    System/Script.lin, after the script packages), or None."""
    with open(path, 'rb') as f:
        data = f.read()
    tag = struct.pack('<I', TAG)
    at = data.find(tag)
    while at >= 0:
        try:
            pkg = Package()
            r = Reader(data, at)
            pkg.version, pkg.licensee, pkg.flags, counts, pkg.guid, _ = Package._summary(r)
            nc, _, ec, _, ic, _ = counts
            if pkg.version == 151 and 0 < nc < 100000 and 0 < ec < 100000:
                pkg._read_names(r, nc)
                pkg._read_imports(r, ic)
                pkg._read_exports(r, ec)
                if any(pkg.export_class(i) == 'Level' for i in range(ec)):
                    return at
        except (FormatError, IndexError, UnicodeDecodeError, struct.error):
            pass
        at = data.find(tag, at + 1)
    return None


def _creation_order(pkg):
    """GObjLoaded order after ULinkerLoad::LoadAllObjects: table order, each export's outer created first."""
    order, seen = [], set()

    def create(i):
        if i in seen:
            return
        e = pkg.exports[i]
        if not e.runtime():
            return
        seen.add(i)
        if e.pkg > 0:
            create(e.pkg - 1)
        order.append(i)

    for i in range(len(pkg.exports)):
        create(i)
    return order


def read_lin(path, foreign, start=0):
    """Cut a .LIN back into the map package it was recorded from (Xbox format, licensee 1). start: where the
    package's summary is in the stream (System/Script.lin holds the entry level after the script packages)."""
    with open(path, 'rb') as f:
        data = f.read()
    pkg = Package()
    pkg.path = path
    r = Reader(data, start)
    pkg.version, pkg.licensee, pkg.flags, counts, pkg.guid, _ = Package._summary(r)
    nc, _, ec, _, ic, _ = counts
    pkg._read_names(r, nc)
    pkg._read_imports(r, ic)
    pkg._read_exports(r, ec)
    for e in pkg.exports:
        e.data = None
    order = _creation_order(pkg)
    if not order:
        raise FormatError('%s: no exports to load' % path)
    lin = _LinCursor(data, r.p, pkg, foreign, order)
    for i in order:
        if pkg.exports[i].data is None:
            lin.load(i)
    lin.tail = len(data) - lin.p if lin.open is None else 0
    lin.finish()
    # what the Xbox never loaded: no data; empty packages need none either
    for e in pkg.exports:
        if e.data is None:
            e.data = b''
    pkg.lin_tail = lin.tail
    pkg.lin_foreign = lin.foreign_bytes
    return pkg


# Classes whose serialisation leaves lazy arrays unread on the Xbox (GLazyLoad forced on): their stream length
# comes from a walker (class name -> f(data, pos, export, pkg) -> length), else from where the next export starts.
LIN_WALKERS = {}
LIN_LAZY = {'SkeletalMesh'}


class _LinCursor:
    def __init__(self, data, pos, pkg, foreign, order):
        self.d, self.p, self.pkg, self.foreign = data, pos, pkg, foreign
        self.next = {a: b for a, b in zip(order, order[1:])}
        self.foreign_bytes = 0
        self.open = None        # (export, start) of a lazy export whose end is the next export's start

    def take(self, n):
        if self.p + n > len(self.d):
            raise FormatError('%s: stream ends inside an export' % self.pkg.path)
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def plausible(self, i, pos):
        """Whether export i's data can start at pos: its state frame and tags parse against the tables and end
        inside its size, exactly at its end for the classes without native data."""
        e = self.pkg.exports[i]
        end = check_object_head(self.d, e.flags, self.pkg, pos, e.size)
        if end is None or end > e.size:
            return False
        cls = self.pkg.export_class(i)
        if end == e.size:
            return True
        if cls not in NATIVE_CLASSES:
            return False
        check = NATIVE_CHECKS.get(cls)
        return check is None or check(self.d, pos + end, pos + e.size, self.pkg)

    def seek(self, i):
        """Move to where export i's data starts, past reads from other packages (the classes and objects they
        loaded, not all of them in a file on the disc)."""
        start = self.p
        if self.open is not None:
            self.p += 8
        limit = min(len(self.d), start + (1 << 22))
        while self.p < limit:
            if self.plausible(i, self.p) and (self.p == start and self.open is None or self.genuine(i, self.p)):
                if self.open is not None:
                    self.p = self.prefer_tags(i, self.p)
                    j, at = self.open
                    self.pkg.exports[j].lin = self.d[at:self.p]
                    self.open = None
                else:
                    self.foreign_bytes += self.p - start
                return
            self.p += 1
        raise FormatError('%s: export %d (%s) not found after %d' % (
            self.pkg.path, i, self.pkg.export_path(i), start))

    def prefer_tags(self, i, pos):
        """Zero bytes parse as an empty tag block: past a lazy export, take a later start with tags if one follows
        within a few bytes."""
        e = self.pkg.exports[i]
        if check_object_head(self.d, e.flags, self.pkg, pos, e.size) > 5 + (9 if e.flags & RF_HasStack else 0):
            return pos
        for q in range(pos + 1, pos + 17):
            end = check_object_head(self.d, e.flags, self.pkg, q, e.size)
            if end is not None and end > 5 and self.plausible(i, q) and self.confirmed(i, q):
                return q
        return pos

    def genuine(self, i, pos):
        """A start found by searching: not bytes of another package (a run read from a script package can parse
        as a small export), and past a lazy export, followed by the next export."""
        size = self.pkg.exports[i].size
        if size >= 8 and self.foreign.contains(self.d[pos:pos + size]):
            return False
        return self.open is None or self.confirmed(i, pos)

    def confirmed(self, i, pos):
        """A start found inside a lazy export's stream: the next unloaded export in load order also parses after
        it (past reads from other packages)."""
        cls = self.pkg.export_class(i)
        if cls in ('Model', 'Level') or cls in LIN_LAZY:
            return True
        j = self.next.get(i)
        while j is not None and self.pkg.exports[j].data is not None:
            j = self.next.get(j)
        if j is None:
            return True
        at = pos + self.pkg.exports[i].size
        for _ in range(64):
            if self.plausible(j, at):
                return True
            n = self.foreign.match(self.d, at)
            if not n:
                return False
            at += n
        return False

    def finish(self):
        if self.open is not None:
            j, at = self.open
            self.pkg.exports[j].lin = self.d[at:]
            self.open = None
            self.p = len(self.d)

    def load(self, i):
        e = self.pkg.exports[i]
        if e.data is not None:
            return
        self.seek(i)
        e.data = b''            # in progress: a nested preload of this export reads nothing
        cls = self.pkg.export_class(i)
        if cls == 'Model':
            cut, polys = model_polys_ref_end(self.d[self.p:self.p + e.size], e.flags, XBOX_BSP_NODE, self.pkg)
            part = self.take(cut)
            # the Xbox never created exports flagged for neither client nor server, so never preloaded them
            if polys > 0 and self.pkg.exports[polys - 1].runtime():
                self.load(polys - 1)
            e.data = part + self.take(e.size - cut)
        elif cls == 'Level':
            # ULevel::Serialize preloads its Model after everything else it reads
            e.data = self.take(e.size)
            model = level_model_ref(e.data, e.flags, self.pkg)
            if model > 0 and self.pkg.exports[model - 1].runtime():
                self.load(model - 1)
        elif cls in LIN_LAZY:
            walker = LIN_WALKERS.get(cls)
            if walker is not None:
                e.lin = self.take(walker(self.d, self.p, e, self.pkg))
            else:
                self.open = (i, self.p)
        else:
            e.data = self.take(e.size)


def level_model_ref(data, flags, pkg=None):
    """ULevel::Serialize: ULevelBase (actor count, max, the actors, URL), then the Model reference."""
    r = Reader(data, object_head_end(data, flags, pkg))
    n = r.count()
    r.i32()
    r.take(4 * n)
    _skip_url(r)
    return r.i32()


def _skip_fstring(r):
    n = r.i32()
    if n < 0:
        r.take(-2 * n)
    else:
        r.take(n)


def _skip_url(r):
    """FURL<<: Protocol, Host, Map, Portal, Op (array of strings), Port, Valid."""
    for _ in range(4):
        _skip_fstring(r)
    for _ in range(r.count()):
        _skip_fstring(r)
    r.i32()
    r.i32()
