#!/usr/bin/env python3
"""uc-patches: the script patch sets of patches/ (make, apply, check, rebase, compare).

Our UnrealScript changes are unified diffs against the base: the classes of the shipped .u as tools/scripts/
uc-decompile.py writes them (decompiled; UCMenu: its own shipped text plus decompiled defaultproperties). A tree is
<dir>/<Pkg>/Classes/<Class>.uc; a set is patches/<set>/ with one <Pkg>.diff per package it changes and whole new
classes under patches/<set>/new/<Pkg>/Classes/. The sets, their order and dependencies are patches/sets.txt.
patches/README.md for users.

  uc-patches.py make <set> --base <tree> --changed <tree> [--packages A,B] [--patches <dir>]
      write patches/<set>/ from the difference base -> changed (the set's .diff files and new/ are replaced; its
      README.md is not touched). Line 1 of a base class is its identity line (decompiler version, source .u) and
      is never part of a patch.
  uc-patches.py apply --base <tree> --out <tree> [--sets a,b,... | --default] [--patches <dir>]
      copy the base to out and apply the sets in order. Exact context, no fuzz: a hunk whose context lines are not
      found exactly is a reject and the command fails (exit 1). A hunk may move (other sets changed the file
      above it): the offset is reported. A package the base tree does not have is left out (not rebuilt).
  uc-patches.py check --base <tree> [--sets ... | --default]   apply into a scratch folder and report
  uc-patches.py sets [--sets ... | --default]                 the sets that would be applied, in order
  uc-patches.py packages [--sets ... | --default] [--with-epic-only] [--compile-list <shipped System>]
      the packages the sets change, one per line (the epic-fixes-only packages are left out unless
      --with-epic-only); with --compile-list also the packages that must be compiled first to build them
      against the shipped ones (see compile_list)
  uc-patches.py compare <a.u> <b.u> [--system-a <dir>] [--system-b <dir>]   P2: same content, index order aside
  uc-patches.py rebase --new-base <tree> --stages <dir> --sets ...   re-make every set against a new base (see
      cmd_rebase; tools/scripts/uc-patches-proof.sh rebase drives it)

Bytes in, bytes out: every file is read and written as Latin-1 bytes, lines split at LF only (0x02, 0x85 and CR
bytes inside a line are line content), so a patch carries 0x02 glyph bytes unchanged.
"""
import difflib
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PATCHES = os.path.join(REPO, 'patches')
IDENT = re.compile(r'// (decompiled by uc-decompile |shipped text of )')


class PatchError(Exception):
    pass


# --- files ---------------------------------------------------------------------------------------------------------
def read_lines(path):
    """Lines of a file, each with its LF (the last one without when the file does not end in LF)."""
    with open(path, 'rb') as f:
        data = f.read().decode('latin-1')
    out, i = [], 0
    while i < len(data):
        j = data.find('\n', i)
        if j < 0:
            out.append(data[i:])
            break
        out.append(data[i:j + 1])
        i = j + 1
    return out


