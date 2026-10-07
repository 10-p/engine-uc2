#!/usr/bin/env python3
"""M10 input scenario runner (used by tools/dev/m10-controls.sh; not run on its own outside Xvfb).

Drives a running uc2.bin with real X events (XTEST through ctypes: xdotool and python3-xlib are not
installed on ion-kubuai) and with the engine's input test channel (-inputtest=<file>, SDL2Drv), waits on
the engine log, and decides PASS/FAIL from log lines and probe state. Usage:

    m10-inject.py <scenario-file> <engine-log> <channel-file> <result-file>

Scenario language, one command per line (# comments):
    wait <regex> [timeout]        wait for a new log line matching <regex> (searched from the last match on)
    expect <name> <regex> [timeout]   like wait, but records check <name> PASS/FAIL and goes on
    absent <name> <regex>         check <name>: no log line since the last 'mark' matches <regex>
    mark                          remember the current log position (for 'absent')
    focus                         find the game window, give it the X input focus
    key <keysym> [hold-seconds]   press and release a key (X keysym names: Return, Down, w, F1 ...)
    keydown <keysym> / keyup <keysym>
    type <text>                   type characters (Shift for capitals)
    move <x> <y>                  absolute pointer position in window coordinates (640x480 design pixels mapped
                                  into the UI box when M10_UIMAP=<W>x<H> is set: D-2)
    rmove <dx> <dy> [steps]       relative pointer motion, split in <steps> events 20 ms apart
    click <button> [x y]          button 1 left, 2 middle, 3 right; 4/5 wheel up/down
    pad <channel command>         e.g. "pad button a 1" (written to the channel as is)
    padpress <button> [hold]      virtual pad button down, hold (0.15 s), up
    padtrigger <axis> [hold]      virtual pad trigger fully in, hold (0.2 s), out
    chan <channel command>        any channel line (exec ..., log ...)
    probe <tag>                   ask the engine for a probe line and keep its fields as <tag>
    seek <max> <expr> ;; <cmd>    probe (as '_') until <expr> holds, running <cmd> in between, at most <max> times
    tryseek <max> <expr> ;; <cmd> the same, but the scenario goes on when <expr> is not reached (a later check says)
    shot <name>                   engine screenshot (SHOT); the shell renames it to <name>.png afterwards
    sleep <seconds>
    check <name> <python expr>    record PASS/FAIL; the expression sees P (probes by tag), yawdiff(a,b),
                                  pitchdiff(a,b), dist(a,b), fwd(a,b), side(a,b) (displacement along/right of a's facing)
    fail_fast                     stop at the first failed check (default: keep going)
"""
import ctypes, re, sys, time, os, math

scen_path, log_path, chan_path, result_path = sys.argv[1:5]

# ---------------------------------------------------------------- X / XTEST
x = ctypes.CDLL("libX11.so.6")
xt = ctypes.CDLL("libXtst.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XDefaultRootWindow.restype = ctypes.c_ulong
x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x.XStringToKeysym.restype = ctypes.c_ulong
x.XKeysymToKeycode.restype = ctypes.c_ubyte
x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                         ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint)]
x.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
x.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
x.XRaiseWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x.XTranslateCoordinates.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_int, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_ulong)]
x.XFlush.argtypes = [ctypes.c_void_p]
x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
xt.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xt.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xt.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
xt.XTestFakeRelativeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]

d = ctypes.c_void_p(x.XOpenDisplay(None))
if not d.value:
    print("m10-inject: no X display", file=sys.stderr)
    sys.exit(2)
root = x.XDefaultRootWindow(d)
win = None
win_origin = (0, 0)


def find_window():
    """The game's top-level window: the named child of the root (Xvfb has no window manager)."""
    r = ctypes.c_ulong(); p = ctypes.c_ulong(); ch = ctypes.POINTER(ctypes.c_ulong)(); n = ctypes.c_uint()
    x.XQueryTree(d, root, ctypes.byref(r), ctypes.byref(p), ctypes.byref(ch), ctypes.byref(n))
    found = None
    for i in range(n.value):
        name = ctypes.c_char_p()
        if x.XFetchName(d, ch[i], ctypes.byref(name)) and name.value:
            found = ch[i]
    return found


def focus():
    global win, win_origin
    for _ in range(100):
        win = find_window()
        if win:
            break
        time.sleep(0.1)
    if not win:
        raise RuntimeError("game window not found")
    x.XRaiseWindow(d, win)
    x.XSetInputFocus(d, win, 2, 0)   # RevertToParent, CurrentTime
    ox = ctypes.c_int(); oy = ctypes.c_int(); c = ctypes.c_ulong()
    x.XTranslateCoordinates(d, win, root, 0, 0, ctypes.byref(ox), ctypes.byref(oy), ctypes.byref(c))
    win_origin = (ox.value, oy.value)
    x.XSync(d, 0)


