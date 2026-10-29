#!/usr/bin/env python3
"""Where an export's data holds object references and names, for rewriting a package into another package's
tables (tools/cook-xbox-packages.py: the disc's packages in the layout of Epic's PC packages, so the network sees the
same packages). PC format (licensee 0), package version 151.

    from uc2xrefs import references
    for offset, kind in references(pkg, types, i):     # kind 'ref' (i32 object index) or 'name' (i32 name index)
        ...
    tag_references(pkg, types, i)                      # the state frame and the tags only (Xbox format too)

Covered: the state frame, the tagged properties (UnClass.cpp FPropertyTag<<: the key, a struct tag's ItemName, the
values; struct values are tagged too from version 118, but Vector, Rotator and Color, which are binary; array
elements by their inner property, from the script packages through uc2types, or NATIVE_ARRAYS for the properties
the engine declares in C++), and the native data of the classes that have it (NATIVE).
"""
import struct

from uc2xpkg import FormatError, Reader, RF_HasStack
from uc2types import BINARY_STRUCTS

# Array properties declared in C++ (StaticConstructor), with no script definition: inner kind
NATIVE_ARRAYS = {
    ('engine.staticmesh', 'materials'): 'StructProperty',
}
# A struct's binary size where struct values are binary
BINARY_SIZE = {'vector': 12, 'rotator': 12, 'color': 4}
ELEMENT_SIZE = {'ByteProperty': 1, 'IntProperty': 4, 'FloatProperty': 4, 'BoolProperty': 1}


class Walker:
    def __init__(self, pkg, types, data):
        self.pkg, self.types, self.d = pkg, types, data
        self.out = []
        self.none = pkg.none_index()

    def ref(self, at):
        self.out.append((at, 'ref'))

    def name(self, at):
        self.out.append((at, 'name'))

    # -- tagged properties
    def tags(self, pos, owner, end=None):
        """The tag stream at pos of an object of class (or a struct value of struct) owner: its end."""
        r = Reader(self.d, pos, end)
        while True:
            ver = r.u8()
            at = r.p
            key = r.i32()
            if ver == 0:
                self.name(at)
                if key == self.none:
                    return r.p
                pname = self.pkg.name(key)
                prop = self.types.find(owner, pname) if owner else None
            else:
                if key == 0:
                    return r.p
                self.ref(at)
                path = self.pkg.ref_path(key)
                pname = path.rsplit('.', 1)[-1]
                prop = self.types.prop(path)
            info = r.u8()
            typ = info & 0x0F
            item = None
            if ver == 0 and typ == 10:
                self.name(r.p)
                item = self.pkg.name(r.i32())
            sz = {0x00: 1, 0x10: 2, 0x20: 4, 0x30: 12, 0x40: 16}.get(info & 0x70)
            if sz is None:
                sz = {0x50: r.u8, 0x60: r.u16, 0x70: r.i32}[info & 0x70]()
            if info & 0x80 and typ != 3:
                b = r.u8()
                if b & 0x80:
                    r.take(1 if b & 0xC0 == 0x80 else 3)
            if typ == 3:
                continue
            v = r.p
            r.take(sz)
            if typ in (5, 8):
                self.ref(v)
            elif typ == 6:
                self.name(v)
            elif typ == 7:
                self.ref(v)
                self.name(v + 4)
            elif typ == 9:
                self.array(v, v + sz, owner, pname, prop)
            elif typ == 10:
                spath = prop.struct if prop is not None and prop.struct else None
                sname = (item or (spath or '').rsplit('.', 1)[-1]).lower()
                if sname not in BINARY_STRUCTS:
                    if spath is None:
                        spath = self.types.struct_path(sname, owner)
                    end = self.tags(v, spath, v + sz)
                    if end != v + sz:
                        raise FormatError('%s: struct %s ends at %d of %d' % (owner, sname, end - v, sz))
            elif typ in (14, 15):
                raise FormatError('%s.%s: map and fixed-array tags are not walked' % (owner, pname))

    def array(self, pos, end, owner, pname, prop):
        if prop is not None and prop.inner is not None:
            kind, spath = prop.inner.kind, prop.inner.struct
        else:
            kind = NATIVE_ARRAYS.get(((owner or '').lower(), pname.lower()))
            spath = None
            if kind is None:
                raise FormatError('%s.%s: array of unknown elements' % (owner, pname))
        r = Reader(self.d, pos, end)
        n = r.count()
        for _ in range(n):
            if kind in ('ObjectProperty', 'ClassProperty'):
                self.ref(r.p)
                r.take(4)
            elif kind == 'NameProperty':
                self.name(r.p)
                r.take(4)
            elif kind == 'DelegateProperty':
                self.ref(r.p)
                self.name(r.p + 4)
                r.take(8)
            elif kind == 'StrProperty':
                ln = r.i32()
                r.take(ln if ln >= 0 else -2 * ln)
            elif kind in ELEMENT_SIZE:
                r.take(ELEMENT_SIZE[kind])
            elif kind == 'StructProperty':
                sname = (spath or '').rsplit('.', 1)[-1].lower()
                if sname in BINARY_SIZE:
                    r.take(BINARY_SIZE[sname])
                else:
                    r.p = self.tags(r.p, spath, end)
            else:
                raise FormatError('%s.%s: array of %s is not walked' % (owner, pname, kind))
        if r.p != end:
            raise FormatError('%s.%s: array ends at %d of %d' % (owner, pname, r.p - pos, end - pos))

    # -- an export
    def frame(self, i):
        """The state frame of export i: where its tags start."""
        r = Reader(self.d)
        if self.pkg.exports[i].flags & RF_HasStack:
            self.ref(r.p)
            node = r.i32()
            self.ref(r.p)
            r.i32()
            r.take(10)
            if node:
                r.i32()
        return r.p

    def export(self, i, owner):
        end = self.tags(self.frame(i), owner)
        cls = self.pkg.export_class(i)
        native = NATIVE.get(cls)
        if native is not None:
            native(self, Reader(self.d, end))
        elif end != len(self.d):
            raise FormatError('%s (%s): %d bytes after the tags and no native walker' % (
                self.pkg.export_path(i), cls, len(self.d) - end))
        return self.out


