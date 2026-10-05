#!/usr/bin/env python3
"""Network play tests: servers and clients of the native build on 127.0.0.1 (driven by tools/dev/net-test.sh,
which runs this inside ONE bin/capped scope; not meant to run on its own).

    net-test.py <run-tree System dir> <out dir> <check> [<check> ...]

Every process runs from the same System/ with its own ini, user ini and log (INI=, USERINI=, LOG=: Epic's
appInit / FOutputDeviceFile options) and its own command files:
  -nettest=<file>    the engine's test command file (Engine/Src/UC2NetTest.cpp): "exec netprobe <tag>",
                     "exec netplacenear ...", "exec servertravel ..." -- every process has one
  -inputtest=<file>  SDL2Drv's input test channel (clients and the listen server): the virtual pad
Each client and the listen server has its own Xvfb display (xvfb-run -a), so a virtual pad never depends on
which window has the focus. Results: one "PASS|FAIL <check>.<item>: <details>" line per item on stdout and in
<out>/net-summary.txt; logs as <out>/<check>-<proc>.log/.out. Decided by log lines and probe values only.
"""
import os, re, signal, subprocess, sys, time

SYS, OUT = sys.argv[1], sys.argv[2]
CHECKS = sys.argv[3:]
SUMMARY = os.path.join(OUT, "net-summary.txt")
PORT = int(os.environ.get("NET_PORT", "7850"))
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

    def __init__(self, check, name, kind, url, render="null", extra=()):
        self.check, self.name, self.kind, self.url = check, name, kind, url
        self.render, self.extra = render, list(extra)
        self.logname = "Net%s.log" % name
        self.log = os.path.join(SYS, self.logname)
        self.netcmd = os.path.join(SYS, "net-%s.cmd" % name)
        self.inputcmd = os.path.join(SYS, "net-%s-input.cmd" % name)
        self.pos = 0
        self.popen = None

    def start(self):
        for f in ("Net%s.ini" % self.name, "Net%sUser.ini" % self.name, self.logname):
            p = os.path.join(SYS, f)
            if os.path.exists(p):
                os.remove(p)
        open(self.netcmd, "w").close()
        open(self.inputcmd, "w").close()
        common = ["INI=Net%s.ini" % self.name, "USERINI=Net%sUser.ini" % self.name, "LOG=" + self.logname,
                  "-nettest=" + os.path.basename(self.netcmd)]
        if self.kind == "server":
            argv = ["./ucc-bin", "server", self.url, "PORT=%d" % PORT] + common
        else:
            argv = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24", "./uc2.bin", self.url, "-log"] + common
            argv += ["-inputtest=" + os.path.basename(self.inputcmd)]
            if self.kind == "listen":
                argv += ["PORT=%d" % PORT]
            if self.render == "null":
                argv += ["-nullrenderer"]
        argv += self.extra
        env = dict(os.environ, SDL_AUDIODRIVER="dummy")
        self.out = open(os.path.join(OUT, "%s-%s.out" % (self.check, self.name)), "w")
        self.popen = subprocess.Popen(argv, cwd=SYS, stdout=self.out, stderr=subprocess.STDOUT, env=env,
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


def server(check, url="DM-Deadbolt", extra=()):
    s = Proc(check, "srv", "server", url, extra=extra).start()
    if not s.wait(r"TcpNetDriver on port|Bringing Level .* up for play", 60):
        return s
    s.wait(r"TcpNetDriver on port", 20, from_start=True)
    return s


def client(check, name, render="null", extra=(), host="127.0.0.1"):
    return Proc(check, name, "client", "%s:%d?Name=%s" % (host, PORT, name), render=render, extra=extra).start()


def spawn(c, tag, tries=10):
    """Press Fire until the client's own probe shows its pawn; returns the probe or None."""
    c.input("pad attach")
    time.sleep(1.0)
    for i in range(tries):
        c.fire()
        time.sleep(1.5)
        p = c.probe("%s-%d" % (tag, i))
        if p and p["summary"].get("localpawn") == "1":
            return p
    return None


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
    for p in procs:
        if p.alive():
            p.stop()
    bad = crit_lines(procs)
    inv = [b for b in bad if "invalid actor class" in b]
    rest = [b for b in bad if "invalid actor class" not in b]
    result(not rest and len(inv) <= allow_invalid_class, check + ".clean",
           "Critical/assert/overflow/bunch lines: %d%s; 'Received invalid actor class': %d (allowed %d)"
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


def fight(ck, srvproc, attacker, victim, aname, vname, max_shots=40):
    """attacker aims at victim and fires until the victim dies on the server; returns (first-hit, kill) dicts."""
    sp0 = srvproc.probe("F0")
    v0 = pawn_of(sp0, vname)
    h0 = int(v0["health"]) if v0 else None
    first_hit = None
    kill = None
    deaths0 = num(sp0["pris"].get(vname, {}).get("deaths")) if sp0 else None
    for i in range(max_shots):
        attacker.send("exec netaim " + vname)
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
        pa, pb = spawn(a, "Aspawn"), spawn(b, "Bspawn")
        result(bool(pa and pb), ck + ".spawn", "NetA localpawn=%s, NetB localpawn=%s"
               % (bool(pa), bool(pb)))
        s.send("exec netplacenear NetB NetA 450")
        pl = s.wait(r"^NetProbe: placenear NetB (ok|failed.*)", 15)
        result(bool(pl) and pl.group(1).startswith("ok"), ck + ".place", "server: %s" % (pl.group(0) if pl else None))
        time.sleep(1.5)
        sp, ap, bp = s.probe("P1"), a.probe("P1"), b.probe("P1")
        sB, aB = pawn_of(sp, "NetB"), pawn_of(ap, "NetB")
        sA, bA = pawn_of(sp, "NetA"), pawn_of(bp, "NetA")
        d1 = dist(sB["loc"], aB["loc"]) if sB and aB else 1e9
        d2 = dist(sA["loc"], bA["loc"]) if sA and bA else 1e9
        result(d1 < 40 and d2 < 40, ck + ".seeother", "NetA sees NetB %.1f units from the server's NetB; NetB sees NetA %.1f from the server's"
               % (d1, d2))
        # Movement of B changes what A sees.
        b.input("pad axis lefty -32768")
        time.sleep(1.2)
        b.input("pad axis lefty 0")
        time.sleep(1.0)
        sp2, ap2 = s.probe("P2"), a.probe("P2")
        sB2, aB2 = pawn_of(sp2, "NetB"), pawn_of(ap2, "NetB")
        mv = dist(aB["loc"], aB2["loc"]) if aB and aB2 else 0
        d3 = dist(sB2["loc"], aB2["loc"]) if sB2 and aB2 else 1e9
        result(mv > 100 and d3 < 40, ck + ".othermoves", "NetB walked: NetA's view of NetB moved %.1f units, %.1f from the server's"
               % (mv, d3))
        # Put B back in front of A and shoot it.
        s.send("exec netplacenear NetB NetA 450")
        s.wait(r"^NetProbe: placenear NetB", 15)
        time.sleep(1.0)
        first, kill = fight(ck, s, a, b, "NetA", "NetB")
        time.sleep(1.0)
        ap3, bp3 = a.probe("K1"), b.probe("K1")
        result(bool(first), ck + ".hit", "NetB health on the server: %s" % first)
        if first and first["health"] > 0:
            pass
        sp3 = kill["probe"] if kill else s.probe("K1s")
        def sc(p, who, field):
            return num(p["pris"].get(who, {}).get(field)) if p else None
        scores = [(sc(x, "NetA", "score"), sc(x, "NetB", "deaths")) for x in (sp3, ap3, bp3)]
        ok = bool(kill) and all(sa is not None and sa >= 1 and db is not None and db >= 1 for sa, db in scores)
        result(ok, ck + ".kill", "after %s shots: (NetA score, NetB deaths) server %s, NetA's view %s, NetB's view %s"
               % (kill and kill["shot"], scores[0], scores[1], scores[2]))
        # Health seen by the victim's own client and by the attacker: compared in a separate shot sequence after respawn.
        pb2 = spawn(b, "Brespawn")
        if pb2:
            s.send("exec netplacenear NetB NetA 450")
            s.wait(r"^NetProbe: placenear NetB", 15)
            time.sleep(1.0)
            h_before = pawn_of(s.probe("H0"), "NetB")
            got = None
            for i in range(20):
                a.send("exec netaim NetB")
                time.sleep(0.2)
                a.fire(0.3)
                time.sleep(1.0)
                sv, av, bv = s.probe("H%d" % (i + 1)), a.probe("H%d" % (i + 1)), b.probe("H%d" % (i + 1))
                hs, ha, hb = (pawn_of(x, "NetB") for x in (sv, av, bv))
                if hs and h_before and 0 < int(hs["health"]) < int(h_before["health"]):
                    got = (int(h_before["health"]), int(hs["health"]), ha and int(ha["health"]), hb and int(hb["health"]))
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
        pl, pb = spawn(l, "Lspawn"), spawn(b, "Bspawn")
        result(bool(pl and pb), ck + ".spawn", "NetL localpawn=%s NetB localpawn=%s" % (bool(pl), bool(pb)))
        l.send("exec netplacenear NetB NetL 450")
        pm = l.wait(r"^NetProbe: placenear NetB (ok|failed.*)", 15)
        result(bool(pm) and pm.group(1).startswith("ok"), ck + ".place", pm.group(0) if pm else "no answer")
        time.sleep(1.5)
        lp, bp = l.probe("P1"), b.probe("P1")
        d1 = dist(pawn_of(lp, "NetB")["loc"], pawn_of(bp, "NetB")["loc"]) if pawn_of(lp, "NetB") and pawn_of(bp, "NetB") else 1e9
        d2 = dist(pawn_of(lp, "NetL")["loc"], pawn_of(bp, "NetL")["loc"]) if pawn_of(lp, "NetL") and pawn_of(bp, "NetL") else 1e9
        result(d1 < 40 and d2 < 40, ck + ".seeother", "NetB: client vs listen server %.1f units; NetL: %.1f" % (d1, d2))
        b.input("pad axis lefty -32768")
        time.sleep(1.2)
        b.input("pad axis lefty 0")
        time.sleep(1.0)
        lp2 = l.probe("P2")
        mv = dist(pawn_of(lp, "NetB")["loc"], pawn_of(lp2, "NetB")["loc"]) if pawn_of(lp, "NetB") and pawn_of(lp2, "NetB") else 0
        result(mv > 100, ck + ".clientmoves", "the listen server sees NetB move %.1f units" % mv)
        l.send("exec netplacenear NetB NetL 450")
        l.wait(r"^NetProbe: placenear NetB", 15)
        time.sleep(1.0)
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
        cj = s.wait(r"Join succeeded: NetA", 120)
        result(bool(cl and cj), ck + ".clientfollows", "client: %s; server: %s" % (cl and cl.group(0), cj and cj.group(0)))
        lv = a.probe("T1")
        result(bool(lv) and lv["summary"].get("level") == "DM-Sever", ck + ".clientlevel",
               "client probe level=%s" % (lv and lv["summary"].get("level")))
        p2 = spawn(a, "Aspawn2")
        sp = s.probe("T2")
        result(bool(p2) and bool(pawn_of(sp, "NetA")), ck + ".spawn2", "client localpawn=%s, server pawn NetA at %s"
               % (bool(p2), pawn_of(sp, "NetA") and pawn_of(sp, "NetA").get("loc")))
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
        spawn(a, "Aspawn")
        spawn(b, "Bspawn")
        p0 = s.probe("D0")
        result(bool(p0) and p0["summary"].get("conns") == "2", ck + ".twojoined", "server conns=%s" % (p0 and p0["summary"].get("conns")))
        # A leaves cleanly: the console command DISCONNECT, then the process quits.
        a.send("exec disconnect")
        time.sleep(4)
        p1 = s.probe("D1")
        result(bool(p1) and p1["summary"].get("conns") == "1" and "NetA" not in p1["pris"], ck + ".clean",
               "server after NetA's DISCONNECT: conns=%s, PRIs %s" % (p1 and p1["summary"].get("conns"), p1 and sorted(p1["pris"])))
        a.stop()
        # B is killed: the server must drop it after ConnectionTimeout.
        b.stop(signal.SIGKILL, grace=5)
        t0 = time.time()
        dropped = None
        while time.time() - t0 < 75:
            p2 = s.probe("D2-%d" % int(time.time() - t0))
            if p2 and p2["summary"].get("conns") == "0":
                dropped = time.time() - t0
                break
            time.sleep(3)
        result(dropped is not None and s.alive(), ck + ".timeout",
               "server dropped the killed NetB after %s s (ConnectionTimeout 30), server alive=%s" % (dropped and round(dropped), s.alive()))
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
    s = server(ck, url="DM-Deadbolt?NumBots=2")
    a = client(ck, "NetA")
    b = client(ck, "NetB")
    procs = [s, a, b]
    try:
        s.wait(r"Join succeeded: ", 60)
        s.wait(r"Join succeeded: ", 60)
        spawn(a, "Aspawn")
        spawn(b, "Bspawn")
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
    check_dedicated2("lag", srv_extra=("PktLag=50", "PktLoss=5"), cli_extra=("PktLag=50", "PktLoss=5"))


def check_glshot():
    ck = "glshot"
    s = server(ck)
    a = client(ck, "NetA", render="gl")
    b = client(ck, "NetB")
    procs = [s, a, b]
    try:
        s.wait(r"Join succeeded: ", 90)
        s.wait(r"Join succeeded: ", 90)
        pa, pb = spawn(a, "Aspawn", tries=15), spawn(b, "Bspawn")
        result(bool(pa and pb), ck + ".spawn", "NetA (GL) localpawn=%s, NetB localpawn=%s" % (bool(pa), bool(pb)))
        s.send("exec netplacenear NetB NetA 350")
        s.wait(r"^NetProbe: placenear NetB", 15)
        time.sleep(2.0)
        a.send("exec netaim NetB")
        time.sleep(2.0)
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


ALL = {"dedicated1": check_dedicated1, "dedicated2": check_dedicated2, "listen": check_listen,
       "travel": check_travel, "disconnect": check_disconnect, "soak": check_soak, "lag": check_lag,
       "glshot": check_glshot}

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