def keycode(name):
    ks = x.XStringToKeysym(name.encode())
    if not ks:
        raise RuntimeError("unknown keysym " + name)
    return x.XKeysymToKeycode(d, ks)


def key_event(name, down):
    xt.XTestFakeKeyEvent(d, keycode(name), 1 if down else 0, 0)
    x.XFlush(d)


# M10_KEYHOLD=<seconds>: how long `key` holds a key when the scenario gives no time (default 0.12). A press
# and its release inside one engine frame are lost; big windows in software GL (D-2's 1920x1080 runs) have
# frames of half a second on the character page. Below the X server's auto-repeat delay (0.66 s) no repeat.
KEYHOLD = float(os.environ.get('M10_KEYHOLD', '0.12'))
# M10_SLOW=<factor>: every scenario `sleep` is that much longer (default 1). For big windows in software GL,
# where two wheel clicks 0.5 s apart can land in one engine frame and count once (D-2's 1920x1080 mouse run).
SLOW = float(os.environ.get('M10_SLOW', '1'))


def key(name, hold=None):
    key_event(name, True); time.sleep(KEYHOLD if hold is None else hold); key_event(name, False); time.sleep(0.08)


CHARNAMES = {' ': 'space', '.': 'period', '-': 'minus', '_': 'underscore', ',': 'comma'}


def type_text(text):
    for ch in text:
        name = CHARNAMES.get(ch, ch)
        shift = ch.isalpha() and ch.isupper()
        if shift:
            key_event('Shift_L', True)
        key(name.lower() if ch.isalpha() else name, 0.06)
        if shift:
            key_event('Shift_L', False)
        time.sleep(0.05)


