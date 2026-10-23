#!/usr/bin/env python3
"""cook.py: cook the game folder the engine runs from, out of the game's Xbox disc or Epic's source tree.

  cook.py --in <UC2.iso | disc folder | uc2-avaris-2226> --out <dir> [--xbox-audio] [--skip audio,movies]
  cook.py --check [--in <...>]       a dry run of the tools (and the input's checks), nothing written

Inputs (recognised by themselves; --source xbox|epic when one is not)
  a disc image   the game's Xbox disc as an .iso: an xiso or a full dump (read by xiso.py)
  a disc folder  the disc's files: System/ (the 17 shipped .u, the final inis and localisation), Sounds/ (the XACT
                 banks and Avaris.xap), Movies/ and Textures/Movies/, and the Xbox-format packages (XboxMaps/*.LIN
                 linear loads, XboxTextures/, XboxStaticMeshes/, XboxAnimations/ with the texture and vertex data in
                 .xpr bundles), which cook-xbox-packages.py converts to the PC format, in the tables of the PC game
                 folder's packages (uc2-pc-tables.json.gz): a server on them and a client on the PC folder play
                 together. --pc-packages <dir> takes PC-format packages from a folder instead.
  Epic's tree    uc2-avaris-2226: the PC-format packages (Maps/, Textures/, StaticMeshes/, Animations/), System/, the
                 XACT project with its WAVs (Sounds/Avaris.xap, SoundData/, Music/) and the XMV movies.

Standalone: Python 3.8 or newer, the scripts beside this one, and ffmpeg with ffprobe for the audio and the movies.
Nothing is kept between runs: a disc image is extracted into <out>/.cook-work, where the steps also work, and that
folder is removed at the end; every sound and movie is encoded again.

Steps
  1. tools: Python and the modules the cook uses, the scripts beside this one, the PC tables, ffmpeg and ffprobe:
     each encoder tried on a generated sound or picture, each decoder and demuxer looked up (and, with --build, the
     libraries the game loads). What is missing is listed with how to install it and what the cook does without it.
  2. input: recognised and its layout checked; a disc image extracted; ffmpeg tried on the input's smallest movie.
  3. System/ and the packages: copied (a reflink where the file system can), each package checked to be PC format;
     from a disc, converted by cook-xbox-packages.py (report: Maps/xbox-packages.txt).
  4. audio (cook-audio.py): Sounds/*.uc2wb, *.ogp, *.uc2cues, Music/*.ogg (Ogg Vorbis), from the project's WAVs or
     decoded from the disc's banks. --xbox-audio copies the disc's .xsb/.xwb instead, which the engine plays itself.
  5. movies (cook-movies.py): Movies/*.ogv, *.oga and Textures/Movies/*.ogv (Ogg Theora/Vorbis).
  6. --build <build-dir> (from the repository): the build's uc2.bin, ucc-bin and libopenal.so.1 (or the Windows .exe
     and OpenAL32.dll), its patched script packages and the repository's inis (Engine/Config) go into System/.
  7. COOK.txt (what was cooked from what) and MANIFEST.sha256.

The output folder is created, or must be one cook.py made (it keeps the list of its files in .cook-files): a re-cook
replaces its files and removes the ones it no longer writes, and leaves everything else (the engine's Avaris.ini,
User.ini, logs, Save/) alone. Each step's full log is in <out>/.cook-logs/.

  --skip audio,movies   leave those steps out (an earlier cook's files stay)
  --jobs N              parallel workers (default: half the CPUs)
"""

import argparse
import datetime
import hashlib
import importlib
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile

try:
    import fcntl        # reflink copies (Linux)
except ImportError:
    fcntl = None

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

PACKAGE_TAG = 0x9E2A83C1
SCRIPT_PACKAGES = ('Core', 'Engine', 'UCAnnouncements', 'Editor', 'UnrealEd', 'IpDrv', 'UWeb', 'GamePlay', 'UnrealGame',
                   'UCElements', 'UCGameplay', 'UCEffects', 'UCCharacters', 'UCInventory', 'UCMutators', 'GUI', 'UCMenu')
# The folders of PC-format packages: (folder, extension).
PACKAGE_DIRS = (('Maps', '.unr'), ('Maps/ComboMatinees', '.unr'), ('Textures', '.utx'), ('StaticMeshes', '.usx'),
                ('Animations', '.ukx'))
# What the engine needs from the disc (an image is extracted only that far).
DISC_DIRS = ('System', 'Sounds', 'Movies', 'Textures', 'XboxMaps', 'XboxTextures', 'XboxStaticMeshes',
             'XboxAnimations')
# The input's top folders the cook reads, Epic's tree's and the disc's.
READ_DIRS = {d.lower() for d in DISC_DIRS + ('Maps', 'StaticMeshes', 'Animations', 'SoundData', 'Music')}
FICLONE = 0x40049409
TABLES = 'uc2-pc-tables.json.gz'
# The scripts the cook runs and imports, beside this one.
SCRIPTS = ('cook-xbox-packages.py', 'cook-audio.py', 'cook-movies.py', 'xiso.py', 'uc2xpkg.py', 'uc2xpr.py',
           'uc2xbsp.py', 'uc2xtex.py', 'uc2xmesh.py', 'uc2xskel.py', 'uc2xterrain.py', 'uc2xtables.py', 'uc2xrefs.py',
           'uc2types.py', 'scripts/ucpkg.py', 'uc2xwb.py', 'uc2xsb.py')
