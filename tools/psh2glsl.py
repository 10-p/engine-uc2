#!/usr/bin/env python3
"""psh2glsl.py - Xbox register-combiner pixel shaders (.psh, xps.1.1 / ps.1.1) to GLSL.

Three things in one tool, all working on Epic's sources in Source/ShaderData/PixelShaders
(read-only reference, never edited):

  1. a translator: .psh text -> GLSL 1.20 fragment shader body that is also valid GLSL ES 1.00
     (no #version line, no fixed-function built-ins; the consumer prepends the header);
  2. a CPU reference interpreter of the same instruction set ("path A", works on the parsed text);
  3. an independent evaluator of the compiled Xbox pixel-shader definition words in the .h files
     next to the sources ("path B": D3DPIXELSHADERDEF as documented in the XDK's d3d8types.h).

  --selftest        headless: for every shader, path A against path B on generated inputs
  --emit            write Source/UC2GLDrv/Shaders/*.glsl, the EPixelShader table and the embedded sources
  --check-emitted   fail if --emit would change any checked-in file
  --gputest         render every generated GLSL shader with a real GL context (run it under Xvfb)
                    and compare with path A
  --dump NAME       show the parsed text, the decoded definition words and the GLSL of one shader

Semantics and their sources are documented in docs/porting/analysis/shaders.md.
"""

import argparse
import os
import random
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PSH_DIR = os.path.join(REPO, 'Source', 'ShaderData', 'PixelShaders')
OUT_DIR = os.path.join(REPO, 'Source', 'UC2GLDrv', 'Shaders')
RENDEV_CPP = os.path.join(REPO, 'Source', 'D3DDrv', 'Src', 'D3DRenderDevice.cpp')
ENGINE_CLASSES_H = os.path.join(REPO, 'Source', 'Engine', 'Inc', 'EngineClasses.h')
GPU_SRC = os.path.join(REPO, 'tools', 'pshgpu', 'pshgpu.c')

TEX_SIZE = 16           # test textures are TEX_SIZE x TEX_SIZE, nearest filtered, clamped
GPU_TOLERANCE = 1.0     # in 8-bit steps of the framebuffer


class PshError(Exception):
    pass


# =====================================================================================
# Parsing the .psh text
# =====================================================================================

class Src:
    """One source argument: register, channel selector and range modifier."""
    __slots__ = ('reg', 'chan', 'mod', 'text')

    def __init__(self, reg, chan, mod, text):
        self.reg, self.chan, self.mod, self.text = reg, chan, mod, text


class Instr:
    __slots__ = ('op', 'omod', 'dests', 'srcs', 'text', 'line')

    def __init__(self, op, omod, dests, srcs, text, line):
        self.op, self.omod, self.dests, self.srcs, self.text, self.line = op, omod, dests, srcs, text, line


class Shader:
    def __init__(self, name):
        self.name = name
        self.version = None
        self.texops = [None, None, None, None]   # (op, srcstage, srcmod, text)
        self.stages = []                         # [{'rgb': Instr|None, 'a': Instr|None}]
        self.xfc = None                          # [Src]*7 or None
        self.xfc_text = None
        self.warnings = []


SRC_RE = re.compile(r'^(1-|-)?([a-z]+[0-9]*)(_sat|_bx2|_bias)?(?:\.(rgb|a|b))?$')
DEST_RE = re.compile(r'^([a-z]+[0-9]*)(?:\.(rgb|a))?$')
READABLE = {'r0', 'r1', 't0', 't1', 't2', 't3', 'v0', 'v1', 'fog', 'zero'} | {'c%d' % i for i in range(8)}
WRITABLE = {'r0', 'r1', 't0', 't1', 't2', 't3', 'v0', 'v1', 'discard'}
NSRC = {'mov': 1, 'add': 2, 'sub': 2, 'mul': 2, 'mad': 3, 'lrp': 3, 'dp3': 2, 'xmma': 4, 'xdd': 4}
NDEST = {'xmma': 3, 'xdd': 2}
OMODS = ('x2', 'x4', 'd2', 'bias', 'bx2')


def parse_src(tok, final=False):
    t = tok.replace(' ', '').replace('\t', '').lower()
    m = SRC_RE.match(t)
    if not m:
        raise PshError('bad source argument "%s"' % tok)
    pre, reg, suf, chan = m.groups()
    if reg == 'discard':
        reg = 'zero'        # xdd/xmma take "discard" for an unused source: it reads as zero
    ok = READABLE | ({'prod', 'sum'} if final else set())
    if reg not in ok:
        raise PshError('unknown source register "%s"' % tok)
    if pre == '1-':
        if suf:
            raise PshError('cannot combine 1- with %s in "%s"' % (suf, tok))
        mod = 'inv'
    elif pre == '-':
        mod = {None: 'neg', '_bx2': 'negbx2', '_bias': 'negbias'}.get(suf)
        if mod is None:
            raise PshError('cannot negate "%s"' % tok)
    else:
        mod = {None: '', '_sat': 'sat', '_bx2': 'bx2', '_bias': 'bias'}[suf]
    return Src(reg, chan, mod, t)


def parse_psh(name, text):
    sh = Shader(name)
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    for lineno, raw in enumerate(text.split('\n'), 1):
        line = re.sub(r'//.*', '', raw)
        line = re.sub(r';.*', '', line).strip()
        if not line:
            continue
        low = line.lower()
        if low in ('xps.1.1', 'ps.1.1', 'ps.1.0', 'xps.1.0'):
            sh.version = low
            continue
        paired = low.startswith('+')
        if paired:
            low = low[1:].strip()
        parts = low.split(None, 1)
        opfull = parts[0]
        args = [a.strip() for a in parts[1].split(',')] if len(parts) > 1 else []
        pretty = re.sub(r'\s+', ' ', line)

        if opfull in ('tex', 'texcoord', 'texkill', 'texm3x2pad', 'texm3x2tex', 'texreg2gb', 'texreg2ar'):
            if sh.stages or sh.xfc:
                raise PshError('%s:%d: texture instruction after a combiner instruction' % (name, lineno))
            m = re.match(r'^t([0-3])$', args[0])
            if not m:
                raise PshError('%s:%d: bad texture register' % (name, lineno))
            stage = int(m.group(1))
            src, srcmod = None, ''
            if opfull in ('texm3x2pad', 'texm3x2tex', 'texreg2gb', 'texreg2ar'):
                m2 = re.match(r'^t([0-3])(_bx2)?$', args[1])
                if not m2:
                    raise PshError('%s:%d: bad texture source' % (name, lineno))
                src = int(m2.group(1))
                srcmod = 'bx2' if m2.group(2) else ''
                if src >= stage:
                    raise PshError('%s:%d: texture source must be an earlier stage' % (name, lineno))
            if sh.texops[stage] is not None:
                raise PshError('%s:%d: stage %d declared twice' % (name, lineno, stage))
            sh.texops[stage] = (opfull, src, srcmod, pretty)
            continue

        if opfull == 'xfc':
            if len(args) != 7:
                raise PshError('%s:%d: xfc takes 7 arguments' % (name, lineno))
            sh.xfc = [parse_src(a, final=True) for a in args]
            sh.xfc_text = pretty
            continue

        op, omod = opfull, ''
        for om in OMODS:
            if opfull.endswith('_' + om):
                op, omod = opfull[:-len(om) - 1], om
        if op not in NSRC:
            raise PshError('%s:%d: unknown instruction "%s"' % (name, lineno, opfull))
        nd = NDEST.get(op, 1)
        if len(args) != nd + NSRC[op]:
            raise PshError('%s:%d: %s takes %d arguments' % (name, lineno, op, nd + NSRC[op]))
        dests = []
        for a in args[:nd]:
            m = DEST_RE.match(a.replace(' ', ''))
            if not m or m.group(1) not in WRITABLE:
                raise PshError('%s:%d: bad destination "%s"' % (name, lineno, a))
            dests.append((m.group(1), m.group(2)))
        masks = set(d[1] for d in dests)
        if len(masks) != 1:
            raise PshError('%s:%d: mixed write masks' % (name, lineno))
        mask = masks.pop()
        ins = Instr(op, omod, [d[0] for d in dests], [parse_src(a) for a in args[nd:]], pretty, lineno)
        if sh.xfc:
            raise PshError('%s:%d: instruction after xfc' % (name, lineno))
        if paired:
            if not sh.stages:
                raise PshError('%s:%d: + without a previous instruction' % (name, lineno))
            st = sh.stages[-1]
            part = 'rgb' if mask == 'rgb' else 'a' if mask == 'a' else None
            if part is None or st[part] is not None:
                raise PshError('%s:%d: + pairing needs one .rgb and one .a instruction' % (name, lineno))
            st[part] = ins
        else:
            st = {'rgb': None, 'a': None}
            if mask in (None, 'rgb'):
                st['rgb'] = ins
            if mask in (None, 'a'):
                st['a'] = ins
            sh.stages.append(st)
    if sh.version is None:
        raise PshError('%s: no version line' % name)
    if len(sh.stages) > 8:
        raise PshError('%s: more than 8 combiner stages' % name)
    lint(sh)
    return sh


