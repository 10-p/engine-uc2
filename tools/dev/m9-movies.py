#!/usr/bin/env python3
"""M9 / V5: the movies in the running game, every scenario repeatable, one PASS/FAIL line each.

  tools/dev/m9-movies.py [--build build-native] [--tree m9] [--only logos,cinematic,...] [--no-mkrun]

Rebuilds the run tree <WorkUC2>/run/<tree> from the build (tools/dev/mkrun-uc2.sh), then runs each scenario
headless (Xvfb, Mesa software GL) inside <WorkUC2>/bin/capped 2G, with OpenAL Soft's null backend or its wave
writer (-audiowave=, for the sound evidence). Writes logs, screenshots and captures to <WorkUC2>/logs/<tree>/
under stable names (m9-<scenario>.*). Decisions come from log lines and measurements:
  logos           start, advance (two engine screenshots at known movie times, each compared with the cooked
                  file's frame shown at that time: PSNR against its own frame and against the other's), end on
                  their own at the last frame
  logos-skip      a key 3 s into the logos ends them, the menu runs on
  menu-movie      the arena page (INSTANT ACTION) with its game-type portrait, a MovieMaterial: the material's frame
                  advances (log), and two screenshots 2 s apart differ inside the portrait, not in static parts
  cinematic       DM-Deadbolt?StartCinematic=04a_AscPre5: static loading screen, the cinematic full screen
                  with its international sound (capture: envelope correlation and gain against the cooked
                  track, tools/dev/audio-music-check.py), a screenshot matched to its frame, the level after it
  cinematic-skip  the same, skipped by a key 3 s in; the level runs after
  nullrenderer    the same URL with -nullrenderer: no movie activity, the level loads
  server          ucc-bin server with the same URL: no movie activity, the level loads
Nothing is left running: capped kills its whole process tree.
"""
import argparse, json, math, os, re, shutil, struct, subprocess, sys, time

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..'))
work = os.path.abspath(os.path.join(repo, '..'))
capped = os.path.join(work, 'bin', 'capped')
RESULTS = []


def result(name, ok, text):
    line = '%s m9-%s: %s' % ('PASS' if ok else 'FAIL', name, text)
    RESULTS.append(line)
    print(line, flush=True)


# ------------------------------------------------------------------------------------------------------------
# Running the game
# ------------------------------------------------------------------------------------------------------------

INNER = r'''
import ctypes, os, subprocess, sys, time
log, keyspec, args = sys.argv[1], sys.argv[2], sys.argv[3:]
env = dict(os.environ, SDL_AUDIODRIVER='dummy')
out = open(log, 'wb')
p = subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT, env=env)
# keyspec: "" or "<pattern>|<delay>|<keysym>[,<keysym>...]": press the keys (1.5 s apart) <delay> s after
# <pattern> appears in the log;
# "click|<pattern>|<delay>|x,y": click there instead
if keyspec:
    parts = keyspec.split('|')
    click = parts[0] == 'click'
    if click:
        parts = parts[1:]
    pattern, delay, what = parts[0], float(parts[1]), parts[2]
    seen = None
    while p.poll() is None:
        try:
            if pattern.encode() in open(log, 'rb').read():
                seen = time.time()
                break
        except OSError:
            pass
        time.sleep(0.1)
    if seen is not None:
        time.sleep(delay)
        x = ctypes.CDLL("libX11.so.6"); t = ctypes.CDLL("libXtst.so.6")
        x.XOpenDisplay.restype = ctypes.c_void_p
        x.XStringToKeysym.restype = ctypes.c_ulong
        x.XKeysymToKeycode.restype = ctypes.c_ubyte
        x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        d = ctypes.c_void_p(x.XOpenDisplay(None))
        if click:
            cx, cy = [int(v) for v in what.split(',')]
            t.XTestFakeMotionEvent(d, -1, cx, cy, 0); x.XFlush(d); time.sleep(0.3)
            t.XTestFakeButtonEvent(d, 1, 1, 0); x.XFlush(d); time.sleep(0.2)
            t.XTestFakeButtonEvent(d, 1, 0, 0); x.XFlush(d)
        else:
            for i, key in enumerate(what.split(',')):
                if i:
                    time.sleep(1.5)
                kc = x.XKeysymToKeycode(d, x.XStringToKeysym(key.encode()))
                t.XTestFakeKeyEvent(d, kc, 1, 0); x.XFlush(d); time.sleep(0.15)
                t.XTestFakeKeyEvent(d, kc, 0, 0); x.XFlush(d)
        open(log + '.inject', 'w').write('%s at %.3f\n' % (what, time.time()))
p.wait()
'''


