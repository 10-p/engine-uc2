#!/usr/bin/env python3
"""cook-xbox-packages.py: convert Unreal Championship 2's Xbox-format packages from the retail disc into the PC
format the engine loads (package version 151, licensee 0).

Input (read only; --in <dir>, the disc's files):
  XboxMaps/<Map>.LIN            the maps, as the Xbox loader's linear-load streams
  XboxTextures/*.utx            texture packages without their pixels
  XboxStaticMeshes/*.usx        static mesh packages without their vertex and index data
  XboxAnimations/*.ukx          skeletal mesh and animation packages without their vertex and motion data
  XboxTextures/*.xpr            the bundles holding what the packages leave out
  System/Script.lin             the script packages' and the entry level's linear-load stream
  System/*.u                    the script packages (the .LIN streams hold reads from them)

Also read: --tables <file> (default uc2-pc-tables.json.gz next to this script), the tables of the PC packages
clients run, which the packages are written in (uc2xtables.py; tools/dev/make-pc-tables.py makes the file).

Output (never committed; --out <dir>, a game folder):
  Maps/<Map>.unr, Maps/Entry.unr (from System/Script.lin), Textures/*.utx, StaticMeshes/*.usx, Animations/*.ukx
  Maps/xbox-packages.txt        per package: exports converted, copied and left out, what the PC tables took, and
                                every warning (the textures without pixels and the meshes without geometry on the
                                disc)

Usage:

  tools/cook-xbox-packages.py --in <disc folder> --out <game folder> [--only DM-Legacy,T_FX] [--jobs N]
  tools/cook-xbox-packages.py --in <disc folder> --out <dir> --disc-tables     the disc's own tables

The formats and what the conversion keeps are in docs/porting/analysis/xbox-packages.md. Per export: the
tagged properties are the same in both formats and are copied; the classes whose native data differs are rewritten
by the converters (uc2xbsp.py: models, uc2xtex.py: textures, uc2xmesh.py: static meshes, uc2xskel.py: skeletal
meshes and animations, uc2xterrain.py: terrain); everything else is copied as it is. Left out: what the PC never
loads (editor-only exports, which a .LIN does not hold) and Xbox-only data the PC builds itself (terrain sector
blocks and layer weight maps, clipped static projectors). The maps are converted first: their merged copies of
content-package assets supply the pixels and the geometry those packages lack, each copy taken for the object its
map's PC tables say it stands for (uc2xtables.copy_identities). Each package is then rewritten into its PC
package's tables, so that a server on these packages and a client on the PC ones agree. tools/cook.py runs it as one
step of a whole cook (--source xbox).
"""

import argparse
import concurrent.futures
import os
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)

import uc2xbsp      # noqa: E402
import uc2xmesh     # noqa: E402
import uc2xpkg      # noqa: E402
import uc2xpr       # noqa: E402
import uc2xskel     # noqa: E402
import uc2types     # noqa: E402
import uc2xtables   # noqa: E402
import uc2xterrain  # noqa: E402
import uc2xtex      # noqa: E402

TABLES = 'uc2-pc-tables.json.gz'
DT_StaticProjectorBatch = 13    # Actor.EDrawType
FOLDERS = (
    ('XboxMaps', '.lin', 'Maps', '.unr'),
    ('XboxTextures', '.utx', 'Textures', '.utx'),
    ('XboxStaticMeshes', '.usx', 'StaticMeshes', '.usx'),
    ('XboxAnimations', '.ukx', 'Animations', '.ukx'),
)


def converters():
    """Class name -> convert(pkg, i, ctx) for every class whose native data differs between the formats."""
    table = {'Model': uc2xbsp.convert}
    for c in ('Texture', 'Cubemap', 'Palette', 'Font'):
        table[c] = uc2xtex.convert
    for c in ('StaticMesh', 'StaticMeshInstance', 'StaticProjectorClippedPrim'):
        table[c] = uc2xmesh.convert
    for c in ('SkeletalMesh', 'MeshAnimation'):
        table[c] = uc2xskel.convert
    for c in uc2xterrain.CLASSES:
        table[c] = uc2xterrain.convert
    # what the .LIN cut uses to place the exports of these classes
    uc2xpkg.NATIVE_CHECKS.update(uc2xmesh.NATIVE_CHECKS)
    uc2xpkg.LIN_WALKERS['SkeletalMesh'] = uc2xskel.lin_size
    return table


