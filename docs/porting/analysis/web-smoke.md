# Early web smoke (M4w): the engine as WebAssembly

Running notebook, appended after every step so a successor can continue. Goal (PLAN.md row M4w): the engine
runs as WebAssembly with the null render device and null audio: W0 the commandlet host under Node, W1 the
client built for the browser (SDLLaunch.js/.wasm/.data + meta.json), W2 that client in headless Chromium
reaching the same menu log lines as the native client. Worktree `../engine-uc2-m4w`, branch `m4w`.

## Commands

```sh
cd /home/ion/WorkUC2/engine-uc2-m4w
cmake --preset native && cmake --build build-native -j12
source /home/ion/WorkUC2/env.sh && cmake --preset wasm && cmake --build build-wasm --target UCC -j10
#   clang 23 (emsdk 5.0.7) sometimes crashes with a signal at -j10 (seen on Engine/Src/UnModel.cpp);
#   re-running the same build continues and succeeds. Not a source problem.
tools/dev/mkrun-uc2.sh build-native m4w build-wasm     # native + web binaries in <WorkUC2>/run/m4w
cd /home/ion/WorkUC2/run/m4w/System
node ucc-bin.js verifyclasses
timeout -s INT 60 node ucc-bin.js server DM-Deadbolt -log
tools/dev/layout-diff.py build-wasm -j 8               # i386 vs wasm32 record layouts (from the repo root)
```

## W0 - `ucc-bin` as WebAssembly under Node

### Build: what was added and why

- `cmake/UC2Web.cmake` (new), included from the root CMakeLists in a delimited `if(EMSCRIPTEN)` block:
  - `-sNO_DISABLE_EXCEPTION_CATCHING` at compile time for every target (and `UC2_WEB_EXCEPTION_FLAGS` for
    links). UC2's loaders throw on a missing/incompatible package or object and catch it a few frames up;
    56 of 58 maps import `Editor.TransBuffer`, which is not built, so that throw is ordinary control flow.
    Emscripten's default compiles every catch away (first throw = abort).
    JS exceptions, not `-fwasm-exceptions`: measured in a scratch program, `-fwasm-exceptions -sASYNCIFY`
    fails in wasm-opt (`Module::getFunction: __asyncify_get_call_index does not exist`; emcc also warns
    "ASYNCIFY=1 is not compatible with -fwasm-exceptions"), while JS exceptions with Asyncify work in 5.0.7
    (throw, catch, destructor and an `emscripten_sleep` inside the try all behaved; Emscripten lists
    `env.invoke_*` as Asyncify imports). engine-ut2004 instead leaves catching off and patches every loader
    to fail soft (its "Asyncify fail-soft loader patches"); not ported. Cost (invoke_ trampolines, Asyncify
    instrumentation around them) is an M11 performance question.
  - `-fno-lifetime-dse` for C++ (see defect 2 below).
  - `SDL2::SDL2-static` as an INTERFACE IMPORTED target carrying `-sUSE_SDL=2`, so SDL2Drv/SDLLaunch
    CMakeLists link the same name on both toolchains (for W1).
- Root `CMakeLists.txt`: two delimited `# --- M4w` blocks only: the include above, and
  `list(REMOVE_ITEM UC2_NATIVE_ONLY_PACKAGES UCC SDL2Drv SDLLaunch)` under Emscripten.
- `Source/UCC/CMakeLists.txt`: native keeps `-Wl,-export-dynamic`; Emscripten link options:
  | flag | why |
  |---|---|
  | `-sENVIRONMENT=node -sNODERAWFS=1` | Node with the host file system, relative to the cwd (run from `System/` like the native ucc-bin) |
  | `-pthread -sPROXY_TO_PTHREAD -sPTHREAD_POOL_SIZE=2` | every object is compiled `-pthread` (atomics; wasm-ld refuses to mix), Core starts its file-streaming thread in `appPlatformInit`, main() on a worker as in the browser. Decision: the streaming thread is KEPT on the web (it idles with `usleep(1000)` on its own worker; FFileStream::Destroy waits for it; nothing creates streams yet). Pool = main + streaming thread |
  | `-sEXIT_RUNTIME=1` | main()'s return code is the process exit code |
  | `-sSTACK_SIZE=2097152` | engine-ut2004's value; Emscripten's default is 64 KB |
  | `-sINITIAL_MEMORY=256MB -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB` | room for map loads |
  | `-sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=2 -g` | with assertions on, SOCKFS's "select() assumes fd < 64" aborts `server` when it opens its socket (NODERAWFS hands out real descriptors; dozens of packages are open). Without it the engine's own error path runs, which is what we want to see. Stack checks stay |
- `tools/dev/mkrun-uc2.sh`: optional extra build dirs after the tree name; copies `ucc-bin.js/.wasm`.

### Defects found by running, with cause and fix

1. **`appSleep` aborted**: "Please compile your program with async support ... emscripten_sleep" (from
   `FFileStream::Destroy` in `appExit`). engine-ut2004's appSleep always calls `emscripten_sleep`, which
   needs Asyncify; ucc-bin has none. Fix (`Core/Src/UnUnix.cpp`): `emscripten_has_asyncify()` ?
   `emscripten_sleep` : `usleep` (blocking is legal on a worker).
