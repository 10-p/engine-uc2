#!/usr/bin/env bash
# Saves check (web build under Node, no browser): the wasm build's file layer writes, lists, loads and deletes a
# profile where the web host's persistence daemon would look, and the `-ini:` overrides work and are written back.
#
#   tools/dev/mkrun-uc2.sh build-native c1w build-wasm-perf        # the tree: native ucc-bin + ucc-bin.js/.wasm
#   tools/dev/c1-web-saves.sh
#
# Every run is inside <repo>/../bin/capped (wasm under Node: 3G, native ucc: 2G). `node ucc-bin.js` is the
# commandlet host built with -sNODERAWFS: the same C++ file manager, the Emscripten libc, but the HOST file
# system relative to the working directory (System/), not WasmFS. So this shows the engine's side (which paths, which
# bytes, which calls) and not the browser's file layer (WasmFS permissions, the JS backend, IndexedDB, the daemon):
# that needs a browser, which nothing here may start. Output: PASS/FAIL lines; logs in <repo>/../logs/c1/web/.
set -u
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(cd "$repo/.." && pwd)
tree=${C1_WEB_TREE:-c1w}
sys=$work/run/$tree/System
out=${C1_WEB_OUT:-$work/logs/c1/web}
capped=$work/bin/capped
# The frontend whose userFiles compiler is asked (the profile folder glob needs a frontend that has it).
glue=${C1_UFRONT_GLUE:-$work/ufrontend/packages/glue/src/target/user-files.js}
mkdir -p "$out"
[ -f "$sys/ucc-bin.js" ] && [ -x "$sys/ucc-bin" ] || { echo "no run tree with ucc-bin.js at $sys (tools/dev/mkrun-uc2.sh build-native $tree build-wasm-perf)" >&2; exit 2; }
fails=0
result() { echo "$1 $2: $3"; [ "$1" = PASS ] || fails=$((fails+1)); }
check() { local n=$1 d; shift; if d=$("$@" 2>&1); then result PASS "$n" "$d"; else result FAIL "$n" "$d"; fi; }
wasm() { ( cd "$sys" && "$capped" 3G 300 -- node ucc-bin.js "$@" ); }
native() { ( cd "$sys" && "$capped" 2G 300 -- ./ucc-bin "$@" ); }
ini_get() { awk -v sec="$2" -v key="$3" '{ sub(/\r$/, "") } /^\[/ { cur = substr($0, 2, length($0) - 2); next }
  cur == sec { i = index($0, "="); if (i > 1 && substr($0, 1, i-1) == key) { print substr($0, i+1); found = 1; exit } } END { exit found ? 0 : 1 }' "$1"; }

export -f ini_get
cd "$sys" || exit 2
rm -rf Profiles Avaris.ini User.ini *.log *.log.*

echo "== the wasm build under Node writes a profile (Epic's layout since G-7: a folder per profile)"
wasm profiletest save WebProf "$out/wasm-saved.txt" > "$out/wasm-save.out" 2>&1; rc=$?
check w-save-exits-ok test "$rc" = 0
check w-profile-is-epics-folder-layout bash -c "ls Profiles && [ -f Profiles/WebProf/AVARIS_PROFILE.PRO ] && [ ! -e Profiles/WebProf.PRO ] && echo 'Profiles/WebProf/AVARIS_PROFILE.PRO, $(stat -c %s Profiles/WebProf/AVARIS_PROFILE.PRO) bytes'"
cp -f Profiles/WebProf/AVARIS_PROFILE.PRO "$out/wasm.PRO"
base=$(grep -a -m1 'Base directory:' "$sys/ucc.log" | sed 's/.*Base directory: //' | tr -d '\r')
check w-path-is-relative-to-the-engines-base-directory bash -c "echo 'base directory (the web: /System/): $base; profile: Profiles/WebProf/AVARIS_PROFILE.PRO -> the daemon sees /System/Profiles/WebProf/AVARIS_PROFILE.PRO'; [ '$base' = '$sys/' ]"

echo "== list / exists / load / delete through the same file layer"
wasm profiletest list x "$out/wasm-list.txt" > "$out/wasm-list.out" 2>&1
check w-list-names-the-profile bash -c "[ \"\$(cat '$out/wasm-list.txt')\" = WebProf ] && echo 'list: WebProf'"
wasm profiletest exists WebProf x > /dev/null 2>&1; e1=$?
wasm profiletest exists NoSuchProfile x > /dev/null 2>&1; e2=$?
check w-exists-true-for-the-saved-profile test "$e1" = 0
check w-exists-false-for-another-name test "$e2" != 0
wasm profiletest load WebProf "$out/wasm-loaded.txt" > "$out/wasm-load.out" 2>&1
check w-load-gives-back-the-saved-values bash -c "diff <(grep -a -v -E '^ (PlayerName|ProfileName|CustomText|MaplistText)=' '$out/wasm-saved.txt') <(grep -a -v -E '^ (PlayerName|ProfileName|CustomText|MaplistText)=' '$out/wasm-loaded.txt') > /dev/null && echo 'every stored property equal (the 4 names/texts the loader sets are left out, as in M13)'"

echo "== the web profile and the native one: same path, same bytes, each build loads the other's"
rm -rf Profiles
native profiletest save WebProf "$out/native-saved.txt" > "$out/native-save.out" 2>&1
check w-native-profile-same-path-same-bytes bash -c "cmp '$out/wasm.PRO' Profiles/WebProf/AVARIS_PROFILE.PRO && echo 'Profiles/WebProf/AVARIS_PROFILE.PRO: native == web, $(stat -c %s "$out/wasm.PRO") bytes'"
cp -f Profiles/WebProf/AVARIS_PROFILE.PRO "$out/native.PRO"
wasm profiletest load WebProf "$out/wasm-loaded-native.txt" > /dev/null 2>&1
rm -rf Profiles; mkdir -p Profiles/WebProf; cp -f "$out/wasm.PRO" Profiles/WebProf/AVARIS_PROFILE.PRO
native profiletest load WebProf "$out/native-loaded-web.txt" > /dev/null 2>&1
check w-web-loads-the-native-file-and-native-loads-the-web-file cmp "$out/wasm-loaded-native.txt" "$out/native-loaded-web.txt"

echo "== delete"
wasm profiletest delete WebProf x > /dev/null 2>&1; d=$?
check w-delete-removes-the-folder bash -c "[ $d = 0 ] && [ ! -e '$sys/Profiles/WebProf' ] && echo deleted"
wasm profiletest list x "$out/wasm-list2.txt" > /dev/null 2>&1
check w-list-is-empty-afterwards bash -c "[ ! -s '$out/wasm-list2.txt' ] && echo empty"

echo "== would the frontend's persistence daemon see it? (its own glob compiler, read-only import)"
if [ -f "$glue" ]; then
  cat > "$out/glob-check.mjs" <<MJS
import { compileUserFiles, matchesUserFile } from '$glue';
const nested = '/System/Profiles/WebProf/AVARIS_PROFILE.PRO', flat = '/System/Profiles/WebProf.PRO';
let c;
try { c = compileUserFiles(['System/Profiles/*/AVARIS_PROFILE.PRO']); } catch (e) { console.log('the folder glob is rejected (a frontend before G-7): ' + e.message.slice(0, 110)); process.exit(1); }
console.log('System/Profiles/*/AVARIS_PROFILE.PRO matches ' + nested + ': ' + matchesUserFile(c, nested) + '; matches the flat path: ' + matchesUserFile(c, flat));
process.exit(matchesUserFile(c, nested) && !matchesUserFile(c, flat) ? 0 : 1);
MJS
  check w-userFiles-folder-glob-matches-epics-path node "$out/glob-check.mjs"
else
  echo "SKIP w-userFiles-glob: $glue not found"
fi

echo "== -ini: overrides and their write-back under wasm (the C-1 ucc check, on the web build)"
rm -f Avaris.ini User.ini *.log
wasm verifyclasses '"-ini:System:[C1Test]:Key=wasm value"' '-ini:User:[C1Test]:UserKey=wasm-user' '-ini:bad' > "$out/wasm-ini.out" 2>&1; rc=$?
check w-ini-ucc-exits-normally test "$rc" = 0
check w-ini-log-lines bash -c "grep -a -F 'Ini override: [C1Test] Key=wasm value (System)' ucc.log && grep -a -F 'Ini override: [C1Test] UserKey=wasm-user (User)' ucc.log && grep -a -F 'Ini override ignored' ucc.log | head -1"
check w-ini-system-value-written-to-Avaris.ini bash -c "[ \"\$(ini_get Avaris.ini C1Test Key)\" = 'wasm value' ] && echo '[C1Test] Key=wasm value in Avaris.ini'"
check w-ini-user-value-written-to-User.ini bash -c "[ \"\$(ini_get User.ini C1Test UserKey)\" = 'wasm-user' ] && echo '[C1Test] UserKey=wasm-user in User.ini'"

echo "c1-web-saves: $([ "$fails" = 0 ] && echo ALL PASS || echo "$fails FAILED") (logs in $out)"
[ "$fails" = 0 ]
