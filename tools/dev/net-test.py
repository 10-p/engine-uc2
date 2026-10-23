#!/usr/bin/env python3
"""Network play tests: servers and clients of the native build on 127.0.0.1 (driven by tools/dev/net-test.sh,
which runs this inside one bin/capped scope; not meant to run on its own).

    net-test.py <run-tree System dir> <out dir> <check> [<check> ...]

Every process runs from the same System/ with its own ini, user ini and log (INI=, USERINI=, LOG=: Epic's
appInit / FOutputDeviceFile options) and its own command files:
  [UC2NetTest] CommandFile=<file> (-ini:)  the engine's test command file (Engine/Src/UC2NetTest.cpp): "exec netprobe <tag>",
                     "exec netplacenear ...", "exec servertravel ..." -- every process has one
  [SDL2Drv.SDL2Client] InputTestFile=<file> (-ini:)  SDL2Drv's input test channel (clients and the listen server): the virtual pad
Each client and the listen server has its own Xvfb display (xvfb-run -a), so a virtual pad never depends on
which window has the focus; NET_VIDEO=offscreen runs them on SDL's offscreen video instead (no X server).
NET_SERVER_SYS=<System dir> runs the dedicated server from another run tree (a server on one game folder, the
clients on another). Results: one "PASS|FAIL <check>.<item>: <details>" line per item on stdout and in
<out>/net-summary.txt; logs as <out>/<check>-<proc>.log/.out. Decided by log lines and probe values only.
"""
import shutil
import math, os, re, signal, subprocess, sys, time

SYS, OUT = sys.argv[1], sys.argv[2]
CHECKS = sys.argv[3:]
SUMMARY = os.path.join(OUT, "net-summary.txt")
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WINSYS = os.environ.get("NET_WIN_SYS", os.path.join(os.path.dirname(os.path.dirname(SYS)), "netwin", "System"))
PORT = int(os.environ.get("NET_PORT", "7850"))
SERVER_SYS = os.environ.get("NET_SERVER_SYS") or None
OFFSCREEN = os.environ.get("NET_VIDEO") == "offscreen"
results = []


def result(ok, name, details):
    line = "%s %s: %s" % ("PASS" if ok else "FAIL", name, details)
    print(line, flush=True)
    with open(SUMMARY, "a") as f:
        f.write(line + "\n")
    results.append(ok)
    return ok


def num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def parse_kv(text):
    d = {}
    for m in re.finditer(r'(\w+)=(\([^)]*\)|"[^"]*"|\S+)', text):
        v = m.group(2)
        if v.startswith("("):
            v = tuple(float(x) for x in v[1:-1].split(","))
        d[m.group(1)] = v
    return d


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


class Proc:
    """One engine process: kind 'server' (ucc-bin server), 'listen' (uc2.bin <map>?Listen) or 'client'."""

    def __init__(self, check, name, kind, url, render="null", extra=(), sysdir=None, wine=False, cheatkey=True, ini_add=None):
        self.check, self.name, self.kind, self.url = check, name, kind, url
        self.cheatkey = cheatkey
        # ini_add: {section: [line, ...]} appended to a copy of Default.ini that becomes this process's own ini (for
        # keys that repeat, ServerPackages= or Paths=, which an -ini: override cannot add: it sets the first one).
        self.ini_add = ini_add
        self.render, self.extra = render, list(extra)
        self.wine = wine
        self.sys = sysdir or SYS
        self.logname = "Net%s.log" % name
        self.log = os.path.join(self.sys, self.logname)
        self.netcmd = os.path.join(self.sys, "net-%s.cmd" % name)
        self.inputcmd = os.path.join(self.sys, "net-%s-input.cmd" % name)
        self.pos = 0
        self.popen = None

    def start(self):
        for f in ("Net%s.ini" % self.name, "Net%sUser.ini" % self.name, self.logname):
            p = os.path.join(self.sys, f)
            if os.path.exists(p):
                os.remove(p)
        if self.ini_add:
            with open(os.path.join(self.sys, "Default.ini"), "rb") as f:
                lines = f.read().decode("latin-1").split("\n")
            for section, add in self.ini_add.items():
                head = lines.index("[%s]" % section)
                end = next((k for k in range(head + 1, len(lines)) if lines[k].startswith("[")), len(lines))
                while end > head + 1 and not lines[end - 1].strip():
                    end -= 1
                lines[end:end] = add
            with open(os.path.join(self.sys, "Net%s.ini" % self.name), "wb") as f:
                f.write("\n".join(lines).encode("latin-1"))
        open(self.netcmd, "w").close()
        open(self.inputcmd, "w").close()
        # NETPROBE/NETPLACE/NETPLACENEAR are cheats (Source/Engine/Src/UC2Cheats.cpp): a client and a
        # dedicated server refuse them unless the process's own ini allows them for tests (a listen host needs no
        # key). The players are lined up around the shooter's own view (line_up).
        common = ["INI=Net%s.ini" % self.name, "USERINI=Net%sUser.ini" % self.name, "LOG=" + self.logname,
                  "-ini:System:[UC2NetTest]:CommandFile=" + os.path.basename(self.netcmd)]
        common += ["-ini:System:[Engine.GameEngine]:AllowNetCheats=%s" % ("True" if self.cheatkey else "False")]
        if self.kind == "server" and self.wine:
            # The MinGW ucc.exe under wine (tools/dev/m12-wine.sh: own prefix, nothing left running).
            argv = [os.path.join(REPO, "tools/dev/m12-wine.sh"), self.sys, "ucc.exe", "server", self.url,
                    "PORT=%d" % PORT] + common
        elif self.kind == "server":
            argv = ["./ucc-bin", "server", self.url, "PORT=%d" % PORT] + common
        else:
            argv = ([] if OFFSCREEN else ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24"]) + ["./uc2.bin"] + ([self.url] if self.url else []) + ["-log"] + common
            # StartupMovies=False: with the GL device the logos would play first and own the input.
            # (The virtual pad is player one here: the default, [SDL2Drv.SDL2Client] PadsStartAtPlayerTwo=False.)
            argv += ["-ini:System:[SDL2Drv.SDL2Client]:InputTestFile=" + os.path.basename(self.inputcmd), "-ini:System:[UC2Movie]:StartupMovies=False"]
            if self.kind == "listen":
                argv += ["PORT=%d" % PORT]
            if self.render == "null":
                argv += ["-nullrenderer"]
        argv += self.extra
        env = dict(os.environ, SDL_AUDIODRIVER="dummy")
        if OFFSCREEN and self.kind != "server":
            env["SDL_VIDEODRIVER"] = "offscreen"
        self.out = open(os.path.join(OUT, "%s-%s.out" % (self.check, self.name)), "w")
        self.popen = subprocess.Popen(argv, cwd=self.sys, stdout=self.out, stderr=subprocess.STDOUT, env=env,
                                      start_new_session=True)
        self.started = time.time()
        return self

    def text(self):
        try:
            with open(self.log, "rb") as f:
                return f.read().decode("latin-1")
        except OSError:
            return ""

    def wait(self, regex, timeout=30, from_start=False):
        """Wait for a new log line (after the last match) matching regex; returns the match or None."""
        rx = re.compile(regex, re.M)
        end = time.time() + timeout
        while True:
            t = self.text()
            m = rx.search(t, 0 if from_start else self.pos)
            if m:
                self.pos = m.end()
                return m
            if time.time() > end or (self.popen and self.popen.poll() is not None):
                t = self.text()
                m = rx.search(t, 0 if from_start else self.pos)
                if m:
                    self.pos = m.end()
                return m
            time.sleep(0.2)

    def count(self, regex):
        return len(re.findall(regex, self.text(), re.M))

    def send(self, line):
        with open(self.netcmd, "a") as f:
            f.write(line + "\n")

    def input(self, line):
        with open(self.inputcmd, "a") as f:
            f.write(line + "\n")

    def probe(self, tag, timeout=15):
        """NETPROBE in this process: {'summary': {...}, 'pawns': {name: {...}}, 'pris': {name: {...}}} or None."""
        self.send("exec netprobe " + tag)
        m = self.wait(r"^NetProbe: %s summary (.*)$" % re.escape(tag), timeout)
        if not m:
            return None
        time.sleep(0.2)  # the pawn/pri lines follow in the same tick
        t = self.text()
        p = {"summary": parse_kv(m.group(1)), "pawns": {}, "pris": {}}
        for mm in re.finditer(r"^NetProbe: %s (pawn|pri) (.*)$" % re.escape(tag), t, re.M):
            kv = parse_kv(mm.group(2))
            p["pawns" if mm.group(1) == "pawn" else "pris"].setdefault(kv.get("name", "None"), kv)
        return p

    def fire(self, hold=0.25):
        self.input("pad axis righttrigger 32767")
        time.sleep(hold)
        self.input("pad axis righttrigger 0")

    def alive(self):
        return self.popen is not None and self.popen.poll() is None

    def engine_pid(self):
        """The ucc-bin/uc2.bin process itself (xvfb-run is its grandparent)."""
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            try:
                with open("/proc/%s/cmdline" % pid, "rb") as f:
                    cl = f.read().split(b"\0")
            except OSError:
                continue
            if cl and cl[0] in (b"./uc2.bin", b"./ucc-bin") and ("LOG=" + self.logname).encode() in cl:
                return int(pid)
        return None

    def rss_mb(self):
        pid = self.engine_pid()
        if not pid:
            return None
        try:
            with open("/proc/%d/status" % pid) as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        return int(line.split()[1]) / 1024.0
        except OSError:
            return None

    def stop(self, sig=signal.SIGINT, grace=10):
        if not self.popen:
            return None
        if self.popen.poll() is None and self.wine:
            # m12-wine.sh passes USR1 on to ucc.exe as a Ctrl+C and kills its wineserver at the end.
            try:
                os.kill(self.popen.pid, signal.SIGUSR1 if sig == signal.SIGINT else sig)
            except OSError:
                pass
            try:
                self.popen.wait(grace + 20)
            except subprocess.TimeoutExpired:
                os.killpg(self.popen.pid, signal.SIGKILL)
                self.popen.wait(5)
        if self.popen.poll() is None:
            pid = self.engine_pid()
            try:
                if pid:
                    os.kill(pid, sig)
                else:
                    os.killpg(self.popen.pid, sig)
            except OSError:
                pass
            try:
                self.popen.wait(grace)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.popen.pid, signal.SIGKILL)
                except OSError:
                    pass
                self.popen.wait(5)
        self.out.close()
        try:
            import shutil
            shutil.copyfile(self.log, os.path.join(OUT, "%s-%s.log" % (self.check, self.name)))
        except OSError:
            pass
        return self.popen.returncode


