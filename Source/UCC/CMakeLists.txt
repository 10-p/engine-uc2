#=============================================================================
# UCC - commandlet host (ucc-bin): `ucc-bin server <map>`, `batchexport`, and from M14 `make`.
#=============================================================================

project(UCC CXX)

add_executable(${PROJECT_NAME} Src/UCC.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ucc-bin)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  ${CMAKE_SOURCE_DIR}/Source/Engine/Src
  ${CMAKE_SOURCE_DIR}/Source/IpDrv/Src
)

# Static link: every package whole, so the class registrants and native tables survive; exported dynamically
# because appGetDllHandle finds a linked-in package by looking up its GLoaded<Package> symbol in the program.
set(LINK_PACKAGES GUI UCGameplay UCElements UnrealGame UWeb IpDrv Engine Core)
target_link_libraries(${PROJECT_NAME} PRIVATE
  -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
  zlib dl pthread m
)
if(NOT EMSCRIPTEN)
  target_link_options(${PROJECT_NAME} PRIVATE -Wl,-export-dynamic)
else()
  # M4w: ucc-bin.js + ucc-bin.wasm, a Node program that runs inside a run tree like the native ucc-bin
  # (`cd <run>/System && node ucc-bin.js verifyclasses`). It is the wasm32 test vehicle: same objects as the
  # browser build, no browser. Every flag and why: docs/porting/analysis/web-smoke.md (W0).
  # Static link: appGetDllHandle answers the "in this binary" sentinel on Emscripten, so nothing needs
  # exporting dynamically.
  target_link_options(${PROJECT_NAME} PRIVATE
    ${UC2_WEB_EXCEPTION_FLAGS}
    -sENVIRONMENT=node
    -sNODERAWFS=1                     # the host file system, relative to the working directory
    -pthread                          # every object is compiled -pthread (atomics); Core starts the
    -sPROXY_TO_PTHREAD                #   file-streaming thread; main() runs on a worker as in the browser
    -sPTHREAD_POOL_SIZE=2             # main + the file-streaming thread
    -sEXIT_RUNTIME=1                  # main()'s return value is the process exit code
    -sSTACK_SIZE=2097152              # engine-ut2004's value (the Emscripten default is 64 KB)
    -sINITIAL_MEMORY=268435456
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=2147483648
    # No Emscripten runtime assertions: the one that matters here is SOCKFS's "select() assumes fd < 64",
    # which aborts `ucc-bin.js server` when it opens its socket (NODERAWFS hands out real descriptors and
    # the packages hold dozens). Without it the engine's own error path runs ("Dedicated server can't
    # listen"), which is the behaviour to test. Stack overflow checks stay on.
    -sASSERTIONS=0
    -sSTACK_OVERFLOW_CHECK=2
    -g
  )
endif()
