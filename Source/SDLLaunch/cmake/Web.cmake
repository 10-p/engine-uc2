#=============================================================================
# SDLLaunch for the browser: SDLLaunch.js + SDLLaunch.wasm + SDLLaunch.data + meta.json, the artifact the
# frontend consumes (frontend docs/05-engine-repos.md section 4; the names are fixed). Included from
# Source/SDLLaunch/CMakeLists.txt under Emscripten. The module links the GL4ES renderer (UC2GL4ESDrv),
# the OpenAL audio device on openal-soft's Web Audio backend and the WebRTC net driver.
#=============================================================================

target_sources(${PROJECT_NAME} PRIVATE Src/SDLLaunchWeb.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME SDLLaunch SUFFIX ".js")

if(UC2_WEB_PERF)
  target_link_options(${PROJECT_NAME} PRIVATE -O3 -g0 --emit-symbol-map -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=0)
else()
  # Dev build: -O2 at link and names without DWARF (-g2). Without -O binaryen leaves Asyncify's
  # instrumentation unoptimised (functions with tens of thousands of locals) and a browser compiling the
  # module runs out of memory; -O2 -g2 compiles in about the memory of the release build. Stack traces keep
  # function names; assertions and stack checks stay on. For DWARF, build the wasm-debug preset and run it
  # under Node, not in a browser.
  target_link_options(${PROJECT_NAME} PRIVATE -O2 -g2 -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2)
endif()

target_link_options(${PROJECT_NAME} PRIVATE
  # Catching is on at the link: openal-soft reports every AL error by throwing and catching
  # it internally, and with catching off the first AL error aborts. Only objects compiled with -fexceptions
  # (openal-soft's, root CMakeLists.txt) carry landing pads; the engine's own code is compiled without catching
  # and gains no invoke_ calls (cmake/UC2Web.cmake).
  -sNO_DISABLE_EXCEPTION_CATCHING
  -sENVIRONMENT=web,worker
  # The engine runs on a worker; its blocking main loop yields once per frame through Asyncify
  # (SDLLaunchWeb.cpp). appSleep's emscripten_sleep is the other suspension point.
  -pthread
  -sPROXY_TO_PTHREAD
  -sPTHREAD_POOL_SIZE=2                # main() + Core's file-streaming thread
  -sASYNCIFY
  -sASYNCIFY_STACK_SIZE=262144
  # Emscripten appends its defaults to this list (__asyncjs__*, which covers the frame yield's EM_ASYNC_JS, and
  # invoke_* while indirect calls are not ignored), and emscripten_sleep is an async JS function in any case.
  "-sASYNCIFY_IMPORTS=['emscripten_sleep','emscripten_set_main_loop','__syscall_openat']"
  -sSTACK_SIZE=2097152
  -sINITIAL_MEMORY=268435456
  -sALLOW_MEMORY_GROWTH=1
  # An explicit ceiling: growth stops at 2 GB instead of whatever the
  # toolchain's default happens to be.
  -sMAXIMUM_MEMORY=2147483648
  # The canvas goes to the engine worker, so the GL device can create its context there.
  -sOFFSCREENCANVAS_SUPPORT
  "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"     # quoted: CMake would read '#' as a comment
  # WasmFS with the host-implemented (JSImpl) backend: the host creates one backend file per asset and
  # serves the bytes (vendor/glue/wasmfs-shim.js forwards WasmFS's hooks to Module.UE.fs.backend).
  -sFORCE_FILESYSTEM
  -sWASMFS
  "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_close','_ue_exec_command','_wasmfs_create_jsimpl_backend','_wasmfs_create_file','_ufront_wasmfs_register','_ufront_wasmfs_set_pending']"
  "-sEXPORTED_RUNTIME_METHODS=['callMain','stringToUTF8','UTF8ToString','HEAPU8','stackSave','stackAlloc','stackRestore','lengthBytesUTF8','PThread','FS']"
  # The worker-side bootstrap of the shim hangs off a __postset that is only emitted when referenced.
  "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['$ueWorkerBoot']"
  "SHELL:--js-library ${CMAKE_SOURCE_DIR}/vendor/glue/wasmfs-shim.js"
  # Boot configuration, from the tracked web set (Engine/Config/web), never from the asset tree. appInit
  # derives Avaris.ini / User.ini from these two in /System.
  "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/Engine/Config/web/Default.ini@/System/Default.ini"
  "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/Engine/Config/web/DefUser.ini@/System/DefUser.ini"
)
# Link inputs CMake cannot see: without these an edited ini, shim or emitter leaves a stale artifact.
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_SOURCE_DIR}/Engine/Config/web/Default.ini"
  "${CMAKE_SOURCE_DIR}/Engine/Config/web/DefUser.ini"
  "${CMAKE_SOURCE_DIR}/vendor/glue/wasmfs-shim.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/emit-artifact-meta.mjs")

# meta.json, after the .js is final, so its checksum is of the served file.
file(STRINGS ${CMAKE_SOURCE_DIR}/vendor/glue/ue_abi.h _ue_abi_line REGEX "define[ \t]+UE_ABI_VERSION")
string(REGEX MATCH "([0-9]+)" UE_ABI_VERSION "${_ue_abi_line}")
set(META_EMCC "unknown")
if(EXISTS "${EMSCRIPTEN_ROOT_PATH}/emscripten-version.txt")
  file(READ "${EMSCRIPTEN_ROOT_PATH}/emscripten-version.txt" _emcc_ver)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" META_EMCC "${_emcc_ver}")