# Standard modules the cook's scripts import that a Python build can lack.
MODULES = ('zlib', 'gzip', 'json', 'hashlib', 'concurrent.futures', 'multiprocessing', 'difflib', 'array')

# ffmpeg features per step: (kind, name, why).
FFMPEG_NEEDS = {
    'audio': [('encoder', 'libvorbis', 'Ogg Vorbis sound and music'), ('demuxer', 'wav', 'the WAV sources')],
    'audio-xbox': [('decoder', 'wmav2', 'the WMA music track of the disc'), ('demuxer', 'asf', 'the WMA music track')],
    'movies': [('demuxer', 'xmv', 'the Xbox movies'), ('decoder', 'wmv2', 'XMV video'),
               ('decoder', 'adpcm_ima_xbox', 'XMV audio'), ('encoder', 'libtheora', 'Ogg Theora video'),
               ('encoder', 'libvorbis', 'Ogg Vorbis movie audio')],
}
# The encoders tried on a generated input: (encoder, lavfi source, ffmpeg options, what ffprobe must read back).
FFMPEG_TRIES = {
    'libvorbis': ('anullsrc=r=44100:cl=stereo', ['-t', '0.2', '-c:a', 'libvorbis', '-q:a', '4'], 'vorbis'),
    'libtheora': ('testsrc=size=64x64:rate=10', ['-t', '0.3', '-c:v', 'libtheora', '-q:v', '7'], 'theora'),
}
# Libraries uc2.bin loads, and the packages that provide them: (soname, debian/ubuntu, fedora, arch).
GAME_LIBS = (
    ('libX11.so.6', 'libx11-6', 'libX11', 'libx11'),
    ('libXext.so.6', 'libxext6', 'libXext', 'libxext'),
    ('libXcursor.so.1', 'libxcursor1', 'libXcursor', 'libxcursor'),
    ('libXi.so.6', 'libxi6', 'libXi', 'libxi'),
    ('libXfixes.so.3', 'libxfixes3', 'libXfixes', 'libxfixes'),
    ('libXrandr.so.2', 'libxrandr2', 'libXrandr', 'libxrandr'),
    ('libXss.so.1', 'libxss1', 'libXScrnSaver', 'libxss'),
    ('libGL.so.1', 'libgl1', 'mesa-libGL', 'libglvnd'),
)
# Space the cooked folder takes, in bytes (the extracted disc image comes on top while the cook runs).
OUT_SIZE = 2200 << 20


class CookError(Exception):
    pass


def say(msg=''):
    print(msg, flush=True)


# ------------------------------------------------------------------------------------------------------------
# Tools
# ------------------------------------------------------------------------------------------------------------

def distro():
    """'debian', 'fedora', 'arch', 'suse', 'macos', 'windows' or ''."""
    if sys.platform == 'darwin':
        return 'macos'
    if sys.platform.startswith(('win', 'cygwin', 'msys')):
        return 'windows'
    try:
        text = open('/etc/os-release').read().lower()
    except OSError:
        return ''
    ids = ' '.join(l.split('=', 1)[1].strip('"') for l in text.splitlines() if l.startswith(('id=', 'id_like=')))
    for name, keys in (('debian', ('debian', 'ubuntu')), ('fedora', ('fedora', 'rhel')), ('arch', ('arch',)),
                       ('suse', ('suse',))):
        if any(k in ids.split() for k in keys):
            return name
    return ''


def install_hint(debian=(), fedora=(), arch=(), suse=(), macos=(), windows=''):
    d = distro()
    if d == 'windows':
        return windows or 'see the tool\'s website'
    cmd = {'debian': ('sudo apt install', debian), 'fedora': ('sudo dnf install', fedora),
           'arch': ('sudo pacman -S', arch), 'suse': ('sudo zypper install', suse),
           'macos': ('brew install', macos)}.get(d)
    if cmd and cmd[1]:
        return '%s %s' % (cmd[0], ' '.join(cmd[1]))
    return 'Debian/Ubuntu: sudo apt install %s; Fedora: sudo dnf install %s; Arch: sudo pacman -S %s' % (
        ' '.join(debian), ' '.join(fedora), ' '.join(arch))


def ffmpeg_hint():
    return install_hint(['ffmpeg'], ['ffmpeg (RPM Fusion)'], ['ffmpeg'], ['ffmpeg-7 (Packman)'], ['ffmpeg'],
                        'winget install Gyan.FFmpeg, or an ffmpeg build from https://ffmpeg.org/download.html '
                        'with ffmpeg.exe and ffprobe.exe on PATH')


