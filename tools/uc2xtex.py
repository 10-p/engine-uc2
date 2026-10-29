#!/usr/bin/env python3
"""Texture family of Unreal Championship 2's Xbox packages turned into the PC format (licensee 0): the Mips of a
Texture rebuilt from the pixels in the disc's XPR1 bundles. Used by the Xbox package converter.

    import uc2xtex
    out = uc2xtex.convert(pkg, i, ctx)   # export i of an Xbox package (uc2xpkg.Package), PC format:
                                         # bytes, or (bytes, lazy) for a Texture with mips
    # lazy: offsets in the bytes of the i32 TLazyArray end positions, written relative to the export's start;
    # the package writer adds the export's file offset to each

ctx carries
  xpr           uc2xpr.XprSet over the disc's XboxTextures folder (the disc's Textures folder holds XPR0 files of
                the native code, no texture pixels)
  prefer        bundle basenames searched first: a map's own and the natives (['dm-legacy.xpr', 'natives.xpr'])
  warn(msg)     told about every texture without pixels and every choice between differing copies
  map_copies    optional uc2xtables.MapCopies: the maps' copies by the object they stand for, for the many texture
                package Textures the bundles hold only under a map's cla<N> name
  map_textures  optional MapTextures: the maps' copies of the texture packages' textures by key, for the copies
                map_copies does not place
  pixels        optional {export path (lower case): top mip bytes in the texture's own format}, for pixels other
                converters rebuild (G16 terrain heightmaps); the texture gets that one mip
  placeholder   optional 'image' (default) or 'empty': what a texture without pixels gets (placeholder_mips)

What the formats hold (Engine/Src/UnTex.cpp, XboxAssetConversion/Src/Launch.cpp, Bundler/*.cpp):
  Texture   tags, then Mips: i32 count, per mip a TLazyArray<BYTE> (i32 file offset of its end, i32 count, bytes),
            i32 USize, i32 VSize, u8 UBits, u8 VBits. The Xbox packages have the count 0 (StripTextureData); the
            pixels are in the XPRs under the texture's object name (D3DTexture: Common, Data, Lock, Format, Size);
            a map's textures are renamed cla<N> and bundled in <Map>.xpr.
  Cubemap   a Texture whose Mips are empty in both formats (the faces are Textures): copied.
  Palette   tags, TArray<FColor>: the same in both formats, copied.
  Font      tags, Characters, Textures, Kerning, CharRemap, IsRemapped: the same in both formats, copied; a Font
            none of whose pages has pixels on the disc (fonts_without_pages) is left out by the package converter,
            with the exports inside it (its glyphs would draw as placeholder blocks).
How the bundler stored each PC format (UMaterialExporterRDF, CTexture2D):
  DXT1/3/5  the PC mips verbatim in block order ('Raw 1': D3DX loads the exported DDS with a full chain, so the
            data runs down to 1x1 whatever the header's level count). D3DX decodes and re-encodes the levels
            narrower than 4 texels, so those are close to, not equal to, the PC blocks.
  RGBA8     the top mip as D3DFMT_B8G8R8A8, swizzled; the lower levels are the bundler's own (linear filter from
            the top mip). The PC lower mips are UTexture::CreateMips' box filter, which mip_chain() repeats.
  others    P8, L8, A8, G16, V8U8, L6V5U5, V16U16, RRRGGGBBB were not bundled: those Textures, and the ones no map,
            character or the natives loaded at conversion time, get a placeholder.
"""
import array
import os
import re
import struct

from uc2xpkg import Reader, FormatError, skip_stack

# ETextureFormat (Engine/Inc/UnRenderResource.h)
TEXF_P8, TEXF_RGBA7, TEXF_RGB16, TEXF_DXT1, TEXF_RGB8, TEXF_RGBA8 = 0, 1, 2, 3, 4, 5
TEXF_NODATA, TEXF_DXT3, TEXF_DXT5, TEXF_L8, TEXF_G16, TEXF_RRRGGGBBB = 6, 7, 8, 9, 10, 11
TEXF_V8U8, TEXF_L6V5U5, TEXF_V16U16, TEXF_A8, TEXF_R5G6B5 = 12, 13, 14, 15, 16
FORMAT_NAMES = {0: 'P8', 1: 'RGBA7', 2: 'RGB16', 3: 'DXT1', 4: 'RGB8', 5: 'RGBA8', 6: 'NODATA', 7: 'DXT3', 8: 'DXT5',
                9: 'L8', 10: 'G16', 11: 'RRRGGGBBB', 12: 'V8U8', 13: 'L6V5U5', 14: 'V16U16', 15: 'A8', 16: 'R5G6B5'}
