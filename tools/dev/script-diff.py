#!/usr/bin/env python3
"""Compare recompiled UnrealScript packages with the shipped ones, object by object (M14).

    tools/dev/script-diff.py <shipped System dir> <recompiled System dir> [--patches ScriptPatches.h]
                             [--details <file>] [--functions <file>] [--patch-table <file>] [--packages A,B,...]

Both directories hold the 17 EditPackages .u. The recompiled packages were saved conforming to the shipped ones
(`ucc-bin make` conforms against ../GUIRes/<Pkg>.u), so names, imports and exports of the shipped file come first
and in the same order, and object references are the same indices in both files; what the newer source adds is
appended. Every export is parsed with the v151 serialisers of Source/Core (UObject/UField/UStruct/UFunction/UState/
UClass/UProperty and subclasses/UConst/UEnum/UTextBuffer; script bytecode token by token as UStruct::SerializeExpr
reads it; tagged default properties as FPropertyTag), the parse must consume each export exactly, and the two
parses are compared field by field with references resolved to object paths.

Differences that are explained, and how each is recognised (docs/porting/analysis/scripts.md has the evidence):
  stripsource   the shipped packages went through `ucc editor.stripsource` after `make`: ScriptText/CppText
                text buffers hold " " (Editor.u was skipped by that commandlet), and loading for it set
                CLASS_FullDefaults (0x08000000) in every class's flags;
  patch         a function or state whose recompiled bytecode is byte-for-byte the bytecode of a
                ScriptPatches.h patch (the patch exists because the shipped .u is older than the source);
  patch-object  objects, names and imports that ScriptPatches.h appends, compared with what was appended here.
Everything else is a difference and is listed.

One summary line per package on stdout; per-object detail (and a bytecode listing for every function that
differs) in --details.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upkg151 import Package  # noqa: E402

PACKAGES = ['Core', 'Engine', 'UCAnnouncements', 'Editor', 'UnrealEd', 'IpDrv', 'UWeb', 'GamePlay', 'UnrealGame',
            'UCElements', 'UCGameplay', 'UCEffects', 'UCCharacters', 'UCInventory', 'UCMutators', 'GUI', 'UCMenu']

RF_HasStack = 0x02000000
CPF_Net = 0x20
FUNC_Net = 0x40
CLASS_FullDefaults = 0x08000000
PROPERTY_CLASSES = {'ByteProperty', 'IntProperty', 'BoolProperty', 'FloatProperty', 'ObjectProperty',
                    'NameProperty', 'DelegateProperty', 'ClassProperty', 'ArrayProperty', 'StructProperty',
                    'StrProperty', 'MapProperty', 'FixedArrayProperty', 'PointerProperty'}
TAG_TYPES = {1: 'Byte', 2: 'Int', 3: 'Bool', 4: 'Float', 5: 'Object', 6: 'Name', 7: 'Delegate', 8: 'Class',
             9: 'Array', 10: 'Struct', 11: 'Vector', 12: 'Rotator', 13: 'Str', 14: 'Map', 15: 'FixedArray'}

# EExprToken (Source/Core/Inc/UnStack.h)
EX = {0x00: 'LocalVariable', 0x01: 'InstanceVariable', 0x02: 'DefaultVariable', 0x04: 'Return', 0x05: 'Switch',
      0x06: 'Jump', 0x07: 'JumpIfNot', 0x08: 'Stop', 0x09: 'Assert', 0x0A: 'Case', 0x0B: 'Nothing',
      0x0C: 'LabelTable', 0x0D: 'GotoLabel', 0x0E: 'DestroyReturnValue', 0x0F: 'Let', 0x10: 'DynArrayElement',
      0x11: 'New', 0x12: 'ClassContext', 0x13: 'MetaCast', 0x14: 'LetBool', 0x16: 'EndFunctionParms',
      0x17: 'Self', 0x18: 'Skip', 0x19: 'Context', 0x1A: 'ArrayElement', 0x1B: 'VirtualFunction',
      0x1C: 'FinalFunction', 0x1D: 'IntConst', 0x1E: 'FloatConst', 0x1F: 'StringConst', 0x20: 'ObjectConst',
      0x21: 'NameConst', 0x22: 'RotationConst', 0x23: 'VectorConst', 0x24: 'ByteConst', 0x25: 'IntZero',
      0x26: 'IntOne', 0x27: 'True', 0x28: 'False', 0x29: 'NativeParm', 0x2A: 'NoObject', 0x2C: 'IntConstByte',
      0x2D: 'BoolVariable', 0x2E: 'DynamicCast', 0x2F: 'Iterator', 0x30: 'IteratorPop', 0x31: 'IteratorNext',
      0x32: 'StructCmpEq', 0x33: 'StructCmpNe', 0x34: 'UnicodeStringConst', 0x35: 'RangeConst',
      0x36: 'StructMember', 0x37: 'DynArrayLength', 0x38: 'GlobalFunction', 0x39: 'PrimitiveCast',
      0x40: 'DynArrayInsert', 0x41: 'DynArrayRemove', 0x42: 'DebugInfo', 0x43: 'DelegateFunction',
      0x44: 'DelegateProperty', 0x45: 'LetDelegate', 0x46: 'DynArrayShuffle', 0x47: 'Conditional',
      0x48: 'FinalDelegate', 0x49: 'LocalOutVariable', 0x50: 'DynArrayAppendArray'}
EX_EndFunctionParms = 0x16
EX_DebugInfo = 0x42
EX_ExtendedNative = 0x60
EX_FirstNative = 0x70


class ParseError(Exception):
    pass


class Reader:
    def __init__(self, pkg, data):
        self.pkg, self.d, self.p = pkg, data, 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise ParseError('read past end (%d+%d > %d)' % (self.p, n, len(self.d)))
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack('<H', self.take(2))[0]

    def i32(self):
        return struct.unpack('<i', self.take(4))[0]

    def u32(self):
        return struct.unpack('<I', self.take(4))[0]

    def u64(self):
        return struct.unpack('<Q', self.take(8))[0]

    def f32(self):
        return struct.unpack('<f', self.take(4))[0]

    def name(self):
        i = self.i32()
        if 0 <= i < len(self.pkg.names):
            return self.pkg.names[i]
        raise ParseError('bad name index %d' % i)

    def ref(self):
        i = self.i32()
        try:
            return self.pkg.ref_name(i)
        except IndexError:
            raise ParseError('bad object index %d' % i)

    def fstring(self):
        n = self.i32()
        if n < 0 or n > 10000000:
            raise ParseError('bad string length %d' % n)
        return self.take(n).rstrip(b'\0').decode('latin-1')

    def done(self):
        return self.p == len(self.d)


# ---------------------------------------------------------------------------------------------------------------
# Bytecode (UStruct::SerializeExpr, Source/Core/Src/UnClass.cpp)
# ---------------------------------------------------------------------------------------------------------------
class Disasm:
    def __init__(self, pkg, script):
        self.pkg, self.s, self.i, self.out = pkg, script, 0, []

    def b(self):
        v = self.s[self.i]
        self.i += 1
        return v

    def w(self):
        v = struct.unpack_from('<H', self.s, self.i)[0]
        self.i += 2
        return v

    def int_(self):
        v = struct.unpack_from('<i', self.s, self.i)[0]
        self.i += 4
        return v

    def flt(self):
        v = struct.unpack_from('<f', self.s, self.i)[0]
        self.i += 4
        return v

    def ref(self):
        return self.pkg.ref_name(self.int_())

    def nm(self):
        i = self.int_()
        return self.pkg.names[i] if 0 <= i < len(self.pkg.names) else '<name %d>' % i

    def emit(self, at, text):
        self.out.append((at, text))

    def debuginfo(self):
        # HANDLE_OPTIONAL_DEBUG_INFO: an EX_DebugInfo with version 100 may follow a call
        if self.i < len(self.s) and self.s[self.i] == EX_DebugInfo and self.i + 5 <= len(self.s):
            if struct.unpack_from('<i', self.s, self.i + 1)[0] == 100:
                self.expr()

    def parms(self):
        while self.expr() != EX_EndFunctionParms:
            pass
        self.debuginfo()

    def expr(self):
        at = self.i
        t = self.b()
        if t >= EX_FirstNative:
            self.emit(at, 'Native %d' % t)
            self.parms()
            return t
        if t >= EX_ExtendedNative:
            n = ((t - EX_ExtendedNative) << 8) + self.b()
            self.emit(at, 'Native %d' % n)
            self.parms()
            return t
        name = EX.get(t)
        if name is None:
            raise ParseError('bad token 0x%02x at %d' % (t, at))
        if t == 0x39:
            self.emit(at, 'PrimitiveCast %d' % self.b())
            self.expr()
        elif t in (0x0F, 0x14, 0x45):
            self.emit(at, name)
            self.expr()
            self.expr()
        elif t == 0x06:
            self.emit(at, 'Jump %d' % self.w())
        elif t in (0x00, 0x01, 0x02, 0x49, 0x0E, 0x29):
            self.emit(at, '%s %s' % (name, self.ref()))
        elif t == 0x42:
            v, ln, pos, op = self.int_(), self.int_(), self.int_(), self.b()
            self.emit(at, 'DebugInfo v%d line %d pos %d op %d' % (v, ln, pos, op))
        elif t == 0x2D:
            self.emit(at, name)
            self.expr()
        elif t in (0x0B, 0x16, 0x25, 0x26, 0x27, 0x28, 0x2A, 0x17, 0x30, 0x08, 0x31):
            self.emit(at, name)
        elif t == 0x04:
            self.emit(at, name)
            self.expr()
        elif t == 0x1C:
            self.emit(at, 'FinalFunction %s' % self.ref())
            self.parms()
        elif t in (0x1B, 0x38):
            self.emit(at, '%s %s' % (name, self.nm()))
            self.parms()
        elif t == 0x43:
            p = self.ref()
            self.emit(at, 'DelegateFunction %s %s' % (p, self.nm()))
        elif t == 0x48:
            p = self.ref()
            n = self.nm()
            self.emit(at, 'FinalDelegate %s %s %s' % (p, n, self.ref()))
        elif t in (0x12, 0x19):
            self.emit(at, name)
            self.expr()
            w, sz = self.w(), self.b()
            self.emit(self.i, '  skip %d size %d' % (w, sz))
            self.expr()
        elif t in (0x1A, 0x10):
            self.emit(at, name)
            self.expr()
            self.expr()
        elif t in (0x37, 0x46):
            self.emit(at, name)
            self.expr()
        elif t in (0x40, 0x41):
            self.emit(at, name)
            self.expr()
            self.expr()
            self.expr()
        elif t == 0x50:
            self.emit(at, name)
            self.expr()
            self.expr()
        elif t == 0x47:
            self.emit(at, name)
            self.expr()
            self.emit(self.i, '  skip %d' % self.w())
            self.expr()
            self.emit(self.i, '  skip %d' % self.w())
            self.expr()
        elif t == 0x11:
            self.emit(at, name)
            for _ in range(4):
                self.expr()
        elif t == 0x1D:
            self.emit(at, 'IntConst %d' % self.int_())
        elif t == 0x1E:
            self.emit(at, 'FloatConst %r' % self.flt())
        elif t == 0x1F:
            j = self.s.index(b'\0', self.i)
            txt = self.s[self.i:j].decode('latin-1')
            self.i = j + 1
            self.emit(at, 'StringConst %r' % txt)
        elif t == 0x34:
            chars = []
            while True:
                c = self.w()
                if c == 0:
                    break
                chars.append(chr(c))
            self.emit(at, 'UnicodeStringConst %r' % ''.join(chars))
        elif t == 0x20:
            self.emit(at, 'ObjectConst %s' % self.ref())
        elif t == 0x21:
            self.emit(at, 'NameConst %s' % self.nm())
        elif t == 0x22:
            self.emit(at, 'RotationConst %d %d %d' % (self.int_(), self.int_(), self.int_()))
        elif t == 0x23:
            self.emit(at, 'VectorConst %r %r %r' % (self.flt(), self.flt(), self.flt()))
        elif t == 0x35:
            self.emit(at, 'RangeConst %r %r' % (self.flt(), self.flt()))
        elif t in (0x24, 0x2C):
            self.emit(at, '%s %d' % (name, self.b()))
        elif t in (0x13, 0x2E):
            self.emit(at, '%s %s' % (name, self.ref()))
            self.expr()
        elif t == 0x07:
            self.emit(at, 'JumpIfNot %d' % self.w())
            self.expr()
        elif t == 0x2F:
            self.emit(at, name)
            self.expr()
            self.emit(self.i, '  skip %d' % self.w())
        elif t == 0x05:
            self.emit(at, 'Switch size %d' % self.b())
            self.expr()
        elif t == 0x09:
            self.emit(at, 'Assert line %d' % self.w())
            self.expr()
        elif t == 0x0A:
            w = self.w()
            self.emit(at, 'Case %d' % w)
            if w != 0xFFFF:
                self.expr()
        elif t == 0x0C:
            self.emit(at, name)
            while True:
                n, off = self.nm(), self.int_()
                self.emit(self.i - 8, '  label %s %d' % (n, off))
                if n == 'None':
                    break
        elif t == 0x0D:
            self.emit(at, name)
            self.expr()
        elif t == 0x18:
            self.emit(at, 'Skip %d' % self.w())
            self.expr()
        elif t in (0x32, 0x33):
            self.emit(at, '%s %s' % (name, self.ref()))
            self.expr()
            self.expr()
        elif t == 0x36:
            self.emit(at, 'StructMember %s' % self.ref())
            self.expr()
        elif t == 0x44:
            self.emit(at, 'DelegateProperty %s' % self.nm())
        else:
            raise ParseError('unhandled token 0x%02x' % t)
        return t

    def run(self):
        while self.i < len(self.s):
            self.expr()
        if self.i != len(self.s):
            raise ParseError('script overrun')
        return self.out


def disasm(pkg, script):
    try:
        return Disasm(pkg, script).run()
    except (ParseError, IndexError, struct.error) as e:
        return [(0, 'UNDECODABLE: %s' % e)]


# ---------------------------------------------------------------------------------------------------------------
# Objects
# ---------------------------------------------------------------------------------------------------------------
def tagged(r):
    """FPropertyTag stream; returns list of (key, type, size, arrayindex, struct name, value bytes, bool)."""
    tags = []
    while True:
        ver = r.u8()
        if ver == 0:
            key = r.name()
            if key == 'None':
                return tags
        else:
            idx = r.i32()
            if idx == 0:
                return tags
            key = '#ref:' + r.pkg.ref_name(idx)
        info = r.u8()
        typ = info & 0x0F
        item = None
        if ver == 0 and typ == 10:
            item = r.name()
        sz = {0x00: 1, 0x10: 2, 0x20: 4, 0x30: 12, 0x40: 16}.get(info & 0x70)
        if sz is None:
            sz = {0x50: r.u8, 0x60: r.u16, 0x70: r.i32}[info & 0x70]()
        idx = 0
        boolval = None
        if typ == 3:
            boolval = bool(info & 0x80)
        elif info & 0x80:
            b = r.u8()
            if b & 0x80 == 0:
                idx = b
            elif b & 0xC0 == 0x80:
                idx = ((b & 0x7F) << 8) + r.u8()
            else:
                c, d, e = r.u8(), r.u8(), r.u8()
                idx = ((b & 0x3F) << 24) + (c << 16) + (d << 8) + e
        value = r.take(sz) if typ != 3 else b''
        tags.append((key, TAG_TYPES.get(typ, str(typ)), sz, idx, item, value, boolval))


def parse_export(pkg, i):
    """Return a dict of named fields; raises ParseError if the layout does not consume the export exactly."""
    ex = pkg.exports[i]
    cls = pkg.export_class(i)
    r = Reader(pkg, pkg.export_data(i))
    f = {'@class': cls}
    if ex['size'] == 0:
        return f
    if cls != 'Class':
        if ex['flags'] & RF_HasStack:
            node = r.ref()
            f['stack.node'] = node
            f['stack.statenode'] = r.ref()
            f['stack.probemask'] = r.u64()
            f['stack.latent'] = r.u16()
            if node != 'None':
                f['stack.offset'] = r.i32()
        f['@props'] = tagged(r)
    known = cls in ('Class', 'State', 'Function', 'Struct', 'Const', 'Enum', 'TextBuffer') or cls in PROPERTY_CLASSES
    if not known:
        f['@rest'] = r.d[r.p:]
        return f
    if cls == 'TextBuffer':
        f['pos'] = r.i32()
        f['top'] = r.i32()
        f['text'] = r.fstring()
    else:
        f['super'] = r.ref()
        f['next'] = r.ref()
        if cls == 'Const':
            f['value'] = r.fstring()
        elif cls == 'Enum':
            f['names'] = [r.name() for _ in range(r.i32())]
        elif cls in PROPERTY_CLASSES:
            f['arraydim'] = r.u16()
            f['flags'] = r.u32()
            f['category'] = r.name()
            if f['flags'] & CPF_Net:
                f['repoffset'] = r.u16()
            if cls == 'ByteProperty':
                f['enum'] = r.ref()
            elif cls in ('ObjectProperty', 'ClassProperty'):
                f['propertyclass'] = r.ref()
                if cls == 'ClassProperty':
                    f['metaclass'] = r.ref()
            elif cls == 'DelegateProperty':
                f['function'] = r.ref()
            elif cls in ('ArrayProperty',):
                f['inner'] = r.ref()
            elif cls == 'FixedArrayProperty':
                f['inner'] = r.ref()
                f['count'] = r.i32()
            elif cls == 'MapProperty':
                f['key'] = r.ref()
                f['value'] = r.ref()
            elif cls == 'StructProperty':
                f['struct'] = r.ref()
        else:  # UStruct and below
            f['scripttext'] = r.ref()
            f['children'] = r.ref()
            f['cpptext'] = r.ref()
            f['line'] = r.i32()
            f['textpos'] = r.i32()
            size = r.i32()
            f['script'] = r.take(size)
            if cls == 'Function':
                f['inative'] = r.u16()
                f['operprecedence'] = r.u8()
                f['funcflags'] = r.u32()
                if f['funcflags'] & FUNC_Net:
                    f['repoffset'] = r.u16()
                f['friendlyname'] = r.name()
            elif cls in ('State', 'Class'):
                f['probemask'] = r.u64()
                f['ignoremask'] = r.u64()
                f['labeltableoffset'] = r.u16()
                f['stateflags'] = r.u32()
                f['funcmap'] = sorted((r.name(), r.ref()) for _ in range(r.i32()))
                if cls == 'Class':
                    f['classflags'] = r.u32()
                    f['dependencies'] = [(r.ref(), r.i32(), r.u32()) for _ in range(r.i32())]
                    f['packageimports'] = [r.name() for _ in range(r.i32())]
                    f['within'] = r.ref()
                    f['configname'] = r.name()
                    f['hidecategories'] = [r.name() for _ in range(r.i32())]
                    f['@defaults'] = tagged(r)
    if not r.done():
        raise ParseError('%d bytes left over' % (len(r.d) - r.p))
    return f


def fmt_tag(pkg, t):
    key, typ, sz, idx, item, value, boolval = t
    if typ == 'Bool':
        v = str(boolval)
    elif typ in ('Int',) and sz == 4:
        v = str(struct.unpack('<i', value)[0])
    elif typ == 'Float' and sz == 4:
        v = repr(struct.unpack('<f', value)[0])
    elif typ == 'Byte' and sz == 1:
        v = str(value[0])
    elif typ in ('Object', 'Class') and sz == 4:
        v = pkg.ref_name(struct.unpack('<i', value)[0])
    elif typ == 'Name' and sz == 4:
        n = struct.unpack('<i', value)[0]
        v = pkg.names[n] if 0 <= n < len(pkg.names) else '<%d>' % n
    elif typ == 'Str':
        v = repr(value[4:].rstrip(b'\0').decode('latin-1'))
    else:
        v = value.hex()
    return '%s[%d] (%s%s) = %s' % (key, idx, typ, (' ' + item) if item else '', v)


# ---------------------------------------------------------------------------------------------------------------
# ScriptPatches.h
# ---------------------------------------------------------------------------------------------------------------
def parse_patches(path):
    text = open(path, 'rb').read().decode('latin-1')
    blobs = {}
    for m in re.finditer(r'struct (\w+) : public FPatchData\s*\{.*?Size = (\d+);\s*DataName = TEXT\("([^"]*)"\);'
                         r'\s*BYTE bytes\[\d+\]=\s*\{([^}]*)\}', text, re.S):
        sname, size, dname, body = m.group(1), int(m.group(2)), m.group(3), m.group(4)
        data = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
        assert len(data) == size, sname
        blobs[sname] = (dname, data)
    packages = {}
    for m in re.finditer(r'struct (\w+)_Patch : public FLinkerPatchData\s*\{.*?\{(.*?)\n\t\}', text, re.S):
        body = m.group(2)
        p = {'package': re.search(r'PackageName = TEXT\("(\w+)"\)', body).group(1),
             'namecount': int(re.search(r'NameCount = (\d+)', body).group(1)),
             'exportcount': int(re.search(r'ExportCount = (\d+)', body).group(1)),
             'names': re.findall(r'new\(Names\) FName\(TEXT\("(\w+)"\)\)', body),
             'imports': [(a, b, c, int(d)) for a, b, c, d in re.findall(
                 r'BuildImport\(FName\(TEXT\("(\w+)"\)\),FName\(TEXT\("(\w+)"\)\),FName\(TEXT\("(\w+)"\)\),(-?\d+)\)',
                 body)],
             'exports': [(a, int(b), int(c), int(d), int(e)) for a, b, c, d, e in re.findall(
                 r'BuildExport\(FName\(TEXT\("(\w+)"\)\), (\d+), (-?\d+), (-?\d+), (-?\d+)\)', body)],
             'objects': [blobs[s] for s in re.findall(r'Objects\.AddItem\(new (\w+)\(\)\)', body)],
             'scripts': [blobs[s] for s in re.findall(r'ScriptPatches\.AddItem\(new (\w+)\(\)\)', body)]}
        packages[p['package']] = p
    return packages


# ---------------------------------------------------------------------------------------------------------------
# Comparison
# ---------------------------------------------------------------------------------------------------------------

# ---------------------------------------------------------------------------------------------------------------
# Explanations
# ---------------------------------------------------------------------------------------------------------------
# Neutral: no effect on what runs. Each recognised by a rule below, never by a list of objects.
NEUTRAL = {
    'stripsource': 'text buffer emptied to " " by `ucc editor.stripsource` (shipped files went through it)',
    'stripsource-flags': 'CLASS_FullDefaults set on the stripsource load',
    'positions': 'Line/TextPos of a function in a class whose source changed after retail (text moved)',
    'dependency-crc': 'dependency records carry the script-text CRC of classes whose source changed after retail',
    'import-time': 'InternalTime of an #exec-imported texture (appSeconds() at import)',
    'lazy-offsets': 'absolute file offsets inside lazy arrays (the export sits elsewhere in the file)',
    'mipzero-rounding': 'MipZero (average colour, unused at run time) off by one: float summation at import',
    'name-case': 'an FName spelt with other case (names compare without case; first creation decides)',
    'patch': 'bytecode identical, token for token, to the ScriptPatches.h bytecode for this function/state',
    'patch-object': 'object appended by ScriptPatches.h, identical here',
    'patch-link': 'Children/FuncMap/Next take in a field the patch appends (the patcher links it at load time)',
    'patch-removed-local': 'local variable of a function whose patched body no longer has it (empty export slot)',
    'appended-order': 'what the patch appends is appended here too, in another order',
    'patch-config-names': 'names the patch appends that only Epic\'s later, ini-loaded defaults used (no bytecode uses them)',
    'dependency-list': 'dependency list (incremental-compile bookkeeping) of a class whose source changed after retail',
    'source-text': 'script text of a class whose source changed after retail (unstripped packages; diff in the details)',
}


class PatchedView(Package):
    """The shipped package with what ScriptPatches.h appends, in the patch's order: the index space the patch
    bytecode and objects were written in."""
    def __init__(self, old, patch):  # noqa: super().__init__ not called on purpose (no file)
        self.path = old.path + ' + ScriptPatches.h'
        self.data = b''
        self.names = list(old.names) + list(patch['names'])
        self.imports = list(old.imports) + [{'name': a, 'class_name': b, 'class_package': c, 'package': d}
                                            for a, b, c, d in patch['imports']]
        self.objects = {len(old.exports) + k: data for k, (dname, data) in enumerate(patch['objects'])}
        self.exports = list(old.exports) + [{'name': n, 'flags': f, 'class': c, 'super': sp, 'package': pk,
                                             'size': len(self.objects.get(len(old.exports) + k, b'')), 'offset': 0}
                                            for k, (n, f, c, sp, pk) in enumerate(patch['exports'])]

    def export_data(self, i):
        return self.objects.get(i, b'')


def class_crcs(pkgname, pkg):
    """{'Pkg.Class': CRC of its script text} from each class's own dependency record."""
    out = {}
    for i in range(len(pkg.exports)):
        if pkg.export_class(i) != 'Class' or pkg.exports[i]['size'] == 0:
            continue
        try:
            f = parse_export(pkg, i)
        except (ParseError, struct.error, IndexError):
            continue
        path = pkg.export_path(i)
        for ref, deep, crc in f.get('dependencies', []):
            if ref == path:
                out['%s.%s' % (pkgname, path)] = crc
                break
    return out


