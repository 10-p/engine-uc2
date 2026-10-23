#!/usr/bin/env python3
"""Skeletal meshes and animations of Unreal Championship 2's disc (Xbox format, licensee 1) converted to the PC format
(licensee 0) of the engine-uc2 port. Used by the package converter through uc2xpkg; stdlib only.

    import uc2xskel
    data, seekpos = uc2xskel.convert(pkg, i, ctx)   # SkeletalMesh: PC export data and its lazy-array SeekPos fields
    data = uc2xskel.convert(pkg, i, ctx)            # MeshAnimation: PC export data
    n = uc2xskel.lin_size(lin, pos, export, pkg)    # stream bytes of a .LIN SkeletalMesh export (uc2xpkg.LIN_WALKERS)
    xdata = uc2xskel.lin_expand(e.lin, e, pkg)      # that stream back to Xbox export data, lazy arrays empty
                                                    # (convert does this itself for an export with only e.lin)

The SeekPos of each lazy array in the returned data is the offset, from the start of the export data, just after the
array; the package writer adds the export's file offset to the i32 at each offset in seekpos (add_offset does it).
ctx: .xpr (uc2xpr.XprSet over the disc's XboxTextures), .prefer (bundle basenames searched first), .warn(msg);
optional .ukx (the disc's XboxAnimations folder, by default the one beside XboxTextures) and .stats (a
collections.Counter that gets one count per LOD naming where its data came from). Missing lazy-array contents (a
.LIN, a mesh merged into a map) are detected from the data, so ctx.lin is not needed.

Native data after the tagged properties (Engine/Src/UnSkeletalMesh.cpp, UnLodMesh.cpp, UnPrim.cpp; Inc/UnSkeletalMesh.h;
i32 array counts, version 151):
  UPrimitive      BoundingBox (24), BoundingSphere (16)
  ULodMesh        InternalVersion, ModelVerts, Verts, Materials, Scale, Origin, RotOrigin, FaceLevel, Faces,
                  CollapseWedgeThus, Wedges, MeshMaterials, MeshScaleMax, LODHysteresis, LODStrength, LODMinVerts,
                  LODMorph, LODZDisplace, bImpostorPresent and ImpostorProps (version 3), SkinTesselationFactor (4)
  USkeletalMesh   Points, RefSkeleton (60 bytes a bone), DefaultAnim, SkeletalDepth, MultiBlends, Weights, TagAliases,
                  TagNames, TagCoords, bIsTristripped, LODModels, DefaultRefMesh, lazy RawVerts, RawWedges, RawFaces,
                  RawInfluences, RawCollapseWedges, RawFaceLevel, AuthenticationKey, BoneCollisionSpheres,
                  BoneCollisionBoxes, BoneCollisionBoxModels
  FStaticLODModel SkinningStream, SmoothVerts, SmoothStreamWedges, SmoothSections, RigidSections, SmoothIndexBuffer,
                  RigidIndexBuffer, RigidVertexStream, lazy Influences, Wedges, Faces, Points, DisplayFactor,
                  LODHysteresis, DupVertCount, MaxInfluences, bUniqueSubset, bUseSmoothing, CompressionScale,
                  CompressionInvScale, CompressionOffset, GPUMultiBoneSections, GPUIndexBuffer, GPUVertexStream,
                  GPUBoneChunks
  lazy array      i32 SeekPos (file offset just after the array), then the array
The two formats differ only in the GPU vertex stream: the disc's vertices (FSkinGPUVertex under _XBOX, pack(1), 22
bytes: 3 SWORD position over the LOD's compression box, FPackedNormal, two SWORD UVs, FColor bone indices and weights)
follow a UBOOL bIsInXPR, and with it set they are in '<mesh>_vs_<lod>' of a *_uma.xpr. But the Xbox conversion
emptied SkinningStream, SmoothVerts, SmoothSections and SmoothIndexBuffer of every LOD with GPU sections, and the PC
skins on the CPU from exactly those: they are rebuilt from the LOD's lazy arrays (CreateSkinningStream), and the GPU
vertices from the same data (ConstructGPUStream), else unpacked from the disc. Meshes merged into maps (cla<N>) also
lost their lazy arrays and GPU indices: the same mesh in a disc .ukx supplies them, else the LOD is derived from the
disc's GPU vertices and push buffers.
  UMeshAnimation  InternalVersion, RefBones (25 bytes), FRawMotionDataBlock (u32 size; licensee 1: UBOOL bIsInXPR; the
                  bytes, unless in '<set>_anim' of a *_a_uma.xpr), Moves (MotionChunk heads), the byte count of each
                  array in the block, AnimSeqs; FMeshAnimNotify is Time, Function (licensee 0 only), NotifyObject
"""
import math
import os
import struct
from array import array

import uc2xpkg
from uc2xpkg import FormatError, Package, Reader, object_head_end, skip_stack

_F = struct.Struct('<f')
_PACKED = struct.Struct('<hhhIhh4B4B')      # FSkinGPUVertex, Xbox, as serialised
_PC_VERT = struct.Struct('<16f')            # FSkinGPUVertex, PC: Position, Normal, U, V, Index[4], Weight[4]
EMPTY = (0, b'')
GPUBONES_MAX = 43                           # MaxBonesPerChunk the editor built the GPU streams with


def f32(x):
    """x rounded to single precision."""
    return _F.unpack(_F.pack(x))[0]


def _round(x):
    """appRound on the PC: floor(x + 0.5)."""
    return int(math.floor(x + 0.5))


def _arr(r, size):
    n = r.count()
    return n, r.take(n * size)


def _put(out, a):
    out += struct.pack('<i', a[0])
    out += a[1]


def _decode(a, fmt):
    return list(struct.iter_unpack(fmt, a[1])) if a is not None and a[0] else []


def _count(ctx, key):
    stats = getattr(ctx, 'stats', None)
    if stats is not None:
        stats[key] += 1


def _prefer(ctx):
    return getattr(ctx, 'prefer', None) or ()


