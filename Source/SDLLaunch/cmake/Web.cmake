#=============================================================================
# SDLLaunch for the browser (M4w): SDLLaunch.js + SDLLaunch.wasm + SDLLaunch.data + meta.json, the
# artifact the ufront frontend consumes (frontend docs/05-engine-repos.md section 4; the names are fixed).
# Included from Source/SDLLaunch/CMakeLists.txt under Emscripten. Arrangement and most flags are
# engine-ut2004's (its Source/SDLLaunch/CMakeLists.txt); every choice is in docs/porting/analysis/
# web-smoke.md (W1). Null render device and null audio: no GL, no OpenAL, no WebRTC yet (M5/M8/M11).
#=============================================================================

target_sources(${PROJECT_NAME} PRIVATE Src/SDLLaunchWeb.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME SDLLaunch SUFFIX ".js")

if(UC2_WEB_PERF)
  target_link_options(${PROJECT_NAME} PRIVATE -O3 -g0 --emit-symbol-map -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=0)
else()
  target_link_options(${PROJECT_NAME} PRIVATE -g -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2)
endif()

target_link_options(${PROJECT_NAME} PRIVATE
  ${UC2_WEB_EXCEPTION_FLAGS}
  -sENVIRONMENT=web,worker
  # The engine runs on a worker; its blocking main loop yields once per frame through Asyncify
  # (SDLLaunchWeb.cpp). appSleep's emscripten_sleep is the other suspension point.
  -pthread
  -sPROXY_TO_PTHREAD
  -sPTHREAD_POOL_SIZE=2                # main() + Core's file-streaming thread
  -sASYNCIFY
  -sASYNCIFY_STACK_SIZE=262144
  -sSTACK_SIZE=2097152
  -sINITIAL_MEMORY=268435456
  -sALLOW_MEMORY_GROWTH=1
  # An explicit ceiling (CLAUDE.md, "Running anything"): growth stops at 2 GB instead of whatever the
  # toolchain's default happens to be. Raise it deliberately, with a measurement, never by removing it.
  -sMAXIMUM_MEMORY=2147483648
  # The canvas goes to the engine worker now, so the GL device (M5/M11) can create its context there.
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
find_program(NODE_EXECUTABLE node REQUIRED)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emit-artifact-meta.mjs
    --out=$<TARGET_FILE_DIR:${PROJECT_NAME}> --target=uc2 --abi=${UE_ABI_VERSION} --emcc=${META_EMCC}
  COMMENT "Emitting artifact meta.json"
  VERBATIM)