def run_game(tree, name, secs, args, inject='', binary='./uc2.bin', xvfb=True, alsoft='null'):
    sysdir = os.path.join(work, 'run', tree, 'System')
    out = os.path.join(work, 'logs', tree)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(sysdir):
        if re.match(r'Shot\d+\.bmp$', f):
            os.remove(os.path.join(sysdir, f))
    for f in ('Avaris.ini', 'User.ini'):	# derive the inis afresh (a -nullrenderer run writes its choice there)
        if os.path.exists(os.path.join(sysdir, f)):
            os.remove(os.path.join(sysdir, f))
    log = os.path.join(out, 'm9-%s.out' % name)
    for f in (log, log + '.inject'):
        if os.path.exists(f):
            os.remove(f)
    inner = os.path.join(out, '.m9-inner.py')
    open(inner, 'w').write(INNER)
    cmd = [capped, '2G', str(secs), '--']
    if xvfb:
        cmd += ['xvfb-run', '-a', '-s', '-screen 0 1280x720x24']
    if binary == './uc2.bin':
        # M11: UC2_GLARGS="-gl4es" runs every scenario on the GL4ES/GLES2 twin of the driver
        # (a -nullrenderer on the command line still wins: the launcher takes the first switch of its table)
        args = args + os.environ.get('UC2_GLARGS', '').split()
    cmd += ['python3', inner, log, inject, binary] + args
    env = dict(os.environ, ALSOFT_DRIVERS=alsoft)
    t0 = time.time()
    subprocess.run(cmd, cwd=sysdir, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    shots = []
    for i, f in enumerate(sorted(x for x in os.listdir(sysdir) if re.match(r'Shot\d+\.bmp$', x))):
        dst = os.path.join(out, 'm9-%s-%d.png' % (name, i))
        subprocess.run(['python3', '-c', 'import sys; from PIL import Image; Image.open(sys.argv[1]).convert("RGB").save(sys.argv[2])',
                        os.path.join(sysdir, f), dst], check=True)
        shots.append(dst)
    text = open(log, 'rb').read().decode('latin-1') if os.path.exists(log) else ''
    leftovers = subprocess.run(['pgrep', '-f', os.path.join(work, 'run', tree)], stdout=subprocess.PIPE).stdout.split()
    return text, shots, time.time() - t0, leftovers


def criticals(text):
    return len(re.findall(r'Critical', text))


def gl_errors(text):
    m = re.findall(r'UC2GLDrv: frame (\d+), (\d+) GL errors', text)
    return int(m[-1][1]) if m else None


# ------------------------------------------------------------------------------------------------------------
# Pictures
# ------------------------------------------------------------------------------------------------------------

def load_rgb(png):
    from PIL import Image
    im = Image.open(png).convert('RGB')
    return im.size, im.tobytes()


def cooked_frame_rgb(ogv, frame, size, tmp):
    """The picture the cooked file shows at frame <frame>: the last decoded picture with pts <= frame
    (zero-byte packets hold the previous one), converted as the player does (BT.601 limited, chroma
    replicated), scaled to <size> as the movie quad is drawn (bilinear)."""
    crc = subprocess.run([capped, '1G', '120', '--', 'ffmpeg', '-nostdin', '-v', 'error', '-threads', '1', '-i', ogv,
                          '-map', '0:v:0', '-f', 'framecrc', '-'], stdout=subprocess.PIPE, check=True).stdout.decode()
    pts = [int(l.split(',')[2]) for l in crc.splitlines() if l and not l.startswith('#')]
    pick = max(p for p in pts if p <= frame)
    w, h = size
    raw = os.path.join(tmp, 'ref.rgb')
    subprocess.run([capped, '1G', '120', '--', 'ffmpeg', '-nostdin', '-y', '-v', 'error', '-threads', '1', '-i', ogv,
                    '-map', '0:v:0', '-vf', 'select=eq(pts\\,%d),scale=flags=neighbor+bitexact,format=yuv444p,'
                    'scale=in_color_matrix=bt601:in_range=tv:out_range=pc:flags=neighbor+accurate_rnd+bitexact,'
                    'format=rgb24,scale=%d:%d:flags=bilinear' % (pick, w, h),
                    '-frames:v', '1', '-f', 'rawvideo', raw], check=True)
    return open(raw, 'rb').read(), pick


def psnr(a, b):
    n = min(len(a), len(b))
    if n == 0:
        return 0.0
    mse = sum((x - y) * (x - y) for x, y in zip(a[:n], b[:n])) / n
    return 99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse)


