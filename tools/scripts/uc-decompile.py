#!/usr/bin/env python3
"""uc-decompile: UnrealScript decompiler for Unreal Championship 2 script packages (v151).

    uc-decompile.py <System>/<Pkg>.u --out <dir> [--shipped-text] [--patches ScriptPatches.h]
                    [--content <root>] [--class Name ...]

Writes <dir>/<Class>.uc for every class of <Pkg>.u (or the named ones), decompiled from the bytecode and the
declarations of the package, in a form `ucc make` accepts and compiles back to the same bytecode
(docs/porting/analysis/decompile-spike.md, script-patches.md). The other 16 script packages must sit next to
<Pkg>.u (types, functions and constants are looked up across all 17); content packages (textures, meshes, ...)
are looked up under --content (default: the folder above <Pkg>.u's), for the `#exec OBJ LOAD` lines.

  --shipped-text  write the package's own script text (ScriptText) instead of decompiling, when it has one: the
                  shipped UCMenu.u was never stripped and carries Epic's retail text for all 171 classes. That
                  text is what Epic's preprocessor left (comments blanked, `#linenumber` marks) and holds no
                  defaultproperties (the class factory cuts them off): the decompiled defaultproperties block is
                  appended. CR LF becomes LF. A class without text (" ") is decompiled.
  --patches       decompile the package as Epic's load-time patcher leaves it in memory: the shipped .u plus the
                  ScriptPatches.h entries for it (replaced bytecode, appended locals and functions linked at the
                  head of their parent's Children, as FLinkerPatchData::Apply does).

Exit status: 0 when every class was written without a decompile error; 1 when any class has one (the files are
still written, each error as a `// DECOMPILE ERROR` line and on stderr); 2 for bad arguments or unreadable input.

Canonical form (this is the context of the patch sets in patches/: changing any of it changes the base text, see
`uc-patches.py rebase`):
  - Bytes: Latin-1, LF line ends, a final LF; 0x02 glyph bytes in strings are written as they are.
  - Line 1 of every file: `// decompiled by uc-decompile <VERSION> from <Pkg>.u <sha256 of the .u, 12 hex>`, plus
    ` + ScriptPatches.h <sha256, 12 hex>` with --patches, or `// shipped text of <Class> from <Pkg>.u <sha>` with
    --shipped-text (replacing the text's first line, which is blank in every shipped text, so line numbers keep).
  - Decompiled class: the class line; `#exec OBJ LOAD FILE=../<Dir>/<File>` lines, one per content package the class
    references, in order of first use; a blank line; the declarations in the order that rebuilds the shipped
    field chain (Decompiler.decl_order: consts, enums, structs, vars, functions, states as the compiler prepends
    them); `replication`; a one-line `cpptext` block when the class has one; `defaultproperties`.
  - One declaration per line; one statement per line; tabs for indentation (one per level); braces on their own
    line; every binary operator parenthesised `(a + b)`; `else if` chains flat; no comments other than line 1.
  - Blank lines only: one after the #exec block, one before each function/state, and the padding that puts an
    `assert` on the source line its bytecode records.
  - defaultproperties: subobjects (`Begin Object`) first in export order, then the class's own values in the
    order the package stores them; arrays as `X=()` then `X(i)=v`; static arrays `X(i)=v`, inside a struct
    value `X[i]=v`; floats as the shortest decimal that reads back to the same float32.
  Same input, same bytes: nothing depends on the clock, the locale, hash order or the file system's order.
"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ucpkg  # noqa: E402
from ucpkg import Package, parse_export  # noqa: E402

VERSION = '1.0'
sd = ucpkg      # the shared reader (constants, Reader, tagged, ParseError, PACKAGES)

# --- flags (Source/Core/Inc/UnObjBas.h, UnStack.h) ------------------------------------------------------------
RF_Public, RF_Final, RF_Protected, RF_Native = 0x4, 0x80, 0x800, 0x04000000
CPF = [(0x00000002, 'const'), (0x00000004, 'input'), (0x00000008, 'export'), (0x00001000, 'native'),
       (0x00002000, 'transient'), (0x00008000, 'localized'), (0x00010000, 'travel'), (0x00020000, 'editconst'),
       (0x00000040, 'editconstarray'), (0x00800000, 'noexport'), (0x08000000, 'edfindable'),
       (0x20000000, 'deprecated'), (0x02000000, 'precache')]
CPF_Edit, CPF_OptionalParm, CPF_Parm, CPF_OutParm, CPF_SkipParm, CPF_ReturnParm, CPF_CoerceParm = \
    0x1, 0x10, 0x80, 0x100, 0x200, 0x400, 0x800
CPF_Config, CPF_GlobalConfig = 0x4000, 0x40000
CPF_EditInline, CPF_EditInlineUse, CPF_EditInlineNotify, CPF_Automated = 0x04000000, 0x10000000, 0x40000000, 0x80000000
FUNC_Final, FUNC_Defined, FUNC_Iterator, FUNC_Latent, FUNC_PreOperator, FUNC_Singular = 1, 2, 4, 8, 0x10, 0x20
FUNC_Net, FUNC_NetReliable, FUNC_Simulated, FUNC_Exec, FUNC_Native, FUNC_Event = 0x40, 0x80, 0x100, 0x200, 0x400, 0x800
FUNC_Operator, FUNC_Static, FUNC_Public, FUNC_Private, FUNC_Protected = 0x1000, 0x2000, 0x20000, 0x40000, 0x80000
FUNC_Delegate, FUNC_DebugOnly, FUNC_Multicast = 0x100000, 0x200000, 0x400000
CLASS_Abstract, CLASS_Config, CLASS_Transient, CLASS_NoExport, CLASS_Placeable = 1, 4, 8, 0x100, 0x200
CLASS_PerObjectConfig, CLASS_NativeReplication, CLASS_EditInlineNew, CLASS_CollapseCategories = 0x400, 0x800, 0x1000, 0x2000
CLASS_SafeReplace, CLASS_AutoInstancedProps, CLASS_Deprecated, CLASS_AutoPrecache, CLASS_Poolable = 0x40, 0x200000, 0x400000, 0x800000, 0x1000000
CLASS_NativeSkipAutoLoad, CLASS_FullDefaultsBase = 0x2000000, 0x40000000
STATE_Editable, STATE_Auto, STATE_Simulated = 1, 2, 4
PROPS = sd.PROPERTY_CLASSES

# Probe names (Core/Inc/UnNames.h, REGISTER_NAME 300..363): an `ignores` of a probe clears a bit of the state's
# IgnoreMask.
PROBES = dict(enumerate((
    'Spawned Destroyed GainedChild LostChild Probe4 Probe5 Trigger UnTrigger Timer HitWall Falling Landed '
    'PhysicsVolumeChange Touch UnTouch Bump BeginState EndState BaseChange Attach Detach ActorEntered ActorLeaving '
    'ZoneChange AnimEnd EndedRotation InterpolateEnd EncroachingOn EncroachedBy NotifyTurningInPlace '
    'HeadVolumeChange PostTouch PawnEnteredVolume MayFall CheckDirectionChange PawnLeavingVolume Tick PlayerTick '
    'ModifyVelocity CheckMovementTransition SeePlayer EnemyNotVisible HearNoise UpdateEyeHeight SeeMonster '
    'SeeFriend SpecialHandling BotDesireability NotifyBump NotifyPhysicsVolumeChange AIHearSound '
    'NotifyHeadVolumeChange NotifyLanded NotifyHitWall PostNetReceive PreBeginPlay BeginPlay PostBeginPlay Probe58 '
    'PhysicsChangedFor ActorEnteredVolume ActorLeavingVolume PrepareForMove All').split()))
assert len(PROBES) == 64


class DecompileError(Exception):
    pass


SOFT = object()     # an optional blank line (Decompiler.pad_lines)


# --- the world: all 17 packages, objects by full path ----------------------------------------------------------
class Obj:
    def __init__(self, world, pkgname, idx):
        self.world, self.pkgname, self.idx = world, pkgname, idx
        self.pkg = world.pkgs[pkgname]
        self.cls = self.pkg.export_class(idx)
        self.path = self.pkg.export_path(idx)
        self.key = pkgname + '.' + self.path
        self.name = self.path.split('.')[-1]
        self.flags = self.pkg.exports[idx]['flags']
        self._f = None

    @property
    def f(self):
        if self._f is None:
            self._f = parse_export(self.pkg, self.idx)
        return self._f

    def ref(self, field):
        """Obj for a reference-valued field (or None)."""
        return self.world.resolve(self.pkgname, self.f.get(field, 'None'))

    def children(self):
        out = []
        c = self.ref('children')
        while c is not None:
            out.append(c)
            c = c.ref('next')
        return out

    def outer(self):
        p = self.pkg.exports[self.idx]['package']
        if p > 0:
            return self.world.objs[self.pkgname][p - 1]
        return None

    def __repr__(self):
        return '<%s %s>' % (self.cls, self.key)


class World:
    """The 17 script packages of one System folder (optionally as Epic's load-time patcher leaves them), objects by
    full path, and the content packages under `content` (for `#exec OBJ LOAD`)."""
    def __init__(self, sysdir, content=None, patches=None):
        self.pkgs, self.objs, self.index = {}, {}, {}
        self.content = {}
        root = content or os.path.dirname(os.path.abspath(sysdir.rstrip('/')))
        sysname = os.path.basename(os.path.abspath(sysdir.rstrip('/')))
        for d in sorted(os.listdir(root)):
            if d == sysname or not os.path.isdir(os.path.join(root, d)):
                continue
            for fn in sorted(os.listdir(os.path.join(root, d))):
                base, ext = os.path.splitext(fn)
                if ext.lower() in ('.utx', '.usx', '.ukx', '.uax', '.u', '.umx', '.upx', '.ugx'):
                    self.content.setdefault(base.lower(), '../%s/%s' % (d, fn))
        self.sha = {}
        for p in sd.PACKAGES:
            path = os.path.join(sysdir, p + '.u')
            if not os.path.isfile(path):
                raise DecompileError('%s: missing (all 17 script packages must be in %s)' % (path, sysdir))
            pk = Package(path)
            self.sha[p] = hashlib.sha256(pk.data).hexdigest()[:12]
            if patches and p in patches:
                pk = sd.PatchedView(pk, patches[p], full=True)
            self.pkgs[p] = pk
            self.objs[p] = [Obj(self, p, i) for i in range(len(pk.exports))]
            pk.exp_paths = {}
            for o in self.objs[p]:
                pk.exp_paths[o.path] = o
                self.index.setdefault(o.key.lower(), o)
        for p in sd.PACKAGES:
            if isinstance(self.pkgs[p], sd.PatchedView):
                self.link_appended(p)
        # Any enum yields a ByteConst for its n-th tag: the fallback when the context's enum is unknown (an enum
        # tag assigned to an int, e.g. `m_CTIState = ECrossTitleInvite.CTI_None`). The largest of Core/Engine.
        self.fallback_enum = max((o for p in ('Core', 'Engine') for o in self.objs[p] if o.cls == 'Enum'),
                                 key=lambda o: len(o.f['names']))
        self.natives = {}
        for p in sd.PACKAGES:
            for o in self.objs[p]:
                if o.cls == 'Function' and o.pkg.exports[o.idx]['size']:
                    try:
                        n = o.f['inative']
                    except sd.ParseError:
                        continue
                    if n:
                        self.natives[n] = o

    def link_appended(self, p):
        """FLinkerPatchData::Apply: each appended UField whose outer is a UStruct goes to the head of the outer's
        Children (Field->Next = Parent->Children; Parent->Children = Field), in the patch's export order."""
        pk = self.pkgs[p]
        for i in range(len(pk.old.exports), len(pk.exports)):
            o = self.objs[p][i]
            parent = o.outer()
            if parent is None or parent.cls not in ('Class', 'State', 'Function', 'Struct'):
                continue
            if o.cls not in sd.PROPERTY_CLASSES and o.cls not in ('Function', 'State', 'Const', 'Enum', 'Struct'):
                continue
            # "if this object already exists in the struct's Children list ... we don't need to do anything": an
            # appended function's own appended locals are already in its serialized Children
            c, seen = parent.f['children'], set()
            while c != 'None' and c not in seen and c != o.path:
                seen.add(c)
                c = pk.exp_paths[c].f['next'] if c in pk.exp_paths else 'None'
            if c == o.path:
                continue
            o.f['next'] = parent.f['children']
            parent.f['children'] = o.path

    def resolve(self, pkgname, ref):
        if ref is None or ref == 'None':
            return None
        o = self.pkgs[pkgname].exp_paths.get(ref)
        if o is not None:
            return o
        return self.index.get(ref.lower())

    def resolve_index(self, pkgname, i):
        """Obj for a raw object index of package pkgname, or ('import', path, classname) for an unknown import."""
        pk = self.pkgs[pkgname]
        if i > 0:
            return self.objs[pkgname][i - 1]
        if i < 0:
            path = pk.import_path(-i - 1)
            o = self.index.get(path.lower())
            if o is not None:
                return o
            return ('import', path, pk.imports[-i - 1]['class_name'])
        return None


# --- helpers -----------------------------------------------------------------------------------------------------
def fmt_float(v):
    """Fixed-point text that the compiler (appAtof -> FLOAT) and ImportText read back to exactly v (float32)."""
    if v != v or v in (float('inf'), float('-inf')):
        raise DecompileError('non-finite float')
    target = struct.pack('<f', v)
    for digits in range(1, 60):
        s = '%.*f' % (digits, v)
        if struct.pack('<f', float(s)) == target:
            break
    else:
        raise DecompileError('float %r not representable' % v)
    if '.' in s:
        s = s.rstrip('0')
        if s.endswith('.'):
            s += '0'
    if s.startswith('-0.') and float(s) == 0 and struct.pack('<f', v) != struct.pack('<f', 0.0):
        pass
    if len(s) > 60:
        raise DecompileError('float %r needs %d characters' % (v, len(s)))
    return s


def esc_str(s):
    out = []
    for ch in s:
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\t':
            out.append('\\t')
        elif ch == '\r':
            out.append('\\r')
        else:
            out.append(ch)
    return '"' + ''.join(out) + '"'


def owner_class(o):
    while o is not None and o.cls != 'Class':
        o = o.outer()
    return o


def super_chain(cls):
    out = []
    c = cls
    while c is not None:
        out.append(c)
        c = c.ref('super')
    return out


# --- types -------------------------------------------------------------------------------------------------------
class Typer:
    def __init__(self, world, cls):
        self.world, self.cls = world, cls
        self.chain = super_chain(cls)
        self.chain_keys = {c.key for c in self.chain}
        self.deps = []      # classes of this package whose types are used: `dependson` (parse order)

    def scoped(self, o):
        """Name of a struct/enum o as usable from self.cls."""
        oc = owner_class(o)
        if oc is None or oc.key in self.chain_keys:
            return o.name
        if oc.pkgname == self.cls.pkgname and oc not in self.deps:
            self.deps.append(oc)
        return '%s.%s' % (oc.name, o.name)

    def type_of(self, p, in_array=False):
        c = p.cls
        f = p.f
        if c == 'ByteProperty':
            e = p.ref('enum')
            return self.scoped(e) if e is not None else 'byte'
        if c == 'IntProperty':
            return 'int'
        if c == 'BoolProperty':
            return 'bool'
        if c == 'FloatProperty':
            return 'float'
        if c == 'NameProperty':
            return 'name'
        if c == 'StrProperty':
            return 'string'
        if c == 'ObjectProperty':
            return self.class_name(f['propertyclass'], p)
        if c == 'ClassProperty':
            m = self.class_name(f['metaclass'], p)
            return 'class' if m == 'Object' else 'class<%s>' % m
        if c == 'StructProperty':
            return self.scoped(p.ref('struct'))
        if c == 'ArrayProperty':
            inner = self.type_of(p.ref('inner'), True)
            return 'array<%s>' % (inner + ' ' if inner.endswith('>') else inner)
        if c == 'PointerProperty':
            return 'pointer'
        if c == 'DelegateProperty':
            fn = p.ref('function')
            return 'delegate<%s>' % (fn.name if fn else 'None')
        raise DecompileError('type %s' % c)

    def class_name(self, ref, ctx):
        if ref == 'None':
            return 'Object'
        return ref.split('.')[-1]


def var_flags(p, in_func=False):
    fl = p.f['flags']
    words = []
    of = p.flags
    if not in_func:
        if not of & RF_Public and of & RF_Final:
            words.append('private')
        elif of & RF_Protected:
            words.append('protected')
    for bit, w in CPF:
        if fl & bit:
            words.append(w)
    if fl & CPF_GlobalConfig:
        words.append('globalconfig')
    elif fl & CPF_Config:
        words.append('config')
    if fl & CPF_Automated:
        words.append('automated')
    elif fl & CPF_EditInlineUse:
        words.append('editinlineuse')
    elif fl & CPF_EditInlineNotify:
        words.append('editinlinenotify')
    elif fl & CPF_EditInline:
        words.append('editinline')
    if in_func:
        # parameters and locals: only the parameter keywords (the save adds export/editinline to object properties)
        words = ['const'] if fl & 0x2 and fl & CPF_Parm else []
        if fl & CPF_OptionalParm:
            words.append('optional')
        if fl & CPF_OutParm and not fl & CPF_ReturnParm:
            words.append('out')
        if fl & CPF_SkipParm:
            words.append('skip')
        if fl & CPF_CoerceParm:
            words.append('coerce')
    return words


# --- bytecode to expressions -------------------------------------------------------------------------------------
CASTS = {0x39: 'vector', 0x3A: 'int', 0x3B: 'bool', 0x3C: 'float', 0x3D: 'byte', 0x3E: 'bool', 0x3F: 'float',
         0x40: 'byte', 0x41: 'int', 0x42: 'float', 0x43: 'byte', 0x44: 'int', 0x45: 'bool', 0x47: 'bool',
         0x48: 'bool', 0x49: 'byte', 0x4A: 'int', 0x4B: 'bool', 0x4C: 'float', 0x4D: 'vector', 0x4E: 'rotator',
         0x4F: 'bool', 0x50: 'rotator', 0x51: 'bool', 0x52: 'string', 0x53: 'string', 0x54: 'string',
         0x55: 'string', 0x56: 'string', 0x57: 'string', 0x58: 'string', 0x59: 'string', 0x5A: 'name',
         0x5B: 'string'}   # ECastToken, Source/Core/Inc/UnStack.h


class E:
    """Expression node: text plus a kind used by the statement builder."""
    __slots__ = ('t', 'k', 'a', 'en')

    def __init__(self, t, k='expr', a=None, en=None):
        self.t, self.k, self.a, self.en = t, k, a, en

    def __repr__(self):
        return self.t


def prop_enum(o):
    """The enum of a byte property (or of an array's byte inner), else None."""
    if o is None or isinstance(o, tuple):
        return None
    if o.cls == 'ByteProperty':
        return o.ref('enum')
    if o.cls == 'ArrayProperty':
        return prop_enum(o.ref('inner'))
    return None


def bc(e, en):
    """A byte constant (or its implicit int conversion) printed as the enum tag when the context's enum is known:
    the compiler emits ByteConst only for enum tags; a bare number would compile to an int constant."""
    if en is not None and e.k == 'cond' and e.en is None:
        c, x, y = e.a
        x, y = bc(x, en), bc(y, en)
        return E('(%s ? %s : %s)' % (c.t, x.t, y.t), 'cond', (c, x, y), en)
    if en is not None and e.k in ('byteconst', 'castbyteconst'):
        names = en.f['names']
        tag = names[e.a] if e.a < len(names) else 'EnumCount'
        return E('%s.%s' % (en.name, tag), 'enumconst', e.a, en)
    return e


def to_enum(e, en):
    """Assigning a plain byte to an enum byte needs `EnumName(x)` (no code: same type)."""
    e = bc(e, en)
    if en is not None and e.en is not en and e.k not in ('enumconst', 'nothing', 'byteconst', 'cond'):
        return E('%s(%s)' % (en.name, e.t), e.k if e.k == 'call' else 'expr', e.a, en)
    return e


def params_of(fn):
    if fn is None or isinstance(fn, tuple):
        return [], None
    kids = fn.children()
    ps = [c for c in kids if c.cls in PROPS and c.f['flags'] & CPF_Parm and not c.f['flags'] & CPF_ReturnParm]
    rs = [c for c in kids if c.cls in PROPS and c.f['flags'] & CPF_ReturnParm]
    return ps, (rs[0] if rs else None)


class Code:
    def __init__(self, dec, owner, script):
        self.dec, self.world, self.owner, self.s, self.i = dec, dec.world, owner, script, 0
        self.pkgname = owner.pkgname
        self.pkg = owner.pkg
        self.ctx_class = None

    # raw readers
    def b(self):
        v = self.s[self.i]
        self.i += 1
        return v

    def w(self):
        v = struct.unpack_from('<H', self.s, self.i)[0]
        self.i += 2
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.s, self.i)[0]
        self.i += 4
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.s, self.i)[0]
        self.i += 4
        return v

    def obj(self):
        return self.world.resolve_index(self.pkgname, self.i32())

    def nm(self):
        return self.pkg.names[self.i32()]

    def objname(self, o):
        if o is None:
            return 'None'
        n = o[1].split('.')[-1] if isinstance(o, tuple) else o.name
        if n.startswith('__') and n.endswith('__Delegate'):
            n = n[2:-10]        # a delegate's hidden property is written as the delegate's name
        return n

    # statements: a flat list (offset, end, kind, payload)
    def statements(self):
        out = []
        while self.i < len(self.s):
            at = self.i
            t = self.s[at]
            if t == 0x06:
                self.i += 1
                out.append((at, None, 'jump', self.w()))
            elif t == 0x07:
                self.i += 1
                w = self.w()
                out.append((at, None, 'jumpifnot', (w, self.expr())))
            elif t == 0x05:
                self.i += 1
                self.b()
                out.append((at, None, 'switch', self.expr()))
            elif t == 0x0A:
                self.i += 1
                w = self.w()
                out.append((at, None, 'case', (w, self.expr() if w != 0xFFFF else None)))
            elif t == 0x2F:
                self.i += 1
                e = self.expr()
                out.append((at, None, 'iterator', (e, self.w())))
            elif t == 0x31:
                self.i += 1
                out.append((at, None, 'iternext', None))
            elif t == 0x30:
                self.i += 1
                out.append((at, None, 'iterpop', None))
            elif t == 0x04:
                self.i += 1
                e = self.expr()
                out.append((at, None, 'return', None if e.k == 'nothing' else e))
            elif t == 0x08:
                self.i += 1
                out.append((at, None, 'stop', None))
            elif t == 0x0B:
                self.i += 1
                out.append((at, None, 'nop', None))
            elif t == 0x0C:
                self.i += 1
                labels = []
                while True:
                    n = self.nm()
                    off = self.i32()
                    if n == 'None':
                        break
                    labels.append((n, off))
                out.append((at, None, 'labeltable', labels))
            elif t == 0x0D:
                self.i += 1
                out.append((at, None, 'gotolabel', self.expr()))
            elif t == 0x09:
                self.i += 1
                line = self.w()
                e = self.expr()
                out.append((at, None, 'assert', E(e.t, e.k, line)))
            elif t == 0x0E:
                self.i += 1
                self.i32()
                out.append((at, None, 'destroyret', None))
            else:
                out.append((at, None, 'expr', self.expr()))
        res = []
        for k, (at, _, kind, pl) in enumerate(out):
            end = out[k + 1][0] if k + 1 < len(out) else len(self.s)
            res.append((at, end, kind, pl))
        return res

    def args(self):
        a = []
        while True:
            if self.s[self.i] == 0x16:
                self.i += 1
                break
            a.append(self.expr())
        while a and a[-1].k == 'nothing':
            a.pop()
        return a

    def call(self, fn, args, how, name=None):
        """Text of a call to function Obj fn (how: final/virtual/global/native/delegate)."""
        if fn is None and name is not None:
            fn = self.dec.find_function(name, self.ctx_class)
        ps, rp = params_of(fn)
        ren = prop_enum(rp)
        if fn is not None and not isinstance(fn, tuple) and fn.f['funcflags'] & FUNC_Operator and len(args) == 2:
            en = args[0].en or args[1].en
            args = [bc(args[0], en), bc(args[1], en)]
        else:
            args = [to_enum(a, prop_enum(ps[n])) if n < len(ps) else a for n, a in enumerate(args)]
        e = self.call0(fn, args, how, name)
        if e.en is None:
            e.en = ren
        if e.a is None and e.k == 'call':
            e.a = rp
        return e

    def call0(self, fn, args, how, name=None):
        if fn is not None and not isinstance(fn, tuple):
            ff = fn.f['funcflags']
            if ff & FUNC_Operator:
                sym = fn.f['friendlyname']
                nparms = len([c for c in fn.children() if c.cls in PROPS and c.f['flags'] & CPF_Parm and not c.f['flags'] & CPF_ReturnParm])
                if nparms == 2 and len(args) == 2:
                    return E('(%s %s %s)' % (args[0].t, sym, args[1].t))
                if ff & FUNC_PreOperator and len(args) == 1:
                    return E('%s(%s)' % (sym, args[0].t))
                if len(args) == 1:
                    return E('(%s)%s' % (args[0].t, sym), 'postop')
            name = fn.f['friendlyname'] if fn.f['friendlyname'] != 'None' else fn.name
            if how == 'final' and not ff & FUNC_Final:
                outer = fn.outer()
                if self.dec.plain_super(self.owner, name) is fn:
                    return E('Super.%s(%s)' % (name, ', '.join(x.t for x in args)), 'call')
                return E('Super(%s).%s(%s)' % (outer.name, name, ', '.join(x.t for x in args)), 'call')
        return E('%s(%s)' % (name, ', '.join(x.t for x in args)), 'call')

    def expr(self):
        at = self.i
        t = self.b()
        if t >= 0x70 or 0x60 <= t < 0x70:
            n = t if t >= 0x70 else ((t - 0x60) << 8) + self.b()
            fn = self.world.natives.get(n)
            if fn is None:
                raise DecompileError('native %d unknown' % n)
            return self.call(fn, self.args(), 'native')
        if t in (0x00, 0x01, 0x49):
            o = self.obj()
            return E(self.objname(o), 'var', o, prop_enum(o))
        if t == 0x02:
            o = self.obj()
            return E('default.' + self.objname(o), 'var', o, prop_enum(o))
        if t == 0x2D:
            return self.expr()
        if t == 0x17:
            return E('self')
        if t in (0x0F, 0x14, 0x45):
            l = self.expr()
            r = to_enum(self.expr(), l.en)
            return E('%s = %s' % (l.t, r.t), 'let')
        if t == 0x1B:
            name = self.nm()
            cc, self.ctx_class = self.ctx_class, None
            args = self.args()
            self.ctx_class = cc
            return self.call(None, args, 'virtual', name)
        if t == 0x38:
            name = self.nm()
            return E('Global.%s(%s)' % (name, ', '.join(x.t for x in self.args())), 'call')
        if t == 0x1C:
            fn = self.obj()
            if isinstance(fn, tuple):
                raise DecompileError('final call to unknown %s' % fn[1])
            return self.call(fn, self.args(), 'final')
        if t == 0x43:
            dp = self.obj()
            name = self.nm()
            fn = dp.ref('function') if dp is not None and not isinstance(dp, tuple) else None
            cc, self.ctx_class = self.ctx_class, None
            args = self.args()
            self.ctx_class = cc
            if fn is None:
                return E('%s(%s)' % (name, ', '.join(x.t for x in args)), 'call')
            return self.call(fn, args, 'delegate', name)
        if t == 0x48:
            self.obj()
            name = self.nm()
            fn = self.obj()
            return E('Super(%s).%s(%s)' % (fn.outer().name, name, ', '.join(x.t for x in self.args())), 'call')
        if t == 0x44:
            return E(self.nm(), 'delegate')
        if t in (0x12, 0x19):
            scope = self.ctx_class if self.ctx_class is not None else self.dec.cls  # whose `Outer` this may be
            ctx = self.expr()
            skip = self.w()
            self.b()
            saved = self.ctx_class
            self.ctx_class = self.class_of(ctx, t == 0x12)
            m_at = self.i
            try:
                m = self.expr()
            finally:
                self.ctx_class = saved
            m_len = self.i - m_at
            if t == 0x12:
                if m.k not in ('call', 'var') and not m.t.startswith('default.'):
                    return E('%s.const.%s' % (ctx.t, self.const_named(ctx, m)), 'expr', None, m.en)
                if m.t.startswith('default.'):
                    return E('%s.%s' % (ctx.t, m.t), 'expr', None, m.en)
                return E('%s.static.%s' % (ctx.t, m.t), 'call' if m.k == 'call' else 'expr', None, m.en)
            if ctx.k == 'var' and ctx.a is not None and not isinstance(ctx.a, tuple) and ctx.a.key == 'Core.Object.Outer' \
                    and scope.f.get('within', 'None') not in ('None', 'Core.Object') and m.k in ('var', 'call') \
                    and skip != m_len:
                # a member of the `within` class used without context: the compiler adds Context(Outer, ...)
                # itself, with a skip that does not count all of the member's bytes (a bool variable's
                # BoolVariable token is left out); an explicit `Outer.x` skips exactly the member's bytes and is
                # printed as such (PlayerInput's `Outer.bDemoOwner`, CheatManager's `Outer.GotoState(...)`)
                return m
            if m.k not in ('call', 'var', 'delegate') and not m.t.startswith('default.'):
                # `obj.const.NAME`: the compiler emits Context(obj, <the constant's value>)
                return E('%s.const.%s' % (ctx.t, self.const_named(ctx, m)), 'expr', None, m.en)
            return E('%s.%s' % (ctx.t, m.t), m.k if m.k in ('call', 'var') else 'expr', m.a if m.k == 'var' else None, m.en)
        if t in (0x1A, 0x10):
            idx = self.expr()
            arr = self.expr()
            return E('%s[%s]' % (arr.t, idx.t), 'var', None, arr.en)
        if t == 0x37:
            a = self.expr()
            return E('%s.Length' % a.t, 'var')
        if t == 0x40:
            a = self.expr()
            x = self.expr()
            y = self.expr()
            return E('%s.Insert(%s, %s)' % (a.t, x.t, y.t), 'call')
        if t == 0x41:
            a = self.expr()
            x = self.expr()
            y = self.expr()
            return E('%s.Remove(%s, %s)' % (a.t, x.t, y.t), 'call')
        if t == 0x46:
            a = self.expr()
            return E('%s.Shuffle()' % a.t, 'call')
        if t == 0x50:
            a = self.expr()
            b = self.expr()
            return E('%s.AppendArray(%s)' % (a.t, b.t), 'call')
        if t == 0x47:
            c = self.expr()
            self.w()
            x = self.expr()
            self.w()
            y = self.expr()
            en = x.en or y.en
            x, y = bc(x, en), bc(y, en)
            return E('(%s ? %s : %s)' % (c.t, x.t, y.t), 'cond', (c, x, y), en)
        if t == 0x11:
            outer, name, flags, cls = self.expr(), self.expr(), self.expr(), self.expr()
            parts = [outer.t, name.t, flags.t]
            while parts and parts[-1] == '':
                parts.pop()
            return E('new%s %s' % ('(%s)' % ', '.join(parts) if parts else '', cls.t), 'call')
        if t == 0x1D:
            return E(str(self.i32()))
        if t == 0x1E:
            v = self.f32()
            s = fmt_float(v)
            return E(s)
        if t == 0x1F:
            j = self.s.index(b'\0', self.i)
            txt = self.s[self.i:j].decode('latin-1')
            self.i = j + 1
            return E(esc_str(txt))
        if t == 0x34:
            chars = []
            while True:
                c = self.w()
                if c == 0:
                    break
                chars.append(chr(c))
            return E(esc_str(''.join(chars)))
        if t == 0x20:
            o = self.obj()
            return E(self.objconst(o), 'objconst', o)
        if t == 0x21:
            return E("'%s'" % self.nm())
        if t == 0x22:
            return E('rot(%d,%d,%d)' % (self.i32(), self.i32(), self.i32()))
        if t == 0x23:
            return E('vect(%s,%s,%s)' % (fmt_float(self.f32()), fmt_float(self.f32()), fmt_float(self.f32())))
        if t == 0x35:
            return E('rng(%s,%s)' % (fmt_float(self.f32()), fmt_float(self.f32())))
        if t == 0x24:
            n = self.b()
            return E(str(n), 'byteconst', n)
        if t == 0x2C:
            return E(str(self.b()))
        if t == 0x25:
            return E('0')
        if t == 0x26:
            return E('1')
        if t == 0x27:
            return E('True')
        if t == 0x28:
            return E('False')
        if t == 0x2A:
            return E('None')
        if t == 0x0B:
            return E('', 'nothing')
        if t == 0x2E:
            o = self.obj()
            e = self.expr()
            return E('%s(%s)' % (self.objname(o), e.t), 'dyncast', o)
        if t == 0x13:
            o = self.obj()
            e = self.expr()
            return E('class<%s>(%s)' % (self.objname(o), e.t))
        if t == 0x39:
            c = self.b()
            e = self.expr()
            if c not in CASTS:
                raise DecompileError('cast 0x%02x' % c)
            if c == 0x3A and e.k == 'byteconst':
                fb = bc(E('', 'castbyteconst', e.a), self.world.fallback_enum)
                return E(fb.t, 'castbyteconst', e.a)
            return E('%s(%s)' % (CASTS[c], e.t), 'expr', None, e.en if c == 0x3A else None)
        if t in (0x32, 0x33):
            self.obj()
            a = self.expr()
            b = self.expr()
            return E('(%s %s %s)' % (a.t, '==' if t == 0x32 else '!=', b.t))
        if t == 0x36:
            m = self.obj()
            e = self.expr()
            return E('%s.%s' % (e.t, self.objname(m)), 'var', m, prop_enum(m))
        if t == 0x18:
            self.w()
            return self.expr()
        raise DecompileError('token 0x%02x at %d in expression' % (t, at))

    def class_of(self, e, meta):
        """The class an object expression refers to (or, for a class context, the class it names), if known."""
        p = e.a if e.k in ('var', 'objconst', 'dyncast', 'call') else None
        if p is None or isinstance(p, tuple):
            return None
        if p.cls == 'Class':
            return p
        if p.cls == 'ObjectProperty':
            return self.world.resolve(p.pkgname, p.f['propertyclass'])
        if p.cls == 'ClassProperty':
            return self.world.resolve(p.pkgname, p.f['metaclass'])
        return None

    def const_named(self, ctx, m):
        p = ctx.a if ctx.k in ('var', 'objconst', 'dyncast') else None
        if p is None or isinstance(p, tuple):
            raise DecompileError('const access through %s' % ctx.t)
        if p.cls == 'Class':
            c = p
        elif p.cls == 'ObjectProperty':
            c = self.world.resolve(p.pkgname, p.f['propertyclass'])
        elif p.cls == 'ClassProperty':
            c = self.world.resolve(p.pkgname, p.f['metaclass'])
        else:
            raise DecompileError('const access through %s' % ctx.t)
        want = m.t
        while c is not None:
            for k in c.children():
                if k.cls == 'Const':
                    v = k.f['value'].strip()
                    same = v == want
                    try:
                        iv = int(v, 0) & 0xFFFFFFFF      # appAtoi wraps like MSVC (UnAnsi.cpp)
                        same = same or str(iv - (1 << 32) if iv >= 1 << 31 else iv) == want
                    except ValueError:
                        pass
                    try:    # an int constant used where a float is wanted compiles to a FloatConst
                        same = same or struct.pack('<f', float(v)) == struct.pack('<f', float(want))
                    except ValueError:
                        pass
                    if same:
                        return k.name
            c = c.ref('super')
        raise DecompileError('no const %s in the class of %s' % (want, ctx.t))

    def objconst(self, o):
        if o is None:
            return 'None'
        if isinstance(o, tuple):
            self.dec.need_package(o[1])
            return "%s'%s'" % (o[2], o[1])
        cls = o.cls
        return "%s'%s'" % (cls, o.key)


