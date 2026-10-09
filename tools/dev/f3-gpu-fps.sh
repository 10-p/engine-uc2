#!/usr/bin/env bash
# F-3: frame time of the anti-aliasing variants on this machine's GPU (the Quadro P620), headless (SDL's
# offscreen video driver: EGL, no window), from the driver's own statistics line (test key Stats=True) ("UC2GLDrv: stats: frames a-b:
# x ms/frame"; the F-2 FPS line is on main, not on this branch). DM-Deadbolt, the pre-match view (no input),
# 1280x720, 60 s per run, one run at a time, each inside bin/capped 2G.
#
#   tools/dev/f3-gpu-fps.sh [<tree> [<fxaa-binary>]]      default tree f3g (made from build-native)
#
# Variants: desktop GL none / 4x MSAA; GL4ES on OpenGL ES 3 none / 4x MSAA; GL4ES on OpenGL ES 2 (GLESVersion=2)
# with the FXAA binary (before F-3 removed it) when given. Prints one line per variant: the mean ms/frame of the
# stats lines after the first (frames 302 on), and the anti-aliasing line of the log. Output: logs/f3/gpu-fps-*.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${1:-f3g}; fx=${2:-}
out=$work/logs/f3; mkdir -p "$out"
"$repo/tools/dev/mkrun-uc2.sh" "$repo/build-native" $tree > /dev/null || exit 1
sys=$work/run/$tree/System
sed -i -E "s/^(WindowedViewportX|FullscreenViewportX)=.*/\\1=1280/; s/^(WindowedViewportY|FullscreenViewportY)=.*/\\1=720/" "$sys/Default.ini"
[ -n "$fx" ] && cp -f "$fx" "$sys/uc2-fxaa.bin"
ES2="-ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:GLESVersion=2"
glini=$repo/tools/dev/glini
NOAA=$("$glini" AntiAliasing=0)
FXNOAA="-glnoaa"   # the FXAA binary predates the test keys
one() {  # one <name> <binary> <args...>
  local name=$1 bin=$2; shift 2
  ( cd "$sys" && SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy "$work/bin/capped" 2G 75 -- ./$bin DM-Deadbolt -log -ini:System:[UC2Movie]:StartupMovies=False $("$glini" Stats=True CheckGLErrors=True) "$@" > "$out/gpu-fps-$name.out" 2>&1 )
  local ms; ms=$(grep -a -o "stats: frames [0-9]*-[0-9]*: [0-9.]* ms/frame" "$out/gpu-fps-$name.out" | tail -n +2 | sed -E 's/.*: ([0-9.]+) ms.*/\1/' | awk '{s+=$1;n++} END{if(n) printf "%.2f ms/frame (%.1f frames/s) over %d stats lines", s/n, 1000*n/s, n; else print "no stats line"}')
  echo "$name: $ms; $(grep -a -o 'GLES hardware: [^/]*/ [^(]*\|GL_RENDERER *: .*' "$out/gpu-fps-$name.out" | head -1); $(grep -a -o 'anti-aliasing: .*' "$out/gpu-fps-$name.out" | head -1); $(grep -a -o '[0-9]* GL errors so far' "$out/gpu-fps-$name.out" | tail -1)"
}
{
  one desktop-none uc2.bin -opengl $NOAA
  one desktop-msaa uc2.bin -opengl
  one es3-none uc2.bin -gl4es $NOAA
  one es3-msaa uc2.bin -gl4es
  [ -n "$fx" ] && one es2-fxaa uc2-fxaa.bin -gl4es $ES2 -nostartupmovies -glstats -glerrors
  [ -n "$fx" ] && one es2-none uc2-fxaa.bin -gl4es $FXNOAA $ES2 -nostartupmovies -glstats -glerrors
} | tee "$out/gpu-fps.txt"
