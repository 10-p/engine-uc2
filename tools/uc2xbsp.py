#!/usr/bin/env python3
"""BSP models of Unreal Championship 2's Xbox packages (UModel, licensee 1) as PC models (licensee 0), for
tools/cook-xbox-packages.py.

    import uc2xbsp
    data, lazy = uc2xbsp.convert(pkg, i, ctx)      # pkg: uc2xpkg.Package (Xbox), ctx.xpr: uc2xpr.XprSet

What changes (Engine/Src/UnModel.cpp, Engine/Inc/UnModel.h, Engine/Inc/UnObj.h):
  FBspNode    16-bit leaves, section, first vertex and light map on the Xbox (82 bytes), 32-bit on the PC (92);
              the 32-bit index fields keep the next field's bytes in their high half on the Xbox
  FBspSurf    the same size; base, normal, texture vectors and brush poly are 16-bit values in 32-bit fields
  sections    the vertices (FBspVertex: packed normal, 16-bit light map coordinates) are in '<model>_vs_<n>' of
              the map's _uma bundle; written as PC vertices (float normal and light map coordinates)
  LightMaps   left out on the Xbox. The PC rebuilds the level's sections from them when it loads the map
              (ULevel::PostLoad -> UModel::BuildRenderData: light map texture and coordinates per node), so one
              FLightMap per index the nodes use is rebuilt: its texture from the node's section, its world to light
              map transform fitted to the section vertices' light map coordinates (exact to the 16-bit packing),
              its size from their extent. The light visibility bitmaps are not on the disc (the game never
              recomposes BSP light maps: UPDATE_DYNAMIC_LIGHTMAPS is 0).
  LightMapTextures  the composed light maps (StaticTexture, first mip) are kept; the second mip is not on the
              disc; the textures' light map lists are rebuilt from the FLightMaps above.
"""
import math
import struct

from uc2xpkg import FormatError, Reader, object_head_end

LIGHTMAP_SIZE = 512.0
INDEX_NONE = -1


def _s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def _u16(v):
    return v & 0xFFFF


def unpack_normal(dw):
    """FPackedNormal: 11-bit X, 11-bit Y, 10-bit Z, signed, as written by FVector::Pack (Core/Src/UnMath.cpp)."""
    x = dw & 0x7FF
    y = (dw >> 11) & 0x7FF
    z = (dw >> 22) & 0x3FF
    x = (x - 0x800 if x & 0x400 else x) / 1023.0
    y = (y - 0x800 if y & 0x400 else y) / 1023.0
    z = (z - 0x400 if z & 0x200 else z) / 511.0
    return x, y, z


class Model:
    """A UModel's native data, in fields."""


