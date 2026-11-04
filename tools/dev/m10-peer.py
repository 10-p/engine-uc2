# A second game for the network scenarios of tools/dev/m10-controls.sh (loaded by m10-inject.py's 'pyfile'; not run on
# its own): the peer, a client of the listen server the scenario's game hosts, on this run tree, on SDL's offscreen
# video with the null renderer and its own inis and log (Peer<name>.ini, Peer<name>User.ini, Peer<name>.log). It joins
# 127.0.0.1:<M10_NET_PORT> and takes System Link's beacon ports from M10_NET_PORT+1 and +2 (default 7880-7882).
#
#   peer_start(name)     start it (its virtual pad is on SDL2Drv's test channel Peer<name>-input.cmd)
#   spawn_both()         the peer's Fire (its pad's right trigger) and this game's left click: a deathmatch starts once
#                        every player has pressed Fire
#   peer_freeze()        SIGSTOP: its socket stays open, so the host only stops hearing from it
#   peer_thaw()          SIGCONT
#   peer_stop()          SIGTERM, then SIGKILL after 15 s
#
# The peer is signalled by the PID it was started with, checked against a ps listing first. It runs in the scenario's
# bin/capped scope, so nothing outlives the run. Keep a freeze short: a game stopped for half a minute is killed by the
# kernel when it resumes (OpenAL Soft's mixer thread runs at real-time priority, and catching up on the stopped time
# takes it past RLIMIT_RTTIME).
import os, signal, subprocess, time

PORT = int(os.environ.get('M10_NET_PORT', '7880'))
PEER = {}


def peer_start(name):
    sysdir = os.getcwd()
    log, chan = 'Peer%s.log' % name, 'Peer%s-input.cmd' % name
    for f in ('Peer%s.ini' % name, 'Peer%sUser.ini' % name, log):
        if os.path.exists(os.path.join(sysdir, f)):
            os.remove(os.path.join(sysdir, f))
    open(os.path.join(sysdir, chan), 'w').close()
    argv = ['./uc2.bin', '127.0.0.1:%d?Name=%s' % (PORT, name), '-log', 'INI=Peer%s.ini' % name,
            'USERINI=Peer%sUser.ini' % name, 'LOG=' + log, '-UFRONTLOG', '-nullrenderer',
            '-ini:System:[UC2Movie]:StartupMovies=False', '-ini:System:[SDL2Drv.SDL2Client]:InputTestFile=' + chan,
            '-ini:System:[IpDrv.UdpBeacon]:ServerBeaconPort=%d' % (PORT + 1),
            '-ini:System:[IpDrv.UdpBeacon]:BeaconPort=%d' % (PORT + 2)]
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
               SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT='0x0000/0x0000')
    out = open(os.path.join(sysdir, 'Peer%s.out' % name), 'w')
    PEER.update(p=subprocess.Popen(argv, cwd=sysdir, stdout=out, stderr=subprocess.STDOUT, env=env,
                                   start_new_session=True),
                name=name, chan=os.path.join(sysdir, chan), pad=False)
    print('  peer started: pid %d: %s' % (PEER['p'].pid, ' '.join(argv)), flush=True)


def peer_fire(hold=0.25):
    if not PEER['pad']:
        with open(PEER['chan'], 'a') as f:
            f.write('pad attach\n')
        PEER['pad'] = True
        time.sleep(1.0)
    for line, wait in (('pad axis righttrigger 32767', hold), ('pad axis righttrigger 0', 0)):
        with open(PEER['chan'], 'a') as f:
            f.write(line + '\n')
        time.sleep(wait)


def spawn_both():
    import __main__
    peer_fire()
    __main__.execute('click 1 1.0')


def _peer_pid():
    pid = PEER['p'].pid
    if PEER['p'].poll() is not None:
        raise RuntimeError('peer: pid %d has exited with status %d' % (pid, PEER['p'].returncode))
    listing = subprocess.run(['ps', '-eo', 'pid,args'], capture_output=True, text=True).stdout.splitlines()
    mine = [l.strip() for l in listing if l.split(None, 1)[0] == str(pid)]
    if not mine or 'LOG=Peer%s.log' % PEER['name'] not in mine[0]:
        raise RuntimeError('peer: pid %d is not the peer (%s)' % (pid, mine[0] if mine else 'not listed'))
    print('  ps: %s' % mine[0][:120], flush=True)
    return pid


def peer_freeze():
    os.kill(_peer_pid(), signal.SIGSTOP)
    print('  peer frozen', flush=True)


def peer_thaw():
    os.kill(_peer_pid(), signal.SIGCONT)
    print('  peer thawed', flush=True)


def peer_stop():
    pid = _peer_pid()
    os.kill(pid, signal.SIGCONT)
    os.kill(pid, signal.SIGTERM)
    try:
        PEER['p'].wait(15)
    except subprocess.TimeoutExpired:
        os.kill(pid, signal.SIGKILL)
        PEER['p'].wait(5)
    print('  peer stopped', flush=True)