def write_lines(path, lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(''.join(lines).encode('latin-1'))


def tree_files(root):
    """{'<Pkg>/Classes/<Class>.uc': path} of a tree."""
    out = {}
    if not os.path.isdir(root):
        raise PatchError('%s: no such folder' % root)
    for pkg in sorted(os.listdir(root)):
        cdir = os.path.join(root, pkg, 'Classes')
        if not os.path.isdir(cdir):
            continue
        for fn in sorted(os.listdir(cdir)):
            if fn.endswith('.uc'):
                out['%s/Classes/%s' % (pkg, fn)] = os.path.join(cdir, fn)
    return out


def ident_split(line):
    """(line 1 without its identity comment, the identity comment) or (line, None)."""
    m = IDENT.search(line)
    if not m:
        return line, None
    return line[:m.start()].rstrip(), line[m.start():]


# --- sets ----------------------------------------------------------------------------------------------------------
def read_sets(pdir):
    """patches/sets.txt: [(name, depends, default, epic_only)] in apply order."""
    path = os.path.join(pdir, 'sets.txt')
    sets = []
    with open(path, 'rb') as f:
        for raw in f.read().decode('latin-1').split('\n'):
            line = raw.split('#', 1)[0].strip()
            if not line:
                continue
            w = line.split()
            if len(w) != 4:
                raise PatchError('%s: bad line %r (want: name depends-on default kind)' % (path, raw))
            name, dep, default, kind = w
            deps = [] if dep == '-' else dep.split(',')
            if default not in ('on', 'off', 'auto'):
                raise PatchError('%s: %s: default must be on, off or auto' % (path, name))
            if kind not in ('base-fixes', 'ours'):
                raise PatchError('%s: %s: kind must be base-fixes or ours' % (path, name))
            sets.append((name, deps, default, kind))
    return sets


def resolve_sets(pdir, wanted, default):
    """The ordered list of sets to apply: the default ones or the named ones, plus every `auto` set whose
    dependencies are all in; refuses a set whose dependency is missing."""
    table = read_sets(pdir)
    names = [s[0] for s in table]
    if default:
        chosen = {s[0] for s in table if s[2] == 'on'}
    else:
        chosen = set(wanted)
        for w in wanted:
            if w not in names:
                raise PatchError('unknown set %s (patches/sets.txt: %s)' % (w, ', '.join(names)))
    for name, deps, dflt, kind in table:
        if dflt == 'auto' and all(d in chosen for d in deps):
            chosen.add(name)
    out = []
    for name, deps, dflt, kind in table:
        if name in chosen:
            miss = [d for d in deps if d not in chosen]
            if miss:
                raise PatchError('set %s needs %s' % (name, ', '.join(miss)))
            out.append(name)
    return out


def set_files(pdir, name):
    """(diff files, {'<Pkg>/Classes/<Class>.uc': path} of new classes) of a set."""
    d = os.path.join(pdir, name)
    if not os.path.isdir(d):
        raise PatchError('%s: no such set folder' % d)
    diffs = [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith('.diff')]
    new = tree_files(os.path.join(d, 'new')) if os.path.isdir(os.path.join(d, 'new')) else {}
    return diffs, new


def set_packages(pdir, name):
    diffs, new = set_files(pdir, name)
    pk = {os.path.basename(f)[:-5] for f in diffs} | {k.split('/')[0] for k in new}
    return sorted(pk)


# --- make ----------------------------------------------------------------------------------------------------------
def unified(rel, a, b, n=3):
    """A unified diff of line lists a -> b (each line with its LF), as a list of lines."""
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    out = []
    for group in sm.get_grouped_opcodes(n):
        i1, i2, j1, j2 = group[0][1], group[-1][2], group[0][3], group[-1][4]
        out.append('@@ -%s +%s @@\n' % (rng(i1, i2), rng(j1, j2)))
        for tag, a1, a2, b1, b2 in group:
            if tag == 'equal':
                out += [' ' + x for x in a[a1:a2]]
                continue
            out += ['-' + x for x in a[a1:a2]]
            out += ['+' + x for x in b[b1:b2]]
    for x in out:
        if not x.endswith('\n'):
            raise PatchError('%s: a line without LF at the end of the file' % rel)
    if out:
        out = ['--- a/%s\n' % rel, '+++ b/%s\n' % rel] + out
    return out


def rng(i1, i2):
    n = i2 - i1
    start = i1 + 1 if n else i1
    return '%d' % start if n == 1 else '%d,%d' % (start, n)


def cmd_make(name, base, changed, packages, pdir):
    sets = [s[0] for s in read_sets(pdir)]
    if name not in sets:
        raise PatchError('set %s is not in %s/sets.txt' % (name, pdir))
    bf, cf = tree_files(base), tree_files(changed)
    if packages:
        bf = {k: v for k, v in bf.items() if k.split('/')[0] in packages}
        cf = {k: v for k, v in cf.items() if k.split('/')[0] in packages}
    gone = sorted(set(bf) - set(cf))
    if gone:
        raise PatchError('classes removed in the changed tree (a set cannot delete a class): %s' % ', '.join(gone))
    sdir = os.path.join(pdir, name)
    os.makedirs(sdir, exist_ok=True)
    # the set's files for the packages in scope are replaced; with --packages, the others are kept
    for f in os.listdir(sdir):
        if f.endswith('.diff') and (not packages or f[:-5] in packages):
            os.remove(os.path.join(sdir, f))
    for pk in (os.listdir(os.path.join(sdir, 'new')) if os.path.isdir(os.path.join(sdir, 'new')) else []):
        if not packages or pk in packages:
            # only the classes: a set's resources (new/<Pkg>/<folder>/, files an #exec imports) are not made from trees
            if os.path.isdir(os.path.join(sdir, 'new', pk, 'Classes')):
                shutil.rmtree(os.path.join(sdir, 'new', pk, 'Classes'))
    per_pkg = {}
    stats = []
    for rel in sorted(cf):
        b = read_lines(cf[rel])
        if rel not in bf:
            if b and IDENT.search(b[0]):
                raise PatchError('%s: a new class must be our own text, not a decompiled/shipped one' % rel)
            write_lines(os.path.join(sdir, 'new', rel), b)
            stats.append('new %s (%d lines)' % (rel, len(b)))
            continue
        a = read_lines(bf[rel])
        if not a or ident_split(a[0])[1] is None:
            raise PatchError('%s: the base file has no identity line (not written by uc-decompile)' % bf[rel])
        if b:
            ra, rb = ident_split(a[0])[0], ident_split(b[0])[0]
            if ra != rb:
                raise PatchError('%s: line 1 differs beyond the identity comment (%r -> %r): a set may not change '
                                 'line 1' % (rel, ra, rb))
            b = [a[0]] + b[1:]
        d = unified(rel, a, b)
        if d:
            per_pkg.setdefault(rel.split('/')[0], []).extend(d)
            stats.append('diff %s (%d hunks, +%d -%d)' % (rel, sum(1 for x in d if x.startswith('@@')),
                                                         sum(1 for x in d[2:] if x.startswith('+') and not x.startswith('+++')),
                                                         sum(1 for x in d[2:] if x.startswith('-') and not x.startswith('---'))))
    for pkg, lines in per_pkg.items():
        write_lines(os.path.join(sdir, pkg + '.diff'), lines)
    print('set %s: %s' % (name, '; '.join(stats) if stats else 'empty'))
    return stats


# --- apply ---------------------------------------------------------------------------------------------------------
def parse_diff(path):
    """[(rel, [(old_start, old_lines, new_lines)])] of a unified diff file."""
    lines = read_lines(path)
    files = []
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith('--- '):
            if i + 1 >= len(lines) or not lines[i + 1].startswith('+++ '):
                raise PatchError('%s:%d: --- without +++' % (path, i + 1))
            rel = lines[i + 1][4:].rstrip('\n')
            if rel.startswith('b/'):
                rel = rel[2:]
            files.append((rel, []))
            i += 2
            continue
        m = re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', l)
        if m:
            if not files:
                raise PatchError('%s:%d: hunk before a file header' % (path, i + 1))
            os_, oc = int(m.group(1)), int(m.group(2) if m.group(2) is not None else 1)
            nc = int(m.group(4) if m.group(4) is not None else 1)
            old, new = [], []
            i += 1
            while i < len(lines) and (len(old) < oc or len(new) < nc):
                x = lines[i]
                if x.startswith(' '):
                    old.append(x[1:])
                    new.append(x[1:])
                elif x.startswith('-'):
                    old.append(x[1:])
                elif x.startswith('+'):
                    new.append(x[1:])
                elif x.startswith('\\'):
                    raise PatchError('%s:%d: "no newline" marks are not used by these patches' % (path, i + 1))
                else:
                    raise PatchError('%s:%d: unexpected line in a hunk: %r' % (path, i + 1, x[:60]))
                i += 1
            if len(old) != oc or len(new) != nc:
                raise PatchError('%s: hunk at line %d is short' % (path, os_))
            files[-1][1].append((os_, old, new))
            continue
        if l.strip():
            raise PatchError('%s:%d: unexpected line %r' % (path, i + 1, l[:60]))
        i += 1
    return files


def apply_hunks(rel, lines, hunks, report):
    """Apply hunks in order (exact context, no fuzz); the search for a moved hunk goes outwards from where it
    should be."""
    out = list(lines)
    first = out[0] if out and ident_split(out[0])[1] else None
    delta = 0

    def same(seg, old):
        # the identity line (line 1) matches whatever decompiler version or .u it names
        return all(x == y or (ident_split(x)[1] and ident_split(y)[1] and ident_split(x)[0] == ident_split(y)[0])
                   for x, y in zip(seg, old))
    for os_, old, new in hunks:
        want = (os_ - 1 if old else os_) + delta
        pos = None
        for d in range(0, len(out) + 1):
            for p in (want - d, want + d) if d else (want,):
                if 0 <= p <= len(out) - len(old) and same(out[p:p + len(old)], old):
                    pos = p
                    break
            if pos is not None:
                break
        if pos is None:
            raise PatchError('%s: hunk @@ -%d,%d @@ does not apply (context not found; no fuzz)' % (rel, os_, len(old)))
        if pos != want:
            report.append('  %s: hunk @@ -%d @@ applied at offset %+d' % (rel, os_, pos - want))
        out[pos:pos + len(old)] = new
        delta += len(new) - len(old) + (pos - want)
    if first is not None and out and ident_split(out[0])[1]:
        out[0] = first
    return out


def cmd_apply(base, out, sets, pdir, quiet=False):
    if os.path.abspath(out) == os.path.abspath(base):
        raise PatchError('--out must not be the base')
    if os.path.exists(out):
        shutil.rmtree(out)
    shutil.copytree(base, out)
    files = tree_files(out)
    summary = []
    for name in sets:
        diffs, new = set_files(pdir, name)
        report = []
        nh = 0
        skipped = set()
        for d in diffs:
            for rel, hunks in parse_diff(d):
                if not os.path.isdir(os.path.join(out, rel.split('/')[0])):
                    skipped.add(rel.split('/')[0])     # a package this build does not rebuild
                    continue
                if rel not in files:
                    raise PatchError('set %s: %s: no such class in the tree' % (name, rel))
                lines = read_lines(files[rel])
                write_lines(files[rel], apply_hunks(rel, lines, hunks, report))
                nh += len(hunks)
        for rel, src in sorted(new.items()):
            if not os.path.isdir(os.path.join(out, rel.split('/')[0])):
                skipped.add(rel.split('/')[0])
                continue
            if rel in files:
                raise PatchError('set %s: new class %s already exists in the tree' % (name, rel))
            dst = os.path.join(out, rel)
            write_lines(dst, read_lines(src))
            files[rel] = dst
        # A set's resources, new/<Pkg>/<folder>/ beside Classes (files its classes import with #exec, e.g.
        # Textures/), go into the tree as <Pkg>/<folder>/; uc-build.sh links them beside the package's Classes
        ndir = os.path.join(pdir, name, 'new')
        for pk in (sorted(os.listdir(ndir)) if os.path.isdir(ndir) else []):
            for sub in sorted(os.listdir(os.path.join(ndir, pk))):
                src = os.path.join(ndir, pk, sub)
                if sub == 'Classes' or not os.path.isdir(src) or not os.path.isdir(os.path.join(out, pk)):
                    continue
                shutil.copytree(src, os.path.join(out, pk, sub), dirs_exist_ok=True)
        summary.append('set %-22s %3d hunks, %d new classes%s%s' % (
            name, nh, len(new) - len([r for r in new if r.split('/')[0] in skipped]),
            '' if not report else ', %d moved' % len(report),
            '' if not skipped else ' (not in this tree, left out: %s)' % ', '.join(sorted(skipped))))
        summary += report
    if not quiet:
        print('\n'.join(summary))
    return summary


# --- compare (P2) --------------------------------------------------------------------------------------------------
def cmd_compare(a_path, b_path, sys_a, sys_b, report_path=None):
    """Two builds of one package, conformed to the same shipped file but possibly appending in another order:
    every export path present in both with the same class and flags; per function/state the bytecode token stream
    (offsets, tokens, references resolved to paths) and every other field; per class the declarations and the
    decompiled defaultproperties text; names and imports as sets. Line/TextPos, dependency records, text buffers
    and Children/Next order are bookkeeping and not compared."""
    sys.path.insert(0, HERE)
    import importlib.util
    import ucpkg
    spec = importlib.util.spec_from_file_location('ucdec', os.path.join(HERE, 'uc-decompile.py'))
    ucdec = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ucdec)
    pkgname = os.path.basename(a_path)[:-2]
    wa = ucdec.World(sys_a or os.path.dirname(os.path.abspath(a_path)))
    wb = ucdec.World(sys_b or os.path.dirname(os.path.abspath(b_path)))
    pa, pb = wa.pkgs[pkgname], wb.pkgs[pkgname]
    lines, bad = [], []

    def norm(t):
        """Names compare without case (an FName keeps the case of its first creation, which depends on what was
        loaded first: neutral, as script-diff.py's name-case); strings keep theirs."""
        if t.startswith(('StringConst', 'UnicodeStringConst')):
            return t
        return re.sub(r'"(?:[^"\\]|\\.)*"|[^"]+', lambda m: m.group(0) if m.group(0).startswith('"') else m.group(0).lower(), t)
    na, nb = {n.lower() for n in pa.names}, {n.lower() for n in pb.names}
    if na != nb:
        bad.append('names differ: only a %s; only b %s' % (sorted(na - nb)[:20], sorted(nb - na)[:20]))
    ia = {pa.import_path(i).lower() for i in range(len(pa.imports))}
    ib = {pb.import_path(i).lower() for i in range(len(pb.imports))}
    if ia != ib:
        bad.append('imports differ: only a %s; only b %s' % (sorted(ia - ib)[:20], sorted(ib - ia)[:20]))
    ea = {pa.export_path(i).lower(): i for i in range(len(pa.exports)) if pa.exports[i]['name'] != 'None' or pa.exports[i]['size']}
    eb = {pb.export_path(i).lower(): i for i in range(len(pb.exports)) if pb.exports[i]['name'] != 'None' or pb.exports[i]['size']}
    for p in sorted(set(ea) ^ set(eb)):
        bad.append('export %s only in %s' % (p, 'a' if p in ea else 'b'))
    skip = {'line', 'textpos', 'dependencies', 'next', 'children', 'text', 'pos', 'top', 'scripttext', 'cpptext',
            'script', '@defaults', '@props', 'funcmap', '@rest'}
    nfun = nsame = 0
    for p in sorted(set(ea) & set(eb)):
        i, j = ea[p], eb[p]
        ca, cb = pa.export_class(i), pb.export_class(j)
        if ca != cb:
            bad.append('%s: class %s / %s' % (p, ca, cb))
            continue
        if pa.exports[i]['flags'] != pb.exports[j]['flags']:
            bad.append('%s: object flags 0x%08x / 0x%08x' % (p, pa.exports[i]['flags'], pb.exports[j]['flags']))
        try:
            fa, fb = ucpkg.parse_export(pa, i), ucpkg.parse_export(pb, j)
        except ucpkg.ParseError as e:
            bad.append('%s: cannot parse (%s)' % (p, e))
            continue
        for k in sorted(set(fa) | set(fb), key=str):
            if k in skip:
                continue
            if fa.get(k) != fb.get(k):
                if k == 'classflags' and (fa[k] ^ fb[k]) == ucpkg.CLASS_FullDefaults:
                    continue
                if norm(repr(fa.get(k))) == norm(repr(fb.get(k))):
                    continue
                bad.append('%s: %s %r / %r' % (p, k, fa.get(k), fb.get(k)))
        if 'funcmap' in fa and sorted(norm(repr(x)) for x in fa['funcmap']) != sorted(norm(repr(x)) for x in fb['funcmap']):
            bad.append('%s: funcmap differs' % p)
        if ca in ('Function', 'State') or ca == 'Class':
            ta, tb = ucpkg.disasm(pa, fa.get('script', b'')), ucpkg.disasm(pb, fb.get('script', b''))
            if ca in ('Function', 'State'):
                nfun += 1
            ta = [(o, norm(x)) for o, x in ta]
            tb = [(o, norm(x)) for o, x in tb]
            if ta != tb:
                bad.append('%s: bytecode differs' % p)
                bad += ['    ' + x for x in difflib.unified_diff(['%5d %s' % t for t in ta], ['%5d %s' % t for t in tb],
                                                                  'a', 'b', n=1, lineterm='')][:40]
            elif ca in ('Function', 'State'):
                nsame += 1
        if '@rest' in fa and fa['@rest'] != fb.get('@rest'):
            bad.append('%s: object data differs (%d / %d bytes)' % (p, len(fa['@rest'] or b''), len(fb.get('@rest') or b'')))
    # defaults (class defaults and subobjects), as the decompiler prints them: references resolved by path
    da, db = ucdec.Decompiler(wa), ucdec.Decompiler(wb)
    ndef = 0
    for o in wa.objs[pkgname]:
        if o.cls != 'Class' or not o.pkg.exports[o.idx]['size']:
            continue        # (an empty export slot of a conformed save: name None, no data)
        ob = wb.pkgs[pkgname].exp_paths.get(o.path)
        if ob is None:
            ob = next((x for x in wb.objs[pkgname] if x.path.lower() == o.path.lower()), None)
        if ob is None:
            continue
        for dd, oo in ((da, o), (db, ob)):
            dd.cls, dd.typer, dd.errors, dd.loads = oo, ucdec.Typer(dd.world, oo), [], []
        try:
            xa, xb = da.defaults(o), db.defaults(ob)
        except Exception as e:  # noqa: BLE001
            bad.append('%s: defaults not printable (%s)' % (o.path, e))
            continue
        ndef += 1
        if [norm(x) for x in xa] != [norm(x) for x in xb]:
            bad.append('%s: defaults differ' % o.path)
            bad += ['    ' + x for x in difflib.unified_diff(xa, xb, 'a', 'b', n=0, lineterm='')][:40]
    lines.append('%s: exports %d / %d, functions+states %d (%d bytecode-identical), classes with defaults compared %d, '
                 'differences %d' % (pkgname, len(ea), len(eb), nfun, nsame, ndef, len([x for x in bad if not x.startswith('    ')])))
    if report_path:
        with open(report_path, 'w') as f:
            f.write('\n'.join(lines + bad) + '\n')
    print(lines[0])
    for x in bad[:30]:
        print('  ' + x)
    return 0 if not bad else 1


