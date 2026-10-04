#=============================================================================
# engine-uc2 -- Unreal Championship 2 (UE2 build 2226, package v151) portable build.
#
# Replaces the original Visual Studio .NET 2003 solution (Source/Avaris.sln) and the stale UT2003-era
# makefiles. Layout and conventions follow engine-ut2004; see BUILD.md and docs/porting/PLAN.md.
#=============================================================================

cmake_minimum_required(VERSION 3.21)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

project(UC2 C CXX)

if(EMSCRIPTEN)
  message(STATUS "Building for Emscripten/WebAssembly")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)          # gnu++14
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

#-----------------------------------------------------------------------------
# Options
#-----------------------------------------------------------------------------

option(UC2_ALLOW_64BIT "Build x86_64 instead of demoting to -m32 (32-bit is the default; it matches wasm32)" OFF)
option(UC2_WEB_PERF    "Emscripten: shipping build -- -O3, no assertions, no debug info" OFF)
option(UC2_WEB_DEBUG   "Emscripten: -O0 instead of the -O2 dev default" OFF)
set(UC2_ASSETS_DIR "${CMAKE_SOURCE_DIR}/../assets/uc2" CACHE PATH
    "Canonical UC2 asset tree (never in this repo). Read only for the files the web build preloads.")

# Every package is a static library: Epic's own __STATIC_LINK scheme, the one the Xbox XBE used.
set(LIB_TYPE STATIC)
add_definitions(-D__STATIC_LINK=1)

#-----------------------------------------------------------------------------
# Architecture
#-----------------------------------------------------------------------------

include(TargetArch)
target_architecture(TARGET_ARCH)

set(TARGET_IS_X86 FALSE)
set(TARGET_IS_64BIT FALSE)
if((TARGET_ARCH MATCHES "86") OR (TARGET_ARCH STREQUAL "ia64"))
  set(TARGET_IS_X86 TRUE)
  if(TARGET_ARCH MATCHES "64")
    set(TARGET_IS_64BIT TRUE)
    set(TARGET_ARCH "x86_64")
  else()
    set(TARGET_ARCH "x86")
  endif()
elseif(TARGET_ARCH MATCHES "aarch64|arm64")
  set(TARGET_IS_64BIT TRUE)
endif()

# The pointer width this build will actually produce. CMAKE_SIZEOF_VOID_P does not see the -m32 demotion
# below and keeps a stale value on reconfigure, so it is corrected here once and read everywhere else.
set(UC2_PTR_BYTES ${CMAKE_SIZEOF_VOID_P})
if(EMSCRIPTEN)
  set(UC2_PTR_BYTES 4)
elseif(TARGET_IS_X86 AND NOT UC2_ALLOW_64BIT)
  set(UC2_PTR_BYTES 4)
endif()
message(STATUS "Target arch: ${TARGET_ARCH}; pointer width: ${UC2_PTR_BYTES} bytes")

if(NOT EMSCRIPTEN AND TARGET_IS_X86 AND TARGET_IS_64BIT AND NOT UC2_ALLOW_64BIT)
  message(STATUS "Building x86 binary with x86_64 compiler (-m32)")
  set(TARGET_IS_64BIT FALSE)
  set(TARGET_ARCH "x86")
  add_compile_options(-m32)
  # try_compile()/check_*() probes (SDL2's X11 checks) only honour the FLAGS variables.
  string(APPEND CMAKE_C_FLAGS " -m32")
  string(APPEND CMAKE_CXX_FLAGS " -m32")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -m32")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -m32")
  string(APPEND CMAKE_MODULE_LINKER_FLAGS " -m32")
  if(NOT CMAKE_LIBRARY_ARCHITECTURE)
    set(CMAKE_FIND_LIBRARY_CUSTOM_LIB_SUFFIX 32)
  endif()
endif()

#-----------------------------------------------------------------------------
# Platform defines
#-----------------------------------------------------------------------------

if(UC2_PTR_BYTES EQUAL 4)
  add_definitions(-DPLATFORM_32BIT)
endif()

if(NOT WIN32)
  add_definitions(-DLINUX=1 -D__LINUX_X86__)
endif()

if(MINGW)
  add_link_options(-static-libgcc -static-libstdc++)
endif()

# TCHAR is ANSI on every target. UC2 removed Unicode at the source ("jg -- Removed unicode support",
# Source/Core/Inc/Core.h), so -fshort-wchar / UNICODE must never be set here.
add_definitions(-DNO_UNICODE_OS_SUPPORT=1)

# Case-insensitive file lookup on case-sensitive file systems.
add_definitions(-D__FILE_NOCASE=1)

# The original projects compile everything with 4-byte struct packing (/Zp4) and the class headers generated
# from script wrap themselves in #pragma pack(push,4) when this is set. The script VM computes property
# offsets on that assumption, so it must hold on every target (wasm32 and x86_64 would otherwise align
# 8-byte members to 8).
add_definitions(-DSUPPORTS_PRAGMA_PACK=1)

