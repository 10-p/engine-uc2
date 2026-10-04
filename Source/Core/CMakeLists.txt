#=============================================================================
# Core - engine foundation: object system, names, memory, file I/O, math, serialisation, the
# UnrealScript VM. Modelled on engine-ut2004/Source/Core/CMakeLists.txt.
#
# The file list is Core.vcproj's PC Release set, minus the Windows/Xbox/console platform layers, plus
# the portable platform layer (UnUnix.cpp, UnThreadingPosix.cpp). Every file left out, and why, is in
# docs/porting/analysis/core-port.md ("Excluded files").
#=============================================================================

project(Core CXX)

set(SRC_FILES
  "Src/Core.cpp"
  "Src/UExporter.cpp"
  "Src/UFactory.cpp"
  "Src/UnAnsi.cpp"
  "Src/UnBits.cpp"
  "Src/UnCache.cpp"
  "Src/UnClass.cpp"
  "Src/UnCorSc.cpp"
  "Src/UnCoreNative.cpp"
  "Src/UnCoreNet.cpp"
  "Src/UnGUID.cpp"
  "Src/UnLinker.cpp"
  "Src/UnMath.cpp"
  "Src/UnMem.cpp"
  "Src/UnMisc.cpp"
  "Src/UnName.cpp"
  "Src/UnObj.cpp"
  "Src/UnProp.cpp"
  # Platform layer for every non-MSVC target (replaces UnVcWin32.cpp / UnThreadingWindows.cpp).
  "Src/UnUnix.cpp"
  "Src/UnThreadingPosix.cpp"
)

# GCC miscompiles FFrame::Step()'s reference juggling at -O2 (seen in the UE1/UT2004 ports, same VM code).
# -Og keeps these two files correct; everything else gets the normal optimisation level.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
  set_source_files_properties("Src/UnCorSc.cpp" "Src/UnProp.cpp" PROPERTIES COMPILE_FLAGS -Og)
endif()

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=Core)

# zlib: Inc/FFileManagerGeneric.h (compressed-file reads) uses it, and that header is compiled by the
# launchers, so the dependency is PUBLIC. UC2 has no Ogg/Vorbis in Core (UnUnix.cpp's include was stale).
target_link_libraries(${PROJECT_NAME} PUBLIC zlib)

# The pthread platform layer. Emscripten already compiles everything with -pthread (root CMakeLists).
if(NOT EMSCRIPTEN AND NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
endif()