def parse(data, flags, xbox, pkg=None):
    """Fields of a UModel export (xbox: licensee 1 layout)."""
    m = Model()
    m.head_end = object_head_end(data, flags, pkg)
    m.head = data[:m.head_end]
    r = Reader(data, m.head_end)
    m.bounds = r.take(40)
    m.vectors = r.take(12 * r.count())
    m.points = r.take(12 * r.count())
    m.nodes = []
    for _ in range(r.count()):
        n = {}
        n['plane'] = r.take(16)
        n['zonemask'] = r.take(8)
        n['flags'] = r.u8()
        n['vertpool'] = r.i32()
        if xbox:
            n['surf'] = _u16(r.i32())
            n['child'] = [_s16(r.i32()) for _ in range(3)]
            n['collision'] = r.i32()
            n['render'] = _s16(r.i32())
            n['sphere'] = r.take(16)
            n['zone'] = r.take(2)
            n['numverts'] = r.u8()
            n['leaf'] = [r.i16(), r.i16()]
            # unsigned on the Xbox: INDEX_NONE is 0xFFFF
            n['section'] = r.u16()
            n['firstvert'] = r.u16()
            n['lightmap'] = r.i16()
            for k in ('section', 'firstvert'):
                if n[k] == 0xFFFF:
                    n[k] = INDEX_NONE
        else:
            n['surf'] = r.i32()
            n['child'] = [r.i32() for _ in range(3)]
            n['collision'] = r.i32()
            n['render'] = r.i32()
            n['sphere'] = r.take(16)
            n['zone'] = r.take(2)
            n['numverts'] = r.u8()
            n['leaf'] = [r.i32(), r.i32()]
            n['section'] = r.i32()
            n['firstvert'] = r.i32()
            n['lightmap'] = r.i32()
        m.nodes.append(n)
    m.surfs = []
    for _ in range(r.count()):
        s = {'material': r.i32(), 'polyflags': r.u32()}
        vals = [r.i32() for _ in range(5)]
        s['idx'] = [_s16(v) for v in vals] if xbox else vals
        s['actor'] = r.i32()
        s['plane'] = r.take(16)
        s['lmscale'] = r.take(4)
        m.surfs.append(s)
    m.verts = r.take(8 * r.count())
    m.shared_sides = r.i32()
    nz = r.i32()
    m.zones = [r.i32() for _ in range(nz)]
    m.polys = r.i32()
    m.boxes = r.take(24 * r.count())
    m.leaf_hulls = r.take(4 * r.count())
    m.leaves = r.take(8 * r.count())
    m.lights = r.take(4 * r.count())
    m.root_outside = r.i32()
    m.linked = r.i32()
    m.sections = []
    for _ in range(r.count()):
        s = {}
        if xbox:
            s['in_xpr'] = r.i32()
            s['verts'] = None
            if not s['in_xpr']:
                s['verts'] = [_xbox_bsp_vertex(r.take(28)) for _ in range(r.count())]
        else:
            s['in_xpr'] = 0
            s['verts'] = [r.take(40) for _ in range(r.count())]
        s['revision'] = r.i32()
        s['material'] = r.i32()
        s['numnodes'] = r.i32()
        s['polyflags'] = r.u32()
        s['lmtexture'] = r.i32()
        m.sections.append(s)
    m.lightmaps_raw = []
    nlm = r.count()
    if xbox and nlm:
        raise FormatError('Xbox model with light maps')
    for _ in range(nlm):
        m.lightmaps_raw.append(_skip_pc_lightmap(r))
    m.lmtextures = []
    for _ in range(r.count()):
        t = {'level': r.i32()}
        t['lightmaps'] = [r.i32() for _ in range(r.count())]
        t['cacheid'] = r.take(8)
        t['revision'] = r.i32()
        # FStaticLightMapTexture: two lazy byte arrays (absolute end offsets), format, size, revision
        t['data'] = []
        for _ in range(2):
            r.i32()
            t['data'].append(r.take(r.count()))
        t['format'] = r.u8()
        t['size'] = r.take(8)
        t['tex_revision'] = r.i32()
        m.lmtextures.append(t)
    if r.p != len(data):
        raise FormatError('UModel: %d bytes left' % (len(data) - r.p))
    return m


def _xbox_bsp_vertex(b):
    """FBspVertex as serialised on the Xbox: Position, U, V, U2 and V2 as 16-bit /32767, packed normal."""
    px, py, pz, u, v, u2, v2, nrm = struct.unpack('<5f2hI', b)
    return (px, py, pz, u, v, u2 / 32767.0, v2 / 32767.0, nrm)


def _xbox_bsp_vertex_mem(b):
    """FBspVertex as the bundles hold it, the Xbox struct in memory: Position, packed normal, U, V, U2, V2."""
    px, py, pz, nrm, u, v, u2, v2 = struct.unpack('<3fI2f2h', b)
    return (px, py, pz, u, v, u2 / 32767.0, v2 / 32767.0, nrm)


def _skip_pc_lightmap(r):
    start = r.p
    r.take(28 + 64 + 36)
    for _ in range(r.count()):
        r.i32()
        r.take(r.count())
        r.take(4 * 7)
    r.take(8)
    return r.d[start:r.p]


def _pc_bsp_vertex(v):
    px, py, pz, u, vv, u2, v2, nrm = v
    nx, ny, nz = unpack_normal(nrm)
    return struct.pack('<5f2f3f', px, py, pz, u, vv, u2, v2, nx, ny, nz)


def _plane_basis(normal):
    """Two unit vectors spanning the plane with this normal."""
    nx, ny, nz = normal
    ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    nx, ny, nz = nx / ln, ny / ln, nz / ln
    ax = (1.0, 0.0, 0.0) if abs(nx) < 0.9 else (0.0, 1.0, 0.0)
    e1 = (ny * ax[2] - nz * ax[1], nz * ax[0] - nx * ax[2], nx * ax[1] - ny * ax[0])
    l1 = math.sqrt(sum(x * x for x in e1))
    e1 = tuple(x / l1 for x in e1)
    e2 = (ny * e1[2] - nz * e1[1], nz * e1[0] - nx * e1[2], nx * e1[1] - ny * e1[0])
    return e1, e2