# --- build order ---------------------------------------------------------------------------------------------------
ORDER = ['Core', 'Engine', 'UCAnnouncements', 'Editor', 'UnrealEd', 'IpDrv', 'UWeb', 'GamePlay', 'UnrealGame',
         'UCElements', 'UCGameplay', 'UCEffects', 'UCCharacters', 'UCInventory', 'UCMutators', 'GUI', 'UCMenu']


def compile_list(shipped, targets):
    """The packages to compile, in order, to build `targets` against the shipped packages one at a time (each
    with only the packages before it present, as Epic's build from scratch had them): a shipped package that
    imports a package at or after the one being compiled cannot be loaded then (the shipped files went through
    `stripsource` with every package loaded, so they refer forward: UCAnnouncements.u imports UCEffects, which
    imports UCGameplay), so it is compiled from its base first. Prints targets and those extra packages."""
    sys.path.insert(0, HERE)
    import ucpkg
    canon = {p.lower(): p for p in ORDER}
    imports = {}
    for p in ORDER:
        pk = ucpkg.Package(os.path.join(shipped, p + '.u'))
        imports[p] = {canon[im['name'].lower()] for im in pk.imports
                      if im['class_name'] == 'Package' and im['package'] == 0 and im['name'].lower() in canon} - {p}
    need = set(targets)
    changed = True
    while changed:
        changed = False
        for t in sorted(need, key=ORDER.index):
            for p in ORDER[:ORDER.index(t)]:
                if p in need:
                    continue
                if any(ORDER.index(q) >= ORDER.index(t) for q in imports[p]):
                    need.add(p)
                    changed = True
    return [p for p in ORDER if p in need]


