#!/usr/bin/env python3
"""One-shot import of the pristine Epic tree into the engine-uc2 layout (M0).

Reads   <WorkUC2>/epic-sources (never written)
Writes  <repo>/Source, <repo>/Thirdparty, <repo>/Engine/Config, <repo>/docs/epic

Rules (see docs/SOURCE-LAYOUT.md):
  * only text files are imported; binaries (import textures, icons, prebuilt libs, tools) stay in epic-sources
  * code files get CRLF -> LF, bytes otherwise untouched (the sources are Latin-1, some .uc carry 0x02 bytes)
  * Windows-native project files (.vcproj/.sln/.bat/.rc/...) are copied byte for byte
The script refuses to overwrite: it is the record of how the first commit was made, not a sync tool.
"""
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EPIC = os.path.abspath(os.path.join(REPO, "..", "epic-sources"))

# Top-level epic folders that are source packages or build bookkeeping -> Source/<same name>
SOURCE_DIRS = """
Core Engine Editor UnrealEd Window WinDrv D3DDrv XboxDrv XboxAudio XboxLive IpDrv UWeb GUI UCGameplay
UCElements UnrealGame UnrealUtil Launch XboxLaunch UCC DebuggerLaunch Setup SDLDrv SDLLaunch Gameplay
UCInventory UCEffects UCCharacters UCMutators UCAnnouncements UCMenu UIX GUIDesigner UccDepend Bundler
XboxAssetConversion FNameDebug FNameDebugCOM ShaderData BuildProcess !BuildAll !BuildEngine
""".split()
SOURCE_ROOT_FILES = ["Avaris.sln", "defmacros.dsm", "fixit", "sync_LS.bat", "BuildFileList.txt", "ScriptFileList.txt"]

# epic folder -> Thirdparty/<name>  (headers/sources only; zlib and ogg/vorbis come from the UT2004 port)
THIRDPARTY_DIRS = {"nvTriStrip": "nvTriStrip", "nvDXT": "nvDXT", "DirectX8": "DirectX8"}

CONFIG_FILES = ["Default.ini", "DefUser.ini", "DefaultXbox.ini", "DefUserXbox.ini", "KeyBinds.ini"]
EPIC_DOCS = ["Help/ReadMe.txt", "Help/MacroLanguage.txt", "Help/XMV_Compression.txt", "Help/AVI2XMV.bat",
             "Bundler/readme.txt"]

LF_EXT = {".h", ".hpp", ".cpp", ".c", ".inl", ".inc", ".uc", ".huc", ".psh", ".vsh", ".pl", ".txt"}
RAW_EXT = {".vcproj", ".sln", ".ucproject", ".upkg", ".rc", ".rc2", ".def", ".idl", ".rgs", ".mk", ".bat",
           ".dsm", ".dsp", ".dsw", ".inx", ".inf", ".rdf", ".xap", ".ini", ".int"}
LF_NAMES = {"makefile", "fixit"}

stats = {"lf": 0, "raw": 0, "skipped": 0}
skipped_ext = {}


def classify(name):
    base = name.lower()
    ext = os.path.splitext(base)[1]
    if base in LF_NAMES or base.startswith("makefile") or ext in LF_EXT:
        return "lf"
    if ext in RAW_EXT:
        return "raw"
    return None


def copy(src, dst, mode):
    if os.path.exists(dst):
        sys.exit(f"refusing to overwrite {dst}")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(src, "rb") as f:
        data = f.read()
    if mode == "lf":
        data = data.replace(b"\r\n", b"\n")
    with open(dst, "wb") as f:
        f.write(data)
    stats[mode] += 1


def import_tree(src_root, dst_root):
    for cur, dirs, files in os.walk(src_root):
        dirs.sort()
        for name in sorted(files):
            src = os.path.join(cur, name)
            mode = classify(name)
            if mode is None:
                stats["skipped"] += 1
                ext = os.path.splitext(name.lower())[1] or "(noext)"
                skipped_ext[ext] = skipped_ext.get(ext, 0) + 1
                continue
            copy(src, os.path.join(dst_root, os.path.relpath(src, src_root)), mode)


for d in SOURCE_DIRS:
    import_tree(os.path.join(EPIC, d), os.path.join(REPO, "Source", d))
for f in SOURCE_ROOT_FILES:
    copy(os.path.join(EPIC, f), os.path.join(REPO, "Source", f), classify(f) or "raw")
for src, dst in THIRDPARTY_DIRS.items():
    import_tree(os.path.join(EPIC, src), os.path.join(REPO, "Thirdparty", dst))
for f in CONFIG_FILES:
    copy(os.path.join(EPIC, "System", f), os.path.join(REPO, "Engine", "Config", f), "raw")
for f in EPIC_DOCS:
    copy(os.path.join(EPIC, f), os.path.join(REPO, "docs", "epic", f.replace("/", "-")), "lf")

print(f"imported: {stats['lf']} LF-normalised, {stats['raw']} byte-for-byte; skipped {stats['skipped']} binary files")
print("skipped by extension:", ", ".join(f"{k}:{v}" for k, v in sorted(skipped_ext.items(), key=lambda kv: -kv[1])))
