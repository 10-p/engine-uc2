#!/usr/bin/env python3
"""Reader for the Xbox packed resource bundles (.xpr, XPR1) of Unreal Championship 2's disc: the texture pixels,
the vertex and index arrays and the push buffers that the Xbox packages leave out. Used by tools/uc2xpkg.py.

    from uc2xpr import Bundle, XprSet
    b = Bundle('XboxTextures/DM-Legacy_uma.xpr')
    hdr = b.header('cla121_VS')          # the resource header in system memory (D3D resource, or Count/Offset)
    vid = b.vidmem                       # the bundle's video-memory part, where Data/Offset point

Layout (XboxDrv/Src/XBResource.cpp, CXBPackedResource::Create): u32 magic 'XPR1', u32 total size, u32 header size;
the header (system memory) holds u32 tag count, then per tag u32 name offset and u32 resource offset (both into the
header), the resource headers and the names; video memory follows the header to the end of the file.
Resources by bundle (XboxAssetConversion/Src/Launch.cpp):
  <Map>.xpr, Natives.xpr, <Character>.xpr   textures and cubemaps by texture name (D3DTexture: Common, Data, Lock,
                                             Format, Size)
  <Map>_uma.xpr, <Name>_uma.xpr              TUmaArray contents by '<object>_VS|_CS|_AS|_UV<n>' (static meshes),
                                             '<model>_vs_<section>' (BSP), '<instance>_CS', ... (u32 Count, u32 Offset)
  <Map>_pb.xpr, <Name>_pb.xpr                push buffers by '<mesh>_<section>_pb' (D3DPushBuffer: Common, Size,
                                             AllocationSize, Data; the commands in system memory, Data relative
                                             to the header, when the Common word has bit 31 set, else in video
                                             memory at Data)
  <Set>_a_uma.xpr                            animation motion blocks by '<set>_anim' (u32 Size, u32 Offset)
"""
import os
import struct

XPR1 = 0x31525058
XPR0 = 0x30525058


class Bundle:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            magic, total, head = struct.unpack('<III', f.read(12))
            if magic != XPR1:
                raise ValueError('%s: not an XPR1 bundle' % path)
            self.sysmem = f.read(head - 12)
        self.total, self.head = total, head
        self._vidmem = None
        n = struct.unpack_from('<I', self.sysmem, 0)[0]
        self.tags = {}
        self.order = []
        for i in range(n):
            name_off, res_off = struct.unpack_from('<II', self.sysmem, 4 + 8 * i)
            end = self.sysmem.index(b'\0', name_off)
            name = self.sysmem[name_off:end].decode('latin-1')
            self.order.append(name)
            self.tags.setdefault(name.lower(), res_off)

    @property
    def vidmem(self):
        """The video-memory part, read on first use."""
        if self._vidmem is None:
            with open(self.path, 'rb') as f:
                f.seek(self.head)
                self._vidmem = f.read(self.total - self.head)
        return self._vidmem

    def drop(self):
        self._vidmem = None

    def __contains__(self, name):
        return name.lower() in self.tags

    def header(self, name, size=32):
        off = self.tags[name.lower()]
        return self.sysmem[off:off + size]

    def uma(self, name, elem_size):
        """A TUmaArray's elements as bytes (Count elements of elem_size from Offset in video memory)."""
        count, off = struct.unpack_from('<II', self.header(name, 8))
        return count, self.vidmem[off:off + count * elem_size]

    def push_buffer(self, name):
        """A push buffer's command words as bytes (D3DPushBuffer: Common, Size, AllocationSize, Data)."""
        common, size, _alloc, data = struct.unpack_from('<IIII', self.header(name, 16))
        if common & 0x80000000:
            off = self.tags[name.lower()] + data
            return self.sysmem[off:off + size]
        return self.vidmem[data:data + size]


class XprSet:
    """The bundles of a disc's XboxTextures folder, looked up by resource name: the bundles named in prefer
    first (a map's own, the natives), then any bundle holding the name. Headers are read up front, video memory
    when a resource is used, and at most `keep` bundles' video memory stay loaded."""

    def __init__(self, folder, keep=24):
        self.folder = folder
        self.paths = {f.lower(): os.path.join(folder, f) for f in os.listdir(folder) if f.lower().endswith('.xpr')}
        self.bundles = {k: Bundle(p) for k, p in sorted(self.paths.items())}
        self.where = {}
        for k, b in self.bundles.items():
            for name in b.tags:
                self.where.setdefault(name, []).append(k)
        self.keep = keep
        self.loaded = []

    def bundle(self, base):
        return self.bundles.get(base.lower())

    def _touch(self, key):
        if key in self.loaded:
            self.loaded.remove(key)
        self.loaded.append(key)
        while len(self.loaded) > self.keep:
            self.bundles[self.loaded.pop(0)].drop()

    def all(self):
        return [self.bundles[k] for k in sorted(self.bundles)]

    def holders(self, name):
        """Basenames of every bundle holding resource name."""
        return list(self.where.get(name.lower(), ()))

    def find(self, name, prefer=()):
        """The first bundle holding resource name, trying the bundle names in prefer first."""
        keys = self.where.get(name.lower())
        if not keys:
            return None
        for base in prefer:
            if base.lower() in keys:
                self._touch(base.lower())
                return self.bundles[base.lower()]
        self._touch(keys[0])
        return self.bundles[keys[0]]
