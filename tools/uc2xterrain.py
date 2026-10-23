#!/usr/bin/env python3
"""Terrain of Unreal Championship 2's Xbox maps (ATerrainInfo, UTerrainSector, UTerrainSectorBlock, licensee 1) as
PC terrain (licensee 0), for tools/cook-xbox-packages.py.

    import uc2xterrain
    pixels = uc2xterrain.heightmaps(pkg)          # {texture export path, lower case: G16 bytes}, before the textures
    data = uc2xterrain.convert(pkg, i, ctx)       # None: the export is left out

What the Xbox keeps (Engine/Src/UnTerrain.cpp, Engine/Inc/UnTerrain.h):
  TerrainInfo      the PC fields with Vertices, FaceNormals and VertexColors empty, then its own render data
                   (DecoLayerData, Primitive, RenderCombinations, CollapsedLayers, the quad bitmaps, SectorBlocks)
  TerrainSector    the PC fields, then RenderPasses, BlockIndex and VertexStreamOffset
  TerrainSectorBlock  Xbox only: OffsetX/Y, SizeX/Y, WorldMat, Sectors, TerrainInfo and the vertex stream of its
                   sectors (FTerrainVertex: Height as heightmap - 32768, OffsetXY from the block's corner, packed
                   normal, colour)
  layer weight maps  RGBA8 textures the Xbox saved inside the LevelInfo (ATerrainInfo::PrecomputeLayerWeights
                   makes them at load, outer the LevelInfo); the PC makes its own, and a saved one would be cleared
                   with its mips not loaded: left out
  The heightmap textures (G16) were never bundled. The PC builds its terrain at load from the heightmap
  (ATerrainInfo::UpdateVertices), so the heightmaps are rebuilt from the sector blocks' vertices, exactly (every
  heightmap point is a sector vertex; points two sectors share agree), and so are VertexColors. The PC rebuilds
  Vertices, FaceNormals and the render data itself; the blocks are left out.
"""
import struct

from uc2xpkg import FormatError, Reader, object_head_end
from uc2xtex import read_tags

CLASSES = ('TerrainInfo', 'TerrainSector', 'TerrainSectorBlock')


def _info_fields(r):
    """ATerrainInfo native data up to and with VertexColors: (end offset, values)."""
    sectors = r.take(4 * r.count())
    r.take(12 * r.count())                      # Vertices
    sx, sy = r.i32(), r.i32()
    r.take(24 * r.count())                      # FaceNormals (FTerrainNormalPair)
    to_world, to_heightmap = r.take(48), r.take(48)
    hx, hy = r.i32(), r.i32()
    colors = r.take(4 * r.count())
    return {'sectors': sectors, 'sx': sx, 'sy': sy, 'to_world': to_world, 'to_heightmap': to_heightmap,
            'hx': hx, 'hy': hy, 'colors': colors}


def _sector_fields(r):
    """UTerrainSector native data on the Xbox: the PC part (Info, quads, offsets, box, light infos) and its tail."""
    start = r.p
    info = r.i32()
    qx, qy, ox, oy = r.i32(), r.i32(), r.i32(), r.i32()
    r.take(24)
    for _ in range(r.count()):
        r.i32()
        r.take(r.count())
    pc_end = r.p
    for _ in range(r.count()):                  # RenderPasses
        r.i32()
        r.take(2 * r.count())
        r.take(16)
    block = r.i32()
    first = r.u16()
    return {'start': start, 'pc_end': pc_end, 'info': info, 'qx': qx, 'qy': qy, 'ox': ox, 'oy': oy,
            'block': block, 'first': first}


def _block_fields(r):
    ox, oy, sx, sy = r.i32(), r.i32(), r.i32(), r.i32()
    r.take(64)
    sectors = [r.i32() for _ in range(r.count())]
    info = r.i32()
    n = r.count()
    verts = r.take(12 * n)
    return {'ox': ox, 'oy': oy, 'sx': sx, 'sy': sy, 'sectors': sectors, 'info': info, 'verts': verts}


def _native(pkg, i):
    e = pkg.exports[i]
    return Reader(e.data, object_head_end(e.data, e.flags, pkg))


