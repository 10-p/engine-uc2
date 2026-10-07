#!/usr/bin/env python3
"""Reader for Unreal Championship 2 packages (version 151, licensee 0): summary, name, import and export tables.

Layout: docs/porting/analysis/package-format-v151.md (compact index = plain int32 from version 145; the 10-byte
export head of version 150+). Used by tools/dev/script-diff.py (M14); also a small command-line dumper:

    upkg151.py <file.u>            summary + tables
    upkg151.py <file.u> --names    one name per line
"""
import struct
import sys


class Package:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        tag, self.version, self.licensee, self.flags = struct.unpack_from('<IHHI', d, 0)
        if tag != 0x9E2A83C1:
            raise ValueError('%s: not a package' % path)
        (self.name_count, self.name_offset, self.export_count, self.export_offset,
         self.import_count, self.import_offset) = struct.unpack_from('<6i', d, 12)
        self.guid = d[36:52]
        (gen_count,) = struct.unpack_from('<i', d, 52)
        self.generations = [struct.unpack_from('<ii', d, 56 + 8 * i) for i in range(gen_count)]
        if self.version < 150:
            raise ValueError('%s: version %d; only 150+ layouts are read here' % (path, self.version))
        # names
        self.names = []
        self.name_flags = []
        p = self.name_offset
        for _ in range(self.name_count):
            (n,) = struct.unpack_from('<i', d, p); p += 4
            s = d[p:p + n]; p += n
            (fl,) = struct.unpack_from('<I', d, p); p += 4
            self.names.append(s.rstrip(b'\0').decode('latin-1'))
            self.name_flags.append(fl)
        # imports: class package i32, class name i32, package index i16, object name i32
        self.imports = []
        p = self.import_offset
        for _ in range(self.import_count):
            cp, cn = struct.unpack_from('<ii', d, p); p += 8
            (pk,) = struct.unpack_from('<h', d, p); p += 2
            (on,) = struct.unpack_from('<i', d, p); p += 4
            self.imports.append({'class_package': self.names[cp], 'class_name': self.names[cn],
                                 'package': pk, 'name': self.names[on]})
        # exports: five i16 (class, super, super, package, package), name i32, flags u32, size i32, offset i32
        self.exports = []
        p = self.export_offset
        for _ in range(self.export_count):
            h = struct.unpack_from('<5h', d, p); p += 10
            (nm,) = struct.unpack_from('<i', d, p); p += 4
            (fl,) = struct.unpack_from('<I', d, p); p += 4
            (size,) = struct.unpack_from('<i', d, p); p += 4
            off = 0
            if size:
                (off,) = struct.unpack_from('<i', d, p); p += 4
            self.exports.append({'class': h[0], 'super': h[1], 'package': h[3], 'name': self.names[nm],
                                 'flags': fl, 'size': size, 'offset': off})

    # object references: >0 export index+1, <0 -(import index+1), 0 none
    def ref_name(self, i):
        if i == 0:
            return 'None'
        if i > 0:
            return self.export_path(i - 1)
        return self.import_path(-i - 1)

    def import_path(self, i):
        im = self.imports[i]
        parts = [im['name']]
        pk = im['package']
        while pk:
            if pk < 0:
                o = self.imports[-pk - 1]
                parts.append(o['name'])
                pk = o['package']
            else:
                o = self.exports[pk - 1]
                parts.append(o['name'])
                pk = o['package']
        return '.'.join(reversed(parts))

    def export_path(self, i):
        ex = self.exports[i]
        parts = [ex['name']]
        pk = ex['package']
        while pk:
            if pk > 0:
                o = self.exports[pk - 1]
            else:
                o = self.imports[-pk - 1]
            parts.append(o['name'])
            pk = o['package']
        return '.'.join(reversed(parts))

    def export_class(self, i):
        c = self.exports[i]['class']
        if c == 0:
            return 'Class'
        if c < 0:
            return self.imports[-c - 1]['name']
        return self.exports[c - 1]['name']

    def export_data(self, i):
        ex = self.exports[i]
        return self.data[ex['offset']:ex['offset'] + ex['size']]


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
