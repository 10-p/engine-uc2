#!/usr/bin/env python3
"""Merge the compiled Win32 resources (.res) of several packages into one, for one statically linked executable.

    python3 tools/merge-res.py -o <merged.res> <first.res> <second.res> ...

Each package's .rc was written for its own DLL, so their resources clash once they share one module:
- windres numbers the images of every ICON and CURSOR from 1 in each script; here they are renumbered so that every
  image keeps a number of its own, and each GROUP_ICON / GROUP_CURSOR directory is updated to match;
- a named resource (dialog, bitmap, icon group, ...) that a later input also defines, with the same type, name and
  language, is taken from the later input; each one replaced is reported on stderr.
The output is a .res that `windres -J res -O coff` turns into the object the executable links.
"""
import argparse
import struct
import sys

RT_CURSOR, RT_ICON, RT_GROUP_CURSOR, RT_GROUP_ICON = 1, 3, 12, 14
TYPE_NAMES = {1: "CURSOR", 2: "BITMAP", 3: "ICON", 4: "MENU", 5: "DIALOG", 6: "STRING", 9: "ACCELERATOR",
              12: "GROUP_CURSOR", 14: "GROUP_ICON", 16: "VERSION", 240: "DLGINIT", 241: "TOOLBAR"}


def read_id(buf, pos):
    """A resource type or name: an ordinal (0xFFFF, value) or a NUL-terminated UTF-16 string."""
    (first,) = struct.unpack_from("<H", buf, pos)
    if first == 0xFFFF:
        return struct.unpack_from("<H", buf, pos + 2)[0], pos + 4
    end = pos
    while struct.unpack_from("<H", buf, end)[0] != 0:
        end += 2
    return buf[pos:end].decode("utf-16-le"), end + 2


def write_id(value):
    if isinstance(value, int):
        return struct.pack("<HH", 0xFFFF, value)
    return value.encode("utf-16-le") + b"\0\0"


def read_res(path):
    """The entries of a .res file: (type, name, data version, memory flags, language, version, characteristics, data)."""
    buf = open(path, "rb").read()
    pos, entries = 0, []
    while pos + 8 <= len(buf):
        data_size, header_size = struct.unpack_from("<II", buf, pos)
        rtype, p = read_id(buf, pos + 8)
        name, p = read_id(buf, p)
        p = (p + 3) & ~3
        data_version, flags, lang, version, chars = struct.unpack_from("<IHHII", buf, p)
        data = buf[pos + header_size:pos + header_size + data_size]
        # The first entry of every .res is the empty marker (type 0, name 0, no data).
        if not (rtype == 0 and name == 0 and data_size == 0):
            entries.append([rtype, name, data_version, flags, lang, version, chars, data])
        pos = (pos + header_size + data_size + 3) & ~3
    return entries


def write_res(path, entries):
    out = bytearray(struct.pack("<II", 0, 32) + write_id(0) + write_id(0) + struct.pack("<IHHII", 0, 0, 0, 0, 0))
    for rtype, name, data_version, flags, lang, version, chars, data in entries:
        ids = write_id(rtype) + write_id(name)
        ids += b"\0" * ((-(8 + len(ids))) % 4)
        header = ids + struct.pack("<IHHII", data_version, flags, lang, version, chars)
        out += struct.pack("<II", len(data), 8 + len(header)) + header + data
        out += b"\0" * ((-len(out)) % 4)
    open(path, "wb").write(out)


def renumber_group(data, mapping):
    """GROUP_ICON / GROUP_CURSOR: NEWHEADER (6 bytes), then 14-byte entries whose last WORD is the image's number."""
    data = bytearray(data)
    (count,) = struct.unpack_from("<H", data, 4)
    for i in range(count):
        at = 6 + i * 14 + 12
        (old,) = struct.unpack_from("<H", data, at)
        struct.pack_into("<H", data, at, mapping[old])
    return bytes(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("inputs", nargs="+")
    args = ap.parse_args()

    merged = {}				# (type, name, language) -> (input, entry)
    order = []
    next_image = {RT_ICON: 1, RT_CURSOR: 1}
    for path in args.inputs:
        entries = read_res(path)
        # This input's image numbers, mapped to numbers not used by an earlier input.
        mapping = {RT_ICON: {}, RT_CURSOR: {}}
        for e in entries:
            if e[0] in mapping and isinstance(e[1], int):
                mapping[e[0]][e[1]] = next_image[e[0]]
                next_image[e[0]] += 1
        for e in entries:
            if e[0] in mapping and isinstance(e[1], int):
                e[1] = mapping[e[0]][e[1]]
            elif e[0] == RT_GROUP_ICON:
                e[7] = renumber_group(e[7], mapping[RT_ICON])
            elif e[0] == RT_GROUP_CURSOR:
                e[7] = renumber_group(e[7], mapping[RT_CURSOR])
            key = (e[0], e[1], e[4])
            if key in merged:
                print("merge-res: %s %s (language 0x%04x) of %s replaced by %s's" % (
                    TYPE_NAMES.get(e[0], e[0]), e[1], e[4], merged[key][0], path), file=sys.stderr)
            else:
                order.append(key)
            merged[key] = (path, e)
    write_res(args.output, [merged[k][1] for k in order])
    return 0


if __name__ == "__main__":
    sys.exit(main())