# --- main ----------------------------------------------------------------------------------------------------------
def opt(args, name, default=None):
    if name in args:
        i = args.index(name)
        if i + 1 >= len(args):
            raise PatchError('%s needs a value' % name)
        v = args[i + 1]
        del args[i:i + 2]
        return v
    return default


def flag(args, name):
    if name in args:
        args.remove(name)
        return True
    return False


def main():
    a = sys.argv[1:]
    if not a or a[0] in ('-h', '--help'):
        print(__doc__)
        return 0 if a else 2
    cmd = a.pop(0)
    pdir = opt(a, '--patches', PATCHES)
    if cmd == 'make':
        base, changed, pk = opt(a, '--base'), opt(a, '--changed'), opt(a, '--packages')
        if len(a) != 1 or not base or not changed:
            raise PatchError('usage: make <set> --base <tree> --changed <tree> [--packages A,B]')
        cmd_make(a[0], base, changed, pk.split(',') if pk else None, pdir)
        return 0
    if cmd == 'sets':
        sl = opt(a, '--sets')
        dflt = flag(a, '--default')
        print(','.join(resolve_sets(pdir, sl.split(',') if sl else [], dflt or not sl)))
        return 0
    if cmd in ('apply', 'check', 'packages'):
        base, out, sl = opt(a, '--base'), opt(a, '--out'), opt(a, '--sets')
        dflt = flag(a, '--default')
        compile_from = opt(a, '--compile-list')
        with_epic = flag(a, '--with-epic-only')
        if a:
            raise PatchError('unexpected arguments %s' % a)
        if bool(sl) == dflt:
            raise PatchError('give either --sets a,b,... or --default')
        sets = resolve_sets(pdir, sl.split(',') if sl else [], dflt)
        if cmd == 'packages':
            table = {s[0]: s for s in read_sets(pdir)}
            pk = set()
            for s in sets:
                if table[s][3] == 'ours' or with_epic:
                    pk |= set(set_packages(pdir, s))
            if compile_from:
                for p in compile_list(compile_from, pk):
                    print(p if p in pk else p + ' (compiled to build the others, not kept)')
                return 0
            for p in ORDER:
                if p in pk:
                    print(p)
            return 0
        if not base:
            raise PatchError('--base is required')
        if cmd == 'apply':
            if not out:
                raise PatchError('--out is required')
            cmd_apply(base, out, sets, pdir)
        else:
            with tempfile.TemporaryDirectory(prefix='uc-patches-check-') as t:
                cmd_apply(base, os.path.join(t, 'tree'), sets, pdir)
            print('check: %d sets apply cleanly' % len(sets))
        return 0
    if cmd == 'compare':
        sa, sb, rp = opt(a, '--system-a'), opt(a, '--system-b'), opt(a, '--report')
        if len(a) != 2:
            raise PatchError('usage: compare <a.u> <b.u> [--report <file>]')
        return cmd_compare(a[0], a[1], sa, sb, rp)
    if cmd == 'rebase':
        return cmd_rebase(a, pdir)
    raise PatchError('unknown command %s' % cmd)