# ---------------------------------------------------------------------------------------------------------------
# USkeletalMesh layout
# ---------------------------------------------------------------------------------------------------------------
class Lod:
    """One FStaticLODModel: arrays as (count, element bytes); a lazy array is None when only its SeekPos is there."""


class Mesh:
    """pre: the bytes before LODModels, the same in both formats; lods; refmesh (DefaultRefMesh); raw: the six lazy
    Raw* arrays; post: AuthenticationKey and the collision arrays; bones, bones_at: RefSkeleton's count and offset."""


def _lazy(r, size, lin):
    r.i32()
    return None if lin else _arr(r, size)


def _parse_native(data, head, licensee, lin):
    r = Reader(data, head)
    m = Mesh()
    r.take(40)                              # UPrimitive
    iv = r.i32()
    if iv < 4:
        raise FormatError('SkeletalMesh internal version %d' % iv)
    r.i32()                                 # ModelVerts
    r.take(4 * r.count())                   # Verts
    r.take(4 * r.count())                   # Materials
    r.take(36)                              # Scale, Origin, RotOrigin
    r.take(2 * r.count())                   # FaceLevel
    r.take(8 * r.count())                   # Faces
    r.take(2 * r.count())                   # CollapseWedgeThus
    r.take(10 * r.count())                  # Wedges
    r.take(8 * r.count())                   # MeshMaterials
    r.take(24 + 60 + 4)                     # LOD values, impostor, SkinTesselationFactor
    r.take(12 * r.count())                  # Points
    m.bones_at = r.p
    m.bones = r.count()
    r.take(60 * m.bones)                    # RefSkeleton: name, flags, VJointPos, NumChildren, ParentIndex
    r.take(8)                               # DefaultAnim, SkeletalDepth
    for _ in range(r.count()):              # MultiBlends: point indices, weight base
        r.take(2 * r.count())
        r.i32()
    r.take(4 * r.count())                   # Weights
    r.take(4 * r.count())                   # TagAliases
    r.take(4 * r.count())                   # TagNames
    r.take(48 * r.count())                  # TagCoords
    r.take(4)                               # bIsTristripped
    m.pre = bytes(data[:r.p])
    m.lods = [_parse_lod(r, licensee, lin) for _ in range(r.count(64))]
    m.refmesh = r.i32()
    m.raw = [_lazy(r, size, lin) for size in (12, 10, 12, 8, 2, 2)]
    start = r.p
    r.take(4)                               # AuthenticationKey
    r.take(20 * r.count())                  # BoneCollisionSpheres
    r.take(28 * r.count())                  # BoneCollisionBoxes
    r.take(4 * r.count())                   # BoneCollisionBoxModels
    m.post = bytes(data[start:r.p])
    m.end = r.p
    return m


def _parse_lod(r, licensee, lin):
    l = Lod()
    l.skin = _arr(r, 4)
    l.smooth_verts = _arr(r, 16)            # FSkinPoint: point, FMeshNorm
    l.ssw = r.i32()
    l.smooth_sec = _arr(r, 18)              # FSkelMeshSection: nine WORDs (TotalWedges is not serialised)
    l.rigid_sec = _arr(r, 18)
    l.smooth_ib = _arr(r, 2)
    l.smooth_ib_rev = r.i32()
    l.rigid_ib = _arr(r, 2)
    l.rigid_ib_rev = r.i32()
    l.rigid_vs_head = r.take(12)            # Revision, bPartial, bStreamCallback
    l.rigid_verts = _arr(r, 32)
    l.infl = _lazy(r, 8, lin)               # FVertInfluence: weight, vertex, bone
    l.wedges = _lazy(r, 10, lin)            # FMeshWedge: vertex, U, V
    l.faces = _lazy(r, 8, lin)              # FMeshFace: three wedges, material
    l.points = _lazy(r, 12, lin)
    l.misc = r.take(24)                     # DisplayFactor .. bUseSmoothing
    l.comp = r.take(36)                     # CompressionScale, CompressionInvScale, CompressionOffset
    l.gpu_sec = _arr(r, 18)
    l.gpu_ib = _arr(r, 2)
    l.gpu_ib_rev = r.i32()
    l.gpu_vs_head = r.take(12)
    l.gpu_in_xpr = False
    if licensee == 1:
        l.gpu_in_xpr = bool(r.u32())
        l.gpu_verts = None if l.gpu_in_xpr else _arr(r, 22)
    else:
        l.gpu_verts = _arr(r, 64)
    start = r.p
    for _ in range(r.count()):              # GPUBoneChunks: FirstBoneSlot, ChunkBoneIndices
        r.take(2)
        r.take(2 * r.count())
    l.chunks = bytes(r.d[start:r.p])
    return l


def parse_mesh(data, flags, licensee, pkg=None, lin=False):
    """A SkeletalMesh export's data; lin: lazy arrays reduced to their SeekPos, as in a .LIN stream. pkg tells the
    tag terminator (uc2xpkg.object_head_end)."""
    return _parse_native(data, object_head_end(data, flags, pkg), licensee, lin)


def write_mesh(m, licensee=0):
    """(export data, offsets of the SeekPos fields); each SeekPos relative to the start of the data."""
    out = bytearray(m.pre)
    seeks = []

    def lazy(a):
        at = len(out)
        out.extend(b'\0\0\0\0')
        _put(out, a if a is not None else EMPTY)
        struct.pack_into('<i', out, at, len(out))
        seeks.append(at)

    out += struct.pack('<i', len(m.lods))
    for l in m.lods:
        _put(out, l.skin)
        _put(out, l.smooth_verts)
        out += struct.pack('<i', l.ssw)
        _put(out, l.smooth_sec)
        _put(out, l.rigid_sec)
        _put(out, l.smooth_ib)
        out += struct.pack('<i', l.smooth_ib_rev)
        _put(out, l.rigid_ib)
        out += struct.pack('<i', l.rigid_ib_rev)
        out += l.rigid_vs_head
        _put(out, l.rigid_verts)
        for a in (l.infl, l.wedges, l.faces, l.points):
            lazy(a)
        out += l.misc + l.comp
        _put(out, l.gpu_sec)
        _put(out, l.gpu_ib)
        out += struct.pack('<i', l.gpu_ib_rev)
        out += l.gpu_vs_head
        if licensee == 1:
            out += struct.pack('<I', 1 if l.gpu_in_xpr else 0)
            if not l.gpu_in_xpr:
                _put(out, l.gpu_verts)
        else:
            _put(out, l.gpu_verts if l.gpu_verts is not None else EMPTY)
        out += l.chunks
    out += struct.pack('<i', m.refmesh)
    for a in m.raw:
        lazy(a)
    out += m.post
    return bytes(out), seeks


