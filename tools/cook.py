#!/usr/bin/env python3
"""cook.py: cook the game folder the engine runs from, out of Epic's source tree or the game's Xbox disc.

  tools/cook.py --source epic --in <uc2-avaris-2226> --out <dir> [--build <build-dir>]
  tools/cook.py --source xbox --in <disc folder or image.iso> --out <dir> [--xbox-audio] [--build <build-dir>]
  tools/cook.py --check --source <epic|xbox> --in <...> [...]      the tools and the input only, nothing written

Sources (--source)
  epic  Epic's source tree: the PC-format packages (Maps/, Textures/, StaticMeshes/, Animations/), System/ (the 17
        shipped .u, the inis, the localisation), the XACT project with its WAVs (Sounds/Avaris.xap, SoundData/,
        Music/) and the XMV movies (Movies/, Textures/Movies/).
  xbox  the game's Xbox disc, as a folder or a disc image (an xiso or a full dump, read by tools/xiso.py): System/
        (the same .u, the final inis and localisation), Sounds/ (the XACT banks and Avaris.xap), Movies/ and
        Textures/Movies/, and the Xbox-format packages (XboxMaps/*.LIN linear loads, XboxTextures/,
        XboxStaticMeshes/, XboxAnimations/ with the texture and vertex data in .xpr bundles), which
        tools/cook-xbox-packages.py converts to the PC format. --pc-packages <dir> takes the PC-format packages
        from a folder instead (Epic's tree or a cooked game folder).

Steps
  1. tools: Python 3.8, ffmpeg and ffprobe with the codecs the cook needs (and, with --build, the libraries the game
     loads). Everything missing is listed with the packages that provide it, and the cook stops.
  2. input: its layout is checked against --source; a disc image is extracted once into <cache>/disc/<image>.
  3. System/ and the packages: copied (a reflink where the file system can), each package checked to be PC format;
     from a disc, converted by tools/cook-xbox-packages.py (report: Maps/xbox-packages.txt).
  4. audio (tools/cook-audio.py): Sounds/*.uc2wb, *.ogp, *.uc2cues, Music/*.ogg (Ogg Vorbis), from the project's WAVs or
     decoded from the disc's banks. --xbox-audio copies the disc's .xsb/.xwb instead, which the engine plays itself.
  5. movies (tools/cook-movies.py): Movies/*.ogv, *.oga and Textures/Movies/*.ogv (Ogg Theora/Vorbis).
  6. --build <build-dir>: the build's uc2.bin, ucc-bin and libopenal.so.1 (or the Windows .exe and OpenAL32.dll), its
     patched script packages and the repository's inis (Engine/Config) go into System/: start System/uc2.bin there.
  7. COOK.txt (what was cooked from what) and MANIFEST.sha256.

The output folder is created, or must be one cook.py made (it keeps the list of its files in .cook-files): a re-cook
replaces its files and removes the ones it no longer writes, and leaves everything else (the engine's Avaris.ini,
User.ini, logs, Save/) alone. The encoder caches are shared by every cook (--cache, default
$XDG_CACHE_HOME/uc2-cook or ~/.cache/uc2-cook): a second cook of the same sounds or movies only copies.

  --skip audio,movies   leave those steps out      --jobs N   parallel encoders (default: half the CPUs)
"""

import argparse
import datetime
import fcntl
import hashlib
import os
import shutil
import struct
import subprocess
import sys

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
FICLONE = 0x40049409

