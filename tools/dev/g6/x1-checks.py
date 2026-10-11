# X-1 helpers for the x1menu4 and x1armory parts of tools/dev/g6-splitscreen.sh (loaded with `pyfile` after g6-checks.py).
# They read the engine log for the character page's own lines (patch set players34, UC2Arena_Character):
#   "X-1 players34 cursors (<why>): 1P <idx>[ ready]; 2P <idx>; 3P <idx>; 4P <idx>; players <n>"
#   "X-1 players34 text: <line one> | <line two>"
# X1_LOG must be set to the engine log's path (the scenario does it with pyexec).
import re

X1_LOG = None
X1 = {}
CUR_RX = re.compile(r'X-1 players34 cursors \(([^)]*)\):(.*) players (\d+)')
SLOT_RX = re.compile(r'(\d)P (-?\d+)( ready)?;')


def x1_lines():
    with open(X1_LOG, encoding='latin-1', errors='replace') as f:
        return f.read().split('\n')


def x1_cursors():
    """The page's last cursor line: {'why', 'idx': [1P..4P], 'ready': [..], 'n'} (None when there is none)."""
    last = None
    for line in x1_lines():
        m = CUR_RX.search(line)
        if m:
            last = m
    if not last:
        return None
    idx, ready = [None] * 4, [False] * 4
    for s in SLOT_RX.finditer(last.group(2)):
        k = int(s.group(1)) - 1
        idx[k], ready[k] = int(s.group(2)), bool(s.group(3))
    return {'why': last.group(1), 'idx': idx, 'ready': ready, 'n': int(last.group(3))}


def x1_snap(tag):
    X1[tag] = x1_cursors()
    print(f'  x1 cursors {tag}: {X1[tag]}', flush=True)
    return X1[tag] is not None


def x1_only(a, b, k):
    """Between snapshots a and b only player k's cursor (0-3) moved."""
    ia, ib = X1[a]['idx'], X1[b]['idx']
    return ia[k] != ib[k] and all(ia[i] == ib[i] for i in range(4) if i != k)


def x1_text():
    last = None
    for line in x1_lines():
        if 'X-1 players34 text:' in line:
            last = line.split('X-1 players34 text:', 1)[1].strip()
    return last


def x1_count(regex):
    rx = re.compile(regex)
    return sum(1 for line in x1_lines() if rx.search(line))


def x1_oob():
    """The script VM's out-of-bounds lines of the whole run, by array."""
    out = {}
    for line in x1_lines():
        m = re.search(r"Accessed array '(\w+)' out of bounds", line)
        if m:
            out[m.group(1)] = out.get(m.group(1), 0) + 1
    return out


def x1_chosen():
    """The classes players three and four chose ({player number: class}), from the page's lines."""
    out = {}
    for line in x1_lines():
        m = re.search(r'X-1 players34 cursors \(player (\d) port \d+ chose (\S+)\)', line)
        if m:
            out[int(m.group(1))] = m.group(2)
    return out


# X-1 part B: the four-player armory (patch set players34, UC2Arena_QuadArmory) logs
#   "X-1 players34 armory (<why>): 1P <panel>; 2P <panel>; 3P <panel>; 4P <panel>; players <n>"
# <panel>: "none" or "<list in use: x|e|-> <explosive row>/<energy row> sel <explosive>/<energy>[ ready <class>/<class>]"
X1A = {}
ARM_RX = re.compile(r'X-1 players34 armory \(([^)]*)\):(.*) players (\d+)')
PANEL_RX = re.compile(r'(\d)P (none|([xe-]) (-?\d+)/(-?\d+) sel (-?\d+)/(-?\d+)(?: ready (\S+)/(\S+))?);')


def x1a_parse(line):
    m = ARM_RX.search(line)
    if not m:
        return None
    p = [None] * 4
    for s in PANEL_RX.finditer(m.group(2)):
        if s.group(2) != 'none':
            p[int(s.group(1)) - 1] = {'on': s.group(3), 'x': int(s.group(4)), 'e': int(s.group(5)),
                                      'sx': int(s.group(6)), 'se': int(s.group(7)), 'ready': s.group(8) is not None,
                                      'exp': s.group(8), 'en': s.group(9)}
    return {'why': m.group(1), 'p': p, 'n': int(m.group(3))}


def x1a_last():
    last = None
    for line in x1_lines():
        if 'X-1 players34 armory (' in line:
            last = line
    return x1a_parse(last) if last else None


def x1a_snap(tag):
    X1A[tag] = x1a_last()
    print(f'  x1 armory {tag}: {X1A[tag]}', flush=True)
    return X1A[tag] is not None


def x1a_key(q):
    return None if q is None else (q['on'], q['x'], q['e'], q['sx'], q['se'])


def x1a_only(a, b, k):
    """Between armory snapshots a and b only player k's panel (0-3) changed."""
    pa, pb = X1A[a]['p'], X1A[b]['p']
    return x1a_key(pa[k]) != x1a_key(pb[k]) and all(x1a_key(pa[i]) == x1a_key(pb[i]) for i in range(4) if i != k)


def x1a_whys(regex):
    """The 'why' of every armory line matching regex (e.g. which port a line came from)."""
    rx = re.compile(regex)
    return [m.group(1) for m in (ARM_RX.search(l) for l in x1_lines()) if m and rx.search(m.group(1))]


def x1_match_urls():
    """The split players' spawn URLs after the last travel to a match (USplitScreenMgr::CreateSplitPlayer, DevSplitScreen):
    players two, three, four in order."""
    out = []
    for line in x1_lines():
        if re.search(r'Bringing Level (?!Menu\.|Entry\.)\S+ up for play', line):
            out = []
        m = re.search(r"Split player spawn URL:'([^']*)'", line)
        if m:
            out.append(m.group(1))
    return out


X1_NETLOG = None  # the engine's own log file (System/Avaris.log): NETPROBE's lines go there, not to the console


def x1_pris(tag):
    """NETPROBE <tag>'s PlayerReplicationInfo lines: {name: {'energy', 'explosive', 'pawnclass'}}."""
    out = {}
    with open(X1_NETLOG or X1_LOG, encoding='latin-1', errors='replace') as f:
        lines = f.read().split('\n')
    for line in lines:
        m = re.search(r'NetProbe: %s pri name=(\S+) .*pawnclass=(\S+) energy=(\S+) explosive=(\S+)' % re.escape(tag), line)
        if m:
            out[m.group(1)] = {'pawnclass': m.group(2), 'energy': m.group(3), 'explosive': m.group(4)}
    return out


def x1_default_loadout():
    """(explosive, energy) of the first split player's spawn URL on the character page (before any travel to a match):
    a joining player's profile's weapons, i.e. the loadout nobody chose."""
    for line in x1_lines():
        if re.search(r'Bringing Level (?!Menu\.|Entry\.)\S+ up for play', line):
            break
        m = re.search(r"Split player spawn URL:'([^']*)'", line)
        if m:
            o = x1_url_opts(m.group(1))
            return (o.get('ExplosiveWeapon'), o.get('EnergyWeapon'))
    return None


def x1_url_opts(url):
    """A URL's options, the last value of each (as FURL::AddOption keeps it)."""
    out = {}
    for o in url.split('?')[1:]:
        k, _, v = o.partition('=')
        out[k] = v
    return out


def x1_url_has(url, exp, en):
    o = x1_url_opts(url)
    return o.get('ExplosiveWeapon') == exp and o.get('EnergyWeapon') == en