def add_offset(data, seekpos, offset):
    """data with offset added to the i32 at each offset in seekpos."""
    out = bytearray(data)
    for at in seekpos:
        struct.pack_into('<i', out, at, struct.unpack_from('<i', out, at)[0] + offset)
    return bytes(out)


def seekpos_fields(data, flags, pkg=None):
    """Offsets of the SeekPos fields in PC (licensee 0) SkeletalMesh data."""
    return write_mesh(parse_mesh(data, flags, 0, pkg), 0)[1]


# ---------------------------------------------------------------------------------------------------------------
# .LIN streams: the Xbox loader read each lazy array's SeekPos and seeked past the contents (GLazyLoad forced on)
# ---------------------------------------------------------------------------------------------------------------
def _tag_ends(data, flags):
    """Where the tags may end when the name 'None' is unknown: after a null property reference (tags by reference),
    or after any by-name tag header, which may be the terminator."""
    r = Reader(data)
    skip_stack(r, flags)
    try:
        while True:
            ver, key = r.u8(), r.i32()
            if ver != 0 and key == 0:
                yield r.p
                return
            if ver == 0:
                yield r.p
            info = r.u8()
            if ver == 0 and info & 0x0F == 10:
                r.i32()
            size = {0x00: 1, 0x10: 2, 0x20: 4, 0x30: 12, 0x40: 16}.get(info & 0x70)
            if size is None:
                size = {0x50: r.u8, 0x60: r.u16, 0x70: r.i32}[info & 0x70]()
            if info & 0x80 and info & 0x0F != 3:
                b = r.u8()
                if b & 0x80:
                    r.take(1 if b & 0xC0 == 0x80 else 3)
            if info & 0x0F != 3:
                r.take(size)
    except FormatError:
        return


def _lin_parse(stream, export, pkg):
    """The stream parsed as a .LIN SkeletalMesh; its skipped bytes, from each SeekPos, must add up to SerialSize."""
    heads = [object_head_end(stream, export.flags, pkg)] if pkg is not None else _tag_ends(stream, export.flags)
    for head in heads:
        try:
            m = _parse_native(stream, head, 1, True)
        except FormatError:
            continue
        # write_mesh puts an empty array's count after each SeekPos: field k sits 4 * k bytes earlier in the stream
        skipped = 0
        for k, at in enumerate(write_mesh(m, 1)[1]):
            at -= 4 * k
            skipped += struct.unpack_from('<i', stream, at)[0] - (export.offset + at + 4 + skipped)
        if m.end + skipped == export.size:
            return m
    raise FormatError('no SkeletalMesh stream of SerialSize %d' % export.size)


def lin_size(data, pos, export, pkg=None):
    """Bytes a SkeletalMesh export occupies at pos of a .LIN stream: SerialSize less the lazy-array contents."""
    return _lin_parse(bytes(data[pos:pos + export.size]), export, pkg).end


def lin_expand(stream, export, pkg=None):
    """Xbox export data of a .LIN SkeletalMesh stream: each lazy array put back empty, its SeekPos just after it as if
    the export sat at its SerialOffset (for the meshes merged into maps, the original data: their arrays were empty)."""
    data, seeks = write_mesh(_lin_parse(bytes(stream), export, pkg), 1)
    return add_offset(data, seeks, export.offset)


# ---------------------------------------------------------------------------------------------------------------
# CPU skinning data and GPU stream from a LOD's lazy arrays (Engine/Src/UnSkeletalTools.cpp)
# ---------------------------------------------------------------------------------------------------------------
def _face_normal(p0, p1, p2):
    """(p0 - p1) ^ (p2 - p0) at the x87's double precision, stored as single."""
    a = (p0[0] - p1[0], p0[1] - p1[1], p0[2] - p1[2])
    b = (p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2])
    return f32(a[1] * b[2] - a[2] * b[1]), f32(a[2] * b[0] - a[0] * b[2]), f32(a[0] * b[1] - a[1] * b[0])


def smooth_normals(points, wedges, faces):
    """RecalculateSmoothNormals: area-weighted face normals summed per point, packed 10:10:10 around 511 (FMeshNorm)."""
    acc = [[0.0, 0.0, 0.0] for _ in points]
    for f in faces:
        v0, v1, v2 = wedges[f[0]][0], wedges[f[1]][0], wedges[f[2]][0]
        n = _face_normal(points[v0], points[v1], points[v2])
        for v in (v0, v1, v2):
            a = acc[v]
            a[0], a[1], a[2] = f32(a[0] + n[0]), f32(a[1] + n[1]), f32(a[2] + n[2])
    out = []
    for x, y, z in acc:
        rs = 1.0 / math.sqrt(x * x + y * y + z * z + f32(0.0001))
        x, y, z = f32(x * rs), f32(y * rs), f32(z * rs)
        out.append((_round(511.0 + x * 511.0) & 0x3FF) | (_round(511.0 + y * 511.0) & 0x3FF) << 10 |
                   (_round(511.0 + z * 511.0) & 0x3FF) << 20)
    return out


