#!/usr/bin/env python3
"""Regenerate Engine/Config/web/{Default,DefUser}.ini from the native files (M11).

  tools/dev/sync-web-ini.py            rewrite the two web files
  tools/dev/sync-web-ini.py --check    exit 1 if they are not what this script would write

The web build preloads Engine/Config/web/*.ini as /System/Default.ini and /System/DefUser.ini. They are the
native files with the differences listed in DELTAS below, and the keys and sections listed in WEB_KEYS /
WEB_SECTIONS kept as the current web file has them (the web audio and movie set-up), and nothing else, so that a change of the native
defaults (key bindings, the menus' key table, pad map, renderer settings) reaches the browser without anybody
copying it by hand: at M11 the web copies had fallen behind M7 and M10 and the menus would not have answered
the keyboard. Bytes are kept as they are (Latin-1), and every file in Engine/Config and Engine/Config/web has LF line
endings, as engine-ut2004's and engine-ut99's (owner, F-4d: "follow what UT2004 UT99 and the rest does"): the script
refuses a CR anywhere (--check fails, a rewrite stops) and writes the web files with LF. Except Epic's three reference
copies (EPIC_COPIES below), which stay byte for byte Epic's (CRLF; owner, F-4 follow-up): `cmp` against
<WorkUC2>/epic-sources/System/<file> must be silent, and --check says so when it is not.
"""
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
cfg = os.path.join(here, '..', '..', 'Engine', 'Config')

# Epic's files kept as reference copies, byte for byte (CRLF): DefaultXbox.ini and DefUserXbox.ini are read by nothing;
# KeyBinds.ini is read by the game (UCMenu UC2Settings_Controller: GetConfigValue("KeyBinds", "Layout_<n>", ...), the pad
# layouts) and is unchanged from Epic's, CRLF or not (the config cache reads both).
EPIC_COPIES = ('DefaultXbox.ini', 'DefUserXbox.ini', 'KeyBinds.ini')

# (file, section, key, native value expected, web value, comment line put before it or None)
DELTAS = [
    ('Default.ini', 'Engine.Engine', 'RenderDevice', 'UC2GLDrv.UC2GLRenderDevice', 'UC2GL4ESDrv.UC2GL4ESRenderDevice', None),
    # (G-5: the web keeps the native UseJoystick=True; the browser's Gamepad API reaches SDL2Drv through Emscripten's
    # SDL2 port, docs/porting/analysis/web-gamepad.md. The launcher's "Game pad" switch sets it per player.)
    # E-2: a browser has no sockets; the WebRTC driver over the frontend's data channels (webrtc.md)
    ('Default.ini', 'Engine.Engine', 'NetworkDevice', 'IpDrv.TcpNetDriver', 'WebRTCDrv.WebRTCNetDriver', None),
    # G-10 (owner, 2026-10-10: "disable antialiasing by default on the web, it is very heavy on a laptop, people can
    # go into the ini and enable it"): no multisampled main framebuffer in the browser unless the player sets it.
    ('Default.ini', 'UC2GLDrv.UC2GLRenderDevice', 'AntiAliasing', '4', '0', None),
    ('Default.ini', 'UC2GL4ESDrv.UC2GL4ESRenderDevice', 'AntiAliasing', '4', '0', None),
    # (G-11: no delta for [SDL2Drv.SDL2Client] PadsStartAtPlayerTwo, G-9's KeyboardIsPlayer: False natively and on the
    # web, the owner's rule; the launcher's "Force gamepads to start with second player" sets it per player.)
]

# Owned by the web build, not derived from the native file: whatever the CURRENT web file says for these is
# kept as it is (the web audio and movie set-up of the M11 audio work, merged separately: the audio device line,
# the openal-soft section with its web-only Backends= key, the movie section). A key or section the current web
# file does not have comes from the native file, or is left out if that has none either.
WEB_KEYS = [('Default.ini', 'Engine.Engine', 'AudioDevice')]
WEB_SECTIONS = [('Default.ini', 'UC2ALAudio.UC2ALAudioSubsystem'), ('Default.ini', 'UC2Movie'),
                # E-2: the WebRTC driver's own config section (no native counterpart; docs/porting/analysis/webrtc.md)
                ('Default.ini', 'WebRTCDrv.WebRTCNetDriver')]