def lint(sh):
    """Reads of registers that nothing has written, as the hardware would see them."""
    written = {'v0', 'v1', 'fog', 'zero'} | {'c%d' % i for i in range(8)}
    for i, t in enumerate(sh.texops):
        if t is not None and t[0] in ('tex', 'texcoord', 'texm3x2tex', 'texreg2gb', 'texreg2ar'):
            written.add('t%d' % i)
    have = {}   # reg -> set of parts
    for r in written:
        have[r] = {'rgb', 'a'}
    if 't0' in written:
        have.setdefault('r0', set()).add('a')   # R0_ALPHA is initialised to T0_ALPHA (d3d8types.h)

    def check(src, part, where):
        need = 'a' if (src.chan == 'a' or (part == 'a' and src.chan != 'b' and src.chan != 'rgb')) else 'rgb'
        if src.reg in ('prod', 'sum'):
            return
        if need not in have.get(src.reg, set()):
            sh.warnings.append('%s reads %s.%s before anything wrote it' % (where, src.reg, need))

    for n, st in enumerate(sh.stages):
        for part in ('rgb', 'a'):
            ins = st[part]
            if ins and not (part == 'a' and ins is st['rgb'] and ins.op in ('dp3', 'xdd')):
                for s in ins.srcs:
                    check(s, part, 'stage %d (%s)' % (n, ins.text))
        for part in ('rgb', 'a'):
            ins = st[part]
            if ins:
                for d in ins.dests:
                    if d != 'discard':
                        have.setdefault(d, set()).add(part)
    if sh.xfc:
        for k, s in enumerate(sh.xfc):
            check(s, 'a' if k == 6 else 'rgb', 'xfc')
    else:
        for need in ('rgb', 'a'):
            if need not in have.get('r0', set()):
                sh.warnings.append('result r0.%s is never written' % need)


# =====================================================================================
# Shared numeric helpers and the test environment
# =====================================================================================

def clamp(x, lo, hi):
    return lo if x < lo else hi if x > hi else x


class Env:
    """Everything a pixel shader can read for one fragment."""

    def __init__(self):
        self.tc = [[0.0, 0.0, 0.0, 1.0] for _ in range(4)]      # s,t,r,q per stage
        self.textures = [None] * 4                               # TEX_SIZE*TEX_SIZE rgba tuples (0..255)
        self.v0 = [0.0] * 4
        self.v1 = [0.0] * 4
        self.c = [[0.0] * 4 for _ in range(8)]
        self.fog_color = [0.0, 0.0, 0.0]
        self.fog = 1.0
        self.specular_enable = False

    def texel_index(self, u, v):
        x = clamp(int(_floor(u * TEX_SIZE)), 0, TEX_SIZE - 1)
        y = clamp(int(_floor(v * TEX_SIZE)), 0, TEX_SIZE - 1)
        return y * TEX_SIZE + x

    def sample(self, stage, u, v):
        px = self.textures[stage][self.texel_index(u, v)]
        return [px[0] / 255.0, px[1] / 255.0, px[2] / 255.0, px[3] / 255.0]


def _floor(x):
    i = int(x)
    return i - 1 if x < i else i


def near_texel_edge(u):
    f = u * TEX_SIZE
    if f <= 0.0 or f >= TEX_SIZE:
        return False        # clamped: stable
    d = abs(f - round(f))
    return d < 0.02


def out_map(x, omod):
    if omod == 'bias':
        x = x - 0.5
    elif omod == 'x2':
        x = x * 2.0
    elif omod == 'bx2':
        x = (x - 0.5) * 2.0
    elif omod == 'x4':
        x = x * 4.0
    elif omod == 'd2':
        x = x * 0.5
    return clamp(x, -1.0, 1.0)


def in_map(x, mod):
    if mod == '':
        return x
    if mod == 'neg':
        return -x
    p = x if x > 0.0 else 0.0
    if mod == 'sat':
        return p
    if mod == 'inv':
        return 1.0 - p
    if mod == 'bx2':
        return 2.0 * p - 1.0
    if mod == 'negbx2':
        return 1.0 - 2.0 * p
    if mod == 'bias':
        return p - 0.5
    if mod == 'negbias':
        return 0.5 - p
    raise PshError('modifier ' + mod)


# =====================================================================================
# Path A: interpreter of the parsed text
# =====================================================================================

def eval_textures(env, modes):
    """modes: per stage (op, src, srcmod). Returns (t registers, killed, case is numerically fragile)."""
    t = [[0.0] * 4 for _ in range(4)]
    killed = False
    fragile = False
    dots = [0.0] * 4
    for i in range(4):
        m = modes[i]
        if m is None:
            continue
        op, src, srcmod = m[0], m[1], m[2]
        s, tt, r, q = env.tc[i]
        if op == 'tex':
            u, v = s / q, tt / q
            fragile |= near_texel_edge(u) or near_texel_edge(v)
            t[i] = env.sample(i, u, v)
        elif op == 'texcoord':
            t[i] = [clamp(s, 0.0, 1.0), clamp(tt, 0.0, 1.0), clamp(r, 0.0, 1.0), clamp(q, 0.0, 1.0)]
        elif op == 'texkill':
            if s < 0.0 or tt < 0.0 or r < 0.0 or q < 0.0:
                killed = True
        elif op in ('texm3x2pad', 'texm3x2tex'):
            a = t[src][:3]
            if srcmod == 'bx2':
                a = [2.0 * (x - 0.5) for x in a]
            dots[i] = s * a[0] + tt * a[1] + r * a[2]
            if op == 'texm3x2tex':
                u, v = dots[i - 1], dots[i]
                fragile |= near_texel_edge(u) or near_texel_edge(v)
                t[i] = env.sample(i, u, v)
        elif op == 'texreg2gb':
            u, v = t[src][1], t[src][2]
            fragile |= near_texel_edge(u) or near_texel_edge(v)
            t[i] = env.sample(i, u, v)
        elif op == 'texreg2ar':
            u, v = t[src][3], t[src][0]
            fragile |= near_texel_edge(u) or near_texel_edge(v)
            t[i] = env.sample(i, u, v)
        else:
            raise PshError('texture op ' + op)
    return t, killed, fragile


def a_src_rgb(regs, s):
    v = regs[s.reg]
    if s.chan == 'a':
        base = [v[3]] * 3
    elif s.chan == 'b':
        base = [v[2]] * 3
    else:
        base = v[:3]
    return [in_map(x, s.mod) for x in base]


def a_src_alpha(regs, s):
    v = regs[s.reg]
    # In the alpha half of a combiner a register is read as its alpha unless blue is asked for.
    # An explicit .rgb selector in an unmasked instruction reads blue there (verified against the
    # assembler output, see the notebook).
    x = v[2] if s.chan in ('b', 'rgb') else v[3]
    return in_map(x, s.mod)


def a_portion(regs, ins, part):
    """Returns the list of (dest, value) writes of one half (rgb or alpha) of a stage."""
    if part == 'rgb':
        S = [a_src_rgb(regs, s) for s in ins.srcs]
        n = 3
    else:
        S = [[a_src_alpha(regs, s)] for s in ins.srcs]
        n = 1
    op = ins.op

    def mul(a, b):
        return [a[i] * b[i] for i in range(n)]

    def add(a, b):
        return [a[i] + b[i] for i in range(n)]

    def dot(a, b):
        if part == 'a':
            raise PshError('%s: a dot product has no alpha half' % ins.text)
        d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
        return [d, d, d]

    if op == 'mov':
        outs = [S[0]]
    elif op == 'add':
        outs = [add(S[0], S[1])]
    elif op == 'sub':
        outs = [[S[0][i] - S[1][i] for i in range(n)]]
    elif op == 'mul':
        outs = [mul(S[0], S[1])]
    elif op == 'mad':
        outs = [add(mul(S[0], S[1]), S[2])]
    elif op == 'lrp':
        # s0*s1 + (1-s0)*s2; the complement is the combiner's unsigned-invert mapping of s0
        s0 = ins.srcs[0]
        if part == 'rgb':
            raw = a_src_rgb(regs, Src(s0.reg, s0.chan, '', s0.text))
        else:
            raw = [a_src_alpha(regs, Src(s0.reg, s0.chan, '', s0.text))]
        if s0.mod == '':
            comp = [1.0 - max(x, 0.0) for x in raw]
        elif s0.mod == 'inv':
            comp = [max(x, 0.0) for x in raw]
        else:
            raise PshError('%s: lrp factor modifier' % ins.text)
        outs = [add(mul(S[0], S[1]), mul(comp, S[2]))]
    elif op == 'dp3':
        outs = [dot(S[0], S[1])]
    elif op == 'xmma':
        ab, cd = mul(S[0], S[1]), mul(S[2], S[3])
        outs = [ab, cd, add(ab, cd)]
    elif op == 'xdd':
        outs = [dot(S[0], S[1]), dot(S[2], S[3])]
    else:
        raise PshError(op)
    res = []
    for d, val in zip(ins.dests, outs):
        if d != 'discard':
            res.append((d, [out_map(x, ins.omod) for x in val]))
    return res