def skinning_stream(l, nbones):
    """CreateSkinningStream over a LOD's Points, Wedges, Faces and Influences: the decoded inputs, SkinningStream,
    SmoothVerts and SmoothIndexBuffer as (count, bytes), the sections, DupVertCount and SmoothStreamWedges."""
    points = _decode(l.points, '<fff')
    wedges = _decode(l.wedges, '<Hff')
    wedge_uv = _decode(l.wedges, '<HII')
    faces = _decode(l.faces, '<HHHH')
    infl = _decode(l.infl, '<fHH')
    normals = smooth_normals(points, wedges, faces) if faces else [0] * len(points)
    verts = b''.join(struct.pack('<fffI', p[0], p[1], p[2], n) for p, n in zip(points, normals))

    # wedges sharing a point: the first stores the skinned point in the duplicate buffer, the others fetch it
    uses = [0] * len(points)
    marker = [0] * len(wedges)
    slot = [0] * len(points)
    for w, wd in enumerate(wedges):
        uses[wd[0]] += 1
        if uses[wd[0]] == 1:
            marker[w] = -1
    dupes = 0
    for w, wd in enumerate(wedges):
        v = wd[0]
        if uses[v] > 1:
            if marker[w] == -1:
                marker[w] = 0
                dupes += 1
                slot[v] = dupes
            elif marker[w] == 0:
                marker[w] = slot[v]

    stream = array('I')
    at = 0
    for w, wd in enumerate(wedges):
        v = wd[0]
        if marker[w] > 0:
            stream.append((marker[w] - 1) * 6 | 0xF0000000)
        else:
            end = at
            while end < len(infl) and infl[end][1] == v:
                end += 1
            count = min(end - at, 7)
            bits = (count - 1) << 28 if count else 0
            if marker[w] == 0 and count:
                bits |= 0x80000000
            for k in range(count):
                weight, _, bone = infl[at + k]
                stream.append(bits | (int(weight * 65535.0) & 0xFFFF) << 12 | (bone if bone < nbones else 0) * 6)
                bits = 0
            if count == 0:
                stream.append(65535 << 12)
            at = end
        stream.append(wedge_uv[w][1])
        stream.append(wedge_uv[w][2])
    stream.append(0xFFFFFFFF)

    indices = array('H')
    sections = []
    for fi, f in enumerate(faces):
        indices.extend(f[:3])
        if not sections or f[3] != sections[-1]['mat']:
            sections.append({'mat': f[3], 'first_face': fi, 'faces': 0, 'min': len(wedges), 'max': 0})
        s = sections[-1]
        s['faces'] += 1
        s['min'] = min(s['min'], f[0], f[1], f[2])
        s['max'] = max(s['max'], f[0], f[1], f[2])
    return dict(points=points, wedges=wedges, faces=faces, infl=infl, normals=normals, sections=sections,
                skin=(len(stream), stream.tobytes()), verts=(len(points), verts),
                indices=(len(indices), indices.tobytes()), dupes=dupes, ssw=len(wedges))


def smooth_sections(sk):
    """SmoothSections as (count, bytes). The editor leaves FirstIndex, MaxInfluences and ShaderIndex uninitialised
    (the renderer reads none of them): written as 3 x FirstFace, 0 and 0."""
    out = bytearray()
    for s in sk['sections']:
        out += struct.pack('<9H', s['mat'], 3 * s['first_face'] & 0xFFFF, s['min'], s['max'], s['max'] - s['min'] + 1,
                           0, 0, s['first_face'], s['faces'])
    return len(sk['sections']), bytes(out)


def gpu_stream(nbones, sk, normalize=True, max_bones=GPUBONES_MAX):
    """ConstructGPUStream and AddNewGPUStreamSection over skinning_stream's result: the PC GPU vertices as (count,
    bytes), and the sections (before tristripping), bone chunks and triangles they come with. normalize: weights
    scaled to sum 1, as the editor did for most LODs."""
    points, wedges, faces, infl, normals = sk['points'], sk['wedges'], sk['faces'], sk['infl'], sk['normals']
    runs = {}
    for k, (_, v, _) in enumerate(infl):
        run = runs.setdefault(v, [k, 0])
        if run[0] + run[1] == k:
            run[1] += 1
    verts, sections, chunks, tris = [], [], [], []

    def add(chunk_faces, subset, mat):
        remap, chunk_bones = {}, []
        chunk_wedges, at, local = [], {}, []
        for f in chunk_faces:
            t = []
            for wi in faces[f][:3]:
                if wi not in at:
                    at[wi] = len(chunk_wedges)
                    chunk_wedges.append(wi)
                t.append(at[wi])
            local.append(t)
        done = {}
        first = len(verts)
        for wi in chunk_wedges:
            pi, u, v = wedges[wi]
            if pi in done:
                # the point is skinned already: a copy with this wedge's UV
                nv = list(verts[done[pi]])
                nv[6], nv[7] = u, v
                verts.append(nv)
                continue
            start, n = runs.get(pi, (0, 0))
            ws = [(infl[k][0], infl[k][2] if infl[k][2] < nbones else 0) for k in range(start, start + n)]
            idx, wt = [0.0] * 4, [0.0] * 4
            for k in range(min(4, len(ws))):
                wt[k], idx[k] = ws[k][0], float(ws[k][1] * 3)
                if subset:
                    if ws[k][1] not in chunk_bones:
                        chunk_bones.append(ws[k][1])
                    remap[ws[k][1]] = chunk_bones.index(ws[k][1])
            total = f32(wt[0] + wt[1] + wt[2] + wt[3])
            if total > 0 and normalize:
                wt = [f32(x / total) for x in wt]
            nm, p = normals[pi], points[pi]
            done[pi] = len(verts)
            verts.append([p[0], p[1], p[2], float((nm & 1023) - 511), float((nm >> 10 & 1023) - 511),
                          float((nm >> 20 & 1023) - 511), u, v] + idx + wt)
        if subset:
            for g in range(first, len(verts)):
                for k in range(8, 12):
                    verts[g][k] = 3.0 * remap.get(_round(verts[g][k] / 3.0), 0)
        chunks.append(chunk_bones if subset else [])
        ids = [k + first for t in local for k in t]
        tris.extend(tuple(ids[k:k + 3]) for k in range(0, len(ids), 3))
        sections.append({'mat': mat, 'first_face': len(tris) - len(local), 'faces': len(local),
                         'min': min(ids) if ids else 0xFFFF, 'max': max(ids) if ids else 0, 'verts': len(chunk_wedges)})

    if nbones <= max_bones:
        for s in sk['sections']:
            add(range(s['first_face'], s['first_face'] + s['faces']), False, s['mat'])
    else:
        point_bones = [[] for _ in points]
        for _, v, b in infl:
            point_bones[v].append(b)
        for s in sk['sections']:
            ff, nf = s['first_face'], s['faces']
            required, face_bones = [], []
            for f in range(ff, ff + nf):
                fb = []
                for wi in faces[f][:3]:
                    for b in point_bones[wedges[wi][0]]:
                        if b not in required:
                            required.append(b)
                        if b not in fb:
                            fb.append(b)
                face_bones.append(fb)
            if len(required) <= max_bones:
                add(range(ff, ff + nf), True, s['mat'])
                continue
            # greedy split; the first face of each part goes in by its section-relative index, as in the editor
            rest = list(range(nf))
            while rest:
                part, combined = [rest.pop(0)], []
                at, eligible = 0, len(rest)
                while eligible > 0:
                    cur = rest[at]
                    eligible -= 1
                    trial = combined + [b for b in face_bones[cur] if b not in combined]
                    if len(trial) <= max_bones:
                        combined = trial
                        part.append(ff + cur)
                        rest.remove(cur)
                    else:
                        at += 1
                add(part, True, s['mat'])
    return dict(verts=(len(verts), b''.join(_PC_VERT.pack(*v) for v in verts)), sections=sections, chunks=chunks,
                tris=tris)


