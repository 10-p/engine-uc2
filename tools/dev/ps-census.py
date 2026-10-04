#!/usr/bin/env python3
"""ps-census.py - which packages define or use pixel-shader materials (classes PS*, PixelShaderMaterial).

Reads only the name, import and export tables of UC2 packages (version 150/151, licensee 0; layout in
docs/porting/analysis/package-format-v151.md). Usage:

    tools/dev/ps-census.py <assets dir> [--objects] [--package NAME]

Prints, per material class, how many objects of it exist and in which packages they are defined, then per map
the pixel-shader material objects it imports (class.package.object).
"""
import os
import struct
import sys
from collections import defaultdict


def read_package(path):
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 64 or struct.unpack_from('<I', data, 0)[0] != 0x9E2A83C1:
        return None
    ver, lic = struct.unpack_from('<HH', data, 4)
    if ver < 150:
        return None
    name_count, name_off, exp_count, exp_off, imp_count, imp_off = struct.unpack_from('<6i', data, 12)
    names = []
    pos = name_off
    for _ in range(name_count):
        n = struct.unpack_from('<i', data, pos)[0]
        pos += 4
        names.append(data[pos:pos + n - 1].decode('latin-1'))
        pos += n + 4
    imports = []
    pos = imp_off
    for _ in range(imp_count):
        cpkg, cname, pkg, oname = struct.unpack_from('<iihi', data, pos)
        pos += 14
        imports.append((names[cpkg], names[cname], pkg, names[oname]))
    exports = []
    pos = exp_off
    for _ in range(exp_count):
        cls, sup, sup2, pkg, pkg2 = struct.unpack_from('<5h', data, pos)
        pos += 10
        oname, flags, size = struct.unpack_from('<iIi', data, pos)
        pos += 12
        if size:
            pos += 4
        exports.append((cls, pkg, names[oname]))
    return ver, names, imports, exports


def class_name(pkg, cls):
    _, _, imports, exports = pkg
    if cls < 0:
        return imports[-cls - 1][3]
    if cls > 0:
        return exports[cls - 1][2]
    return 'Class'


def import_path(pkg, idx):
    _, _, imports, _ = pkg
    parts = []
    while idx < 0:
        imp = imports[-idx - 1]
        parts.append(imp[3])
        idx = imp[2]
    return '.'.join(reversed(parts))


def is_ps(name):
    return name == 'PixelShaderMaterial' or (name.startswith('PS') and len(name) > 2 and name[2].isupper())


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show_objects = '--objects' in sys.argv
    root = args[0]
    only = args[1] if len(args) > 1 else None
    defined = defaultdict(lambda: defaultdict(list))   # class -> package -> [objects]
    used = {}                                          # map -> [(class, path)]
    for dirpath, _, files in os.walk(root):
        for fn in sorted(files):
            if os.path.splitext(fn)[1].lower() not in ('.unr', '.utx', '.usx', '.ukx', '.u', '.uax'):
                continue
            if only and os.path.splitext(fn)[0].lower() != only.lower():
                continue
            pkg = read_package(os.path.join(dirpath, fn))
            if pkg is None:
                continue
            for cls, _, oname in pkg[3]:
                cn = class_name(pkg, cls)
                if is_ps(cn):
                    defined[cn][fn].append(oname)
            uses = []
            for i, (cpkg, cname, _, oname) in enumerate(pkg[2]):
                if is_ps(cname) and cpkg == 'Engine':
                    uses.append((cname, import_path(pkg, -i - 1)))
            if uses or any(is_ps(class_name(pkg, c)) for c, _, _ in pkg[3]):
                used[fn] = uses
    print('== pixel-shader material objects by class (defined in)')
    for cn in sorted(defined):
        total = sum(len(v) for v in defined[cn].values())
        print('%-32s %4d objects in %d packages: %s' % (cn, total, len(defined[cn]), ', '.join(
            '%s(%d)' % (p, len(o)) for p, o in sorted(defined[cn].items(), key=lambda kv: -len(kv[1]))[:12])))
        if show_objects:
            for p, objs in sorted(defined[cn].items()):
                print('    %s: %s' % (p, ' '.join(objs)))
    print('== packages that import pixel-shader materials')
    for fn in sorted(used):
        if not used[fn]:
            continue
        by = defaultdict(list)
        for cn, path in used[fn]:
            by[cn].append(path)
        print('%s' % fn)
        for cn in sorted(by):
            print('    %-28s %3d  %s' % (cn, len(by[cn]), ' '.join(by[cn][:6 if not show_objects else 999])))


if __name__ == '__main__':
    main()
