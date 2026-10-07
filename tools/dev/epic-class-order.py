#!/usr/bin/env python3
"""Generate Source/Editor/Src/UC2EpicClassOrder.h: the order in which Epic's Win32 DLL build registered native
classes, which is the order `ucc make` walked sibling classes in (M14; docs/porting/analysis/scripts.md).

Why it matters. UEditorEngine::MakeScripts parses classes depth-first over the class tree, visiting siblings in
object-table order (TObjectIterator). A class that names a type declared in another class (`var UCPawn.EWeapon-
AnimType X;` in UCWeapon) compiles only if the other class was parsed first; the script sources rely on the
order Epic's machine produced. In the DLL build a class got its object index when its DLL's registrants were
processed: every IMPLEMENT_CLASS is a global whose constructor pushes the class on a LIFO list (UObject's
EC_NativeConstructor path), so the list holds the DLL's classes in reverse static-initialisation order (link
order of the .obj files = the .vcproj file order, then textual order within each file), and
UObject::ProcessRegistrants registers them in that list order; UField::Register registers a class first and then
its superclass if that was not registered yet. Our __STATIC_LINK build registers every class at start-up in the
generated AUTO_INITIALIZE_REGISTRANTS order instead, so this table restores Epic's order for the compiler only.

DLL load times during `ucc make` on Epic's machine: Core.dll and Engine.dll with ucc.exe (one list: Core's
constructors first, then Engine's, processed together at UObject::StaticInit), Editor.dll when the make
commandlet class is looked up, then each other package's DLL when the commandlet creates that package
(UPackage's constructor binds the DLL and processes its registrants), in EditPackages order.

Approximations (said so in the header): #if blocks around IMPLEMENT_CLASS are not evaluated; a class whose
DECLARE_CLASS is not found registers without pulling a superclass.

    tools/dev/epic-class-order.py            write the header
    tools/dev/epic-class-order.py --check    exit 1 if the committed header is out of date
"""
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(REPO, 'Source')
OUT = os.path.join(SRC, 'Editor', 'Src', 'UC2EpicClassOrder.h')

# (DLL/package, vcproj relative to Source, epoch). Epoch -1: loaded before any script is compiled; otherwise the
# EditPackages index of the package whose creation loads it.
DLLS = [
    ('Core', 'Core/Src/Core.vcproj', -1),
    ('Engine', 'Engine/Src/Engine.vcproj', -1),
    ('Editor', 'Editor/Src/Editor.vcproj', -1),
    ('IpDrv', 'IpDrv/Src/IpDrv.vcproj', 5),
    ('UWeb', 'UWeb/Src/UWeb.vcproj', 6),
    ('UnrealGame', 'UnrealGame/UnrealGame.vcproj', 8),
    ('UCElements', 'UCElements/UCElements.vcproj', 9),
    ('UCGameplay', 'UCGameplay/UCGameplay.vcproj', 10),
    ('GUI', 'GUI/GUI.vcproj', 15),
]
# Processed together at StaticInit: one LIFO list, Core's constructors pushed first.
GROUPS = [['Core', 'Engine'], ['Editor'], ['IpDrv'], ['UWeb'], ['UnrealGame'], ['UCElements'], ['UCGameplay'],
          ['GUI']]

DECL = re.compile(r'DECLARE_(?:ABSTRACT_|BASE_)?CLASS\s*\(\s*(\w+)\s*,\s*(\w+)\s*,[^,]*,\s*(\w+)\s*\)')
IMPL = re.compile(r'^\s*IMPLEMENT_CLASS\s*\(\s*(\w+)\s*\)', re.M)
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def read(path):
    with open(path, 'rb') as f:
        return f.read().decode('latin-1')


def vcproj_sources(path):
    base = os.path.dirname(path)
    out = []
    for m in re.finditer(r'RelativePath="([^"]+)"', read(path)):
        rel = m.group(1).replace('\\', '/')
        if rel.lower().endswith('.cpp'):
            p = os.path.normpath(os.path.join(base, rel))
            if not os.path.exists(p):  # case on Linux
                d, n = os.path.split(p)
                if os.path.isdir(d):
                    for e in os.listdir(d):
                        if e.lower() == n.lower():
                            p = os.path.join(d, e)
            out.append(p)
    return out


def main():
    supers = {}   # C++ class name -> (super C++ name, package)
    for root, _, files in os.walk(SRC):
        for f in files:
            if f.endswith(('.h', '.cpp')):
                text = COMMENT.sub('', read(os.path.join(root, f)))
                for m in DECL.finditer(text):
                    supers.setdefault(m.group(1), (m.group(2), m.group(3)))
    constructed = {}  # dll -> [class] in static-init order
    for dll, vcproj, _ in DLLS:
        seq = []
        for src in vcproj_sources(os.path.join(SRC, vcproj)):
            if not os.path.exists(src):
                continue
            seq += IMPL.findall(COMMENT.sub('', read(src)))
        constructed[dll] = seq
    epoch = {dll: e for dll, _, e in DLLS}
    registered = set()
    order = []   # (epoch, C++ class name, package)

    def register(cls, ep):
        if cls in registered:
            return
        registered.add(cls)
        pkg = supers.get(cls, (None, '?'))[1]
        order.append((ep, cls, pkg))
        sup = supers.get(cls, (None,))[0]
        if sup and sup != cls and sup in supers:
            register(sup, ep)

    for group in GROUPS:
        lifo = []
        for dll in group:
            lifo += constructed[dll]
        for cls in reversed(lifo):
            register(cls, epoch[group[0]])

    lines = ['/*=============================================================================',
             '\tUC2EpicClassOrder.h: GENERATED by tools/dev/epic-class-order.py; do not edit.',
             '',
             '\tThe order in which Epic\'s Win32 DLL build registered native classes (object-table order), used',
             '\tby UEditorEngine::MakeScripts to visit sibling classes as `ucc make` did on Epic\'s machine.',
             '\tEach row: epoch (-1 = before any script compiled, else the EditPackages index of the package',
             '\twhose creation loaded the DLL), package, class name without its C++ prefix.',
             '\tApproximation: #if blocks around IMPLEMENT_CLASS are not evaluated (see the generator).',
             '=============================================================================*/',
             '']
    lines.append('struct FUC2EpicClassOrder { INT Epoch; const TCHAR* Package; const TCHAR* Name; };')
    lines.append('static const FUC2EpicClassOrder GUC2EpicClassOrder[] =')
    lines.append('{')
    for ep, cls, pkg in order:
        lines.append('\t{ %d, TEXT("%s"), TEXT("%s") },' % (ep, pkg, cls[1:]))
    lines.append('};')
    lines.append('')
    text = '\n'.join(lines)
    if '--check' in sys.argv:
        ok = os.path.exists(OUT) and read(OUT) == text
        print('UC2EpicClassOrder.h is %s' % ('up to date' if ok else 'OUT OF DATE'))
        sys.exit(0 if ok else 1)
    with open(OUT, 'wb') as f:
        f.write(text.encode('latin-1'))
    print('%s: %d classes' % (OUT, len(order)))


if __name__ == '__main__':
    main()