# Never define _XBOX, XBOX_ASSET_FORMAT, LIN_LOAD, LIN_SAVE or USE_LINEAR_LOADING: they select the Xbox
# serialisation layouts and linear loading. The portable build loads the PC-format (licensee 0) packages.

#-----------------------------------------------------------------------------
# Compiler flags
#-----------------------------------------------------------------------------

add_compile_options(-pipe -fsigned-char -fno-strict-aliasing)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fpermissive> -fno-lifetime-dse)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-register>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fdelayed-template-parsing>
                      -Wno-writable-strings -Wno-reserved-user-defined-literal)
endif()

if(EMSCRIPTEN)
  # One -O per configuration: the per-config CMake flag variables are blanked so nothing appends a second.
  foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_C_FLAGS_${cfg} "")
    set(CMAKE_CXX_FLAGS_${cfg} "")
  endforeach()
  if(UC2_WEB_PERF)
    add_compile_options(-O3 -g0)
  elseif(UC2_WEB_DEBUG)
    add_compile_options(-O0 -g)
  else()
    add_compile_options(-O2 -g)
  endif()
  add_compile_options(-pthread)
endif()

# --- M4w: web build additions (exception catching, SDL2 port); see cmake/UC2Web.cmake -------------------
if(EMSCRIPTEN)
  include(UC2Web)
endif()
# --- end M4w ---------------------------------------------------------------------------------------------

#-----------------------------------------------------------------------------
# Third-party libraries (vendored in Thirdparty/, never under Source/)
#-----------------------------------------------------------------------------

add_definitions(-DZ_PREFIX)
add_subdirectory(Thirdparty/zlib)

#-----------------------------------------------------------------------------
# Fetched libraries (never vendored). SDL2 for the native client (SDL2Drv, SDLLaunch); the arrangement is
# engine-ut2004's. The web build will take SDL2 from Emscripten's -sUSE_SDL=2 port when its client is
# added (M4w), so nothing is fetched there.
#-----------------------------------------------------------------------------

if(NOT EMSCRIPTEN)
  include(FetchContent)
  FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.32.x
    GIT_SHALLOW    TRUE
  )
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  # SDL2main is only a WinMain shim; the Windows build (M12) will want it, nothing else does.
  if(MINGW)
    set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
  else()
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
  endif()
  if(NOT MINGW)
    set(SDL_X11 ON CACHE BOOL "" FORCE)
    set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
    set(SDL_X11_SHARED OFF CACHE BOOL "" FORCE)
  endif()
  if(NOT MINGW AND UC2_PTR_BYTES EQUAL 4)
    # -m32 on an x86_64 host: SDL2's X11 probes must find the i386 libraries. Without this they link
    # 32-bit objects against 64-bit libs, every check fails silently and SDL_x11xinput2.h then emits a
    # fallback XGenericEventCookie that collides with Xlib's (engine-ut2004's notes, same machine setup).
    list(APPEND CMAKE_LIBRARY_PATH /usr/lib/i386-linux-gnu)
    list(APPEND CMAKE_PREFIX_PATH /usr/lib/i386-linux-gnu)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
  endif()
  FetchContent_MakeAvailable(SDL2)
endif()

#-----------------------------------------------------------------------------
# Engine packages. A package joins the build when its CMakeLists.txt lands (docs/porting/PLAN.md lists
# the order); everything else under Source/ is reference and is not compiled.
#-----------------------------------------------------------------------------

set(UC2_PACKAGES
  Core Engine IpDrv UWeb UnrealGame UCElements UCGameplay GUI
  Editor UCC
  SDL2Drv UC2GLDrv UC2GL4ESDrv UC2ALAudio WebRTCDrv SDLLaunch)

# Native-only tools: the commandlet host is not part of the web build. SDL2Drv and SDLLaunch are native-only
# until the web client is wired (M4w: -sUSE_SDL=2, Asyncify yield, preload).
set(UC2_NATIVE_ONLY_PACKAGES UCC Editor SDL2Drv SDLLaunch)

# --- M4w: the web build has the SDL2 client and launcher (browser), and ucc-bin (run under Node to check
# the wasm32 layout and load maps without a browser) -------------------------------------------------------
if(EMSCRIPTEN)
  list(REMOVE_ITEM UC2_NATIVE_ONLY_PACKAGES UCC SDL2Drv SDLLaunch)
endif()
# --- end M4w ---------------------------------------------------------------------------------------------

foreach(pkg IN LISTS UC2_PACKAGES)
  if(EMSCRIPTEN AND pkg IN_LIST UC2_NATIVE_ONLY_PACKAGES)
    continue()
  endif()
  if(EXISTS ${CMAKE_SOURCE_DIR}/Source/${pkg}/CMakeLists.txt)
    add_subdirectory(Source/${pkg})
  endif()
endforeach()