def server(check, url="DM-Deadbolt", extra=(), wine=False):
    s = Proc(check, "srv", "server", url, extra=extra, sysdir=WINSYS if wine else SERVER_SYS, wine=wine).start()
    if not s.wait(r"TcpNetDriver on port|Bringing Level .* up for play", 180 if wine else 60):
        return s
    s.wait(r"TcpNetDriver on port", 60 if wine else 20, from_start=True)
    return s


def client(check, name, render="null", extra=(), host="127.0.0.1"):
    url = "%s:%d?Name=%s" % (host, PORT, name)
    if render != "gl":
        return Proc(check, name, "client", url, render=render, extra=extra).start()
    # A GL client under Xvfb's software GL can stall for 20 s and more while it starts up (shader compiles; seen
    # in a full-suite run under load): connecting from the command line then puts that stall into the handshake,
    # where the server's InitialConnectTimeout drops the silent connection. Start it in the menu and connect
    # once it renders, as a player does (the console OPEN command, here through the CommandFile).
    c = Proc(check, name, "client", "", render=render, extra=extra).start()
    c.wait(r"Bringing Level Menu\.MyLevel up for play", 180)
    time.sleep(5)
    c.send("exec open " + url)
    return c


def spawn_all(cs, tag, tries=12):
    """Press Fire on every player until each one's own probe shows its pawn; returns the list of probes (None
    for a player that did not spawn). All together, because before the match starts a DM waits until every
    player has pressed Fire (one client pressing alone stays a spectator: seen in the first dedicated2 run)."""
    for c in cs:
        if not getattr(c, "pad", False):
            c.input("pad attach")
            c.pad = True
    time.sleep(1.0)
    got = [None] * len(cs)
    for i in range(tries):
        for k, c in enumerate(cs):
            if got[k] is None:
                c.fire()
        time.sleep(1.5)
        for k, c in enumerate(cs):
            if got[k] is None:
                p = c.probe("%s-%s-%d" % (tag, c.name, i))
                if p and p["summary"].get("localpawn") == "1":
                    got[k] = p
        if all(got):
            break
    return got


def spawn(c, tag, tries=12):
    return spawn_all([c], tag, tries)[0]


def crit_lines(procs):
    bad = []
    for p in procs:
        t = p.text()
        for rx in (r"^Critical:.*$", r"Assertion failed.*$", r"appError called", r"^.*[Oo]verflow.*$",
                   r"^(?!DevNet:   channel).*[Bb]unch.*(error|overflow|fail).*$", r"^.*Received invalid actor class.*$"):
            for m in re.finditer(rx, t, re.M):
                bad.append("%s: %s" % (p.name, m.group(0)[:160]))
    return bad


def finish(check, procs, allow_invalid_class=0):
    """Stop everything; '.clean' = no Critical/assert/overflow/bunch line in any log, and every client-side
    'Received invalid actor class' explained by a server-side 'persistent actor ...DefaultPhysicsVolume... has no
    package-map index' line (one per connection). allow_invalid_class is ignored."""
    for p in procs:
        if p.alive():
            p.stop()
    allow_invalid_class = sum(p.count(r"persistent actor DefaultPhysicsVolume \S+ has no package-map index")
                              for p in procs if p.kind in ("server", "listen"))
    bad = crit_lines(procs)
    inv = [b for b in bad if "invalid actor class" in b]
    rest = [b for b in bad if "invalid actor class" not in b]
    result(not rest and len(inv) <= allow_invalid_class, check + ".clean",
           "Critical/assert/overflow/bunch lines: %d%s; 'Received invalid actor class': %d (server-side unmapped DefaultPhysicsVolume channels: %d)"
           % (len(rest), (" first: " + rest[0]) if rest else "", len(inv), allow_invalid_class))


def pawn_of(probe, name):
    return probe["pawns"].get(name) if probe else None


# ------------------------------------------------------------------------------------------------- checks

def check_dedicated1():
    ck = "dedicated1"
    s = server(ck)
    a = client(ck, "NetA")
    procs = [s, a]
    try:
        j = s.wait(r"Join succeeded: (\S+)", 60)
        result(bool(j), ck + ".join", "server: %s" % (j.group(0) if j else "no 'Join succeeded' in 60 s"))
        lv = a.probe("A0")
        result(bool(lv) and lv["summary"].get("level") == "DM-Deadbolt" and lv["summary"].get("net") == "Client",
               ck + ".level", "client probe: %s" % (lv["summary"] if lv else None))
        p = spawn(a, "Aspawn")
        sp = s.probe("S1")
        sa = pawn_of(sp, "NetA")
        result(bool(p) and bool(sa), ck + ".spawn", "client localpawn=%s, server pawn NetA=%s"
               % (p["summary"].get("localpawn") if p else None, sa and sa.get("loc")))
        pri = sp["pris"].get("NetA") if sp else None
        result(bool(pri) and pri.get("pawnclass") == "UCPawn_Nakhti_Anubis" and pri.get("energy") == "UCInventory.UCWeap_SniperRifle"
               and pri.get("explosive") == "UCInventory.UCWeap_RocketLauncher", ck + ".urloptions",
               "server PRI of NetA: pawnclass=%s energy=%s explosive=%s" % (pri and pri.get("pawnclass"),
               pri and pri.get("energy"), pri and pri.get("explosive")))
        # Hold forward 1.5 s; the server must see the pawn move.
        a.input("pad axis lefty -32768")
        time.sleep(1.5)
        a.input("pad axis lefty 0")
        time.sleep(1.0)
        sp2 = s.probe("S2")
        ap2 = a.probe("A2")
        sa2, aa2 = pawn_of(sp2, "NetA"), pawn_of(ap2, "NetA")
        moved = dist(sa["loc"], sa2["loc"]) if sa and sa2 else 0
        result(moved > 150, ck + ".servermove", "server sees NetA move %.1f units (%s -> %s)"
               % (moved, sa and sa["loc"], sa2 and sa2["loc"]))
        agree = dist(sa2["loc"], aa2["loc"]) if sa2 and aa2 else 1e9
        result(agree < 30, ck + ".agree", "after stopping: client and server NetA %.1f units apart" % agree)
        # The client's HUD/menu state: no menu open, not paused.
        pr = None
        a.input("probe A3")
        m = a.wait(r"probe A3 (.*)$", 10)
        if m:
            pr = parse_kv(m.group(1))
        result(bool(pr) and pr.get("menu") == "None" and pr.get("paused") == "0" and pr.get("pawn") == "1",
               ck + ".clientstate", "SDL2 probe: menu=%s paused=%s pawn=%s weapon=%s" % (pr and pr.get("menu"),
               pr and pr.get("paused"), pr and pr.get("pawn"), pr and pr.get("weapon")))
    finally:
        finish(ck, procs, allow_invalid_class=1)


def place_near(srv, name, other, d=450, tag="PL"):
    """NETPLACENEAR, then check on the server 1.5 s later that the pawn stayed (a spot can be on a jump pad or a
    slope, and B ends up 700 units from where it was put); next heading if not. Returns the
    log line of the accepted placement or None."""
    first = 0
    for attempt in range(16):
        srv.send("exec netplacenear %s %s %d %d" % (name, other, d, first))
        m = srv.wait(r"^NetProbe: placenear %s (ok loc=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\) heading=\d+ index=(\d+)|failed.*)$" % name, 15)
        if not m or not m.group(1).startswith("ok"):
            return None
        placed = (float(m.group(2)), float(m.group(3)), float(m.group(4)))
        time.sleep(1.5)
        p = srv.probe("%s%d" % (tag, attempt))
        now = pawn_of(p, name)
        if now and dist(now["loc"], placed) < 30:
            return m.group(0)
        first = int(m.group(5)) + 1
        if first >= 16:
            return None
    return None


