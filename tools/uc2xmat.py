#!/usr/bin/env python3
"""What a static mesh section's material does with the order its triangles are drawn in, for the disc cook: the
material each section draws with (StaticMesh Materials[k].Material) followed through modifiers to its blending.
Used by tools/uc2xmesh.py.

    import uc2xmat
    kinds = uc2xmat.section_kinds(pkg, i, lookup)    # one per Materials entry
    # lookup(name): the package of that name (to resolve imports), or None
    shows = [k in uc2xmat.ORDER_SHOWS for k in kinds]

  blend           alpha blending (Shader OB_Normal with an Opacity, FinalBlend FB_AlphaBlend or FB_AlphaModulate,
                  a Texture with bAlphaTexture, the translucent pixel-shader materials): where a section's triangles
                  overlap on screen, their order decides the colour
  add/mul zwrite  additive or multiplicative blending with depth writes: the nearer of two overlapping triangles
                  hides the farther only when it is drawn first
  add/mul         the same without depth writes: the sum or product is the same in any order
  masked, opaque  alpha test or none: order shows only where coplanar triangles of different look overlap
  invisible, unknown
"""
import struct

import uc2xtex
from uc2xpkg import Reader

ORDER_SHOWS = ('blend', 'add/mul zwrite')

MODIFIERS = {'TexPanner', 'TexRotator', 'TexScaler', 'TexOscillator', 'TexEnvMap', 'TexMatrix', 'TexCoordSource',
             'VariableTexPanner', 'TexPannerTriggered', 'TexOscillatorTriggered', 'Modifier', 'ColorModifier',
             'TexModifier', 'OpacityModifier', 'MaterialSwitch', 'MaterialSequence'}
# Shader.OutputBlending
SHADER_BLENDING = {1: ('masked', 'Shader OB_Masked'), 2: ('add/mul', 'Shader OB_Modulate'),
                   3: ('add/mul', 'Shader OB_Translucent'), 4: ('invisible', 'Shader OB_Invisible'),
                   5: ('add/mul', 'Shader OB_Brighten'), 6: ('add/mul', 'Shader OB_Darken')}
# FinalBlend.FrameBufferBlending
FB_KIND = {1: 'add/mul', 2: 'blend', 3: 'blend', 4: 'add/mul', 5: 'add/mul', 6: 'add/mul', 7: 'invisible'}
TRANSLUCENT_PS = ('PSDiffuseOpacity', 'PSDiffuseOpacitySpecularMask', 'PSShenuSkin')


def materials(pkg, i):
    """Per Materials entry of StaticMesh export i: its Material reference (0 for none)."""
    e = pkg.exports[i]
    props, _ = uc2xtex.read_tags(pkg, e.data, e.flags)
    raw = props.get(('Materials', 0))
    if not isinstance(raw, bytes):
        return []
    r = Reader(raw)
    out = []
    pos = 4
    for _ in range(r.i32()):
        # each element is a tag stream of its own
        elem, pos = uc2xtex.read_tags(pkg, raw, 0, pos)
        ref = elem.get(('Material', 0), 0)
        out.append(ref if isinstance(ref, int) else 0)
    return out


# per package looked up: (the package, {export path, lower case: index}), kept for the worker's life
_INDEX = {}


class Resolver:
    """Object references to (package, export index), imports through lookup(top package name)."""

    def __init__(self, lookup):
        self.lookup = lookup

    def resolve(self, pkg, ref):
        if ref > 0:
            return pkg, ref - 1
        if ref == 0 or self.lookup is None:
            return None
        top, _, rest = pkg.ref_path(ref).partition('.')
        other = self.lookup(top)
        if other is None:
            return None
        key = id(other)
        if key not in _INDEX or _INDEX[key][0] is not other:
            _INDEX[key] = (other, {other.export_path(j).lower(): j for j in range(len(other.exports))})
        j = _INDEX[key][1].get(rest.lower())
        return (other, j) if j is not None else None

    def classify(self, pkg, j, depth=0):
        """(kind, what said so) for material export j of pkg."""
        if depth > 8:
            return 'unknown', 'too deep'
        cls = pkg.export_class(j)
        e = pkg.exports[j]
        if not e.data:
            return 'unknown', cls + ' without data'
        try:
            t, _ = uc2xtex.read_tags(pkg, e.data, e.flags)
        except Exception as ex:  # noqa: BLE001 - a material whose tags do not read is left as it is
            return 'unknown', '%s: %s' % (cls, ex)

        def ref(name):
            v = t.get((name, 0), 0)
            return v if isinstance(v, int) and not isinstance(v, bool) else 0

        def follow(name):
            rr = self.resolve(pkg, ref(name))
            return self.classify(rr[0], rr[1], depth + 1) if rr else ('opaque', '%s without %s' % (cls, name))
        if cls in ('Texture', 'Cubemap'):
            if t.get(('bAlphaTexture', 0)):
                return 'blend', cls + ' bAlphaTexture'
            if t.get(('bMasked', 0)):
                return 'masked', cls + ' bMasked'
            return 'opaque', cls
        if cls == 'Shader':
            ob = ref('OutputBlending')
            if ob == 0:
                return ('blend', 'Shader OB_Normal with Opacity') if ref('Opacity') else ('opaque', 'Shader')
            return SHADER_BLENDING.get(ob, ('unknown', 'Shader OutputBlending %d' % ob))
        if cls == 'FinalBlend':
            fb = ref('FrameBufferBlending')
            zwrite = t.get(('ZWrite', 0), True)
            if fb == 0:
                kind, said = follow('Material')
                if t.get(('AlphaTest', 0)) and kind == 'opaque':
                    return 'masked', 'FinalBlend AlphaTest'
                return kind, 'FinalBlend FB_Overwrite of ' + said
            kind = FB_KIND.get(fb, 'unknown')
            if kind == 'add/mul' and zwrite:
                kind = 'add/mul zwrite'
            return kind, 'FinalBlend FrameBufferBlending %d ZWrite %s' % (fb, bool(zwrite))
        if cls in MODIFIERS or cls.startswith('Tex'):
            for name in ('Material', 'Materials'):
                if ref(name):
                    return follow(name)
            return 'opaque', cls + ' without a material'
        if cls == 'Combiner':
            return 'opaque', cls
        if cls.startswith('PS'):
            if t.get(('bIsTranslucent', 0)) or cls in TRANSLUCENT_PS:
                return 'blend', cls
            if ref('Mask') and cls == 'PSBackBufferEffect':
                return 'blend', cls + ' Mask'
            if ref('Diffuse'):
                kind, said = follow('Diffuse')
                return (kind if kind in ('blend', 'masked') else 'opaque'), cls + ' diffuse ' + said
            return 'opaque', cls
        return 'unknown', cls


def section_kinds(pkg, i, lookup):
    """The kind of each Materials entry of StaticMesh export i (unknown where there is no material or it does not
    resolve)."""
    res = Resolver(lookup)
    out = []
    for m in materials(pkg, i):
        rr = res.resolve(pkg, m) if m else None
        out.append(res.classify(*rr)[0] if rr else 'unknown')
    return out