def ffmpeg_features(kind):
    flag = {'encoder': '-encoders', 'decoder': '-decoders', 'demuxer': '-demuxers'}[kind]
    r = subprocess.run(['ffmpeg', '-hide_banner', flag], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    names = set()
    for line in r.stdout.decode('latin-1').splitlines():
        parts = line.split()
        # " V....D libtheora  ..." (codecs) or " D  xmv  ..." / " DE asf ..." (formats, comma lists allowed)
        if len(parts) >= 2 and parts[0] != '--' and not parts[0].endswith('='):
            for n in parts[1].split(','):
                names.add(n)
    return names


def ffmpeg_try(encoder, tmp):
    """Encodes a generated input with encoder and reads it back with ffprobe: None, or what went wrong."""
    source, opts, codec = FFMPEG_TRIES[encoder]
    out = os.path.join(tmp, encoder + '.ogg')
    r = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i', source] + opts +
                       ['-f', 'ogg', out], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode or not os.path.isfile(out) or not os.path.getsize(out):
        err = (r.stdout.decode('latin-1').strip().splitlines() or ['?'])[-1]
        if 'lavfi' in err:
            return None         # an ffmpeg without the generated sources: its encoder list has to do
        return 'ffmpeg could not encode with it: %s' % err
    r = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=codec_name', '-of', 'csv=p=0', out],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    got = r.stdout.decode('latin-1').split()
    if r.returncode or codec not in got:
        return 'ffprobe read %s back, not %s' % (' '.join(got) or 'nothing', codec)
    return None


def ffmpeg_steps(args):
    steps = []
    if 'audio' not in args.skip and not args.xbox_audio:
        steps.append('audio')
        if args.source != 'epic':
            steps.append('audio-xbox')
    if 'movies' not in args.skip:
        steps.append('movies')
    return steps


def check_tools(args):
    """A dry run of what the cook runs. Returns the problems: (what is missing, what for, how to get it)."""
    problems = []
    if sys.version_info < (3, 8):
        problems.append(('Python 3.8 or newer (this is %s)' % platform.python_version(), 'every step',
                         install_hint(['python3'], ['python3'], ['python'], ['python3'], ['python'],
                                      'https://www.python.org/downloads/')))
    for m in MODULES:
        try:
            importlib.import_module(m)
        except ImportError:
            problems.append(('the Python module %s' % m, 'the package conversion',
                             'a Python build with it (the python.org builds and the distributions\' have it)'))
    lacking = [s for s in SCRIPTS if not os.path.isfile(os.path.join(HERE, *s.split('/')))]
    if lacking:
        problems.append(('%s beside cook.py' % ', '.join(lacking), 'the cook',
                         'the whole tool: unpack every file of its archive into one folder'))
    for s in SCRIPTS:
        p = os.path.join(HERE, *s.split('/'))
        if s not in lacking:
            try:
                with open(p, 'rb') as f:
                    compile(f.read(), p, 'exec')
            except (SyntaxError, ValueError) as ex:
                problems.append(('a readable %s (%s)' % (s, ex), 'the cook', 'the tool\'s archive unpacked again'))
    if args.source != 'epic' and not args.pc_packages and not args.disc_tables:
        tables = os.path.join(HERE, TABLES)
        try:
            import uc2xtables
            if not uc2xtables.load(tables):
                raise ValueError('no packages')
        except (OSError, ValueError, EOFError) as ex:
            why = ex.strerror if isinstance(ex, OSError) and ex.strerror else str(ex)
            problems.append(('%s beside cook.py (%s)' % (TABLES, why), 'the disc\'s packages, written in the PC '
                             'packages\' tables so that they play online with the PC game folder',
                             'the tool\'s archive, which has it (in the repository: tools/dev/make-pc-tables.py)'))
    steps = ffmpeg_steps(args)
    if steps:
        missing = [t for t in ('ffmpeg', 'ffprobe') if not shutil.which(t)]
        for t in missing:
            problems.append((t, 'the audio and movie steps (the packages need no ffmpeg: --skip audio,movies '
                             'leaves those steps out, --xbox-audio copies the disc\'s sound banks instead)',
                             ffmpeg_hint()))
        if not missing:
            have = {k: ffmpeg_features(k) for k in ('encoder', 'decoder', 'demuxer')}
            tried = {}
            with tempfile.TemporaryDirectory(prefix='uc2-cook-check-') as tmp:
                for step in steps:
                    for kind, name, why in FFMPEG_NEEDS[step]:
                        if kind == 'encoder' and name in FFMPEG_TRIES and name in have[kind]:
                            if name not in tried:
                                tried[name] = ffmpeg_try(name, tmp)
                            err = tried[name]
                        else:
                            err = None if name in have[kind] else 'not in this ffmpeg'
                        if err:
                            problems.append(('ffmpeg %s %s: %s' % (kind, name, err), why,
                                             'an ffmpeg built with it: ' + ffmpeg_hint()))
    if args.build:
        game = os.path.join(args.build, 'Source', 'SDLLaunch', 'uc2.bin')
        if os.path.isfile(game) and shutil.which('ldd'):
            r = subprocess.run(['ldd', game], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            out = r.stdout.decode('latin-1')
            libs = [l for l in GAME_LIBS if '%s => not found' % l[0] in out]
            import ctypes.util
            if not ctypes.util.find_library('GL'):
                libs += [l for l in GAME_LIBS if l[0] == 'libGL.so.1']
            for soname, deb, fed, arc in libs:
                problems.append((soname, 'the game (uc2.bin)', install_hint([deb], [fed], [arc])))
    return problems


# ------------------------------------------------------------------------------------------------------------
# Input
# ------------------------------------------------------------------------------------------------------------

def find_ci(root, rel):
    """The path of rel below root, matched case-insensitively (disc folders are upper case on some dumps)."""
    cur = root
    for part in rel.split('/'):
        if os.path.exists(os.path.join(cur, part)):
            cur = os.path.join(cur, part)
            continue
        try:
            match = [e for e in os.listdir(cur) if e.lower() == part.lower()]
        except OSError:
            return None
        if not match:
            return None
        cur = os.path.join(cur, match[0])
    return cur


def files_in(root, rel, ext):
    d = find_ci(root, rel)
    if not d or not os.path.isdir(d):
        return []
    return sorted(os.path.join(d, f) for f in os.listdir(d)
                  if f.lower().endswith(ext) and os.path.isfile(os.path.join(d, f)))


class Listing:
    """The input's files, '/'-separated paths below its root with their sizes: of a folder or of a disc image."""

    def __init__(self, files):
        self.files = {p.lower(): (p, size) for p, size in files}
        self.dirs = set()
        for low in self.files:
            parts = low.split('/')
            for k in range(1, len(parts)):
                self.dirs.add('/'.join(parts[:k]))

    @classmethod
    def of_folder(cls, root):
        """The files of the folders the cook reads (Epic's tree has its sources beside them)."""
        out = []
        for top in sorted(os.listdir(root)):
            p = os.path.join(root, top)
            if os.path.isfile(p):
                out.append((top, os.path.getsize(p)))
            elif top.lower() in READ_DIRS:
                for dirpath, _, files in os.walk(p):
                    for fn in files:
                        f = os.path.join(dirpath, fn)
                        out.append((os.path.relpath(f, root).replace(os.sep, '/'), os.path.getsize(f)))
        return cls(out)

    def size(self, rel_dirs):
        """The bytes of the files below these folders."""
        pre = tuple(d.lower() + '/' for d in rel_dirs)
        return sum(size for low, (_, size) in self.files.items() if low.startswith(pre))

    def has(self, rel):
        return rel.lower() in self.files or rel.lower() in self.dirs

    def find(self, rel_dir, ext):
        """(path, size) of the files of rel_dir (not below it) ending in ext."""
        pre = rel_dir.lower().rstrip('/') + '/'
        return sorted(v for low, v in self.files.items()
                      if low.startswith(pre) and '/' not in low[len(pre):] and low.endswith(ext))


def looks_like(listing):
    """'epic', 'xbox' or None, from the folders only Epic's tree or only a disc has."""
    if listing.has('SoundData') or listing.find('Maps', '.unr'):
        return 'epic'
    if listing.find('XboxMaps', '.lin') or listing.has('default.xbe') or not listing.has('Maps'):
        return 'xbox' if listing.has('System') else None
    return None


def open_input(args):
    """The input's listing, and the image when it is a disc image."""
    path = os.path.abspath(args.input)
    if os.path.isdir(path):
        return Listing.of_folder(path), None
    if not os.path.isfile(path):
        raise CookError('--in %s: no such folder or file' % args.input)
    import xiso
    try:
        img = xiso.Image(path)
        return Listing((p, size) for p, size, _ in img.walk()), img
    except (xiso.XisoError, OSError) as ex:
        raise CookError('--in %s: not a disc image the cook reads (%s)' % (args.input, ex))


def extract(args, img, work):
    """A disc image's files the cook reads, into work/disc."""
    import xiso
    dest = os.path.join(work, 'disc')
    say('  extracting %s of the image into %s' % (', '.join(DISC_DIRS), dest))
    try:
        n, total = img.extract(dest, DISC_DIRS)
    except xiso.XisoError as ex:
        raise CookError(str(ex))
    if n == 0:
        raise CookError('%s: the image has none of %s' % (args.input, ', '.join(DISC_DIRS)))
    say('  %d files, %.1f MB' % (n, total / 1e6))
    return dest


def check_input(args, listing):
    """Returns the list of problems with the input's layout for args.source."""
    problems = []
    have = {os.path.splitext(os.path.basename(p))[0].lower() for p, _ in listing.find('System', '.u')}
    lacking = [p for p in SCRIPT_PACKAGES if p.lower() not in have]
    if lacking:
        problems.append('System/ lacks the script packages %s' % ', '.join(lacking))
    if 'audio' not in args.skip:
        if args.xbox_audio:
            if not listing.find('Sounds', '.xsb'):
                problems.append('Sounds/ has no .xsb/.xwb banks (--xbox-audio)')
        elif not listing.has('Sounds/Avaris.xap'):
            problems.append('no Sounds/Avaris.xap (the XACT project the audio cook reads; --xbox-audio copies the '
                            'banks instead)')
        elif args.source == 'epic' and not listing.has('SoundData'):
            say('  note: no SoundData/: every wave is decoded from the shipped .xwb')
    if 'movies' not in args.skip and not listing.find('Movies', '.xmv'):
        problems.append('no Movies/*.xmv')
    if args.source == 'xbox' and not args.pc_packages:
        for rel, ext in (('XboxMaps', '.lin'), ('XboxTextures', '.xpr'), ('XboxTextures', '.utx'),
                         ('XboxStaticMeshes', '.usx'), ('XboxAnimations', '.ukx')):
            if not listing.find(rel, ext):
                problems.append('no %s/*%s (the Xbox-format packages to convert; --pc-packages takes PC ones '
                                'instead)' % (rel, ext))
        if not listing.has('System/Script.lin'):
            problems.append('no System/Script.lin (the entry level)')
    elif args.source == 'epic':
        for rel, ext in PACKAGE_DIRS[:1] + PACKAGE_DIRS[2:]:
            if not listing.find(rel, ext):
                problems.append('no %s/*%s' % (rel, ext))
    elif args.pc_packages:
        for rel, ext in PACKAGE_DIRS[:1] + PACKAGE_DIRS[2:]:
            if not files_in(args.pc_packages, rel, ext):
                problems.append('%s: no %s/*%s' % (args.pc_packages, rel, ext))
    return problems


def try_movie(args, listing, img):
    """ffmpeg on the first second of the input's smallest movie: None, or what went wrong."""
    if 'movies' not in args.skip and shutil.which('ffmpeg'):
        movies = listing.find('Movies', '.xmv')
        if movies:
            rel, size = min(movies, key=lambda m: m[1])
            with tempfile.TemporaryDirectory(prefix='uc2-cook-check-') as tmp:
                if img is not None:
                    img.extract(tmp, [rel])
                    path = find_ci(tmp, rel)
                else:
                    path = find_ci(os.path.abspath(args.input), rel)
                r = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', path, '-t', '1', '-f',
                                    'null', '-'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if r.returncode:
                    return '%s: %s' % (rel, (r.stdout.decode('latin-1').strip().splitlines() or ['?'])[-1])
    return None


def check_space(args, listing, img):
    """The free space where the output goes, against what the steps write there."""
    need = OUT_SIZE + (listing.size(DISC_DIRS) if img is not None else 0)
    d = os.path.abspath(args.out)
    while not os.path.isdir(d):
        d = os.path.dirname(d)
    free = shutil.disk_usage(d).free
    if free < need:
        return '%s has %.1f GB free; the cook writes about %.1f GB there%s' % (
            d, free / 1e9, need / 1e9, ' (the extracted disc image is removed at the end)' if img else '')
    return None


def package_licensee(path):
    with open(path, 'rb') as f:
        head = f.read(8)
    if len(head) < 8:
        return None
    tag, ver, lic = struct.unpack('<IHH', head)
    return lic if tag == PACKAGE_TAG else None


# ------------------------------------------------------------------------------------------------------------
# Output
# ------------------------------------------------------------------------------------------------------------

class Output:
    """The output folder and the files the cook writes in it."""

    def __init__(self, path):
        self.path = path
        self.list_path = os.path.join(path, '.cook-files')
        self.work = os.path.join(path, '.cook-work')
        self.logs = os.path.join(path, '.cook-logs')
        self.old = set()
        self.new = set()

    def open(self):
        if os.path.isdir(self.path) and os.listdir(self.path) and not os.path.isfile(self.list_path):
            raise CookError('%s exists and was not made by cook.py (no .cook-files): give an empty or new folder'
                            % self.path)
        os.makedirs(self.path, exist_ok=True)
        if os.path.isfile(self.list_path):
            self.old = {l.rstrip('\n') for l in open(self.list_path) if l.strip()}
        shutil.rmtree(self.work, ignore_errors=True)
        shutil.rmtree(self.logs, ignore_errors=True)
        os.makedirs(self.work)
        os.makedirs(self.logs)

    def close(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def abs(self, rel):
        return os.path.join(self.path, *rel.split('/'))

    def log(self, step):
        return os.path.join(self.logs, step + '.log')

    def copy(self, src, rel):
        """rel becomes a copy of src (a reflink where the file system can) unless it already is one (size, mtime)."""
        dst = self.abs(rel)
        self.new.add(rel)
        st = os.stat(src)
        try:
            dt = os.stat(dst)
            if dt.st_size == st.st_size and int(dt.st_mtime) == int(st.st_mtime):
                return False
        except FileNotFoundError:
            pass
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        tmp = dst + '.part'
        with open(src, 'rb') as f, open(tmp, 'wb') as g:
            try:
                if fcntl is None:
                    raise OSError
                fcntl.ioctl(g.fileno(), FICLONE, f.fileno())
            except OSError:
                shutil.copyfileobj(f, g, 1 << 22)
        os.utime(tmp, (st.st_atime, st.st_mtime))
        os.chmod(tmp, 0o755 if st.st_mode & 0o111 else 0o644)
        os.replace(tmp, dst)
        return True

    def move(self, src, rel):
        """rel becomes src, moved (src is in the work folder, on the same file system)."""
        dst = self.abs(rel)
        self.new.add(rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        os.replace(src, dst)

    def adopt(self, rel_dir):
        """Counts every file below rel_dir (written by a step's own tool) as the cook's."""
        top = self.abs(rel_dir)
        for dirpath, _, files in os.walk(top):
            for fn in files:
                self.new.add(os.path.relpath(os.path.join(dirpath, fn), self.path).replace(os.sep, '/'))

    def keep(self, prefixes):
        """Keeps the files of the previous cook below these folders (a skipped step)."""
        self.new |= {rel for rel in self.old if rel.startswith(prefixes) and os.path.isfile(self.abs(rel))}

    def drop(self, prefixes):
        """Removes the files of the previous cook below these folders (a step that rewrites them all)."""
        for rel in sorted(self.old):
            if rel.startswith(prefixes) and os.path.isfile(self.abs(rel)):
                os.remove(self.abs(rel))

    def finish(self):
        for rel in sorted(self.old - self.new):
            p = self.abs(rel)
            if os.path.isfile(p):
                os.remove(p)
        with open(self.list_path + '.part', 'w', newline='\n') as f:
            f.write(''.join(r + '\n' for r in sorted(self.new | {'MANIFEST.sha256', 'COOK.txt'})))
        os.replace(self.list_path + '.part', self.list_path)


def in_repository(*rel):
    """A file of the repository this tool sits in, or None (the tool on its own)."""
    p = os.path.join(REPO, *rel)
    return p if os.path.exists(p) and os.path.isdir(os.path.join(REPO, 'Engine', 'Config')) else None


def copy_packages(args, src, out, log):
    """System/ from the input; the PC-format packages from the input (epic), --pc-packages (xbox) or converted
    from the disc (xbox)."""
    n = 0
    sysdir = find_ci(src, 'System')
    for f in sorted(os.listdir(sysdir)):
        p = os.path.join(sysdir, f)
        if os.path.isfile(p) and (f.lower().endswith(('.u', '.ini')) or f.lower() == 'packages.md5'):
            n += out.copy(p, 'System/' + f)
    for sub in ('Ladders', 'Localization'):
        d = find_ci(sysdir, sub)
        for dirpath, _, files in os.walk(d) if d else ():
            for fn in sorted(files):
                rel = os.path.relpath(os.path.join(dirpath, fn), sysdir).replace(os.sep, '/')
                n += out.copy(os.path.join(dirpath, fn), 'System/' + rel)
    # The script build reads Epic's Xbox inis from the game folder; a disc has none, the repository has Epic's.
    for ini in ('DefaultXbox.ini', 'DefUserXbox.ini'):
        if not find_ci(sysdir, ini):
            p = in_repository('Engine', 'Config', ini)
            if p:
                n += out.copy(p, 'System/' + ini)
                log.append('System/%s: from Engine/Config (the input has none)' % ini)
            else:
                log.append('System/%s: not on the input (only the script build reads it)' % ini)
    shipped = in_repository('patches', 'shipped.sha256')
    for line in open(shipped) if shipped else ():
        if line.strip() and not line.startswith('#'):
            h, name = line.split()
            p = out.abs('System/' + name)
            if os.path.isfile(p) and sha256(p) != h:
                log.append('System/%s: not the shipped package (sha256 differs from patches/shipped.sha256)' % name)
    say('  System/: %d files written' % n)

    if args.source == 'xbox' and not args.pc_packages:
        convert_packages(args, src, out, log)
        return
    root = src if args.source == 'epic' else os.path.abspath(args.pc_packages)
    xbox = []
    for rel, ext in PACKAGE_DIRS:
        files = files_in(root, rel, ext)
        written = 0
        for p in files:
            lic = package_licensee(p)
            if lic != 0:
                xbox.append('%s (%s)' % (p, 'not a package' if lic is None else 'licensee %d: Xbox format' % lic))
                continue
            written += out.copy(p, '%s/%s' % (rel, os.path.basename(p)))
        say('  %s/: %d packages, %d written' % (rel, len(files), written))
        log.append('%s/: %d packages from %s' % (rel, len(files), find_ci(root, rel)))
    if xbox:
        raise CookError('packages the engine cannot load:\n  ' + '\n  '.join(xbox))


def convert_packages(args, src, out, log):
    """The disc's Xbox-format maps, textures, static meshes and animations as PC packages
    (cook-xbox-packages.py), written straight into the output folder."""
    cmd = [sys.executable, os.path.join(HERE, 'cook-xbox-packages.py'), '--in', src, '--out', out.path,
           '--jobs', str(args.jobs)] + (['--disc-tables'] if args.disc_tables else [])
    run_step(cmd, out.log('packages'), lambda l: l.startswith('FAILED'))
    bad = []
    for rel, ext in PACKAGE_DIRS:
        files = files_in(out.path, rel, ext)
        for p in files:
            if package_licensee(p) != 0:
                bad.append(p)
            out.new.add('%s/%s' % (rel, os.path.basename(p)))
        if files:
            say('  %s/: %d packages converted' % (rel, len(files)))
    out.new.add('Maps/xbox-packages.txt')
    if bad:
        raise CookError('converted packages not in PC format:\n  ' + '\n  '.join(bad))
    log.append('Maps/, Textures/, StaticMeshes/, Animations/: converted from the disc by cook-xbox-packages.py, '
               '%s (report: Maps/xbox-packages.txt)' % (
                   "with the disc's tables (--disc-tables)" if args.disc_tables else 'in the PC packages\' tables'))


def cook_audio(args, src, out, log):
    sounds = find_ci(src, 'Sounds')
    out.drop(('Sounds/', 'Music/'))
    if args.xbox_audio:
        n = 0
        for f in sorted(os.listdir(sounds)):
            if f.lower().endswith(('.xsb', '.xwb')):
                n += 1
                out.copy(os.path.join(sounds, f), 'Sounds/' + f)
        say('  Sounds/: %d Xbox banks (.xsb/.xwb) as shipped; the engine plays them, no Music/' % n)
        log.append('audio: the shipped Xbox banks, %d files from %s (--xbox-audio)' % (n, sounds))
        return
    cmd = [sys.executable, os.path.join(HERE, 'cook-audio.py'), '--src', src, '--out', out.path,
           '--cache', os.path.join(out.work, 'audio'), '--jobs', str(args.jobs)]
    run_step(cmd, out.log('audio'), lambda l: l[:1].islower() or l.startswith('  '))
    out.adopt('Sounds')
    out.adopt('Music')
    log.append('audio: cook-audio.py from %s (report: Sounds/cook-report.txt)' % sounds)


def cook_movies(args, src, out, log):
    cooked = os.path.join(out.work, 'movies')
    cmd = [sys.executable, os.path.join(HERE, 'cook-movies.py'), '--src', src, '--out', cooked,
           '--jobs', str(min(4, args.jobs))]
    run_step(cmd, out.log('movies'), lambda l: 'unchanged' not in l)
    import json
    manifest = json.load(open(os.path.join(cooked, 'Movies', 'movie-manifest.json')))
    out.drop(('Movies/', 'Textures/Movies/'))
    n = 0
    for m in manifest['movies']:
        for o in m['outputs']:
            rel = o['file'].replace(os.sep, '/')
            out.move(os.path.join(cooked, rel), rel)
            n += 1
    for f in ('movie-manifest.json', 'cook-report.txt'):
        out.move(os.path.join(cooked, 'Movies', f), 'Movies/' + f)
    say('  %d movies -> %d files' % (len(manifest['movies']), n))
    log.append('movies: cook-movies.py from %s, %d movies, %d files' % (src, len(manifest['movies']), n))


def run_step(cmd, logfile, show):
    """Runs a step's tool, its whole output into logfile and the lines show() picks onto the console."""
    say('  %s (log: %s)' % (' '.join(os.path.relpath(c, HERE) if c.startswith(HERE) else c for c in cmd[1:3]),
                            logfile))
    with open(logfile, 'w') as lf:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=HERE)
        for raw in p.stdout:
            line = raw.decode('utf-8', 'replace')
            lf.write(line)
            if show(line):
                say('    ' + line.rstrip())
        rc = p.wait()
    if rc:
        raise CookError('%s failed (exit %d): %s' % (os.path.basename(cmd[1]), rc, logfile))


def install_build(args, out, log):
    """The build's binaries, its patched script packages and the repository's inis into System/."""
    if not in_repository('Engine', 'Config'):
        raise CookError('--build takes the repository\'s inis (Engine/Config): run the cook from the repository')
    b = os.path.abspath(args.build)
    bins = []
    for rel in ('Source/SDLLaunch/uc2.bin', 'Source/UCC/ucc-bin', 'Source/SDLLaunch/uc2.exe', 'Source/UCC/ucc.exe',
                '_deps/openalsoft-build/OpenAL32.dll'):
        p = os.path.join(b, rel)
        if os.path.isfile(p):
            bins.append((p, os.path.basename(p)))
    lib = os.path.join(b, '_deps', 'openalsoft-build', 'libopenal.so.1')
    if os.path.exists(lib):
        bins.append((os.path.realpath(lib), 'libopenal.so.1'))
    if not any(n in ('uc2.bin', 'uc2.exe') for _, n in bins):
        raise CookError('--build %s: no Source/SDLLaunch/uc2.bin or uc2.exe (build it first)' % args.build)
    for p, name in bins:
        out.copy(p, 'System/' + name)
    for ini in sorted(os.listdir(os.path.join(REPO, 'Engine', 'Config'))):
        if ini.endswith('.ini'):
            out.copy(os.path.join(REPO, 'Engine', 'Config', ini), 'System/' + ini)
    scripts = []
    sdir = os.path.join(b, 'Scripts', 'System')
    for f in sorted(os.listdir(sdir)) if os.path.isdir(sdir) else []:
        if f.endswith('.u') and os.path.isfile(out.abs('System/' + f)):
            out.copy(os.path.join(sdir, f), 'System/' + f)
            scripts.append(f)
    for d in ('Save', 'Cache'):
        os.makedirs(out.abs(d), exist_ok=True)
    say('  System/: %s; the repository\'s inis; script packages from the build: %s' % (
        ', '.join(n for _, n in bins), ', '.join(scripts) or 'none (the shipped ones)'))
    log.append('build: %s: %s' % (b, ', '.join(n for _, n in bins)))
    log.append('build: the repository\'s Engine/Config/*.ini in System/')
    log.append('build: script packages %s' % (', '.join(scripts) if scripts else 'the shipped ones (the build has no '
                                                                                  'Scripts/System)'))
    # The build strips its packages with its game folder's per-class inis (BUILD.md, "Script patches"): their config
    # defaults are that folder's where the inis differ from this one's.
    assets = ''
    cache = os.path.join(b, 'CMakeCache.txt')
    for line in open(cache) if scripts and os.path.isfile(cache) else ():
        if line.startswith('UE_ASSETS_DIR:'):
            assets = line.split('=', 1)[1].strip()
    if assets:
        differ = [ini for ini in ('Characters.ini', 'Gametypes.ini', 'Maplists.ini')
                  if find_ci(assets, 'System/' + ini) and os.path.isfile(out.abs('System/' + ini))
                  and sha256(find_ci(assets, 'System/' + ini)) != sha256(out.abs('System/' + ini))]
        if differ:
            note = ('the build\'s script packages were stripped with %s/System, whose %s differ from this folder\'s: '
                    'their config defaults are that folder\'s (configure the build with -DUE_ASSETS_DIR=<a folder '
                    'cooked from this input> for this folder\'s)' % (assets, ', '.join(differ)))
            say('  note: ' + note)
            log.append('build: note: ' + note)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def tool_version():
    """The repository's commit, or the VERSION file the tool's archive carries."""
    if in_repository('.git') and shutil.which('git'):
        r = subprocess.run(['git', '-C', REPO, 'describe', '--always', '--dirty'], stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            return 'repository ' + r.stdout.decode().strip()
    try:
        with open(os.path.join(HERE, 'VERSION')) as f:
            return f.read().strip()
    except OSError:
        return 'unknown version'


def write_record(args, src, out, log):
    ff = subprocess.run(['ffmpeg', '-version'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode(
        'latin-1').split('\n')[0] if shutil.which('ffmpeg') else 'no ffmpeg'
    lines = ['cook.py %s' % ' '.join(sys.argv[1:]),
             'date: %s' % datetime.datetime.now().isoformat(timespec='seconds'),
             'source: %s, %s' % (args.source, os.path.abspath(args.input)),
             'tools: %s, %s, Python %s' % (tool_version(), ff, sys.version.split()[0]), ''] + log
    with open(out.abs('COOK.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    rels = sorted(r for r in out.new if r not in ('MANIFEST.sha256', 'COOK.txt'))
    with open(out.abs('MANIFEST.sha256'), 'w', newline='\n') as f:
        for rel in rels:
            f.write('%s  ./%s\n' % (sha256(out.abs(rel)), rel))
    out.new |= {'COOK.txt', 'MANIFEST.sha256'}


def report(problems):
    for what, why, how in problems:
        say('  missing: %s\n    for: %s\n    install: %s' % (what, why, how))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--in', dest='input', help='the input: a disc image, a disc folder or Epic\'s tree')
    ap.add_argument('--out', help='the game folder to write')
    ap.add_argument('--source', choices=('epic', 'xbox'), help="the input's kind, when it is not recognised")
    ap.add_argument('--pc-packages', default='', help='xbox: take the PC-format packages from this folder instead '
                                                      'of converting the disc\'s')
    ap.add_argument('--disc-tables', action='store_true', help="xbox: keep the disc's package tables (no network "
                                                               'play with the PC game folder)')
    ap.add_argument('--xbox-audio', action='store_true', help='the shipped .xsb/.xwb instead of cooked audio')
    ap.add_argument('--build', default='', help='a build directory whose binaries go into System/')
    ap.add_argument('--skip', default='', help='comma list of steps to leave out: audio, movies')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument('--check', action='store_true', help='a dry run of the tools and the input\'s checks, write '
                                                         'nothing')
    args = ap.parse_args()
    args.skip = {s for s in args.skip.split(',') if s}
    if args.skip - {'audio', 'movies'}:
        ap.error('--skip takes audio and movies')
    if not args.check and not (args.out and args.input):
        ap.error('--in and --out are needed (or --check)')
    if args.pc_packages and args.source == 'epic':
        ap.error('--pc-packages is for a disc (Epic\'s tree has its own)')

    img = None
    try:
        listing = None
        if args.input:
            listing, img = open_input(args)
            kind = 'xbox' if img is not None else looks_like(listing)
            if args.source and kind and kind != args.source:
                raise CookError('%s looks like %s, not --source %s' % (
                    args.input, {'epic': "Epic's source tree", 'xbox': 'an Xbox disc'}[kind], args.source))
            args.source = args.source or kind
            if not args.source:
                raise CookError('%s is neither an Xbox disc nor Epic\'s tree (--source says which)' % args.input)

        say('1. tools')
        problems = check_tools(args)
        report(problems)
        if problems:
            raise CookError('%d tool(s) missing: install them and run again' % len(problems))
        say('  Python %s, the scripts%s%s: ok' % (
            platform.python_version(), ', the PC tables' if args.source != 'epic' and not args.disc_tables else '',
            ', ffmpeg and ffprobe' if ffmpeg_steps(args) else ''))
        if not args.input:
            say('check: ok (--in <input> checks the input too)')
            return 0

        say('2. input')
        problems = check_input(args, listing)
        err = try_movie(args, listing, img) if not problems else None
        if err:
            problems.append('ffmpeg could not decode the movies: %s (%s)' % (err, ffmpeg_hint()))
        err = check_space(args, listing, img) if args.out else None
        if err:
            problems.append(err)
        for p in problems:
            say('  problem: ' + p)
        if problems:
            raise CookError('the input does not fit: %s' % args.source)
        say('  %s: %s%s' % (args.source, os.path.abspath(args.input), ' (a disc image)' if img else ''))
        if args.check:
            say('check: ok')
            return 0

        out = Output(os.path.abspath(args.out))
        out.open()
        try:
            src = extract(args, img, out.work) if img is not None else os.path.abspath(args.input)
            if img is not None:
                img.close()
                img = None
            log = []
            say('3. System/ and the packages')
            copy_packages(args, src, out, log)
            say('4. audio' + (' (skipped: an earlier cook\'s audio stays)' if 'audio' in args.skip else ''))
            if 'audio' not in args.skip:
                cook_audio(args, src, out, log)
            else:
                out.keep(('Sounds/', 'Music/'))
            say('5. movies' + (' (skipped: an earlier cook\'s movies stay)' if 'movies' in args.skip else ''))
            if 'movies' not in args.skip:
                cook_movies(args, src, out, log)
            else:
                out.keep(('Movies/', 'Textures/Movies/'))
            say('6. build' + ('' if args.build else ' (none: the engine\'s files go into System/)'))
            if args.build:
                install_build(args, out, log)
            say('7. COOK.txt, MANIFEST.sha256')
            write_record(args, src, out, log)
            out.finish()
        finally:
            out.close()
        say('done: %s (%d files)%s' % (out.path, len(out.new),
                                       '; run: cd %s/System && ./uc2.bin' % out.path if args.build else ''))
        return 0
    except CookError as ex:
        say('cook.py: %s' % ex)
        return 1
    finally:
        if img is not None:
            img.close()


if __name__ == '__main__':
    sys.exit(main())