def sections(lines):
    """[(name or None, first line index, end index)] over a list of lines."""
    out = []; cur = None; start = 0
    for i, line in enumerate(lines):
        m = re.match(r'\[(.+)\]\s*$', line)
        if m:
            out.append((cur, start, i)); cur = m.group(1); start = i
    out.append((cur, start, len(lines)))
    return out

def keep_web_owned(name, lines, nl):
    path = os.path.join(cfg, 'web', name)
    if not os.path.exists(path):
        return lines
    web = open(path, 'rb').read().decode('latin-1').replace('\r\n', '\n').split('\n')
    for f, section, key in WEB_KEYS:
        if f != name:
            continue
        have = [l for (sec, a, b) in sections(web) if sec == section for l in web[a:b] if l.startswith(key + '=')]
        if not have:
            continue
        for (sec, a, b) in sections(lines):
            if sec == section:
                for i in range(a, b):
                    if lines[i].startswith(key + '='):
                        lines[i] = have[0]; break
    for f, section in WEB_SECTIONS:
        if f != name:
            continue
        body = [web[a:b] for (sec, a, b) in sections(web) if sec == section]
        if not body:
            continue
        body = body[0]
        placed = False
        for (sec, a, b) in sections(lines):
            if sec == section:
                lines[a:b] = body; placed = True; break
        if not placed:
            # A section the native file does not have: before the SDL2 client's, as the web file had it.
            at = [a for (sec, a, b) in sections(lines) if sec == 'SDL2Drv.SDL2Client']
            at = at[0] if at else len(lines)
            lines[at:at] = body
    return lines

def build(name):
    data = open(os.path.join(cfg, name), 'rb').read().decode('latin-1')
    if '\r' in data:
        sys.exit('%s has CR line endings: Engine/Config is LF (F-4d, as engine-ut2004)' % name)
    nl = '\n'
    lines = keep_web_owned(name, data.split(nl), nl)
    for f, section, key, native, web, _ in DELTAS:
        if f != name:
            continue
        cur = None; done = False
        for i, line in enumerate(lines):
            m = re.match(r'\[(.+)\]\s*$', line)
            if m:
                cur = m.group(1); continue
            if cur == section and line.startswith(key + '='):
                if line != key + '=' + native:
                    sys.exit('%s [%s] %s: the native file has %r, this script expects %r: update DELTAS' % (name, section, key, line, key + '=' + native))
                lines[i] = key + '=' + web
                done = True
                break
        if not done:
            sys.exit('%s: [%s] %s not found' % (name, section, key))
    return nl.join(lines).encode('latin-1')

def main():
    check = '--check' in sys.argv
    bad = 0
    # LF everywhere in Engine/Config (F-4d): the native files, the web files, Epic's reference copies.
    for d in ('', 'web'):
        for f in sorted(os.listdir(os.path.join(cfg, d))):
            path = os.path.join(cfg, d, f)
            if d == '' and f in EPIC_COPIES:
                # Epic's file: the UC2 workspace's epic-sources/System, else WorkUnreal's cooked tree (the home since
                # 2026-10-11), whose System/ keeps Epic's inis byte for byte.
                for epic in (os.path.join(cfg, '..', '..', '..', 'epic-sources', 'System', f),
                             os.path.join(cfg, '..', '..', '..', 'assets', 'uc2-assets', 'System', f)):
                    if os.path.exists(epic):
                        if open(epic, 'rb').read() != open(path, 'rb').read():
                            print('%s is not byte for byte %s' % (f, os.path.normpath(epic))); bad = 1
                        break
                continue
            if f.lower().endswith('.ini') and b'\r' in open(path, 'rb').read():
                print('%s has CR line endings: Engine/Config is LF (F-4d, as engine-ut2004)' % os.path.join(d, f)); bad = 1
    for name in ('Default.ini', 'DefUser.ini'):
        want = build(name)
        path = os.path.join(cfg, 'web', name)
        have = open(path, 'rb').read() if os.path.exists(path) else b''
        if have != want:
            if check:
                print('web/%s is not the native file with the web deltas (run tools/dev/sync-web-ini.py)' % name); bad = 1
            else:
                tmp = path + '.tmp'
                open(tmp, 'wb').write(want); os.replace(tmp, path)
                print('web/%s rewritten (%d bytes)' % (name, len(want)))
        else:
            print('web/%s is up to date' % name)
    sys.exit(bad)

main()
