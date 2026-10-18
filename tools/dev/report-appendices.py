#!/usr/bin/env python3
"""Regenerate the appendices A-C of docs/porting/REPORT.md from git.

    tools/dev/report-appendices.py [TIP] > appendices.md        (TIP defaults to main)
    tools/dev/report-appendices.py <snapshot> | diff - <(sed -n '/^# Appendices/,$p' docs/porting/REPORT.md)

The method is the one written at the top of the appendices (kept here so anyone can reproduce them):

- A. Epic's files we modified: `git diff --name-status IMPORT TIP -- Source`, status M (a file that existed in
  Epic's import). +/- from `git diff --numstat`; the commit list is `git log --reverse IMPORT..TIP -- <file>`.
  Grouped by module (Source/<Module>/...), modules by file count then name, files by path; a commit line
  is the short hash and subject cut at 150 characters.
- B. New files of ours under Source/: status A, by module: file count, lines added, the first four paths.
- C. Every ini key that differs from Epic's: our `Engine/Config/*.ini` at TIP (read with `git show`, so the
  worktree does not matter) against `epic-sources/System/*.ini`, key by key. Multi-valued keys show only the
  values added (+) or removed (-). The comment column is the comment written above the key.

The report names its snapshot and commits by hashes of an older history; docs/porting/history-rewrite-commit-map.txt
maps them to this one.
"""
import collections
import os
import re
import subprocess
import sys

IMPORT = '7633696'
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
EPIC = os.path.join(os.path.dirname(REPO), 'epic-sources', 'System')
# The worktrees live next to engine-uc2; epic-sources is a sibling of them all.
if not os.path.isdir(EPIC):
    EPIC = '/home/ion/WorkUC2/epic-sources/System'


def git(*args, raw=False):
    out = subprocess.run(['git', '-C', REPO] + list(args), check=True, capture_output=True).stdout
    return out if raw else out.decode('utf-8', 'replace')


def short(rev):
    return git('rev-parse', '--short=7', rev).strip()


# ---------------------------------------------------------------- A and B: files
def files_part(tip):
    status = {}
    for line in git('diff', '--name-status', IMPORT, tip, '--', 'Source').splitlines():
        st, path = line.split('\t', 1)
        status[path] = st[0]
    numstat = {}
    for line in git('diff', '--numstat', IMPORT, tip, '--', 'Source').splitlines():
        a, d, path = line.split('\t', 2)
        numstat[path] = (a, d)

    def module(p):
        return p.split('/')[1]

    out = []
    modified = sorted(p for p, s in status.items() if s == 'M')
    bymod = collections.defaultdict(list)
    for p in modified:
        bymod[module(p)].append(p)
    out.append("## Appendix A. Epic's files we modified (%d), with the commits that touched them\n" % len(modified))
    out.append('Generated from `git diff --name-status %s %s -- Source` (files that existed in Epic\'s tree). '
               '+/- = lines added/removed. The commit list is every commit since the import that touched the file, '
               'oldest first.\n' % (IMPORT, tip))
    for mod in sorted(bymod, key=lambda m: (-len(bymod[m]), m)):
        out.append('### %s (%d files)\n' % (mod, len(bymod[mod])))
        for p in bymod[mod]:
            a, d = numstat.get(p, ('0', '0'))
            out.append('- `%s` (+%s/-%s)' % (p, a, d))
            for c in git('log', '--reverse', '--format=%h %s', '%s..%s' % (IMPORT, tip), '--', p).splitlines():
                out.append('  - ' + c[:150])  # hash + subject, cut at 150 characters
        out.append('')

    added = sorted(p for p, s in status.items() if s == 'A')
    amod = collections.defaultdict(list)
    for p in added:
        amod[module(p)].append(p)
    out.append('## Appendix B. New files of ours under Source/ (%d), by module\n' % len(added))
    out.append('| Module | Files | Lines | Examples |')
    out.append('|---|---|---|---|')
    for mod in sorted(amod, key=lambda m: (-len(amod[m]), m)):
        ps = amod[mod]
        lines = sum(int(numstat[p][0]) for p in ps if numstat.get(p, ('-',))[0] != '-')
        ex = ', '.join('`%s`' % p.split('/', 2)[2] for p in ps[:4])
        out.append('| %s | %d | %d | %s |' % (mod, len(ps), lines, ex))
    out.append('')
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- C: ini keys
def parse(text):
    secs = collections.OrderedDict()
    cur = None
    pend = []
    for raw in text.splitlines(True):
        line = raw.rstrip('\r\n')
        s = line.strip()
        if not s:
            pend = []
            continue
        if s.startswith(';'):
            t = s.lstrip('; ')
            if not re.match(r'^[A-Za-z_][\w.()]*\s*=', t):
                pend.append(t)
            continue
        m = re.match(r'^\[(.*)\]$', s)
        if m:
            cur = m.group(1)
            secs.setdefault(cur, [])
            pend = []
            continue
        if '=' in s and cur is not None:
            k, v = s.split('=', 1)
            secs[cur].append((k.strip(), v.strip(), ' '.join(pend)[:300]))
            pend = []
    return secs


