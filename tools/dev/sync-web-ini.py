#!/usr/bin/env python3
"""Regenerate Engine/Config/web/{Default,DefUser}.ini from the native files (M11).

  tools/dev/sync-web-ini.py            rewrite the two web files
  tools/dev/sync-web-ini.py --check    exit 1 if they are not what this script would write

The web build preloads Engine/Config/web/*.ini as /System/Default.ini and /System/DefUser.ini. They are the
native files with the differences listed in DELTAS below and nothing else, so that a change of the native
defaults (key bindings, the menus' key table, pad map, renderer settings) reaches the browser without anybody
copying it by hand: at M11 the web copies had fallen behind M7 and M10 and the menus would not have answered
the keyboard. Bytes are kept as they are (Latin-1, CRLF).
"""
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
cfg = os.path.join(here, '..', '..', 'Engine', 'Config')

# (file, section, key, native value expected, web value, comment line put before it or None)
DELTAS = [
    ('Default.ini', 'Engine.Engine', 'RenderDevice', 'UC2GLDrv.UC2GLRenderDevice', 'UC2GL4ESDrv.UC2GL4ESRenderDevice', None),
    # no audio device in the web build yet (openal-soft's Web Audio backend is a later step)
    ('Default.ini', 'Engine.Engine', 'AudioDevice', 'UC2ALAudio.UC2ALAudioSubsystem', 'Engine.NullAudioDevice', None),
    # game pads through the browser's Gamepad API are untested
    ('Default.ini', 'SDL2Drv.SDL2Client', 'UseJoystick', 'True', 'False', None),
]

def build(name):
    data = open(os.path.join(cfg, name), 'rb').read().decode('latin-1')
    nl = '\r\n' if '\r\n' in data else '\n'
    lines = data.split(nl)
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