def has_alpha_half(ins):
    """The alpha combiner cannot do a dot product: an unmasked dp3/xdd runs in the rgb half only
    and the assembler asks for the result's blue to be copied to the destination's alpha
    (PS_COMBINEROUTPUT_AB/CD_BLUE_TO_ALPHA), which gives ps.1.1's "replicate to all four"."""
    return ins.op not in ('dp3', 'xdd')


def dot_to_alpha(st):
    ins = st['rgb']
    return ins is not None and ins is st['a'] and ins.op in ('dp3', 'xdd')


def a_final(regs, env, xfc, specular_enable):
    def rgb(s):
        if s.reg == 'prod':
            return None
        if s.reg == 'sum':
            a, b = regs['r0'], regs['v1']
            # r0 and v1 are clamped to [0,1] before the sum; the sum itself is clamped because
            # _sat is the default modifier of every xfc input (XDK "xfc" page) - the assembler sets
            # PS_FINALCOMBINERSETTING_CLAMP_SUM in every definition it writes.
            return [clamp(clamp(a[i], 0.0, 1.0) + clamp(b[i], 0.0, 1.0), 0.0, 1.0) for i in range(3)]
        v = regs[s.reg]
        base = [v[3]] * 3 if s.chan == 'a' else [v[2]] * 3 if s.chan == 'b' else v[:3]
        base = [clamp(x, 0.0, 1.0) for x in base]
        if s.mod == 'inv':
            base = [1.0 - x for x in base]
        elif s.mod not in ('', 'sat'):
            raise PshError('xfc modifier')
        return base

    if xfc is None:
        # No xfc: D3D programs the final combiner from the fog and specular render states
        # (XDK "xfc" page): colour = lerp(fog colour, r0 [+ v1], fog factor), alpha = r0.a.
        r0 = regs['r0']
        col = [clamp(r0[i], 0.0, 1.0) for i in range(3)]
        if specular_enable:
            col = [clamp(col[i] + clamp(regs['v1'][i], 0.0, 1.0), 0.0, 1.0) for i in range(3)]
        f = clamp(regs['fog'][3], 0.0, 1.0)
        out = [f * col[i] + (1.0 - f) * regs['fog'][i] for i in range(3)]
        return [clamp(x, 0.0, 1.0) for x in out] + [clamp(r0[3], 0.0, 1.0)]

    e, f = rgb(xfc[4]), rgb(xfc[5])
    prod = [e[i] * f[i] for i in range(3)]
    vals = []
    for s in xfc[:4]:
        v = rgb(s)
        vals.append(prod if v is None else v)
    A, B, C, D = vals
    out = [clamp(A[i] * B[i] + (1.0 - A[i]) * C[i] + D[i], 0.0, 1.0) for i in range(3)]
    g = xfc[6]
    gv = regs[g.reg]
    ga = clamp(gv[3] if g.chan == 'a' else gv[2], 0.0, 1.0)
    if g.mod == 'inv':
        ga = 1.0 - ga
    return out + [ga]


def run_text(sh, env):
    """Path A. Returns (rgba in 0..1 or None when the fragment is killed, fragile)."""
    t, killed, fragile = eval_textures(env, sh.texops)
    regs = {'zero': [0.0] * 4,
            'fog': list(env.fog_color) + [clamp(env.fog, 0.0, 1.0)],
            'v0': [clamp(x, 0.0, 1.0) for x in env.v0],
            'v1': [clamp(x, 0.0, 1.0) for x in env.v1],
            'r0': [0.0, 0.0, 0.0, t[0][3]],
            'r1': [0.0] * 4}
    for i in range(4):
        regs['t%d' % i] = t[i]
    for i in range(8):
        regs['c%d' % i] = [clamp(x, 0.0, 1.0) for x in env.c[i]]
    for st in sh.stages:
        writes = []
        if st['rgb']:
            for d, val in a_portion(regs, st['rgb'], 'rgb'):
                writes.append((d, 'rgb', val))
                if dot_to_alpha(st):
                    writes.append((d, 'a', [val[2]]))
        if st['a'] and (st['a'] is not st['rgb'] or has_alpha_half(st['a'])):
            for d, val in a_portion(regs, st['a'], 'a'):
                writes.append((d, 'a', val))
        for d, part, val in writes:
            cur = list(regs[d])
            if part == 'rgb':
                cur[0:3] = val
            else:
                cur[3] = val[0]
            regs[d] = cur
    out = a_final(regs, env, sh.xfc, env.specular_enable)
    return (None if killed else out), fragile


# =====================================================================================
# Path B: the compiled definition words (D3DPIXELSHADERDEF, XDK d3d8types.h)
# =====================================================================================

DEF_FIELDS = [('PSAlphaInputs', 8), ('PSFinalCombinerInputsABCD', 1), ('PSFinalCombinerInputsEFG', 1),
              ('PSConstant0', 8), ('PSConstant1', 8), ('PSAlphaOutputs', 8), ('PSRGBInputs', 8),
              ('PSCompareMode', 1), ('PSFinalCombinerConstant0', 1), ('PSFinalCombinerConstant1', 1),
              ('PSRGBOutputs', 8), ('PSCombinerCount', 1), ('PSTextureModes', 1), ('PSDotMapping', 1),
              ('PSInputTexture', 1), ('PSC0Mapping', 1), ('PSC1Mapping', 1), ('PSFinalCombinerConstants', 1)]

REGNAMES = {0: 'zero', 1: 'C0', 2: 'C1', 3: 'fog', 4: 'v0', 5: 'v1', 8: 't0', 9: 't1', 10: 't2', 11: 't3',
            12: 'r0', 13: 'r1', 14: 'sum', 15: 'prod'}
MAPNAMES = {0x00: 'sat', 0x20: 'inv', 0x40: 'bx2', 0x60: 'negbx2', 0x80: 'bias', 0xa0: 'negbias',
            0xc0: '', 0xe0: 'neg'}
TEXMODES = {0: None, 1: 'PROJECT2D', 2: 'PROJECT3D', 3: 'CUBEMAP', 4: 'PASSTHRU', 5: 'CLIPPLANE',
            6: 'BUMPENVMAP', 7: 'BUMPENVMAP_LUM', 8: 'BRDF', 9: 'DOT_ST', 10: 'DOT_ZW', 11: 'DOT_RFLCT_DIFF',
            12: 'DOT_RFLCT_SPEC', 13: 'DOT_STR_3D', 14: 'DOT_STR_CUBE', 15: 'DPNDNT_AR', 16: 'DPNDNT_GB',
            17: 'DOTPRODUCT', 18: 'DOT_RFLCT_SPEC_CONST'}


def load_def(path):
    text = open(path, encoding='latin-1').read()
    words = [int(w, 16) for w in re.findall(r'0x([0-9a-fA-F]{8})', text)]
    if len(words) != 60:
        raise PshError('%s: expected 60 definition words, found %d' % (path, len(words)))
    d = {}
    pos = 0
    for name, n in DEF_FIELDS:
        d[name] = words[pos] if n == 1 else words[pos:pos + n]
        pos += n
    return d


def b_input(regs, byte, alpha_half, c0, c1):
    reg = byte & 0x0f
    chan_alpha = bool(byte & 0x10)
    mod = MAPNAMES[byte & 0xe0]
    name = REGNAMES.get(reg)
    if name is None:
        raise PshError('definition uses reserved register %d' % reg)
    if name == 'C0':
        v = c0
    elif name == 'C1':
        v = c1
    else:
        v = regs[name]
    if v is None:
        raise PshError('definition reads a constant with no mapping')
    if alpha_half:
        return [in_map(v[3] if chan_alpha else v[2], mod)]
    base = [v[3]] * 3 if chan_alpha else v[:3]
    return [in_map(x, mod) for x in base]


