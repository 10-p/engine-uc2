#!/usr/bin/env python3
"""xbox-packages-check.py: compare the packages tools/cook-xbox-packages.py made from the Xbox disc with Epic's PC
packages of the same content, and the disc's .LIN cut with Epic's Xbox maps. Prints one line per check.

    tools/dev/xbox-packages-check.py --game <cooked game folder> --epic <uc2-avaris-2226> [--disc <disc folder>]
                                     [--only tables,lin,bsp,terrain,textures,meshes,anims,used]
                                     [--maps DM-Legacy,DM-Azure]

Checks (docs/porting/analysis/xbox-packages.md has the numbers of a full run):
  tables    each package against Epic's PC package of the same path: the GUID and generations identical, the
            names, imports and exports of the PC tables in the same order (export flags but the load flags), what
            was added after them, and the exports whose data is byte-identical, per class
  lin       (needs --disc) each XboxMaps/<Map>.LIN cut by uc2xpkg.read_lin against Epic's XboxMaps/<Map>.unr: the
            runtime exports byte-identical, the skeletal mesh streams expanded back (uc2xskel.lin_expand)
  bsp       each map's level model against Epic's PC map: geometry (vectors, points, nodes, surface indices,
            vertex pool, bounds, leaves) identical, light map textures identical, the largest light map coordinate
            error of the rebuilt light maps, in texels
  terrain   each terrain's heightmap and vertex colours against Epic's PC map
  textures  each texture of Textures/*.utx with a PC original: top mip identical, or a placeholder
  meshes    each static mesh of StaticMeshes/*.usx with a PC original of package version 151: colours, UVs,
            indices identical, the largest position error in quantisation steps
  anims     each animation of Animations/*.ukx with a PC original of version 151: motion block identical
  used      the textures, static meshes and Fonts the cook wrote without data (its report's placeholders, meshes
            without geometry, Fonts left out) that a map's runtime exports or a script package's imports reach,
            directly or through other content, one line each with the maps and packages that reach it
Reads only; about two minutes for everything.
"""
import argparse
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..'))

import uc2xbsp      # noqa: E402
import uc2xmesh     # noqa: E402
import uc2xpkg      # noqa: E402
import uc2xskel     # noqa: E402
import uc2xterrain  # noqa: E402
import uc2xtex      # noqa: E402
from uc2xpkg import Package, Reader, files_in, find_ci, object_head_end  # noqa: E402


def say(msg):
    print(msg, flush=True)


def map_names(game, only):
    names = [os.path.splitext(os.path.basename(p))[0] for p in files_in(game, 'Maps', '.unr')]
    return [n for n in names if n != 'Entry' and (not only or n.lower() in only)]


def level_model(pkg):
    for i, e in enumerate(pkg.exports):
        if pkg.export_class(i) == 'Level':
            return uc2xpkg.level_model_ref(e.data, e.flags, pkg) - 1
    return None


def by_path(pkg, cls):
    return {pkg.export_path(i).lower(): i for i in range(len(pkg.exports)) if pkg.export_class(i) == cls}


