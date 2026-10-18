#!/usr/bin/env python3
"""Compare the script layout the engine computes at run time with the C++ layout the compiler chose.

    ./ucc-bin verifyclasses DUMP=layout.txt                  # (in a run tree) the linked layout of every class
                                                              # and script struct: size, offset of each property
    python3 tools/dev/dwarf-layouts.py ucc-bin > dwarf.txt   # the C++ layout of every type in the binary
    python3 tools/dev/m13-layout-check.py layout.txt dwarf.txt

For every script class X it looks for the C++ type AX or UX, for every script struct X for FX, for every function F
of class X the event parameter struct AX_eventF_Parms / UX_eventF_Parms, and compares the
total size and the offset of every property that has a C++ member of the same name (bool properties: the
32-bit word that holds the bit). Native placeholders (script ints standing in for C++ data, UnClass.cpp) have no
C++ member of their name; the offsets after them prove their size. Prints one line per mismatch and a summary;
exit status 1 if anything mismatches. Script types with no C++ type (pure script classes and structs) are
counted, not checked: native code never sees them. `verifyclasses` itself checks sizes and the generated-header
member lists in the binary; this adds the hand-written headers and every script struct.
"""
import re, sys, collections

def load_dwarf(path):
    size = collections.defaultdict(set)
    members = collections.defaultdict(lambda: collections.defaultdict(set))
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r'^(\S+) size (\S+)$', line)
        if m:
            try: size[m.group(1)].add(int(m.group(2), 0))
            except ValueError: pass
            continue
        m = re.match(r'^(\S+)\.([^.\s]+) off (\S+) bit (\S+)/(\S+)$', line)
        if m:
            t, mem, off, bit, bits = m.groups()
            if off != "None":
                o = int(off, 0)
                if bits != "None" and bit != "None":     # DWARF 4 bitfield: storage unit + bit offset
                    o = o + (int(bit, 0) // 32) * 4 if int(bit, 0) >= 32 else o
                members[t][mem].add(o)
            elif bit != "None":                         # DWARF 5 bitfield: DW_AT_data_bit_offset
                members[t][mem].add((int(bit, 0) // 32) * 4)
    return size, members

def load_dump(path):
    types = []
    cur = None
    for line in open(path, errors="replace"):
        f = line.split()
        if not f: continue
        if f[0] == "S":
            cur = {"kind": f[1], "path": f[2], "size": int(f[3]), "native": int(f[4]), "props": []}
            types.append(cur)
        elif f[0] == "P" and cur is not None:
            cur["props"].append({"name": f[1], "off": int(f[2]), "elem": int(f[3]), "dim": int(f[4]),
                                 "type": f[5], "bitmask": int(f[6]), "inner": int(f[7]), "placeholder": int(f[8])})
    return types

def main():
    if len(sys.argv) != 3:
        print(__doc__); return 2
    types = load_dump(sys.argv[1])
    dsize, dmem = load_dwarf(sys.argv[2])
    stats = collections.Counter()
    bad = 0
    for t in types:
        name = t["path"].split(".")[-1]
        if t["kind"] == "function":   # an event's C++ parameter struct, e.g. AActor_eventTouch_Parms
            owner = t["path"].split(".")[-2]
            cands = [f"{pre}{owner}_event{name}_Parms" for pre in ("A", "U")]
        else:
            cands = ["A" + name, "U" + name] if t["kind"] == "class" else ["F" + name]
        cpp = [c for c in cands if c in dsize]
        if not cpp:
            stats[t["kind"] + " without C++ type"] += 1
            continue
        if len(cpp) > 1:   # AX and UX both exist: prefer the one whose members match by name
            cpp = sorted(cpp, key=lambda c: -sum(1 for p in t["props"] if p["name"] in dmem[c]))
        c = cpp[0]
        stats[t["kind"] + " checked"] += 1
        sizes = dsize[c]
        # A parameter struct only has to place each parameter where the function's frame has it: the frame is
        # the script size (C++ may pad its tail), so only offsets are compared for functions.
        if t["kind"] != "function" and t["size"] not in sizes:
            # A class's PropertiesSize includes the script tail; C++ may differ only by its own non-script tail.
            print(f"SIZE   {t['kind']} {t['path']}: script {t['size']}, C++ {c} {sorted(sizes)}")
            bad += 1
        for p in t["props"]:
            offs = dmem[c].get(p["name"])
            if not offs:
                stats["property without C++ member" + (" (placeholder)" if p["placeholder"] else "")] += 1
                continue
            stats["property checked"] += 1
            want = p["off"]
            if p["type"] == "BoolProperty":
                if want not in offs:
                    print(f"OFFSET {t['path']}.{p['name']} (bool): script {want}, C++ {c} {sorted(offs)}"); bad += 1
            elif want not in offs:
                print(f"OFFSET {t['path']}.{p['name']}: script {want}, C++ {c} {sorted(offs)}"); bad += 1
    for k in sorted(stats): print(f"  {k}: {stats[k]}")
    print(f"layout-check: {bad} mismatches")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