def _fit_lightmap(points, normal):
    """World to light map transform of one surface from its vertices' (position, (u2, v2)): per axis
    u = a.p + b with a in the surface plane, least squares (minimum norm when the vertices are collinear), in
    texels."""
    e1, e2 = _plane_basis(normal)
    n = len(points)
    cx = [sum(p[k] for p, _ in points) / n for k in range(3)]
    st = []
    for p, _ in points:
        q = [p[k] - cx[k] for k in range(3)]
        st.append((sum(q[k] * e1[k] for k in range(3)), sum(q[k] * e2[k] for k in range(3))))
    sss = sum(x * x for x, _ in st)
    stt = sum(y * y for _, y in st)
    sst = sum(x * y for x, y in st)
    rows = []
    for axis in range(2):
        tgt = [uv[axis] * LIGHTMAP_SIZE for _, uv in points]
        mean = sum(tgt) / n
        sy = sum(x * (y - mean) for (x, _), y in zip(st, tgt))
        ty = sum(t * (y - mean) for (_, t), y in zip(st, tgt))
        det = sss * stt - sst * sst
        if det > 1e-12 * max(sss * stt, 1e-30):
            ka = (sy * stt - ty * sst) / det
            kb = (ty * sss - sy * sst) / det
        else:
            # collinear vertices: the least-squares fit along the line they lie on
            angle = 0.5 * math.atan2(2.0 * sst, sss - stt)
            c, d = math.cos(angle), math.sin(angle)
            tt = sum((x * c + y * d) ** 2 for x, y in st)
            k = (sy * c + ty * d) / tt if tt else 0.0
            ka, kb = k * c, k * d
        vec = [ka * e1[k] + kb * e2[k] for k in range(3)]
        off = mean - sum(vec[k] * cx[k] for k in range(3))
        rows.append((vec, off))
    return rows


def _lightmaps(m, section_verts):
    """FLightMap records for the light map indices the nodes use, fitted to the section vertices."""
    by_lm = {}
    for ni, n in enumerate(m.nodes):
        lm = n['lightmap']
        if lm == INDEX_NONE or n['numverts'] == 0:
            continue
        verts = section_verts.get(n['section'])
        if verts is None:
            continue
        rec = by_lm.setdefault(lm, {'node': n, 'pts': [], 'tex': m.sections[n['section']]['lmtexture']})
        for v in verts[n['firstvert']:n['firstvert'] + n['numverts']]:
            rec['pts'].append(((v[0], v[1], v[2]), (v[5], v[6])))
    count = max(by_lm) + 1 if by_lm else 0
    for n in m.nodes:
        if n['lightmap'] != INDEX_NONE:
            count = max(count, n['lightmap'] + 1)
    out = []
    for lm in range(count):
        rec = by_lm.get(lm)
        if rec is None or len(rec['pts']) < 3:
            out.append(_pc_lightmap(INDEX_NONE, 0, 0, 0, 0, 1, 1, ([0.0] * 3, 0.0), ([0.0] * 3, 0.0), m.level))
            continue
        (ru, ou), (rv, ov) = _fit_lightmap(rec['pts'], struct.unpack('<3f', rec['node']['plane'][:12]))
        us = [p[1][0] * LIGHTMAP_SIZE for p in rec['pts']]
        vs = [p[1][1] * LIGHTMAP_SIZE for p in rec['pts']]
        x0, y0 = int(math.floor(min(us))), int(math.floor(min(vs)))
        sx = max(1, int(math.ceil(max(us))) - x0)
        sy = max(1, int(math.ceil(max(vs))) - y0)
        node = rec['node']
        out.append(_pc_lightmap(rec['tex'], node['surf'], node['zone'][0], x0, y0, sx, sy,
                                (ru, ou - x0), (rv, ov - y0), m.level))
    return out


def _pc_lightmap(tex, surf, zone, ox, oy, sx, sy, row_u, row_v, level):
    """FLightMap<< (PC): indices, WorldToLightMap (FMatrix, row-vector convention: column j holds output j),
    LightMapBase/X/Y, no bitmaps, Level, Revision."""
    (ux, uy, uz), uo = row_u
    (vx, vy, vz), vo = row_v
    matrix = [ux, vx, 0.0, 0.0,
              uy, vy, 0.0, 0.0,
              uz, vz, 0.0, 0.0,
              uo, vo, 0.0, 1.0]
    b = struct.pack('<7i', tex, surf, zone, ox, oy, sx, sy)
    b += struct.pack('<16f', *matrix)
    b += struct.pack('<9f', *([0.0] * 9))
    b += struct.pack('<i', 0)                   # Bitmaps
    b += struct.pack('<ii', level, 0)           # Level, Revision
    return b