def rebuild(pkg):
    """Per TerrainInfo export index: its heightmap (HeightmapX x HeightmapY, G16) and vertex colours, rebuilt from
    its sector blocks' vertices."""
    blocks = {}
    for i, e in enumerate(pkg.exports):
        if e.data and pkg.export_class(i) == 'TerrainSectorBlock':
            blocks[i + 1] = _block_fields(_native(pkg, i))
    out = {}
    for i, e in enumerate(pkg.exports):
        if not e.data or pkg.export_class(i) != 'TerrainInfo':
            continue
        info = _info_fields(_native(pkg, i))
        hx, hy = info['hx'], info['hy']
        heights = [None] * (hx * hy)
        colors = [None] * (hx * hy)
        for ref, b in blocks.items():
            if b['info'] != i + 1:
                continue
            for sref in b['sectors']:
                if sref <= 0 or not pkg.exports[sref - 1].data:
                    continue
                s = _sector_fields(_native(pkg, sref - 1))
                for y in range(s['qy'] + 1):
                    for x in range(s['qx'] + 1):
                        k = s['first'] + x + y * (s['qx'] + 1)
                        h, bx, by, _n, col = struct.unpack_from('<hBBI4s', b['verts'], 12 * k)
                        gx, gy = x + s['ox'], y + s['oy']
                        if (bx, by) != ((gx - b['ox']) & 0xFF, (gy - b['oy']) & 0xFF):
                            raise FormatError('%s: sector vertex not where its block says' % pkg.export_path(sref - 1))
                        g = gx + gy * hx
                        v = h + 32768
                        if heights[g] is not None and heights[g] != v:
                            raise FormatError('%s: sectors disagree on the height at %d,%d' % (
                                pkg.export_path(i), gx, gy))
                        heights[g], colors[g] = v, col
        missing = sum(1 for v in heights if v is None)
        out[i] = {'hx': hx, 'hy': hy, 'missing': missing,
                  'heights': struct.pack('<%dH' % len(heights), *[v or 0 for v in heights]),
                  'colors': b''.join(c or b'\xff\xff\xff\xff' for c in colors)}
    return out


def weight_maps(pkg):
    """Export indices of the layer weight maps the Xbox saved: Textures whose outer is a LevelInfo."""
    out = []
    for i, e in enumerate(pkg.exports):
        if e.pkg > 0 and pkg.export_class(i) == 'Texture' and pkg.export_class(e.pkg - 1) == 'LevelInfo':
            out.append(i)
    return out


def heightmaps(pkg, terrain=None):
    """{export path of each TerrainInfo's TerrainMap, lower case: its G16 pixels} for uc2xtex's ctx.pixels."""
    terrain = rebuild(pkg) if terrain is None else terrain
    pixels = {}
    for i, t in terrain.items():
        e = pkg.exports[i]
        props, _ = read_tags(pkg, e.data, e.flags)
        ref = props.get(('TerrainMap', 0))
        if isinstance(ref, int) and ref > 0:
            pixels[pkg.export_path(ref - 1).lower()] = t['heights']
    return pixels


def convert(pkg, i, ctx):
    """PC export data for Xbox terrain export i; None for a sector block (Xbox only, left out)."""
    e = pkg.exports[i]
    cls = pkg.export_class(i)
    head = object_head_end(e.data, e.flags, pkg)
    if cls == 'TerrainSectorBlock':
        return None
    if cls == 'TerrainSector':
        s = _sector_fields(Reader(e.data, head))
        return e.data[:s['pc_end']]
    if cls != 'TerrainInfo':
        raise FormatError('%s: not terrain' % cls)
    terrain = getattr(ctx, 'terrain', None)
    if terrain is None:
        terrain = ctx.terrain = rebuild(pkg)
    t = terrain[i]
    if t['missing']:
        ctx.warn('%s: %d heightmap points no sector holds (written 0)' % (pkg.export_path(i), t['missing']))
    f = _info_fields(Reader(e.data, head))
    out = bytearray(e.data[:head])
    out += struct.pack('<i', len(f['sectors']) // 4) + f['sectors']
    out += struct.pack('<i', 0)                                        # Vertices: built at load
    out += struct.pack('<ii', f['sx'], f['sy'])
    out += struct.pack('<i', 0)                                        # FaceNormals: built at load
    out += f['to_world'] + f['to_heightmap']
    out += struct.pack('<ii', f['hx'], f['hy'])
    out += struct.pack('<i', len(t['colors']) // 4) + t['colors']
    return bytes(out)
