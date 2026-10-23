#!/usr/bin/env python3
"""make-pc-tables.py: write tools/uc2-pc-tables.json.gz, the tables of the PC game folder's packages (GUID,
generations, names, imports, exports) and how the disc's packages map into them, which tools/cook-xbox-packages.py
writes the disc's packages into (uc2xtables.py says how).

    tools/cook-xbox-packages.py --in <disc folder> --out <dir> --disc-tables
    tools/dev/make-pc-tables.py --pc <PC game folder> --disc <dir> [--out tools/uc2-pc-tables.json.gz]

--pc is the game folder clients run (Epic's PC packages: Maps/, Textures/, StaticMeshes/, Animations/ and the
script packages in System/); --disc the disc's packages converted with their own tables. One entry per package
of --disc that --pc has. Prints per package what was matched; the file is generated, never committed.
"""
import argparse
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..'))

import uc2types     # noqa: E402
import uc2xtables   # noqa: E402
from uc2xpkg import Package, files_in, find_ci  # noqa: E402

FOLDERS = (('Maps', '.unr'), ('Textures', '.utx'), ('StaticMeshes', '.usx'), ('Animations', '.ukx'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--pc', required=True, help='the PC game folder (Epic\'s PC packages)')
    ap.add_argument('--disc', required=True, help="the disc's packages, converted with --disc-tables")
    ap.add_argument('--out', default=os.path.join(here, '..', 'uc2-pc-tables.json.gz'))
    a = ap.parse_args()
    types = uc2types.Types(files_in(a.pc, 'System', '.u'))
    packages = {}
    missing = []
    for folder, ext in FOLDERS:
        for path in files_in(a.disc, folder, ext):
            pc_path = find_ci(a.pc, '%s/%s' % (folder, os.path.basename(path)))
            if pc_path is None:
                missing.append(os.path.relpath(path, a.disc))
                continue
            rel = os.path.relpath(pc_path, a.pc).replace(os.sep, '/')
            pc, disc = Package.load(pc_path), Package.load(path)
            t, stats = uc2xtables.make(pc, disc, types)
            packages[rel] = t
            print('%-40s %s' % (rel, ', '.join('%s %d' % kv for kv in sorted(stats.items())) or 'by path'),
                  flush=True)
    for rel in missing:
        print('%s: not in the PC folder' % rel)
    uc2xtables.save(a.out, packages)
    print('%s: %d packages, %d bytes' % (a.out, len(packages), os.path.getsize(a.out)))


if __name__ == '__main__':
    main()