def _write_pc(m, lightmaps, section_verts):
    out = bytearray(m.head)
    lazy = []
    out += m.bounds
    out += struct.pack('<i', len(m.vectors) // 12) + m.vectors
    out += struct.pack('<i', len(m.points) // 12) + m.points
    out += struct.pack('<i', len(m.nodes))
    for n in m.nodes:
        out += n['plane'] + n['zonemask'] + struct.pack('<B', n['flags'])
        out += struct.pack('<5i', n['vertpool'], n['surf'], *n['child'])
        out += struct.pack('<ii', n['collision'], n['render'])
        out += n['sphere'] + n['zone'] + struct.pack('<B', n['numverts'])
        out += struct.pack('<5i', n['leaf'][0], n['leaf'][1], n['section'], n['firstvert'], n['lightmap'])
    out += struct.pack('<i', len(m.surfs))
    for s in m.surfs:
        out += struct.pack('<iI5ii', s['material'], s['polyflags'], *s['idx'], s['actor'])
        out += s['plane'] + s['lmscale']
    out += struct.pack('<i', len(m.verts) // 8) + m.verts
    out += struct.pack('<ii', m.shared_sides, len(m.zones))
    out += struct.pack('<%di' % len(m.zones), *m.zones)
    out += struct.pack('<i', m.polys)
    out += struct.pack('<i', len(m.boxes) // 24) + m.boxes
    out += struct.pack('<i', len(m.leaf_hulls) // 4) + m.leaf_hulls
    out += struct.pack('<i', len(m.leaves) // 8) + m.leaves
    out += struct.pack('<i', len(m.lights) // 4) + m.lights
    out += struct.pack('<ii', m.root_outside, m.linked)
    out += struct.pack('<i', len(m.sections))
    for si, s in enumerate(m.sections):
        verts = section_verts.get(si) or []
        out += struct.pack('<i', len(verts))
        for v in verts:
            out += _pc_bsp_vertex(v)
        out += struct.pack('<iiiIi', s['revision'], s['material'], s['numnodes'], s['polyflags'], s['lmtexture'])
    out += struct.pack('<i', len(lightmaps))
    for lm in lightmaps:
        out += lm
    children = {}
    for li, lm in enumerate(lightmaps):
        tex = struct.unpack_from('<i', lm, 0)[0]
        if tex != INDEX_NONE:
            children.setdefault(tex, []).append(li)
    out += struct.pack('<i', len(m.lmtextures))
    for ti, t in enumerate(m.lmtextures):
        kids = children.get(ti, [])
        out += struct.pack('<ii', t['level'], len(kids)) + struct.pack('<%di' % len(kids), *kids)
        out += t['cacheid'] + struct.pack('<i', t['revision'])
        for d in t['data']:
            lazy.append(len(out))
            out += struct.pack('<ii', len(out) + 8 + len(d), len(d)) + d
        out += struct.pack('<B', t['format']) + t['size'] + struct.pack('<i', t['tex_revision'])
    return bytes(out), lazy


def section_vertices(pkg, i, m, ctx):
    """The Xbox model's section vertices: inline, or from '<model>_vs_<n>' in the bundles."""
    name = pkg.name(pkg.exports[i].name)
    found = {}
    for si, s in enumerate(m.sections):
        if s['verts'] is not None:
            found[si] = s['verts']
            continue
        res = '%s_vs_%d' % (name, si)
        b = ctx.xpr.find(res, ctx.prefer)
        if b is None:
            ctx.warn('%s: no vertices for section %d (%s)' % (pkg.export_path(i), si, res))
            continue
        cnt, raw = b.uma(res, 28)
        found[si] = [_xbox_bsp_vertex_mem(raw[k:k + 28]) for k in range(0, len(raw), 28)]
    return found


def convert(pkg, i, ctx):
    """PC export data for Xbox UModel export i, with the offsets of its lazy arrays' end fields."""
    e = pkg.exports[i]
    m = parse(e.data, e.flags, True, pkg)
    m.level = _level_ref(pkg)
    verts = section_vertices(pkg, i, m, ctx)
    lightmaps = _lightmaps(m, verts) if m.lmtextures else []
    return _write_pc(m, lightmaps, verts)


def _level_ref(pkg):
    for k, ex in enumerate(pkg.exports):
        if pkg.export_class(k) == 'Level':
            return k + 1
    return 0
