#!/usr/bin/env python3
"""The property types of Unreal Championship 2's script packages (System/*.u), for walking the object references and
names inside tagged property values. Used by tools/uc2xrefs.py.

    from uc2types import Types
    types = Types(['System/Core.u', 'System/Engine.u', ...])
    p = types.find('Engine.StaticMesh', 'Materials')      # Prop: kind 'ArrayProperty', inner Prop, struct path
    p = types.prop('Engine.Actor.Tag')                    # by the property's own path (a tag stored by reference)

A tag says its type and size but not what an array holds; struct values are themselves tagged from package version
118 (UStructProperty::SerializeItem), except Vector, Rotator and Color, which are binary. So the walker needs, per
array property, its inner property, and per struct property, its struct (for the nested tags' owner).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scripts'))

import ucpkg  # noqa: E402

BINARY_STRUCTS = {'vector', 'rotator', 'color'}


class Prop:
    __slots__ = ('path', 'kind', 'inner', 'struct', 'arraydim')

    def __init__(self, path, kind, arraydim):
        self.path, self.kind, self.arraydim = path, kind, arraydim
        self.inner = None       # Prop of an ArrayProperty's elements
        self.struct = None      # struct path of a StructProperty


class Types:
    def __init__(self, paths):
        self.props = {}         # 'pkg.owner.name' (lower case) -> Prop
        self.supers = {}        # class or struct path (lower case) -> its super's path (lower case)
        self.structs = {}       # struct name (lower case) -> [struct paths]
        raw = []
        for path in paths:
            pkg = ucpkg.Package(path)
            base = os.path.splitext(os.path.basename(path))[0]
            for i, e in enumerate(pkg.exports):
                cls = pkg.export_class(i)
                if cls not in ucpkg.PROPERTY_CLASSES and cls not in ('Class', 'Struct', 'State'):
                    continue
                full = '%s.%s' % (base, pkg.export_path(i))
                try:
                    f = ucpkg.parse_export(pkg, i)
                except ucpkg.ParseError:
                    continue
                if cls in ('Class', 'Struct', 'State'):
                    sup = f.get('super')
                    if sup and sup != 'None':
                        self.supers[full.lower()] = self._full(base, pkg, sup).lower()
                    if cls == 'Struct':
                        self.structs.setdefault(e['name'].lower(), []).append(full.lower())
                    continue
                p = Prop(full, cls, f.get('arraydim', 1))
                self.props[full.lower()] = p
                raw.append((p, base, pkg, f))
        for p, base, pkg, f in raw:
            if p.kind == 'ArrayProperty' and f.get('inner') not in (None, 'None'):
                p.inner = self.props.get(self._full(base, pkg, f['inner']).lower())
            elif p.kind == 'StructProperty' and f.get('struct') not in (None, 'None'):
                p.struct = self._full(base, pkg, f['struct'])

    @staticmethod
    def _full(base, pkg, path):
        """ucpkg gives export paths without their package and import paths with it."""
        top = path.split('.')[0]
        if any(im['name'] == top and im['package'] == 0 for im in pkg.imports):
            return path
        return '%s.%s' % (base, path)

    def prop(self, path):
        return self.props.get(path.lower())

    def find(self, owner, name):
        """The property name of class or struct owner (a path), looked up along its supers."""
        seen = 0
        o = owner.lower()
        while o and seen < 64:
            p = self.props.get('%s.%s' % (o, name.lower()))
            if p is not None:
                return p
            o = self.supers.get(o)
            seen += 1
        return None

    def struct_path(self, name, owner=None):
        """A struct by its name (a tag's ItemName): the one owned along owner's supers if there are several."""
        cands = self.structs.get(name.lower(), [])
        if len(cands) <= 1 or owner is None:
            return cands[0] if cands else None
        o = owner.lower()
        while o:
            for c in cands:
                if c.startswith(o + '.'):
                    return c
            o = self.supers.get(o)
        return cands[0]
