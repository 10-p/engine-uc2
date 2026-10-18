#!/usr/bin/env python3
"""Summarise the engine's LODTrace lines (test key [Engine.SkeletalMeshInstance] LODTrace=True; UnSkeletalMesh.cpp).

    lodtrace.py <engine log or harness log> [more logs...]

Per log: the traced draws counted by pass (main scene / the pawn's shadow), mesh, LOD, whether the driver uploaded
the skinned vertices and whether the draw was STALE (one LOD's index buffer drawn over vertices skinned for another
LOD: Lauren's polygons crossing). Last line per log:
    lodtrace <log>: draws=N stale=M main_stale=K shadow_stale=J two_lods_in_a_frame=F
"""
import re
import sys
from collections import Counter, defaultdict

LINE = re.compile(r"LODTrace: tick (\d+) (main|shadow) (\S+) width (\d+) factor ([-\d.]+) lod (\d+) .*?upload (\d+) held (-?\d+)")


def summarise(path):
    rows = Counter()
    lods_per_tick = defaultdict(set)
    draws = stale = main_stale = shadow_stale = 0
    with open(path, "r", encoding="latin-1", errors="replace") as f:
        for line in f:
            m = LINE.search(line)
            if not m:
                continue
            tick, kind, mesh, width, _factor, lod, upload, held = m.groups()
            is_stale = "STALE" in line
            draws += 1
            stale += is_stale
            main_stale += is_stale and kind == "main"
            shadow_stale += is_stale and kind == "shadow"
            rows[(kind, mesh, f"width {width}", f"lod {lod}", "uploaded" if int(upload) else "not uploaded", "STALE" if is_stale else "")] += 1
            lods_per_tick[(tick, mesh)].add(lod)
    two = sum(1 for v in lods_per_tick.values() if len(v) > 1)
    for k, n in sorted(rows.items()):
        print(f"  {n:6d}  " + "  ".join(x for x in k if x))
    print(f"lodtrace {path}: draws={draws} stale={stale} main_stale={main_stale} shadow_stale={shadow_stale} two_lods_in_a_frame={two}")
    return draws, stale


if __name__ == "__main__":
    for p in sys.argv[1:]:
        summarise(p)