2. **CPUSPEED=**: `appPlatformInit` appErrorf'd "Please specify CPUSPEED=x on cmdline" (no x86 cycle counter).
   On `UT_NO_X86_INTRIN` the value is unused (GSecondsPerCycle pinned, appCycles counts microseconds), so
   that branch no longer requires it (`#elif defined(UT_NO_X86_INTRIN)` in UnUnix.cpp).
3. **Heap corruption loading Engine.u** (`free()` abort in `TArray<FString>::~TArray` in
   `UObject::LoadConfig`). Found with an AddressSanitizer build (`cmake --preset wasm -B build-wasm-asan
   -DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_C_FLAGS=-fsanitize=address
   -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address`, target UCC, run under Node): heap-use-after-free, memset
   in `LoadConfig` into a block freed by `UArrayProperty::DestroyValue`. Cause: DestroyValue calls
   `DestArray->~FArray()` and the object is reused; `~FArray` sets `Data=NULL; ArrayNum=ArrayMax=0`, and
   clang 23 deletes those stores as dead after the end of the object's lifetime (checked on a 3-line
   example: 0 stores at -O2, stores kept with `-fno-lifetime-dse`). So AddZeroed saw the old ArrayMax and
   wrote into freed memory. GCC does the same and the root CMakeLists already passes `-fno-lifetime-dse`
   to GCC only. Fix: the same flag for the Emscripten clang (UC2Web.cmake). A native clang build would
   need it too (not done: no such build exists).
4. **Struct layout on wasm32** (the key risk). Method: `tools/dev/layout-diff.py` (new) parses every TU of
   the web build twice with the Emscripten clang, for wasm32 and for `--target=i386-pc-linux-gnu`, with
   `-fdump-record-layouts-complete`, and prints every engine record whose size/alignment/member offsets
   differ. i386 is the reference (native i386 passes verifyclasses and the census). Before the fix, the
   script-visible differences were:
   - `UTexture` (204 -> 208; `__LastUpdateTime` @196 -> @200) and hence `UCubemap` (Faces @204 -> @208).
     Script `Texture.uc` declares `var const transient int __LastUpdateTime[2]` (4-aligned). Epic's code
     marks the member `GCC_PACK(4)`, but our `GCC_PACK` is `aligned(n)`, which cannot lower alignment.
     verifyclasses lists UTexture/UCubemap too.
   - `FParticleTrailData`, `FUCParticleTrailData` (28 -> 32; `Time` DOUBLE @20 -> @24). Mirror of script
     struct `UCParticleTrailData` (`DoubleDummy1/2` ints), used in `var transient array<UCParticleTrailData>
     TrailData`: element stride 28 in script, 32 in C++. verifyclasses does NOT check structs.
   Fix: `Engine/Inc/UnTex.h` and `Engine/Inc/UnParticleSystem.h` (both hand-written mirrors, no includes
   inside, ASCII) wrapped in `#if SUPPORTS_PRAGMA_PACK / #pragma pack (push,4)` ... `pop`, exactly what the
   generated `*Classes.h` do (= the original /Zp4). No change on i386.
   After the fix (`layout-diff.py build-wasm`): 159 TUs, 3244 engine records compared, 25 still differ,
   none script-visible; each checked:
   - intrinsic, size taken from C++ (no `.uc`): `UViewport`, `ULevel`, `UState`/`UClass` (QWORD
     Probe/IgnoreMask), `UDemoRecDriver`, `USkeletalMeshInstance`, `ULodMeshInstance`, `UVertMeshInstance`;
     `USDL2Viewport` (C++ only).
   - alignment only, same size and offsets: `FBspNode` (QWORD ZoneMask @16; serialised member-wise),
     `FMemCache(+FCacheItem)`, `FOutstandingPingInfo`, `FRenderState`, `UHTTPDownload`.
   - C++-only helpers: `DelayedPacket`, `FActorLightCache(+Influence)`, `FCoronaRender(+FCoronaInfo)`,
     `FProjectorRenderInfo`, `FSortedStaticMeshBatchIndexBuffer`, `FMasterServerUplinkLink`, `CMainLoop`,
     the PC stub `XUID` typedef in `GUI/Inc/UnUIXInteraction.h`.
   The three SDL2Drv TUs cannot be parsed for i386 here (SDL.h); their engine records are compared in
   other TUs. Remaining risk: a raw `appMemcpy`/bulk `Serialize` of one of the C++-only records against
   a file format; none found by inspection of the list above, not exhaustively searched.

### Evidence (2026-10-04, run from `/home/ion/WorkUC2/run/m4w/System`)

- `node ucc-bin.js verifyclasses` -> exit 0, `All native class layouts match`; log: `VERIFY: loading
  package Engine` ... `GUI`, `VERIFY: all native class layouts match`.