class Context:
    def __init__(self, xpr, prefer, lin, map_textures=None, map_copies=None):
        self.xpr, self.prefer, self.lin = xpr, prefer, lin
        self.map_textures = map_textures
        self.map_copies = map_copies
        self.warnings = []

    def warn(self, msg):
        self.warnings.append(msg)


_state = {}


def _init(disc):
    """Per worker: the bundles' index, the script packages the maps' streams read from, the converters."""
    _state['xpr'] = uc2xpr.XprSet(uc2xpkg.find_ci(disc, 'XboxTextures'))
    foreign = uc2xpkg.ForeignIndex()
    for path in uc2xpkg.files_in(disc, 'System', '.u'):
        foreign.add_file(path)
    _state['foreign'] = foreign
    _state['conv'] = converters()
    _state['types'] = uc2types.Types(uc2xpkg.files_in(disc, 'System', '.u'))
    _state['content'] = {os.path.splitext(os.path.basename(p))[0].lower(): p
                         for folder, ext, _, _ in FOLDERS[1:] for p in uc2xpkg.files_in(disc, folder, ext)}
    _state['originals'] = {}


def original(name):
    """The disc's content package of that name (texture, static mesh or animation package), or None."""
    key = name.lower()
    if key not in _state['originals']:
        path = _state['content'].get(key)
        _state['originals'][key] = uc2xpkg.Package.load(path) if path else None
    return _state['originals'][key]


def prefer_for(name, is_map):
    base = name.lower()
    own = [base + '.xpr', base + '_uma.xpr', base + '_pb.xpr'] if is_map else []
    return own + ['natives.xpr', 'natives_uma.xpr', 'natives_pb.xpr']


def drop_export(e):
    """An export the conversion could not write: flagged for neither client nor server, so the engine never
    creates it and references to it load as None (a size-0 export would be read from the file's start)."""
    e.data, e.lazy, e.lin = b'', (), None
    e.flags &= ~uc2xpkg.RF_LoadRuntime


def load_xbox(src):
    """An Xbox package: a .LIN cut back into its exports (the entry level out of System/Script.lin), or a file."""
    if src.lower().endswith('.lin'):
        start = 0
        if os.path.basename(src).lower() == 'script.lin':
            start = uc2xpkg.find_level(src)
            if start is None:
                raise uc2xpkg.FormatError('%s: no level in the stream' % src)
        pkg = uc2xpkg.read_lin(src, _state['foreign'], start)
    else:
        pkg = uc2xpkg.Package.load(src)
    if pkg.licensee != 1:
        raise uc2xpkg.FormatError('%s: licensee %d, not the Xbox format' % (src, pkg.licensee))
    return pkg


def map_copies(pkg, name, tables):
    """What a map holds of the content packages' assets (their merged cla<N> copies, with the pixels and the
    geometry in the map's bundles), for the content packages whose own data the disc lacks: (the textures' copies
    by key, the copies by the object they stand for, from the map's PC tables)."""
    copies = uc2xtex.MapTextures()
    copies.add(pkg, name.lower() + '.xpr')
    named = uc2xtables.MapCopies()
    if tables:
        named.add(name, uc2xtables.copy_identities(pkg, tables, _state['types'], original))
    return copies, named


def static_projectors(pkg, counts):
    """The Xbox saves static projectors already clipped (StaticPrims, and their StaticProjectorClippedPrim
    objects). The PC clips them when the projector attaches, unless StaticPrims came from the package (then
    AProjector::Attach keeps them and UGameEngine::LoadMap's batching empties them): the PC gets projectors
    without them, as in Epic's PC maps, and without the bHidden and DrawType the batching set
    (AProjector::RebuildStaticPrimsBatch)."""
    for i, e in enumerate(pkg.exports):
        if not e.data:
            continue
        cls = pkg.export_class(i)
        if cls == 'StaticProjectorClippedPrim':
            drop_export(e)
            counts['converted'] -= 1
            counts['left out'] += 1
        elif e.runtime() and cls not in ('Model', 'Polys', 'Texture', 'StaticMesh', 'SkeletalMesh'):
            data = uc2xpkg.drop_tags(e.data, e.flags, pkg, ('staticprims',))
            batched = len(data) != len(e.data)
            if not batched:
                props, _ = uc2xtex.read_tags(pkg, e.data, e.flags)
                batched = any(k[0].lower() == 'drawtype' and v == DT_StaticProjectorBatch for k, v in props.items())
            if batched:
                data = uc2xpkg.drop_tags(data, e.flags, pkg, ('bhidden', 'drawtype'))
            if len(data) != len(e.data):
                lazy = e.lazy
                if lazy:
                    raise uc2xpkg.FormatError('%s: lazy data in a projector' % pkg.export_path(i))
                e.data = data