# ffmpeg features per step: (kind, name, why).
FFMPEG_NEEDS = {
    'audio': [('encoder', 'libvorbis', 'Ogg Vorbis sound and music'), ('demuxer', 'wav', 'the WAV sources')],
    'audio-xbox': [('decoder', 'wmav2', 'the WMA music track of the disc'), ('demuxer', 'asf', 'the WMA music track')],
    'movies': [('demuxer', 'xmv', 'the Xbox movies'), ('decoder', 'wmv2', 'XMV video'),
               ('decoder', 'adpcm_ima_xbox', 'XMV audio'), ('encoder', 'libtheora', 'Ogg Theora video'),
               ('encoder', 'libvorbis', 'Ogg Vorbis movie audio')],
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


class CookError(Exception):
    pass


def say(msg=''):
    print(msg, flush=True)


# ------------------------------------------------------------------------------------------------------------
# Tools
# ------------------------------------------------------------------------------------------------------------

def distro():
    """'debian', 'fedora', 'arch', 'suse' or '' from /etc/os-release."""
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


def install_hint(debian=(), fedora=(), arch=(), suse=()):
    d = distro()
    cmd = {'debian': ('sudo apt install', debian), 'fedora': ('sudo dnf install', fedora),
           'arch': ('sudo pacman -S', arch), 'suse': ('sudo zypper install', suse)}.get(d)
    if cmd and cmd[1]:
        return '%s %s' % (cmd[0], ' '.join(cmd[1]))
    return 'Debian/Ubuntu: sudo apt install %s; Fedora: sudo dnf install %s; Arch: sudo pacman -S %s' % (
        ' '.join(debian), ' '.join(fedora), ' '.join(arch))


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


def check_tools(args):
    """Returns the list of problems: (what is missing, why, how to install)."""
    problems = []
    if sys.version_info < (3, 8):
        problems.append(('Python 3.8 or newer', 'this tool', install_hint(['python3'], ['python3'], ['python'])))
    steps = []
    if 'audio' not in args.skip and not args.xbox_audio:
        steps.append('audio')
        if args.source == 'xbox':
            steps.append('audio-xbox')
    if 'movies' not in args.skip:
        steps.append('movies')
    if steps:
        missing = [t for t in ('ffmpeg', 'ffprobe') if not shutil.which(t)]
        for t in missing:
            problems.append((t, 'the audio and movie cooks', install_hint(['ffmpeg'], ['ffmpeg'], ['ffmpeg'],
                                                                          ['ffmpeg-7'])))
        if not missing:
            have = {k: ffmpeg_features(k) for k in ('encoder', 'decoder', 'demuxer')}
            for step in steps:
                for kind, name, why in FFMPEG_NEEDS[step]:
                    if name not in have[kind]:
                        problems.append(('ffmpeg %s %s' % (kind, name), why,
                                         'an ffmpeg built with it: ' + install_hint(
                                             ['ffmpeg'], ['ffmpeg (RPM Fusion)'], ['ffmpeg'], ['ffmpeg-7 (Packman)'])))
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


def looks_like(root):
    """'epic', 'xbox' or None, from the folders only Epic's tree or only a disc has."""
    if find_ci(root, 'SoundData') or files_in(root, 'Maps', '.unr'):
        return 'epic'
    if files_in(root, 'XboxMaps', '.lin') or find_ci(root, 'default.xbe') or not find_ci(root, 'Maps'):
        return 'xbox' if find_ci(root, 'System') else None
    return None


def open_input(args):
    """The input folder; a disc image is extracted into the cache first (System, Sounds, Movies, Textures)."""
    path = os.path.abspath(args.input)
    if os.path.isdir(path):
        return path
    if not os.path.isfile(path):
        raise CookError('--in %s: no such folder or file' % args.input)
    import xiso
    st = os.stat(path)
    dest = os.path.join(args.cache, 'disc', os.path.splitext(os.path.basename(path))[0])
    stamp = os.path.join(dest, '.extracted')
    # the folders are part of the key: an image extracted for another list is extracted again (kept files resume)
    key = '%s %d %d %s\n' % (os.path.basename(path), st.st_size, int(st.st_mtime), ','.join(DISC_DIRS))
    if os.path.exists(stamp) and open(stamp).read() == key:
        say('input: %s, extracted before into %s' % (path, dest))
        return dest
    say('input: %s, a disc image: extracting %s into %s' % (path, ', '.join(DISC_DIRS), dest))
    try:
        with xiso.Image(path) as img:
            n, total = img.extract(dest, DISC_DIRS)
    except xiso.XisoError as ex:
        raise CookError(str(ex))
    if n == 0:
        raise CookError('%s: the image has none of %s' % (path, ', '.join(DISC_DIRS)))
    with open(stamp, 'w') as f:
        f.write(key)
    say('  %d files, %.1f MB' % (n, total / 1e6))
    return dest


def check_input(args, src):
    """Returns the list of problems with the input's layout for --source."""
    problems = []
    kind = looks_like(src)
    if kind and kind != args.source:
        problems.append('%s looks like %s, not --source %s' % (
            src, {'epic': "Epic's source tree", 'xbox': 'an Xbox disc'}[kind], args.source))
    have = {os.path.splitext(os.path.basename(f))[0].lower() for f in files_in(src, 'System', '.u')}
    lacking = [p for p in SCRIPT_PACKAGES if p.lower() not in have]
    if lacking:
        problems.append('System/ lacks the script packages %s' % ', '.join(lacking))
    if 'audio' not in args.skip:
        if args.xbox_audio:
            if not files_in(src, 'Sounds', '.xsb'):
                problems.append('Sounds/ has no .xsb/.xwb banks (--xbox-audio)')
        elif not find_ci(src, 'Sounds/Avaris.xap'):
            problems.append('no Sounds/Avaris.xap (the XACT project the audio cook reads; --xbox-audio copies the '
                            'banks instead)')
        elif args.source == 'epic' and not find_ci(src, 'SoundData'):
            say('note: no SoundData/: every wave is decoded from the shipped .xwb')
    if 'movies' not in args.skip and not files_in(src, 'Movies', '.xmv'):
        problems.append('no Movies/*.xmv')
    pkgs = src if args.source == 'epic' else args.pc_packages
    if args.source == 'xbox' and not pkgs:
        for rel, ext in (('XboxMaps', '.lin'), ('XboxTextures', '.xpr'), ('XboxTextures', '.utx'),
                         ('XboxStaticMeshes', '.usx'), ('XboxAnimations', '.ukx')):
            if not files_in(src, rel, ext):
                problems.append('no %s/*%s (the Xbox-format packages to convert; --pc-packages takes PC ones '
                                'instead)' % (rel, ext))
        if not find_ci(src, 'System/Script.lin'):
            problems.append('no System/Script.lin (the entry level)')
    elif pkgs:
        for rel, ext in PACKAGE_DIRS[:1] + PACKAGE_DIRS[2:]:
            if not files_in(pkgs, rel, ext):
                problems.append('%s: no %s/*%s' % (pkgs, rel, ext))
    return problems


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
        self.old = set()
        self.new = set()

    def open(self):
        if os.path.isdir(self.path) and os.listdir(self.path) and not os.path.isfile(self.list_path):
            raise CookError('%s exists and was not made by cook.py (no .cook-files): give an empty or new folder'
                            % self.path)
        os.makedirs(self.path, exist_ok=True)
        if os.path.isfile(self.list_path):
            self.old = {l.rstrip('\n') for l in open(self.list_path) if l.strip()}

    def abs(self, rel):
        return os.path.join(self.path, *rel.split('/'))

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
                fcntl.ioctl(g.fileno(), FICLONE, f.fileno())
            except OSError:
                shutil.copyfileobj(f, g, 1 << 22)
        os.utime(tmp, (st.st_atime, st.st_mtime))
        os.chmod(tmp, 0o755 if st.st_mode & 0o111 else 0o644)
        os.replace(tmp, dst)
        return True

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
            n += out.copy(os.path.join(REPO, 'Engine', 'Config', ini), 'System/' + ini)
            log.append('System/%s: from Engine/Config (the input has none)' % ini)
    shipped = {}
    for line in open(os.path.join(REPO, 'patches', 'shipped.sha256')):
        if line.strip() and not line.startswith('#'):
            h, name = line.split()
            shipped[name] = h
    for name, h in sorted(shipped.items()):
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
    (tools/cook-xbox-packages.py), written straight into the output folder."""
    logfile = os.path.join(args.cache, 'logs', os.path.basename(out.path) + '-packages.log')
    cmd = [sys.executable, os.path.join(HERE, 'cook-xbox-packages.py'), '--in', src, '--out', out.path,
           '--jobs', str(args.jobs)]
    run_step(cmd, logfile, lambda l: l.startswith('FAILED'))
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
    log.append('Maps/, Textures/, StaticMeshes/, Animations/: converted from the disc by tools/cook-xbox-packages.py '
               '(report: Maps/xbox-packages.txt)')


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
    logfile = os.path.join(args.cache, 'logs', os.path.basename(out.path) + '-audio.log')
    cmd = [sys.executable, os.path.join(HERE, 'cook-audio.py'), '--src', src, '--out', out.path,
           '--cache', os.path.join(args.cache, 'audio'), '--jobs', str(args.jobs)]
    run_step(cmd, logfile, lambda l: l[:1].islower() or l.startswith('  '))
    out.adopt('Sounds')
    out.adopt('Music')
    log.append('audio: tools/cook-audio.py from %s (report: Sounds/cook-report.txt)' % sounds)


def cook_movies(args, src, out, log):
    cooked = os.path.join(args.cache, 'movies')
    logfile = os.path.join(args.cache, 'logs', os.path.basename(out.path) + '-movies.log')
    cmd = [sys.executable, os.path.join(HERE, 'cook-movies.py'), '--src', src, '--out', cooked,
           '--jobs', str(min(4, args.jobs))]
    run_step(cmd, logfile, lambda l: 'unchanged' not in l)
    import json
    manifest = json.load(open(os.path.join(cooked, 'Movies', 'movie-manifest.json')))
    out.drop(('Movies/', 'Textures/Movies/'))
    n = 0
    for m in manifest['movies']:
        for o in m['outputs']:
            rel = o['file'].replace(os.sep, '/')
            out.copy(os.path.join(cooked, rel), rel)
            n += 1
    for f in ('movie-manifest.json', 'cook-report.txt'):
        out.copy(os.path.join(cooked, 'Movies', f), 'Movies/' + f)
    say('  %d movies -> %d files' % (len(manifest['movies']), n))
    log.append('movies: tools/cook-movies.py from %s, %d movies, %d files' % (src, len(manifest['movies']), n))


def run_step(cmd, logfile, show):
    """Runs a step's tool, its whole output into logfile and the lines show() picks onto the console."""
    os.makedirs(os.path.dirname(logfile), exist_ok=True)
    say('  %s (log: %s)' % (' '.join(os.path.relpath(c, REPO) if c.startswith(REPO) else c for c in cmd[1:3]),
                            logfile))
    with open(logfile, 'w') as lf:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=REPO)
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