DXT_BLOCK = {TEXF_DXT1: 8, TEXF_DXT3: 16, TEXF_DXT5: 16}
TEXEL_BYTES = {TEXF_RGBA8: 4, TEXF_V16U16: 4, TEXF_RGBA7: 4, TEXF_G16: 2, TEXF_V8U8: 2, TEXF_L6V5U5: 2,
               TEXF_R5G6B5: 2, TEXF_RGB16: 2, TEXF_RGB8: 3}

# Xbox D3DFORMAT codes (Format bits 8-15 of the resource header) and the swizzled texel sizes
X_L8, X_A8R8G8B8, X_X8R8G8B8, X_DXT1, X_DXT3, X_DXT5, X_A8, X_B8G8R8A8 = 0x00, 0x06, 0x07, 0x0C, 0x0E, 0x0F, 0x19, 0x3B
X_BLOCK = {X_DXT1: 8, X_DXT3: 16, X_DXT5: 16}
X_SWIZZLED = {X_L8: 1, X_A8: 1, X_A8R8G8B8: 4, X_X8R8G8B8: 4, X_B8G8R8A8: 4}
# the Xbox formats a PC format can come back from
FROM_XBOX = {TEXF_DXT1: (X_DXT1,), TEXF_DXT3: (X_DXT3,), TEXF_DXT5: (X_DXT5,),
             TEXF_RGBA8: (X_B8G8R8A8, X_A8R8G8B8, X_X8R8G8B8), TEXF_L8: (X_L8,), TEXF_A8: (X_A8,)}

D3DCOMMON_TYPE_TEXTURE = 0x00040000
D3DFORMAT_CUBEMAP = 0x4

COPIED_CLASSES = ('Cubemap', 'Palette', 'Font')
CONVERTED_CLASSES = ('Texture',)


# ---------------------------------------------------------------------------------------------------------------
# Tagged properties
# ---------------------------------------------------------------------------------------------------------------
def read_tags(pkg, data, flags, pos=0):
    """The state frame and tagged properties of an export: ({(name, index): value}, offset of the native data,
    which is uc2xpkg.object_head_end's). Byte, int and object values come back as ints, bools as bools, the rest
    as raw bytes."""
    none = pkg.none_index()
    r = Reader(data, pos)
    skip_stack(r, flags)
    props = {}
    while True:
        ver = r.u8()
        key = r.i32()
        if ver == 0:
            if key == none:
                break
            name = pkg.name(key)
        else:
            if key == 0:
                break
            name = pkg.ref_path(key).split('.')[-1]
        info = r.u8()
        typ = info & 0x0F
        if ver == 0 and typ == 10:
            r.i32()
        size = {0x00: 1, 0x10: 2, 0x20: 4, 0x30: 12, 0x40: 16}.get(info & 0x70)
        if size is None:
            size = {0x50: r.u8, 0x60: r.u16, 0x70: r.i32}[info & 0x70]()
        index = 0
        if info & 0x80 and typ != 3:
            b = r.u8()
            if b & 0x80 == 0:
                index = b
            elif b & 0xC0 == 0x80:
                index = ((b & 0x7F) << 8) | r.u8()
            else:
                c = r.take(3)
                index = ((b & 0x3F) << 24) | (c[0] << 16) | (c[1] << 8) | c[2]
        if typ == 3:
            props[(name, index)] = bool(info & 0x80)
            continue
        raw = r.take(size)
        if typ == 1 and size == 1:
            props[(name, index)] = raw[0]
        elif typ in (2, 5) and size == 4:
            props[(name, index)] = struct.unpack('<i', raw)[0]
        else:
            props[(name, index)] = raw
    return props, r.p


class TextureInfo:
    """What a Texture's tags say about its pixels (class defaults where a tag is absent)."""

    def __init__(self, props):
        self.format = props.get(('Format', 0), TEXF_P8)
        self.usize = props.get(('USize', 0), 0)
        self.vsize = props.get(('VSize', 0), 0)
        self.ubits = props.get(('UBits', 0), 0)
        self.vbits = props.get(('VBits', 0), 0)
        self.parametric = props.get(('bParametric', 0), False)

    @property
    def full_levels(self):
        """Levels of UTexture::CreateMips' full chain: down to 1x1."""
        return max(self.ubits, self.vbits) + 1

    @property
    def short_levels(self):
        """Levels down to where the smaller side reaches 1 (what the renderers use at most)."""
        return min(self.ubits, self.vbits) + 1