def check_tables(game, epic):
    import collections
    pkgs = same_guid = same_tables = 0
    added = collections.Counter()
    same, differ = collections.Counter(), collections.Counter()
    bad = []
    for folder, ext in (('Maps', '.unr'), ('Textures', '.utx'), ('StaticMeshes', '.usx'), ('Animations', '.ukx')):
        for path in files_in(game, folder, ext):
            ref = find_ci(epic, '%s/%s' % (folder, os.path.basename(path)))
            if ref is None:
                continue
            g, e = Package.load(path), Package.load(ref)
            pkgs += 1
            same_guid += g.guid == e.guid and g.gens == e.gens
            load = uc2xpkg.RF_LoadRuntime | uc2xpkg.RF_LoadForEdit
            ok = (g.names[:len(e.names)] == e.names and g.imports[:len(e.imports)] == e.imports and
                  all((x.cls, x.sup, x.pkg, x.name, x.flags | load) == (y.cls, y.sup, y.pkg, y.name, y.flags | load)
                      for x, y in zip(g.exports, e.exports)) and len(g.exports) >= len(e.exports))
            same_tables += ok
            if not ok or g.guid != e.guid:
                bad.append(os.path.relpath(path, game))
            added['names'] += len(g.names) - len(e.names)
            added['imports'] += len(g.imports) - len(e.imports)
            added['exports'] += len(g.exports) - len(e.exports)
            if e.version != 151:
                continue
            for j, (x, y) in enumerate(zip(g.exports, e.exports)):
                if x.data and y.data:
                    (same if x.data == y.data else differ)[e.export_class(j)] += 1
    say('tables: %d packages, %d with the PC GUID and generations, %d with the PC tables; added after them %d '
        'names, %d imports, %d exports%s' % (pkgs, same_guid, same_tables, added['names'], added['imports'],
                                             added['exports'], '; differ: ' + ', '.join(bad) if bad else ''))
    classes = sorted(set(same) | set(differ), key=lambda c: -(same[c] + differ[c]))
    say('tables: export data identical in the packages of version 151: %d of %d (%s)' % (
        sum(same.values()), sum(same.values()) + sum(differ.values()),
        ', '.join('%s %d/%d' % (c, same[c], same[c] + differ[c]) for c in classes)))


def check_lin(disc, epic, maps):
    foreign = uc2xpkg.ForeignIndex()
    for p in files_in(disc, 'System', '.u'):
        foreign.add_file(p)
    uc2xpkg.NATIVE_CHECKS.update(uc2xmesh.NATIVE_CHECKS)
    uc2xpkg.LIN_WALKERS['SkeletalMesh'] = uc2xskel.lin_size
    same = differ = lazy = lazy_same = editor = 0
    for m in maps:
        lin = find_ci(disc, 'XboxMaps/%s.LIN' % m)
        ref = find_ci(epic, 'XboxMaps/%s.unr' % m)
        if not lin or not ref:
            continue
        p, r = uc2xpkg.read_lin(lin, foreign), Package.load(ref)
        for a, b in zip(p.exports, r.exports):
            if a.lin is not None:
                lazy += 1
                lazy_same += uc2xskel.lin_expand(a.lin, a, p) == b.data
            elif not a.runtime():
                editor += 1
            elif a.data == b.data:
                same += 1
            else:
                differ += 1
    say('lin: %d maps, %d runtime exports identical, %d differ, %d of %d skeletal mesh streams expanded exactly, '
        '%d editor-only exports not in the streams' % (len(maps), same, differ, lazy_same, lazy, editor))


