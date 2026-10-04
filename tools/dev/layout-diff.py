#!/usr/bin/env python3
"""Compare C++ record layouts of the engine between i386 (the reference) and wasm32.

    source <WorkUC2>/env.sh
    tools/dev/layout-diff.py build-wasm [-j 8] [--filter REGEX]

Every translation unit of the Emscripten build (its compile_commands.json) is parsed twice with the
Emscripten clang, once for wasm32 (its own command line) and once with --target=i386-pc-linux-gnu (same
defines and include paths), with -fdump-record-layouts-complete. Every engine record (class/struct/union)
whose size, alignment or direct-member offsets differ between the two is printed, with the first differing
members. System records (libc, SDL, std::) are skipped.

Why: the original projects compile with /Zp4 and the script packages were laid out to match; the native
i386 build is proven against them (`ucc-bin verifyclasses`), and i386 never aligns a member above 4.
wasm32 aligns DOUBLE/QWORD/SQWORD to 8, so a record differs exactly where an 8-byte member is not under
`#pragma pack(4)`. verifyclasses checks native CLASSES only; this also finds script STRUCTS (array
element strides) and anything else. TUs that include SDL.h do not parse for i386 here (no i386 SDL
configuration); they are listed as not compared (their engine records are compared in the other TUs).
Notebook: docs/porting/analysis/web-smoke.md (W0).
"""
import argparse, json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

SYSTEM = re.compile(
    r'^(std::|__|_|wasi|emscripten|pthread|max_align|SDL_|Mix_|\((unnamed|anonymous) at (/usr|.*/emsdk/))'
    r'|^(cmsghdr|dirent|f_owner_ex|file_handle|flock|in6_addr|itimerspec|itimerval|mallinfo|msghdr|passwd|'
    r'sched_param|sigaction|sigevent|sockaddr\w*|stat|statx\w*|timespec|timeval|tm|rlimit|rusage|utsname|'
    r'ip_mreq\w*|group_\w+|hostent|servent|protoent|netent|addrinfo|linger|iovec|ifreq|ifconf|pollfd|'
    r'termios|winsize|utimbuf|lconv|timezone|ucred|mmsghdr|in_addr|in_pktinfo|ip_opts|sigstack|sigaltstack|'
    r'random_data|drand48_data|ip_msfilter|group_filter|ipv6_mreq|in6_pktinfo|ip6_mtuinfo|ntptimeval|timex|'
    r'tms|\w+_t)$')


def parse(text):
    records = {}
    for chunk in text.split('*** Dumping AST Record Layout')[1:]:
        lines = chunk.strip('\n').split('\n')
        m = re.match(r'\s*0 \| (?:class|struct|union) (.*)', lines[0])
        size = re.search(r'sizeof=(\d+)', chunk)
        align = re.search(r'\balign=(\d+)', chunk)
        if not m or not size or SYSTEM.match(m.group(1)):
            continue
        body = []
        for line in lines[1:]:
            mm = re.match(r'\s*(\d+)(?::[\d-]+)? \|( +)(.*)', line)
            if mm and len(mm.group(2)) == 3:      # direct members and bases only
                body.append((int(mm.group(1)), mm.group(3).strip()))
        records[m.group(1)] = (int(size.group(1)), int(align.group(1)), body)
    return records


def dump(entry, emsdk_bin):
    args = shlex.split(entry['command'])
    keep, skip = [], False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ('-o', '-c'):
            skip = True
            continue
        if a.startswith('-O') or a == '-g':
            continue
        if a.startswith('@'):
            keep += shlex.split(open(os.path.join(entry['directory'], a[1:])).read())
            continue
        keep.append(a)
    flags = ['-fsyntax-only', '-w', '-Xclang', '-fdump-record-layouts-complete', entry['file']]
    run = lambda cmd: subprocess.run(cmd, cwd=entry['directory'], capture_output=True, text=True,
                                     errors='replace').stdout
    wasm = run([args[0]] + keep + flags)
    i386_args = [a for a in keep if not a.startswith('-s') and a != '-pthread']
    i386 = run([os.path.join(emsdk_bin, 'clang++'), '--target=i386-pc-linux-gnu'] + i386_args + flags)
    return entry['file'], parse(i386), parse(wasm)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('build_dir')
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--filter', default='/Source/', help='only TUs whose path matches (default: /Source/)')
    o = ap.parse_args()
    emsdk = os.environ.get('EMSDK')
    if not emsdk:
        sys.exit('source <WorkUC2>/env.sh first (EMSDK unset)')
    entries = [e for e in json.load(open(os.path.join(o.build_dir, 'compile_commands.json')))
               if re.search(o.filter, e['file']) and '/Thirdparty/' not in e['file']]
    diffs, broken, compared = {}, set(), set()
    with ThreadPoolExecutor(o.j) as pool:
        for src, a, b in pool.map(lambda e: dump(e, os.path.join(emsdk, 'upstream', 'bin')), entries):
            if a.get('FString') != b.get('FString'):    # the i386 parse failed part-way (SDL.h)
                broken.add(os.path.relpath(src))
                continue
            for k in set(a) & set(b):
                compared.add(k)
                if a[k] != b[k]:
                    diffs.setdefault(k, (a[k], b[k], src))
    print(f'{len(entries)} translation units, {len(compared)} engine records compared, '
          f'{len(diffs)} differ between i386 and wasm32')
    for k, (x, y, src) in sorted(diffs.items()):
        print(f'{k}: i386 sizeof={x[0]} align={x[1]} | wasm32 sizeof={y[0]} align={y[1]}  ({os.path.relpath(src)})')
        for p, q in [(p, q) for p, q in zip(x[2], y[2]) if p != q][:3]:
            print(f'    {p[1][:64]:64} i386 @{p[0]:<5} wasm32 @{q[0]}')
    if broken:
        print(f'not compared (the i386 parse could not complete them): {", ".join(sorted(broken))}')


if __name__ == '__main__':
    main()