def mip_dims(info, level):
    return max(1, info.usize >> level), max(1, info.vsize >> level)


def mip_bytes(fmt, w, h):
    """Bytes of one PC mip of a format."""
    if fmt in DXT_BLOCK:
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * DXT_BLOCK[fmt]
    return w * h * TEXEL_BYTES.get(fmt, 1)


# ---------------------------------------------------------------------------------------------------------------
# XPR texture resources
# ---------------------------------------------------------------------------------------------------------------
class XboxTexture:
    """A D3DTexture/D3DCubeTexture resource header: Common, Data (offset into video memory), Lock, Format, Size."""

    def __init__(self, bundle, header, name=''):
        self.bundle, self.name = bundle, name
        self.common, self.data, self.lock, self.format_word, self.size = struct.unpack('<5I', header[:20])
        fw = self.format_word
        self.cube = bool(fw & D3DFORMAT_CUBEMAP)
        self.xformat = (fw >> 8) & 0xFF
        self.levels = (fw >> 16) & 0xF
        self.width = 1 << ((fw >> 20) & 0xF)
        self.height = 1 << ((fw >> 24) & 0xF)

    def is_texture(self):
        return self.common & 0x00070000 == D3DCOMMON_TYPE_TEXTURE


def xbox_texture_layout(xformat, width, height, levels):
    """Where each level of a 2D Xbox texture lies in its data: [(width, height, offset, size)]. The levels follow
    one another without padding; compressed levels are whole 4x4 blocks, swizzled ones width*height texels."""
    out, pos = [], 0
    for level in range(levels):
        w, h = max(1, width >> level), max(1, height >> level)
        if xformat in X_BLOCK:
            size = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * X_BLOCK[xformat]
        else:
            size = w * h * X_SWIZZLED[xformat]
        out.append((w, h, pos, size))
        pos += size
    return out


def swizzle_offsets(width, height):
    """Texel offsets of the Xbox swizzle (XGSwizzleRect): the bits of u and v interleaved from bit 0, u first,
    the longer side's remaining bits on top. Returns (offset of each u, offset of each v); a texel is at their sum."""
    umask = vmask = 0
    bit, w, h = 0, width, height
    while w > 1 or h > 1:
        if w > 1:
            umask |= 1 << bit
            bit += 1
            w >>= 1
        if h > 1:
            vmask |= 1 << bit
            bit += 1
            h >>= 1

    def spread(n, mask):
        out = []
        for value in range(n):
            r, k, m = 0, 0, mask
            while m:
                low = m & -m
                if value >> k & 1:
                    r |= low
                k += 1
                m ^= low
            out.append(r)
        return out

    return spread(width, umask), spread(height, vmask)


def unswizzle(data, width, height, texel):
    """Linear rows of a swizzled level (texel = bytes per texel: 1, 2 or 4)."""
    code = {1: 'B', 2: 'H', 4: 'I'}[texel]
    src = array.array(code)
    src.frombytes(bytes(data[:width * height * texel]))
    uo, vo = swizzle_offsets(width, height)
    out = array.array(code)
    for v in vo:
        out.extend([src[v + u] for u in uo])
    return out.tobytes()


def xbox_to_pc(xformat, pcformat, level, width, height):
    """One Xbox level as the PC format's bytes."""
    if xformat in X_BLOCK:
        return bytes(level)
    texels = unswizzle(level, width, height, X_SWIZZLED[xformat])
    if pcformat == TEXF_RGBA8:
        a = array.array('I')
        a.frombytes(texels)
        if xformat == X_B8G8R8A8:
            # B8G8R8A8 is the DWORD B<<24|G<<16|R<<8|A: bytes A,R,G,B, the PC's B,G,R,A reversed
            a.byteswap()
        elif xformat == X_X8R8G8B8:
            a = array.array('I', [t | 0xFF000000 for t in a])
        return a.tobytes()
    return texels


def texture_resources(xpr, name):
    """The texture resources named name in an XprSet's bundles, in the set's bundle order."""
    out = []
    for base in xpr.holders(name):
        tex = XboxTexture(base, xpr.bundle(base).header(name, 20), name.lower())
        if tex.is_texture():
            out.append(tex)
    return out