def check_bsp(game, epic, maps):
    worst = 0.0
    geometry = textures = lit = second = 0
    for m in maps:
        pc_path = find_ci(epic, 'Maps/%s.unr' % m)
        if not pc_path:
            continue
        a_pkg, b_pkg = Package.load(os.path.join(game, 'Maps', m + '.unr')), Package.load(pc_path)
        ai, bi = level_model(a_pkg), level_model(b_pkg)
        ae, be = a_pkg.exports[ai], b_pkg.exports[bi]
        a = uc2xbsp.parse(ae.data, ae.flags, False, a_pkg)
        b = uc2xbsp.parse(be.data, be.flags, False, b_pkg)
        same = all(getattr(a, f) == getattr(b, f) for f in ('vectors', 'points', 'verts', 'boxes', 'leaves'))
        same = same and a.nodes == b.nodes and [s['idx'] for s in a.surfs] == [s['idx'] for s in b.surfs]
        geometry += same
        textures += [t['data'][0] for t in a.lmtextures] == [t['data'][0] for t in b.lmtextures]
        second += sum(1 for t in b.lmtextures if t['data'][1])
        if len(a.lightmaps_raw) != len(b.lightmaps_raw):
            continue
        pts = [struct.unpack_from('<3f', a.points, 12 * k) for k in range(len(a.points) // 12)]
        pool = [struct.unpack_from('<i', a.verts, 8 * k)[0] for k in range(len(a.verts) // 8)]
        for n in a.nodes:
            if n['lightmap'] < 0 or not n['numverts']:
                continue
            lit += 1
            for k in range(n['numverts']):
                p = pts[pool[n['vertpool'] + k]]
                ua, va = _lightmap_uv(a.lightmaps_raw[n['lightmap']], p)
                ub, vb = _lightmap_uv(b.lightmaps_raw[n['lightmap']], p)
                worst = max(worst, abs(ua - ub), abs(va - vb))
    say('bsp: %d maps, geometry identical in %d, light map textures (first mip) identical in %d (the PC has a '
        'second mip in %d, not on the disc), %d lit nodes, largest light map coordinate error %.4f texels' % (
            len(maps), geometry, textures, second, lit, worst))


def _lightmap_uv(raw, p):
    v = struct.unpack_from('<7i16f', raw, 0)
    m = v[7:]
    return (v[3] + p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12],
            v[4] + p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13])


def _top_mip(pkg, i):
    e = pkg.exports[i]
    mips, _ = uc2xtex.read_mips(e.data, object_head_end(e.data, e.flags, pkg))
    return mips[0][3] if mips else None


def check_terrain(game, epic, maps):
    n = heights = colors = 0
    for m in maps:
        pc_path = find_ci(epic, 'Maps/%s.unr' % m)
        a_pkg = Package.load(os.path.join(game, 'Maps', m + '.unr'))
        infos = by_path(a_pkg, 'TerrainInfo')
        if not infos or not pc_path:
            continue
        b_pkg = Package.load(pc_path)
        b_infos = by_path(b_pkg, 'TerrainInfo')
        for path, ai in infos.items():
            bi = b_infos.get(path)
            if bi is None:
                continue
            n += 1
            fa = uc2xterrain._info_fields(Reader(a_pkg.exports[ai].data, object_head_end(
                a_pkg.exports[ai].data, a_pkg.exports[ai].flags, a_pkg)))
            fb = uc2xterrain._info_fields(Reader(b_pkg.exports[bi].data, object_head_end(
                b_pkg.exports[bi].data, b_pkg.exports[bi].flags, b_pkg)))
            colors += fa['colors'] == fb['colors']
            ta, _ = uc2xtex.read_tags(a_pkg, a_pkg.exports[ai].data, a_pkg.exports[ai].flags)
            tb, _ = uc2xtex.read_tags(b_pkg, b_pkg.exports[bi].data, b_pkg.exports[bi].flags)
            ra, rb = ta.get(('TerrainMap', 0)), tb.get(('TerrainMap', 0))
            if isinstance(ra, int) and isinstance(rb, int) and ra > 0 and rb > 0:
                heights += _top_mip(a_pkg, ra - 1) == _top_mip(b_pkg, rb - 1)
    say('terrain: %d terrains, heightmaps identical in %d, vertex colours identical in %d' % (n, heights, colors))


def placeholders(game, ending=', placeholder'):
    """(package file name, export path) of every export the cook's report gives a warning with that ending:
    the textures that got a placeholder (default), the meshes written without geometry ('without geometry'),
    the Fonts left out ('Font left out')."""
    out, package = set(), None
    report = os.path.join(game, 'Maps', 'xbox-packages.txt')
    for line in open(report, encoding='latin-1') if os.path.isfile(report) else ():
        if not line.startswith(' '):
            package = os.path.basename(line.split(':')[0]).lower()
        elif line.rstrip().endswith(ending):
            out.add((package, line.split(':')[0].strip().lower()))
    return out


class _Reach:
    """check_used's walk: content exports reached from a root (a map or a script package), and the ones the cook
    wrote without data."""

    def __init__(self, game, kinds, types):
        content = (('Textures', '.utx'), ('StaticMeshes', '.usx'), ('Animations', '.ukx'))
        self.files = {os.path.basename(p).lower(): p for folder, ext in content for p in files_in(game, folder, ext)}
        self.by_name = {os.path.splitext(f)[0]: f for f in self.files}
        self.kinds, self.types = kinds, types
        self.loaded, self.seen, self.queue, self.found, self.cache = {}, set(), [], {}, {}

    def package(self, f):
        if f not in self.loaded:
            p = self.loaded[f] = Package.load(self.files[f])
            p.paths = {p.export_path(i).lower(): i for i in range(len(p.exports))}
        return self.loaded[f]

    def refs(self, pkg, i):
        import uc2xrefs
        try:
            return [struct.unpack_from('<i', pkg.exports[i].data, at)[0]
                    for at, kind in uc2xrefs.references(pkg, self.types, i) if kind == 'ref']
        except (uc2xpkg.FormatError, IndexError, KeyError, struct.error):
            return []

    def visit(self, f, j, root):
        if (f, j) in self.seen:
            return
        self.seen.add((f, j))
        q = self.package(f)
        self.queue.append((q, j))
        kind = self.kinds.get((f, q.export_path(j).lower()))
        if kind:
            self.found.setdefault((kind, '%s.%s' % (os.path.splitext(f)[0], q.export_path(j))), set()).add(root)

    def ref(self, pkg, v, root):
        """Follow reference v of pkg: an import of a content package's export, or an export of a content package."""
        if v > 0:
            f = os.path.basename(pkg.path).lower()
            if f in self.files:
                self.visit(f, v - 1, root)
        elif v < 0:
            path = pkg.ref_path(v)
            f = self.by_name.get(path.split('.')[0].lower())
            if f is not None:
                q = self.package(f)
                j = q.paths.get(path.split('.', 1)[-1].lower())
                # the engine resolves an import by path and class (ULinkerLoad::VerifyImport)
                if j is not None and q.export_class(j).lower() == pkg.ref_class(v).lower():
                    self.visit(f, j, root)

    def run(self, root, pkg, refs):
        for v in refs:
            self.ref(pkg, v, root)
        while self.queue:
            q, j = self.queue.pop()
            key = (q.path, j)
            if key not in self.cache:
                self.cache[key] = self.refs(q, j) if q.exports[j].data else []
            for v in self.cache[key]:
                self.ref(q, v, root)


def check_used(game):
    """The content the game reaches that the cook wrote without data: from every map's runtime exports and every
    script package's imports, the references followed through the content packages."""
    import uc2types
    kinds = {k: 'placeholder' for k in placeholders(game)}
    kinds.update({k: 'no geometry' for k in placeholders(game, 'without geometry')})
    kinds.update({k: 'Font left out' for k in placeholders(game, 'Font left out')})
    walk = _Reach(game, kinds, uc2types.Types(files_in(game, 'System', '.u')))
    for path in files_in(game, 'Maps', '.unr'):
        pkg = Package.load(path)
        refs = [v for i, e in enumerate(pkg.exports) if e.data and e.runtime() for v in walk.refs(pkg, i)]
        walk.seen = set()
        walk.run(os.path.splitext(os.path.basename(path))[0], pkg, refs)
    for path in files_in(game, 'System', '.u'):
        pkg = Package.load(path)
        walk.seen = set()
        walk.run(os.path.basename(path), pkg, [-k - 1 for k in range(len(pkg.imports))])
    counts = {}
    for (kind, path), where in sorted(walk.found.items()):
        counts[kind] = counts.get(kind, 0) + 1
        say('used: %s %s, from %s' % (kind, path, ', '.join(sorted(where)[:6]) + (
            ' and %d more' % (len(where) - 6) if len(where) > 6 else '')))
    say('used: %d content exports that maps or script packages reach have no data from the disc (%s)' % (
        len(walk.found), ', '.join('%s %d' % kv for kv in sorted(counts.items())) or 'none'))


def check_textures(game, epic):
    same = differ = placeholder = 0
    missing = placeholders(game)
    for path in files_in(game, 'Textures', '.utx'):
        pc_path = find_ci(epic, 'Textures/' + os.path.basename(path))
        if not pc_path:
            continue
        a, b = Package.load(path), Package.load(pc_path)
        b_tex = by_path(b, 'Texture')
        for p, i in by_path(a, 'Texture').items():
            j = b_tex.get(p)
            if j is None:
                continue
            if (os.path.basename(path).lower(), p) in missing:
                placeholder += 1
            elif _top_mip(a, i) == _top_mip(b, j):
                same += 1
            else:
                differ += 1
    say('textures: %d top mips identical to the PC original, %d differ, %d placeholders (no pixels on the disc)'
        % (same, differ, placeholder))


def check_meshes(game, epic):
    n = colors = uvs = indices = 0
    worst = 0.0
    for path in files_in(game, 'StaticMeshes', '.usx'):
        pc_path = find_ci(epic, 'StaticMeshes/' + os.path.basename(path))
        if not pc_path:
            continue
        a, b = Package.load(path), Package.load(pc_path)
        if b.version != 151:
            continue
        b_mesh = by_path(b, 'StaticMesh')
        for p, i in by_path(a, 'StaticMesh').items():
            j = b_mesh.get(p)
            if j is None:
                continue
            ma = uc2xmesh.parse_mesh(a.exports[i].data, uc2xmesh.head_end(a, i), 0, a.exports[i].offset)
            mb = uc2xmesh.parse_mesh(b.exports[j].data, uc2xmesh.head_end(b, j), 0, b.exports[j].offset)
            if not ma.vertices:
                continue
            n += 1
            colors += ma.colors == mb.colors and ma.alpha == mb.alpha
            uvs += [u[0] for u in ma.uvs] == [u[0] for u in mb.uvs]
            indices += ma.indices == mb.indices
            if len(ma.vertices) == len(mb.vertices):
                step = [s / 32767.0 or 1.0 for s in mb.scale]
                for k in range(0, len(ma.vertices), 24):
                    pa = struct.unpack_from('<3f', ma.vertices, k)
                    pb = struct.unpack_from('<3f', mb.vertices, k)
                    worst = max(worst, *(abs(x - y) / s for x, y, s in zip(pa, pb, step)))
    say('meshes: %d static meshes with geometry and a version-151 PC original: colours identical in %d, UVs in %d, '
        'indices in %d, largest position error %.3f quantisation steps' % (n, colors, uvs, indices, worst))


def check_anims(game, epic):
    n = same = 0
    for path in files_in(game, 'Animations', '.ukx'):
        pc_path = find_ci(epic, 'Animations/' + os.path.basename(path))
        if not pc_path:
            continue
        a, b = Package.load(path), Package.load(pc_path)
        if b.version != 151:
            continue
        b_anim = by_path(b, 'MeshAnimation')
        for p, i in by_path(a, 'MeshAnimation').items():
            j = b_anim.get(p)
            if j is None:
                continue
            n += 1
            ea, eb = a.exports[i], b.exports[j]
            ra, rb = uc2xskel.parse_anim(ea.data, ea.flags, 0, a), uc2xskel.parse_anim(eb.data, eb.flags, 0, b)
            same += ea.data[ra['block_at']:ra['moves_at']] == eb.data[rb['block_at']:rb['moves_at']]
    say('anims: %d animations with a version-151 PC original, motion block identical in %d' % (n, same))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', required=True, help='a game folder cooked from the disc')
    ap.add_argument('--epic', required=True, help="Epic's tree (uc2-avaris-2226)")
    ap.add_argument('--disc', default='', help="the disc's files, for the lin check")
    ap.add_argument('--only', default='tables,lin,bsp,terrain,textures,meshes,anims,used')
    ap.add_argument('--maps', default='')
    a = ap.parse_args()
    only = set(a.only.split(','))
    maps = map_names(a.game, {m.strip().lower() for m in a.maps.split(',') if m.strip()})
    if 'tables' in only:
        check_tables(a.game, a.epic)
    if 'lin' in only and a.disc:
        check_lin(a.disc, a.epic, maps)
    if 'bsp' in only:
        check_bsp(a.game, a.epic, [m for m in maps if m != 'Menu'])
    if 'terrain' in only:
        check_terrain(a.game, a.epic, maps)
    if 'textures' in only:
        check_textures(a.game, a.epic)
    if 'meshes' in only:
        check_meshes(a.game, a.epic)
    if 'anims' in only:
        check_anims(a.game, a.epic)
    if 'used' in only:
        check_used(a.game)


if __name__ == '__main__':
    main()
