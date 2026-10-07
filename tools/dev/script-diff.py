#!/usr/bin/env python3
"""Compare recompiled UnrealScript packages with the shipped ones, object by object (M14).

    tools/dev/script-diff.py <shipped System dir> <recompiled System dir> [--patches ScriptPatches.h]
                             [--details <file>] [--packages A,B,...]

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
def compare_package(name, old, new, patch, det):
    """Returns (verdict, counts dict, list of difference strings)."""
    counts = {}
    problems = []
    explained = {}

    def note(kind):
        explained[kind] = explained.get(kind, 0) + 1

    det.append('=' * 100)
    det.append('PACKAGE %s' % name)
    det.append('  shipped:    %d names, %d imports, %d exports, guid %s, generations %s' % (
        len(old.names), len(old.imports), len(old.exports), old.guid.hex(), old.generations))
    det.append('  recompiled: %d names, %d imports, %d exports, guid %s, generations %s' % (
        len(new.names), len(new.imports), len(new.exports), new.guid.hex(), new.generations))
    if old.guid != new.guid:
        problems.append('package GUID differs')
    if (old.version, old.licensee) != (new.version, new.licensee) or old.flags != new.flags:
        problems.append('summary differs: %r vs %r' % ((old.version, old.licensee, old.flags),
                                                       (new.version, new.licensee, new.flags)))
    # Tables: the shipped entries must be a prefix (conform), and what is appended must be what the patch appends.
    tables_ok = True
    for i, n in enumerate(old.names):
        if i >= len(new.names) or new.names[i] != n:
            problems.append('name %d: shipped %r, recompiled %r' % (i, n, new.names[i] if i < len(new.names) else None))
            tables_ok = False
    for i, im in enumerate(old.imports):
        if i >= len(new.imports) or new.import_path(i) != old.import_path(i) or \
                (new.imports[i]['class_package'], new.imports[i]['class_name']) != (im['class_package'], im['class_name']):
            problems.append('import %d: shipped %s, recompiled %s' % (
                i, old.import_path(i), new.import_path(i) if i < len(new.imports) else None))
            tables_ok = False
    for i, ex in enumerate(old.exports):
        if i >= len(new.exports) or new.export_path(i) != old.export_path(i) or new.export_class(i) != old.export_class(i):
            problems.append('export %d: shipped %s %s, recompiled %s' % (
                i + 1, old.export_class(i), old.export_path(i),
                ('%s %s' % (new.export_class(i), new.export_path(i))) if i < len(new.exports) else None))
            tables_ok = False
        elif new.exports[i]['flags'] != ex['flags']:
            problems.append('export %d %s: object flags 0x%08x -> 0x%08x' % (i + 1, old.export_path(i), ex['flags'],
                                                                           new.exports[i]['flags']))
    added_names = new.names[len(old.names):]
    added_imports = [(new.imports[i]['name'], new.imports[i]['class_name'], new.imports[i]['class_package'],
                      new.imports[i]['package']) for i in range(len(old.imports), len(new.imports))]
    added_exports = list(range(len(old.exports), len(new.exports)))
    if patch:
        if patch['namecount'] != len(old.names) or patch['exportcount'] != len(old.exports):
            problems.append('ScriptPatches.h counts (%d names, %d exports) do not match the shipped file' % (
                patch['namecount'], patch['exportcount']))
        pn = patch['names']
        if added_names == pn:
            if pn:
                note('patch-names')
        else:
            problems.append('appended names %s, ScriptPatches.h appends %s' % (added_names, pn))
        pi = [(a, b, c, d) for a, b, c, d in patch['imports']]
        if added_imports == pi:
            if pi:
                note('patch-imports')
        else:
            problems.append('appended imports %s, ScriptPatches.h appends %s' % (added_imports, pi))
        pe = patch['exports']
        got = [(new.exports[j]['name'], new.exports[j]['flags'], new.exports[j]['class'], new.exports[j]['super'],
                new.exports[j]['package']) for j in added_exports]
        if got == pe:
            for k, j in enumerate(added_exports):
                if new.export_data(j) == patch['objects'][k][1]:
                    note('patch-object')
                else:
                    problems.append('appended export %s: data differs from the ScriptPatches.h object' %
                                    new.export_path(j))
        else:
            problems.append('appended exports %s, ScriptPatches.h appends %s' % (got, pe))
    else:
        if added_names or added_imports or added_exports:
            problems.append('appended without a patch: names %s imports %s exports %s' % (
                added_names, added_imports, [new.export_path(j) for j in added_exports]))
    if added_names or added_imports or added_exports:
        det.append('  appended: names %s; imports %s; exports %s' % (
            added_names, added_imports, ['%s %s' % (new.export_class(j), new.export_path(j)) for j in added_exports]))
    patch_scripts = {}
    if patch:
        for dname, data in patch['scripts']:
            patch_scripts[dname] = data
    used_patches = set()

    for i in range(len(old.exports)):
        if i >= len(new.exports):
            break
        path = old.export_path(i)
        cls = old.export_class(i)
        a, b = old.export_data(i), new.export_data(i)
        if a == b:
            counts['identical'] = counts.get('identical', 0) + 1
            continue
        try:
            fa = parse_export(old, i)
            fb = parse_export(new, i)
        except (ParseError, struct.error, IndexError) as e:
            problems.append('%s %s: unparsed (%s), %d vs %d bytes' % (cls, path, e, len(a), len(b)))
            det.append('  DIFF %s %s: cannot parse (%s)' % (cls, path, e))
            continue
        diffs = []
        reasons = []
        for k in sorted(set(fa) | set(fb)):
            va, vb = fa.get(k), fb.get(k)
            if va == vb:
                continue
            if k == 'text' and cls == 'TextBuffer' and va == ' ' and name != 'Editor':
                reasons.append('stripsource')
                continue
            if k == 'classflags' and va is not None and vb is not None and (va ^ vb) == CLASS_FullDefaults \
                    and va & CLASS_FullDefaults:
                reasons.append('stripsource-fulldefaults')
                continue
            if k == 'script':
                full = '%s.%s' % (name, path)
                if full in patch_scripts and patch_scripts[full] == vb:
                    reasons.append('patch')
                    used_patches.add(full)
                    continue
                if full in patch_scripts:
                    diffs.append('script differs from the shipped bytecode AND from the ScriptPatches.h bytecode')
                    det.append('  %s %s: recompiled bytecode vs ScriptPatches.h bytecode:' % (cls, path))
                    det += listing_diff(new, patch_scripts[full], new, vb)
                    used_patches.add(full)
                    continue
                diffs.append('script (%d -> %d bytes)' % (len(va or b''), len(vb or b'')))
                det.append('  %s %s: bytecode, shipped vs recompiled:' % (cls, path))
                det += listing_diff(old, va or b'', new, vb or b'')
                continue
            if k in ('@props', '@defaults'):
                da = {(t[0], t[3]): t for t in va or []}
                db = {(t[0], t[3]): t for t in vb or []}
                lines = []
                for key in sorted(set(da) | set(db), key=str):
                    ta, tb = da.get(key), db.get(key)
                    if ta and tb and ta[1:] == tb[1:]:
                        continue
                    lines.append('      %s | %s' % (fmt_tag(old, ta) if ta else '(absent)',
                                                    fmt_tag(new, tb) if tb else '(absent)'))
                if lines:
                    diffs.append('%s: %d properties' % (k[1:], len(lines)))
                    det.append('  %s %s: %s differ (shipped | recompiled):' % (cls, path, k[1:]))
                    det += lines
                elif [t[0] for t in va or []] != [t[0] for t in vb or []]:
                    diffs.append('%s: same values, different order' % k[1:])
                continue
            if k == 'dependencies':
                diffs.append('dependencies')
                det.append('  %s %s: dependencies shipped %s' % (cls, path, va))
                det.append('  %s %s: dependencies recompiled %s' % (cls, path, vb))
                continue
            if isinstance(va, int) and isinstance(vb, int) and k.endswith('flags'):
                diffs.append('%s 0x%08x -> 0x%08x (xor 0x%08x)' % (k, va, vb, va ^ vb))
            else:
                diffs.append('%s %r -> %r' % (k, va if not isinstance(va, (bytes, list)) or len(str(va)) < 200 else '...',
                                                vb if not isinstance(vb, (bytes, list)) or len(str(vb)) < 200 else '...'))
        if diffs:
            problems.append('%s %s: %s' % (cls, path, '; '.join(diffs)))
            det.append('  DIFF %s %s: %s%s' % (cls, path, '; '.join(diffs),
                                               (' [explained: %s]' % ', '.join(reasons)) if reasons else ''))
        elif reasons:
            for rr in set(reasons):
                note(rr)
            det.append('  explained %s %s: %s' % (cls, path, ', '.join(sorted(set(reasons)))))
        else:
            det.append('  same fields, different bytes?? %s %s' % (cls, path))
            problems.append('%s %s: bytes differ but no field does' % (cls, path))
    for p in sorted(set(patch_scripts) - used_patches):
        problems.append('ScriptPatches.h bytecode for %s not matched by any recompiled difference' % p)
    if problems:
        verdict = 'different (%d)' % len(problems)
    elif explained:
        verdict = 'equivalent (%s)' % ', '.join('%s %d' % kv for kv in sorted(explained.items()))
    else:
        verdict = 'identical'
    for pr in problems:
        det.append('  PROBLEM: ' + pr)
    det.append('  verdict: ' + verdict)
    return verdict, counts, problems, explained


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
    packages = PACKAGES
    i = 2
    while i < len(args):
        if args[i] == '--patches':
            patches_path = args[i + 1]
            i += 2
        elif args[i] == '--details':
            details = args[i + 1]
            i += 2
        elif args[i] == '--packages':
            packages = args[i + 1].split(',')
            i += 2
        else:
            print('unknown argument %s' % args[i])
            sys.exit(2)
    patches = parse_patches(patches_path) if patches_path else {}
    det = []
    worst = 0
    for p in packages:
        old = Package(os.path.join(old_dir, p + '.u'))
        newpath = os.path.join(new_dir, p + '.u')
        if not os.path.exists(newpath):
            print('%-16s MISSING' % p)
            worst = 2
            continue
        new = Package(newpath)
        verdict, counts, problems, explained = compare_package(p, old, new, patches.get(p), det)
        print('%-16s %-12s exports %5d, byte-identical %5d: %s' % (
            p, verdict.split(' ')[0], len(old.exports), counts.get('identical', 0), verdict))
        if problems:
            worst = max(worst, 1)
    if details:
        with open(details, 'w') as f:
            f.write('\n'.join(det) + '\n')
    sys.exit(worst)


if __name__ == '__main__':
    main()
