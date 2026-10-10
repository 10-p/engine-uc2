#!/usr/bin/env python3
"""Reader for Unreal Championship 2 script packages (package version 151, licensee 0), shared by the script tools.

    from ucpkg import Package, parse_export, disasm, parse_patches, PatchedView, ...

What it reads (docs/porting/analysis/package-format-v151.md, scripts.md, script-patches.md):
  Package        summary, name/import/export tables (compact index = plain int32 from version 145; the 10-byte
                 export head of version 150+); object references resolved to dotted paths
  parse_export   one export with the v151 serialisers of Source/Core (UObject stack + tagged properties, UField,
                 UStruct with its script, UFunction, UState, UClass with dependencies and tagged defaults, every
                 UProperty kind, UConst, UEnum, UTextBuffer); the parse must consume the export exactly
  disasm         script bytecode token by token, as UStruct::SerializeExpr reads it
  parse_patches  Epic's load-time patch table, Source/Engine/Inc/ScriptPatches.h
  PatchedView    a shipped package with what ScriptPatches.h appends (the index space of the patches); with
                 full=True also the shipped exports' data with the patched bytecode in place: the package as the
                 load-time patcher (Engine/Src/UnScriptPatcher.cpp) leaves it in memory

Users: tools/scripts/uc-decompile.py, tools/scripts/uc-patches.py, tools/dev/script-diff.py, tools/dev/upkg151.py.
Bytes in, bytes out: names and strings are decoded as Latin-1 (0x02 glyph bytes pass through unchanged).
"""
import re
import struct


class Package:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        tag, self.version, self.licensee, self.flags = struct.unpack_from('<IHHI', d, 0)
        if tag != 0x9E2A83C1:
            raise ValueError('%s: not a package' % path)
        (self.name_count, self.name_offset, self.export_count, self.export_offset,
         self.import_count, self.import_offset) = struct.unpack_from('<6i', d, 12)
        self.guid = d[36:52]
        (gen_count,) = struct.unpack_from('<i', d, 52)
        self.generations = [struct.unpack_from('<ii', d, 56 + 8 * i) for i in range(gen_count)]
        if self.version < 150:
            raise ValueError('%s: version %d; only 150+ layouts are read here' % (path, self.version))
        # names
        self.names = []
        self.name_flags = []
        p = self.name_offset
        for _ in range(self.name_count):
            (n,) = struct.unpack_from('<i', d, p); p += 4
            s = d[p:p + n]; p += n
            (fl,) = struct.unpack_from('<I', d, p); p += 4
            self.names.append(s.rstrip(b'\0').decode('latin-1'))
            self.name_flags.append(fl)
        # imports: class package i32, class name i32, package index i16, object name i32
        self.imports = []
        p = self.import_offset
        for _ in range(self.import_count):
            cp, cn = struct.unpack_from('<ii', d, p); p += 8
            (pk,) = struct.unpack_from('<h', d, p); p += 2
            (on,) = struct.unpack_from('<i', d, p); p += 4
            self.imports.append({'class_package': self.names[cp], 'class_name': self.names[cn],
                                 'package': pk, 'name': self.names[on]})
        # exports: five i16 (class, super, super, package, package), name i32, flags u32, size i32, offset i32
        self.exports = []
        p = self.export_offset
        for _ in range(self.export_count):
            h = struct.unpack_from('<5h', d, p); p += 10
            (nm,) = struct.unpack_from('<i', d, p); p += 4
            (fl,) = struct.unpack_from('<I', d, p); p += 4
            (size,) = struct.unpack_from('<i', d, p); p += 4
            off = 0
            if size:
                (off,) = struct.unpack_from('<i', d, p); p += 4
            self.exports.append({'class': h[0], 'super': h[1], 'package': h[3], 'name': self.names[nm],
                                 'flags': fl, 'size': size, 'offset': off})

    # object references: >0 export index+1, <0 -(import index+1), 0 none
    def ref_name(self, i):
        if i == 0:
            return 'None'
        if i > 0:
            return self.export_path(i - 1)
        return self.import_path(-i - 1)

    def import_path(self, i):
        im = self.imports[i]
        parts = [im['name']]
        pk = im['package']
        while pk:
            if pk < 0:
                o = self.imports[-pk - 1]
                parts.append(o['name'])
                pk = o['package']
            else:
                o = self.exports[pk - 1]
                parts.append(o['name'])
                pk = o['package']
        return '.'.join(reversed(parts))

    def export_path(self, i):
        ex = self.exports[i]
        parts = [ex['name']]
        pk = ex['package']
        while pk:
            if pk > 0:
                o = self.exports[pk - 1]
            else:
                o = self.imports[-pk - 1]
            parts.append(o['name'])
            pk = o['package']
        return '.'.join(reversed(parts))

    def export_class(self, i):
        c = self.exports[i]['class']
        if c == 0:
            return 'Class'
        if c < 0:
            return self.imports[-c - 1]['name']
        return self.exports[c - 1]['name']

    def export_data(self, i):
        ex = self.exports[i]
        return self.data[ex['offset']:ex['offset'] + ex['size']]


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


