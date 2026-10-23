#!/usr/bin/env bash
# Assemble a self-contained Windows folder of the game from a MinGW build, ready to copy to a real Windows
# machine, and check that the programs need nothing but Windows' own DLLs and the ones shipped beside them.
#
#   tools/dev/package-windows.sh [<build-dir> [<dest>]]
#   tools/dev/package-windows.sh build-windows                  ->  <repo>/../run/m12-package
#
#   UC2_PACKAGE_CONTENT=0   only System/ (programs, DLL, inis, script packages, localisation): to be put over the
#                           System folder of an asset tree that is already on the Windows machine
#   UC2_PACKAGE_STRIP=1     strip the debug information from the copies (uc2.exe 81 MB -> about 15 MB); the crash
#                           report's addresses are then resolved with the unstripped exe from the build directory
#
# The folder: <dest>/System (uc2.exe, ucc.exe, the editor UnrealEd.exe when the build has it (the 32-bit preset),
# OpenAL32.dll, our inis over the asset tree's System/, README-Windows.txt,
# DLLS.txt) and, unless UC2_PACKAGE_CONTENT=0, the content folders of the asset tree (<repo>/../assets/uc2-assets,
# UC2_ASSETS_DIR) with its cooked Sounds/Music (UC2_COOKED_DIR for another folder), hard-linked (a copy of the folder to another disk copies the data).
# Nothing in the asset tree is written. Re-running replaces <dest>, which must be under <repo>/../run.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
build=$(cd "${1:-$repo/build-windows}" && pwd)
dest=${2:-$work/run/m12-package}
assets=${UC2_ASSETS_DIR:-$work/assets/uc2-assets}
cooked=${UC2_COOKED_DIR:-$assets}
objdump=${OBJDUMP:-i686-w64-mingw32-objdump}

case "$(cd "$(dirname "$dest")" 2>/dev/null && pwd)/$(basename "$dest")" in
  "$work"/run/?*) ;;
  *) echo "refusing to write $dest: the package goes under $work/run" >&2; exit 1;;
esac
exe_uc2=$build/Source/SDLLaunch/uc2.exe
exe_ucc=$build/Source/UCC/ucc.exe
dll_al=$build/_deps/openalsoft-build/OpenAL32.dll
exe_ed=$build/Source/UnrealEd/UnrealEd.exe
for f in "$exe_uc2" "$exe_ucc" "$dll_al"; do [ -f "$f" ] || { echo "missing $f (cmake --build $build)" >&2; exit 1; }; done
programs="uc2.exe ucc.exe OpenAL32.dll"
[ -f "$exe_ed" ] && programs="uc2.exe ucc.exe UnrealEd.exe OpenAL32.dll"
[ -d "$assets/System" ] || { echo "no asset tree at $assets" >&2; exit 1; }

rm -rf "$dest"
mkdir -p "$dest"
cp -a "$assets/System" "$dest/System"
chmod -R u+w "$dest/System"
rm -f "$dest/System"/*.bin "$dest/System"/ucc-bin* "$dest/System"/*.so* "$dest/System"/*.log
cp -f "$repo"/Engine/Config/*.ini "$dest/System/"
cp -f "$exe_uc2" "$exe_ucc" "$dll_al" "$dest/System/"
[ -f "$exe_ed" ] && cp -f "$exe_ed" "$dest/System/"
if [ "${UC2_PACKAGE_STRIP:-0}" = 1 ]; then
  for f in $programs; do i686-w64-mingw32-strip --strip-debug "$dest/System/$f"; done
fi
if [ "${UC2_PACKAGE_CONTENT:-1}" = 1 ]; then
  for d in "$assets"/*/; do
    d=$(basename "$d"); [ "$d" = System ] && continue
    cp -al "$assets/$d" "$dest/$d"
  done
  for d in Sounds Music; do
    [ -d "$cooked/$d" ] && cp -al "$cooked/$d" "$dest/$d" || echo "note: no cooked $d at $cooked: the package is silent" >&2
  done
  mkdir -p "$dest/Save" "$dest/Cache"
fi

