#!/usr/bin/env python3
"""One unattended frame-pacing run of UC2, on Windows or Linux: the game starts on a map with no bots, spawns, stands
at a fixed spot (TELEPORT), turns at a steady rate on a virtual pad's look stick, records the pace trace (PACETRACE,
Source/Engine/Src/UC2PaceTrace.cpp) and quits. With --presentmon, PresentMon records the same run (Windows, from an
elevated shell). Input goes through the engine's input test channel ([SDL2Drv.SDL2Client] InputTestFile,
Source/SDL2Drv/Src/SDL2InputTest.h); the game's log is followed as it is written.

    python pace-run.py --exe <game copy>/System/uc2.exe --name vsync-on --vsync 1
    python pace-run.py --exe ... --name cap60 --cap 60 --presentmon C:/Tools/PresentMon-2.6.0-x64.exe
    python pace-run.py --exe ... --name vsync-step0 --vsync 1 --set SDLLaunch:VSyncStep=0

Options:
  --vsync 0|1               [UC2GLDrv.UC2GLRenderDevice] UseVSync (default 0)
  --cap <fps>               [Engine.Engine] FrameRateCap (default 0: uncapped)
  --windowed                a window instead of fullscreen at the desktop's resolution; --res WxH sets its size
  --frames <n>              frames to record (default 1200)
  --set Section:Key=Value   one more -ini:System: override, e.g. UC2GLDrv.UC2GLRenderDevice:VSyncFinish=2 (repeatable)
  --map <map>               default DM-Apex
  --teleport "x y z yaw pitch"   default the DM-Apex spot of every earlier run
  --turn <value>            the look stick during the trace, -32768..32767 (default 6000; 0: stand still)
  --presentmon <exe>        PresentMon 2.x console build: records System/pm-<name>.csv, and the run prints the
                            buffers its trace session lost after the trace
  --presentmon-arg=<arg>    one more PresentMon argument, e.g. --presentmon-arg=--qpc_time_ms (repeatable)
  --timeout <seconds>       the whole run's limit (default 420); the game is ended when it is reached
  --wrapper <program>       start the exe through it (wine, to try a Windows build on Linux)
Writes, beside the exe: pace-<name>.log (the game's log with the trace, tag p), pace-<name>.channel (the commands
sent) and pm-<name>.csv; files of the same name are replaced. Prints the trace's summary line, then the analysis by
pace-analyze.py when it is beside this script. Exit 0 with a trace, 1 without one.
"""
import argparse
import os
import re
import subprocess
import sys
import time

DEFAULT_SPOT = '1218 952 117.9 38972 0'


def game_window(pid):
    """Windows: the game's visible SDL window, or None."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL('user32')
    found = []
    proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def each(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, name, 64)
        if owner.value == pid and user32.IsWindowVisible(hwnd) and name.value == 'SDL_app':
            found.append(hwnd)
        return True
    user32.EnumWindows(proc(each), 0)
    return found[0] if found else None


def is_foreground(pid):
    """Whether the game's window has the foreground (always true off Windows)."""
    if os.name != 'nt':
        return True
    import ctypes
    hwnd = game_window(pid)
    return bool(hwnd) and ctypes.WinDLL('user32').GetForegroundWindow() == hwnd


def bring_to_foreground(pid, timeout=15.0):
    """Windows: gives the game's window the foreground, as a click on it would. A program started from a background
    process opens behind the foreground one, and then SDL drops the pad's events and a fullscreen window is composed.
    Windows lets a process take the foreground once a key it sent went through, so an Alt press and release goes
    first. Returns whether the window has the foreground."""
    if os.name != 'nt':
        return True
    import ctypes
    user32 = ctypes.WinDLL('user32')
    end = time.time() + timeout
    while time.time() < end:
        hwnd = game_window(pid)
        if hwnd:
            user32.keybd_event(0x12, 0, 0, 0)          # VK_MENU down
            user32.keybd_event(0x12, 0, 2, 0)          # KEYEVENTF_KEYUP
            user32.SetForegroundWindow(hwnd)
            time.sleep(0.5)
            if user32.GetForegroundWindow() == hwnd:
                return True
        time.sleep(0.5)
    return False