def ranged(attacker, aname, tag):
    """Make sure the attacker holds its race weapon (unlimited ammo). Under simulated loss a player sometimes
    came out of the spawn holding the melee weapon or the energy weapon (UCWeap_SniperRifle, which starts without
    ammo) -- the same on server and client; neither can hit at 450 units. NextWeapon
    (the Q binding) cycles to it."""
    for k in range(5):
        p = attacker.probe("%s%d" % (tag, k))
        w = pawn_of(p, aname)
        if not w or "Race" in w.get("weapon", ""):
            return
        attacker.input("exec NextWeapon")
        time.sleep(1.0)


# A client's view cannot be turned from outside (no such command ships), so the players are lined up around the
# client's own view instead: the shooter's view (its own SDL2 test-channel
# probe: loc and rot of the local player) and the server's NETPLACE (allowed on the server with the test key, and for
# a listen host). P0 is an open, flat spot of DM-Deadbolt with room around it (jit-trace.sh's spot).
P0 = (1977.8, 583.7, -34.1)


def view_of(c, tag):
    """The local player's (loc, yaw, pitch) from this process's SDL2 probe, or None."""
    c.input("probe " + tag)
    m = c.wait(r"probe %s (.*)$" % re.escape(tag), 10)
    if not m:
        return None
    kv = parse_kv(m.group(1))
    loc, rot = kv.get("loc"), kv.get("rot")
    if not isinstance(loc, tuple) or not isinstance(rot, tuple):
        return None
    pitch = int(rot[0]) & 65535
    return loc, int(rot[1]) & 65535, pitch - 65536 if pitch >= 32768 else pitch


def camera_height(c, tag):
    """How far the camera is above the local pawn (SDL2 probe + probe2 lines), or None."""
    c.input("probe " + tag)
    m = c.wait(r"probe %s (.*)$" % re.escape(tag), 10)
    m2 = c.wait(r"probe2 %s (.*)$" % re.escape(tag), 5)
    if not m or not m2:
        return None
    loc, cam = parse_kv(m.group(1)).get("loc"), parse_kv(m2.group(1)).get("cam")
    return cam[2] - loc[2] if isinstance(loc, tuple) and isinstance(cam, tuple) else None


def first_person(c, tag="FP"):
    """The shooter aims with its own view: in UC2's third person the shot leaves the camera, ~90 units above the
    pawn, so a level view passes over a pawn standing on the same floor; in first person it leaves the eyes. The
    player's own console command ToggleThirdCam (the C key), until the camera is at eye height."""
    for k in range(3):
        h = camera_height(c, "%s%d" % (tag, k))
        if h is not None and h < 70:
            return True
        c.input("exec ToggleThirdCam")
        time.sleep(1.0)
    h = camera_height(c, tag + "x")
    return h is not None and h < 70


def place(srv, name, x, y, z, yaw):
    srv.send("exec netplace %s %.1f %.1f %.1f %d" % (name, x, y, z, yaw))
    m = srv.wait(r"^NetProbe: place %s (ok|failed).*$" % name, 15)
    return bool(m) and m.group(1) == "ok"


def line_up(srv, shooter, aname, vname, d=300, tag="LU"):
    """Put <vname> at P0 and the shooter <aname> d units behind it along the shooter's own view yaw: the shooter
    then looks straight at the victim (pitch as it is: level after a spawn; first_person first, see there). Returns
    the shooter's (yaw, pitch) or None."""
    v = view_of(shooter, tag)
    if not v:
        return None
    _, yaw, pitch = v
    a = yaw * math.pi / 32768.0
    ok1 = place(srv, vname, P0[0], P0[1], P0[2], (yaw + 32768) & 65535)
    ok2 = place(srv, aname, P0[0] - d * math.cos(a), P0[1] - d * math.sin(a), P0[2], yaw)
    time.sleep(1.0)
    return (yaw, pitch) if ok1 and ok2 else None


def walk_toward(walker, wname, srv, target_name, secs=1.2, tag="WT"):
    """The walker's left stick pushed toward <target_name>'s pawn (as the server sees both), in the walker's own view
    frame: forward = its view yaw, right = yaw + 90 degrees."""
    v = view_of(walker, tag)
    sp = srv.probe(tag + "s")
    me, it = pawn_of(sp, wname), pawn_of(sp, target_name)
    if not v or not me or not it:
        return False
    _, yaw, _ = v
    dx, dy = it["loc"][0] - me["loc"][0], it["loc"][1] - me["loc"][1]
    n = math.hypot(dx, dy) or 1.0
    a = yaw * math.pi / 32768.0
    fwd = (dx * math.cos(a) + dy * math.sin(a)) / n
    right = (-dx * math.sin(a) + dy * math.cos(a)) / n
    walker.input("pad axis leftx %d" % round(right * 32767))
    walker.input("pad axis lefty %d" % round(-fwd * 32767))
    time.sleep(secs)
    walker.input("pad axis leftx 0")
    walker.input("pad axis lefty 0")
    return True



def fight(ck, srvproc, attacker, victim, aname, vname, max_shots=40):
    """attacker aims at victim and fires until the victim dies on the server; returns (first-hit, kill) dicts."""
    ranged(attacker, aname, "RW")
    first_person(attacker)
    sp0 = srvproc.probe("F0")
    v0 = pawn_of(sp0, vname)
    h0 = int(v0["health"]) if v0 else None
    first_hit = None
    kill = None
    deaths0 = num(sp0["pris"].get(vname, {}).get("deaths")) if sp0 else None
    for i in range(max_shots):
        if i % 5 == 0:   # the victim in front of the shooter's own view (it may have been knocked back)
            line_up(srvproc, attacker, aname, vname, tag="LU%d" % i)
        time.sleep(0.2)
        attacker.fire(0.3)
        time.sleep(0.6)
        sp = srvproc.probe("F%d" % (i + 1))
        if not sp:
            break
        v = pawn_of(sp, vname)
        pri = sp["pris"].get(vname, {})
        if first_hit is None and v and h0 is not None and int(v["health"]) < h0:
            first_hit = {"shot": i + 1, "health": int(v["health"]), "h0": h0}
        if deaths0 is not None and num(pri.get("deaths")) and num(pri.get("deaths")) > deaths0:
            kill = {"shot": i + 1, "probe": sp}
            if first_hit is None:
                first_hit = {"shot": i + 1, "health": 0, "h0": h0}
            break
    return first_hit, kill