def b_omod(flags):
    f = flags & 0x38
    return {0x00: '', 0x08: 'bias', 0x10: 'x2', 0x18: 'bx2', 0x20: 'x4', 0x30: 'd2'}[f]


def b_texmodes(d):
    modes = []
    tm = d['PSTextureModes']
    it = d['PSInputTexture']
    dm = d['PSDotMapping']
    srcs = [None, 0, (it >> 16) & 1, (it >> 20) & 3]
    dotmaps = [0, dm & 7, (dm >> 4) & 7, (dm >> 8) & 7]
    for i in range(4):
        mode = TEXMODES[(tm >> (5 * i)) & 0x1f]
        if mode is None:
            modes.append(None)
        elif mode == 'PROJECT2D':
            modes.append(('tex', None, ''))
        elif mode == 'PASSTHRU':
            modes.append(('texcoord', None, ''))
        elif mode == 'CLIPPLANE':
            if (d['PSCompareMode'] >> (4 * i)) & 0xf:
                raise PshError('compare mode other than "less than zero"')
            modes.append(('texkill', None, ''))
        elif mode in ('DOTPRODUCT', 'DOT_ST'):
            if dotmaps[i] not in (0, 1):
                raise PshError('dot mapping %d' % dotmaps[i])
            modes.append(('texm3x2pad' if mode == 'DOTPRODUCT' else 'texm3x2tex', srcs[i],
                          'bx2' if dotmaps[i] == 1 else ''))
        elif mode == 'DPNDNT_GB':
            modes.append(('texreg2gb', srcs[i], ''))
        elif mode == 'DPNDNT_AR':
            modes.append(('texreg2ar', srcs[i], ''))
        else:
            raise PshError('texture mode %s is not implemented' % mode)
    return modes


def run_def(d, env):
    """Path B: evaluate the definition words as the register combiner hardware is described."""
    t, killed, fragile = eval_textures(env, b_texmodes(d))
    cc = [[clamp(x, 0.0, 1.0) for x in env.c[i]] for i in range(8)]
    regs = {'zero': [0.0] * 4,
            'fog': list(env.fog_color) + [clamp(env.fog, 0.0, 1.0)],
            'v0': [clamp(x, 0.0, 1.0) for x in env.v0],
            'v1': [clamp(x, 0.0, 1.0) for x in env.v1],
            'r0': [0.0, 0.0, 0.0, t[0][3]],
            'r1': [0.0] * 4}
    for i in range(4):
        regs['t%d' % i] = t[i]
    count = d['PSCombinerCount'] & 0xf
    mux_msb = bool(d['PSCombinerCount'] & 0x100)

    def cmap(word, i):
        n = (word >> (4 * i)) & 0xf
        return cc[n] if n < 8 else None

    for i in range(count):
        c0, c1 = cmap(d['PSC0Mapping'], i), cmap(d['PSC1Mapping'], i)
        writes = []
        for half, inw, outw in (('rgb', d['PSRGBInputs'][i], d['PSRGBOutputs'][i]),
                                ('a', d['PSAlphaInputs'][i], d['PSAlphaOutputs'][i])):
            al = half == 'a'
            A = b_input(regs, (inw >> 24) & 0xff, al, c0, c1)
            B = b_input(regs, (inw >> 16) & 0xff, al, c0, c1)
            C = b_input(regs, (inw >> 8) & 0xff, al, c0, c1)
            D = b_input(regs, inw & 0xff, al, c0, c1)
            cd_reg, ab_reg, sum_reg = outw & 0xf, (outw >> 4) & 0xf, (outw >> 8) & 0xf
            flags = outw >> 12
            n = 1 if al else 3
            if flags & 0x02:
                if al:
                    raise PshError('dot product in the alpha half')
                x = A[0] * B[0] + A[1] * B[1] + A[2] * B[2]
                ab = [x, x, x]
            else:
                ab = [A[k] * B[k] for k in range(n)]
            if flags & 0x01:
                if al:
                    raise PshError('dot product in the alpha half')
                x = C[0] * D[0] + C[1] * D[1] + C[2] * D[2]
                cd = [x, x, x]
            else:
                cd = [C[k] * D[k] for k in range(n)]
            if flags & 0x04:
                r0a = regs['r0'][3]
                pick_cd = (r0a >= 0.5) if mux_msb else (int(round(r0a * 255.0)) & 1) == 1
                sm = cd if pick_cd else ab
            else:
                sm = [ab[k] + cd[k] for k in range(n)]
            om = b_omod(flags)
            ab = [out_map(x, om) for x in ab]
            cd = [out_map(x, om) for x in cd]
            sm = [out_map(x, om) for x in sm]
            for reg, val in ((ab_reg, ab), (cd_reg, cd), (sum_reg, sm)):
                if reg:
                    writes.append((REGNAMES[reg], half, val))
            if not al:
                if flags & 0x80 and ab_reg:
                    writes.append((REGNAMES[ab_reg], 'a', [ab[2]]))
                if flags & 0x40 and cd_reg:
                    writes.append((REGNAMES[cd_reg], 'a', [cd[2]]))
        for reg, half, val in writes:
            cur = list(regs[reg])
            if half == 'rgb':
                cur[0:3] = val
            else:
                cur[3] = val[0]
            regs[reg] = cur

    abcd, efg = d['PSFinalCombinerInputsABCD'], d['PSFinalCombinerInputsEFG']
    if abcd == 0 and efg == 0:
        out = a_final(regs, env, None, env.specular_enable)
        return (None if killed else out), fragile

    fcc = d['PSFinalCombinerConstants']
    fc0 = cc[fcc & 0xf] if (fcc & 0xf) < 8 else None
    fc1 = cc[(fcc >> 4) & 0xf] if ((fcc >> 4) & 0xf) < 8 else None
    settings = efg & 0xff

    def fin(byte, alpha_only=False):
        reg = byte & 0xf
        name = REGNAMES[reg]
        mod = MAPNAMES[byte & 0xe0]
        if mod not in ('sat', 'inv'):
            raise PshError('final combiner input mapping')
        if name == 'sum':
            a = [clamp(x, 0.0, 1.0) for x in regs['r0'][:3]]
            b = [clamp(x, 0.0, 1.0) for x in regs['v1'][:3]]
            if settings & 0x20:
                a = [1.0 - x for x in a]
            if settings & 0x40:
                b = [1.0 - x for x in b]
            v = [a[k] + b[k] for k in range(3)]
            if settings & 0x80:
                v = [clamp(x, 0.0, 1.0) for x in v]
            v = v + [0.0]
        elif name == 'prod':
            v = prod + [0.0]
        elif name == 'C0':
            v = fc0
        elif name == 'C1':
            v = fc1
        else:
            v = regs[name]
        if alpha_only:
            x = clamp(v[3] if byte & 0x10 else v[2], 0.0, 1.0)
            return 1.0 - x if mod == 'inv' else x
        base = [v[3]] * 3 if byte & 0x10 else v[:3]
        base = [clamp(x, 0.0, 1.0) for x in base]
        return [1.0 - x for x in base] if mod == 'inv' else base

    prod = [0.0, 0.0, 0.0]
    E, F = fin((efg >> 24) & 0xff), fin((efg >> 16) & 0xff)
    prod = [E[k] * F[k] for k in range(3)]
    A, B = fin((abcd >> 24) & 0xff), fin((abcd >> 16) & 0xff)
    C, D = fin((abcd >> 8) & 0xff), fin(abcd & 0xff)
    out = [clamp(A[k] * B[k] + (1.0 - A[k]) * C[k] + D[k], 0.0, 1.0) for k in range(3)]
    out.append(fin((efg >> 8) & 0xff, alpha_only=True))
    return (None if killed else out), fragile