def convert_package(src, dst, copies=None, write=True, tables=None):
    """Convert one package, into the PC package's tables when tables are given (uc2xtables.py); returns (dst,
    counts, warnings, the map's copies of content-package assets: map_copies()). copies: the maps' copies, for a
    content package (map_copies() of every map, merged)."""
    name = os.path.splitext(os.path.basename(dst))[0]
    is_map = src.lower().endswith('.lin')
    pkg = load_xbox(src)
    found = map_copies(pkg, name, tables) if is_map else None
    if not write:
        return dst, None, [], found
    ctx = Context(_state['xpr'], prefer_for(name, is_map), is_map, *(copies or (None, None)))
    counts = {'converted': 0, 'copied': 0, 'editor-only': 0, 'left out': 0, 'failed': 0}
    left_out = set()
    if is_map:
        # the heightmaps the PC builds its terrain from, out of the sector blocks, before the textures
        ctx.terrain = uc2xterrain.rebuild(pkg)
        ctx.pixels = uc2xterrain.heightmaps(pkg, ctx.terrain)
        for i in uc2xterrain.weight_maps(pkg):
            drop_export(pkg.exports[i])
            left_out.add(i)
            counts['left out'] += 1
    conv = _state['conv']
    for i, e in enumerate(pkg.exports):
        if i in left_out:
            continue
        cls = pkg.export_class(i)
        if not e.runtime() and not e.data:
            counts['editor-only'] += 1
            continue
        if not e.data and e.lin is None:
            if cls == 'Package':
                e.data = b'\x01\x00\x00\x00\x00'
                counts['copied'] += 1
                continue
            ctx.warn('%s (%s): no data in the stream' % (pkg.export_path(i), cls))
            counts['failed'] += 1
            drop_export(e)
            continue
        fn = conv.get(cls)
        if fn is None:
            if e.lin is not None:
                ctx.warn('%s (%s): lazy data left out by the Xbox and no converter' % (pkg.export_path(i), cls))
                counts['failed'] += 1
                drop_export(e)
            else:
                counts['copied'] += 1
            continue
        try:
            out = fn(pkg, i, ctx)
        except Exception as ex:   # one bad export must not lose the package: report it, keep the others
            ctx.warn('%s (%s): %s: %s' % (pkg.export_path(i), cls, type(ex).__name__, ex))
            counts['failed'] += 1
            drop_export(e)
            continue
        if out is None:
            # Xbox-only data the PC builds itself
            drop_export(e)
            counts['left out'] += 1
            continue
        if isinstance(out, tuple):
            e.data, e.lazy = out
        else:
            e.data, e.lazy = out, ()
        e.lin = None
        counts['converted'] += 1
    static_projectors(pkg, counts)
    if tables is not None:
        pkg, stats = uc2xtables.apply(pkg, tables, _state['types'], ctx.warn)
        counts.update(stats)
    else:
        pkg.gens = None
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    pkg.write(dst + '.part', licensee=0)
    os.replace(dst + '.part', dst)
    return dst, counts, ctx.warnings, found


def _job(args):
    src, dst, copies, write, tables = args
    t = time.time()
    try:
        dst, counts, warnings, found = convert_package(src, dst, copies, write, tables)
        return src, dst, counts, warnings, found, time.time() - t, None
    except Exception as ex:
        return src, dst, None, [], None, time.time() - t, '%s: %s' % (type(ex).__name__, ex)


def with_tables(job, out, tables, disc_tables):
    """A job with its package's PC tables, written under the name the PC folder gives it."""
    src, dst, write = job
    if disc_tables:
        return src, dst, write, None
    rel, t = uc2xtables.find(tables, os.path.relpath(dst, out).replace(os.sep, '/'))
    if t is None:
        return src, dst, write, None
    # a map not written still says what its merged copies stand for
    return src, os.path.join(out, rel), write, t if write or src.lower().endswith('.lin') else None