def etw_lost():
    """Windows: the buffers PresentMon's trace session lost so far (logman's count), as text. Presents in a lost buffer
    are missing from the CSV, and the screen seems to hold the picture before them."""
    try:
        out = subprocess.run(['logman', 'query', 'PresentMon', '-ets'], capture_output=True, text=True,
                             timeout=20).stdout
    except (OSError, subprocess.TimeoutExpired) as e:
        return f'trace session not queried: {e}'
    m = re.search(r'Buffers Lost:\s*(\d+)', out)
    return f'trace session buffers lost: {m.group(1)}' if m else 'trace session buffers lost: unknown'


class Run:
    def __init__(self, game, log_path, chan_path, deadline):
        self.game = game
        self.log_path = log_path
        self.chan_path = chan_path
        self.deadline = deadline
        self.pos = 0
        self.partial = ''
        self.pending = []   # lines read but not yet searched

    def chan(self, line):
        with open(self.chan_path, 'a', newline='\n') as f:
            f.write(line + '\n')
        print(f'{time.strftime("%H:%M:%S")} > {line}', flush=True)

    def new_lines(self):
        try:
            with open(self.log_path, 'rb') as f:
                f.seek(self.pos)
                data = f.read()
        except FileNotFoundError:
            return []
        self.pos += len(data)
        text = self.partial + data.decode('latin-1')
        lines = text.split('\n')
        self.partial = lines.pop()
        return [l.rstrip('\r') for l in lines]

    def wait(self, pattern, timeout):
        rx = re.compile(pattern)
        end = min(self.deadline, time.time() + timeout)
        while time.time() < end:
            self.pending += self.new_lines()
            while self.pending:
                line = self.pending.pop(0)
                if rx.search(line):
                    print(f'{time.strftime("%H:%M:%S")} < {line.strip()}', flush=True)
                    return line
            if self.game.poll() is not None:
                raise SystemExit(f'the game exited (code {self.game.returncode}) while waiting for: {pattern}')
            time.sleep(0.1)
        raise TimeoutError(f'no log line matching {pattern!r} within {timeout} s')