# --- statements: structured control flow -------------------------------------------------------------------------
class Structurer:
    def __init__(self, code, stmts, labels, in_state):
        self.code, self.st, self.labels, self.in_state = code, stmts, labels, in_state
        self.at = {s[0]: k for k, s in enumerate(stmts)}
        self.gotos = []
        self.ret_enum = None

    def idx(self, off):
        if off in self.at:
            return self.at[off]
        if off == (self.st[-1][1] if self.st else 0):
            return len(self.st)
        raise DecompileError('jump into the middle of a statement (%d)' % off)

    def off(self, k):
        return self.st[k][0] if k < len(self.st) else (self.st[-1][1] if self.st else 0)

    def block(self, lo, hi, ctx, ind):
        out = []
        k = lo
        last_expr = None
        while k < hi:
            at, end, kind, pl = self.st[k]
            for name, loff in reversed(self.labels):     # the label list is built by prepending
                if loff == at:
                    out.append('%s:' % name)
            # loops: the farthest backward jump to this statement
            back = None
            for j in range(hi - 1, k - 1, -1):
                s = self.st[j]
                if s[2] == 'jump' and s[3] == at or s[2] == 'jumpifnot' and s[3][0] == at:
                    back = j
                    break
            pad = '\t' * ind
            if back is not None:
                bs = self.st[back]
                if kind == 'jumpifnot' and pl[0] > at and self.idx(pl[0]) - 1 == back and bs[2] == 'jump':
                    t = self.idx(pl[0])
                    inc = back - 1
                    if inc > k and self.st[inc][2] == 'expr' and last_expr is not None and last_expr[0] == k - 1 \
                            and last_expr[1] == len(out) - 1 and any(
                                self.st[x][2] == 'jump' and self.st[x][3] == self.st[inc][0] for x in range(k + 1, inc)):
                        # `for (init; cond; inc)` whose `continue` jumps to the increment (a `while` would jump
                        # to the back jump)
                        init = out.pop().strip().rstrip(';')
                        body = self.block(k + 1, inc, {'brk': pl[0], 'cont': self.st[inc][0], 'iter': False}, ind + 1)
                        out.append('%sfor (%s; %s; %s)' % (pad, init, pl[1].t, self.st[inc][3].t))
                        out.append('%s{' % pad)
                        out += body
                        out.append('%s}' % pad)
                        k = t
                        continue
                    body = self.block(k + 1, back, {'brk': pl[0], 'cont': bs[0], 'iter': False}, ind + 1)
                    out.append('%swhile (%s)' % (pad, pl[1].t))
                    out.append('%s{' % pad)
                    out += body
                    out.append('%s}' % pad)
                    k = t
                    continue
                if bs[2] == 'jumpifnot':
                    body = self.block(k, back, {'brk': bs[1], 'cont': bs[0], 'iter': False}, ind + 1)
                    out.append('%sdo' % pad)
                    out.append('%s{' % pad)
                    out += body
                    out.append('%s} until (%s);' % (pad, bs[3][1].t))
                    k = back + 1
                    continue
                body = self.block(k, back, {'brk': bs[1], 'cont': bs[0], 'iter': False}, ind + 1)
                out.append('%sdo' % pad)
                out.append('%s{' % pad)
                out += body
                out.append('%s}' % pad)
                k = back + 1
                continue
            if kind == 'jumpifnot':
                t = self.idx(pl[0])
                if t > hi:
                    raise DecompileError('if jumps out of its block at %d' % at)
                els = None
                if t - 1 > k and self.st[t - 1][2] == 'jump':
                    e = self.st[t - 1][3]
                    # a jump to the if's own end is a break of an inner switch, not an empty else
                    if pl[0] < e <= self.off(hi) and not (e == ctx.get('brk') and e > self.off(hi)):
                        els = self.idx(e)
                out.append('%sif (%s)' % (pad, pl[1].t))
                out.append('%s{' % pad)
                out += self.block(k + 1, t - 1 if els is not None else t, ctx, ind + 1)
                out.append('%s}' % pad)
                if els is not None:
                    flat = self.block(t, els, ctx, ind)
                    tops = [l for l in flat if l.startswith(pad) and not l.startswith(pad + '\t')
                            and not l.startswith('\x01')]
                    if flat and flat[0].startswith(pad + 'if (') and all(
                            l in (pad + '{', pad + '}', pad + 'else') or l.startswith(pad + 'else if (') or l == flat[0]
                            for l in tops):
                        # `else if`: one nesting level for the whole chain (the compiler allows 16)
                        out.append('%selse %s' % (pad, flat[0][len(pad):]))
                        out += flat[1:]
                    else:
                        out.append('%selse' % pad)
                        out.append('%s{' % pad)
                        out += self.block(t, els, ctx, ind + 1)
                        out.append('%s}' % pad)
                    k = els
                else:
                    k = t
                continue
            if kind == 'jump':
                if pl == ctx.get('brk') and not ctx.get('iterbrk'):
                    out.append('%sbreak;' % pad)
                elif pl == ctx.get('cont') and not ctx.get('iter'):
                    out.append('%scontinue;' % pad)
                elif ctx.get('iter') and pl == ctx.get('brk'):
                    out.append('%sbreak;' % pad)
                else:
                    raise DecompileError('unstructured jump at %d to %d' % (at, pl))
                k += 1
                continue
            if kind == 'iternext':
                if ctx.get('iter') and k + 1 < hi and self.st[k + 1][2] == 'jump' and self.st[k + 1][3] == ctx['brk']:
                    out.append('%scontinue;' % pad)
                    k += 2
                    continue
                raise DecompileError('stray IteratorNext at %d' % at)
            if kind == 'iterpop':
                j = k
                while j < hi and self.st[j][2] == 'iterpop':
                    j += 1
                if j < hi and self.st[j][2] == 'return':
                    k = j
                    continue
                raise DecompileError('stray IteratorPop at %d' % at)
            if kind == 'iterator':
                e, w = pl
                t = self.idx(w)
                if self.st[t - 1][2] != 'iternext' or self.st[t][2] != 'iterpop':
                    raise DecompileError('foreach shape at %d' % at)
                out.append('%sforeach %s' % (pad, e.t))
                out.append('%s{' % pad)
                out += self.block(k + 1, t - 1, {'brk': w, 'cont': None, 'iter': True}, ind + 1)
                out.append('%s}' % pad)
                k = t + 1
                continue
            if kind == 'switch':
                k = self.switch(k, hi, ctx, ind, out)
                continue
            if kind == 'return':
                out.append('%sreturn%s;' % (pad, ' ' + bc(pl, self.ret_enum).t if pl is not None else ''))
            elif kind == 'stop':
                out.append('%sstop;' % pad)
            elif kind == 'gotolabel':
                out.append('%sgoto %s;' % (pad, pl.t))
            elif kind == 'assert':
                # the compiler stores the source line in the bytecode: the line is padded to it at the end
                out.append('\x01%d\x01%sassert(%s);' % (pl.a, pad, pl.t))
            elif kind in ('nop', 'destroyret', 'labeltable'):
                pass
            elif kind == 'expr':
                out.append('%s%s;' % (pad, pl.t))
                last_expr = (k, len(out) - 1)
            elif kind == 'case':
                raise DecompileError('stray case at %d' % at)
            else:
                raise DecompileError('statement %s at %d' % (kind, at))
            k += 1
        return out

    def switch(self, k, hi, ctx, ind, out):
        pad = '\t' * ind
        at, end, kind, e = self.st[k]
        cases = []
        c = k + 1
        while True:
            if c >= hi or self.st[c][2] != 'case':
                raise DecompileError('switch without case at %d' % at)
            w, ce = self.st[c][3]
            cases.append(c)
            if w == 0xFFFF:
                break
            c = self.idx(w)
        last = cases[-1]
        # the switch's end: what its breaks jump to
        brk = None
        for j in range(k + 1, last):
            s = self.st[j]
            if s[2] == 'jump' and s[3] > self.st[last][0] and s[3] <= self.off(hi):
                brk = s[3] if brk is None else min(brk, s[3])
        default_body = last + 1 < hi
        if brk is None:
            brk = self.off(hi) if default_body else self.off(last + 1)
        if not default_body:
            brk = max(brk, self.off(last + 1))
        endk = self.idx(brk)
        has_default = endk > last + 1
        out.append('%sswitch (%s)' % (pad, e.t))
        out.append('%s{' % pad)
        sctx = {'brk': brk, 'cont': ctx.get('cont'), 'iter': False}
        for n, c in enumerate(cases):
            w, ce = self.st[c][3]
            nxt = cases[n + 1] if n + 1 < len(cases) else endk
            if w == 0xFFFF:
                if not has_default:
                    break
                out.append('%s\tdefault:' % pad)
            else:
                out.append('%s\tcase %s:' % (pad, bc(ce, e.en).t))
            out += self.block(c + 1, nxt, sctx, ind + 2)
        out.append('%s}' % pad)
        return endk