def read_video(xpr, tex, size):
    """size bytes of a resource's video memory, read from its bundle's file (the rest of the bundle is not)."""
    bundle = xpr.bundle(tex.bundle)
    with open(bundle.path, 'rb') as f:
        f.seek(bundle.head + tex.data)
        return f.read(size)


MAP_TEXTURE = re.compile(r'^cla\d+$', re.I)


def _fits(ctx, name, info, bundle=None):
    """The texture resources of that name, size and a format the PC format comes back from."""
    return [t for t in texture_resources(ctx.xpr, name)
            if not t.cube and t.xformat in FROM_XBOX.get(info.format, ())
            and t.width == info.usize and t.height == info.vsize and t.levels >= 1
            and (bundle is None or t.bundle == bundle)]


def _majority(ctx, fits):
    """Of several resources that fit, the first of those whose top level is the most common, and how many
    different top levels there are."""
    if len(fits) < 2:
        return fits[0], 1
    tops = []
    for t in fits:
        w, h, off, size = xbox_texture_layout(t.xformat, t.width, t.height, 1)[0]
        tops.append(read_video(ctx.xpr, t, size))
    best = max(range(len(fits)), key=lambda k: (tops.count(tops[k]), -k))
    return fits[best], len(set(tops))


def find_texture(ctx, name, info, key=None, path=None):
    """The XPR resource holding a texture's pixels, how many resources fit and how many different top levels
    they have: by the texture's name, from the first of ctx.prefer's bundles that holds it; merged map textures
    (cla<N>, numbered per map) only from those. Elsewhere, from the maps' copies of the texture at path
    (ctx.map_copies, uc2xtables.MapCopies) or from the copies with its key (ctx.map_textures) that stand for no
    other object, the content most of them agree on."""
    prefer = [p.lower() for p in getattr(ctx, 'prefer', ())]
    fits = _fits(ctx, name, info)
    preferred = sorted((t for t in fits if t.bundle in prefer), key=lambda t: prefer.index(t.bundle))
    if preferred:
        return preferred[0], len(fits), 1
    if fits and not MAP_TEXTURE.match(name):
        tex, split = _majority(ctx, fits)
        return tex, len(fits), split
    named = getattr(ctx, 'map_copies', None)
    if named is not None and path is not None:
        found = [t for m, copy in named.find(path) for t in _fits(ctx, copy, info, m + '.xpr')]
        if found:
            tex, split = _majority(ctx, found)
            return tex, len(found), split
    copies = getattr(ctx, 'map_textures', None)
    if key is not None and copies is not None:
        found = [t for bundle, copy in copies.find(key)
                 if named is None or named.stands_for(bundle[:-len('.xpr')], copy) in (None, (path or '').lower())
                 for t in _fits(ctx, copy, info, bundle)]
        if found:
            tex, split = _majority(ctx, found)
            return tex, len(found), split
    return None, len(fits), 0


def texture_key(info, props):
    """What identifies a texture in the packages it was copied into: its InternalTime (the clock of the editor
    session that imported it, so textures imported together share it), format, size and MipZero (the average
    colour, when it is set). None without an InternalTime."""
    t0, t1 = props.get(('InternalTime', 0), 0), props.get(('InternalTime', 1), 0)
    if not (t0 or t1):
        return None
    return t0, t1, info.format, info.usize, info.vsize, props.get(('MipZero', 0))


