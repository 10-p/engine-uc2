#!/usr/bin/env python3
"""Static meshes of Unreal Championship 2's Xbox packages (licensee 1) rewritten in the PC format (licensee 0):
UStaticMesh, UStaticMeshInstance and UStaticProjectorClippedPrim exports, their vertex, colour, UV and index data
taken back from the disc's XPR bundles. Used by the package converter next to tools/uc2xpkg.py.

    import uc2xmesh
    out = uc2xmesh.convert(pkg, i, ctx)     # PC export data for export i of the Xbox package pkg
    # ctx.xpr     uc2xpr.XprSet over the disc's XboxTextures
    # ctx.prefer  bundle basenames searched first ('dm-legacy_uma.xpr', 'dm-legacy_pb.xpr', 'natives_uma.xpr', ...)
    # ctx.warn    callable(msg); ctx.info, if present, hears which package meshes took a map copy's streams
    # ctx.map_copies  optional uc2xtables.MapCopies: the maps' merged copies by the object they stand for
    data, lazy = out if isinstance(out, tuple) else (out, [])
    # lazy: offsets in data of the TLazyArray skip offsets (StaticMesh RawTriangles), written relative to the
    # export's start; the package writer adds the export's file offset to each (uc2xmesh.relocate)
    uc2xpkg.NATIVE_CHECKS.update(uc2xmesh.NATIVE_CHECKS)   # before read_lin: where these exports start in a .LIN

Layouts (Engine/Src/UnStaticMesh.cpp UStaticMesh::Serialize; array counts and object references are i32 from
package version 145; UBOOL is 4 bytes):
  UStaticMesh   tags | UPrimitive BoundingBox (24), BoundingSphere (16) | Sections: n x {UBOOL IsStrip, u16
                FirstIndex, MinVertexIndex, MaxVertexIndex, NumTriangles, NumPrimitives} | BoundingBox (24) |
                CompressionScale, CompressionInvScale (2 x 12), UseTriStrips, OldUseTriStrips, bCacheOptimize,
                OldbCacheOptimize (4 x 4), CompressionOffset (12) | VertexStream {Vertices, i32 Revision} |
                ColorStream, AlphaStream {Colors, i32 Revision} | UVStreams: n x {UVs, i32 CoordinateIndex, i32
                Revision} | IndexBuffer {u16 array, i32 Revision} | WireframeIndexBuffer (PC only) | CollisionModel
                ref | kDOPTree {Nodes: n x 32 (6 floats, UBOOL, 2 x u16), Triangles: n x 8 (4 x u16)} |
                RawTriangles (PC only: i32 absolute file offset of its end, then the array) | i32 InternalVersion
                | u32 AuthenticationKey
  vertex        PC: FVector Position, FVector Normal (24). Xbox (XBOX_ASSET_FORMAT): s16 x 3 FPackedPosition
                (x / 32767 * CompressionScale + CompressionOffset), u32 FPackedNormal (11:11:10 signed, x/1023,
                y/1023, z/511, packed by truncation), 10 bytes
  Xbox arrays   Vertices, Colors and UVs are TUmaArray: a UBOOL bIsInXPR first; when set the array has no count
                and no data here, they are the bundle resource '<mesh>_VS', '_CS', '_AS', '_UV<n>' (u32 Count,
                u32 Offset into video memory; elements as in memory: FColor is B,G,R,A there, R,G,B,A in a
                package). The Xbox IndexBuffer is empty: each section with primitives is the push buffer
                '<mesh>_<section>_pb' (XGCompileDrawIndexedVertices of NumPrimitives + 2 strip indices or
                NumPrimitives x 3 list indices from FirstIndex). No WireframeIndexBuffer, no RawTriangles.
  UStaticMeshInstance  tags | ColorStream {Colors, i32 Revision} | Lights (PC only: n x {ref LightActor, u8 array
                VisibilityBits, UBOOL Applied}); the Xbox colours are '<instance>_CS' in the map's bundle
  UStaticProjectorClippedPrim  tags | UPrimitive | VertexStream {Vertices, Revision} (PC vertex: WorldPosition,
                Normal, f32 Attenuation, FVector ProjectedPosition, 40 bytes; Xbox: WorldPosition, FPackedNormal,
                16 bytes, '<prim>_pvs') | UVStream {UVs, Revision} | IndexBuffer {u16 array, Revision} (Xbox: the
                triangle-list push buffer '<prim>_pb') | BaseMaterial ref

Where the streams are: a mesh's own '<mesh>_VS' etc. in any bundle (ctx.prefer's first), each copy checked against
the export (vertex count, BoundingBox extent, kDOP leaf bounds, section index ranges, collision triangles among the
drawn ones). A map's merged meshes are renamed cla<N> with their streams in the map's bundles; a static mesh
package's mesh used only by maps has no streams under its own name, so the maps' copies that stand for it stand in
(ctx.map_copies, uc2xtables.MapCopies, from the maps' PC tables), and else the copies with the same native data
that stand for no other object, provided no other package mesh has that native data (the disc's maps and packages
are indexed once per XprSet, next to its XboxTextures folder). A mesh with no streams on the disc is written
without geometry.

What the PC format gets: positions unpacked to within half a quantisation step (CompressionScale / 32767 per axis)
plus single-precision rounding, normals to within an eighth of a degree (the middle of FVector::Pack's truncation
interval, normalised), colours, UVs, indices and everything else exact; the wireframe rebuilt as UStaticMesh::Build
does but from the stripped triangles (the same line segments, in another order and direction); RawTriangles, the
editor's source triangles (only UStaticMesh::Build and the editor read them; InternalVersion 14 keeps PostLoad from
rebuilding), rebuilt from the render data by tools/uc2xrawtri.py; instance Lights empty (the baked ColorStream is
drawn as is; the editor's incremental relighting reads them).
"""
import array
import hashlib
import os
import struct

