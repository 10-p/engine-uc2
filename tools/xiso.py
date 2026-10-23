#!/usr/bin/env python3
"""xiso.py: list and extract the files of an Xbox disc image (XDVDFS), with the standard library only.

Reads both kinds of image: a trimmed xiso (the file system at the start of the file) and a full disc dump (redump:
the video partition first, the game partition at 0x18300000 for XGD1, 0xFD90000 for XGD2, 0x2080000 for XGD3).

The file system (XDVDFS): 2048-byte sectors; the volume descriptor is sector 32 of the partition, 'MICROSOFT*XBOX*MEDIA',
u32 root directory sector, u32 root directory size. A directory is a binary tree of entries: u16 left and u16 right
(offsets of the subtrees in 4-byte units, 0 = none), u32 first sector, u32 size, u8 attributes (0x10 = directory),
u8 name length, the name; entries are 4-byte aligned and never cross a sector, the rest of a sector is 0xFF.

  tools/xiso.py list <image.iso>                          every file with its size
  tools/xiso.py extract <image.iso> <dir> [<path>...]     the whole disc, or only the given files and folders
"""

import os
import struct
import sys

SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'
PARTITIONS = (0, 0x18300000, 0xFD90000, 0x2080000)
ATTR_DIRECTORY = 0x10


class XisoError(Exception):
    pass


class Image:
    """An open disc image: walk() yields (path, size, byte offset) for every file, '/'-separated paths."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        self.size = os.fstat(self.f.fileno()).st_size
        for base in PARTITIONS:
            if base + 33 * SECTOR > self.size:
                continue
            self.f.seek(base + 32 * SECTOR)
            vd = self.f.read(SECTOR)
            if vd[:20] == MAGIC and vd[0x7EC:0x800] == MAGIC:
                self.base = base
                self.root_sector, self.root_size = struct.unpack_from('<II', vd, 20)
                return
        self.f.close()
        raise XisoError('%s: no Xbox file system (XDVDFS) found' % path)

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def read(self, offset, length):
        self.f.seek(offset)
        data = self.f.read(length)
        if len(data) != length:
            raise XisoError('%s: truncated image (%d bytes at %d)' % (self.path, length, offset))
        return data

    def entries(self, sector, size):
        """The entries of one directory, in tree order: (name, attributes, sector, size)."""
        if size == 0:
            return []
        table = self.read(self.base + sector * SECTOR, size)
        out, stack, seen = [], [0], set()
        while stack:
            off = stack.pop()
            if off in seen or off + 14 > len(table):
                raise XisoError('%s: bad directory entry at sector %d + %d' % (self.path, sector, off))
            seen.add(off)
            left, right, first, length, attr, nlen = struct.unpack_from('<HHIIBB', table, off)
            if left == 0xFFFF:
                continue
            out.append((table[off + 14:off + 14 + nlen].decode('latin-1'), attr, first, length))
            if left:
                stack.append(left * 4)
            if right:
                stack.append(right * 4)
        return sorted(out, key=lambda e: e[0].lower())

    def walk(self, sector=None, size=None, prefix=''):
        if sector is None:
            sector, size = self.root_sector, self.root_size
        for name, attr, first, length in self.entries(sector, size):
            path = prefix + name
            if attr & ATTR_DIRECTORY:
                yield from self.walk(first, length, path + '/')
            else:
                yield path, length, self.base + first * SECTOR

    def extract(self, dest, only=(), progress=None):
        """Writes the files (all, or those at or below the paths in only, case-insensitive) below dest.
        A file whose size already matches is kept, so a stopped extraction resumes. Returns (files, bytes)."""
        wanted = [p.strip('/').lower() for p in only]
        files = total = 0
        for path, length, offset in self.walk():
            low = path.lower()
            if wanted and not any(low == w or low.startswith(w + '/') for w in wanted):
                continue
            out = os.path.join(dest, *path.split('/'))
            if os.path.isfile(out) and os.path.getsize(out) == length:
                files += 1
                total += length
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            self.f.seek(offset)
            left = length
            with open(out + '.part', 'wb') as g:
                while left:
                    chunk = self.f.read(min(left, 1 << 22))
                    if not chunk:
                        raise XisoError('%s: truncated image in %s' % (self.path, path))
                    g.write(chunk)
                    left -= len(chunk)
            os.replace(out + '.part', out)
            files += 1
            total += length
            if progress:
                progress(path, length)
        return files, total


def main(argv):
    if len(argv) >= 2 and argv[0] == 'list':
        with Image(argv[1]) as img:
            n = total = 0
            for path, length, _ in img.walk():
                print('%12d  %s' % (length, path))
                n += 1
                total += length
            print('%d files, %d bytes (file system at 0x%x)' % (n, total, img.base))
        return 0
    if len(argv) >= 3 and argv[0] == 'extract':
        with Image(argv[1]) as img:
            n, total = img.extract(argv[2], argv[3:])
        print('%d files, %d bytes -> %s' % (n, total, argv[2]))
        return 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except XisoError as ex:
        print('xiso.py: %s' % ex, file=sys.stderr)
        sys.exit(1)
