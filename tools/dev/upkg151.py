#!/usr/bin/env python3
"""Dump the tables of an Unreal Championship 2 package (version 151, licensee 0).

    upkg151.py <file.u>            summary + tables
    upkg151.py <file.u> --names    one name per line

The reader itself is tools/scripts/ucpkg.py (Package; the shipping script tools and the dev
tools share one parser). Layout: docs/porting/analysis/package-format-v151.md.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts'))
from ucpkg import Package  # noqa: E402,F401  (re-exported for older callers)


def main():
    pkg = Package(sys.argv[1])
    if '--names' in sys.argv:
        for n in pkg.names:
            print(n)
        return
    print('version %d licensee %d flags 0x%08x names %d exports %d imports %d guid %s generations %s'
          % (pkg.version, pkg.licensee, pkg.flags, pkg.name_count, pkg.export_count, pkg.import_count,
             pkg.guid.hex(), pkg.generations))
    for i, im in enumerate(pkg.imports):
        print('import %4d %s.%s %s' % (-i - 1, im['class_package'], im['class_name'], pkg.import_path(i)))
    for i, ex in enumerate(pkg.exports):
        print('export %4d %-14s %-60s flags=0x%08x size=%d super=%s' % (
            i + 1, pkg.export_class(i), pkg.export_path(i), ex['flags'], ex['size'], pkg.ref_name(ex['super'])))


if __name__ == '__main__':
    main()