def qualify(pkgname, pkg, ref):
    """Package-qualified path of a reference as printed by ref_name (exports are unqualified)."""
    return ref if ref == 'None' or '.' in ref and ref.split('.')[0] in PACKAGES + ['Core', 'Engine'] \
        else '%s.%s' % (pkgname, ref)


def mips(rest):
    r = struct.Struct('<i')
    p = 0
    out = []
    n = r.unpack_from(rest, p)[0]
    p += 4
    for _ in range(n):
        seek = r.unpack_from(rest, p)[0]
        cnt = r.unpack_from(rest, p + 4)[0]
        data = rest[p + 8:p + 8 + cnt]
        p += 8 + cnt
        us, vs = struct.unpack_from('<ii', rest, p)
        ub, vb = rest[p + 8], rest[p + 9]
        p += 10
        out.append((data, us, vs, ub, vb))
    if p != len(rest):
        raise ParseError('mips: %d bytes left' % (len(rest) - p))
    return out


def relocated(a, off_a, b, off_b):
    """True if a and b differ only in 4-byte words that are absolute file offsets of the same place."""
    if a is None or b is None:      # one side is not a plain object (S1: a decompiled package lacks #exec imports)
        return False
    if len(a) != len(b):
        return False
    i = 0
    while i < len(a):
        if a[i] == b[i]:
            i += 1
            continue
        ok = False
        for s in range(max(0, i - 3), i + 1):
            if s + 4 <= len(a):
                va, vb = struct.unpack_from('<i', a, s)[0], struct.unpack_from('<i', b, s)[0]
                if va - off_a == vb - off_b and 0 <= va - off_a <= len(a) + 16:
                    i = s + 4
                    ok = True
                    break
        if not ok:
            return False
    return True