class MapTextures:
    """The maps' copies of texture-package textures (cla<N>, their pixels in the map's bundle) by texture_key:
    a texture package's Texture that no bundle holds under its own name takes its pixels from a map's copy.
    A key that more than one texture-package Texture has names no copy. Fill it with every Xbox texture
    package and map before converting the texture packages:

        copies = uc2xtex.MapTextures()
        copies.add_originals(texture_pkg)           # each XboxTextures/*.utx
        copies.add(map_pkg, 'dm-legacy.xpr')        # each map, with its texture bundle
        ctx.map_textures = copies
    """

    def __init__(self):
        self.copies = {}
        self.originals = {}

    @staticmethod
    def _keys(pkg):
        for i, e in enumerate(pkg.exports):
            if pkg.export_class(i) == 'Texture' and e.data:
                props, _ = read_tags(pkg, e.data, e.flags)
                key = texture_key(TextureInfo(props), props)
                if key is not None:
                    yield i, key

    def add(self, pkg, bundle):
        """Record the Textures of an Xbox map whose pixels are in bundle (a basename)."""
        for i, key in self._keys(pkg):
            self.copies.setdefault(key, []).append((bundle.lower(), pkg.name(pkg.exports[i].name)))

    def add_originals(self, pkg):
        """Record the Textures of an Xbox texture package."""
        for i, key in self._keys(pkg):
            self.originals.setdefault(key, set()).add((pkg.path, pkg.export_path(i).lower()))

    def merge(self, other):
        """Take in what another MapTextures recorded (one filled per worker, say)."""
        for key, found in other.copies.items():
            self.copies.setdefault(key, []).extend(found)
        for key, found in other.originals.items():
            self.originals.setdefault(key, set()).update(found)

    def find(self, key):
        """The (bundle, resource name) of the maps' copies of the texture with that key."""
        if len(self.originals.get(key, ())) > 1:
            return []
        return self.copies.get(key, [])


# ---------------------------------------------------------------------------------------------------------------
# Mip chains
# ---------------------------------------------------------------------------------------------------------------
_ONE_OVER_255 = array.array('f', [0.00392156862745])[0]
_PLANE = array.array('f', [i * _ONE_OVER_255 for i in range(256)]).tolist()


def _f32(values):
    return array.array('f', values).tolist()