def plan(disc, out, only):
    """(source, destination, written) for every package: the maps first (read even when not written, for the
    texture packages' copies), then the content packages."""
    jobs = []
    script = uc2xpkg.find_ci(disc, 'System/Script.lin')
    if script:
        jobs.append((script, os.path.join(out, 'Maps', 'Entry.unr'), not only or 'entry' in only))
    for folder, ext_in, outdir, ext in FOLDERS:
        for src in uc2xpkg.files_in(disc, folder, ext_in):
            name = os.path.splitext(os.path.basename(src))[0]
            write = not only or name.lower() in only
            if write or folder == 'XboxMaps':
                jobs.append((src, os.path.join(out, outdir, name + ext), write))
    return jobs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--in', dest='disc', required=True, help="the disc's files (XboxMaps/, XboxTextures/, ...)")
    ap.add_argument('--out', required=True, help='the game folder the PC packages go to')
    ap.add_argument('--only', default='', help='comma-separated package names (without extension)')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument('--tables', default=os.path.join(here, TABLES),
                    help='the PC packages\' tables (default: %s next to this script)' % TABLES)
    ap.add_argument('--disc-tables', action='store_true',
                    help="keep the disc's tables (for tools/dev/make-pc-tables.py): no network play with the PC "
                         "packages")
    a = ap.parse_args()
    only = {s.strip().lower() for s in a.only.split(',') if s.strip()}
    jobs = plan(a.disc, a.out, only)
    if not any(w for _, _, w in jobs):
        sys.exit('no Xbox packages under %s' % a.disc)
    tables = {}
    if not a.disc_tables:
        if not os.path.isfile(a.tables):
            sys.exit('%s: no PC tables (tools/dev/make-pc-tables.py makes them)' % a.tables)
        tables = uc2xtables.load(a.tables)
    jobs = [with_tables(j, a.out, tables, a.disc_tables) for j in jobs]
    report = ['%s: no PC tables, written with the disc\'s' % os.path.relpath(d, a.out)
              for _, d, w, t in jobs if w and t is None and not a.disc_tables]
    failed = 0
    maps = [j for j in jobs if j[0].lower().endswith('.lin')]
    rest = [j for j in jobs if not j[0].lower().endswith('.lin')]
    with concurrent.futures.ProcessPoolExecutor(max_workers=a.jobs, initializer=_init,
                                                initargs=(a.disc,)) as pool:
        copies, named = uc2xtex.MapTextures(), uc2xtables.MapCopies()
        results = []
        for r in pool.map(_job, [(s, d, None, w, t) for s, d, w, t in maps]):
            results.append(r)
            if r[4] is not None:
                copies.merge(r[4][0])
                named.merge(r[4][1])
        for src, _, _, _ in rest:
            if src.lower().endswith('.utx'):
                copies.add_originals(uc2xpkg.Package.load(src))
        results += list(pool.map(_job, [(s, d, (copies, named), w, t) for s, d, w, t in rest]))
        for src, dst, counts, warnings, _, secs, err in results:
            if counts is None and not err:
                continue
            rel = os.path.relpath(dst, a.out)
            if err:
                failed += 1
                print('FAILED %s: %s' % (rel, err), flush=True)
                report.append('%s: FAILED: %s' % (rel, err))
                continue
            line = ('%s: %d converted, %d copied, %d editor-only and %d Xbox-only left out, %d failed, '
                    '%d warnings' % (rel, counts['converted'], counts['copied'], counts['editor-only'],
                                     counts['left out'], counts['failed'], len(warnings)))
            if 'exports' in counts:
                line += ('; PC tables: %d exports from the disc, %d not on the disc, %d disc exports left out, '
                         'added %d names, %d imports, %d exports' % (
                             counts['exports'], counts.get('PC exports not on the disc', 0),
                             counts.get('disc exports left out', 0), counts.get('names added', 0),
                             counts.get('imports added', 0), counts.get('exports added', 0)))
            # the report holds no timings: the same disc gives the same report
            print('%s (%.1f s)' % (line, secs), flush=True)
            report.append(line)
            report.extend('    ' + w for w in warnings)
    os.makedirs(os.path.join(a.out, 'Maps'), exist_ok=True)
    with open(os.path.join(a.out, 'Maps', 'xbox-packages.txt'), 'w') as f:
        f.write('\n'.join(report) + '\n')
    if failed:
        sys.exit('%d packages failed' % failed)


if __name__ == '__main__':
    main()