class Comparison:
    def __init__(self, name, old, new, patch, ctx, det):
        self.name, self.old, self.new, self.patch, self.ctx, self.det = name, old, new, patch, ctx, det
        self.view = PatchedView(old, patch) if patch else None
        self.neutral = {}
        self.findings = []      # behaviour-relevant, explained
        self.problems = []      # unexplained
        self.identical = 0
        self.patch_scripts = {d: b for d, b in patch['scripts']} if patch else {}
        self.used = set()
        self.patched_owners = set()
        for d in self.patch_scripts:
            parts = d.split('.')[1:]
            for k in range(1, len(parts) + 1):
                self.patched_owners.add('.'.join(parts[:k]))
        self.status = {}        # ScriptPatches.h entry -> what the comparison found
        self.fstatus = []       # (class, path, shipped bytes, recompiled bytes, status) of every function/state
        self.added = set()      # recompiled export paths appended by the patch
        self.removed = set()    # shipped export paths whose slot is empty in the recompiled file

    def neutral_(self, kind):
        self.neutral[kind] = self.neutral.get(kind, 0) + 1

    def finding(self, text):
        self.findings.append(text)
        self.det.append('  FINDING: ' + text)

    def problem(self, text):
        self.problems.append(text)
        self.det.append('  PROBLEM: ' + text)

    def changed(self, cls_path):
        return cls_path in self.ctx['changed']

    # -- tables ------------------------------------------------------------------------------------------------
    def tables(self):
        old, new, patch = self.old, self.new, self.patch
        d = self.det
        d.append('=' * 100)
        d.append('PACKAGE %s' % self.name)
        d.append('  shipped:    %d names, %d imports, %d exports, guid %s, generations %s' % (
            len(old.names), len(old.imports), len(old.exports), old.guid.hex(), old.generations))
        d.append('  recompiled: %d names, %d imports, %d exports, guid %s, generations %s' % (
            len(new.names), len(new.imports), len(new.exports), new.guid.hex(), new.generations))
        if old.guid != new.guid:
            self.problem('package GUID differs')
        if (old.version, old.licensee, old.flags) != (new.version, new.licensee, new.flags):
            self.problem('summary differs')
        for i, n in enumerate(old.names):
            if i >= len(new.names):
                self.problem('name %d missing' % i)
            elif new.names[i] != n:
                if new.names[i].lower() == n.lower():
                    self.neutral_('name-case')
                    d.append('  name %d: %r / %r (case)' % (i, n, new.names[i]))
                else:
                    self.problem('name %d: shipped %r, recompiled %r' % (i, n, new.names[i]))
        for i in range(len(old.imports)):
            a, b = old.imports[i], new.imports[i] if i < len(new.imports) else None
            if b is None or old.import_path(i).lower() != new.import_path(i).lower() or \
                    (a['class_package'], a['class_name']) != (b['class_package'], b['class_name']):
                self.problem('import %d: shipped %s, recompiled %s' % (i, old.import_path(i),
                                                                      new.import_path(i) if b else None))
        for i, ex in enumerate(old.exports):
            if i >= len(new.exports):
                self.problem('export %d missing' % (i + 1))
                continue
            pa, pb = old.export_path(i), new.export_path(i)
            nb = new.exports[i]
            if nb['name'] == 'None' and nb['class'] == 0 and nb['size'] == 0:
                owner = '.'.join(pa.split('.')[:-1])
                if owner in self.patched_owners:
                    self.removed.add(pa)
                    self.neutral_('patch-removed-local')
                    d.append('  export %d %s: empty slot (local of a patched function)' % (i + 1, pa))
                else:
                    self.problem('export %d %s %s: empty in the recompiled file' % (i + 1, old.export_class(i), pa))
                continue
            if pa != pb or old.export_class(i) != new.export_class(i):
                if pa.lower() == pb.lower() and old.export_class(i) == new.export_class(i):
                    self.neutral_('name-case')
                else:
                    self.problem('export %d: shipped %s %s, recompiled %s %s' % (
                        i + 1, old.export_class(i), pa, new.export_class(i), pb))
            if nb['flags'] != ex['flags']:
                self.problem('export %d %s: object flags 0x%08x -> 0x%08x' % (i + 1, pa, ex['flags'], nb['flags']))
        # appended
        an = new.names[len(old.names):]
        ai = [(new.imports[i]['name'], new.imports[i]['class_name'], new.imports[i]['class_package'],
               new.import_path(i)) for i in range(len(old.imports), len(new.imports))]
        ae = list(range(len(old.exports), len(new.exports)))
        if an or ai or ae:
            d.append('  appended: names %s; imports %s; exports %s' % (
                an, [x[3] for x in ai], ['%s %s' % (new.export_class(j), new.export_path(j)) for j in ae]))
        pn = list(patch['names']) if patch else []
        if sorted(an) != sorted(pn):
            extra, missing = sorted(set(an) - set(pn)), sorted(set(pn) - set(an))
            for n in extra:
                self.ctx_extra_name(n)
            if missing:
                if self.patch_name_unused(missing):
                    self.neutral_('patch-config-names')
                    d.append('  names the patch appends but nothing here uses: %s' % missing)
                else:
                    self.problem('names the patch appends and the bytecode uses are missing here: %s' % missing)
        elif an != pn:
            self.neutral_('appended-order')
        pi = [(a, b, c) for a, b, c, dd in patch['imports']] if patch else []
        vi = [(x[0], x[1], x[2]) for x in ai]
        if sorted(vi) != sorted(pi):
            for x in sorted(set(vi) - set(pi)):
                self.ctx_extra_import(x)
            for x in sorted(set(pi) - set(vi)):
                self.problem('import %s the patch appends is not appended here' % (x,))
        elif vi != pi:
            self.neutral_('appended-order')
        pe = [x[0] for x in patch['exports']] if patch else []
        if sorted(new.exports[j]['name'] for j in ae) != sorted(pe):
            self.problem('appended exports %s, ScriptPatches.h appends %s' % (
                [new.export_path(j) for j in ae], pe))
        elif [new.exports[j]['name'] for j in ae] != pe:
            self.neutral_('appended-order')
        # appended objects, semantically
        if patch:
            view = self.view
            used = set()
            for k in range(len(patch['exports'])):
                vi_ = len(old.exports) + k
                vpath, vcls = view.export_path(vi_), view.export_class(vi_)
                match = [j for j in ae if new.export_path(j) == vpath and new.export_class(j) == vcls and j not in used]
                if not match:
                    self.problem('patch object %s %s not found here' % (vcls, vpath))
                    continue
                j = match[0]
                used.add(j)
                self.added.add(vpath)
                try:
                    fa, fb = parse_export(view, vi_), parse_export(new, j)
                except (ParseError, struct.error, IndexError) as e:
                    self.problem('patch object %s: cannot parse (%s)' % (vpath, e))
                    continue
                diffs = [k2 for k2 in set(fa) | set(fb) if k2 != 'script' and fa.get(k2) != fb.get(k2)]
                if 'script' in fa or 'script' in fb:
                    if [t for o, t in disasm(view, fa.get('script', b''))] != [t for o, t in disasm(new, fb.get('script', b''))]:
                        diffs.append('script')
                self.status['%s.%s' % (self.name, vpath)] = ('object', vcls, 'identical' if not diffs else
                                                            'DIFFERS in %s' % sorted(diffs))
                if diffs:
                    self.problem('patch object %s differs here in %s' % (vpath, sorted(diffs)))
                    d.append('    patch %r' % {k2: fa.get(k2) for k2 in diffs})
                    d.append('    here  %r' % {k2: fb.get(k2) for k2 in diffs})
                else:
                    self.neutral_('patch-object')

    def patch_name_unused(self, names):
        # A patch name nothing in the patch bytecode or objects refers to.
        view = self.view
        idx = {view.names.index(n) for n in names if n in view.names}
        blobs = list(self.patch_scripts.values()) + [b for _, b in self.patch['objects']]
        for b in blobs:
            for o, t in disasm(view, b):
                if any(t.endswith(' ' + view.names[i]) or (' %s ' % view.names[i]) in t for i in idx):
                    return False
        return True

    def ctx_extra_name(self, n):
        self.ctx['extra_names'].setdefault(self.name, []).append(n)

    def ctx_extra_import(self, x):
        self.ctx['extra_imports'].setdefault(self.name, []).append(x)

    # -- exports -----------------------------------------------------------------------------------------------
    def exports(self):
        old, new, d = self.old, self.new, self.det
        for i in range(min(len(old.exports), len(new.exports))):
            path, cls = old.export_path(i), old.export_class(i)
            if path in self.removed:
                continue
            a, b = old.export_data(i), new.export_data(i)
            if a == b:
                self.identical += 1
                if cls in ('Function', 'State'):
                    self.fstatus.append((cls, path, len(a), len(b), 'identical'))
                continue
            try:
                fa, fb = parse_export(old, i), parse_export(new, i)
            except (ParseError, struct.error, IndexError) as e:
                self.problem('%s %s: cannot parse (%s)' % (cls, path, e))
                continue
            owner = '%s.%s' % (self.name, path.split('.')[0])
            left = []
            kinds = []
            nf, np_ = len(self.findings), len(self.problems)
            for k in sorted(set(fa) | set(fb), key=str):
                va, vb = fa.get(k), fb.get(k)
                if va == vb:
                    continue
                why = self.explain(i, path, cls, owner, k, va, vb, fa, fb)
                if why is None:
                    left.append(k)
                elif why.startswith('FINDING:'):
                    self.finding('%s %s: %s' % (cls, path, why[8:].strip()))
                else:
                    for w in why.split(','):
                        self.neutral_(w)
                        kinds.append(w)
            if left:
                desc = []
                for k in left:
                    va, vb = fa.get(k), fb.get(k)
                    if isinstance(va, int) and isinstance(vb, int) and k.endswith('flags'):
                        desc.append('%s 0x%08x -> 0x%08x' % (k, va, vb))
                    elif k in ('@props', '@defaults'):
                        desc.append('%s:\n%s' % (k[1:], '\n'.join(self.tag_diff(fa.get(k), fb.get(k)))))
                    elif k == 'script':
                        desc.append('script:\n' + '\n'.join(listing_diff(old, va or b'', new, vb or b'')))
                    else:
                        sa, sb = repr(va), repr(vb)
                        desc.append('%s %s -> %s' % (k, sa[:150], sb[:150]))
                self.problem('%s %s: %s' % (cls, path, '; '.join(desc)))
            if cls in ('Function', 'State'):
                st = 'neutral: %s' % ','.join(sorted(set(kinds))) if kinds else ''
                if len(self.findings) > nf:
                    st = (st + '; ' if st else '') + 'FINDING'
                if len(self.problems) > np_:
                    st = (st + '; ' if st else '') + 'PROBLEM'
                self.fstatus.append((cls, path, len(a), len(b), st or 'bytes differ, fields equal'))

    def config_only(self, clspath, ta, tb):
        da = {(t[0], t[3]): t for t in ta or []}
        db = {(t[0], t[3]): t for t in tb or []}
        keys = [k for k in set(da) | set(db) if not (da.get(k) and db.get(k) and da[k][1:] == db[k][1:])]
        if not keys:
            return False
        for name, idx in keys:
            fl = self.ctx['propflags'](self.name, clspath, name)
            if fl is None or not fl & 0x4000:
                return False
        return True

    def tag_diff(self, ta, tb):
        da = {(t[0], t[3]): t for t in ta or []}
        db = {(t[0], t[3]): t for t in tb or []}
        out = []
        for key in sorted(set(da) | set(db), key=str):
            x, y = da.get(key), db.get(key)
            if x and y and x[1:] == y[1:]:
                continue
            out.append('      %s | %s' % (fmt_tag(self.old, x) if x else '(absent)', fmt_tag(self.new, y) if y else '(absent)'))
        return out

    def explain(self, i, path, cls, owner, k, va, vb, fa, fb):
        old, new = self.old, self.new
        stripped = self.name not in ('Editor', 'UCMenu')
        if k == 'text' and cls == 'TextBuffer' and va == ' ' and stripped:
            return 'stripsource'
        if k == 'classflags' and (va ^ vb) == CLASS_FullDefaults and va & CLASS_FullDefaults and stripped:
            return 'stripsource-flags'
        if k in ('line', 'textpos') and self.changed(owner):
            return 'positions'
        if k == 'dependencies' and self.changed(owner):
            return 'dependency-list'
        if k == 'text' and cls == 'TextBuffer' and self.changed(owner) and not stripped:
            import difflib
            self.det.append('  %s %s: retail text vs ours (unified diff):' % (cls, path))
            self.det += ['      ' + l for l in difflib.unified_diff(
                va.replace('\r', '').split('\n'), vb.replace('\r', '').split('\n'), 'retail', 'source', n=1, lineterm='')
                if l.strip() not in ('', '+', '-')]
            return 'source-text'
        if k == 'funcflags' and self.changed(owner) and (va ^ vb) == 0x2:
            return ('FINDING: FUNC_Defined %s -> %s: the newer source empties the body (an empty body is not "defined", '
                    'UnScrCom.cpp), the patch could only replace the bytecode; the VM skips undefined functions '
                    '(UnCorSc.cpp), so both do nothing' % (bool(va & 2), bool(vb & 2)))
        if k == 'dependencies':
            if va is not None and vb is not None and len(va) == len(vb):
                for (ra, da, ca), (rb, db_, cb) in zip(va, vb):
                    if ra != rb or da != db_:
                        return None
                    if ca != cb:
                        q = qualify(self.name, old, ra)
                        if not (q in self.ctx['changed'] and self.ctx['old_crc'].get(q) == ca
                                and self.ctx['new_crc'].get(q) == cb):
                            return None
                return 'dependency-crc'
            return None
        if k == 'script':
            full = '%s.%s' % (self.name, path)
            if full in self.patch_scripts:
                self.used.add(full)
                ta = [t for o, t in disasm(self.view, self.patch_scripts[full])]
                tb = [t for o, t in disasm(new, vb)]
                oa = [o for o, t in disasm(self.view, self.patch_scripts[full])]
                ob = [o for o, t in disasm(new, vb)]
                if ta == tb and oa == ob:
                    self.status[full] = ('bytecode', cls, 'identical (%d tokens)' % len(ta))
                    return 'patch'
                self.status[full] = ('bytecode', cls, 'DIFFERS (see the findings)')
                self.det.append('  %s %s: ScriptPatches.h bytecode vs recompiled:' % (cls, path))
                self.det += listing_diff(self.view, self.patch_scripts[full], new, vb)
                return 'FINDING: bytecode differs from the ScriptPatches.h bytecode for it (listing in the details)'
            return None
        if k in ('children', 'next'):
            if vb in self.added or va in self.removed:
                return 'patch-link'
            return None
        if k == 'funcmap':
            extra = set(vb or []) - set(va or [])
            gone = set(va or []) - set(vb or [])
            if not gone and all(p in self.added for _, p in extra):
                return 'patch-link'
            return None
        if k in ('probemask', 'labeltableoffset') and self.changed(owner):
            return 'FINDING: %s %r -> %r: the source changed after retail and the patcher cannot change this field' % (k, va, vb)
        if k == '@props' and cls == 'Texture':
            da = {(t[0], t[3]): t for t in va or []}
            db = {(t[0], t[3]): t for t in vb or []}
            kinds = set()
            for key in set(da) | set(db):
                x, y = da.get(key), db.get(key)
                if x and y and x[1:] == y[1:]:
                    continue
                if key[0] == 'InternalTime':
                    kinds.add('import-time')
                elif key[0] == 'MipZero' and x and y:
                    ca, cb = x[5], y[5]
                    if max(abs(p - q) for p, q in zip(ca, cb)) <= 1:
                        kinds.add('mipzero-rounding')
                    else:
                        return None
                else:
                    return None
            return ','.join(sorted(kinds))
        if k == '@rest':
            if cls == 'Texture':
                try:
                    ma, mb = mips(va), mips(vb)
                except (ParseError, struct.error, IndexError, TypeError):
                    return None
                if ma == mb:
                    return 'lazy-offsets'
                self.det.append('  %s %s: mip data differs: %s' % (cls, path, [
                    (j, sum(1 for p, q in zip(x[0], y[0]) if p != q), len(x[0])) for j, (x, y) in enumerate(zip(ma, mb))
                    if x != y]))
                return None
            if relocated(va, old.exports[i]['offset'], vb, new.exports[i]['offset']):
                return 'lazy-offsets'
            if cls == 'StaticMesh' and self.name == 'Editor' and len(va) == len(vb):
                n = sum(1 for x, y in zip(va, vb) if x != y)
                return ('FINDING: %d of %d bytes differ (low bits of floats computed when `#exec NEW StaticMesh` imports '
                        'the .ase); an editor-only mesh (texture browser preview), never loaded by the game' % (n, len(va)))
            return None
        if k == '@defaults':
            cfg = self.config_only(path, va, vb)
            if cfg:
                return ('FINDING: defaults of config properties differ (at run time the ini value replaces them when the '
                        'ini has the key):\n' + '\n'.join(self.tag_diff(va, vb)))
        if k in ('@defaults',) and self.changed(owner):
            return 'FINDING: defaults differ in a class whose source changed after retail (a patch carries bytecode only):\n' + \
                '\n'.join(self.tag_diff(va, vb))
        if k == 'flags' and cls in PROPERTY_CLASSES and self.changed(owner):
            return 'FINDING: property flags 0x%08x -> 0x%08x (xor 0x%08x) in a class whose source changed after retail' % (va, vb, va ^ vb)
        return None

    def run(self):
        self.tables()
        self.exports()
        for p in sorted(set(self.patch_scripts) - self.used):
            self.status[p] = ('bytecode', '?', 'NOT APPLIED: the recompiled bytecode equals the shipped one')
            self.problem('ScriptPatches.h bytecode for %s: the recompiled bytecode equals the shipped one' % p)
        for n in self.ctx['extra_names'].get(self.name, []):
            self.det.append('  appended here, not by the patch: name %s' % n)
        for x in self.ctx['extra_imports'].get(self.name, []):
            self.det.append('  appended here, not by the patch: import %s' % (x,))
        if self.problems:
            verdict = 'different'
        elif self.findings:
            verdict = 'different-explained'
        elif self.neutral:
            verdict = 'equivalent'
        else:
            verdict = 'identical'
        self.det.append('  verdict: %s; neutral %s; findings %d; problems %d' % (
            verdict, self.neutral, len(self.findings), len(self.problems)))
        return verdict