def class_path(pkg, i):
    """The path of export i's class (an import of a script package, or 'Core.Class')."""
    c = pkg.exports[i].cls
    return 'Core.Class' if c == 0 else pkg.ref_path(c)


def references(pkg, types, i):
    """(offset, 'ref' | 'name') of every object reference and name in export i's data."""
    e = pkg.exports[i]
    if not e.data:
        return []
    return Walker(pkg, types, e.data).export(i, class_path(pkg, i))


def tag_references(pkg, types, i):
    """references() of the state frame and the tagged properties only: what the Xbox format and the PC's write
    alike, so it also walks the disc's own packages."""
    e = pkg.exports[i]
    if not e.data:
        return []
    w = Walker(pkg, types, e.data)
    w.tags(w.frame(i), class_path(pkg, i))
    return w.out


# ---------------------------------------------------------------------------------------------------------------
# Native data (PC layouts; the converters' headers and Engine/Src have the serialisers)
# ---------------------------------------------------------------------------------------------------------------
def _refs(w, r, n=None):
    for _ in range(r.count() if n is None else n):
        w.ref(r.p)
        r.take(4)


def _names(w, r):
    for _ in range(r.count()):
        w.name(r.p)
        r.take(4)


def _skip(r, size):
    r.take(size * r.count())


def _bytes(r):
    r.take(r.count())


def _lazy_bytes(r):
    r.i32()
    r.take(r.count())


def _fstring(r):
    n = r.i32()
    r.take(n if n >= 0 else -2 * n)


def _done(r):
    if r.p != r.end:
        raise FormatError('native data: %d bytes left' % (r.end - r.p))