- `timeout -s INT 60 node ucc-bin.js server DM-Deadbolt -log` -> `Executing Class Engine.ServerCommandlet`,
  `Missing Class Class Editor.TransBuffer (../System/Editor.u)` (the caught throw), `Game class is
  'UCDeathmatch'`, `Bringing Level DM-Deadbolt.MyLevel up for play (0)...`, `BeginPlay() took 0.087219`,
  then `Failed to listen: Sockets: setsockopt SO_BROADCAST failed (2175063)`, `Dedicated server can't
  listen: ...`, `Exiting due to error` (exit 1, appError -> caught in main -> clean shutdown). Node has no
  usable sockets for Emscripten (SOCKFS needs a WebSocket peer), so the server cannot listen and therefore
  does not tick for 20 s: accepted for W0 by the brief. The map load and BeginPlay are the evidence; the
  20 s tick is to be shown by the client (W2).
- Native regression in the same worktree: `cmake --build build-native -j12` OK; `./ucc-bin verifyclasses`
  -> all match; `./ucc-bin server DM-Deadbolt -log` -> up for play, ticking at the 25 s timeout.

### Not ported from engine-ut2004 (deferred, with reason)

- `appIsGlobalOptionToken` / `appApplyIniOverrides` (`-ini:` overrides): not needed by ucc; W1/W2 decide.
- `FLUSHCONFIG`, `GetUserDir` virtual + "User directory" line: not needed so far.
- Asyncify fail-soft loader patches (UnObj.cpp/UnLinker.cpp): replaced by real exception catching (above).
- `appUnwindf` latch fix: unreachable on `__UNIX__` (guard/unguard are braces).
- `GObjPreloading` guard: not hit.

## State at this entry

W0 done (verifyclasses passes on wasm32; map loads and BeginPlay runs under Node; listen fails as
explained). W1/W2 not started. Next: W1 (SDL2Drv + SDLLaunch under Emscripten; web inis; meta.json).

## W1 - the client as WebAssembly (SDLLaunch.js/.wasm/.data + meta.json)

Commit after this entry. Built, not yet run (W2 runs it).

- `Source/SDL2Drv` compiles under Emscripten unchanged: SDL2 is Emscripten's port (`-sUSE_SDL=2`, brought
  in through the `SDL2::SDL2-static` interface target of `cmake/UC2Web.cmake`).
- `Source/SDLLaunch`:
  - `Src/SDLLaunchWeb.cpp/.h` (new, web only): `UC2WebYieldFrame()` = EM_ASYNC_JS awaiting
    `setTimeout(0)` (engine-ut2004's ufront 2.43 choice: a timer task, not a MessagePort task, so later a
    worker-owned canvas gets composited); `ue_exec_command` (frozen ABI export, TCHAR is ANSI so no
    conversion); `UC2WebSetEngine`.
  - `Src/SDLLaunch.cpp`: three `#ifdef __EMSCRIPTEN__` blocks only (include of SDLLaunchWeb.h, the yield
    in `CMainLoop::RunLoop` after the tick limiter - skipped when the limiter slept, the engine pointer set
    after InitEngine and cleared before shutdown). main()'s try/catch stays (JS exceptions + Asyncify).
  - `cmake/Web.cmake` (new) included from `CMakeLists.txt` in one delimited block: output name
    `SDLLaunch` + `.js` (fixed artifact names), link flags: dev `-g -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2`
    (perf: `-O3 -g0 --emit-symbol-map`, no assertions); `-sENVIRONMENT=web,worker`, `-pthread
    -sPROXY_TO_PTHREAD -sPTHREAD_POOL_SIZE=2` (main + file-streaming thread), `-sASYNCIFY
    -sASYNCIFY_STACK_SIZE=262144`, `-sSTACK_SIZE=2MB`, 256 MB initial memory + growth,
    `-sOFFSCREENCANVAS_SUPPORT -sOFFSCREENCANVASES_TO_PTHREAD=#canvas` (as the sibling: the GL device will
    need its context on the worker), `-sFORCE_FILESYSTEM -sWASMFS` + `vendor/glue/wasmfs-shim.js` with the
    exported functions the host needs to stand up the JSImpl backend, `$ueWorkerBoot` force-included,
    runtime methods incl. `PThread` and `FS`; preloaded `Engine/Config/web/{Default,DefUser}.ini` ->
    `/System/`. Not linked (vs engine-ut2004): OpenAL, `-sFULL_ES2`, `webrtc-shim.js`
    (`_webrtc_deferred_open` export) - no GL/audio/WebRTC driver yet. `ASYNCIFY_IMPORTS` is NOT set:
    the default list carries `env.invoke_*`, which JS exceptions under Asyncify need.
  - `cmake/emit-artifact-meta.mjs`: engine-ut2004's, targetId `uc2`, repo `engine-uc2`, buildFlags as above,
    no toolchain deps besides emscripten (nothing fetched for the web build).
- `Engine/Config/web/{Default,DefUser}.ini`: byte copies, no change needed (README table).
- Result: `cmake --build build-wasm --target SDLLaunch` -> `SDLLaunch.js` 782 KB, `SDLLaunch.wasm` 47 MB
  (dev, DWARF), `SDLLaunch.data` 18 KB, `meta.json` (`targetId uc2`, abi 1, `wasm-integrated`,
  `global-module`, sha256 of the three files).