def name_norm(t):
    """A line with names lower-cased (outside string literals): an FName keeps the case of its first creation,
    which depends on what was loaded before, so a package compiled here may spell `T_UI_2` as `t_ui_2`."""
    return re.sub(r'"(?:[^"\\]|\\.)*"|[^"]+', lambda m: m.group(0) if m.group(0).startswith('"') else m.group(0).lower(), t)


def same_case(prev, stage):
    """stage, with every line that differs from prev's line only in the case of names put back as prev has it
    (rebase: a stage is decompiled from a package compiled here; the base from the shipped one)."""
    out = []
    sm = difflib.SequenceMatcher(None, prev, stage, autojunk=False)
    for tag, a1, a2, b1, b2 in sm.get_opcodes():
        if tag == 'replace' and a2 - a1 == b2 - b1:
            out += [x if name_norm(x) == name_norm(y) else y for x, y in zip(prev[a1:a2], stage[b1:b2])]
        else:
            out += stage[b1:b2]
    return out


def cmd_rebase(a, pdir):
    """rebase --new-base <tree> --stages <dir> --sets a,b,...

    When uc-decompile changes, the base text changes and every set must be re-made against it. <stages>/<k>/ holds,
    for each set k of the list (in order), the NEW decompiler's text of the packages compiled from the OLD base +
    sets 1..k (tools/scripts/uc-patches-proof.sh rebase builds and decompiles them). Each set is re-made as the
    difference between consecutive stages, the first against new-base. Only classes the new base has are taken
    from a stage: classes a set adds (new/) are our own text and are carried over as they are. The proof that
    nothing moved is the build (the proof script compares the new chain's build with the old chain's)."""
    new, stages, sl = opt(a, '--new-base'), opt(a, '--stages'), opt(a, '--sets')
    if a or not (new and stages and sl):
        raise PatchError('usage: rebase --new-base <tree> --stages <dir> --sets a,b,...')
    sets = resolve_sets(pdir, sl.split(','), False)
    base_classes = set(tree_files(new))
    work = tempfile.mkdtemp(prefix='uc-rebase-')
    try:
        prev = new
        for n, s in enumerate(sets):
            st = os.path.join(stages, s.replace('/', '_'))
            _, newcls = set_files(pdir, s)
            keep = {rel: read_lines(p) for rel, p in newcls.items()}
            cur = os.path.join(work, '%02d' % n)
            shutil.copytree(prev, cur)
            for rel, p in tree_files(st).items():
                if rel in base_classes and os.path.isdir(os.path.join(cur, rel.split('/')[0])):
                    write_lines(os.path.join(cur, rel), same_case(read_lines(os.path.join(cur, rel)), read_lines(p)))
            for rel, lines in keep.items():
                write_lines(os.path.join(cur, rel), lines)
            # only the packages of the new base are re-made; a set's diffs for other packages are kept
            cmd_make(s, prev, cur, sorted({r.split('/')[0] for r in base_classes}), pdir)
            prev = cur
    finally:
        shutil.rmtree(work)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except PatchError as e:
        sys.stderr.write('uc-patches: %s\n' % e)
        sys.exit(1)