def main():
    ap = argparse.ArgumentParser(description='One unattended UC2 frame-pacing run (see the header).')
    ap.add_argument('--exe', required=True)
    ap.add_argument('--name', required=True)
    ap.add_argument('--vsync', type=int, default=0, choices=(0, 1))
    ap.add_argument('--cap', type=float, default=0.0)
    ap.add_argument('--windowed', action='store_true')
    ap.add_argument('--res', default='')
    ap.add_argument('--frames', type=int, default=1200)
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--map', default='DM-Apex')
    ap.add_argument('--teleport', default=DEFAULT_SPOT)
    ap.add_argument('--turn', type=int, default=6000)
    ap.add_argument('--presentmon', default='')
    ap.add_argument('--presentmon-arg', action='append', default=[])
    ap.add_argument('--timeout', type=float, default=420.0)
    ap.add_argument('--wrapper', default='', help='a program to start the exe with, e.g. wine on Linux')
    a = ap.parse_args()

    exe = os.path.abspath(a.exe)
    sysdir = os.path.dirname(exe)
    log_name, chan_name = f'pace-{a.name}.log', f'pace-{a.name}.channel'
    log_path, chan_path = os.path.join(sysdir, log_name), os.path.join(sysdir, chan_name)
    for p in (log_path, chan_path):
        if os.path.exists(p):
            os.remove(p)
    open(chan_path, 'w').close()

    vs = 'True' if a.vsync else 'False'
    # Relative paths: the game runs in its System folder, and a drive letter's colon would end an -ini: value.
    args = [exe, f'{a.map}?Name=PaceCam?NumBots=0', f'-LOG={log_name}', '-UFRONTLOG',
            '-ini:System:[UC2Movie]:StartupMovies=False',
            f'-ini:System:[SDL2Drv.SDL2Client]:InputTestFile={chan_name}',
            '-ini:System:[SDL2Drv.SDL2Client]:CaptureMouse=False',
            f'-ini:System:[UC2GLDrv.UC2GLRenderDevice]:UseVSync={vs}',
            f'-ini:System:[Engine.Engine]:FrameRateCap={a.cap:g}',
            f'-ini:System:[SDL2Drv.SDL2Client]:StartupFullscreen={"False" if a.windowed else "True"}']
    if a.windowed and a.res:
        w, h = a.res.lower().split('x')
        args += [f'-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX={w}',
                 f'-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportY={h}']
    for s in a.set:
        section, kv = s.split(':', 1)
        args.append(f'-ini:System:[{section}]:{kv}')

    pm = None
    if a.presentmon:
        pm_csv = os.path.join(sysdir, f'pm-{a.name}.csv')
        pm = subprocess.Popen([a.presentmon, '--stop_existing_session', '--terminate_on_proc_exit',
                               '--process_name', os.path.basename(exe), '--output_file', pm_csv] + a.presentmon_arg)
        time.sleep(2)
        if pm.poll() is not None:
            raise SystemExit(f'PresentMon exited at once (code {pm.returncode}): it needs an elevated shell')

    if a.wrapper:
        args.insert(0, a.wrapper)
    print(' '.join(args), flush=True)
    game = subprocess.Popen(args, cwd=sysdir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run = Run(game, log_path, chan_path, time.time() + a.timeout)
    traced = False
    try:
        run.wait(r'Bringing Level ' + re.escape(a.map) + r'\.MyLevel up for play', 180)
        time.sleep(6)
        if not bring_to_foreground(game.pid):
            raise SystemExit('the game window did not get the foreground')
        print(f'{time.strftime("%H:%M:%S")} = the game window has the foreground', flush=True)
        run.chan('pad attach')
        time.sleep(1)
        for _ in range(3):  # UC2 spawns the player on Fire some seconds into the match
            run.chan('pad axis righttrigger 32767')
            time.sleep(0.2)
            run.chan('pad axis righttrigger 0')
            time.sleep(1)
        time.sleep(2)
        run.chan('exec God')
        time.sleep(0.5)
        run.chan('exec SetAutoCenterPitch 0')
        time.sleep(0.5)
        for _ in range(2):
            run.chan(f'exec TELEPORT {a.teleport}')
            time.sleep(3)
        if a.turn:
            run.chan(f'pad axis rightx {a.turn}')
            time.sleep(1)
        run.wait(r'Cheat: TELEPORT ok', 10)
        focus_before = is_foreground(game.pid)
        run.chan(f'exec PACETRACE {a.frames} p')
        summary = run.wait(r'pacetrace p summary', a.frames / 20.0 + 60)
        run.wait(r'pacetrace p done after', 30)
        focus_after = is_foreground(game.pid)
        if pm is not None:
            print(f'{time.strftime("%H:%M:%S")} = PresentMon {etw_lost()}', flush=True)
        print(f'{time.strftime("%H:%M:%S")} = foreground at the trace start {focus_before}, at its end {focus_after}',
              flush=True)
        if not (focus_before and focus_after):
            print(f'run {a.name}: the game window lost the foreground during the trace', file=sys.stderr)
        traced = True
        run.chan('pad axis rightx 0')
        time.sleep(0.5)
        run.chan('exec exit')
        try:
            game.wait(30)
        except subprocess.TimeoutExpired:
            pass
    except (TimeoutError, SystemExit) as e:
        print(f'run {a.name}: {e}', file=sys.stderr)
    finally:
        if game.poll() is None:
            game.kill()
            game.wait()
        if pm is not None:
            try:
                pm.wait(30)
            except subprocess.TimeoutExpired:
                pm.terminate()

    if not traced:
        print(f'run {a.name}: no trace; see {log_path}', file=sys.stderr)
        return 1
    print(summary.strip())
    analyze = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pace-analyze.py')
    if os.path.exists(analyze):
        subprocess.call([sys.executable, analyze, log_path])
    return 0


if __name__ == '__main__':
    sys.exit(main())