def describe_def(d):
    lines = []
    count = d['PSCombinerCount'] & 0xf

    def inp(byte, alpha):
        reg = REGNAMES.get(byte & 0xf, '?%d' % (byte & 0xf))
        ch = ('.a' if byte & 0x10 else '.b') if alpha else ('.a' if byte & 0x10 else '.rgb')
        mod = MAPNAMES[byte & 0xe0]
        return '%s%s%s' % (reg, ch, ('[' + mod + ']') if mod else '')

    tm = d['PSTextureModes']
    lines.append('texture modes: ' + ', '.join(str(TEXMODES[(tm >> (5 * i)) & 0x1f]) for i in range(4)) +
                 '  input texture %08x  dot mapping %08x  compare %08x' %
                 (d['PSInputTexture'], d['PSDotMapping'], d['PSCompareMode']))
    lines.append('combiners: %d  flags %03x  C0 map %08x  C1 map %08x' %
                 (count, d['PSCombinerCount'] >> 8, d['PSC0Mapping'], d['PSC1Mapping']))
    for i in range(count):
        for half, inw, outw in (('rgb', d['PSRGBInputs'][i], d['PSRGBOutputs'][i]),
                                ('a  ', d['PSAlphaInputs'][i], d['PSAlphaOutputs'][i])):
            al = half.strip() == 'a'
            ins = [inp((inw >> s) & 0xff, al) for s in (24, 16, 8, 0)]
            flags = outw >> 12
            lines.append('  %d %s A=%s B=%s C=%s D=%s -> AB:%s CD:%s SUM:%s flags %02x (%s)' % (
                i, half, ins[0], ins[1], ins[2], ins[3],
                REGNAMES[(outw >> 4) & 0xf] if (outw >> 4) & 0xf else '-',
                REGNAMES[outw & 0xf] if outw & 0xf else '-',
                REGNAMES[(outw >> 8) & 0xf] if (outw >> 8) & 0xf else '-', flags, b_omod(flags) or 'identity'))
    abcd, efg = d['PSFinalCombinerInputsABCD'], d['PSFinalCombinerInputsEFG']
    if abcd == 0 and efg == 0:
        lines.append('final combiner: not programmed (render states decide)')
    else:
        lines.append('final combiner: A=%s B=%s C=%s D=%s E=%s F=%s G=%s settings %02x constants %03x' % (
            inp((abcd >> 24) & 0xff, False), inp((abcd >> 16) & 0xff, False), inp((abcd >> 8) & 0xff, False),
            inp(abcd & 0xff, False), inp((efg >> 24) & 0xff, False), inp((efg >> 16) & 0xff, False),
            inp((efg >> 8) & 0xff, True), efg & 0xff, d['PSFinalCombinerConstants']))
    return '\n'.join(lines)


# =====================================================================================
# GLSL generation
# =====================================================================================

def fl(x):
    s = repr(float(x))
    return s


def g_base(s, part):
    reg = s.reg
    if reg == 'zero':
        return 'vec3(0.0)' if part == 'rgb' else '0.0'
    if part == 'rgb':
        if s.chan == 'a':
            return 'vec3(%s.a)' % reg
        if s.chan == 'b':
            return 'vec3(%s.b)' % reg
        return '%s.rgb' % reg
    return '%s.b' % reg if s.chan in ('b', 'rgb') else '%s.a' % reg


def g_src(s, part):
    b = g_base(s, part)
    one = 'vec3(1.0)' if part == 'rgb' else '1.0'
    if s.reg == 'zero':
        # constant folding keeps the generated text readable; values are exact
        val = {'': 0.0, 'sat': 0.0, 'neg': 0.0, 'inv': 1.0, 'bx2': -1.0, 'negbx2': 1.0,
               'bias': -0.5, 'negbias': 0.5}[s.mod]
        return 'vec3(%s)' % fl(val) if part == 'rgb' else fl(val)
    m = s.mod
    if m == '':
        return b
    if m == 'neg':
        return '(-%s)' % b
    p = 'max(%s, 0.0)' % b
    if m == 'sat':
        return p
    if m == 'inv':
        return '(%s - %s)' % (one, p)
    if m == 'bx2':
        return '(2.0 * %s - %s)' % (p, one)
    if m == 'negbx2':
        return '(%s - 2.0 * %s)' % (one, p)
    if m == 'bias':
        return '(%s - 0.5)' % p
    if m == 'negbias':
        return '(0.5 - %s)' % p
    raise PshError(m)


def g_omap(expr, omod):
    if omod == 'bias':
        expr = '(%s) - 0.5' % expr
    elif omod == 'x2':
        expr = '(%s) * 2.0' % expr
    elif omod == 'bx2':
        expr = '((%s) - 0.5) * 2.0' % expr
    elif omod == 'x4':
        expr = '(%s) * 4.0' % expr
    elif omod == 'd2':
        expr = '(%s) * 0.5' % expr
    return 'clamp(%s, -1.0, 1.0)' % expr


def g_portion(ins, part):
    """Returns [(dest, glsl expression)] for one half of a stage."""
    S = [g_src(s, part) for s in ins.srcs]
    op = ins.op
    v3 = part == 'rgb'

    def dot(a, b):
        return 'vec3(dot(%s, %s))' % (a, b)

    if op == 'mov':
        outs = [S[0]]
    elif op == 'add':
        outs = ['%s + %s' % (S[0], S[1])]
    elif op == 'sub':
        outs = ['%s - %s' % (S[0], S[1])]
    elif op == 'mul':
        outs = ['%s * %s' % (S[0], S[1])]
    elif op == 'mad':
        outs = ['%s * %s + %s' % (S[0], S[1], S[2])]
    elif op == 'lrp':
        s0 = ins.srcs[0]
        if s0.mod == '':
            comp = g_src(Src(s0.reg, s0.chan, 'inv', s0.text), part)
        elif s0.mod == 'inv':
            comp = g_src(Src(s0.reg, s0.chan, 'sat', s0.text), part)
        else:
            raise PshError('%s: lrp factor modifier' % ins.text)
        outs = ['%s * %s + %s * %s' % (S[0], S[1], comp, S[2])]
    elif op == 'dp3':
        outs = [dot(S[0], S[1])]
    elif op == 'xmma':
        ab = '%s * %s' % (S[0], S[1])
        cd = '%s * %s' % (S[2], S[3])
        outs = [ab, cd, '%s + %s' % (ab, cd)]
    elif op == 'xdd':
        outs = [dot(S[0], S[1]), dot(S[2], S[3])]
    else:
        raise PshError(op)
    return [(d, g_omap(e, ins.omod)) for d, e in zip(ins.dests, outs) if d != 'discard']


def used_registers(sh):
    used = set()
    for st in sh.stages:
        for part in ('rgb', 'a'):
            if st[part]:
                for s in st[part].srcs:
                    used.add(s.reg)
    if sh.xfc:
        for s in sh.xfc:
            used.add(s.reg)
            if s.reg == 'sum':
                used.update(('r0', 'v1'))
    else:
        used.update(('r0', 'fog', 'v1'))
    return used


