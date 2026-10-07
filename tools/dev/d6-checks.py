# D-6 helpers for tools/dev/d6-mouse.sh scenarios, loaded into m10-inject.py's check namespace with `pyfile`.
# They read P (probes) and W (per-tick watch traces) and append measurements to the run's results file
# ($D6_RESULTS, JSON lines) so that d6-mouse.sh can compare runs (the shipped scripts against ours).
import json, math, os

RESULTS = os.environ.get('D6_RESULTS', '')
UNITS_PER_DEG = 65536.0 / 360.0


def save(key, value):
    """Keep a measurement for the cross-run comparison (and the notebook)."""
    if RESULTS:
        with open(RESULTS, 'a') as f:
            f.write(json.dumps({'key': key, 'value': value}) + '\n')
    print(f'  measured {key} = {value}', flush=True)
    return value


def wrap(v):
    v &= 65535
    return v - 65536 if v >= 32768 else v


def unwrap(values):
    """Rotation samples (0..65535) to a continuous series (each step taken as the short way round)."""
    out = [values[0]]
    for a, b in zip(values, values[1:]):
        out.append(out[-1] + wrap(b - a))
    return out


def per_count(a, b, counts, axis='yaw'):
    """Rotation units per mouse count between probes a and b."""
    d = yawdiff(a, b) if axis == 'yaw' else pitchdiff(a, b)
    return d / float(counts)


def cam_moved(a, b):
    """How far the camera moved between probes a and b (Unreal units)."""
    return math.dist(P[a].cam, P[b].cam)


def circle_stats(tag, first=None, last=None):
    """The view-direction path of a watch trace as a circle: centre = mean of the samples (whole turns),
    radius per sample, relative deviation (max-min)/mean. Yaw is unwrapped, pitch taken as signed."""
    rows = W[tag][first:last]
    yaw = unwrap([r['yaw'] for r in rows])
    pitch = [wrap(r['pitch']) for r in rows]
    cy = sum(yaw) / len(yaw)
    cp = sum(pitch) / len(pitch)
    radii = [math.hypot(y - cy, p - cp) for y, p in zip(yaw, pitch)]
    mean = sum(radii) / len(radii)
    return {'n': len(rows), 'radius': round(mean, 1), 'dev': round((max(radii) - min(radii)) / mean, 4),
            'yaw_extent': max(yaw) - min(yaw), 'pitch_extent': max(pitch) - min(pitch)}


def moving_rows(tag):
    """Index range of a watch trace from the first sample whose rotation differs from the first one to the
    last such sample (the part where the input moved the view)."""
    rows = W[tag]
    r0 = (rows[0]['yaw'], rows[0]['pitch'])
    idx = [i for i, r in enumerate(rows) if (r['yaw'], r['pitch']) != r0]
    return (idx[0] - 1, idx[-1] + 1) if idx else (0, 0)


def rate_curve(tag, marks=(0.1, 0.25, 0.5, 0.75, 1.0), axis='yaw'):
    """A held stick: rotation (units) reached at each time mark after the view started to move, from a watch
    trace (level time)."""
    rows = W[tag]
    vals = unwrap([r[axis] for r in rows])
    start = next((i for i in range(1, len(rows)) if vals[i] != vals[0]), None)
    if start is None:
        return {}
    t0 = rows[start - 1]['t']
    v0 = vals[start - 1]
    out = {}
    for m in marks:
        j = next((i for i in range(start, len(rows)) if rows[i]['t'] - t0 >= m), None)
        if j is not None:
            # linear interpolation between the samples around the mark
            ta, tb = rows[j - 1]['t'] - t0, rows[j]['t'] - t0
            va, vb = vals[j - 1] - v0, vals[j] - v0
            out[str(m)] = round(va + (vb - va) * ((m - ta) / (tb - ta) if tb > ta else 1.0), 1)
    return out


def enemy_off(tag):
    """Yaw between the view and the direction to the locked enemy (rotation units, absolute)."""
    for o in P[tag].others:
        if o['name'] == P[tag].enemy:
            return abs(wrap(P[tag].rot[1] - o['face'][1]))
    return 99999