def box_filter_rgba8(src, width, height):
    """The next RGBA8 mip as UTexture::CreateMips builds it: each channel the float average of a 2x2 block
    (FColor::Plane, FPlane sum and /4, FColor(FPlane) floors *255), in single precision."""
    dw, dh = max(1, width >> 1), max(1, height >> 1)
    ud = 1 & (width - 1)
    vd = (1 & (height - 1)) * width
    corner = [u * 2 + v * 2 * width for v in range(dh) for u in range(dw)]
    out = bytearray(dw * dh * 4)
    for ch in range(4):
        c = src[ch::4]
        s = [_PLANE[c[t]] for t in corner]
        for d in (ud, vd, ud + vd):
            s = _f32([x + _PLANE[c[t + d]] for x, t in zip(s, corner)])
        # x * 0.25 is exact; the product with 255 is rounded once, as the single-precision multiply does
        s = _f32([x * 0.25 * 255 for x in s])
        out[ch::4] = bytes(min(255, max(0, int(x // 1))) for x in s)
    return bytes(out)


def pc_level_count(info, tex):
    """How many mips the PC texture had, from the Xbox header. The exporter wrote 'Levels' as the PC count, capped
    at the smaller side's chain for non-square textures (UTexture::GetMinNumMips), and for RGBA8 the bundler caps
    it there for square ones too; where the cap hides the count, the full chain to 1x1 (CreateMips) is assumed."""
    if tex.levels <= 1 and info.short_levels > 1:
        return 1
    if tex.levels < info.short_levels or info.format in DXT_BLOCK and info.usize == info.vsize:
        return min(tex.levels, info.full_levels)
    return info.full_levels


def mip_chain(ctx, info, tex):
    """The PC mips [(width, height, bytes)] of a texture from its XPR resource, and how they were obtained:
    {'levels': n, 'xbox': levels taken from the bundle, 'filtered': levels rebuilt by the box filter}."""
    count = pc_level_count(info, tex)
    note = {'levels': count, 'xbox': 0, 'filtered': 0}
    mips = []
    if tex.xformat in X_BLOCK:
        # the data runs down to 1x1 (D3DX's full chain), whatever tex.levels says
        layout = xbox_texture_layout(tex.xformat, tex.width, tex.height, info.full_levels)[:count]
        raw = read_video(ctx.xpr, tex, layout[-1][2] + layout[-1][3])
        for w, h, off, size in layout:
            mips.append((w, h, raw[off:off + size]))
        note['xbox'] = len(mips)
        return mips, note
    layout = xbox_texture_layout(tex.xformat, tex.width, tex.height, 1)
    w, h, off, size = layout[0]
    raw = read_video(ctx.xpr, tex, size)
    top = xbox_to_pc(tex.xformat, info.format, raw, w, h)
    mips.append((w, h, top))
    note['xbox'] = 1
    while len(mips) < count:
        w, h, data = mips[-1]
        if info.format == TEXF_RGBA8:
            data = box_filter_rgba8(data, w, h)
        else:
            data = box_filter_bytes(data, w, h)
        mips.append((max(1, w >> 1), max(1, h >> 1), data))
        note['filtered'] += 1
    return mips, note


def box_filter_bytes(src, width, height):
    """The next mip of a one-byte format (L8, A8) as UTexture::CreateMips builds it."""
    dw, dh = max(1, width >> 1), max(1, height >> 1)
    ud = 1 & (width - 1)
    vd = (1 & (height - 1)) * width
    corner = [u * 2 + v * 2 * width for v in range(dh) for u in range(dw)]
    s = [_PLANE[src[t]] for t in corner]
    for d in (ud, vd, ud + vd):
        s = _f32([x + _PLANE[src[t + d]] for x, t in zip(s, corner)])
    s = _f32([x * 0.25 * 255 for x in s])
    return bytes(min(255, max(0, int(x // 1))) for x in s)


# DefaultTexture of Natives.xpr, what the Xbox drew for a texture missing from its bundles: a 4x4 DXT1 block,
# green top-left and bottom-right quarters, magenta the others
_MISSING = (0x07F0, 0xF80F)


def _rgb565_bgra(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return bytes((b * 255 // 31, g * 255 // 63, r * 255 // 31, 255))


def placeholder_mips(info):
    """One full-size mip standing in for pixels the disc does not have. DXT and RGBA8 textures show the Xbox's
    DefaultTexture stretched over them (four quarters, as the Xbox drew a missing texture); the other formats are
    zero (palette entry 0, no luminance or alpha, a flat bump map)."""
    w, h = info.usize, info.vsize
    fmt = info.format
    if fmt in DXT_BLOCK:
        bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
        blocks = []
        for c in _MISSING:
            colour = struct.pack('<HHI', c, 0, 0)
            alpha = {TEXF_DXT1: b'', TEXF_DXT3: b'\xff' * 8, TEXF_DXT5: b'\xff\xff' + bytes(6)}[fmt]
            blocks.append(alpha + colour)
        rows = []
        for by in range(bh):
            row = b''.join(blocks[(bx * 2 >= bw) != (by * 2 >= bh)] for bx in range(bw))
            rows.append(row)
        return [(w, h, b''.join(rows))]
    if fmt == TEXF_RGBA8:
        texel = [_rgb565_bgra(c) for c in _MISSING]
        rows = []
        for y in range(h):
            rows.append(b''.join(texel[(x * 2 >= w) != (y * 2 >= h)] for x in range(w)))
        return [(w, h, b''.join(rows))]
    return [(w, h, bytes(mip_bytes(fmt, w, h)))]


# ---------------------------------------------------------------------------------------------------------------
# PC Mips
# ---------------------------------------------------------------------------------------------------------------
def write_mips(mips, start):
    """UTexture's Mips as the PC writes them, starting at offset start of the export: (bytes, offsets in the
    export of the TLazyArray end positions). Each end position is written relative to the export's start."""
    out = bytearray(struct.pack('<i', len(mips)))
    lazy = []
    for w, h, data in mips:
        lazy.append(start + len(out))
        end = start + len(out) + 8 + len(data)
        out += struct.pack('<ii', end, len(data)) + data
        out += struct.pack('<iiBB', w, h, (w - 1).bit_length(), (h - 1).bit_length())
    return bytes(out), lazy


def read_mips(data, pos):
    """A Mips array at pos: ([(seek field offset, width, height, bytes)], end)."""
    r = Reader(data, pos)
    mips = []
    for _ in range(r.count()):
        at = r.p
        r.i32()
        size = r.count(1 << 28)
        body = r.take(size)
        w, h = r.i32(), r.i32()
        r.take(2)
        mips.append((at, w, h, body))
    return mips, r.p


# ---------------------------------------------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------------------------------------------
def texture_report(pkg, i, ctx):
    """What convert does with Texture export i: (head end, TextureInfo, XboxTexture or None, mips, note)."""
    e = pkg.exports[i]
    props, head = read_tags(pkg, e.data, e.flags)
    info = TextureInfo(props)
    name = pkg.name(e.name)
    xbox_mips, end = read_mips(e.data, head)
    if xbox_mips or end != len(e.data):
        raise FormatError('%s: Xbox Mips not empty (%d mips, %d bytes after)' % (
            pkg.export_path(i), len(xbox_mips), len(e.data) - end))
    if info.parametric:
        # UTexture::Serialize keeps parametric mips empty; they are filled at run time
        mips = [(w, h, b'') for w, h in (mip_dims(info, l) for l in range(info.full_levels))]
        return head, info, None, mips, {'kind': 'parametric'}
    supplied = getattr(ctx, 'pixels', None) or {}
    top = supplied.get(pkg.export_path(i).lower())
    if top is not None:
        if len(top) != mip_bytes(info.format, info.usize, info.vsize):
            raise FormatError('%s: %d bytes supplied for a %dx%d %s mip' % (
                pkg.export_path(i), len(top), info.usize, info.vsize, FORMAT_NAMES.get(info.format, info.format)))
        return head, info, None, [(info.usize, info.vsize, bytes(top))], {'kind': 'supplied'}
    tex, candidates, split = None, 0, 0
    if info.format in FROM_XBOX and info.usize > 0 and info.vsize > 0:
        path = '%s.%s' % (os.path.splitext(os.path.basename(pkg.path or ''))[0], pkg.export_path(i))
        tex, candidates, split = find_texture(ctx, name, info, texture_key(info, props), path)
    if tex is None:
        why = 'never bundled (%s)' % FORMAT_NAMES.get(info.format, info.format) \
            if info.format not in FROM_XBOX else 'not in the preferred bundles' if candidates else 'not in any bundle'
        mips = [] if getattr(ctx, 'placeholder', 'image') == 'empty' else placeholder_mips(info)
        return head, info, None, mips, {'kind': 'placeholder', 'why': why}
    mips, note = mip_chain(ctx, info, tex)
    note.update(kind='xpr', bundle=tex.bundle, resource=tex.name, candidates=candidates, split=split,
                xbox_levels=tex.levels)
    return head, info, tex, mips, note


def font_pages(pkg, i):
    """The export indices of Font export i's page Textures (its native Textures array) in its own package."""
    e = pkg.exports[i]
    _, head = read_tags(pkg, e.data, e.flags)
    r = Reader(e.data, head)
    r.take(17 * r.count())                  # Characters: StartU, StartV, USize, VSize (i32), TextureIndex (u8)
    refs = [r.i32() for _ in range(r.count())]
    return [v - 1 for v in refs if v > 0 and pkg.export_class(v - 1) == 'Texture']


def fonts_without_pages(pkg, ctx):
    """(export index, page count) of the Fonts of an Xbox package none of whose pages has pixels on the disc: their
    glyphs would draw as placeholder blocks. Before the conversion (the pages' data is the Xbox's)."""
    out = []
    for i in range(len(pkg.exports)):
        if pkg.export_class(i) == 'Font' and pkg.exports[i].data:
            pages = font_pages(pkg, i)
            if pages and all(texture_report(pkg, j, ctx)[4]['kind'] == 'placeholder' for j in pages):
                out.append((i, len(pages)))
    return out


def convert(pkg, i, ctx):
    """The PC (licensee 0) data of export i of an Xbox package. Textures get their Mips back from the XPRs and
    come back as (data, offsets of the TLazyArray end positions, which are relative to the export's start and
    get its file offset added when the package is written); Cubemaps, Palettes and Fonts are copied as bytes
    (their native data is the same in both formats and holds no lazy arrays)."""
    e = pkg.exports[i]
    cls = pkg.export_class(i)
    if cls in COPIED_CLASSES:
        if cls == 'Cubemap':
            _, head = read_tags(pkg, e.data, e.flags)
            if e.data[head:] != b'\0\0\0\0':
                ctx.warn('%s: Cubemap with Mips on the Xbox, copied' % pkg.export_path(i))
        return e.data
    if cls not in CONVERTED_CLASSES:
        return e.data
    head, info, tex, mips, note = texture_report(pkg, i, ctx)
    if note['kind'] == 'placeholder':
        ctx.warn('%s: %s %dx%d %s, placeholder' % (
            pkg.export_path(i), FORMAT_NAMES.get(info.format, info.format), info.usize, info.vsize, note['why']))
    elif note.get('split', 0) > 1:
        ctx.warn('%s: %d resources fit with %d different contents, the most common taken (%s in %s)' % (
            pkg.export_path(i), note['candidates'], note['split'], note['resource'], note['bundle']))
    tail, lazy = write_mips(mips, head)
    if not lazy:
        return e.data[:head] + tail
    return e.data[:head] + tail, lazy