# D-2: with the UI in a centred 4:3 box ([SDL2Drv.SDL2Client] UIScaleMode=Fit43), the scenarios' absolute
# positions are 640x480 design pixels; M10_UIMAP=<W>x<H> (the window size; m10-controls.sh sets it from M10_RES)
# maps them into the box the engine draws the menus in: scale s = min(W/640, H/480), origin ((W-640s)/2,
# (H-480s)/2), floored as UCanvas::Update does. Unset, or 640x480: positions are window pixels, as before.
def _uimap():
    v = os.environ.get('M10_UIMAP', '')
    m = re.fullmatch(r'(\d+)x(\d+)', v)
    if not m:
        return (1.0, 0, 0)
    w, h = int(m.group(1)), int(m.group(2))
    s = min(w / 640.0, h / 480.0)
    return (s, int((w - 640 * s) // 2), int((h - 480 * s) // 2))


UIMAP = _uimap()


def ui_to_window(px, py):
    s, bx, by = UIMAP
    return (int(round(bx + px * s)), int(round(by + py * s)))


def move(px, py):
    wx, wy = ui_to_window(px, py)
    xt.XTestFakeMotionEvent(d, -1, win_origin[0] + wx, win_origin[1] + wy, 0)
    x.XFlush(d)


def rmove(dx, dy, steps=1):
    for i in range(steps):
        sx = dx // steps + (1 if i < dx % steps else 0) if dx >= 0 else -((-dx) // steps + (1 if i < (-dx) % steps else 0))
        sy = dy // steps + (1 if i < dy % steps else 0) if dy >= 0 else -((-dy) // steps + (1 if i < (-dy) % steps else 0))
        xt.XTestFakeRelativeMotionEvent(d, sx, sy, 0)
        x.XFlush(d)
        time.sleep(0.02)


def click(button, hold=0.1):
    xt.XTestFakeButtonEvent(d, button, 1, 0); x.XFlush(d); time.sleep(hold)
    xt.XTestFakeButtonEvent(d, button, 0, 0); x.XFlush(d); time.sleep(0.1)


# ---------------------------------------------------------------- engine log and channel
log_lines = []
log_pos = 0          # bytes read so far
search_from = 0      # line index where 'wait' searches start
mark_at = 0
partial = b''


def pump_log():
    global log_pos, partial
    try:
        with open(log_path, 'rb') as f:
            f.seek(log_pos)
            data = f.read()
    except FileNotFoundError:
        return
    log_pos += len(data)
    data = partial + data
    parts = data.split(b'\n')
    partial = parts.pop()
    for p in parts:
        log_lines.append(p.decode('latin-1').rstrip('\r'))


def wait_for(regex, timeout):
    """Index of the first line at/after search_from matching regex, or None."""
    global search_from
    rx = re.compile(regex)
    end = time.time() + timeout
    i = search_from
    while True:
        pump_log()
        while i < len(log_lines):
            if rx.search(log_lines[i]):
                search_from = i + 1
                return i
            i += 1
        if time.time() > end:
            return None
        time.sleep(0.05)


def chan(line):
    with open(chan_path, 'a') as f:
        f.write(line + '\n')


PROBE_RX = re.compile(r'probe (\S+) level=(\S+) menu=(\S+) focus=(\S+) text="(.*)" item="(.*)" paused=(\d) pawn=(\d) '
                      r'loc=\(([-\d.]+),([-\d.]+),([-\d.]+)\) rot=\((-?\d+),(-?\d+),(-?\d+)\) weapon=(\S+) '
                      r'ammo=([-\d.]+) shots=(\d+) mouse=\(([-\d.]+),([-\d.]+)\) pads=(\d+)')


# D-7/D-8: fields after pads= (console typing line, scoreboard, the player's FOV, viewport size); absent from
# an older engine, then they read -1 / '' / 0.
PROBE_EXTRA_RX = re.compile(r'console=(-?\d+) typed="(.*)" scores=(-?\d+) fov=([-\d.]+) desiredfov=([-\d.]+) view=(\d+)x(\d+)')


class Probe:
    def __init__(self, m):
        (self.tag, self.level, self.menu, self.focus, self.text, self.item, paused, pawn, lx, ly, lz, rp, ry, rr,
         self.weapon, ammo, shots, mx, my, pads) = m.groups()
        self.paused = int(paused); self.pawn = int(pawn)
        self.loc = (float(lx), float(ly), float(lz)); self.rot = (int(rp), int(ry), int(rr))
        self.ammo = float(ammo); self.shots = int(shots); self.mouse = (float(mx), float(my)); self.pads = int(pads)
        x = PROBE_EXTRA_RX.search(m.string)
        self.console = int(x.group(1)) if x else -1
        self.typed = x.group(2) if x else ''
        self.scores = int(x.group(3)) if x else -1
        self.fov = float(x.group(4)) if x else 0.0
        self.desiredfov = float(x.group(5)) if x else 0.0
        self.view = (int(x.group(6)), int(x.group(7))) if x else (0, 0)

    def __repr__(self):
        return (f"level={self.level} menu={self.menu} focus={self.focus} text={self.text!r} item={self.item!r} paused={self.paused} pawn={self.pawn} loc={self.loc} rot={self.rot} weapon={self.weapon} ammo={self.ammo} shots={self.shots}"
                f" console={self.console} typed={self.typed!r} scores={self.scores} fov={self.fov} desiredfov={self.desiredfov} view={self.view}")


P = {}


def probe(tag):
    chan('probe ' + tag)
    i = wait_for(r'probe ' + re.escape(tag) + ' level=', 20)
    if i is None:
        raise RuntimeError('no probe line for ' + tag)
    m = PROBE_RX.search(log_lines[i])
    if not m:
        raise RuntimeError('unparsable probe line: ' + log_lines[i])
    P[tag] = Probe(m)
    print(f"  probe {tag}: {P[tag]}", flush=True)


def yawdiff(a, b):
    """Signed yaw change from probe a to probe b, in rotation units, wrapped to -32768..32767."""
    v = (P[b].rot[1] - P[a].rot[1]) & 65535
    return v - 65536 if v >= 32768 else v


def pitchdiff(a, b):
    v = (P[b].rot[0] - P[a].rot[0]) & 65535
    return v - 65536 if v >= 32768 else v


def dist(a, b):
    return math.dist(P[a].loc, P[b].loc)


def fwd(a, b):
    """Horizontal displacement from probe a to b along a's facing (yaw)."""
    yaw = P[a].rot[1] * math.pi / 32768.0
    return (P[b].loc[0] - P[a].loc[0]) * math.cos(yaw) + (P[b].loc[1] - P[a].loc[1]) * math.sin(yaw)


def side(a, b):
    """Horizontal displacement from probe a to b to a's right."""
    yaw = P[a].rot[1] * math.pi / 32768.0
    return -(P[b].loc[0] - P[a].loc[0]) * math.sin(yaw) + (P[b].loc[1] - P[a].loc[1]) * math.cos(yaw)


# ---------------------------------------------------------------- run the scenario
results = []
fail_fast = False


def record(name, ok, detail=''):
    results.append((name, ok, detail))
    print(f"  {'PASS' if ok else 'FAIL'} {name} {detail}", flush=True)
    if not ok and fail_fast:
        raise RuntimeError('fail_fast: ' + name)


def run():
    for raw in open(scen_path):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        print(f"> {line}", flush=True)
        execute(line)


def evaluate(expr):
    return eval(expr, {'P': P, 'yawdiff': yawdiff, 'pitchdiff': pitchdiff, 'dist': dist, 'fwd': fwd, 'side': side,
                       'abs': abs, 'min': min, 'max': max})


def execute(line):
    global fail_fast, mark_at
    cmd, _, rest = line.partition(' ')
    if True:
        if cmd in ('seek', 'tryseek'):
            # seek <max> <expr> ;; <command>: probe as '_', stop when <expr> holds, else run <command>
            # tryseek: the same, but not reaching <expr> only prints a note and the scenario goes on (D-8)
            head, _, action = rest.partition(';;')
            limit, _, expr = head.strip().partition(' ')
            for _ in range(int(limit) + 1):
                probe('_')
                if evaluate(expr):
                    return
                execute(action.strip())
                time.sleep(0.3)
            if cmd == 'tryseek':
                print(f'  tryseek: {expr} not reached in {limit} steps; going on', flush=True)
                return
            raise RuntimeError(f'seek: {expr} not reached in {limit} steps')
        elif cmd == 'wait':
            parts = rest.rsplit(' ', 1)
            regex, timeout = (parts[0], float(parts[1])) if len(parts) == 2 and re.fullmatch(r'[\d.]+', parts[1]) else (rest, 30.0)
            if wait_for(regex, timeout) is None:
                raise RuntimeError(f'timed out waiting for /{regex}/')
        elif cmd == 'expect':
            name, _, rest2 = rest.partition(' ')
            parts = rest2.rsplit(' ', 1)
            regex, timeout = (parts[0], float(parts[1])) if len(parts) == 2 and re.fullmatch(r'[\d.]+', parts[1]) else (rest2, 30.0)
            i = wait_for(regex, timeout)
            record(name, i is not None, f'/{regex}/' + ('' if i is None else f' -> {log_lines[i].strip()[:150]}'))
        elif cmd == 'mark':
            pump_log(); mark_at = len(log_lines)
        elif cmd == 'absent':
            name, _, regex = rest.partition(' ')
            pump_log()
            hits = [l for l in log_lines[mark_at:] if re.search(regex, l)]
            record(name, not hits, f'/{regex}/' + (f' found: {hits[0].strip()[:150]}' if hits else ' not found'))
        elif cmd == 'focus':
            focus()
        elif cmd == 'key':
            parts = rest.split()
            key(parts[0], float(parts[1]) if len(parts) > 1 else None)
        elif cmd == 'keydown':
            key_event(rest, True)
        elif cmd == 'keyup':
            key_event(rest, False)
        elif cmd == 'type':
            type_text(rest)
        elif cmd == 'move':
            a, b = rest.split(); move(int(a), int(b))
        elif cmd == 'rmove':
            parts = rest.split(); rmove(int(parts[0]), int(parts[1]), int(parts[2]) if len(parts) > 2 else 1)
        elif cmd == 'click':
            parts = rest.split()
            if len(parts) == 3:
                move(int(parts[1]), int(parts[2])); time.sleep(0.15)
            click(int(parts[0]))
        elif cmd == 'pad':
            chan('pad ' + rest)
        elif cmd == 'padpress':
            parts = rest.split()
            chan(f'pad button {parts[0]} 1'); time.sleep(float(parts[1]) if len(parts) > 1 else 0.15)
            chan(f'pad button {parts[0]} 0'); time.sleep(0.15)
        elif cmd == 'padtrigger':
            parts = rest.split()
            chan(f'pad axis {parts[0]} 32767'); time.sleep(float(parts[1]) if len(parts) > 1 else 0.2)
            chan(f'pad axis {parts[0]} 0'); time.sleep(0.15)
        elif cmd == 'chan':
            chan(rest)
        elif cmd == 'probe':
            probe(rest)
        elif cmd == 'shot':
            chan('exec shot')
            with open(result_path + '.shots', 'a') as f:
                f.write(rest + '\n')
            time.sleep(0.5)
        elif cmd == 'sleep':
            time.sleep(float(rest) * SLOW)
        elif cmd == 'check':
            name, _, expr = rest.partition(' ')
            try:
                ok = bool(evaluate(expr))
                detail = expr
            except Exception as e:
                ok, detail = False, f'{expr} -> {e!r}'
            record(name, ok, detail)
        elif cmd == 'fail_fast':
            fail_fast = True
        else:
            raise RuntimeError('unknown scenario command: ' + line)


status = 0
try:
    run()
except Exception as e:
    record('scenario-complete', False, str(e))
    status = 1
with open(result_path, 'w') as f:
    for name, ok, detail in results:
        f.write(f"{'PASS' if ok else 'FAIL'}\t{name}\t{detail}\n")
sys.exit(status)