def to_glsl(sh, enum_names=()):
    used = used_registers(sh)
    L = []
    w = L.append
    w('// Generated by tools/psh2glsl.py from Source/ShaderData/PixelShaders/%s.psh (%s). Do not edit.' %
      (sh.name, sh.version))
    if enum_names:
        w('// EPixelShader: %s' % ', '.join(enum_names))
    w('// GLSL 1.20 body, also valid GLSL ES 1.00: the consumer prepends the #version line, the precision')
    w('// statement for ES and the optional defines UC2_TEXn_CUBE, UC2_SPECULARENABLE, UC2_ALPHATEST.')
    w('')
    for i in range(4):
        t = sh.texops[i]
        if t is None:
            continue
        op = t[0]
        if op == 'tex':
            w('#ifdef UC2_TEX%d_CUBE' % i)
            w('uniform samplerCube u_tex%d;' % i)
            w('#define UC2_SAMPLE%d(tc) textureCube(u_tex%d, (tc).xyz)' % (i, i))
            w('#else')
            w('uniform sampler2D u_tex%d;' % i)
            w('#define UC2_SAMPLE%d(tc) texture2DProj(u_tex%d, (tc))' % (i, i))
            w('#endif')
        elif op in ('texm3x2tex', 'texreg2gb', 'texreg2ar'):
            w('uniform sampler2D u_tex%d;' % i)
    for i in range(8):
        if 'c%d' % i in used:
            w('uniform vec4 u_c%d;' % i)
    if 'fog' in used:
        w('uniform vec4 u_fogColor;')
    w('#ifdef UC2_ALPHATEST')
    w('uniform float u_alphaRef;')
    w('#endif')
    if 'v0' in used:
        w('varying vec4 v_color0;')
    if 'v1' in used:
        w('varying vec4 v_color1;')
    if 'fog' in used:
        w('varying float v_fog;')
    for i in range(4):
        if sh.texops[i] is not None:
            w('varying vec4 v_tex%d;' % i)
    w('')
    w('void main()')
    w('{')
    for i in range(4):
        t = sh.texops[i]
        if t is None:
            continue
        op, src, srcmod, text = t
        w('    // %s' % text)
        if op == 'tex':
            w('    vec4 t%d = UC2_SAMPLE%d(v_tex%d);' % (i, i, i))
        elif op == 'texcoord':
            w('    vec4 t%d = clamp(v_tex%d, 0.0, 1.0);' % (i, i))
        elif op == 'texkill':
            w('    if (any(lessThan(v_tex%d, vec4(0.0)))) discard;' % i)
        elif op == 'texm3x2pad':
            pass
        elif op == 'texm3x2tex':
            a = 't%d.rgb' % src
            if srcmod == 'bx2':
                a = '(2.0 * (%s - 0.5))' % a
            w('    vec4 t%d = texture2D(u_tex%d, vec2(dot(v_tex%d.xyz, %s), dot(v_tex%d.xyz, %s)));' %
              (i, i, i - 1, a, i, a))
        elif op == 'texreg2gb':
            w('    vec4 t%d = texture2D(u_tex%d, t%d.gb);' % (i, i, src))
        elif op == 'texreg2ar':
            w('    vec4 t%d = texture2D(u_tex%d, t%d.ar);' % (i, i, src))
    for i in range(8):
        if 'c%d' % i in used:
            w('    vec4 c%d = clamp(u_c%d, 0.0, 1.0);' % (i, i))
    if 'v0' in used:
        w('    vec4 v0 = clamp(v_color0, 0.0, 1.0);')
    if 'v1' in used:
        w('    vec4 v1 = clamp(v_color1, 0.0, 1.0);')
    if 'fog' in used:
        w('    vec4 fog = vec4(u_fogColor.rgb, clamp(v_fog, 0.0, 1.0));')
    need_r0 = 'r0' in used or any('r0' in (st[p].dests if st[p] else ()) for st in sh.stages for p in ('rgb', 'a'))
    need_r1 = 'r1' in used or any('r1' in (st[p].dests if st[p] else ()) for st in sh.stages for p in ('rgb', 'a'))
    if need_r0:
        w('    vec4 r0 = vec4(0.0, 0.0, 0.0, %s);' % ('t0.a' if sh.texops[0] and sh.texops[0][0] in
                                                    ('tex', 'texcoord') else '0.0'))
    if need_r1:
        w('    vec4 r1 = vec4(0.0);')
    for n, st in enumerate(sh.stages):
        texts = []
        for part in ('rgb', 'a'):
            if st[part] and st[part].text not in texts:
                texts.append(st[part].text)
        w('    // stage %d: %s' % (n, '  '.join(texts)))
        writes = []
        if st['rgb']:
            for d, e in g_portion(st['rgb'], 'rgb'):
                writes.append((d, 'rgb', e))
                if dot_to_alpha(st):
                    writes.append((d, 'a', '(%s).b' % e))
        if st['a'] and (st['a'] is not st['rgb'] or has_alpha_half(st['a'])):
            for d, e in g_portion(st['a'], 'a'):
                writes.append((d, 'a', e))
        if len(writes) == 1:
            d, part, e = writes[0]
            w('    %s.%s = %s;' % (d, part, e))
        else:
            w('    {')
            for k, (d, part, e) in enumerate(writes):
                w('        %s n%d = %s;' % ('vec3' if part == 'rgb' else 'float', k, e))
            for k, (d, part, e) in enumerate(writes):
                w('        %s.%s = n%d;' % (d, part, k))
            w('    }')
    if sh.xfc is None:
        w('    // no xfc: the final combiner follows the fog and specular render states')
        w('    vec3 col = clamp(r0.rgb, 0.0, 1.0);')
        w('#ifdef UC2_SPECULARENABLE')
        w('    col = clamp(col + v1.rgb, 0.0, 1.0);')
        w('#endif')
        w('    gl_FragColor = vec4(clamp(fog.a * col + (1.0 - fog.a) * fog.rgb, 0.0, 1.0), clamp(r0.a, 0.0, 1.0));')
    else:
        w('    // %s' % sh.xfc_text)

        def frgb(s):
            if s.reg == 'prod':
                return 'prod'
            if s.reg == 'sum':
                return 'clamp(clamp(r0.rgb, 0.0, 1.0) + v1.rgb, 0.0, 1.0)'
            if s.reg == 'zero':
                return 'vec3(1.0)' if s.mod == 'inv' else 'vec3(0.0)'
            b = g_base(Src(s.reg, s.chan, '', s.text), 'rgb')
            b = 'clamp(%s, 0.0, 1.0)' % b
            return '(vec3(1.0) - %s)' % b if s.mod == 'inv' else b

        x = sh.xfc
        if any(s.reg == 'prod' for s in x[:4]):
            w('    vec3 prod = %s * %s;' % (frgb(x[4]), frgb(x[5])))
        A, B, C, D = [frgb(s) for s in x[:4]]
        zero, one = 'vec3(0.0)', 'vec3(1.0)'
        if A == zero:
            expr = C if D == zero else (D if C == zero else '%s + %s' % (C, D))
        elif A == one:
            expr = B if D == zero else '%s + %s' % (B, D)
        else:
            expr = '%s * %s + (vec3(1.0) - %s) * %s' % (A, B, A, C)
            if D != zero:
                expr += ' + %s' % D
        g = x[6]
        if g.reg == 'zero':
            ga = '1.0' if g.mod == 'inv' else '0.0'
        else:
            ga = 'clamp(%s.%s, 0.0, 1.0)' % (g.reg, 'a' if g.chan == 'a' else 'b')
            if g.mod == 'inv':
                ga = '(1.0 - %s)' % ga
        w('    gl_FragColor = vec4(clamp(%s, 0.0, 1.0), %s);' % (expr, ga))
    w('#ifdef UC2_ALPHATEST')
    w('    if (!(gl_FragColor.a > u_alphaRef)) discard;')
    w('#endif')
    w('}')
    return '\n'.join(L) + '\n'


# =====================================================================================
# The EPixelShader table (from Epic's own code)
# =====================================================================================

def read_enum():
    text = open(ENGINE_CLASSES_H, encoding='latin-1').read()
    m = re.search(r'enum EPixelShader\s*\{(.*?)\};', text, re.S)
    out = []
    for name, val in re.findall(r'(PS_\w+)\s*=\s*(\d+)', m.group(1)):
        out.append((name, int(val)))
    return out


def read_enum_map():
    """EPixelShader -> .psh base name, from the _XBOX branch of UD3DRenderDevice::GetPixelShader."""
    text = open(RENDEV_CPP, encoding='latin-1').read()
    start = text.index('FD3DPixelShader* UD3DRenderDevice::GetPixelShader(EPixelShader Type)')
    body = text[start:]
    body = body[body.index('#ifdef _XBOX'):]
    body = body[:body.index('#else')]
    files = {os.path.splitext(f)[0].lower(): os.path.splitext(f)[0]
             for f in os.listdir(PSH_DIR) if f.endswith('.psh')}
    mapping = {}
    pending = []
    for line in body.split('\n'):
        line = line.strip()
        if line.startswith('//'):
            continue
        m = re.match(r'case (PS_\w+)\s*:', line)
        if m:
            pending.append(m.group(1))
            continue
        m = re.search(r'PS_SOURCE\((\w+)\)', line)
        if m:
            base = files.get(m.group(1).lower())
            if base is None:
                raise PshError('GetPixelShader names %s, no such .psh' % m.group(1))
            for p in pending:
                mapping[p] = base
            pending = []
    return mapping


_ENUM_BY_FILE = None


def enum_names_for(base):
    global _ENUM_BY_FILE
    if _ENUM_BY_FILE is None:
        emap = read_enum_map()
        _ENUM_BY_FILE = {}
        for ename, _ in read_enum():
            if ename in emap:
                _ENUM_BY_FILE.setdefault(emap[ename], []).append(ename)
    return _ENUM_BY_FILE.get(base, ())


def all_psh():
    return sorted((os.path.splitext(f)[0] for f in os.listdir(PSH_DIR) if f.endswith('.psh')),
                  key=lambda s: s.lower())


def load_shader(base):
    return parse_psh(base, open(os.path.join(PSH_DIR, base + '.psh'), encoding='latin-1').read())


# =====================================================================================
# Test-case generation
# =====================================================================================

def grid(rng, special=0.25):
    r = rng.random()
    if r < special / 3:
        return 0.0
    if r < 2 * special / 3:
        return 1.0
    if r < special:
        return 128.0 / 255.0
    return rng.randrange(256) / 255.0


def make_textures(rng):
    texs = []
    for _ in range(4):
        px = []
        for _ in range(TEX_SIZE * TEX_SIZE):
            px.append(tuple(rng.choice((0, 255, 128, rng.randrange(256), rng.randrange(256), rng.randrange(256)))
                            for _ in range(4)))
        texs.append(px)
    return texs


