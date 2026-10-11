# X-1 helpers for the x1menu4 part of tools/dev/g6-splitscreen.sh (loaded with `pyfile` after g6-checks.py).
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
