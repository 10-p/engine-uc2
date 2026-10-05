#!/usr/bin/env bash
# Run one Windows program of the M12 build under wine, in the M12 wine prefix, and leave nothing running.
#
#   bin/capped 3G 300 -- tools/dev/m12-wine.sh <dir> <program.exe> [arguments...]
#   bin/capped 3G 300 -- tools/dev/m12-wine.sh <WorkUC2>/run/m12/System ucc.exe verifyclasses
#
# Always call it through <WorkUC2>/bin/capped (CLAUDE.md, "Running anything").
#
# - Prefix: $UC2_WINEPREFIX, default <WorkUC2>/.cache/wine-m12 (created on first use). Never ~/.wine.
# - No desktop integration and nothing installed: winemenubuilder, mscoree (Mono) and mshtml (Gecko) are
#   disabled, so wine never asks to download anything.
# - Display: the owner's DISPLAY/WAYLAND_DISPLAY are dropped. The program sees an X display only when the
#   caller sets UC2_WINE_DISPLAY (an Xvfb started by the caller, e.g. ":97").
# - WINEDEBUG=-all unless the caller set WINEDEBUG.
# - After the program returns (or is stopped by a TERM), `wineserver -k` for this prefix; exit status = the
#   program's (143 after a TERM).
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
dir=${1:?usage: m12-wine.sh <dir> <program.exe> [args...]}; prog=${2:?program}; shift 2

export WINEPREFIX=${UC2_WINEPREFIX:-$work/.cache/wine-m12}
# wine 10 here is a WoW64 build: one 64-bit prefix runs the 32-bit exe (WINEARCH=win32 is refused).
unset WINEARCH
export WINEDEBUG=${WINEDEBUG:--all}
export WINEDLLOVERRIDES="winemenubuilder.exe=d;mscoree=d;mshtml=d${UC2_WINEDLLOVERRIDES:+;$UC2_WINEDLLOVERRIDES}"
unset WAYLAND_DISPLAY
if [ -n "${UC2_WINE_DISPLAY:-}" ]; then export DISPLAY=$UC2_WINE_DISPLAY; else unset DISPLAY; fi

cleanup() { wineserver -k >/dev/null 2>&1; wineserver -w >/dev/null 2>&1; }
trap cleanup EXIT

if [ ! -f "$WINEPREFIX/system.reg" ]; then
  mkdir -p "$WINEPREFIX"
  wineboot -i >/dev/null 2>&1
  wineserver -w >/dev/null 2>&1
fi

cd "$dir" || exit 1
# In the background so that a TERM from capped's time limit reaches this script at once; the program is then
# stopped and the prefix's wineserver killed (the EXIT trap) before capped's own KILL.
wine "./$prog" "$@" &
pid=$!
# TERM (capped's time limit): stop now. INT: passed on as a Ctrl+C (clean exit), and we keep waiting.
trap 'kill -TERM $pid 2>/dev/null; exit 143' TERM
trap 'kill -INT $pid 2>/dev/null' INT
# UC2_WINE_STOP_AFTER=<s>: after that many seconds press Ctrl+C (wine turns SIGINT into a CTRL_C_EVENT; the
# engine's console handler requests a clean exit), then wait up to 20 s for the program to finish by itself.
if [ -n "${UC2_WINE_STOP_AFTER:-}" ]; then
  ( sleep "$UC2_WINE_STOP_AFTER"; kill -INT $pid 2>/dev/null; sleep 20; kill -TERM $pid 2>/dev/null ) &
  stopper=$!
fi
while :; do wait $pid; rc=$?; kill -0 $pid 2>/dev/null || break; done
[ -n "${stopper:-}" ] && kill $stopper 2>/dev/null
exit $rc