def crop(rgb, size, box):
    w, h = size
    x0, y0, x1, y1 = box
    out = bytearray()
    for y in range(y0, y1):
        out += rgb[(y * w + x0) * 3:(y * w + x1) * 3]
    return bytes(out)


# ------------------------------------------------------------------------------------------------------------
# Scenarios
# ------------------------------------------------------------------------------------------------------------

def shot_lines(text):
    return [(float(a), float(b), int(c)) for a, b, c in
            re.findall(r'UC2Movie: shot of .* at movie time ([\d.]+) s \(asked ([\d.]+) s\): frame (\d+)', text)]


def check_shots(name, text, shots, ogv, tmp):
    sl = shot_lines(text)
    if len(sl) < 2 or len(shots) < 2:
        result(name + '-advance', False, '%d shot lines, %d screenshots' % (len(sl), len(shots)))
        return
    (size, a), (_, b) = load_rgb(shots[0]), load_rgb(shots[1])
    # the movie's rectangle in the viewport (640x480 movie, aspect kept)
    w, h = size
    mh = min(h, w * 480 // 640)
    mw = mh * 640 // 480
    box = ((w - mw) // 2, (h - mh) // 2, (w - mw) // 2 + mw, (h - mh) // 2 + mh)
    ca, cb = crop(a, size, box), crop(b, size, box)
    ra, pa = cooked_frame_rgb(ogv, sl[0][2], (mw, mh), tmp)
    rb, pb = cooked_frame_rgb(ogv, sl[1][2], (mw, mh), tmp)
    bands = 'the subtitle bands are hidden' in text
    if bands:
        # XboxMovieThread's HideSubtitles: rows 0-96 and 384-480 of the 640x480 picture are black
        def black(rgb):
            rgb = bytearray(rgb)
            for y in list(range(0, mh * 96 // 480)) + list(range(mh * 384 // 480, mh)):
                rgb[y * mw * 3:(y + 1) * mw * 3] = bytes(mw * 3)
            return bytes(rgb)
        ra, rb = black(ra), black(rb)
    between = psnr(ca, cb)
    own_a, other_a = psnr(ca, ra), psnr(ca, rb)
    own_b, other_b = psnr(cb, rb), psnr(cb, ra)
    result(name + '-advance', between < 30.0,
           'screenshots at movie time %.3f s (frame %d) and %.3f s (frame %d) differ: PSNR between them %.1f dB'
           % (sl[0][0], sl[0][2], sl[1][0], sl[1][2], between))
    result(name + '-match', own_a >= 30.0 and own_b >= 30.0 and own_a > other_a + 3 and own_b > other_b + 3,
           'each screenshot matches the cooked frame shown at its time (decoded picture %d / %d, %dx%d quad): '
           'PSNR %.1f / %.1f dB against its own frame, %.1f / %.1f dB against the other one%s'
           % (pa, pb, mw, mh, own_a, own_b, other_a, other_b,
              ' (subtitles off: the reference has the HideSubtitles bands black too)' if bands else ''))


def scen_logos(a, tmp):
    text, shots, dt, left = run_game(a.tree, 'logos', 90, ['-log', '-glerrors', '-movieshot=4,12'])
    m = re.search(r'UC2Movie: full-screen \.\.\\Movies\\UC2_Logos\.xmv \((\d+)x(\d+), (\d+) frames', text)
    result('logos-start', bool(m), m.group(0) if m else 'no full-screen line for UC2_Logos')
    check_shots('logos', text, shots, os.path.join(work, 'run', a.tree, 'Movies', 'UC2_Logos.ogv'), tmp)
    e = re.search(r'UC2Movie: \.\.\\Movies\\UC2_Logos\.xmv ended at movie time ([\d.]+) s, frame (\d+) of (\d+), (\d+) pictures presented in ([\d.]+) s', text)
    result('logos-end', bool(e) and int(e.group(2)) == int(e.group(3)) - 1 and float(e.group(1)) >= 19.9,
           e.group(0) if e else 'no "ended" line')
    result('logos-clean', criticals(text) == 0 and gl_errors(text) == 0 and not left,
           '%d Critical, %s GL errors, %d processes left' % (criticals(text), gl_errors(text), len(left)))


def scen_logos_skip(a, tmp):
    text, shots, dt, left = run_game(a.tree, 'logos-skip', 60, ['-log'], inject='UC2Movie: full-screen|3.0|Return')
    s = re.search(r'UC2Movie: \.\.\\Movies\\UC2_Logos\.xmv skipped by a key or button at movie time ([\d.]+) s, frame (\d+)', text)
    after = text[s.end():] if s else ''
    result('logos-skip', bool(s) and 2.0 <= float(s.group(1)) <= 8.0 and 'UC2GLDrv: frame' in after or (s and 'Startup' in text),
           (s.group(0) + '; the menu runs on' if s else 'no skip line') + ('; %d Critical' % criticals(text)))


def scen_menu(a, tmp):
    # the arena page: Down to INSTANT ACTION, Return (10 s after start-up: the menu takes input after a few
    # seconds under software GL); it opens with the CTF portrait movie. See docs/porting/analysis/video.md (V5).
    text, shots, dt, left = run_game(a.tree, 'menu-movie', 90, ['-log', '-nostartupmovies', '-glerrors'] + a.menu_args,
                                     inject=a.menu_inject)
    mats = re.findall(r'UC2Movie: MovieMaterial (\S+) frame (\d+) of (\d+)', text)
    names = sorted(set(m[0] for m in mats))
    frames = {}
    for n, f, c in mats:
        frames.setdefault(n, []).append(int(f))
    moving = [n for n, fs in frames.items() if len(set(fs)) > 1]
    result('menu-movie-log', bool(moving), 'MovieMaterials drawn: %s; advancing: %s' % (', '.join(names) or 'none',
                                                                                      ', '.join('%s %s' % (n, frames[n][:6]) for n in moving) or 'none'))
    if len(shots) >= 2:
        (size, x), (_, y) = load_rgb(shots[0]), load_rgb(shots[1])
        # Regions of the 640x480 arena page (UCMenu.UC2Arena_Main, behind the "no profile" dialog): the game
        # type portrait (a MovieMaterial) and two parts that do not move (the page title, the list).
        # The page's background animates everywhere else.
        regions = {'portrait': (412, 96, 565, 176), 'title': (495, 40, 600, 68), 'list': (60, 82, 360, 110)}
        p = {k: psnr(crop(x, size, v), crop(y, size, v)) for k, v in regions.items()}
        result('menu-movie-shots', size == (640, 480) and p['portrait'] < 20.0 and p['title'] > 30.0 and p['list'] > 30.0,
               'two screenshots 2 s apart: the portrait %s differs (PSNR between them %.1f dB), the title %s and the '
               'list %s do not (%.1f, %.1f dB)' % (regions['portrait'], p['portrait'], regions['title'], regions['list'],
                                                   p['title'], p['list']))
    else:
        result('menu-movie-shots', False, '%d screenshots' % len(shots))
    result('menu-movie-clean', criticals(text) == 0 and gl_errors(text) == 0 and not left,
           '%d Critical, %s GL errors, %d processes left' % (criticals(text), gl_errors(text), len(left)))


CINE = '04a_AscPre5'


def cine_url():
    return 'DM-Deadbolt?StartCinematic=%s' % CINE


def cine_exec(tree):
    """A map on the command line loads inside UGameEngine::Init, before the window exists (no movie can show
    there, nor did the game ever start a ladder match that way). The ladder travels from the menu: EXEC= runs
    this file after Init and its OPEN travels on the first tick, as the menu's travel does."""
    path = os.path.join(work, 'run', tree, 'System', 'm9-cinematic.txt')
    open(path, 'w').write('open %s\n' % cine_url())
    return 'EXEC=m9-cinematic.txt'


def scen_cinematic(a, tmp):
    out = os.path.join(work, 'logs', a.tree)
    wav = os.path.join(out, 'm9-cinematic.wav')
    text, shots, dt, left = run_game(a.tree, 'cinematic', 150, [cine_exec(a.tree), '-log', '-nostartupmovies', '-glerrors',
                                     '-movieshot=5,14', '-audiowave=%s' % wav], alsoft='wave')
    ls = re.search(r'UC2Movie: loading screen for DM-Deadbolt[^\n]*', text)
    result('cinematic-loading-screen', bool(ls), ls.group(0) if ls else 'no loading screen line')
    st = re.search(r'UC2Movie: opened \.\./Movies/%s\.ogv for D:\\Movies\\%s\.xmv: [^\n]*' % (CINE, CINE), text)
    result('cinematic-start', bool(st) and 'language int: international track 0' in st.group(0) and 'Vorbis 44100 Hz 2 ch' in st.group(0),
           st.group(0) if st else 'no "opened" line for the cinematic')
    check_shots('cinematic', text, shots, os.path.join(work, 'run', a.tree, 'Movies', CINE + '.ogv'), tmp)
    e = re.search(r'UC2Movie: D:\\Movies\\%s\.xmv ended at movie time ([\d.]+) s, frame (\d+) of (\d+)[^\n]*' % CINE, text)
    after = text[e.end():] if e else ''
    result('cinematic-end', bool(e) and int(e.group(2)) == int(e.group(3)) - 1 and 'UC2GLDrv: frame' in after,
           (e.group(0) if e else 'no "ended" line') + ('; the level renders afterwards (UC2GLDrv frame lines follow)' if 'UC2GLDrv: frame' in after else ''))
    # the sound: where it starts in the capture, then the envelope/gain check against the cooked track
    so = re.search(r'UC2Audio: \[\s*([\d.]+)\] movie sound stream opened', text)
    if so and os.path.exists(wav):
        start = float(so.group(1))
        cut = os.path.join(tmp, 'cine-cut.wav')
        src = os.path.join(tmp, 'cine-src.wav')
        subprocess.run([capped, '1G', '120', '--', 'ffmpeg', '-nostdin', '-y', '-v', 'error', '-ss', '%.3f' % max(0, start - 1.0),
                        '-i', wav, '-t', '30', cut], check=True)
        subprocess.run([capped, '1G', '120', '--', 'ffmpeg', '-nostdin', '-y', '-v', 'error', '-i',
                        os.path.join(work, 'run', a.tree, 'Movies', CINE + '.ogv'), '-map', '0:a:0', src], check=True)
        ctrl = os.path.join(tmp, 'ctrl-src.wav')
        subprocess.run([capped, '1G', '120', '--', 'ffmpeg', '-nostdin', '-y', '-v', 'error', '-i',
                        os.path.join(work, 'run', a.tree, 'Movies', 'UC2_Logos.ogv'), '-map', '0:a:0', ctrl], check=True)
        r = subprocess.run(['python3', os.path.join(here, 'audio-music-check.py'), cut, src, str(a.movie_gain), ctrl],
                           stdout=subprocess.PIPE).stdout.decode().strip()
        result('cinematic-sound', r.endswith('PASS'), 'stream opened at %.2f s of the capture; %s' % (start, r))
    else:
        result('cinematic-sound', False, 'no stream line or no capture')
    cl = re.search(r'UC2Audio: movie sound stream closed after ([\d.]+) s played', text)
    result('cinematic-clean', criticals(text) == 0 and gl_errors(text) == 0 and not left,
           '%d Critical, %s GL errors, %d processes left; %s' % (criticals(text), gl_errors(text), len(left),
                                                                cl.group(0) if cl else 'no stream-closed line'))


def scen_cinematic_skip(a, tmp):
    text, shots, dt, left = run_game(a.tree, 'cinematic-skip', 120, [cine_exec(a.tree), '-log', '-nostartupmovies', '-glerrors'],
                                     inject='UC2Movie: full-screen D:|3.0|Return')
    s = re.search(r'UC2Movie: D:\\Movies\\%s\.xmv skipped by a key or button at movie time ([\d.]+) s[^\n]*' % CINE, text)
    after = text[s.end():] if s else ''
    result('cinematic-skip', bool(s) and 'UC2GLDrv: frame' in after and criticals(text) == 0,
           (s.group(0) if s else 'no skip line') + ('; the level renders afterwards' if 'UC2GLDrv: frame' in after else '')
           + '; %d Critical' % criticals(text))


def no_movie_activity(text):
    return [l for l in text.splitlines() if 'UC2Movie: full-screen' in l or 'UC2Movie: loading screen' in l
            or 'movie sound stream' in l or 'UC2Movie: opened' in l]


def scen_nullrenderer(a, tmp):
    text, shots, dt, left = run_game(a.tree, 'nullrenderer', 60, [cine_exec(a.tree), '-log', '-nullrenderer'])
    act = no_movie_activity(text)
    lv = 'Bringing Level DM-Deadbolt' in text
    result('nullrenderer', not act and lv and 'NullRenderDevice' in text,
           'render device null: %s; level up: %s; movie lines: %d %s' % ('NullRenderDevice' in text, lv, len(act), act[:2]))


def scen_server(a, tmp):
    text, shots, dt, left = run_game(a.tree, 'server', 25, ['server', cine_url(), '-log'], binary='./ucc-bin', xvfb=False)
    act = no_movie_activity(text)
    lv = 'Bringing Level DM-Deadbolt' in text
    result('server', not act and lv, 'level up: %s; movie lines: %d %s' % (lv, len(act), act[:2]))


SCENARIOS = [('logos', scen_logos), ('logos-skip', scen_logos_skip), ('menu-movie', scen_menu),
             ('cinematic', scen_cinematic), ('cinematic-skip', scen_cinematic_skip),
             ('nullrenderer', scen_nullrenderer), ('server', scen_server)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', default=os.path.join(repo, 'build-native'))
    ap.add_argument('--tree', default='m9')
    ap.add_argument('--only', default='')
    ap.add_argument('--no-mkrun', action='store_true')
    # the menu page with a MovieMaterial and how to reach it (see the notebook, V5)
    ap.add_argument('--menu-inject', default='Startup time|10.0|Down,Return')
    ap.add_argument('--menu-args', default='-glshot=26,28')
    # expected level of the movie's sound in the capture: 0 dB at the source (no category, no headroom: the
    # Xbox played XMV sound outside XACT) plus OpenAL Soft's gain for a centred 2D stereo source (measured, see
    # the notebook)
    ap.add_argument('--movie-gain', type=float, default=0.0)
    a = ap.parse_args()
    a.menu_args = a.menu_args.split()
    if not a.no_mkrun:
        subprocess.run([os.path.join(here, 'mkrun-uc2.sh'), a.build, a.tree], check=True, stdout=subprocess.DEVNULL)
    tmp = os.path.join(work, 'logs', a.tree, '.m9-tmp')
    os.makedirs(tmp, exist_ok=True)
    only = set(x for x in a.only.split(',') if x)
    for name, fn in SCENARIOS:
        if only and name not in only:
            continue
        try:
            fn(a, tmp)
        except Exception as e:
            result(name, False, 'scenario error: %r' % (e,))
    shutil.rmtree(tmp, True)
    summary = os.path.join(work, 'logs', a.tree, 'm9-summary.txt')
    open(summary, 'w').write('\n'.join(RESULTS) + '\n')
    fails = sum(1 for r in RESULTS if r.startswith('FAIL'))
    print('m9-movies: %d checks, %d failed (%s)' % (len(RESULTS), fails, summary))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