def make_env(rng, textures, ops, case):
    env = Env()
    env.textures = textures
    for i in range(4):
        op = ops[i][0] if ops[i] else None
        if op in ('texm3x2pad', 'texm3x2tex'):
            env.tc[i] = [rng.randrange(-4, 9) / 4.0, rng.randrange(-4, 9) / 4.0, rng.randrange(-4, 9) / 4.0, 1.0]
        elif op == 'texkill':
            env.tc[i] = [rng.choice((-0.5, 0.25, 1.0, 0.75)) for _ in range(3)] + [1.0]
            if rng.random() < 0.5:
                env.tc[i] = [abs(x) for x in env.tc[i]]
        elif op == 'texcoord':
            env.tc[i] = [rng.choice((-0.25, 0.0, 0.3, 0.5, 1.0, 1.5)) for _ in range(3)] + [rng.choice((0.0, 0.5, 1.0))]
        else:
            q = rng.choice((1.0, 1.0, 2.0, 0.5))
            u = (rng.randrange(TEX_SIZE) + 0.5) / TEX_SIZE
            v = (rng.randrange(TEX_SIZE) + 0.5) / TEX_SIZE
            env.tc[i] = [u * q, v * q, 0.0, q]
    extreme = case % 8
    if extreme == 0:
        f = lambda: 0.0
    elif extreme == 1:
        f = lambda: 1.0
    else:
        f = lambda: grid(rng)
    env.v0 = [f() for _ in range(4)]
    env.v1 = [f() for _ in range(4)]
    env.c = [[f() for _ in range(4)] for _ in range(8)]
    if case % 5 == 3:
        # the engine hands over out-of-range constants (16.0): they must behave as 1.0
        env.c[rng.randrange(2)] = [16.0] * 4
    env.fog_color = [grid(rng) for _ in range(3)]
    env.fog = f() if case % 3 else 1.0
    env.specular_enable = (case % 4 == 2)
    return env


def cases_for(sh, n, need_stable=True):
    rng = random.Random('uc2-psh-' + sh.name.lower())
    textures = make_textures(rng)
    out = []
    tries = 0
    while len(out) < n:
        tries += 1
        if tries > n * 200:
            raise PshError('%s: cannot generate stable test cases' % sh.name)
        env = make_env(rng, textures, sh.texops, len(out))
        res, fragile = run_text(sh, env)
        if fragile and need_stable:
            continue
        out.append((env, res))
    return textures, out


# =====================================================================================
# Synthetic shaders: instruction classes the 100 sources do not exercise, with results
# worked out by hand from the XDK pages (selftest), and rendered like the others (gputest)
# =====================================================================================

SYN_V0 = [0.2, 0.4, 0.6, 0.8]
SYN_V1 = [0.9, 0.1, 0.5, 0.3]
SYN_C0 = [0.5, 0.5, 1.0, 0.25]
SYN_C1 = [1.0, 0.5, 0.25, 0.0]
SYN_FOGC = [1.0, 0.0, 0.5]

SYNTHETIC = [
    # name, text, fog factor, specular enable, expected rgba
    ('syn_multi_output', """xps.1.1
mov r0, v0
mul r1.rgb, v0, c0
+ mov r1.a, v1.b
xmma r1, r0, discard, r0, 1-zero, r1, 1-zero
xfc zero, zero, r0, zero, zero, zero, r1
""", 1.0, False, [0.1, 0.2, 0.6, 0.6]),     # registers swapped in one stage; xfc alpha source defaults to blue
    ('syn_paired_simultaneous', """xps.1.1
mov r0, v0
mov r0.rgb, r0.a
+ mov r0.a, r0.b
xfc zero, zero, r0, zero, zero, zero, r0.a
""", 1.0, False, [0.8, 0.8, 0.8, 0.6]),     # both halves read the registers as they were before the stage
    ('syn_sum_prod', """xps.1.1
mov r0, v0
xfc c0, prod, sum, zero, v0, c1, 1-r0.a
""", 1.0, False, [0.6, 0.35, 0.15, 0.2]),   # prod = v0*c1, sum = sat(r0 + v1)
    ('syn_modifiers', """xps.1.1
mov r0, v0
add_bx2 r1, r0_bias, 1-v1
sub_x4 r0.rgb, -r1, v0_bx2
+ add_d2 r0.a, r1_sat.b, -c0_bx2.a
xfc zero, zero, r0, zero, zero, zero, r0.a
""", 1.0, False, [1.0, 0.0, 0.0, 0.35]),
    ('syn_lrp_dp3', """xps.1.1
dp3 r1, v0, c1
lrp r0, v1.a, r1, c0
xfc zero, zero, zero, r0, zero, zero, r0.a
""", 1.0, False, [0.515, 0.515, 0.865, 0.34]),
    ('syn_default_final', """xps.1.1
mov r0, v0
""", 0.25, False, [0.8, 0.1, 0.525, 0.8]),  # no xfc, fog enabled
    ('syn_default_final_specular', """xps.1.1
mov r0, v0
""", 0.25, True, [1.0, 0.125, 0.625, 0.8]), # no xfc, fog and specular enabled
]


def run_synthetic():
    """Returns the number of failures."""
    fails = 0
    for name, text, fog, spec, expect in SYNTHETIC:
        try:
            sh = parse_psh(name, text)
            env = Env()
            env.textures = make_textures(random.Random(1))
            env.v0, env.v1 = list(SYN_V0), list(SYN_V1)
            env.c[0], env.c[1] = list(SYN_C0), list(SYN_C1)
            env.fog_color, env.fog, env.specular_enable = list(SYN_FOGC), fog, spec
            res, _ = run_text(sh, env)
            to_glsl(sh)
            diff = max(abs(res[k] - expect[k]) for k in range(4))
            if diff > 1e-9 or sh.warnings:
                fails += 1
                print('FAIL %-40s got %s, worked out by hand %s %s' % (name, res, expect, sh.warnings))
        except PshError as e:
            fails += 1
            print('FAIL %-40s %s' % (name, e))
    return fails


# =====================================================================================
# Commands
# =====================================================================================

def cmd_selftest(args):
    names = all_psh()
    total = fails = nodef = cases = 0
    warnings = []
    maxdiff = 0.0
    for base in names:
        total += 1
        try:
            sh = load_shader(base)
        except PshError as e:
            print('FAIL %-40s parse: %s' % (base, e))
            fails += 1
            continue
        for wtxt in sh.warnings:
            warnings.append('%s: %s' % (base, wtxt))
        to_glsl(sh)     # must not raise
        hpath = os.path.join(PSH_DIR, base + '.h')
        if not os.path.exists(hpath):
            nodef += 1
            print('NODEF %-39s no compiled .h next to the source: path A only (%d cases evaluated)' % (base, args.cases))
            cases_for(sh, args.cases)
            continue
        try:
            d = load_def(hpath)
            _, cs = cases_for(sh, args.cases)
            worst = 0.0
            bad = None
            for env, ra in cs:
                rb, _ = run_def(d, env)
                cases += 1
                if (ra is None) != (rb is None):
                    worst = 9.0
                    bad = (env, ra, rb)
                    break
                if ra is None:
                    continue
                diff = max(abs(ra[k] - rb[k]) for k in range(4))
                if diff > worst:
                    worst = diff
                    bad = (env, ra, rb)
            maxdiff = max(maxdiff, worst if worst < 9.0 else 0.0)
            if worst > 1e-9:
                fails += 1
                print('FAIL %-40s text and definition words disagree by %.6f: A=%s B=%s' % (base, worst, bad[1], bad[2]))
            elif args.verbose:
                print('ok   %-40s %d stages%s' % (base, len(sh.stages), '' if sh.xfc else ', no xfc'))
        except PshError as e:
            fails += 1
            print('FAIL %-40s %s' % (base, e))
    for wtxt in warnings:
        print('WARN ' + wtxt)
    synfails = run_synthetic()
    print('psh2glsl selftest: %d shaders, %d compared with their definition words (%d cases, max difference %.2e), '
          '%d without a .h, %d failed, %d lint warnings; %d synthetic semantics checks, %d failed' %
          (total, total - nodef, cases, maxdiff, nodef, fails, len(warnings), len(SYNTHETIC), synfails))
    return 1 if fails or synfails else 0


