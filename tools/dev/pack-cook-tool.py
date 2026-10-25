#!/usr/bin/env python3
"""pack-cook-tool.py: the cook as a standalone archive for players: tools/cook.py, the scripts it runs and imports
(cook.SCRIPTS), the PC tables (tools/uc2-pc-tables.json.gz, which tools/dev/make-pc-tables.py makes), a VERSION and
tools/README.md, in one folder uc2-cook/ of a zip.

    tools/dev/pack-cook-tool.py [--out <dir>]          writes <dir>/uc2-cook-<version>.zip (default: the current dir)

The archive holds no game content: the player's disc (or Epic's tree) is the input. The same commit and tables give
the same archive (fixed timestamps).
"""
import argparse
import os
import subprocess
import sys
import zipfile

here = os.path.dirname(os.path.abspath(__file__))
tools = os.path.dirname(here)
sys.path.insert(0, tools)

import cook  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default='.')
    a = ap.parse_args()
    files = ['cook.py'] + list(cook.SCRIPTS) + [cook.TABLES]
    lacking = [f for f in files if not os.path.isfile(os.path.join(tools, *f.split('/')))]
    if lacking:
        sys.exit('missing: %s%s' % (', '.join(lacking), ' (tools/dev/make-pc-tables.py makes the tables)'
                                    if cook.TABLES in lacking else ''))
    rev = subprocess.run(['git', '-C', tools, 'describe', '--tags', '--always', '--dirty'], stdout=subprocess.PIPE,
                         check=True).stdout.decode().strip()
    version = 'uc2-cook %s' % rev
    path = os.path.join(a.out, 'uc2-cook-%s.zip' % rev)
    stamp = (2026, 1, 1, 0, 0, 0)
    with zipfile.ZipFile(path + '.part', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        def add(name, data, mode=0o644):
            info = zipfile.ZipInfo('uc2-cook/' + name, stamp)
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
        for f in files:
            with open(os.path.join(tools, *f.split('/')), 'rb') as fh:
                add(f, fh.read(), 0o755 if f.endswith('.py') and f != cook.TABLES else 0o644)
        add('VERSION', version + '\n')
        with open(os.path.join(tools, 'README.md'), 'rb') as fh:
            add('README.md', fh.read())
    os.replace(path + '.part', path)
    print('%s: %d files, %d bytes' % (path, len(files) + 2, os.path.getsize(path)))


if __name__ == '__main__':
    main()