endif()
# Every library this build fetches, as <repo>@<branch> from the same cache variables FetchContent read
# (cmake/ArtifactDeps.cmake). On the web SDL2 is Emscripten's own
# port (cmake/UC2Web.cmake), not fetched: the emscripten version names it.
include(${CMAKE_SOURCE_DIR}/cmake/ArtifactDeps.cmake)
set(META_DEPS)
if(DEFINED GL4ES_GIT_REPOSITORY)
  ufront_artifact_dep(META_DEPS gl4es GL4ES "${GL4ES_GIT_REPOSITORY}" "${GL4ES_GIT_TAG}")
endif()
if(DEFINED OPENALSOFT_GIT_REPOSITORY)
  ufront_artifact_dep(META_DEPS openal-soft OpenALSoft "${OPENALSOFT_GIT_REPOSITORY}" "${OPENALSOFT_GIT_TAG}")
endif()
find_program(NODE_EXECUTABLE node REQUIRED)
# The patched script packages travel with the artifact: <artifact>/scripts/<Pkg>.u, a mirror of the build's
# Scripts/System (the `scripts` target; empty with UC2_SCRIPT_PATCHES OFF), listed in meta.json `scripts` with the
# asset path each one replaces (the frontend overlays them on the asset tree). The relink that runs this when only
# the scripts changed: cmake/UC2Scripts.cmake (LINK_DEPENDS on its stamp).
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DUC2_CHECK=copy -DSRC=${CMAKE_BINARY_DIR}/Scripts/System
    -DDST=$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts -P ${CMAKE_SOURCE_DIR}/cmake/UC2ScriptsCheck.cmake
  COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emit-artifact-meta.mjs
    --out=$<TARGET_FILE_DIR:${PROJECT_NAME}> --target=uc2 --abi=${UE_ABI_VERSION} --emcc=${META_EMCC}
    --scripts=scripts --scripts-under=System
    ${META_DEPS}
  COMMENT "Emitting artifact meta.json"
  VERBATIM)

# The audio device on openal-soft's Web Audio backend.
# openal-soft is linked through UC2ALAudio (root CMakeLists.txt). Its web backend starts two threads of its own
# when the device opens: the mixer and the context's event thread. Pool 2 -> 4 (main(), Core's file-streaming
# thread, those two): a thread created with the pool empty still starts, but only after the browser main thread
# has spun up a worker for it, and the audio ring would absorb that delay as an underrun. emcc takes the last
# -s setting, so this overrides the 2 above. Exception catching is on at
# the link (above): openal-soft throws and catches internally.
target_link_options(${PROJECT_NAME} PRIVATE -sPTHREAD_POOL_SIZE=4)
# SDLLaunchWeb.cpp mutes the listener while the tab is hidden, with openal-soft's headers.
if(TARGET OpenAL)
  target_include_directories(${PROJECT_NAME} PRIVATE $<TARGET_PROPERTY:OpenAL,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

# The GL device on the web (UC2GLDrv behind gl4es).
if(TARGET UC2GL4ESDrv)
  target_link_options(${PROJECT_NAME} PRIVATE
    # gl4es and the driver's raw table look GLES entry points up by name (emscripten_webgl_get_proc_address).
    -sGL_ENABLE_GET_PROC_ADDRESS=1
    # WebGL 2 = OpenGL ES 3.0 through gl4es' ES 3 back end (the fork), with WebGL 1 kept for browsers
    # without WebGL 2 (UC2GL4ESDrv falls back; MIN 1 keeps Emscripten's WebGL 1 paths and extension names).
    # FULL_ES2: client-side array emulation for whatever gl4es still sends that way (the driver itself sends
    # none).
    -sMIN_WEBGL_VERSION=1
    -sMAX_WEBGL_VERSION=2
    -sFULL_ES2=1
    # The context is created with explicitSwapControl + renderViaOffscreenBackBuffer and each frame is
    # presented with emscripten_webgl_commit_frame(): both exist only with this flag (without it the call
    # is a stub and presentation depends on the browser compositing when the worker's task ends).
    -sOFFSCREEN_FRAMEBUFFER
  )
endif()

# The network driver's transport.
# WebRTCDrv calls the webrtc_* imports; vendor/glue/webrtc-shim.js (the frontend's ABI v1 copy) defines them
# as forwarders to Module.UE.net, which the host injects before main(). Its
# $ueNet rides in through each forwarder's __deps, so nothing is force-included. send/recv are answered on the
# engine's worker from the glue's shared packet rings (installed by the worker bootstrap of wasmfs-shim.js); the
# other calls are proxied to the main thread, where RTCPeerConnection lives.
if(TARGET WebRTCDrv)
  target_link_options(${PROJECT_NAME} PRIVATE "SHELL:--js-library ${CMAKE_SOURCE_DIR}/vendor/glue/webrtc-shim.js")
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/vendor/glue/webrtc-shim.js")
endif()