def emitted_files():
    """All generated files as {path: content}."""
    names = all_psh()
    emap = read_enum_map()
    enum = read_enum()
    by_file = {}
    for ename, _ in enum:
        if ename in emap:
            by_file.setdefault(emap[ename], []).append(ename)
    files = {}
    glsl = {}
    for base in names:
        sh = load_shader(base)
        glsl[base] = to_glsl(sh, by_file.get(base, ()))
        files[os.path.join(OUT_DIR, base + '.glsl')] = glsl[base]

    # the table, as text
    T = ['# EPixelShader -> generated fragment shader. Generated by tools/psh2glsl.py --emit. Do not edit.',
         '# Source of the mapping: the _XBOX branch of UD3DRenderDevice::GetPixelShader (D3DRenderDevice.cpp).',
         '# value  enum  file']
    for ename, val in enum:
        if ename == 'PS_MAX':
            continue
        T.append('%d\t%s\t%s' % (val, ename, (emap[ename] + '.glsl') if ename in emap else '-'))
    files[os.path.join(OUT_DIR, 'EPixelShader.tsv')] = '\n'.join(T) + '\n'

    # the embedded sources for the driver
    H = ['// Generated by tools/psh2glsl.py --emit from Source/ShaderData/PixelShaders/*.psh. Do not edit.',
         '// One GLSL fragment shader body per Xbox pixel shader, and the EPixelShader table.',
         '']
    order = [b for b in names]
    for base in order:
        sh = load_shader(base)
        H.append('static const char GUC2GLPixelShader_%s[] =' % base)
        for line in glsl[base].split('\n')[:-1]:
            H.append('\t"%s\\n"' % line.replace('\\', '\\\\').replace('"', '\\"'))
        H[-1] += ';'
        H.append('')
    H.append('struct FUC2GLPixelShaderSource')
    H.append('{')
    H.append('\tconst char*\tName;\t\t// .psh base name, NULL when the Xbox build has no shader for the enum')
    H.append('\tconst char*\tSource;')
    H.append('\tBYTE\t\tTexOp[4];\t// 0 none, 1 tex, 2 texcoord, 3 texkill, 4 texm3x2pad, 5 texm3x2tex, 6 texreg2gb, 7 texreg2ar')
    H.append('\tBYTE\t\tConstants;\t// bit n: reads c<n>')
    H.append('\tBYTE\t\tUsesFog;')
    H.append('};')
    H.append('')
    H.append('static const FUC2GLPixelShaderSource GUC2GLPixelShaders[PS_MAX] =')
    H.append('{')
    opcode = {None: 0, 'tex': 1, 'texcoord': 2, 'texkill': 3, 'texm3x2pad': 4, 'texm3x2tex': 5,
              'texreg2gb': 6, 'texreg2ar': 7}
    for ename, val in enum:
        if ename == 'PS_MAX':
            continue
        if ename in emap:
            base = emap[ename]
            sh = load_shader(base)
            used = used_registers(sh)
            ops = ','.join(str(opcode[t[0] if t else None]) for t in sh.texops)
            cbits = sum(1 << i for i in range(8) if 'c%d' % i in used)
            H.append('\t{ "%s", GUC2GLPixelShader_%s, {%s}, 0x%02x, %d },\t// %d %s' %
                     (base, base, ops, cbits, 1 if 'fog' in used else 0, val, ename))
        else:
            H.append('\t{ NULL, NULL, {0,0,0,0}, 0, 0 },\t// %d %s' % (val, ename))
    H.append('};')
    files[os.path.join(OUT_DIR, 'UC2GLPixelShaders.inc')] = '\n'.join(H) + '\n'
    return files


def cmd_emit(args, check=False):
    files = emitted_files()
    changed = 0
    if not check:
        os.makedirs(OUT_DIR, exist_ok=True)
    for path in sorted(files):
        old = None
        if os.path.exists(path):
            old = open(path, encoding='ascii').read()
        if old != files[path]:
            changed += 1
            if check:
                print('differs: ' + os.path.relpath(path, REPO))
            else:
                with open(path, 'w', encoding='ascii', newline='\n') as f:
                    f.write(files[path])
    if check:
        stale = [f for f in os.listdir(OUT_DIR) if f.endswith('.glsl') and os.path.join(OUT_DIR, f) not in files] \
            if os.path.isdir(OUT_DIR) else []
        for s in stale:
            print('stale: ' + s)
        print('psh2glsl check-emitted: %d files, %d differ, %d stale' % (len(files), changed, len(stale)))
        return 1 if changed or stale else 0
    print('psh2glsl emit: %d files in %s, %d written' % (len(files), os.path.relpath(OUT_DIR, REPO), changed))
    return 0


def cmd_dump(args):
    sh = load_shader(args.dump)
    print('--- text (%s): %d stages' % (sh.version, len(sh.stages)))
    for i, t in enumerate(sh.texops):
        print('  t%d: %s' % (i, t[3] if t else '-'))
    for n, st in enumerate(sh.stages):
        print('  %d rgb: %s' % (n, st['rgb'].text if st['rgb'] else '-'))
        print('  %d a  : %s' % (n, st['a'].text if st['a'] else '-'))
    print('  xfc: %s' % sh.xfc_text)
    for wtxt in sh.warnings:
        print('  WARN ' + wtxt)
    hpath = os.path.join(PSH_DIR, sh.name + '.h')
    if os.path.exists(hpath):
        print('--- definition words')
        print(describe_def(load_def(hpath)))
    print('--- GLSL')
    print(to_glsl(sh))
    return 0


def cmd_gputest(args):
    """Writes the case file, builds tools/pshgpu if needed and runs it (needs a display: use xvfb-run)."""
    names = all_psh()
    workdir = args.workdir
    os.makedirs(workdir, exist_ok=True)
    exe = os.path.join(workdir, 'pshgpu')
    if (not os.path.exists(exe)) or os.path.getmtime(exe) < os.path.getmtime(GPU_SRC):
        cc = os.environ.get('CC', 'cc')
        r = subprocess.run([cc, '-O1', '-o', exe, GPU_SRC, '-lGL', '-lX11', '-lm'])
        if r.returncode:
            print('psh2glsl gputest: cannot build %s' % GPU_SRC)
            return 2
    casefile = os.path.join(workdir, 'cases.txt')
    ncases = 0
    with open(casefile, 'w') as f:
        f.write('tolerance %g\n' % GPU_TOLERANCE)
        f.write('texsize %d\n' % TEX_SIZE)
        todo = [(base, None) for base in names] + [(n, t) for n, t, _, _, _ in SYNTHETIC]
        for base, syntext in todo:
            if syntext is None:
                sh = load_shader(base)
                src = os.path.join(OUT_DIR, base + '.glsl')
                if not os.path.exists(src):
                    print('psh2glsl gputest: %s missing, run --emit first' % src)
                    return 2
                if open(src, encoding='ascii').read() != to_glsl(sh, enum_names_for(base)):
                    print('psh2glsl gputest: %s is stale, run --emit first' % src)
                    return 2
            else:
                sh = parse_psh(base, syntext)
                src = os.path.join(workdir, base + '.glsl')
                with open(src, 'w') as g:
                    g.write(to_glsl(sh))
            textures, cs = cases_for(sh, args.cases)
            cube = ''.join('1' if (t and t[0] == 'tex') else '0' for t in sh.texops)
            f.write('shader %s %s %s\n' % (base, src, cube))
            for i in range(4):
                f.write('texture %d %s\n' % (i, ' '.join('%02x%02x%02x%02x' % px for px in textures[i])))
            for env, res in cs:
                ncases += 1
                vals = []
                for i in range(8):
                    vals += env.c[i]
                vals += env.fog_color + [env.fog]
                vals += env.v0 + env.v1
                for i in range(4):
                    vals += env.tc[i]
                exp = 'kill' if res is None else ' '.join('%.9g' % (x * 255.0) for x in res)
                f.write('case %d %s : %s\n' % (1 if env.specular_enable else 0, ' '.join('%.9g' % v for v in vals), exp))
            f.write('end\n')
    r = subprocess.run([exe, casefile] + (['-v'] if args.verbose else []))
    return r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--emit', action='store_true')
    ap.add_argument('--check-emitted', action='store_true')
    ap.add_argument('--gputest', action='store_true')
    ap.add_argument('--dump', metavar='NAME')
    ap.add_argument('--cases', type=int, default=64)
    ap.add_argument('--workdir', default=os.path.join(REPO, 'build-pshgpu'))
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    if args.dump:
        return cmd_dump(args)
    rc = 0
    did = False
    if args.selftest:
        rc |= cmd_selftest(args)
        did = True
    if args.emit:
        rc |= cmd_emit(args)
        did = True
    if args.check_emitted:
        rc |= cmd_emit(args, check=True)
        did = True
    if args.gputest:
        rc |= cmd_gputest(args)
        did = True
    if not did:
        ap.print_help()
        return 2
    return rc


if __name__ == '__main__':
    sys.exit(main())
