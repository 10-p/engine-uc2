#!/usr/bin/env python3
"""check-headers-cpptext.py: compare a committed generated header with one exported from a PATCHED script tree (T3).

  check-headers-cpptext.py <committed.h> <exported.h> <label>     exit 0 equal, 1 different (unified diff on stdout)
  check-headers-cpptext.py --count <committed.h> <exported.h>     what was taken over: "<cpptext> cpptext, <structs>
                                                                  native structs"
  check-headers-cpptext.py --merge <committed.h> <exported.h> <out.h>
                                                                  the committed header with the export's changes
                                                                  (regen-headers on a patched tree, T4): every line
                                                                  the comparison sees comes from the export, the
                                                                  cpptext bodies and native struct blocks the export
                                                                  cannot have stay the committed ones

The patched tree is decompiled from the shipped packages, and two things the header exporter writes are not in them:
- a class's cpptext (the C++ declarations it adds to its header; stripped before Epic shipped). The decompiler writes
  `cpptext { // stripped from the shipped package }`; the exporter drops the comment, so the class's body after its
  DECLARE_CLASS(...) line is blank lines only. For such a class the committed body (same class, same place) is taken.
- `struct native` / `struct export` (UStruct::StructFlags is not serialised; the exporter writes a script struct into
  the header only with one of them). A `struct <PKG>_API F<Name> {...};` block of the committed header whose struct the
  exported header does not have at all is left out of the comparison.
Everything else (class members, offsets, events, natives, enums, constants, the other structs, the registration
tables) must be byte-identical. CRs are dropped (the exporter writes CRLF, the repo LF). A patch that changes a
native struct or a cpptext is therefore NOT checked by this (nor written by --merge): such a change is made in the
committed header by hand, with the patch (a full-source check needs Epic's leaked text: dev machine only).
"""
import difflib
import re
import sys

MARKER = '// stripped from the shipped package'
DECL = re.compile(r'^\s+DECLARE_CLASS\((\w+),')


def lines(path):
    with open(path, 'rb') as f:
        return f.read().decode('latin-1').replace('\r', '').split('\n')


def bodies(ls):
    """{class: (first, end)}: the lines after its DECLARE_CLASS up to (not including) its closing `};`."""
    out = {}
    i = 0
    while i < len(ls):
        m = DECL.match(ls[i])
        if m:
            j = i + 1
            while j < len(ls) and ls[j] != '};':
                j += 1
            out[m.group(1)] = (i + 1, j)
            i = j
        i += 1
    return out


STRUCT = re.compile(r'^struct \w+_API (F\w+)$')


def struct_blocks(ls):
    """{name: (first, end)} of the `struct <PKG>_API F<Name>` blocks, end after the `};` and its blank line."""
    out = {}
    i = 0
    while i < len(ls):
        m = STRUCT.match(ls[i])
        if m:
            j = i + 1
            while j < len(ls) and ls[j] != '};':
                j += 1
            j += 1
            if j < len(ls) and ls[j] == '':
                j += 1
            out[m.group(1)] = (i, j)
            i = j
            continue
        i += 1
    return out


def merge(a, b):
    """The committed header a with the changes of the export b: a's lines where the comparison finds them equal,
    b's where it does not; a's native struct blocks that b lacks stay in place. Returns the new lines."""
    import difflib as _d
    a2, b2, _, _ = take_over(a, b)
    have = set(struct_blocks(b))
    drop = [(s, e) for name, (s, e) in struct_blocks(a).items() if name not in have]
    idx = [i for i in range(len(a)) if not any(s <= i < e for s, e in drop)]
    assert len(idx) == len(a2)
    out, pa = [], 0
    for tag, i1, i2, j1, j2 in _d.SequenceMatcher(None, a2, b2, autojunk=False).get_opcodes():
        if tag == 'equal':
            out += a[pa:idx[i2 - 1] + 1]
            pa = idx[i2 - 1] + 1
            continue
        if i2 > i1:     # replace / delete: a's dropped lines inside the range stay, before the new text
            out += [a[k] for k in range(pa, idx[i2 - 1] + 1) if k not in idx[i1:i2]]
            pa = idx[i2 - 1] + 1
        else:           # insert before a2[i1]: a's dropped lines before it first
            stop = idx[i1] if i1 < len(idx) else len(a)
            out += a[pa:stop]
            pa = stop
        out += b2[j1:j2]
    out += a[pa:]
    return out


def take_over(a, b):
    """(a without the native struct blocks b lacks, b with every stripped cpptext body replaced by a's body of the same
    class, how many cpptext bodies, how many struct blocks)."""
    ba, bb = bodies(a), bodies(b)
    n = 0
    out, pos = [], 0
    for cls, (s, e) in sorted(bb.items(), key=lambda kv: kv[1][0]):
        if e == s or any(x.strip() for x in b[s:e]) or cls not in ba or ba[cls][1] == ba[cls][0]:
            continue
        sa, ea = ba[cls]
        out += b[pos:s] + a[sa:ea]
        pos = e
        n += 1
    b = out + b[pos:]
    # native structs: the committed header without the blocks of structs the exported one does not have
    have = set(struct_blocks(b))
    drop = [(s, e) for name, (s, e) in struct_blocks(a).items() if name not in have]
    keep = [x for i, x in enumerate(a) if not any(s <= i < e for s, e in drop)]
    return keep, b, n, len(drop)


def main():
    args = sys.argv[1:]
    if args[:1] == ['--merge']:
        if len(args) != 4:
            sys.stderr.write(__doc__)
            return 2
        a, b = lines(args[1]), lines(args[2])
        m = merge(a, b)
        a2, b2, _, _ = take_over(m, b)
        if a2 != b2:
            sys.stderr.write('check-headers-cpptext: --merge of %s did not converge\n' % args[1])
            return 1
        with open(args[3], 'wb') as f:
            f.write('\n'.join(m).encode('latin-1'))
        return 0
    count = args[:1] == ['--count']
    if count:
        args = args[1:]
    if len(args) < 2:
        sys.stderr.write(__doc__)
        return 2
    a, b = lines(args[0]), lines(args[1])
    a2, b2, n, ns = take_over(a, b)
    if count:
        print('%d cpptext, %d native structs' % (n, ns))
        return 0
    if a2 == b2:
        return 0
    label = args[2] if len(args) > 2 else args[0]
    sys.stdout.write('\n'.join(difflib.unified_diff(a2, b2, 'Source/%s (committed)' % label,
                                                    'Source/%s (exported from the patched scripts)' % label,
                                                    lineterm='')) + '\n')
    return 1


if __name__ == '__main__':
    sys.exit(main())
