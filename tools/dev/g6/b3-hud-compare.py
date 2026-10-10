#!/usr/bin/env python3
"""B-3: the split HUD's placement of several runs against a reference run (docs/porting/analysis/split-hud.md).

    b3-hud-compare.py <ref-label>=<dir>[:<mode>] <label>=<dir>[:<mode>] ...

Each <dir> holds a run of `tools/dev/g6-splitscreen.sh <tree> hud` (b3-hud-measure.py's b3-hud-measure.json, or the
pictures, measured here with <mode>, default Fit43). The first is the reference (Epic's shipped scripts at 640x480,
UIScaleMode=Native640: the Xbox's own screen). Prints, per layout and per viewport (in the split-screen manager's
order), each element's top-left corner in design pixels for every run, the largest difference from the reference,
and the reticle's centre against the viewport's centre; then PASS/FAIL per run: every element of 2 players within
TOL design pixels of the reference (3 and 4 players are reported, not counted: Epic's scripts lay out halves only).

Epic's intent, from the scripts (UCHUD_Split.uc, UCHUD.SetSplitPosition): the health fill pos_HealthFill X 39..165,
Y 33..50 in the 640x240 half, moved by SafeZoneOffsetY = 0 (client viewport index 0) or -29 (index 1); the reticle's
anchor at (320, 240*0.5 = 120), the centre of the half.
"""
import json, os, subprocess, sys

TOL = 1.5
HERE = os.path.dirname(os.path.abspath(__file__))
ELEMS = ['health', 'weapons', 'score', 'reticle', 'all']


def load(spec):
    label, rest = spec.split('=', 1)
    d, _, mode = rest.partition(':')
    mode = mode or 'Fit43'
    r = subprocess.run([sys.executable, os.path.join(HERE, 'b3-hud-measure.py'), '--json', d, mode],
                       capture_output=True, text=True, check=True)
    return label, json.loads(r.stdout)


def intent(tag, v):
    if tag == 'n1' or v['design'][1] > 240:
        return None
    off = {0: 0, 1: -29}.get(v['i'], 0)
    return {'health': (39 + 0, 33 + off), 'reticle_anchor': (320, 120)}


def main():
    runs = [load(s) for s in sys.argv[1:]]
    ref_label, ref = runs[0]
    fails = {label: 0 for label, _ in runs[1:]}
    worst = {label: 0.0 for label, _ in runs[1:]}
    for tag in ['n1', 'n2', 'n3', 'n4']:
        if tag not in ref:
            continue
        print(f"== {tag} ({len(ref[tag]['views'])} viewport(s)); reference {ref_label} "
              f"{ref[tag]['window'][0]}x{ref[tag]['window'][1]} {ref[tag]['mode']}")
        for k, rv in enumerate(ref[tag]['views']):
            want = intent(tag, rv)
            print(f"  split {rv['split']} (client index {rv['i']}), reference rect {tuple(rv['rect'])}"
                  + (f"; Epic's intent: health fill top-left {want['health']}, reticle anchor {want['reticle_anchor']}"
                     if want else ''))
            for e in ELEMS:
                cells = []
                for label, run in runs:
                    vs = run.get(tag, {}).get('views', [])
                    v = vs[k] if k < len(vs) else None
                    b = v.get(e) if v else None
                    cell = f"{label} " + (f"({b[0]:.1f},{b[1]:.1f}) {b[2]-b[0]:.0f}x{b[3]-b[1]:.0f}" if b else '-')
                    rb = rv.get(e)
                    if label != ref_label and b and rb:
                        d = max(abs(b[0] - rb[0]), abs(b[1] - rb[1]))
                        cell += f" d={d:.1f}"
                        if e != 'reticle' and e != 'all' and tag == 'n2':
                            worst[label] = max(worst[label], d)
                            if d > TOL:
                                fails[label] += 1
                    elif label != ref_label and bool(b) != bool(rb):
                        cell += ' MISSING'
                        if tag == 'n2':
                            fails[label] += 1
                    cells.append(cell)
                print(f"    {e:8s} " + ' | '.join(cells))
            cells = []
            for label, run in runs:
                vs = run.get(tag, {}).get('views', [])
                v = vs[k] if k < len(vs) else None
                if v and v.get('reticle_off_design'):
                    o = v['reticle_off_design']
                    cells.append(f"{label} ({o[0]:+.2f},{o[1]:+.2f}) design = ({v['reticle_off_px'][0]:+.1f},"
                                 f"{v['reticle_off_px'][1]:+.1f}) px")
                    if label != ref_label and tag == 'n2' and max(abs(o[0]), abs(o[1])) > TOL:
                        fails[label] += 1
                else:
                    cells.append(f"{label} -")
            print(f"    reticle centre - viewport centre: " + ' | '.join(cells))
    for label, _ in runs[1:]:
        print(f"{'PASS' if fails[label] == 0 else 'FAIL'} b3-hud-{label}-as-{ref_label}: two players' health, weapons, "
              f"score within {TOL} design px of the reference (worst {worst[label]:.1f}), reticle within {TOL} of the "
              f"viewport centre; {fails[label]} outside")
    return 1 if any(fails.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