def kv(lst):
    d = collections.OrderedDict()
    for k, v, c in lst:
        d.setdefault(k, []).append((v, c))
    return d


# A section of ours that replaces one of Epic's: a key Epic had in the old section is compared with that.
EQUIV = {'SDL2Drv.SDL2Client': 'WinDrv.WindowsClient',
         'UC2GLDrv.UC2GLRenderDevice': 'D3DDrv.D3DRenderDevice',
         'UC2GL4ESDrv.UC2GL4ESRenderDevice': 'D3DDrv.D3DRenderDevice'}


def diff(old_text, new_text, title):
    a, b = parse(old_text), parse(new_text)
    out = ['### %s\n' % title, '| Section | Key | Epic | Ours | Our comment in the ini |', '|---|---|---|---|---|']
    n = 0

    def esc(s):
        return s.replace('|', '\\|')

    for sec in list(b.keys()) + [s for s in a if s not in b]:
        ka, kb = kv(a.get(sec, [])), kv(b.get(sec, []))
        for k in list(kb.keys()) + [k for k in ka if k not in kb]:
            va = [v for v, c in ka.get(k, [])]
            vb = [v for v, c in kb.get(k, [])]
            if va == vb:
                continue
            if len(va) > 1 or len(vb) > 1:
                sa, sb = set(va), set(vb)
                add = [v for v in vb if v not in sa]
                rem = [v for v in va if v not in sb]
                if not add and not rem:
                    continue
                ov = '; '.join('−' + x for x in rem) or ''
                nv = '; '.join('+' + x for x in add) or ''
            else:
                ov = va[0] if va else '(absent)'
                nv = vb[0] if vb else '(removed)'
                eq = EQUIV.get(sec)
                if not va and eq and eq in a:
                    ve = [v for v, c in kv(a[eq]).get(k, [])]
                    if ve:
                        if ve[0] == nv:
                            continue
                        ov = '(%s: %s)' % (eq.split('.')[1], ve[0])
            com = (kb.get(k) or [('', '')])[-1][1]
            out.append('| %s | %s | %s | %s | %s |' % (esc(sec if sec in a else sec + ' (new section)'), esc(k),
                                                       esc(ov[:80]), esc(nv[:80]), esc(com)))
            n += 1
    out.insert(1, '%d keys differ.\n' % n)
    return '\n'.join(out) + '\n'


def ini_part(tip):
    def ours(name, raw=False):
        b = git('show', '%s:Engine/Config/%s' % (tip, name), raw=True)
        return b if raw else b.decode('latin-1')

    def epic(name, raw=False):
        with open(os.path.join(EPIC, name), 'rb') as f:
            b = f.read()
        return b if raw else b.decode('latin-1')

    out = ["## Appendix C. Every ini key that differs from Epic's\n",
           'Generated by comparing our `Engine/Config/*.ini` (snapshot %s) with `epic-sources/System/*.ini`, key by '
           'key. Multi-valued keys (Paths, EditPackages, ServerPackages…) show only the values added (+) or '
           'removed (−). The comment column is the comment we wrote above the key, when there is one.\n' % tip,
           diff(epic('Default.ini'), ours('Default.ini'), 'Default.ini (native; the System ini, "Avaris.ini" at run time)'),
           diff(epic('DefUser.ini'), ours('DefUser.ini'), 'DefUser.ini (native; the user ini)'),
           diff(epic('KeyBinds.ini'), ours('KeyBinds.ini'), 'KeyBinds.ini'),
           diff(ours('Default.ini'), ours('web/Default.ini'), 'web/Default.ini compared with OUR native Default.ini'),
           diff(ours('DefUser.ini'), ours('web/DefUser.ini'), 'web/DefUser.ini compared with OUR native DefUser.ini')]
    for f in ['DefaultXbox.ini', 'DefUserXbox.ini']:
        same = epic(f, raw=True) == ours(f, raw=True)
        out.append('- `%s`: %s' % (f, "identical to Epic's (kept for reference, unused)" if same
                                   else "DIFFERS from Epic's"))
    return '\n'.join(out) + '\n\n'


def main():
    tip = short(sys.argv[1] if len(sys.argv) > 1 else 'main')
    sys.stdout.write('# Appendices (generated from git)\n\n' + files_part(tip) + ini_part(tip))


if __name__ == '__main__':
    main()