# ---------------------------------------------------------------------------------------------------------------
# The disc's packed GPU vertices
# ---------------------------------------------------------------------------------------------------------------
def _unpack_normal(dw):
    """FVector::Unpack of an FPackedNormal: X, Y signed 11 bits over 1023, Z signed 10 bits over 511."""
    x = (dw & 0x7FF) - (dw << 1 & 0x800)
    y = (dw >> 11 & 0x7FF) - (dw >> 10 & 0x800)
    z = (dw >> 22 & 0x3FF) - (dw >> 21 & 0x400)
    return x * f32(1.0 / 1023.0), y * f32(1.0 / 1023.0), z * f32(1.0 / 511.0)


def unpack_gpu(raw, count, comp, memory_order):
    """Packed GPU vertices as (position, unit normal, U, V, bone index bytes, weight bytes); memory_order: FColor bytes
    as laid out in memory (B, G, R, A, an XPR dump) rather than as serialised (R, G, B, A)."""
    sx, sy, sz, _, _, _, ox, oy, oz = struct.unpack('<9f', comp)
    k = f32(1.0 / 32767.0)
    out = []
    for x, y, z, nrm, u, v, i0, i1, i2, i3, w0, w1, w2, w3 in _PACKED.iter_unpack(raw[:22 * count]):
        if memory_order:
            i0, i2, w0, w2 = i2, i0, w2, w0
        pos = f32(f32(f32(x * k) * sx) + ox), f32(f32(f32(y * k) * sy) + oy), f32(f32(f32(z * k) * sz) + oz)
        out.append((pos, _unpack_normal(nrm), f32(u * k), f32(v * k), (i0, i1, i2, i3), (w0, w1, w2, w3)))
    return out


def pc_from_packed(verts):
    """PC GPU vertices from unpacked ones: the normal as the integer vector of the 10-bit smooth normal it came from,
    bone indices as stored (3 x bone), weights over 255."""
    out = bytearray()
    for pos, n, u, v, idx, wt in verts:
        out += _PC_VERT.pack(pos[0], pos[1], pos[2], float(_round(n[0] * 511.0)), float(_round(n[1] * 511.0)),
                             float(_round(n[2] * 511.0)), u, v, float(idx[0]), float(idx[1]), float(idx[2]),
                             float(idx[3]), wt[0] / 255.0, wt[1] / 255.0, wt[2] / 255.0, wt[3] / 255.0)
    return len(verts), bytes(out)


def _weight_misses(pc, packed):
    """Vertices whose weights do not pack (FColor(FPlane): floor of 255 x) to the disc's bytes."""
    n = 0
    for v, p in zip(_PC_VERT.iter_unpack(pc[1]), packed):
        n += any(int(math.floor(f32(v[12 + k] * 255.0))) != p[5][k] for k in range(4))
    return n