def check_dedicated2(ck="dedicated2", srv_extra=(), cli_extra=()):
    s = server(ck, extra=srv_extra)
    a = client(ck, "NetA", extra=cli_extra)
    b = client(ck, "NetB", extra=cli_extra)
    procs = [s, a, b]
    try:
        ja = s.wait(r"Join succeeded: ", 60)
        jb = s.wait(r"Join succeeded: ", 60)
        result(bool(ja and jb), ck + ".join", "server: %d joins" % s.count(r"Join succeeded: "))
        pa, pb = spawn_all([a, b], "spawn")
        result(bool(pa and pb), ck + ".spawn", "NetA localpawn=%s, NetB localpawn=%s"
               % (bool(pa), bool(pb)))
        pl = place_near(s, "NetB", "NetA")
        result(bool(pl), ck + ".place", "server: %s (and still there 1.5 s later)" % pl)
        sp, ap, bp = s.probe("P1"), a.probe("P1"), b.probe("P1")
        sB, aB = pawn_of(sp, "NetB"), pawn_of(ap, "NetB")
        sA, bA = pawn_of(sp, "NetA"), pawn_of(bp, "NetA")
        d1 = dist(sB["loc"], aB["loc"]) if sB and aB else 1e9
        d2 = dist(sA["loc"], bA["loc"]) if sA and bA else 1e9
        result(d1 < 40 and d2 < 40, ck + ".seeother", "NetA sees NetB %.1f units from the server's NetB; NetB sees NetA %.1f from the server's"
               % (d1, d2))
        # Movement of B changes what A sees. B walks toward A, along the path place_near checked clear (its stick
        # pushed in A's direction in B's own view frame: walk_toward), instead of into a wall.
        walk_toward(b, "NetB", s, "NetA", tag="WB")
        time.sleep(1.0)
        sp2, ap2 = s.probe("P2"), a.probe("P2")
        sB2, aB2 = pawn_of(sp2, "NetB"), pawn_of(ap2, "NetB")
        mv = dist(aB["loc"], aB2["loc"]) if aB and aB2 else 0
        d3 = dist(sB2["loc"], aB2["loc"]) if sB2 and aB2 else 1e9
        result(mv > 100 and d3 < 40, ck + ".othermoves", "NetB walked: NetA's view of NetB moved %.1f units, %.1f from the server's"
               % (mv, d3))
        # Put B back in front of A and shoot it.
        place_near(s, "NetB", "NetA", tag="PK")
        first, kill = fight(ck, s, a, b, "NetA", "NetB")
        result(bool(first), ck + ".hit", "NetB health on the server: %s" % first)
        sp3 = kill["probe"] if kill else s.probe("K1s")
        def sc(p, who, field):
            return num(p["pris"].get(who, {}).get(field)) if p else None
        # The clients' PlayerReplicationInfos follow within a few updates; under loss a lost update is resent
        # after the NAK, so poll up to 6 s (a lag run once had NetB's own deaths=0 at 1 s and =1 later).
        for k in range(6):
            time.sleep(1.0)
            ap3, bp3 = a.probe("K%d" % (k + 1)), b.probe("K%d" % (k + 1))
            scores = [(sc(x, "NetA", "score"), sc(x, "NetB", "deaths")) for x in (sp3, ap3, bp3)]
            ok = bool(kill) and all(sa is not None and sa >= 1 and db is not None and db >= 1 for sa, db in scores)
            if ok or not kill:
                break
        result(ok, ck + ".kill", "after %s shots: (NetA score, NetB deaths) server %s, NetA's view %s, NetB's view %s"
               % (kill and kill["shot"], scores[0], scores[1], scores[2]))
        # Health seen by the victim's own client and by the attacker: compared in a separate shot sequence after respawn.
        pb2 = spawn(b, "Brespawn")
        if pb2:
            place_near(s, "NetB", "NetA", tag="PH")
            ranged(a, "NetA", "RH")
            first_person(a, "FPH")
            h_before = pawn_of(s.probe("H0"), "NetB")
            got = None
            for i in range(20):
                if i % 5 == 0:
                    line_up(s, a, "NetA", "NetB", tag="LH%d" % i)
                a.fire(0.3)
                time.sleep(1.0)
                sv, av, bv = s.probe("H%d" % (i + 1)), a.probe("H%d" % (i + 1)), b.probe("H%d" % (i + 1))
                hs, ha, hb = (pawn_of(x, "NetB") for x in (sv, av, bv))
                if hs and h_before and 0 < int(hs["health"]) < int(h_before["health"]):
                    got = (int(h_before["health"]), int(hs["health"]), ha and int(ha["health"]), hb and int(hb["health"]))
                    # the clients follow within a few updates (resent after a NAK under loss): up to 5 s
                    for k in range(5):
                        if got[1] == got[2] == got[3]:
                            break
                        time.sleep(1.0)
                        sv, av, bv = (x.probe("HC%d-%d" % (i, k)) for x in (s, a, b))
                        hs, ha, hb = (pawn_of(x, "NetB") for x in (sv, av, bv))
                        got = (got[0], hs and int(hs["health"]), ha and int(ha["health"]), hb and int(hb["health"]))
                    break
            result(bool(got) and got[1] == got[2] == got[3], ck + ".healthseen",
                   "NetB health before %s; after a hit: server %s, NetA's view %s, NetB's own %s" % got if got else
                   "no non-lethal hit seen in 20 shots")
        else:
            result(False, ck + ".healthseen", "NetB did not respawn")
    finally:
        finish(ck, procs, allow_invalid_class=2)


def check_listen():
    ck = "listen"
    l = Proc(ck, "L", "listen", "DM-Deadbolt?Listen?Name=NetL").start()
    procs = [l]
    try:
        up = l.wait(r"TcpNetDriver on port|Listen", 90)
        b = client(ck, "NetB")
        procs.append(b)
        j = l.wait(r"Join succeeded: ", 60)
        result(bool(j), ck + ".join", "listen server: %s" % (j.group(0) if j else "no join in 60 s"))
        pl, pb = spawn_all([l, b], "spawn")
        result(bool(pl and pb), ck + ".spawn", "NetL localpawn=%s NetB localpawn=%s" % (bool(pl), bool(pb)))
        pm = place_near(l, "NetB", "NetL")
        result(bool(pm), ck + ".place", "%s (and still there 1.5 s later)" % pm)
        lp, bp = l.probe("P1"), b.probe("P1")
        d1 = dist(pawn_of(lp, "NetB")["loc"], pawn_of(bp, "NetB")["loc"]) if pawn_of(lp, "NetB") and pawn_of(bp, "NetB") else 1e9
        d2 = dist(pawn_of(lp, "NetL")["loc"], pawn_of(bp, "NetL")["loc"]) if pawn_of(lp, "NetL") and pawn_of(bp, "NetL") else 1e9
        result(d1 < 40 and d2 < 40, ck + ".seeother", "NetB: client vs listen server %.1f units; NetL: %.1f" % (d1, d2))
        walk_toward(b, "NetB", l, "NetL", tag="WB")   # along the checked path (see dedicated2)
        time.sleep(1.0)
        lp2 = l.probe("P2")
        mv = dist(pawn_of(lp, "NetB")["loc"], pawn_of(lp2, "NetB")["loc"]) if pawn_of(lp, "NetB") and pawn_of(lp2, "NetB") else 0
        result(mv > 100, ck + ".clientmoves", "the listen server sees NetB move %.1f units" % mv)
        place_near(l, "NetB", "NetL", tag="PK")
        first, kill = fight(ck, l, l, b, "NetL", "NetB")
        time.sleep(1.0)
        bp3 = b.probe("K1")
        lp3 = kill["probe"] if kill else l.probe("K1l")
        sl = num(lp3["pris"].get("NetL", {}).get("score")) if lp3 else None
        sb = num(bp3["pris"].get("NetL", {}).get("score")) if bp3 else None
        result(bool(first), ck + ".hit", "NetB health on the listen server: %s" % first)
        result(bool(kill) and sl and sl >= 1 and sb and sb >= 1, ck + ".kill",
               "NetL score: listen server %s, NetB's view %s (after %s shots)" % (sl, sb, kill and kill["shot"]))
    finally:
        finish(ck, procs, allow_invalid_class=1)