def write_record(args, src, out, log):
    git = subprocess.run(['git', '-C', REPO, 'describe', '--always', '--dirty'], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL).stdout.decode().strip()
    ff = subprocess.run(['ffmpeg', '-version'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode(
        'latin-1').split('\n')[0] if shutil.which('ffmpeg') else 'no ffmpeg'
    lines = ['cook.py %s' % ' '.join(sys.argv[1:]),
             'date: %s' % datetime.datetime.now().isoformat(timespec='seconds'),
             'source: %s, %s%s' % (args.source, src, ' (from %s)' % os.path.abspath(args.input) if src !=
                                   os.path.abspath(args.input) else ''),
             'tools: repository %s, %s, Python %s' % (git, ff, sys.version.split()[0]), ''] + log
    with open(out.abs('COOK.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    rels = sorted(r for r in out.new if r not in ('MANIFEST.sha256', 'COOK.txt'))
    with open(out.abs('MANIFEST.sha256'), 'w', newline='\n') as f:
        for rel in rels:
            f.write('%s  ./%s\n' % (sha256(out.abs(rel)), rel))
    out.new |= {'COOK.txt', 'MANIFEST.sha256'}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', required=True, choices=('epic', 'xbox'), help="Epic's source tree or the Xbox disc")
    ap.add_argument('--in', dest='input', required=True, help='the input folder, or a disc image (xbox)')
    ap.add_argument('--out', help='the game folder to write')
    ap.add_argument('--pc-packages', default='', help='xbox: take the PC-format packages from this folder instead '
                                                      'of converting the disc\'s')
    ap.add_argument('--xbox-audio', action='store_true', help='the shipped .xsb/.xwb instead of cooked audio')
    ap.add_argument('--build', default='', help='a build directory whose binaries go into System/')
    ap.add_argument('--skip', default='', help='comma list of steps to leave out: audio, movies')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument('--cache', default=os.path.join(os.environ.get('XDG_CACHE_HOME') or
                                                    os.path.expanduser('~/.cache'), 'uc2-cook'))
    ap.add_argument('--check', action='store_true', help='check the tools and the input, write nothing')
    args = ap.parse_args()
    args.skip = {s for s in args.skip.split(',') if s}
    if args.skip - {'audio', 'movies'}:
        ap.error('--skip takes audio and movies')
    if not args.check and not args.out:
        ap.error('--out is needed (or --check)')
    if args.pc_packages and args.source == 'epic':
        ap.error('--pc-packages is for --source xbox (Epic\'s tree has its own)')
    args.cache = os.path.abspath(args.cache)

    try:
        say('1. tools')
        problems = check_tools(args)
        for what, why, how in problems:
            say('  missing: %s (for %s)\n    install: %s' % (what, why, how))
        if problems:
            raise CookError('%d tool(s) missing: install them and run again' % len(problems))
        say('  ffmpeg, ffprobe and Python: ok')

        say('2. input')
        src = open_input(args)
        problems = check_input(args, src)
        for p in problems:
            say('  problem: ' + p)
        if problems:
            raise CookError('the input does not fit --source %s' % args.source)
        say('  %s: %s' % (args.source, src))
        if args.check:
            say('check: ok')
            return 0

        out = Output(os.path.abspath(args.out))
        out.open()
        os.makedirs(args.cache, exist_ok=True)
        with open(os.path.join(args.cache, '.lock'), 'w') as lock:
            # One cook at a time per cache: the movie step cooks into it.
            fcntl.flock(lock, fcntl.LOCK_EX)
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
            say('6. build' + ('' if args.build else ' (none: --build <build-dir> makes the folder runnable)'))
            if args.build:
                install_build(args, out, log)
            say('7. COOK.txt, MANIFEST.sha256')
            write_record(args, src, out, log)
            out.finish()
        say('done: %s (%d files)%s' % (out.path, len(out.new),
                                       '; run: cd %s/System && ./uc2.bin' % out.path if args.build else ''))
        return 0
    except CookError as ex:
        say('cook.py: %s' % ex)
        return 1


if __name__ == '__main__':
    sys.exit(main())