# --- declarations ------------------------------------------------------------------------------------------------
class Decompiler:
    def __init__(self, world):
        self.world = world

    def run(self, cls, first_line):
        """The decompiled text of class Obj cls, `first_line` as line 1."""
        self.cls = cls
        self.typer = Typer(self.world, cls)
        self.errors = []
        self.loads = []
        lines = []
        kids = cls.children()
        rep = self.replication(cls, kids)
        kids = self.decl_order(kids)
        declared = set()
        for o in kids:
            if o.cls in PROPS and not (o.cls == 'DelegateProperty' and o.name.startswith('__')):
                # a variable named like a struct or enum type hides it for the declarations after it
                # (`var Matrix Matrix, GradientMatrix;` in ProjectorMaterial): such a declaration joins the
                # previous one when that declares the same type the same way
                try:
                    decl = self.vardecl(o, False)
                except (DecompileError, sd.ParseError, IndexError, KeyError, struct.error):
                    decl = None
                if decl is not None:
                    head, name = decl.rsplit(' ', 1)
                    typ = head.split(' ')[-1]
                    if typ.lower() in declared and (o.cls == 'StructProperty' or prop_enum(o) is not None):
                        prev = lines[-1] if lines and isinstance(lines[-1], str) else ''
                        if prev.startswith(head + ' ') and prev.endswith(';'):
                            lines[-1] = prev[:-1] + ', ' + name + ';'
                        else:
                            self.errors.append('%s: type %s hidden by a variable of the same name' % (o.key, typ))
                            lines.append('// DECOMPILE ERROR %s: the type %s is hidden by a variable' % (o.name, typ))
                        declared.add(o.name.lower())
                        continue
                    declared.add(o.name.lower())
            try:
                fl = self.field(o, 0)
            except (DecompileError, sd.ParseError, IndexError, KeyError, struct.error) as e:
                self.errors.append('%s: %s' % (o.key, e))
                fl = ['// DECOMPILE ERROR %s: %s' % (o.name, e)]
            if o.cls in ('Function', 'State') and fl and not fl[0].startswith('//'):
                lines.append(SOFT)
            lines += fl
        if rep:
            lines += rep
        if cls.f['cpptext'] != 'None':
            # the C++ text is stripped in the shipped file (" "); a one-line block recreates the buffer (an empty
            # one creates none: UnEdFact.cpp)
            lines += ['', 'cpptext', '{', '// stripped from the shipped package', '}']
        lines.append('')
        lines += self.defaults_block(cls)
        execs = ['#exec OBJ LOAD FILE=%s' % self.world.content[p.lower()] for p in self.loads]
        return '\n'.join(self.pad_lines([first_line, self.header(cls)] + execs + [''] + lines)) + '\n'

    def defaults_block(self, cls):
        try:
            return self.defaults(cls)
        except (DecompileError, sd.ParseError, IndexError, KeyError, struct.error) as e:
            self.errors.append('%s defaultproperties: %s' % (cls.key, e))
            return ['defaultproperties', '{', '// DECOMPILE ERROR %s' % e, '}']

    def shipped_text(self, cls, text, first_line):
        """The class's own script text (CR LF -> LF; line 1, blank in every shipped text, becomes first_line) and
        the decompiled defaultproperties block (the class factory cuts it off the stored text)."""
        self.cls = cls
        self.typer = Typer(self.world, cls)
        self.errors = []
        self.loads = []
        body = text.replace('\r\n', '\n')
        head, sep, rest = body.partition('\n')
        if '\n' in first_line or '\r' in body:
            raise DecompileError('%s: stray CR in the shipped text' % cls.key)
        # line 1 carries the identity comment: it replaces a blank line 1, or follows a non-blank one (8 UCMenu
        # texts start with their class line), so no line moves
        head = first_line if not head.strip() else head.rstrip() + ' ' + first_line
        if not rest.endswith('\n'):
            rest += '\n'
        return head + '\n' + rest + '\n'.join(self.defaults_block(cls)) + '\n'

    def pad_lines(self, lines):
        """Resolve '\x01<line>\x01' marks: blank lines are inserted so the marked line lands on <line>. SOFT lines
        (the blank line before a function or state) are kept only when every assert still fits; otherwise the file
        is written without any of them."""
        out = self._pad(lines, True)
        if out is None:
            out = self._pad(lines, False)
        if out is None:
            out = self._pad(lines, False, report=True)
        return out

    def _pad(self, lines, keep_soft, report=False):
        out = []
        for l in lines:
            if l is SOFT:
                if keep_soft:
                    out.append('')
                continue
            for part in l.split('\n'):
                if part.startswith('\x01'):
                    _, n, rest = part.split('\x01', 2)
                    n = int(n)
                    if len(out) + 1 > n:
                        if not report:
                            return None
                        self.errors.append('%s: line %d wanted for an assert, already at %d' % (self.cls.key, n, len(out) + 1))
                    while len(out) + 1 < n:
                        out.append('')
                    part = rest
                out.append(part)
        return out

    @staticmethod
    def decl_order(kids):
        # The compiler prepends every field to Children, except properties: the first property is prepended, the
        # next ones are appended after the last property (UnScrCom.cpp). So the chain reads
        #   [non-properties declared after the first var, last first] [properties in order]
        #   [non-properties declared before the first var, last first].
        # A source order that rebuilds the same chain: the tail non-properties reversed, the first var, then the
        # head non-properties reversed with each remaining var placed as soon as the types it uses exist.
        pi = [n for n, k in enumerate(kids) if k.cls in PROPS]
        if not pi:
            return list(reversed(kids))
        first, last = pi[0], pi[-1]
        props = kids[first:last + 1]
        if any(k.cls not in PROPS for k in props):
            raise DecompileError('properties not contiguous in the field chain')
        tail = list(reversed(kids[last + 1:]))
        head = list(reversed(kids[:first]))
        local = {k.key for k in kids if k.cls in ('Struct', 'Enum')}

        def deps(p):
            out = set()
            stack = [p]
            while stack:
                q = stack.pop()
                for fld in ('enum', 'struct', 'inner'):
                    r = q.ref(fld) if fld in q.f else None
                    if r is None:
                        continue
                    if fld == 'inner':
                        stack.append(r)
                    elif r.key in local:
                        out.add(r.key)
            return out
        out = tail + [props[0]]
        done = {k.key for k in out}
        pending = props[1:]
        for k in head:
            if k.cls in ('Function', 'State') and pending:
                out += pending
                pending = []
            out.append(k)
            done.add(k.key)
            ready = [p for p in pending if deps(p) <= done]
            if ready and all(deps(p) <= done for p in pending[:len(ready)]):
                pass
            # keep property order: flush the longest ready prefix
            n = 0
            while n < len(pending) and deps(pending[n]) <= done:
                n += 1
            out += pending[:n]
            pending = pending[n:]
        return out + pending

    def header(self, cls):
        f = cls.f
        sup = cls.ref('super')
        words = ['class', cls.name]
        if sup is not None:
            words += ['extends', sup.name]
        sf = sup.f if sup is not None else {'classflags': 0, 'within': 'None', 'configname': 'None', 'hidecategories': []}
        if sup is not None and f['within'] != sf['within'] and f['within'] not in ('None', 'Core.Object'):
            words += ['within', f['within'].split('.')[-1]]
        fl = f['classflags']
        sfl = sf['classflags']
        if cls.flags & RF_Native:
            words.append('native')
        for bit, w in [(CLASS_Abstract, 'abstract'), (CLASS_NoExport, 'noexport'),
                       (CLASS_NativeReplication, 'nativereplication'), (CLASS_PerObjectConfig, 'perobjectconfig'),
                       (CLASS_Transient, 'transient'), (CLASS_SafeReplace, 'safereplace'),
                       (CLASS_Deprecated, 'deprecated'), (CLASS_Poolable, 'poolable'), (CLASS_AutoPrecache, 'precache'),
                       (CLASS_NativeSkipAutoLoad, 'nativeskipautoload'), (CLASS_FullDefaultsBase, 'fulldefaultsbase')]:
            if fl & bit and not (sfl & bit and bit not in (CLASS_Abstract, CLASS_NoExport, CLASS_NativeReplication,
                                                             CLASS_NativeSkipAutoLoad, CLASS_FullDefaultsBase)):
                words.append(w)
        for bit, on, off in [(CLASS_Placeable, 'placeable', 'notplaceable'),
                             (CLASS_EditInlineNew, 'editinlinenew', 'noteditinlinenew'),
                             (CLASS_CollapseCategories, 'collapsecategories', 'dontcollapsecategories'),
                             (CLASS_AutoInstancedProps, 'instanced', None)]:
            if fl & bit and not sfl & bit:
                words.append(on)
            elif sfl & bit and not fl & bit and off:
                words.append(off)
        if sup is not None and f['configname'] != sf['configname'] and f['configname'] != 'None':
            words.append('config(%s)' % f['configname'])
        hid = [h for h in f['hidecategories'] if h not in sf['hidecategories']]
        shown = [h for h in sf['hidecategories'] if h not in f['hidecategories']]
        for d in self.typer.deps:
            words.append('dependson(%s)' % d.name)
        if hid:
            words.append('hidecategories(%s)' % ','.join(hid))
        if shown:
            words.append('showcategories(%s)' % ','.join(shown))
        return ' '.join(words) + ';'

    def replication(self, cls, kids):
        script = cls.f['script']
        if not script:
            return []
        groups = {}
        for o in kids:
            if o.cls in PROPS and o.f['flags'] & 0x20:
                groups.setdefault(o.f['repoffset'], []).append((o.name, None))
            elif o.cls == 'Function' and o.f['funcflags'] & FUNC_Net and o.f['repoffset'] != 0xFFFF:
                # (0xFFFF: an override of a replicated function inherits the net flags, UnScrCom.cpp)
                groups.setdefault(o.f['repoffset'], []).append((o.name, o.f['funcflags']))
        out = ['', 'replication', '{']
        for off in sorted(groups):
            c = Code(self, cls, script)
            c.i = off
            cond = c.expr()
            names = groups[off]
            rel = any(ff & FUNC_NetReliable for n, ff in names if ff is not None)
            toall = any(ff & FUNC_Multicast for n, ff in names if ff is not None)
            out.append('\t%s%s if (%s)' % ('reliable' if rel else 'unreliable', ' toall' if toall else '', cond.t))
            out.append('\t\t%s;' % ', '.join(n for n, ff in names))
        out.append('}')
        return out

    def field(self, o, ind, in_struct=False):
        pad = '\t' * ind
        c = o.cls
        if c == 'Const':
            return ['%sconst %s = %s;' % (pad, o.name, o.f['value'])]
        if c == 'Enum':
            return ['%senum %s' % (pad, o.name), '%s{' % pad] + ['%s\t%s,' % (pad, n) for n in o.f['names']] + ['%s};' % pad]
        if c == 'Struct':
            words = ['struct']
            if o.flags & RF_Native:
                words.append('native')
            words.append(o.name)
            sup = o.ref('super')
            if sup is not None:
                words += ['extends', self.typer.scoped(sup)]
            lines = ['%s%s' % (pad, ' '.join(words)), '%s{' % pad]
            # enums and structs declared inside a struct only exist as inline `var enum X {...} Name;` forms
            inline = {m.key: m for m in o.children() if m.cls in ('Enum', 'Struct')}
            for m in o.children():
                if m.key in inline:
                    continue
                if m.cls == 'ByteProperty' and m.ref('enum') is not None and m.ref('enum').key in inline:
                    e = inline.pop(m.ref('enum').key)
                    decl = self.vardecl(m, False)
                    head, name = decl.rsplit(' ', 1)
                    head = head.rsplit(' ', 1)[0]
                    lines.append('%s\t%s enum %s { %s } %s;' % (pad, head, e.name, ', '.join(e.f['names']), name))
                    continue
                lines += self.field(m, ind + 1, True)
            if inline:
                raise DecompileError('struct %s: nested %s without a variable' % (o.name, list(inline)))
            lines.append('%s};' % pad)
            return lines
        if c in PROPS:
            if c == 'DelegateProperty' and o.name.startswith('__'):
                return []
            return ['%s%s;' % (pad, self.vardecl(o, False))]
        if c == 'Function':
            return self.function(o, ind)
        if c == 'State':
            return self.state(o, ind)
        raise DecompileError('field kind %s' % c)

    def vardecl(self, o, in_func):
        f = o.f
        words = []
        if not in_func:
            cat = f['category']
            if f['flags'] & CPF_Edit and not f['flags'] & CPF_Automated:
                owner = o.outer()
                words.append('var(%s)' % ('' if cat == owner.name or cat == 'None' else cat))
            else:
                words.append('var')
        words += var_flags(o, in_func)
        words.append(self.typer.type_of(o))
        name = o.name
        if f['arraydim'] > 1:
            name += '[%d]' % f['arraydim']
        words.append(name)
        return ' '.join(words)

    def function(self, o, ind):
        pad = '\t' * ind
        f = o.f
        ff = f['funcflags']
        words = []
        if ff & FUNC_Native:
            words.append('native(%d)' % f['inative'] if f['inative'] else 'native')
        for bit, w in [(FUNC_Static, 'static'), (FUNC_Final, 'final'), (FUNC_Simulated, 'simulated'),
                       (FUNC_Iterator, 'iterator'), (FUNC_Latent, 'latent'), (FUNC_Singular, 'singular'),
                       (FUNC_Exec, 'exec'), (FUNC_Private, 'private'), (FUNC_Protected, 'protected'),
                       (FUNC_DebugOnly, 'debugonly')]:
            if ff & bit:
                words.append(w)
        if ff & FUNC_Operator:
            parms0 = [p for p in o.children() if p.cls in PROPS and p.f['flags'] & CPF_Parm and not p.f['flags'] & CPF_ReturnParm]
            if ff & FUNC_PreOperator:
                words.append('preoperator')
            elif len(parms0) == 1:
                words.append('postoperator')
            else:
                words.append('operator(%d)' % f['operprecedence'])
        elif ff & FUNC_Delegate:
            words.append('delegate')
        elif ff & FUNC_Event:
            words.append('event')
        else:
            words.append('function')
        kids = o.children()
        parms = [p for p in kids if p.cls in PROPS and p.f['flags'] & CPF_Parm and not p.f['flags'] & CPF_ReturnParm]
        ret = [p for p in kids if p.cls in PROPS and p.f['flags'] & CPF_ReturnParm]
        locs = [p for p in kids if p.cls in PROPS and not p.f['flags'] & CPF_Parm]
        if ret:
            words.append(self.typer.type_of(ret[0]))
        name = f['friendlyname'] if f['friendlyname'] != 'None' else o.name
        sig = '%s%s %s(%s)' % (pad, ' '.join(words), name, ', '.join(self.vardecl(p, True) for p in parms))
        if not ff & FUNC_Defined:
            if ff & FUNC_Native or not f['script']:
                return [sig + ';']
        lines = [sig, '%s{' % pad]
        for p in locs:
            lines.append('%s\tlocal %s;' % (pad, self.vardecl(p, True)))
        lines += self.body(o, ind + 1, False)
        lines.append('%s}' % pad)
        return lines

    def body(self, o, ind, in_state):
        script = o.f['script']
        if not script:
            return []
        code = Code(self, o, script)
        st = code.statements()
        labels = []
        if in_state:
            for s in st:
                if s[2] == 'labeltable':
                    labels = s[3]
            # drop the implicit `stop` (and alignment nops) before the label table, or the final stop
            while st and st[-1][2] in ('labeltable', 'nop'):
                st.pop()
            if st and st[-1][2] == 'stop':
                st.pop()
        else:
            if st and st[-1][2] == 'return' and st[-1][3] is None:
                st.pop()
        s = Structurer(code, st, labels, in_state)
        if o.cls == 'Function':
            s.ret_enum = prop_enum(params_of(o)[1])
        try:
            out = s.block(0, len(st), {}, ind)
            # labels at the end of the state code (after its last statement, or a state with only `Begin:`)
            end = st[-1][1] if st else 0
            out += ['%s:' % n for n, loff in reversed(labels) if loff == end and loff not in {x[0] for x in st}]
            return out
        except (DecompileError, IndexError, KeyError, struct.error, ValueError) as e:
            self.errors.append('%s: %s' % (o.key, e))
            return ['// DECOMPILE ERROR %s' % e]

    def state(self, o, ind):
        f = o.f
        sf = f['stateflags']
        words = []
        if sf & STATE_Auto:
            words.append('auto')
        if sf & STATE_Simulated:
            words.append('simulated')       # must directly precede `state` (UnScrCom.cpp CompileDeclaration)
        words.append('state()' if sf & STATE_Editable else 'state')
        words.append(o.name)
        sup = o.ref('super')
        if sup is not None and sup.name != o.name:
            words += ['extends', sup.name]
        lines = [' '.join(words), '{']
        kids = self.decl_order(o.children())
        ign = []
        mask = f['ignoremask']
        for bit in range(64):
            if not mask >> bit & 1 and bit in PROBES:
                ign.append(PROBES[bit])
        fns = []
        prefix = True       # `ignores` is parsed before the state's functions: its functions come first
        for k in kids:
            sup = k.ref('super') if k.cls == 'Function' else None
            if prefix and k.cls == 'Function' and not k.f['funcflags'] & (FUNC_Defined | FUNC_Native) \
                    and k.f['script'] in (b'', b'\x04\x0b') and sup is not None and sup.outer().cls == 'Class' \
                    and k.f["funcflags"] == sup.f["funcflags"] & 0xA221D and k.name not in PROBES.values() \
                    and not [c for c in k.children() if c.cls in PROPS and not c.f['flags'] & CPF_Parm]:
                # `ignores X`: an empty function whose super is the class-level X (UnScrCom.cpp); an empty
                # `function X() {}` in a state would take the parent state's X as its super
                ign.append(k.name)
            else:
                if k.cls == 'Function':
                    prefix = False
                fns.append(k)
        if ign:
            lines.append('\tignores %s;' % ', '.join(ign))
        for k in fns:
            if k.cls == 'Function':
                lines.append(SOFT)
            lines += self.field(k, 1)
        b = self.body(o, 0, True)
        if b:
            lines.append('Begin:' if False else '')
            lines += b
        lines.append('}')
        return lines

    # --- default properties -------------------------------------------------------------------------------------
    def defaults(self, cls):
        tags = cls.f['@defaults']
        lines = ['defaultproperties', '{']
        subs = [o for o in self.world.objs[cls.pkgname] if o.outer() is cls and o.cls not in PROPS
                and o.cls not in ('Function', 'State', 'Const', 'Enum', 'Struct', 'TextBuffer')]
        for so in subs:
            lines.append('\tBegin Object Class=%s Name=%s' % (so.cls, so.name))
            sc = self.world.index.get(self.class_key(so).lower())
            for t in so.f.get('@props', []):
                if t[0].startswith('__') and t[0].endswith('__Delegate') and len(t[5]) == 8 and \
                        self.world.resolve_index(so.pkgname, struct.unpack('<i', t[5][:4])[0]) is so:
                    continue    # bound to the subobject itself: comes from its class's defaults when instanced
                lines.append('\t\t' + self.tagtext(t, sc, so.pkgname))
            lines.append('\tEnd Object')
        for t in tags:
            lines.append('\t' + self.tagtext(t, cls, cls.pkgname))
        lines.append('}')
        return lines

    def class_key(self, o):
        c = o.pkg.exports[o.idx]['class']
        if c < 0:
            return o.pkg.import_path(-c - 1)
        return o.pkgname + '.' + o.pkg.export_path(c - 1)

    def plain_super(self, code_owner, name):
        """The function `Super.name` resolves to from code in code_owner (a function or state), as the compiler
        does: the nearest enclosing struct with an inheritance super, then its super's field search."""
        scope = code_owner.outer() if code_owner.cls == 'Function' else code_owner
        while scope is not None and scope.ref('super') is None:
            scope = scope.outer()
        if scope is None:
            return None
        scope = scope.ref('super')
        lname = name.lower()
        s0 = scope
        while s0 is not None:
            for k in s0.children():
                if k.cls == 'Function' and k.name.lower() == lname:
                    return k
            s0 = s0.ref('super')
        if scope.cls == 'State':
            c = scope.outer()
            while c is not None:
                for k in c.children():
                    if k.cls == 'Function' and k.name.lower() == lname:
                        return k
                c = c.ref('super')
        return None

    def need_package(self, path):
        """A reference into a content package: the class must load it (`#exec OBJ LOAD`; the original may have
        relied on another class loading it first, which a decompiler cannot see)."""
        top = path.split('.')[0]
        if top.lower() in {p.lower() for p in sd.PACKAGES}:
            return
        if top.lower() not in self.world.content:
            raise DecompileError('%s references %s: no content package %s under the content folder' % (
                self.cls.key, path, top))
        if top not in self.loads:
            self.loads.append(top)

    def find_function(self, name, start=None):
        """A function called by name (virtual call) from the class being decompiled (or through a context of
        class `start`), for its parameter types."""
        c = start or self.cls
        lname = name.lower()
        while c is not None:
            for k in c.children():
                if k.cls == 'Function' and k.name.lower() == lname:
                    return k
            c = c.ref('super')
        w = self.cls.f['within']
        if w != 'None':
            c = self.world.resolve(self.cls.pkgname, w)
            while c is not None:
                for k in c.children():
                    if k.cls == 'Function' and k.name.lower() == lname:
                        return k
                c = c.ref('super')
        return None

    def findprop(self, cls, name):
        c = cls
        while c is not None:
            for k in c.children():
                if k.cls in PROPS and k.name.lower() == name.lower():
                    return k
            c = c.ref('super')
        return None

    def tagtext(self, t, cls, pkgname):
        key, typ, sz, idx, item, value, boolval = t
        p = self.findprop(cls, key) if cls is not None else None
        if p is None:
            raise DecompileError('default %s: property not found' % key)
        val = self.value(p, typ, value, boolval, pkgname, item)
        if key.startswith('__') and key.endswith('__Delegate'):
            key = key[2:-10]
        if p.cls == 'ArrayProperty':
            # the whole array at once: indexed lines would keep a longer inherited array's tail
            # emptied first (indexed lines alone would keep a longer inherited array's tail; one long line hits the
            # importer's line length limit)
            return '\n\t'.join(['%s=()' % key] + ['%s(%d)=%s' % (key, n, v) for n, v in enumerate(val)])
        if p.f['arraydim'] > 1 or idx:
            return '%s(%d)=%s' % (key, idx, val)
        return '%s=%s' % (key, val)

    def value(self, p, typ, value, boolval, pkgname, item=None):
        r = sd.Reader(self.world.pkgs[pkgname], value)
        v = self.item(p, r, pkgname, typ, boolval)
        return v

    def item(self, p, r, pkgname, typ=None, boolval=None):
        c = p.cls
        if c == 'BoolProperty':
            return 'True' if boolval else 'False'
        if c == 'ByteProperty':
            b = r.u8()
            e = p.ref('enum')
            if e is not None and b < len(e.f['names']):
                return e.f['names'][b]
            return str(b)
        if c == 'IntProperty':
            return str(r.i32())
        if c == 'FloatProperty':
            return fmt_float(r.f32())
        if c == 'NameProperty':
            n = r.name()
            return n if n != 'None' else "None"
        if c == 'StrProperty':
            return esc_str(r.fstring())
        if c in ('ObjectProperty', 'ClassProperty'):
            o = self.world.resolve_index(pkgname, r.i32())
            if o is None:
                return 'None'
            if isinstance(o, tuple):
                self.need_package(o[1])
                return "%s'%s'" % (o[2], o[1])
            if o.outer() is not None and o.outer().cls == 'Class' and o.cls != 'Class' and o.pkgname == pkgname:
                return "%s'%s'" % (o.cls, o.name)
            return "%s'%s'" % (o.cls, o.key)
        if c == 'StructProperty':
            st = p.ref('struct')
            if st.name in ('Vector', 'Rotator', 'Color'):
                parts = []
                for m in st.children():
                    if m.cls in PROPS:
                        parts.append('%s=%s' % (m.name, self.item(m, r, pkgname)))
                return '(%s)' % ','.join(parts)
            tags = sd.tagged(r)
            parts = []
            for t in tags:
                m = self.findprop(st, t[0])
                if m is None:
                    raise DecompileError('struct member %s' % t[0])
                v = self.item(m, sd.Reader(self.world.pkgs[pkgname], t[5]), pkgname, t[1], t[6])
                if m.cls == 'ArrayProperty':
                    parts.append('%s=(%s)' % (m.name, ','.join(v)))
                elif m.f['arraydim'] > 1 or t[3]:
                    # a static array inside a struct value is written with brackets (`LabelPosn[0]=(...)`); the
                    # parenthesised form is the top-level one only (UC2ControllerLayoutMenu.Labels)
                    parts.append('%s[%d]=%s' % (m.name, t[3], v))
                else:
                    parts.append('%s=%s' % (m.name, v))
            return '(%s)' % ','.join(parts)
        if c == 'ArrayProperty':
            n = r.i32()
            inner = p.ref('inner')
            return [self.item(inner, r, pkgname) for _ in range(n)]
        if c == 'DelegateProperty':
            o = self.world.resolve_index(pkgname, r.i32())
            n = r.name()
            if n == 'None':
                return 'None'
            if o is not None and not isinstance(o, tuple) and o.cls != 'Class' and o.outer() is not None \
                    and o.outer().cls == 'Class':
                return '%s.%s' % (o.name, n)
            return n
        raise DecompileError('default of %s' % c)