def check_profile_login():
    """A client that joins from the command line (127.0.0.1:<port>?Name=NetB) with a saved profile active logs in
    as the profile: the listen server's PlayerReplicationInfo is named after the profile and has its character, not
    the URL's Name and the ini's [DefaultPlayer] Class. The profile is made first with `ucc-bin profiletest make`
    (a new player's default profile + Character=) in the shared System/Profiles; only the client remembers it
    (-ini:User:[Profiles]:LastProfile=, its own NetBUser.ini), so the listen host NetL plays without a profile."""
    ck = "profile-login"
    prof, char, inichar = "NetProf", "UCCharacters.UCPawn_Human_Sapphire", "UCCharacters.UCPawn_Human_Malcolm"
    shutil.rmtree(os.path.join(SYS, "Profiles", prof), ignore_errors=True)
    mk = subprocess.run(["./ucc-bin", "profiletest", "make", prof, os.path.join(OUT, ck + "-make.txt"),
                         "Character=" + char], cwd=SYS, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    made = os.path.exists(os.path.join(SYS, "Profiles", prof, "AVARIS_PROFILE.PRO"))
    result(mk.returncode == 0 and made, ck + ".made", "ucc-bin profiletest make %s Character=%s: rc %d, file %s"
           % (prof, char, mk.returncode, made))
    l = Proc(ck, "L", "listen", "DM-Deadbolt?Listen?Name=NetL").start()
    procs = [l]
    try:
        l.wait(r"TcpNetDriver on port|Listen", 90)
        b = client(ck, "NetB", extra=["-ini:User:[Profiles]:LastProfile=" + prof,
                                       "-ini:User:[DefaultPlayer]:Class=" + inichar])
        procs.append(b)
        st = b.wait(r'Profiles: started with the last profile "%s"' % prof, 60, from_start=True)
        result(bool(st), ck + ".client-in-profile", st.group(0) if st else "the client did not start in " + prof)
        lg = b.wait(r'Profiles: profile "%s" logs in with Class=(\S+) \(URL had Class=(\S*)\)' % prof, 60, from_start=True)
        result(bool(lg) and lg.group(1) == char, ck + ".client-url", lg.group(0) if lg else "no 'logs in with' line on the client")
        j = l.wait(r"Join succeeded: (\S+)", 60)
        result(bool(j) and j.group(1) == prof, ck + ".join-name", "listen server: %s" % (j.group(0) if j else "no join in 60 s"))
        pl, pb = spawn_all([l, b], "spawn")
        lp = l.probe("PL")
        pri = lp["pris"].get(prof) if lp else None
        result(bool(pri) and pri.get("pawnclass", "") == char.split(".")[-1], ck + ".server-pri",
               "listen server's PRI for the client: %s" % (pri if pri else "none named %s (have %s)" % (prof, list(lp["pris"]) if lp else "no probe")))
        pw = pawn_of(lp, prof)
        result(bool(pw) and pw.get("class", "") == char.split(".")[-1], ck + ".server-pawn",
               "listen server's pawn for the client: %s" % pw)
        result("NetB" not in (lp["pris"] if lp else {}), ck + ".not-url-name", "no PRI named NetB (the URL's Name)")
    finally:
        finish(ck, procs, allow_invalid_class=1)
        shutil.rmtree(os.path.join(SYS, "Profiles", prof), ignore_errors=True)


def check_travel():
    ck = "travel"
    s = server(ck)
    a = client(ck, "NetA")
    procs = [s, a]
    try:
        s.wait(r"Join succeeded: ", 60)
        p = spawn(a, "Aspawn")
        result(bool(p), ck + ".spawn1", "NetA spawned on DM-Deadbolt: %s" % bool(p))
        s.send("exec servertravel DM-Sever")
        sl = s.wait(r"Bringing Level DM-Sever\.MyLevel up for play", 120)
        result(bool(sl), ck + ".servertravel", "server: %s" % (sl.group(0) if sl else "no DM-Sever in 120 s"))
        cl = a.wait(r"Welcomed by server: LEVEL=DM-Sever", 120)
        cj = s.wait(r"Join succeeded: \S+", 120)
        result(bool(cl and cj), ck + ".clientfollows", "client: %s; server: %s" % (cl and cl.group(0), cj and cj.group(0)))
        lv = a.probe("T1")
        result(bool(lv) and lv["summary"].get("level") == "DM-Sever", ck + ".clientlevel",
               "client probe level=%s" % (lv and lv["summary"].get("level")))
        p2 = spawn(a, "Aspawn2")
        sp = s.probe("T2")
        # After the travel the client logs in under the name its own settings give (UC2: the character's,
        # "Anubis"), not the command line's ?Name=NetA: look it up from its own probe.
        me = p2["summary"].get("local") if p2 else None
        sv = pawn_of(sp, me) if me else None
        cv = pawn_of(p2, me) if me else None
        result(bool(p2) and bool(sv), ck + ".spawn2", "client %s localpawn=%s, server pawn %s at %s"
               % (me, bool(p2), me, sv and sv.get("loc")))
    finally:
        finish(ck, procs, allow_invalid_class=2)


def check_disconnect():
    ck = "disconnect"
    s = server(ck, extra=())
    a = client(ck, "NetA")
    b = client(ck, "NetB")
    procs = [s, a, b]
    try:
        s.wait(r"Join succeeded: ", 60)
        s.wait(r"Join succeeded: ", 60)
        spawn_all([a, b], "spawn")
        p0 = s.probe("D0")
        result(bool(p0) and p0["summary"].get("conns") == "2", ck + ".twojoined", "server conns=%s" % (p0 and p0["summary"].get("conns")))
        # A leaves cleanly: the console command DISCONNECT, then the process quits.
        a.send("exec disconnect")
        time.sleep(4)
        p1 = s.probe("D1")
        result(bool(p1) and p1["summary"].get("conns") == "1" and "NetA" not in p1["pris"], ck + ".leave",
               "server after NetA's DISCONNECT: conns=%s, PRIs %s" % (p1 and p1["summary"].get("conns"), p1 and sorted(p1["pris"])))
        a.stop()
        # B freezes (SIGSTOP: its socket stays open, so no ICMP port-unreachable tells the server; a killed
        # process on 127.0.0.1 is dropped at once through that ICMP, which the 'killed' item below shows): the
        # server must drop it after ConnectionTimeout (30 s, Default.ini [IpDrv.TcpNetDriver]).
        bpid = b.engine_pid()
        if bpid:
            os.kill(bpid, signal.SIGSTOP)
        t0 = time.time()
        dropped = None
        while time.time() - t0 < 75:
            p2 = s.probe("D2-%d" % int(time.time() - t0))
            if p2 and p2["summary"].get("conns") == "0":
                dropped = time.time() - t0
                break
            time.sleep(3)
        result(dropped is not None and 25 <= dropped <= 45 and s.alive(), ck + ".timeout",
               "server dropped the frozen NetB after %s s (ConnectionTimeout 30), server alive=%s" % (dropped and round(dropped), s.alive()))
        b.stop(signal.SIGKILL, grace=5)
        # A client process killed outright (socket closed): dropped through the ICMP port-unreachable.
        c0 = client(ck, "NetK")
        procs.append(c0)
        jk = s.wait(r"Join succeeded: NetK", 60)
        time.sleep(2)
        c0.stop(signal.SIGKILL, grace=5)
        t1 = time.time()
        dk = None
        while time.time() - t1 < 45:
            pk = s.probe("DK-%d" % int(time.time() - t1))
            if pk and pk["summary"].get("conns") == "0":
                dk = time.time() - t1
                break
            time.sleep(2)
        result(bool(jk) and dk is not None and s.alive(), ck + ".killed", "server dropped the killed NetK after %s s, server alive=%s"
               % (dk and round(dk), s.alive()))
        # A new client joins afterwards.
        c = client(ck, "NetC")
        procs.append(c)
        j = s.wait(r"Join succeeded: NetC", 60)
        pc = spawn(c, "Cspawn") if j else None
        p3 = s.probe("D3")
        result(bool(j and pc) and bool(pawn_of(p3, "NetC")), ck + ".rejoin", "NetC joined=%s spawned=%s server pawn=%s"
               % (bool(j), bool(pc), bool(pawn_of(p3, "NetC"))))
    finally:
        finish(ck, procs, allow_invalid_class=3)


def check_soak(minutes=float(os.environ.get("NET_SOAK_MIN", "10"))):
    ck = "soak"
    # NET_SOAK_SIM="PktLag=50 PktLoss=5": the soak under simulated lag/loss on every process (long enough to
    # cross the packet-id wrap, ~16k packets).
    sim = tuple(os.environ.get("NET_SOAK_SIM", "").split())
    s = server(ck, url="DM-Deadbolt?NumBots=2", extra=sim)
    a = client(ck, "NetA", extra=sim)
    b = client(ck, "NetB", extra=sim)
    procs = [s, a, b]
    try:
        s.wait(r"Join succeeded: ", 60)
        s.wait(r"Join succeeded: ", 60)
        spawn_all([a, b], "spawn")
        p0 = s.probe("Z0")
        bots = [n for n, v in (p0["pris"].items() if p0 else []) if v.get("bot") == "1"]
        result(len(bots) == 2, ck + ".bots", "server PRIs: %s (bots %s)" % (p0 and sorted(p0["pris"]), bots))
        rss = {p.name: [] for p in procs}
        t0 = time.time()
        k = 0
        while time.time() - t0 < minutes * 60:
            k += 1
            for c in (a, b):
                # wander: walk, turn, fire, respawn when dead (Fire also respawns)
                c.input("pad axis lefty -32768")
                c.input("pad axis rightx %d" % (12000 if k % 2 else -12000))
            time.sleep(2.0)
            for c in (a, b):
                c.input("pad axis rightx 0")
                c.fire(0.2)
            time.sleep(1.0)
            if k % 10 == 0:
                for p in procs:
                    rss[p.name].append((round(time.time() - t0), p.rss_mb()))
                sp = s.probe("Z%d" % k)
                if not sp or not all(p.alive() for p in procs):
                    break
        for c in (a, b):
            c.input("pad axis lefty 0")
            c.input("pad axis rightx 0")
        # Late replication: after minutes of play (past the packet-id wrap, ~16k packets) both clients must show
        # every player's score and deaths as the server has them (PRI updates lost on the way are resent after
        # a NAK; poll up to 10 s).
        agree = None
        for k in range(10):
            time.sleep(1.0)
            sv, av, bv = s.probe("ZV%d" % k), a.probe("ZV%d" % k), b.probe("ZV%d" % k)
            want = {n: (v.get("score"), v.get("deaths")) for n, v in (sv["pris"].items() if sv else [])}
            seen = [{n: (v.get("score"), v.get("deaths")) for n, v in (x["pris"].items() if x else [])} for x in (av, bv)]
            agree = (want, seen)
            if want and all(x == want for x in seen):
                break
        result(bool(agree) and agree[0] and all(x == agree[0] for x in agree[1]), ck + ".views",
               "server (score, deaths) %s; NetA's view %s; NetB's view %s" % (agree[0], agree[1][0], agree[1][1]) if agree else "no probes")
        alive = all(p.alive() for p in procs)
        sp = s.probe("Zend")
        result(alive and bool(sp), ck + ".alive", "after %.1f min: all %d processes alive=%s; server PRIs %s"
               % ((time.time() - t0) / 60, len(procs), alive, sp and {n: (v.get("score"), v.get("deaths")) for n, v in sp["pris"].items()}))
        flat = True
        det = []
        for n, series in rss.items():
            vals = [v for _, v in series if v]
            if len(vals) >= 3:
                growth = vals[-1] - vals[1]
                det.append("%s %.0f->%.0f MB" % (n, vals[0], vals[-1]))
                if growth > 40:
                    flat = False
            else:
                flat = False
                det.append("%s: %d samples" % (n, len(vals)))
        result(flat, ck + ".rss", "; ".join(det) + " (growth from the 2nd sample to the last must stay under 40 MB)")
        with open(os.path.join(OUT, "soak-rss.txt"), "w") as f:
            for n, series in rss.items():
                f.write("%s %s\n" % (n, series))
    finally:
        finish(ck, procs, allow_invalid_class=2)


def check_lag():
    # Epic's own net test switches (Engine/Inc/UnConn.h DO_ENABLE_NET_TEST): every process delays its outgoing
    # packets by 50 ms (=> 100 ms round trip) and drops 5 % of them.
    # NET_LAG_LOSS overrides the loss percentage (information runs at 20 %).
    loss = os.environ.get("NET_LAG_LOSS", "5")
    sim = ("PktLag=50", "PktLoss=" + loss)
    check_dedicated2("lag", srv_extra=sim, cli_extra=sim)


def check_glshot():
    ck = "glshot"
    s = server(ck)
    a = client(ck, "NetA", render="gl")
    b = client(ck, "NetB")
    procs = [s, a, b]
    try:
        s.wait(r"Join succeeded: ", 90)
        s.wait(r"Join succeeded: ", 90)
        pa, pb = spawn_all([a, b], "spawn", tries=20)
        result(bool(pa and pb), ck + ".spawn", "NetA (GL) localpawn=%s, NetB localpawn=%s" % (bool(pa), bool(pb)))
        place_near(s, "NetB", "NetA", 350)
        line_up(s, a, "NetA", "NetB", 350, tag="LG")
        time.sleep(1.0)
        for f in os.listdir(SYS):
            if f.startswith("Shot") and f.endswith(".bmp"):
                os.remove(os.path.join(SYS, f))
        a.input("exec shot")
        time.sleep(3.0)
        shots = sorted(f for f in os.listdir(SYS) if f.startswith("Shot") and f.endswith(".bmp"))
        ap = a.probe("G1")
        aB = pawn_of(ap, "NetB")
        if shots:
            dst = os.path.join(OUT, "glshot-NetA-sees-NetB.bmp")
            os.replace(os.path.join(SYS, shots[-1]), dst)
        result(bool(shots) and bool(aB), ck + ".screenshot", "NetA's GL screenshot %s; NetA sees NetB at %s"
               % ("logs/net/glshot-NetA-sees-NetB.(bmp|png)" if shots else "missing", aB and aB.get("loc")))
    finally:
        finish(ck, procs, allow_invalid_class=2)


def check_wine():
    """N4: the Linux client against the Windows (MinGW) ucc.exe server of this branch under wine."""
    ck = "wine"
    if not os.path.exists(os.path.join(WINSYS, "ucc.exe")):
        result(False, ck + ".tree", "no ucc.exe in %s (tools/dev/mkrun-uc2.sh build-windows netwin)" % WINSYS)
        return
    s = server(ck, wine=True)
    a = client(ck, "NetA")
    procs = [s, a]
    try:
        j = s.wait(r"Join succeeded: (\S+)", 120)
        result(bool(j), ck + ".join", "wine server: %s" % (j.group(0) if j else "no join in 120 s"))
        p = spawn(a, "Aspawn")
        sp = s.probe("W1", timeout=30)
        sa = pawn_of(sp, "NetA")
        result(bool(p) and bool(sa), ck + ".spawn", "client localpawn=%s, wine server pawn NetA at %s" % (bool(p), sa and sa.get("loc")))
        a.input("pad axis lefty -32768")
        time.sleep(1.5)
        a.input("pad axis lefty 0")
        time.sleep(1.0)
        sp2, ap2 = s.probe("W2", timeout=30), a.probe("W2")
        sa2, aa2 = pawn_of(sp2, "NetA"), pawn_of(ap2, "NetA")
        moved = dist(sa["loc"], sa2["loc"]) if sa and sa2 else 0
        agree = dist(sa2["loc"], aa2["loc"]) if sa2 and aa2 else 1e9
        result(moved > 150 and agree < 30, ck + ".move", "wine server sees NetA move %.1f units; client vs server %.1f units" % (moved, agree))
        pm = [l for l in s.text().splitlines() if l.startswith("DevNet: PackageMap ")]
        cm = [l for l in a.text().splitlines() if l.startswith("DevNet: PackageMap ")]
        result(bool(pm) and pm == cm[:len(pm)], ck + ".packagemap", "package-map tables: wine server %d lines, client %d, identical=%s"
               % (len(pm), len(cm), pm == cm[:len(pm)]))
    finally:
        finish(ck, procs)


# Deterministic loss of one handshake/join packet ([UC2NetTest] Drop=, Engine/Src/UC2NetTest.cpp). (case, side, spec)
HANDSHAKE_DROPS = [
    ("hello", "client", "HELLO"),          # the client's first packet
    ("challenge", "server", "CHALLENGE"),  # the server's answer (the coordinator's lag hang)
    ("netspeed", "client", "NETSPEED"),    # NETSPEED and LOGIN usually share a packet
    ("login", "client", "LOGIN"),
    ("uses", "server", "USES#10"),         # one of the 67 package lines
    ("welcome", "server", "WELCOME"),
    ("join", "client", "JOIN"),
    ("afterjoin", "server", "<JOIN+3"),    # the server's first three packets after it got JOIN (actor channels)
    # The server's packet ids start just before the wrap ([UC2NetTest] PacketIdStart=16375; ids run 0..16382), so its WELCOME
    # goes out after the wrap: a loss there needs the ack logic to work across the wrap.
    ("wrap", "server", "WELCOME", "-ini:System:[UC2NetTest]:PacketIdStart=16375"),
    ("wraponly", "server", "NOSUCHTEXT", "-ini:System:[UC2NetTest]:PacketIdStart=16375"),   # the wrap alone, nothing lost
    # The client's ids start at 16381: its JOIN goes out as packet 0 after the wrap.
    ("wrapclient", "client", "JOIN", "c:-ini:System:[UC2NetTest]:PacketIdStart=16381"),
]
# Compatibility (check handshake-compat): one end keeps Epic's UC2 ack logic ([UC2NetTest] OldAcks=True, test key in UnConn.cpp).
# Expected: "join", or "fail" = the unfixed end lost the packet, which it never resends; the fixed client then
# gives up after InitialConnectTimeout (20 s) and shows ConnectionFailureMenu instead of waiting forever.
HANDSHAKE_COMPAT = [
    ("oldclient-challenge", "server", "CHALLENGE", [], ["-ini:System:[UC2NetTest]:OldAcks=True"], "join"),   # fixed server, unfixed client
    ("oldserver-challenge", "server", "CHALLENGE", ["-ini:System:[UC2NetTest]:OldAcks=True"], [], "fail"),   # unfixed server, fixed client
    ("oldclient-login", "client", "LOGIN", [], ["-ini:System:[UC2NetTest]:OldAcks=True"], "join"),
    ("oldserver-login", "client", "LOGIN", ["-ini:System:[UC2NetTest]:OldAcks=True"], [], "join"),
    # Unfixed client whose JOIN is its packet 0 after the wrap: it never resends it, a fixed server cannot help;
    # the client sits in the level without a player (it is past WELCOME, so no handshake timeout either).
    ("oldclient-wrap", "client", "JOIN", [], ["-ini:System:[UC2NetTest]:OldAcks=True", "-ini:System:[UC2NetTest]:PacketIdStart=16381"], "nojoin"),
    ("oldserver-wrap", "server", "WELCOME", ["-ini:System:[UC2NetTest]:OldAcks=True", "-ini:System:[UC2NetTest]:PacketIdStart=16375"], [], "fail"),
]


def check_handshake_loss(compat=False):
    ck = "handshake-compat" if compat else "handshake-loss"
    limit = float(os.environ.get("NET_DROP_LIMIT", "60"))
    only = os.environ.get("NET_DROP_CASES")
    cases = [(e[0], e[1], e[2], [x for x in e[3:] if not x.startswith("c:")], [x[2:] for x in e[3:] if x.startswith("c:")], "join")
             for e in HANDSHAKE_DROPS]
    if compat:
        cases = list(HANDSHAKE_COMPAT)
    for case, side, spec, sextra, cextra, expect in cases:
        if only and case not in only.split(","):
            continue
        sub = "hsloss-" + case
        s = server(sub, extra=(["-ini:System:[UC2NetTest]:Drop=" + spec] if side == "server" else []) + sextra)
        a = client(sub, "NetA", extra=(["-ini:System:[UC2NetTest]:Drop=" + spec] if side == "client" else []) + cextra)
        procs = [s, a]
        try:
            j = s.wait(r"Join succeeded: NetA", limit)
            took = time.time() - a.started
            dropper = s if side == "server" else a
            lost = dropper.count(r"^NetProbe: netdrop: lost packet")
            p = spawn(a, "spawn") if j else None
            sp = s.probe("H1") if j else None
            ok = bool(j) and (lost >= 1 or spec == "NOSUCHTEXT") and bool(p) and bool(pawn_of(sp, "NetA"))
            menu = None
            if j:
                det = "joined %.1f s after the client started, spawned=%s, %d packet(s) lost by %s" % (took, bool(p), lost, side)
            else:
                cl = a.text()
                last = [l for l in cl.splitlines() if "PendingLevel received" in l or "sending text" in l]
                a.input("probe NOJOIN")
                m = a.wait(r"probe NOJOIN (.*)$", 10)
                menu = parse_kv(m.group(1)).get("menu") if m else None
                gave_up = a.count(r"Pending connection: no message from the server for .* before WELCOME, giving up")
                det = "NO JOIN within %.0f s (%d packet(s) lost by %s); client's last handshake line: %s; client gave up: %s, menu now %s" % (
                    limit, lost, side, last[-1][:80] if last else "none", bool(gave_up), menu)
                if expect == "nojoin":
                    ok = lost >= 1
                    det = "expected no join (unfixed client lost it): " + det
                if expect == "fail":
                    ok = lost >= 1 and bool(gave_up) and menu == "ConnectionFailureMenu"
                    det = "expected no join (unfixed end lost it): " + det
            result(ok, "%s.%s" % (ck, case), "%s drops %s: %s" % (side, spec, det))
        finally:
            finish(sub, procs)


def check_cheats():
    """The cheat guard in a network game (Source/Engine/Src/UC2Cheats.cpp). Without the test key anywhere: the
    listen host's own NETPROBE and WHEREAMI work (the host is the server); a client's NETPROBE answers about its own
    player only and its WHEREAMI is refused. Then a client with the key: its NETPROBE is the full answer;
    NETAIM does not exist."""
    ck = "cheats"
    l = Proc(ck, "L", "listen", "DM-Deadbolt?Listen?Name=NetL", cheatkey=False).start()
    procs = [l]
    try:
        l.wait(r"TcpNetDriver on port|Listen", 90)
        # own player names (NetC, NetK): Proc.stop finds an engine by its LOG= name, which other checks' NetB shares
        b = Proc(ck, "NetC", "client", "127.0.0.1:%d?Name=NetC" % PORT, cheatkey=False).start()
        procs.append(b)
        j = l.wait(r"Join succeeded: ", 60)
        result(bool(j), ck + ".join", "listen server: %s" % (j.group(0) if j else "no join in 60 s"))
        time.sleep(3)
        l.send("exec NETPROBE H0")
        m = l.wait(r"NetProbe: H0 summary net=ListenServer.*$", 15)
        result(bool(m), ck + ".host-netprobe-without-key", m.group(0)[:120] if m else "no answer")
        l.send("exec WHEREAMI")
        m = l.wait(r"Cheat: WHEREAMI .*$", 15)
        result(bool(m) and "refused" not in m.group(0), ck + ".host-whereami-without-key", m.group(0) if m else "no line")
        # A client's NETPROBE without the key answers about its own player only (no refusal, no "Cheat:" line)
        time.sleep(3)
        b.send("exec NETPROBE C0")
        m = b.wait(r"^NetProbe: C0 summary net=Client.*$", 15)
        self_line = b.wait(r"^NetProbe: C0 self only \(network client\)$", 5)
        time.sleep(0.5)
        lines = re.findall(r"^NetProbe: C0 (pawn|pri) name=(\S+)", b.text(), re.M)
        names = sorted(set(n for _, n in lines))
        refused = re.search(r"Cheat: NETPROBE", b.text())
        result(bool(m) and bool(self_line) and ("pri", "NetC") in lines and names == ["NetC"] and not refused,
               ck + ".client-netprobe-self-only-without-key",
               "summary %s; self-only line %s; pawn/pri lines about %s (want only NetC); refusal or Cheat line: %s"
               % (bool(m), bool(self_line), names, bool(refused)))
        b.send("exec WHEREAMI")
        m = b.wait(r"Cheat: WHEREAMI refused: .*$", 15)
        result(bool(m), ck + ".client-whereami-refused-without-key", m.group(0) if m else "not refused")
        b.stop()
        time.sleep(2)
        b2 = Proc(ck, "NetK", "client", "127.0.0.1:%d?Name=NetK" % PORT, cheatkey=True).start()
        procs.append(b2)
        l.wait(r"Join succeeded: ", 60)
        time.sleep(3)
        b2.send("exec NETPROBE K0")
        m = b2.wait(r"NetProbe: K0 summary net=Client.*$", 15)
        time.sleep(0.5)
        names = sorted(set(re.findall(r"^NetProbe: K0 pri name=(\S+)", b2.text(), re.M)))
        result(bool(m) and "NetL" in names and not re.search(r"^NetProbe: K0 self only", b2.text(), re.M),
               ck + ".client-netprobe-full-with-key", "pri lines about %s (want NetL too)" % names)
        b2.send("exec NETAIM NetL")
        m = b2.wait(r"Cheat: NETAIM", 5)
        result(not m, ck + ".netaim-does-not-exist", "no 'Cheat: NETAIM' line (the command was removed)" if not m else m.group(0))
    finally:
        finish(ck, procs)


DLPKG = "NetDownloadTest"


def make_download_package():
    """A small package that only the server can see: the smallest shipped texture package (RL_Hud_Movies.utx,
    547 bytes, PKG_AllowDownload) copied under a new name into <tree>/NetDownload/ and resaved by ucc-bin's own
    ResavePackage commandlet, so it is a package of its own (SavePackage gives it a new GUID). Only the server's ini
    lists that folder in Paths. Returns (path, guid hex) or (None, reason)."""
    import shutil, struct
    tree = os.path.dirname(SYS)
    d = os.path.join(tree, "NetDownload")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    src = os.path.join(tree, "Textures", "RL_Hud_Movies.utx")
    dst = os.path.join(d, DLPKG + ".utx")
    shutil.copyfile(src, dst)
    log = os.path.join(OUT, "download-make.out")
    with open(log, "w") as f:
        rc = subprocess.call(["./ucc-bin", "resavepackage", "../NetDownload", DLPKG + ".utx", "LOG=NetDownloadMake.log",
                              "INI=NetDownloadMake.ini", "USERINI=NetDownloadMakeUser.ini"], cwd=SYS, stdout=f,
                             stderr=subprocess.STDOUT, timeout=120)

    def header(p):
        with open(p, "rb") as f:
            h = f.read(64)
        flags = struct.unpack_from("<I", h, 8)[0]
        return flags, h[36:52].hex()   # v151 summary: tag, ver, flags, 3 x (count, offset), then the GUID
    sflags, sguid = header(src)
    dflags, dguid = header(dst)
    ok = rc == 0 and dguid != sguid and dflags & 1
    return (dst if ok else None), "ucc-bin resavepackage exit %d; %s.utx %d bytes, flags 0x%x, GUID %s (source GUID %s)" % (
        rc, DLPKG, os.path.getsize(dst), dflags, dguid, sguid)


def check_download(http=False):
    """Package download: the server lists a package (ServerPackages) that the client's tree does not have. The client
    must fetch it (ChannelDownload through the game connection; with http=True the server's RedirectToURL points at a
    local HTTP server, so the client receives `DLMGR CLASS=IpDrv.HTTPDownload` and fetches it over HTTP), store it in
    its cache under the package GUID, load it, and join."""
    ck = "download-http" if http else "download"
    pkg, how = make_download_package()
    result(bool(pkg), ck + ".package", how)
    if not pkg:
        return
    guid = re.search(r"GUID (\w+)", how).group(1)
    cachedir = os.path.join(os.path.dirname(SYS), "NetDownloadCache-" + ck)
    import shutil
    shutil.rmtree(cachedir, ignore_errors=True)
    ini = {"Core.System": ["Paths=../NetDownload/*.utx"], "Engine.GameEngine": ["ServerPackages=" + DLPKG]}
    httpd = None
    hport = PORT + 1   # TCP; the game uses UDP
    if http:
        # UseCompression=False: the file as it is (with True the client asks for <file>.uz, made by `ucc compress`).
        ini["IpDrv.HTTPDownload"] = ["RedirectToURL=http://127.0.0.1:%d/" % hport]
        httpd_out = open(os.path.join(OUT, ck + "-httpd.out"), "w")
        httpd = subprocess.Popen([sys.executable, "-m", "http.server", str(hport), "--bind", "127.0.0.1", "--directory",
                                  os.path.dirname(pkg)], stdout=httpd_out, stderr=subprocess.STDOUT)
    s = Proc(ck, "srv", "server", "DM-Deadbolt", ini_add=ini,
             extra=["-ini:System:[IpDrv.HTTPDownload]:UseCompression=False"] if http else []).start()
    s.wait(r"TcpNetDriver on port", 60)
    a = Proc(ck, "NetD", "client", "127.0.0.1:%d?Name=NetD" % PORT,
             extra=["-ini:System:[Core.System]:CachePath=../" + os.path.basename(cachedir)]).start()
    procs = [s, a]
    try:
        sp = s.wait(r"Server Package: %s" % DLPKG, 5, from_start=True)
        result(bool(sp), ck + ".serverpackage", sp.group(0) if sp else "no 'Server Package: %s' in the server log" % DLPKG)
        dlwarn = s.count(r"Failed to load .*HTTPDownload")   # a misregistered HTTPDownload logs "Failed to load 'Class IpDrv.IpDrv.HTTPDownload'"
        rcv = a.wait(r"Receiving package '%s'" % DLPKG, 60)
        j = s.wait(r"Join succeeded: NetD", 120)
        # FGuid::String() prints the four DWORDs as %08X each; the file's GUID bytes are those DWORDs little-endian.
        g = bytes.fromhex(guid)
        gstr = "".join("%08X" % int.from_bytes(g[k:k + 4], "little") for k in range(0, 16, 4))
        cached = os.path.join(cachedir, gstr + ".uxx")
        same = os.path.exists(cached) and open(cached, "rb").read() == open(pkg, "rb").read()
        ini_txt = open(os.path.join(cachedir, "cache.ini"), "rb").read().decode("latin-1") if os.path.exists(
            os.path.join(cachedir, "cache.ini")) else ""
        result(bool(rcv) and same and (gstr + "=" + DLPKG) in ini_txt, ck + ".received",
               "client: %s; cache file %s %s; cache.ini %s" % (rcv.group(0) if rcv else "no 'Receiving package' line",
               os.path.basename(cached), "byte-identical to the server's" if same else "MISSING or different",
               ini_txt.strip().replace("\n", " | ") or "missing"))
        if http:
            httpd_out.flush()
            got = open(os.path.join(OUT, ck + "-httpd.out")).read()
            # UHTTPDownload escapes every character but [A-Za-z0-9] (FormatFilename): the dot arrives as %2e.
            m = re.search(r'"GET /%s(\.|%%2e)utx HTTP/1\.\d" 200' % DLPKG, got)
            fallback = a.count(r"HTTPDownload: .* failed")
            result(bool(m) and dlwarn == 0 and fallback == 0, ck + ".http",
                   "HTTP server: %s; client 'HTTPDownload: ... failed' lines: %d; server 'Failed to load ...HTTPDownload': %d"
                   % (m.group(0) if m else "no GET of the package", fallback, dlwarn))
        else:
            snd = s.count(r"^DevNet: .*%s" % DLPKG)
            result(dlwarn == 0, ck + ".dlmgr", "server 'Failed to load ...HTTPDownload' lines (main before D-2 had one per client): %d; "
                   "server DevNet lines naming the package: %d" % (dlwarn, snd))
        lv = a.probe("D0") if j else None
        result(bool(j) and bool(lv) and lv["summary"].get("level") == "DM-Deadbolt", ck + ".join",
               "server: %s; client probe level=%s" % (j.group(0) if j else "no 'Join succeeded: NetD' in 120 s",
               lv["summary"].get("level") if lv else None))
    finally:
        finish(ck, procs)
        if httpd:
            httpd.terminate()
            httpd.wait(5)
            httpd_out.close()


def check_splitclient():
    """The bounded split-screen client test: a listen host and one
    client process with two local players who join through the client's menus. On a client a split player can only be
    made in the menus (UClient::BeginSplitScreen asks the GUI's AllowControllerJoin: the character page; the in-match
    join is for a standalone game or a listen host), so the client walks player one's pad (port 0) from the main menu to
    the character page (VERSUS -> DeathMatch -> DM-Deadbolt, as g6-splitscreen.sh's x1menu4), pad 1's Start joins player
    two there, and then the client connects to the host (console OPEN): after the travel its two viewports are matched
    to players (MatchSplitViewportsToActors -> CreateSplitPlayer -> "JOINSPLIT GAMEPAD=1 URL=..." -> ULevel's JOINSPLIT:
    a child connection, SpawnPlayActor). Checked: the join on the page, the host's JOINSPLIT line, the client's two split
    views, the split player spawns (the host's NETPROBE: three human players) and moves, no Critical."""
    ck = "splitclient"
    l = Proc(ck, "L", "listen", "DM-Deadbolt?Listen?Name=NetL").start()
    procs = [l]

    def press(c, pad, button, after=1.5):
        c.input("pad %s button %s 1" % (pad, button))
        time.sleep(0.15)
        c.input("pad %s button %s 0" % (pad, button))
        time.sleep(after)

    def item(c, tag):
        c.input("probe " + tag)
        m = c.wait(r"probe %s level=\S+ menu=(\S+) .*? item=\"([^\"]*)\"" % re.escape(tag), 15)
        return (m.group(1), m.group(2)) if m else (None, None)

    def seek(c, cond, tries=12):
        for i in range(tries):
            mi = item(c, "s%d" % i)
            if mi[1] is not None and cond(mi[1]):
                return True
            press(c, 0, "dpdown", 0.6)
        return False

    try:
        l.wait(r"TcpNetDriver on port|Listen", 90)
        l.send("exec UNSUPPRESS EVENT=DevSplitScreen")
        l.input("pad attach")
        b = Proc(ck, "NetB", "client", "", render="gl").start()
        procs.append(b)
        up = b.wait(r"OpenMenu \[UCMenu.UC2MainMenu\]", 240)
        for ev in ("DevSplitScreen", "DevUI", "DevGUI"):
            b.send("exec UNSUPPRESS EVENT=" + ev)
        time.sleep(4)
        b.input("pad 0 attach")
        time.sleep(1)
        b.input("pad 1 attach")
        time.sleep(1)
        press(b, 0, "dpdown", 0.6)
        press(b, 0, "a", 1)
        ok = bool(up) and bool(b.wait(r"OpenMenu \[UCMenu.UC2Arena_Main\]", 20))
        if b.wait(r"OpenMenu \[GUI.GUIMessageBox\]", 20):
            b.wait(r"GUIMessageBox Unblocking animation", 30)
            time.sleep(0.5)
            press(b, 0, "b", 1.5)
        ok = ok and seek(b, lambda s: s.startswith("DeathMatch|"))
        press(b, 0, "a", 2)
        press(b, 0, "a", 2)
        ok = ok and seek(b, lambda s: "DM-Deadbolt" in s, 40)
        press(b, 0, "a", 1)
        cp = b.wait(r"OpenMenu \[UCMenu.UC2Arena_Character\]", 20)
        result(bool(cp), ck + ".client-menus-to-the-character-page", (cp.group(0) if cp else "did not reach the character page") +
               ("" if ok else " (a menu step was not confirmed on the way)"))
        time.sleep(4)
        press(b, 1, "start", 3)
        cj = b.wait(r"Split screen: pad 1 Start in the menus: .*", 15, from_start=True)
        result(bool(cj) and "joined" in cj.group(0), ck + ".client-player-two-joins-on-the-page", cj.group(0) if cj else "no 'Split screen: pad 1' line on the client")
        if b.wait(r"OpenMenu \[GUI.GUIMessageBox\]", 8):
            time.sleep(2)
            press(b, 1, "b", 2)
        b.send("exec open 127.0.0.1:%d?Name=NetB" % PORT)
        j = l.wait(r"Join succeeded: .*", 90)
        result(bool(j), ck + ".join", "listen server: %s" % (j.group(0) if j else "no join in 90 s"))
        js = l.wait(r"JOINSPLIT: (Succeeded|Join failure|Already have).*", 60, from_start=True)
        result(bool(js) and js.group(1) == "Succeeded", ck + ".joinsplit", "listen server: %s" % (js.group(0) if js else "no JOINSPLIT line in 60 s"))
        humans, pawned = [], []
        for i in range(12):
            for c, pads in ((l, ("",)), (b, ("", "1 "))):
                for pd in pads:
                    c.input("pad %saxis righttrigger 32767" % pd)
            time.sleep(0.3)
            for c, pads in ((l, ("",)), (b, ("", "1 "))):
                for pd in pads:
                    c.input("pad %saxis righttrigger 0" % pd)
            time.sleep(1.5)
            probe = l.probe("S%d" % i)
            humans = [n for n, r in (probe["pris"] if probe else {}).items() if r.get("bot") == "0" and r.get("spectator") == "0"]
            pawned = [n for n in humans if n in (probe["pawns"] if probe else {})]
            if len(pawned) >= 3:
                break
        views = re.findall(r"Split screen: \d+ views[^\n]*", b.text())
        after = b.text()[b.text().find("Welcomed by server"):]
        result(len(pawned) >= 3, ck + ".split-player-spawns",
               "listen server's human players %s, with a pawn %s; client's last '%s'; after the welcome the client logged %d "
               "'Split player spawn URL' lines" % (humans, pawned, views[-1][:40] if views else "no 'Split screen: N views' line",
                                                  after.count("Split player spawn URL")))
        first = j.group(0).split(": ", 1)[1].strip() if j else "NetB"
        new = [n for n in pawned if n not in ("NetL", first)]
        if len(pawned) >= 3 and new:
            a = l.probe("M0")
            b.input("pad 1 axis lefty -32767")
            time.sleep(1.5)
            b.input("pad 1 axis lefty 0")
            time.sleep(1.0)
            c = l.probe("M1")
            pa, pc = pawn_of(a, new[0]), pawn_of(c, new[0])
            d = dist(pa["loc"], pc["loc"]) if pa and pc else 0
            result(d > 100, ck + ".split-player-moves", "%s moved %.1f units on the listen server (the client's pad 1 stick)" % (new[0], d))
    finally:
        finish(ck, procs, allow_invalid_class=2)


ALL = {"cheats": check_cheats, "dedicated1": check_dedicated1, "dedicated2": check_dedicated2, "listen": check_listen,
       "download": check_download, "download-http": lambda: check_download(True),
       "profile-login": check_profile_login,
       "travel": check_travel, "disconnect": check_disconnect, "soak": check_soak, "lag": check_lag,
       "glshot": check_glshot, "wine": check_wine, "splitclient": check_splitclient,
       "handshake-loss": check_handshake_loss, "handshake-compat": lambda: check_handshake_loss(True)}

if __name__ == "__main__":
    for c in CHECKS:
        if c not in ALL:
            print("unknown check %s (have: %s)" % (c, " ".join(ALL)), file=sys.stderr)
            sys.exit(2)
    for c in CHECKS:
        try:
            ALL[c]()
        except Exception as e:  # a harness error is a FAIL of that check, the others still run
            import traceback
            traceback.print_exc()
            result(False, c + ".harness", "exception %r" % e)
    sys.exit(0 if all(results) else 1)