def parse_export(pkg, i, spans=None):
    """Return a dict of named fields; raises ParseError if the layout does not consume the export exactly.
    With a dict `spans`, a UStruct's script is also located: spans['script'] = (offset of its size, end)."""
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
            at = r.p
            size = r.i32()
            f['script'] = r.take(size)
            if spans is not None:
                spans['script'] = (at, r.p)
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


class PatchedView(Package):
    """The shipped package with what ScriptPatches.h appends, in the patch's order: the index space the patch
    bytecode and objects were written in.

    full=False (script-diff.py): only the appended objects have data. full=True (uc-decompile.py --patched):
    every shipped export too, with the patched bytecode of each ScriptPatches entry in place of the shipped one,
    as FLinkerPatchData::Apply leaves the package in memory (it replaces the Script array of the function or
    state and nothing else; the Children links of appended fields are the loader's, see uc-decompile.py)."""
    def __init__(self, old, patch, full=False):  # noqa: super().__init__ not called on purpose (no file)
        self.path = old.path + ' + ScriptPatches.h'
        self.data = b''
        self.old, self.full = old, full
        self.version, self.licensee, self.flags = old.version, old.licensee, old.flags
        self.guid, self.generations = old.guid, old.generations
        self.names = list(old.names) + list(patch['names'])
        self.imports = list(old.imports) + [{'name': a, 'class_name': b, 'class_package': c, 'package': d}
                                            for a, b, c, d in patch['imports']]
        self.objects = {len(old.exports) + k: data for k, (dname, data) in enumerate(patch['objects'])}
        self.exports = list(old.exports) + [{'name': n, 'flags': f, 'class': c, 'super': sp, 'package': pk,
                                             'size': len(self.objects.get(len(old.exports) + k, b'')), 'offset': 0}
                                            for k, (n, f, c, sp, pk) in enumerate(patch['exports'])]
        self.patched = {}       # export index -> patched export bytes (full view only)
        if full:
            pkgname = patch['package']
            paths = {'%s.%s' % (pkgname, old.export_path(i)): i for i in range(len(old.exports))}
            for dname, script in patch['scripts']:
                i = paths.get(dname)
                if i is None:
                    raise ParseError('ScriptPatches.h entry %s: no such export in %s' % (dname, old.path))
                spans = {}
                parse_export(old, i, spans)
                a, b = spans['script']
                data = old.export_data(i)
                self.patched[i] = data[:a] + struct.pack('<i', len(script)) + script + data[b:]
                self.exports[i] = dict(self.exports[i], size=len(self.patched[i]))

    def export_data(self, i):
        if i in self.objects:
            return self.objects[i]
        if not self.full:
            return b''
        if i in self.patched:
            return self.patched[i]
        return self.old.export_data(i)