def _model(w, r):
    r.take(40)
    _skip(r, 12)                            # Vectors
    _skip(r, 12)                            # Points
    _skip(r, 92)                            # Nodes
    for _ in range(r.count()):              # Surfs: Material, PolyFlags, five indices, Actor, Plane, scale
        w.ref(r.p)
        r.take(28)
        w.ref(r.p)
        r.take(24)
    _skip(r, 8)                             # Verts
    r.i32()                                 # NumSharedSides
    _refs(w, r)                             # Zones (ZoneActor)
    _refs(w, r, 1)                          # Polys
    _skip(r, 24)                            # Bounds
    _skip(r, 4)                             # LeafHulls
    _skip(r, 8)                             # Leaves
    _refs(w, r)                             # Lights
    r.take(8)                               # RootOutside, Linked
    for _ in range(r.count()):              # Sections: vertices, Revision, Material, NumNodes, PolyFlags, texture
        _skip(r, 40)
        r.i32()
        _refs(w, r, 1)
        r.take(12)
    for _ in range(r.count()):              # LightMaps
        r.take(28 + 64 + 36)
        for _ in range(r.count()):          # Bitmaps: LightActor, Bits, sizes and bounds
            _refs(w, r, 1)
            _bytes(r)
            r.take(28)
        _refs(w, r, 1)                      # Level
        r.i32()
    for _ in range(r.count()):              # LightMapTextures: Level, LightMaps, cache id, Revision, StaticTexture
        _refs(w, r, 1)
        _skip(r, 4)
        r.take(12)
        _lazy_bytes(r)
        _lazy_bytes(r)
        r.take(13)
    _done(r)


def _polys(w, r):
    n = r.i32()
    r.i32()
    for _ in range(n):
        nv = r.i32()
        r.take(48 + 12 * nv + 4)            # Base, Normal, TextureU/V, vertices, PolyFlags
        _refs(w, r, 2)                      # Actor, Material
        w.name(r.p)                         # ItemName
        r.take(4 + 12)                      # iLink, iBrushPoly, LightMapScale
    _done(r)


def _level(w, r):
    n = r.i32()
    r.i32()
    _refs(w, r, n)                          # Actors
    for _ in range(4):                      # URL: Protocol, Host, Map, Portal, Op, Port, Valid
        _fstring(r)
    for _ in range(r.count()):
        _fstring(r)
    r.take(8)
    _refs(w, r, 1)                          # Model
    r.take(4)                               # ApproxTime
    _refs(w, r, 1)                          # FirstDeleted
    _refs(w, r, 16)                         # TextBlocks
    for _ in range(r.count()):              # TravelInfo
        _fstring(r)
        _fstring(r)
    _done(r)


def _static_mesh(w, r):
    r.take(40)
    _skip(r, 14)                            # Sections
    r.take(24 + 40 + 12)                    # BoundingBox, compression and strip flags, CompressionOffset
    for size in (24, 4, 4):                 # vertex, colour, alpha streams with their revisions
        _skip(r, size)
        r.i32()
    for _ in range(r.count()):              # UV streams
        _skip(r, 8)
        r.take(8)
    for _ in range(2):                      # IndexBuffer, WireframeIndexBuffer
        _skip(r, 2)
        r.i32()
    _refs(w, r, 1)                          # CollisionModel
    _skip(r, 32)                            # kDOP nodes
    _skip(r, 8)                             # kDOP triangles
    # RawTriangles (a lazy array: its end is a file offset), then InternalVersion and AuthenticationKey, the last
    # 8 bytes; none holds a reference
    r.p = r.end - 8
    r.take(8)
    _done(r)


def _static_mesh_instance(w, r):
    _skip(r, 4)
    r.i32()
    for _ in range(r.count()):              # Lights: LightActor, VisibilityBits, Applied
        _refs(w, r, 1)
        _bytes(r)
        r.take(4)
    _done(r)


def _texture(w, r):
    for _ in range(r.count()):              # Mips: lazy pixels, USize, VSize, UBits, VBits
        _lazy_bytes(r)
        r.take(10)
    _done(r)


def _palette(w, r):
    _skip(r, 4)
    _done(r)


def _font(w, r):
    _skip(r, 17)                            # Characters
    _refs(w, r)                             # Textures
    r.take(4)                               # Kerning
    _skip(r, 4)                             # CharRemap
    r.take(4)                               # IsRemapped
    _done(r)