# Every DLL a program imports must be a Windows system DLL or one we ship. (Run-time LoadLibrary is not in an
# import table: OpenAL32.dll is loaded that way by the game, opengl32.dll and the XInput DLLs by SDL; DLLS.txt
# lists those too.)
system_dlls=" advapi32.dll avrt.dll gdi32.dll imm32.dll kernel32.dll msvcrt.dll ole32.dll oleaut32.dll setupapi.dll "
system_dlls+="shell32.dll user32.dll version.dll winmm.dll ws2_32.dll "
# The editor's: the common controls and dialogs, DirectInput, OpenGL (WGL).
system_dlls+="comctl32.dll comdlg32.dll dinput8.dll opengl32.dll "
shipped=" openal32.dll "
bad=0
{
  echo "DLLs of this folder's programs (objdump -p, 'DLL Name'), $(date -u +%Y-%m-%dT%H:%MZ), build $(git -C "$repo" rev-parse --short HEAD)"
  for f in $programs; do
    names=$("$objdump" -p "$dest/System/$f" | awk '/DLL Name:/{print $3}' | tr '\n' ' ')
    echo "$f: $names"
    for n in $names; do
      l=$(echo "$n" | tr 'A-Z' 'a-z')
      case "$system_dlls$shipped" in *" $l "*) ;; *) echo "  NOT A SYSTEM DLL AND NOT SHIPPED: $n"; bad=1;; esac
    done
  done
  cat <<'TXT'

Loaded at run time (LoadLibrary), not in any import table:
  OpenAL32.dll   shipped here: OpenAL Soft 1.25 built from source by this build (UC2ALAudio looks beside the
                 game first, then for an installed OpenAL32.dll / soft_oal.dll; none at all = "sound is off")
  opengl32.dll   Windows (SDL2 creates the WGL context; the graphics driver supplies the GL implementation)
  XInput1_4.dll, hid.dll   Windows (SDL2's game controller support; older Windows: xinput1_3/xinput9_1_0)
  mmdevapi (WASAPI), dsound.dll   Windows (OpenAL Soft's output backends, through COM)
(Seen under wine 10 with WINEDEBUG=+loaddll on the menu: OpenAL32.dll from this folder, OPENGL32.DLL,
XInput1_4.dll, hid.dll, and Windows' own helpers of the imported DLLs; nothing else from outside system32.)
No MinGW runtime DLL (libgcc_s_dw2-1, libstdc++-6, libwinpthread-1) is needed: all linked statically.
TXT
} > "$dest/System/DLLS.txt"
cat > "$dest/System/README-Windows.txt" <<'TXT'
Unreal Championship 2, portable build for Windows (32-bit, MinGW-w64), engine-uc2 milestone M12.

Start: uc2.exe in this folder (double-click; the working directory must be this System folder, so a shortcut
needs "Start in" set to it). Useful switches: -log (needs a console: start from cmd.exe), -nullrenderer,
-opengl, -640x480 ... -1600x1200, -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True. The log is Avaris.log beside the exe.
Dedicated server / tools: ucc.exe from cmd.exe in this folder, e.g.  ucc.exe server DM-Deadbolt  or
ucc.exe verifyclasses. Ctrl+C stops a server cleanly.
Sound: OpenAL32.dll beside the exe (OpenAL Soft). Delete it to run without sound.
A crash writes "=== CRASH ..." with addresses to Avaris.log; send the log with the exact uc2.exe used.
Editor (when UnrealEd.exe is here): UnrealEd.exe in this folder. It keeps its own settings in UC2Editor.ini
(made from Default.ini on the first start) and UnrealEd.ini (from DefUnrealEd.ini); its log is Editor.log.
TXT
echo "$dest: $(cd "$dest/System" && ls $programs | tr '\n' ' ')($(du -sh "$dest/System" | cut -f1) in System)"
cat "$dest/System/DLLS.txt" | sed -n 2,4p
[ $bad = 0 ] && echo "DLL check: PASS (only Windows system DLLs and OpenAL32.dll)" || { echo "DLL check: FAIL" >&2; exit 1; }