import uc2xpkg
import uc2xrawtri
from uc2xpkg import FormatError, Reader, object_head_end

STATICMESH_VERSION = 14

XBOX_VERTEX = 10        # FPackedPosition (3 x s16) + FPackedNormal (u32)
PC_VERTEX = 24          # FVector Position + FVector Normal
XBOX_PROJ_VERTEX = 16   # FVector WorldPosition + FPackedNormal
PC_PROJ_VERTEX = 40     # + FVector Normal, f32 Attenuation, FVector ProjectedPosition
KDOP_NODE = 32
KDOP_TRIANGLE = 8

# NV2A methods in the push buffers of XGCompileDrawIndexedVertices
NV_NO_OPERATION = 0x0100
NV_SET_BEGIN_END = 0x17FC
NV_ARRAY_ELEMENT16 = 0x1800
NV_ARRAY_ELEMENT32 = 0x1808
PRIM_TRIANGLE_LIST = 5
PRIM_TRIANGLE_STRIP = 6

_ONE_OVER_32767 = struct.unpack('<f', struct.pack('<f', 1.0 / 32767.0))[0]


# ---------------------------------------------------------------------------------------------------------------
# Packed vectors (Core/Inc/UnMath.h)
# ---------------------------------------------------------------------------------------------------------------
def _signed(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def unpack_normal(dw, centred=True):
    """FPackedNormal to (x, y, z). centred=False is FVector::Unpack (x/1023, y/1023, z/511, the truncated values);
    centred=True takes the middle of each truncation interval and normalises, the closer estimate of the PC float."""
    x, y, z = _signed(dw & 0x7FF, 11), _signed((dw >> 11) & 0x7FF, 11), _signed(dw >> 22, 10)
    if not centred:
        return x / 1023.0, y / 1023.0, z / 511.0
    # FVector::Pack truncates toward zero: v stands for [v, v+1) above zero, (v-1, v] below, (-1, 1) at zero
    fx = (x + (0.5 if x > 0 else -0.5 if x < 0 else 0.0)) / 1023.0
    fy = (y + (0.5 if y > 0 else -0.5 if y < 0 else 0.0)) / 1023.0
    fz = (z + (0.5 if z > 0 else -0.5 if z < 0 else 0.0)) / 511.0
    n = (fx * fx + fy * fy + fz * fz) ** 0.5
    if n == 0.0:
        return 0.0, 0.0, 0.0
    return fx / n, fy / n, fz / n


def pack_normal(x, y, z):
    """FVector::Pack (truncation toward zero, as the conversion tool's MSVC float to DWORD did)."""
    return ((int(z * 511.0) & 0x3FF) << 22) | ((int(y * 1023.0) & 0x7FF) << 11) | (int(x * 1023.0) & 0x7FF)


def unpack_position(x, y, z, scale, offset):
    """FPackedPosition::Unpack in single precision: s16 / 32767 * CompressionScale + CompressionOffset."""
    f = array.array('f', (x * _ONE_OVER_32767, y * _ONE_OVER_32767, z * _ONE_OVER_32767))
    f = array.array('f', (f[0] * scale[0], f[1] * scale[1], f[2] * scale[2]))
    f = array.array('f', (f[0] + offset[0], f[1] + offset[1], f[2] + offset[2]))
    return f[0], f[1], f[2]


def unpack_vertices(raw, count, scale, offset):
    """Xbox FStaticMeshVertex records (10 bytes) to PC records (24 bytes: position, normal as floats)."""
    recs = list(struct.iter_unpack('<3hI', raw[:count * XBOX_VERTEX]))
    out = array.array('f', bytes(4 * 6 * count))
    for axis in range(3):
        s, o = scale[axis], offset[axis]
        # each step rounded to single precision, as the engine computes it
        v = array.array('f', [r[axis] * _ONE_OVER_32767 for r in recs])
        v = array.array('f', [a * s for a in v])
        v = array.array('f', [a + o for a in v])
        out[axis::6] = v
    normals = [unpack_normal(r[3]) for r in recs]
    for axis in range(3):
        out[3 + axis::6] = array.array('f', [n[axis] for n in normals])
    return out.tobytes()


def head_end(pkg, i):
    """Offset where export i's native data starts: after the state frame and the tagged properties."""
    e = pkg.exports[i]
    return object_head_end(e.data, e.flags, pkg)


# ---------------------------------------------------------------------------------------------------------------
# XPR lookup
# ---------------------------------------------------------------------------------------------------------------
def _ordered(ctx, bases, first=None):
    """Bundle basenames in search order: first, then ctx.prefer's in their order, then the others by name."""
    prefer = ([first] if first else []) + [p.lower() for p in getattr(ctx, 'prefer', ())]
    order = [p for i, p in enumerate(prefer) if p in bases and p not in prefer[:i]]
    return order + sorted(b for b in bases if b not in order)


def _preferred(ctx):
    return [p.lower() for p in getattr(ctx, 'prefer', ())]


def _candidates(ctx, name, first=None, only=None):
    """(basename, bundle) of the bundles holding resource name in search order, loaded as the caller goes; only
    limits them to the given basenames."""
    bases = ctx.xpr.holders(name)
    if only is not None:
        bases = [b for b in bases if b in only]
    for base in _ordered(ctx, bases, first):
        yield base, ctx.xpr.find(name, [base])


def uma_array(bundle, name, elem_size):
    """A TUmaArray resource: (count, element bytes)."""
    count, off = struct.unpack_from('<II', bundle.header(name, 8))
    data = bundle.vidmem[off:off + count * elem_size]
    if len(data) != count * elem_size:
        raise FormatError('%s: %s runs past the bundle' % (bundle.path, name))
    return count, data


def decode_push_buffer(data):
    """The primitives a push buffer (uc2xpr Bundle.push_buffer) draws: [(primitive type, [indices])]. Reads
    SET_BEGIN_END and the ARRAY_ELEMENT16 (index pairs, low half first) / ARRAY_ELEMENT32 (the odd last index)
    methods up to the END that is not followed by another BEGIN; what follows is allocation padding."""
    words = array.array('I', data[:len(data) // 4 * 4])
    prims, cur, i, n = [], None, 0, len(words)
    while i < n:
        w = words[i]
        if w & 3:
            raise FormatError('push buffer: jump or call %08x' % w)
        method, count, fixed = w & 0x1FFC, (w >> 18) & 0x7FF, bool(w & 0x40000000)
        args = words[i + 1:i + 1 + count]
        if len(args) != count:
            raise FormatError('push buffer: method %04x past the end' % method)
        i += 1 + count
        for k, v in enumerate(args):
            m = method if fixed else method + 4 * k
            if m == NV_SET_BEGIN_END:
                if v:
                    cur = (v, [])
                elif cur is not None:
                    prims.append(cur)
                    cur = None
            elif NV_ARRAY_ELEMENT16 <= m < NV_ARRAY_ELEMENT32 and cur is not None:
                cur[1].append(v & 0xFFFF)
                cur[1].append(v >> 16)
            elif m == NV_ARRAY_ELEMENT32 and cur is not None:
                cur[1].append(v & 0xFFFF)
            elif m == NV_NO_OPERATION:
                break
            else:
                raise FormatError('push buffer: method %04x' % m)
        if cur is None and prims and (i >= n or words[i] & 0x1FFC != NV_SET_BEGIN_END or
                                      i + 1 >= n or words[i + 1] == 0):
            break
    if cur is not None:
        raise FormatError('push buffer: primitive without END')
    return prims


# ---------------------------------------------------------------------------------------------------------------
# UStaticMesh
# ---------------------------------------------------------------------------------------------------------------
class Mesh:
    """UStaticMesh's native data, either format. Array fields hold element bytes in the PC element format; an Xbox
    array still in its bundle is None."""

    def __init__(self):
        self.sections = []          # [is_strip, first, min_vertex, max_vertex, num_triangles, num_primitives]
        self.uvs = []               # [bytes or None, coordinate index, revision]
        self.wire = b''
        self.wire_rev = 0
        self.raw_triangles = None   # PC: (count, bytes)

    def vertex_count(self):
        return len(self.vertices) // PC_VERTEX


def _section(r):
    return list(struct.unpack('<I5H', r.take(14)))


def _xbox_uma(r, elem_xbox, convert=None):
    """A licensee-1 TUmaArray: None when its elements are in the XPR, else its elements."""
    in_xpr = r.u32()
    if in_xpr > 1:
        raise FormatError('TUmaArray: bIsInXPR %d' % in_xpr)
    if in_xpr:
        return None
    n = r.count()
    raw = r.take(n * elem_xbox)
    return convert(raw, n) if convert else raw


def parse_mesh(data, start, licensee, export_offset=0, end=None):
    """UStaticMesh export data of a package of the given licensee, its native data from start (head_end) to end
    (the data's end). For the PC format export_offset is the export's file offset (RawTriangles' skip offset is
    absolute)."""
    r = Reader(data, start, end)
    m = Mesh()
    m.head = data[:r.p]
    m.prim_bounds = r.take(40)
    m.sections = [_section(r) for _ in range(r.count())]
    m.box = r.take(24)
    m.compression = r.take(40)
    m.scale = struct.unpack_from('<3f', m.compression, 0)
    m.inv_scale = struct.unpack_from('<3f', m.compression, 12)
    m.use_strips, m.old_use_strips, m.cache_opt, m.old_cache_opt = struct.unpack_from('<4I', m.compression, 24)
    m.offset = struct.unpack('<3f', r.take(12))
    if licensee == 1:
        m.vertices = _xbox_uma(r, XBOX_VERTEX, lambda raw, n: unpack_vertices(raw, n, m.scale, m.offset))
        m.vertex_rev = r.i32()
        m.colors = _xbox_uma(r, 4)
        m.color_rev = r.i32()
        m.alpha = _xbox_uma(r, 4)
        m.alpha_rev = r.i32()
        for _ in range(r.count()):
            m.uvs.append([_xbox_uma(r, 8), r.i32(), r.i32()])
    else:
        m.vertices = r.take(PC_VERTEX * r.count())
        m.vertex_rev = r.i32()
        m.colors = r.take(4 * r.count())
        m.color_rev = r.i32()
        m.alpha = r.take(4 * r.count())
        m.alpha_rev = r.i32()
        for _ in range(r.count()):
            m.uvs.append([r.take(8 * r.count()), r.i32(), r.i32()])
    m.indices = r.take(2 * r.count())
    m.index_rev = r.i32()
    if licensee != 1:
        m.wire = r.take(2 * r.count())
        m.wire_rev = r.i32()
    m.collision = r.i32()
    m.nodes = r.take(KDOP_NODE * r.count())
    m.tris = r.take(KDOP_TRIANGLE * r.count())
    if licensee != 1:
        end = r.i32() - export_offset
        n = r.count()
        m.raw_triangles = (n, data[r.p:end])
        r.p = end
    m.internal_version = r.i32()
    m.auth_key = r.u32()
    if r.p != r.end:
        raise FormatError('StaticMesh: %d bytes after the native data' % (r.end - r.p))
    return m


def write_mesh(m):
    """PC (licensee 0) export data and the offset of RawTriangles' skip offset in it: the array is m.raw_triangles
    (count, element bytes) or empty, the skip offset relative to the export's start."""
    out = bytearray(m.head)
    out += m.prim_bounds
    out += struct.pack('<i', len(m.sections))
    for s in m.sections:
        out += struct.pack('<I5H', *s)
    out += m.box + m.compression + struct.pack('<3f', *m.offset)
    for arr, size, rev in ((m.vertices, PC_VERTEX, m.vertex_rev), (m.colors, 4, m.color_rev),
                           (m.alpha, 4, m.alpha_rev)):
        out += struct.pack('<i', len(arr) // size) + arr + struct.pack('<i', rev)
    out += struct.pack('<i', len(m.uvs))
    for uv, coord, rev in m.uvs:
        out += struct.pack('<i', len(uv) // 8) + uv + struct.pack('<ii', coord, rev)
    out += struct.pack('<i', len(m.indices) // 2) + m.indices + struct.pack('<i', m.index_rev)
    out += struct.pack('<i', len(m.wire) // 2) + m.wire + struct.pack('<i', m.wire_rev)
    out += struct.pack('<i', m.collision)
    out += struct.pack('<i', len(m.nodes) // KDOP_NODE) + m.nodes
    out += struct.pack('<i', len(m.tris) // KDOP_TRIANGLE) + m.tris
    pos = len(out)
    count, raw = m.raw_triangles or (0, b'')
    out += struct.pack('<ii', pos + 8 + len(raw), count) + raw
    out += struct.pack('<iI', m.internal_version, m.auth_key)
    return bytes(out), [pos]


def swap_rb(raw):
    """FColor in memory (B,G,R,A on the Xbox, as the bundles hold it) to its serialised order (R,G,B,A)."""
    b = bytearray(raw)
    b[0::4], b[2::4] = raw[2::4], raw[0::4]
    return bytes(b)


def section_index_count(s):
    if not s[5]:
        return 0
    return s[5] + 2 if s[0] else s[5] * 3


def strip_triangles(indices, s):
    """A section's triangles as UStaticMesh::Build and xbstrip's TriStripToTriList see them: strip triangles
    with odd ones flipped, degenerate ones left out; list triangles as they are."""
    first, nprim = s[1], s[5]
    if not s[0]:
        return [tuple(indices[first + 3 * t:first + 3 * t + 3]) for t in range(nprim)]
    out = []
    for t in range(nprim):
        a, b, c = indices[first + t:first + t + 3]
        if t & 1:
            a, b = b, a
        if a != b and b != c and a != c:
            out.append((a, b, c))
    return out


def build_wireframe(indices, sections):
    """WireframeIndexBuffer as UStaticMesh::Build makes it: every triangle edge, an edge left out when an earlier
    one runs the other way between the same vertices (FindEdgeIndex)."""
    seen, out = set(), array.array('H')
    for s in sections:
        if not s[5]:
            continue
        for tri in strip_triangles(indices, s):
            for k in range(3):
                a, b = tri[k], tri[(k + 1) % 3]
                if (b, a) in seen:
                    continue
                seen.add((a, b))
                out.append(a)
                out.append(b)
    return out.tobytes()


def _box_check(m, raw, count):
    """Whether packed vertices span the mesh's bounding box (BoundingBox is exactly their extent on PC)."""
    if not count:
        return False
    box = struct.unpack('<6f', m.box)
    recs = list(struct.iter_unpack('<3hI', raw))
    for axis in range(3):
        col = [r[axis] for r in recs]
        step = abs(m.scale[axis]) / 32767.0 + 1e-4 * max(1.0, abs(box[axis]), abs(box[3 + axis]))
        lo = min(col) * _ONE_OVER_32767 * m.scale[axis] + m.offset[axis]
        hi = max(col) * _ONE_OVER_32767 * m.scale[axis] + m.offset[axis]
        if abs(lo - box[axis]) > step or abs(hi - box[3 + axis]) > step:
            return False
    return True


def _kdop_check(m, count, vertices):
    """Whether vertices (PC records) fit the kDOP tree: every collision triangle's vertices exist and lie inside
    the bounds of the leaf holding it (within the quantisation step)."""
    tris = list(struct.iter_unpack('<4H', m.tris))
    if any(max(t[:3]) >= count for t in tris):
        return False
    pos = list(struct.iter_unpack('<6f', vertices))
    tol = [abs(s) / 32767.0 + 1e-3 for s in m.scale]
    for node in struct.iter_unpack('<6fI2H', m.nodes):
        if not node[6]:
            continue
        ntri, start = node[7], node[8]
        for t in tris[start:start + ntri]:
            for v in t[:3]:
                p = pos[v]
                for a in range(3):
                    if p[a] < node[a] - tol[a] or p[a] > node[3 + a] + tol[a]:
                        return False
    return True


def _turn(t):
    """A triangle with its smallest index first, its winding kept."""
    k = t.index(min(t))
    return t[k:] + t[:k]


def _kdop_in_indices(m, indices):
    """Whether every kDOP collision triangle is a drawn triangle (UStaticMesh::Build makes both from the same
    indexed triangles, the strips keeping their winding)."""
    drawn = set()
    for s in m.sections:
        if s[5]:
            drawn.update(_turn(t) for t in strip_triangles(indices, s))
    return all(_turn(t[:3]) in drawn for t in struct.iter_unpack('<4H', m.tris))


def native_key(pkg, i):
    """What identifies a static mesh across packages: its native data with the CollisionModel reference zeroed
    (a map's merged copy, renamed cla<N>, keeps everything else; its tags point at the map's renamed materials)."""
    e = pkg.exports[i]
    start = head_end(pkg, i)
    m = parse_mesh(e.data, start, 1)
    d = bytearray(e.data[start:])
    struct.pack_into('<i', d, len(d) - 8 - len(m.tris) - 4 - len(m.nodes) - 4 - 4, 0)
    return hashlib.sha1(bytes(d)).digest()


def _owner(pkg, i):
    return '%s@%s' % (pkg.export_path(i).lower(), os.path.basename(pkg.path).lower())


def _register(reg, pkg, bundles=None):
    """Note the static meshes of an Xbox package: a map's as copies whose streams are in the map's bundles, a static
    mesh package's as the meshes a native key may belong to."""
    for i in range(len(pkg.exports)):
        if pkg.export_class(i) != 'StaticMesh' or not pkg.exports[i].data:
            continue
        try:
            k = native_key(pkg, i)
        except FormatError:
            continue
        if bundles:
            reg['copies'].setdefault(k, []).append((pkg.name(pkg.exports[i].name), bundles))
        else:
            reg['meshes'].setdefault(k, set()).add(_owner(pkg, i))


def _registry(ctx):
    """The disc's static meshes by native key, built once per XprSet: the merged copies of every map
    (XboxMaps/*.LIN and the entry level in System/Script.lin, next to the XboxTextures folder) and the meshes of
    every static mesh package (XboxStaticMeshes/*.usx)."""
    reg = getattr(ctx.xpr, '_uc2xmesh_registry', None)
    if reg is not None:
        return reg
    reg = ctx.xpr._uc2xmesh_registry = {'copies': {}, 'meshes': {}}
    for cls, check in NATIVE_CHECKS.items():
        uc2xpkg.NATIVE_CHECKS.setdefault(cls, check)
    disc = os.path.dirname(os.path.normpath(ctx.xpr.folder))
    foreign = uc2xpkg.ForeignIndex()
    for path in uc2xpkg.files_in(disc, 'System', '.u'):
        foreign.add_file(path)
    maps = [(path, 0) for path in uc2xpkg.files_in(disc, 'XboxMaps', '.lin')]
    script = uc2xpkg.find_ci(disc, 'System/Script.lin')
    if script:
        start = uc2xpkg.find_level(script)
        if start is not None:
            maps.append((script, start))
    for path, start in maps:
        name = 'entry' if start else os.path.splitext(os.path.basename(path))[0].lower()
        try:
            pkg = uc2xpkg.read_lin(path, foreign, start)
        except (FormatError, ValueError, IndexError, struct.error) as e:
            ctx.warn('%s: not searched for static mesh copies (%s)' % (path, e))
            continue
        _register(reg, pkg, [name + '_uma.xpr', name + '_pb.xpr'])
    for path in uc2xpkg.files_in(disc, 'XboxStaticMeshes', '.usx'):
        _register(reg, uc2xpkg.Package.load(path))
    return reg


def _vertex_sources(pkg, i, name, ctx):
    """Where the streams may be: (bundle basenames, the name they are filed under, whether it is the mesh's own).
    '<name>_VS' in any bundle first; then the maps' merged copies that stand for this mesh (ctx.map_copies,
    uc2xtables.MapCopies); then the copies with the same native data that stand for no other object, when no
    other package mesh has that native data."""
    for base in _ordered(ctx, ctx.xpr.holders(name + '_VS')):
        yield [base], name, True
    named = getattr(ctx, 'map_copies', None)
    path = '%s.%s' % (os.path.splitext(os.path.basename(pkg.path or ''))[0], pkg.export_path(i))
    if named is not None:
        for m, copy in named.find(path):
            yield [m + '_uma.xpr', m + '_pb.xpr'], copy, False
    reg = _registry(ctx)
    k = native_key(pkg, i)
    owners = reg['meshes'].get(k, set()) | {_owner(pkg, i)}
    if len(owners) > 1:
        return
    for copy, bundles in reg['copies'].get(k, []):
        if named is None or named.stands_for(bundles[0][:-len('_uma.xpr')], copy) in (None, path.lower()):
            yield bundles, copy, False


def _streams(m, src, homes, count, ctx, own):
    """Colours, alpha, UVs (PC element order) and the index buffer filed under src, searched in the bundles homes
    first (the vertex stream's and its '_pb' bundle), or None when a copy (own false) lacks one there; the mesh's
    own streams fall back to other bundles, then to white, zero and degenerate data with a warning."""
    homes = list(homes)
    for h in list(homes):
        if h.endswith('_uma.xpr') and h[:-len('_uma.xpr')] + '_pb.xpr' not in homes:
            homes.append(h[:-len('_uma.xpr')] + '_pb.xpr')
    home = homes[0] if homes else None
    pb_home = next((h for h in homes if h.endswith('_pb.xpr')), None)
    only = None if own else homes
    out = {}
    wanted = [('colors', '_CS', 4), ('alpha', '_AS', 4)] + [('uv%d' % k, '_UV%d' % k, 8) for k in range(len(m.uvs))]
    for field, suffix, elem in wanted:
        cur = getattr(m, field) if field in ('colors', 'alpha') else m.uvs[int(field[2:])][0]
        if cur is not None:
            out[field] = cur
            continue
        raw = None
        for _base, b in _candidates(ctx, src + suffix, home, only):
            n, data = uma_array(b, src + suffix, elem)
            if n == count:
                raw = data
                break
        if raw is None:
            if not own:
                return None
            ctx.warn('%s: no %s%s of %d elements; written %s' % (src, src, suffix, count,
                                                                  'white' if elem == 4 else 'as 0'))
            raw = (b'\xff' if elem == 4 else b'\0') * (elem * count)
        out[field] = swap_rb(raw) if elem == 4 else raw
    if m.indices:
        out['indices'] = m.indices
        return out
    total = max([s[1] + section_index_count(s) for s in m.sections] + [0])
    idx = array.array('H', bytes(2 * total))
    filled = [False] * total
    for k, s in enumerate(m.sections):
        want = section_index_count(s)
        if not want:
            continue
        pb = '%s_%d_pb' % (src, k)
        prim = PRIM_TRIANGLE_STRIP if s[0] else PRIM_TRIANGLE_LIST
        found = None
        for _base, b in _candidates(ctx, pb, pb_home, only):
            try:
                prims = decode_push_buffer(b.push_buffer(pb))
            except FormatError as e:
                ctx.warn('%s in %s: %s' % (pb, b.path, e))
                continue
            vals = [v for p, vs in prims if p == prim for v in vs]
            if len(vals) == want and len(prims) == 1 and all(s[2] <= v <= s[3] for v in vals):
                found = vals
                break
        if found is None:
            if not own:
                return None
            ctx.warn('%s: no push buffer %s of %d indices; section %d written degenerate' % (src, pb, want, k))
            found = [s[2]] * want
        idx[s[1]:s[1] + want] = array.array('H', found)
        filled[s[1]:s[1] + want] = [True] * want
    if not _kdop_in_indices(m, idx):
        if not own:
            return None
        ctx.warn('%s: collision triangles missing from the push buffers' % src)
    if not all(filled):
        ctx.warn('%s: %d index buffer entries in no section, written as 0' % (src, filled.count(False)))
    out['indices'] = idx.tobytes()
    return out


def convert_mesh(pkg, i, ctx):
    e = pkg.exports[i]
    name = pkg.name(e.name)
    if pkg.licensee != 1:
        raise FormatError('%s: not an Xbox package' % pkg.path)
    m = parse_mesh(e.data, head_end(pkg, i), pkg.licensee)
    streams = None
    if m.vertices is not None:
        streams = _streams(m, name, [], m.vertex_count(), ctx, True)
    else:
        need = max([s[3] + 1 for s in m.sections if s[5]] + [0])
        for homes, src, own in _vertex_sources(pkg, i, name, ctx):
            b = next(_candidates(ctx, src + '_VS', homes[0], homes), (None, None))[1]
            if b is None:
                continue
            count, raw = uma_array(b, src + '_VS', XBOX_VERTEX)
            if count < need or not _box_check(m, raw, count):
                continue
            verts = unpack_vertices(raw, count, m.scale, m.offset)
            if not _kdop_check(m, count, verts):
                continue
            streams = _streams(m, src, homes, count, ctx, own)
            if streams is not None:
                m.vertices = verts
                if not own and hasattr(ctx, 'info'):
                    ctx.info('%s: streams of the copy %s in %s' % (pkg.export_path(i), src, homes[0]))
                break
    if streams is None:
        # no copy on the disc: a mesh no shipped map loads. Kept loadable: no streams, nothing drawn, no collision
        ctx.warn('%s: no vertex stream for %s on the disc; written without geometry' % (pkg.export_path(i), name))
        m.vertices, m.colors, m.alpha, m.indices = b'', b'', b'', b''
        m.uvs = [[b'', c, rv] for _, c, rv in m.uvs]
        for s in m.sections:
            s[4] = s[5] = 0
        m.nodes = m.tris = b''
        return write_mesh(m)
    m.colors, m.alpha, m.indices = streams['colors'], streams['alpha'], streams['indices']
    for k, uv in enumerate(m.uvs):
        uv[0] = streams['uv%d' % k]
    idx = array.array('H', m.indices)
    m.wire = build_wireframe(idx, m.sections)
    m.wire_rev = m.index_rev
    tris = [(k, t) for k, s in enumerate(m.sections) if s[5] for t in strip_triangles(idx, s)]
    count, raw, stats = uc2xrawtri.raw_triangles(m.vertices, m.colors, [uv[0] for uv in m.uvs], tris)
    if stats['unresolved']:
        ctx.warn('%s: smoothing of %d triangle pairs has no smoothing group bit left; written apart'
                 % (pkg.export_path(i), stats['unresolved']))
    m.raw_triangles = (count, raw)
    return write_mesh(m)


# ---------------------------------------------------------------------------------------------------------------
# UStaticMeshInstance
# ---------------------------------------------------------------------------------------------------------------
def convert_instance(pkg, i, ctx):
    """ColorStream from '<instance>_CS' of the package's own bundles (ctx.prefer: the names repeat from map to
    map), Lights empty."""
    e = pkg.exports[i]
    name = pkg.name(e.name)
    if pkg.licensee != 1:
        raise FormatError('%s: not an Xbox package' % pkg.path)
    r = Reader(e.data, head_end(pkg, i))
    out = bytearray(e.data[:r.p])
    if r.u32():
        b = next(_candidates(ctx, name + '_CS', only=_preferred(ctx)), (None, None))[1]
        if b is not None:
            n, raw = uma_array(b, name + '_CS', 4)
            colors = swap_rb(raw)
        else:
            ctx.warn('%s: no %s_CS in %s; colour stream left empty' % (pkg.export_path(i), name,
                                                                      ', '.join(getattr(ctx, 'prefer', ()))))
            n, colors = 0, b''
    else:
        n = r.count()
        colors = r.take(4 * n)
    rev = r.i32()
    if r.p != len(e.data):
        raise FormatError('StaticMeshInstance: %d bytes after the colour stream' % (len(e.data) - r.p))
    out += struct.pack('<i', n) + colors + struct.pack('<ii', rev, 0)
    return bytes(out)


# ---------------------------------------------------------------------------------------------------------------
# UStaticProjectorClippedPrim
# ---------------------------------------------------------------------------------------------------------------
def convert_projector_prim(pkg, i, ctx):
    """Vertices from '<prim>_pvs' (normals unpacked; Attenuation and ProjectedPosition, which the Xbox computes on
    the GPU and the PC stream does not declare, written 0), indices from the triangle list '<prim>_pb'."""
    e = pkg.exports[i]
    name = pkg.name(e.name)
    if pkg.licensee != 1:
        raise FormatError('%s: not an Xbox package' % pkg.path)
    r = Reader(e.data, head_end(pkg, i))
    out = bytearray(e.data[:r.p])
    out += r.take(40)
    if r.u32():
        b = next(_candidates(ctx, name + '_pvs', only=_preferred(ctx)), (None, None))[1]
        if b is not None:
            n, raw = uma_array(b, name + '_pvs', XBOX_PROJ_VERTEX)
        else:
            ctx.warn('%s: no %s_pvs; vertex stream left empty' % (pkg.export_path(i), name))
            n, raw = 0, b''
    else:
        n = r.count()
        raw = r.take(XBOX_PROJ_VERTEX * n)
    verts = bytearray()
    for x, y, z, nrm in struct.iter_unpack('<3fI', raw):
        verts += struct.pack('<3f3ff3f', x, y, z, *unpack_normal(nrm), 0.0, 0.0, 0.0, 0.0)
    out += struct.pack('<i', n) + verts + struct.pack('<i', r.i32())
    nuv = r.count()
    out += struct.pack('<i', nuv) + r.take(8 * nuv) + struct.pack('<i', r.i32())
    nidx = r.count()
    idx = r.take(2 * nidx)
    irev = r.i32()
    if not nidx:
        b = next(_candidates(ctx, name + '_pb', only=_preferred(ctx)), (None, None))[1]
        if b is not None:
            prims = decode_push_buffer(b.push_buffer(name + '_pb'))
            vals = [v for p, vs in prims if p == PRIM_TRIANGLE_LIST for v in vs]
            if any(v >= n for v in vals):
                ctx.warn('%s: %s_pb indexes past the %d vertices' % (pkg.export_path(i), name, n))
            idx = array.array('H', vals).tobytes()
    out += struct.pack('<i', len(idx) // 2) + idx + struct.pack('<i', irev)
    out += r.take(4)
    if r.p != len(e.data):
        raise FormatError('StaticProjectorClippedPrim: %d bytes after the native data' % (len(e.data) - r.p))
    return bytes(out)


# ---------------------------------------------------------------------------------------------------------------
# Native data checks for the linear-load streams
# ---------------------------------------------------------------------------------------------------------------
def _check_mesh(data, start, end, pkg):
    try:
        m = parse_mesh(data, start, 1, end=end)
    except FormatError:
        return False
    return all(s[0] in (0, 1) and s[2] <= s[3] for s in m.sections) and m.internal_version >= 0


def _check_uma(r, elem):
    in_xpr = r.u32()
    if in_xpr > 1:
        raise FormatError('bIsInXPR %d' % in_xpr)
    if not in_xpr:
        r.take(elem * r.count())


def _check_instance(data, start, end, pkg):
    r = Reader(data, start, end)
    try:
        _check_uma(r, 4)
        r.i32()
    except FormatError:
        return False
    return r.p == end


def _check_projector_prim(data, start, end, pkg):
    r = Reader(data, start, end)
    try:
        r.take(40)
        _check_uma(r, XBOX_PROJ_VERTEX)
        r.i32()
        r.take(8 * r.count() + 4)
        r.take(2 * r.count() + 4)
        r.i32()
    except FormatError:
        return False
    return r.p == end


# check(data, start, end, pkg): whether data[start:end] is the Xbox native data of the class (uc2xpkg.NATIVE_CHECKS,
# which read_lin uses to tell an export's start from the reads of other packages before it)
NATIVE_CHECKS = {
    'StaticMesh': _check_mesh,
    'StaticMeshInstance': _check_instance,
    'StaticProjectorClippedPrim': _check_projector_prim,
}


# ---------------------------------------------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------------------------------------------
CONVERTERS = {
    'StaticMesh': convert_mesh,
    'StaticMeshInstance': convert_instance,
    'StaticProjectorClippedPrim': convert_projector_prim,
}


def convert(pkg, i, ctx):
    """PC (licensee 0) export data for export i (StaticMesh, StaticMeshInstance, StaticProjectorClippedPrim) of
    the Xbox package pkg: bytes, or (bytes, lazy skip offset positions) for a StaticMesh."""
    return CONVERTERS[pkg.export_class(i)](pkg, i, ctx)


def relocate(data, positions, base):
    """Add base (the export's file offset) to the i32 at each position: the lazy skip offsets made absolute."""
    b = bytearray(data)
    for p in positions:
        struct.pack_into('<i', b, p, struct.unpack_from('<i', b, p)[0] + base)
    return bytes(b)