def _skeletal_mesh(w, r):
    from uc2xskel import _parse_lod
    r.take(40)
    r.take(8)                               # InternalVersion, ModelVerts
    _skip(r, 4)                             # Verts
    _refs(w, r)                             # Materials
    r.take(36)
    _skip(r, 2)
    _skip(r, 8)
    _skip(r, 2)
    _skip(r, 10)
    _skip(r, 8)
    r.take(24 + 4)                          # LOD values, bImpostorPresent
    _refs(w, r, 1)                          # ImpostorProps.Material
    r.take(52 + 4)                          # the rest of ImpostorProps, SkinTesselationFactor
    _skip(r, 12)                            # Points
    for _ in range(r.count()):              # RefSkeleton: Name first
        w.name(r.p)
        r.take(60)
    _refs(w, r, 1)                          # DefaultAnim
    r.i32()                                 # SkeletalDepth
    for _ in range(r.count()):              # MultiBlends
        _skip(r, 2)
        r.i32()
    _skip(r, 4)                             # Weights
    _names(w, r)                            # TagAliases
    _names(w, r)                            # TagNames
    _skip(r, 48)                            # TagCoords
    r.i32()                                 # bIsTristripped
    for _ in range(r.count(64)):
        _parse_lod(r, 0, False)
    r.i32()                                 # DefaultRefMesh
    for size in (12, 10, 12, 8, 2, 2):      # Raw* lazy arrays
        r.i32()
        _skip(r, size)
    r.take(4)                               # AuthenticationKey
    for size in (20, 28):                   # bone collision spheres and boxes: BoneName first
        for _ in range(r.count()):
            w.name(r.p)
            r.take(size)
    _refs(w, r)                             # BoneCollisionBoxModels
    _done(r)


def _mesh_animation(w, r):
    from uc2xskel import _FLEX_ARRAYS
    r.i32()                                 # InternalVersion
    for _ in range(r.count()):              # RefBones: Name first
        w.name(r.p)
        r.take(25)
    r.take(r.u32())                         # the motion block
    moves = []
    for _ in range(r.count()):
        r.take(20)
        iv = r.i32()
        tracks = r.count()
        r.take(4 * tracks + 4)
        types = [r.i32() for _ in range(r.count())] if iv >= 3 else []
        moves.append((tracks, types))
    for tracks, types in moves:             # the byte count of each array in the block
        r.take(4 * (3 * tracks + 3 + sum(_FLEX_ARRAYS[t] for t in types)))
    for _ in range(r.count()):              # AnimSeqs
        r.take(4)                           # Bookmark
        w.name(r.p)
        r.take(4)
        _names(w, r)                        # Groups
        r.take(8)
        for _ in range(r.count()):          # Notifys: Time, Function, NotifyObject
            r.take(4)
            w.name(r.p)
            r.take(4)
            _refs(w, r, 1)
        r.take(4)                           # Rate
    _done(r)


def _terrain_info(w, r):
    _refs(w, r)                             # Sectors
    _skip(r, 12)
    r.take(8)
    _skip(r, 24)
    r.take(96 + 8)
    _skip(r, 4)
    _done(r)


def _terrain_sector(w, r):
    _refs(w, r, 1)                          # Info
    r.take(16 + 24)
    for _ in range(r.count()):              # LightInfos: LightActor, VisibilityBitmap
        _refs(w, r, 1)
        _bytes(r)
    _done(r)


def _terrain_primitive(w, r):
    r.take(40)
    _refs(w, r, 1)                          # Info
    _done(r)


NATIVE = {
    'Model': _model, 'Polys': _polys, 'Level': _level, 'StaticMesh': _static_mesh,
    'StaticMeshInstance': _static_mesh_instance, 'Texture': _texture, 'Cubemap': _texture, 'Palette': _palette,
    'Font': _font, 'SkeletalMesh': _skeletal_mesh, 'MeshAnimation': _mesh_animation, 'TerrainInfo': _terrain_info,
    'TerrainSector': _terrain_sector, 'TerrainPrimitive': _terrain_primitive,
}