def usage(msg=None):
    if msg:
        sys.stderr.write('uc-decompile: %s\n' % msg)
    sys.stderr.write('usage: uc-decompile.py <System>/<Pkg>.u --out <dir> [--shipped-text] [--patches ScriptPatches.h] '
                     '[--content <root>] [--class Name ...]\n')
    sys.exit(2)


def main():
    a = sys.argv[1:]
    if not a or a[0] in ('-h', '--help'):
        print(__doc__)
        sys.exit(0 if a else 2)
    upath, outdir, content, patches_path, shipped_text, only = None, None, None, None, False, []
    i = 0
    while i < len(a):
        if a[i] == '--out' and i + 1 < len(a):
            outdir = a[i + 1]
            i += 2
        elif a[i] == '--content' and i + 1 < len(a):
            content = a[i + 1]
            i += 2
        elif a[i] == '--patches' and i + 1 < len(a):
            patches_path = a[i + 1]
            i += 2
        elif a[i] == '--shipped-text':
            shipped_text = True
            i += 1
        elif a[i] == '--class':
            i += 1
            while i < len(a) and not a[i].startswith('--'):
                only.append(a[i])
                i += 1
        elif upath is None and not a[i].startswith('--'):
            upath = a[i]
            i += 1
        else:
            usage('unexpected argument %s' % a[i])
    if upath is None or outdir is None:
        usage('a package and --out are required')
    if not os.path.isfile(upath) or not upath.endswith('.u'):
        usage('%s: not a .u file' % upath)
    sysdir = os.path.dirname(os.path.abspath(upath))
    pkgname = os.path.basename(upath)[:-2]
    canon = {p.lower(): p for p in sd.PACKAGES}
    if pkgname.lower() not in canon:
        usage('%s is not one of the 17 script packages (%s)' % (pkgname, ', '.join(sd.PACKAGES)))
    pkgname = canon[pkgname.lower()]
    patches = None
    try:
        if patches_path:
            patches = sd.parse_patches(patches_path)
            psha = hashlib.sha256(open(patches_path, 'rb').read()).hexdigest()[:12]
        world = World(sysdir, content, patches)
    except (OSError, ValueError, DecompileError, sd.ParseError) as e:
        sys.stderr.write('uc-decompile: %s\n' % e)
        sys.exit(2)
    first = '// decompiled by uc-decompile %s from %s.u %s' % (VERSION, pkgname, world.sha[pkgname])
    if patches_path:
        first += ' + ScriptPatches.h %s' % psha
    os.makedirs(outdir, exist_ok=True)
    dec = Decompiler(world)
    n = bad = 0
    texts = {}
    if shipped_text:
        pk = world.pkgs[pkgname]
        for o in world.objs[pkgname]:
            if o.cls == 'TextBuffer' and o.path.endswith('.ScriptText') and o.path.count('.') == 1:
                t = o.f['text']
                if t.strip():
                    texts[o.path.split('.')[0]] = t
    # a conformed rebuild may hold empty export slots (name None, no data: a local the newer source dropped)
    classes = [o for o in world.objs[pkgname] if o.cls == 'Class' and o.pkg.exports[o.idx]['size']]
    names = [o.name for o in classes]
    for c in only:
        if c not in names:
            usage('no class %s in %s' % (c, pkgname))
    for o in classes:
        if only and o.name not in only:
            continue
        n += 1
        try:
            if o.name in texts:
                text = dec.shipped_text(o, texts[o.name], '// shipped text of %s from %s.u %s, defaultproperties by '
                                        'uc-decompile %s' % (o.name, pkgname, world.sha[pkgname], VERSION))
            else:
                text = dec.run(o, first)
        except Exception as e:      # noqa: BLE001 - record, write a marker file, go on with the next class
            import traceback
            dec.errors = ['CRASH %s: %s' % (type(e).__name__, ' | '.join(traceback.format_exc().strip().splitlines()[-3:]))]
            text = first + '\n// DECOMPILER CRASH\n'
        with open(os.path.join(outdir, o.name + '.uc'), 'wb') as fh:
            fh.write(text.encode('latin-1'))
        if dec.errors:
            bad += 1
            for e in dec.errors:
                sys.stderr.write('uc-decompile: ERROR %s: %s\n' % (o.name, e))
    print('%s: %d classes written (%d shipped text, %d decompiled), %d with decompile errors' % (
        pkgname, n, len([c for c in texts if not only or c in only]), n - len([c for c in texts if not only or c in only]), bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