def listing_diff(pa, a, pb, b):
    import difflib
    la = ['%5d %s' % (o, t) for o, t in disasm(pa, a)]
    lb = ['%5d %s' % (o, t) for o, t in disasm(pb, b)]
    out = []
    for line in difflib.unified_diff(la, lb, 'shipped', 'recompiled', n=2, lineterm=''):
        out.append('      ' + line)
    return out if out else ['      (token streams identical; only operand bytes differ)']


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        sys.exit(2)
    old_dir, new_dir = args[0], args[1]
    patches_path = None
    details = None
    patch_table = None
    functions = None
    packages = PACKAGES
    i = 2
    while i < len(args):
        if args[i] == '--patches':
            patches_path = args[i + 1]
            i += 2
        elif args[i] == '--details':
            details = args[i + 1]
            i += 2
        elif args[i] == '--functions':
            functions = args[i + 1]
            i += 2
        elif args[i] == '--patch-table':
            patch_table = args[i + 1]
            i += 2
        elif args[i] == '--packages':
            packages = args[i + 1].split(',')
            i += 2
        else:
            print('unknown argument %s' % args[i])
            sys.exit(2)
    patches = parse_patches(patches_path) if patches_path else {}
    olds, news = {}, {}
    for p in PACKAGES:
        olds[p] = Package(os.path.join(old_dir, p + '.u'))
        if os.path.exists(os.path.join(new_dir, p + '.u')):
            news[p] = Package(os.path.join(new_dir, p + '.u'))
    ctx = {'old_crc': {}, 'new_crc': {}, 'extra_names': {}, 'extra_imports': {}}
    for p in PACKAGES:
        ctx['old_crc'].update(class_crcs(p, olds[p]))
        if p in news:
            ctx['new_crc'].update(class_crcs(p, news[p]))
    ctx['changed'] = {k for k in ctx['old_crc'] if ctx['new_crc'].get(k) not in (None, ctx['old_crc'][k])}
    # Property flags and class supers of the shipped packages, for config-property recognition.
    props, supers = {}, {}
    for p in PACKAGES:
        pk = olds[p]
        for i in range(len(pk.exports)):
            c = pk.export_class(i)
            if c == 'Class':
                try:
                    supers['%s.%s' % (p, pk.export_path(i))] = qualify(p, pk, parse_export(pk, i).get('super', 'None'))
                except (ParseError, struct.error, IndexError):
                    pass
            elif c in PROPERTY_CLASSES and pk.export_path(i).count('.') == 1:
                try:
                    props['%s.%s' % (p, pk.export_path(i))] = parse_export(pk, i)['flags']
                except (ParseError, struct.error, IndexError):
                    pass

    def propflags(pkg, clspath, name):
        c = '%s.%s' % (pkg, clspath)
        while c and c != 'None':
            if '%s.%s' % (c, name) in props:
                return props['%s.%s' % (c, name)]
            c = supers.get(c)
        return None
    ctx['propflags'] = propflags
    det = ['Classes whose script text changed after retail (own CRC differs): %d' % len(ctx['changed'])]
    det += ['  ' + c for c in sorted(ctx['changed'])]
    worst = 0
    summary = []
    statuses = {}
    flines = []
    for p in packages:
        if p not in news:
            print('%-16s MISSING' % p)
            worst = 2
            continue
        c = Comparison(p, olds[p], news[p], patches.get(p), ctx, det)
        verdict = c.run()
        statuses.update(c.status)
        flines.extend('%-16s %-8s %-70s %6d %6d  %s' % ((p,) + t) for t in c.fstatus)
        line = '%-16s %-19s exports %5d, byte-identical %5d, neutral %4d, findings %2d, problems %2d' % (
            p, verdict, len(olds[p].exports), c.identical, sum(c.neutral.values()), len(c.findings), len(c.problems))
        print(line)
        summary.append(line)
        if c.problems:
            worst = max(worst, 1)
    det.append('')
    det.append('Neutral kinds:')
    det += ['  %-20s %s' % kv for kv in NEUTRAL.items()]
    if functions:
        with open(functions, 'w') as f:
            f.write('%-16s %-8s %-70s %6s %6s  %s\n' % ('package', 'kind', 'function or state', 'ship', 'ours', 'result'))
            f.write('\n'.join(flines) + '\n')
    if patch_table and patches:
        rows = []
        n = 0
        for pkg in PACKAGES:
            pt = patches.get(pkg)
            if not pt:
                continue
            for dname, data in pt['scripts']:
                n += 1
                kind, cls, st = statuses.get(dname, ('bytecode', '?', 'not compared'))
                rows.append('%2d  %-9s %-58s %4d bytes  %s' % (n, 'bytecode', dname, len(data), st))
            for k, (dname, data) in enumerate(pt['objects']):
                n += 1
                view = PatchedView(olds[pkg], pt)
                j = len(olds[pkg].exports) + k
                key = '%s.%s' % (pkg, view.export_path(j))
                kind, cls, st = statuses.get(key, ('object', '?', 'not compared'))
                rows.append('%2d  %-9s %-58s %4d bytes  %s' % (n, 'new %s' % view.export_class(j), key, len(data), st))
        with open(patch_table, 'w') as f:
            f.write('\n'.join(rows) + '\n')
    if details:
        with open(details, 'w') as f:
            f.write('\n'.join(summary + [''] + det) + '\n')
    sys.exit(worst)


if __name__ == '__main__':
    main()