def push_buffer_words(bundle, name):
    """A D3DPushBuffer's command words. Its header is Common, Size, AllocationSize, Data; Data is relative to the
    header when Common has bit 31 (system memory), else an offset into video memory."""
    common, size, _, data = struct.unpack_from('<IIII', bundle.header(name, 16))
    if common & 0x80000000:
        off = bundle.tags[name.lower()] + data
        raw = bundle.sysmem[off:off + size]
    else:
        raw = bundle.vidmem[data:data + size]
    return struct.unpack('<%dI' % (len(raw) // 4), raw[:len(raw) // 4 * 4])


def push_buffer_indices(words):
    """The draws in NV2A push-buffer words as [(primitive, [indices])]."""
    draws, cur, prim = [], None, 0
    i = 0
    while i < len(words):
        w = words[i]
        count, method = w >> 18 & 0x7FF, w & 0x1FFC
        args = words[i + 1:i + 1 + count]
        i += 1 + count
        if method == 0x17FC and args:                   # SET_BEGIN_END
            if args[0]:
                prim, cur = args[0], []
            elif cur is not None:
                draws.append((prim, cur))
                cur = None
        elif method == 0x1800 and cur is not None:     # ARRAY_ELEMENT16: two indices a word
            for a in args:
                cur.extend((a & 0xFFFF, a >> 16))
        elif method == 0x1808 and cur is not None:     # ARRAY_ELEMENT32
            cur.extend(args)
    return draws


def strip_triangles(ib, first, count):
    """The count triangles of the strip at ib[first:], degenerate ones dropped, each in front-face order."""
    out = []
    for k in range(count):
        a, b, c = ib[first + k], ib[first + k + 1], ib[first + k + 2]
        if a != b and b != c and a != c:
            out.append((a, b, c) if k % 2 == 0 else (b, a, c))
    return out


def _section_triangles(ib, s, tristripped):
    if tristripped:
        return strip_triangles(ib, s[1], s[8])
    return [tuple(ib[s[1] + 3 * k:s[1] + 3 * k + 3]) for k in range(s[8])]


def _canon(t):
    k = t.index(min(t))
    return t[k:] + t[:k]


def _chunk_bytes(chunks):
    out = bytearray(struct.pack('<i', len(chunks)))
    for c in chunks:
        out += struct.pack('<Hi', 0, len(c)) + struct.pack('<%dH' % len(c), *c)
    return bytes(out)


def _chunk_lists(data):
    r = Reader(data)
    out = []
    for _ in range(r.count()):
        r.take(2)
        n = r.count()
        out.append(struct.unpack('<%dH' % n, r.take(2 * n)))
    return out


# ---------------------------------------------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------------------------------------------
def convert(pkg, i, ctx):
    """PC export data of export i of the Xbox package pkg: (data, SeekPos offsets) for a SkeletalMesh, bytes for a
    MeshAnimation. The other classes in the disc's .ukx packages (AnimNotify_Script, AnimNotify_Trigger) have no
    native data."""
    cls = pkg.export_class(i)
    if cls == 'SkeletalMesh':
        return convert_mesh(pkg, i, ctx)
    if cls == 'MeshAnimation':
        return convert_anim(pkg, i, ctx)
    raise FormatError('uc2xskel: no conversion for class %s' % cls)


def _has_geometry(l):
    return l is not None and l.wedges is not None and l.wedges[0] > 0 and l.points[0] > 0


def _tristripped(m):
    return struct.unpack_from('<i', m.pre, len(m.pre) - 4)[0]


def _disc_vertices(ctx, name, li, l):
    """The packed GPU vertices of LOD li, inline or from '<mesh>_vs_<lod>' in the bundles; None when absent."""
    if not l.gpu_in_xpr:
        if l.gpu_verts is None or not l.gpu_verts[0]:
            return None
        return unpack_gpu(l.gpu_verts[1], l.gpu_verts[0], l.comp, False)
    key = '%s_vs_%d' % (name, li)
    b = ctx.xpr.find(key, prefer=_prefer(ctx))
    if b is None:
        return None
    n, raw = b.uma(key, 22)
    return unpack_gpu(raw, n, l.comp, True)


def convert_mesh(pkg, i, ctx):
    e = pkg.exports[i]
    name = pkg.name(e.name)
    data = e.data
    if not data and getattr(e, 'lin', None):
        data = lin_expand(e.lin, e, pkg)
    m = parse_mesh(data, e.flags, 1, pkg)
    if m.end != len(data):
        raise FormatError('SkeletalMesh %s: %d bytes left' % (pkg.export_path(i), len(data) - m.end))
    src = None
    if any(l.gpu_sec[0] and not _has_geometry(l) for l in m.lods):
        src = counterpart(pkg, m, ctx, pkg.export_path(i))
        if src is not None:
            m.raw = src.raw
    for li, l in enumerate(m.lods):
        sl = src.lods[li] if src is not None else None
        geo = l if _has_geometry(l) else sl if _has_geometry(sl) else None
        packed = None
        if l.gpu_sec[0]:
            packed = _disc_vertices(ctx, name, li, l)
            if packed is None and sl is not None:
                packed = _disc_vertices(ctx, src.name, li, sl)
        if geo is None:
            if l.gpu_sec[0]:
                _lod_from_gpu(m, li, l, packed, name, ctx)
            else:
                _count(ctx, 'lod smooth kept')
            continue
        if not l.gpu_ib[0] and sl is not None:
            l.gpu_ib = sl.gpu_ib
        l.infl, l.wedges, l.faces, l.points = geo.infl, geo.wedges, geo.faces, geo.points
        sk = skinning_stream(geo, m.bones)
        if l.smooth_sec[0] or l.skin[0]:
            _count(ctx, 'lod smooth kept')
        else:
            _count(ctx, 'lod smooth rebuilt from %s' % ('its lazy arrays' if geo is l else 'the .ukx counterpart'))
            l.skin, l.smooth_verts, l.smooth_ib = sk['skin'], sk['verts'], sk['indices']
            l.smooth_sec = smooth_sections(sk)
            if sk['ssw'] != l.ssw or sk['dupes'] != struct.unpack_from('<i', l.misc, 8)[0]:
                ctx.warn('%s LOD %d: rebuilt skinning stream disagrees with SmoothStreamWedges or DupVertCount' % (
                    name, li))
        if l.gpu_sec[0]:
            l.gpu_verts = _gpu_vertices(m, li, l, sk, packed, name, ctx)
        elif l.gpu_verts is None:
            l.gpu_verts = EMPTY
    return write_mesh(m, 0)


def _gpu_matches(l, g, tristripped):
    """Whether rebuilt GPU data is what the LOD's GPU sections, bone chunks and index buffer were made from."""
    secs = list(struct.iter_unpack('<9H', l.gpu_sec[1]))
    if len(secs) != len(g['sections']) or _chunk_bytes(g['chunks']) != l.chunks:
        return False
    for s, q in zip(secs, g['sections']):
        if (s[0], s[2], s[3], s[4], s[7]) != (q['mat'], q['min'], q['max'], q['verts'], q['first_face']):
            return False
    if not l.gpu_ib[0]:
        return True
    ib = struct.unpack('<%dH' % l.gpu_ib[0], l.gpu_ib[1])
    stored = {_canon(t) for s in secs for t in _section_triangles(ib, s, tristripped)}
    return stored == {_canon(t) for t in g['tris'] if len(set(t)) == 3}


def _gpu_vertices(m, li, l, sk, packed, name, ctx):
    """PC GPU vertices of a LOD: rebuilt when that reproduces its GPU sections (weights normalised or as built,
    whichever the disc's packed weights show), else unpacked from the disc, else rebuilt with new sections."""
    g = gpu_stream(m.bones, sk)
    if _gpu_matches(l, g, _tristripped(m)):
        if packed is not None and len(packed) == g['verts'][0]:
            raw = gpu_stream(m.bones, sk, normalize=False)
            if _weight_misses(raw['verts'], packed) < _weight_misses(g['verts'], packed):
                _count(ctx, 'lod gpu rebuilt, weights not normalised as on the disc')
                return raw['verts']
            _count(ctx, 'lod gpu rebuilt, checked against the disc')
        else:
            _count(ctx, 'lod gpu rebuilt, not on the disc')
        return g['verts']
    if packed is not None:
        _count(ctx, 'lod gpu unpacked from the disc')
        return pc_from_packed(packed)
    # sections made from other geometry, vertices not on the disc: new sections, chunks and a triangle list
    _count(ctx, 'lod gpu rebuilt with new sections')
    ctx.warn('%s LOD %d: GPU vertices not on the disc and the GPU sections do not fit the geometry: sections, bone '
             'chunks and indices rebuilt' % (name, li))
    secs = bytearray()
    for q in g['sections']:
        secs += struct.pack('<9H', q['mat'], 3 * q['first_face'] & 0xFFFF, q['min'], q['max'], q['verts'], 4, q['mat'],
                            q['first_face'], q['faces'])
    l.gpu_sec = (len(g['sections']), bytes(secs))
    l.chunks = _chunk_bytes(g['chunks'])
    ids = [k for t in g['tris'] for k in t]
    l.gpu_ib = (len(ids), struct.pack('<%dH' % len(ids), *ids))
    return g['verts']


def _geometry_from_gpu(l, packed, ib, tristripped):
    """LOD lazy arrays from packed GPU vertices and their index lists: points are the vertices of equal position,
    normal and influences, wedges add the UV, the real bones come from the section's bone chunk."""
    chunks = _chunk_lists(l.chunks)
    point_of, wedge_of, vert_wedge = {}, {}, {}
    points, wedges, infl, faces = [], [], [], []
    for si, s in enumerate(struct.iter_unpack('<9H', l.gpu_sec[1])):
        bones = chunks[si] if si < len(chunks) else ()
        for k in range(s[2], min(s[3] + 1, len(packed))):
            pos, nrm, u, v, idx, wt = packed[k]
            real = tuple(bones[i // 3] if i // 3 < len(bones) else i // 3 for i in idx)
            key = (pos, nrm, real, wt)
            if key not in point_of:
                point_of[key] = len(points)
                points.append(pos)
                infl.extend((w / 255.0, point_of[key], b) for b, w in zip(real, wt) if w)
            wk = (point_of[key], u, v)
            if wk not in wedge_of:
                wedge_of[wk] = len(wedges)
                wedges.append(wk)
            vert_wedge[k] = wedge_of[wk]
        for t in _section_triangles(ib, s, tristripped):
            if all(k in vert_wedge for k in t):
                faces.append((vert_wedge[t[0]], vert_wedge[t[1]], vert_wedge[t[2]], s[0]))
    g = Lod()
    g.infl = (len(infl), b''.join(struct.pack('<fHH', *x) for x in infl))
    g.wedges = (len(wedges), b''.join(struct.pack('<Hff', *x) for x in wedges))
    g.faces = (len(faces), b''.join(struct.pack('<4H', *x) for x in faces))
    g.points = (len(points), b''.join(struct.pack('<3f', *x) for x in points))
    return g


def _lod_from_gpu(m, li, l, packed, name, ctx):
    """A LOD left with only its GPU sections: lazy arrays, smooth data and GPU stream from the disc's GPU vertices
    and push buffers ('<mesh>_<lod>_<section>_pb')."""
    if packed is None:
        _count(ctx, 'lod lost')
        ctx.warn('%s LOD %d: no geometry and no GPU vertices on the disc: LOD left without smooth data' % (name, li))
        l.gpu_verts = EMPTY
        return
    tristripped = _tristripped(m)
    if l.gpu_ib[0]:
        ib = struct.unpack('<%dH' % l.gpu_ib[0], l.gpu_ib[1])
    else:
        ib = []
        for si, s in enumerate(struct.iter_unpack('<9H', l.gpu_sec[1])):
            key = '%s_%d_%d_pb' % (name, li, si)
            b = ctx.xpr.find(key, prefer=_prefer(ctx))
            idx = [k for _, d in push_buffer_indices(push_buffer_words(b, key)) for k in d] if b is not None else []
            if len(ib) != s[1] or len(idx) != (s[8] + 2 if tristripped else 3 * s[8]):
                ctx.warn('%s LOD %d section %d: push buffer does not match the section' % (name, li, si))
            ib.extend(idx)
        l.gpu_ib = (len(ib), struct.pack('<%dH' % len(ib), *ib))
    geo = _geometry_from_gpu(l, packed, ib, tristripped)
    l.infl, l.wedges, l.faces, l.points = geo.infl, geo.wedges, geo.faces, geo.points
    sk = skinning_stream(geo, m.bones)
    l.skin, l.smooth_verts, l.smooth_ib = sk['skin'], sk['verts'], sk['indices']
    l.smooth_sec = smooth_sections(sk)
    l.ssw = sk['ssw']
    l.misc = l.misc[:8] + struct.pack('<i', sk['dupes']) + l.misc[12:]
    l.gpu_verts = pc_from_packed(packed)
    _count(ctx, 'lod smooth derived from the disc GPU vertices')
    _count(ctx, 'lod gpu unpacked from the disc')
    ctx.warn('%s LOD %d: geometry derived from the packed GPU vertices' % (name, li))


# ---------------------------------------------------------------------------------------------------------------
# The same mesh in a disc .ukx (maps hold copies of package meshes, renamed cla<N>)
# ---------------------------------------------------------------------------------------------------------------
_INDEX = {}
_LOADED = {}


def _ukx_folder(ctx):
    return getattr(ctx, 'ukx', None) or uc2xpkg.find_ci(os.path.dirname(os.path.abspath(ctx.xpr.folder)),
                                                         'XboxAnimations')


def fingerprint(pkg, m):
    """What identifies a skeletal mesh across packages: its bones (names, flags, reference pose, links) and per LOD
    SmoothStreamWedges, the LOD values with DupVertCount, the compression box, the GPU sections and bone chunks."""
    r = Reader(m.pre, m.bones_at)
    bones = []
    for _ in range(r.count()):
        b = r.take(60)
        bones.append((pkg.name(struct.unpack_from('<i', b)[0]).lower(), b[4:]))
    return tuple(bones), tuple((l.ssw, l.misc, l.comp, l.gpu_sec, l.chunks) for l in m.lods)


def _index(folder):
    """Fingerprints of the meshes with lazy arrays in the .ukx packages of folder: [(fingerprint, path, export)]."""
    if folder not in _INDEX:
        entries = []
        for path in (uc2xpkg.files_in(folder, '', '.ukx') if folder else []):
            p = Package.load(path)
            for i, e in enumerate(p.exports):
                if p.export_class(i) == 'SkeletalMesh':
                    m = parse_mesh(e.data, e.flags, p.licensee, p)
                    if all(_has_geometry(l) for l in m.lods):
                        entries.append((fingerprint(p, m), path, i))
        _INDEX[folder] = entries
    return _INDEX[folder]


def counterpart(pkg, m, ctx, label=''):
    """The same mesh in a disc .ukx, parsed, with .name and .path (package.mesh); None when there is none."""
    fp = fingerprint(pkg, m)
    hits = [(path, i) for f, path, i in _index(_ukx_folder(ctx)) if f == fp]
    if not hits:
        return None
    path, i = hits[0]
    if path not in _LOADED:
        _LOADED.clear()
        _LOADED[path] = Package.load(path)
    p = _LOADED[path]
    e = p.exports[i]
    src = parse_mesh(e.data, e.flags, p.licensee, p)
    src.name = p.name(e.name)
    src.path = '%s.%s' % (os.path.splitext(os.path.basename(path))[0], p.export_path(i))
    if len(hits) > 1:
        ctx.warn('%s: %d meshes match in the .ukx packages, using %s' % (label, len(hits), src.path))
    _count(ctx, 'mesh completed from %s' % src.path)
    return src


# ---------------------------------------------------------------------------------------------------------------
# UMeshAnimation
# ---------------------------------------------------------------------------------------------------------------
_FLEX_ARRAYS = {0: 0, 1: 2, 2: 3, 3: 3, 4: 3, 5: 2, 6: 2, 7: 1}     # arrays a FlexTrack type keeps in the block


def parse_anim(data, flags, licensee, pkg=None):
    """Where a MeshAnimation's parts are: size_at (the block size), block_at, size, in_xpr, moves_at, seqs_at, seqs
    ([(offset of the notifies, count)]), end."""
    r = Reader(data, object_head_end(data, flags, pkg))
    a = {}
    r.i32()                                 # InternalVersion
    r.take(25 * r.count())                  # RefBones: name, flags, parent, position, translation method
    a['size_at'] = r.p
    a['size'] = r.u32()
    a['in_xpr'] = bool(r.u32()) if licensee == 1 else False
    a['block_at'] = r.p
    if not a['in_xpr']:
        r.take(a['size'])
    a['moves_at'] = r.p
    moves = []
    for _ in range(r.count()):
        r.take(20)                          # RootSpeed3D, TrackTime, StartBone
        iv = r.i32()
        tracks = r.count()
        r.take(4 * tracks + 4)              # AnimTracks and RootTrack: their Flags
        types = [r.i32() for _ in range(r.count())] if iv >= 3 else []
        if iv == 3:
            raise FormatError('MeshAnimation with version 3 motion chunks')
        moves.append((tracks, types))
    total = 0
    for tracks, types in moves:             # FArchiveRawAnimBlock: the byte count of each array in the block
        for _ in range(3 * tracks + 3 + sum(_FLEX_ARRAYS[t] for t in types)):
            total += r.u32()
    if total != a['size']:
        raise FormatError('MeshAnimation: motion arrays of %d bytes, block of %d' % (total, a['size']))
    a['seqs_at'] = r.p
    seqs = []
    for _ in range(r.count()):
        r.take(8)                           # Bookmark, Name
        r.take(4 * r.count())               # Groups
        r.take(8)                           # StartFrame, NumFrames
        n = r.count()
        seqs.append((r.p, n))
        r.take(n * (8 if licensee == 1 else 12))
        r.take(4)                           # Rate
    a['seqs'] = seqs
    a['end'] = r.p
    return a


def convert_anim(pkg, i, ctx):
    e = pkg.exports[i]
    data = e.data
    a = parse_anim(data, e.flags, 1, pkg)
    if a['end'] != len(data):
        raise FormatError('MeshAnimation %s: %d bytes left' % (pkg.export_path(i), len(data) - a['end']))
    if a['in_xpr']:
        key = '%s_anim' % pkg.name(e.name)
        b = ctx.xpr.find(key, prefer=_prefer(ctx))
        if b is None:
            raise FormatError('MeshAnimation %s: %s is in no bundle' % (pkg.export_path(i), key))
        size, block = b.uma(key, 1)
        if size != a['size']:
            raise FormatError('MeshAnimation %s: %s holds %d bytes, the block %d' % (
                pkg.export_path(i), key, size, a['size']))
        _count(ctx, 'anim block from the bundles')
    else:
        block = data[a['block_at']:a['block_at'] + a['size']]
        _count(ctx, 'anim block inline')
    out = bytearray(data[:a['size_at']])
    out += struct.pack('<I', a['size'])
    out += block
    # each notify gets back its Function name, None
    none = pkg.add_name('None') if any(n for _, n in a['seqs']) else 0
    pos = a['moves_at']
    for at, n in a['seqs']:
        out += data[pos:at]
        for k in range(n):
            t, obj = struct.unpack_from('<fi', data, at + 8 * k)
            out += struct.pack('<fii', t, none, obj)
        pos = at + 8 * n
    out += data[pos:]
    return bytes(out)
