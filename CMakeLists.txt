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
  # M12: no MinGW runtime DLL beside the exe (libgcc_s_dw2-1, libstdc++-6, libwinpthread-1 are not part of a
  # Windows install). -static takes the static archive of every -l library that has one (winpthread
  # included); the Windows system libraries only exist as import libraries, so they stay DLL imports. Also
  # applies to the OpenAL32.dll built below.
  add_link_options(-static -static-libgcc -static-libstdc++)
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

# char is signed on x86 and unsigned on arm (and the code was written for signed char). The audit build
# (preset native64-uchar) inverts it to show the code does not depend on it (M13, docs/porting/analysis/64bit.md).
# M13: sanitizer builds (preset native64-asan). -fno-sanitize=vptr,alignment: UE's placement construction of
# objects trips UBSan's vptr check at start-up, and the script mirrors are pack(4) on purpose (8-byte pointers on
# 4-byte boundaries). The engine allocator is plain malloc on Linux (FMallocAnsi), so ASan sees every block.
set(UC2_SANITIZE "" CACHE STRING "-fsanitize= list for every target, e.g. address,undefined (empty: none)")
if(UC2_SANITIZE)
  add_compile_options(-fsanitize=${UC2_SANITIZE} -fno-sanitize=vptr,alignment -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${UC2_SANITIZE} -fno-sanitize=vptr,alignment)
endif()
option(UC2_UNSIGNED_CHAR_AUDIT "Build with -funsigned-char (arm64's char) instead of -fsigned-char" OFF)
if(UC2_UNSIGNED_CHAR_AUDIT)
  add_compile_options(-pipe -funsigned-char -fno-strict-aliasing)
  add_definitions(-DUC2_UNSIGNED_CHAR_AUDIT=1)
else()
  add_compile_options(-pipe -fsigned-char -fno-strict-aliasing)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fpermissive> -fno-lifetime-dse)
  if(UC2_PTR_BYTES EQUAL 8)
    # M13: an integer widened into a pointer is an error on 64-bit targets. (A pointer narrowed into an int is
    # already an error in C++, but -fpermissive turns it into a warning and GCC has no switch to make only that
    # one an error again: tools/dev/m13-cast-census.sh counts it in a build log; 0 since M13.)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Werror=int-to-pointer-cast>)
  endif()
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

# --- M8: audio. Ogg/Vorbis vendored (engine-ut2004's copy); OpenAL Soft fetched from the same fork the sibling
# port uses everywhere (openal-soft 1.25 + a Web Audio backend for M11). Native: a SHARED libopenal that
# UC2ALAudio loads with dlopen when the audio device starts (never DT_NEEDED: no sound library at all must
# still boot, and nothing of OpenAL runs before the engine's allocator exists), copied beside uc2.bin by
# tools/dev/mkrun-uc2.sh. Built as C++20 without the engine's C++ flags (engine-ut2004's finding: its
# static tables miscompile with the engine's), with the wave-file writer backend for capture tests. --------
add_subdirectory(Thirdparty/Vorbis/ogg)
add_subdirectory(Thirdparty/Vorbis/vorbis)
# M12: Windows (MinGW) builds the same source as a DLL, `OpenAL32.dll` (openal-soft's own name on Windows), which
# UC2ALAudio loads with LoadLibrary; nothing binary is downloaded. Its backends there are WASAPI/DirectSound/
# WinMM plus the same null and wave writer.
if(NOT EMSCRIPTEN)
  set(OPENALSOFT_GIT_REPOSITORY "https://github.com/10-p/openal-soft-web.git"
      CACHE STRING "openal-soft git remote; its ufront/web branch carries the Web Audio backend")
  set(OPENALSOFT_GIT_TAG "ufront/web" CACHE STRING "openal-soft git ref")
  set(_UC2_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  set(_UC2_SAVED_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
  set(CMAKE_CXX_STANDARD 20)
  FetchContent_Declare(
    OpenALSoft
    GIT_REPOSITORY ${OPENALSOFT_GIT_REPOSITORY}
    GIT_TAG        ${OPENALSOFT_GIT_TAG}
    GIT_SHALLOW    TRUE
  )
  set(LIBTYPE SHARED CACHE STRING "" FORCE)
  foreach(opt ALSOFT_UTILS ALSOFT_EXAMPLES ALSOFT_TESTS ALSOFT_INSTALL ALSOFT_INSTALL_CONFIG
              ALSOFT_INSTALL_HRTF_DATA ALSOFT_INSTALL_AMBDEC_PRESETS ALSOFT_UPDATE_BUILD_VERSION)
    set(${opt} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(ALSOFT_EAX OFF CACHE BOOL "" FORCE)            # UC2 uses EFX (zone reverb), not EAX
  set(ALSOFT_BACKEND_WAVE ON CACHE BOOL "" FORCE)    # ALSOFT_DRIVERS=wave: render to a .wav (tests)
  set(ALSOFT_BACKEND_NULL ON CACHE BOOL "" FORCE)
  # A MinGW build must not pick up the HOST's Linux audio headers (libpipewire-0.3-dev on the build machine:
  # pipewire/utils.h needs <sys/un.h>, which MinGW lacks). Windows uses openal-soft's WASAPI backend.
  if(WIN32)
    foreach(be PIPEWIRE PULSEAUDIO ALSA OSS JACK SNDIO)
      set(ALSOFT_BACKEND_${be} OFF CACHE BOOL "" FORCE)
    endforeach()
  endif()
  set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
  # The engine's directory-wide definitions and -fpermissive are not openal-soft's business.
  get_directory_property(_UC2_SAVED_DEFS COMPILE_DEFINITIONS)
  get_directory_property(_UC2_SAVED_OPTS COMPILE_OPTIONS)
  set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "" COMPILE_OPTIONS "")
  # openal-soft decides "32-bit x86: build with -msse2 -mfpmath=sse and realign the stack at its entry points"
  # from CMAKE_SIZEOF_VOID_P, which does not see our -m32 demotion (see UC2_PTR_BYTES above): without this it
  # uses SSE intrinsics in code compiled for plain i686 and does not build.
  set(_UC2_SAVED_SIZEOF_VOID_P "${CMAKE_SIZEOF_VOID_P}")
  if(UC2_PTR_BYTES EQUAL 4)
    add_compile_options(-m32)
    set(CMAKE_SIZEOF_VOID_P 4)
  endif()
  FetchContent_MakeAvailable(OpenALSoft)
  set(CMAKE_SIZEOF_VOID_P "${_UC2_SAVED_SIZEOF_VOID_P}")
  set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "${_UC2_SAVED_DEFS}" COMPILE_OPTIONS "${_UC2_SAVED_OPTS}")
  unset(CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_CXX_FLAGS "${_UC2_SAVED_CXX_FLAGS}")
  set(CMAKE_CXX_STANDARD "${_UC2_SAVED_CXX_STANDARD}")
  if(NOT TARGET OpenAL)
    message(FATAL_ERROR "openal-soft: expected target 'OpenAL' was not created")
  endif()
  message(STATUS "openal-soft (shared, wave writer on): ${OPENALSOFT_GIT_REPOSITORY}@${OPENALSOFT_GIT_TAG}")
endif()
# M11 (O-2): the web build links the same fork STATIC into the module, with its Web Audio backend (alc/backends/
# web.cpp: the mixer on its own pthread renders into a ring in the shared wasm memory, an AudioWorklet plays it;
# every AudioContext call goes to the browser main thread; the context resumes on the page's next pointerdown/
# keydown/touchend). Settings as engine-ut2004's web block (ufront 2.61), read-only template:
#  - its scope gets its own compile options: the optimisation level, -pthread and -fexceptions (exception
#    catching, which the engine's own code does not have since F-4a: openal-soft reports every AL error by
#    throwing and catching it internally), plus -msimd128
#    -msse4.1 so its SSE mixers compile through Emscripten's SIMD translation headers; none of the engine's
#    clang compatibility flags (-fdelayed-template-parsing miscompiled openal-soft's static tables elsewhere);
#  - NEON off (its header check passes on Emscripten and the runtime would prefer it), no dlopen, no wave
#    writer, SSE4.1 required (a silent fallback to the C mixers costs 2.5x), EAX off (UC2 uses EFX), the
#    default HRTF data embedded (openal-soft's default; HRTF stays off unless asked).
if(EMSCRIPTEN)
  include(FetchContent)
  set(OPENALSOFT_GIT_REPOSITORY "https://github.com/10-p/openal-soft-web.git"
      CACHE STRING "openal-soft git remote; its ufront/web branch carries the Web Audio backend")
  set(OPENALSOFT_GIT_TAG "ufront/web" CACHE STRING "openal-soft git ref")
  FetchContent_Declare(
    OpenALSoft
    GIT_REPOSITORY ${OPENALSOFT_GIT_REPOSITORY}
    GIT_TAG        ${OPENALSOFT_GIT_TAG}
    GIT_SHALLOW    TRUE
  )
  set(_UC2_SAVED_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
  set(CMAKE_CXX_STANDARD 20)
  set(LIBTYPE STATIC CACHE STRING "" FORCE)
  foreach(opt ALSOFT_UTILS ALSOFT_EXAMPLES ALSOFT_TESTS ALSOFT_INSTALL ALSOFT_INSTALL_CONFIG
              ALSOFT_INSTALL_HRTF_DATA ALSOFT_INSTALL_AMBDEC_PRESETS ALSOFT_UPDATE_BUILD_VERSION
              ALSOFT_BACKEND_WAVE ALSOFT_CPUEXT_NEON ALSOFT_DLOPEN ALSOFT_EAX)
    set(${opt} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(ALSOFT_BACKEND_WEB ON CACHE BOOL "" FORCE)
  set(ALSOFT_BACKEND_NULL ON CACHE BOOL "" FORCE)
  set(ALSOFT_REQUIRE_SSE4_1 ON CACHE BOOL "" FORCE)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
  get_directory_property(_UC2_SAVED_DEFS COMPILE_DEFINITIONS)
  get_directory_property(_UC2_SAVED_OPTS COMPILE_OPTIONS)
  if(UC2_WEB_PERF)
    set(_UC2_AL_OPT -O3 -g0)
  elseif(UC2_WEB_DEBUG)
    set(_UC2_AL_OPT -O0 -g)
  else()
    set(_UC2_AL_OPT -O2 -g)
  endif()
  set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "" COMPILE_OPTIONS "${_UC2_AL_OPT}")
  # (In the language flags rather than the directory options: its configure checks, e.g. for xmmintrin.h,
  # compile with these and fail without -msimd128.)
  set(_UC2_SAVED_C_FLAGS "${CMAKE_C_FLAGS}")
  set(_UC2_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(APPEND CMAKE_C_FLAGS " -pthread -msimd128 -msse4.1")
  string(APPEND CMAKE_CXX_FLAGS " -pthread -fexceptions -msimd128 -msse4.1")   # -fexceptions: as engine-ut2004 (F-4a)
  FetchContent_MakeAvailable(OpenALSoft)
  set(CMAKE_C_FLAGS "${_UC2_SAVED_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${_UC2_SAVED_CXX_FLAGS}")
  set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "${_UC2_SAVED_DEFS}" COMPILE_OPTIONS "${_UC2_SAVED_OPTS}")
  unset(CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_CXX_STANDARD "${_UC2_SAVED_CXX_STANDARD}")
  if(NOT TARGET OpenAL)
    message(FATAL_ERROR "openal-soft: expected target 'OpenAL' was not created")
  endif()
  message(STATUS "openal-soft (static, Web Audio backend): ${OPENALSOFT_GIT_REPOSITORY}@${OPENALSOFT_GIT_TAG}")
endif()
# --- end M8 --------------------------------------------------------------------------------------------------

# --- M9: video. libtheora 1.2.0, decoder only, vendored (Thirdparty/Theora/README.uc2); every target. ---------
add_subdirectory(Thirdparty/Theora)
# --- end M9 --------------------------------------------------------------------------------------------------

# --- M11: GL4ES (desktop OpenGL 1.x/2.x calls translated to OpenGL ES 2 / WebGL 1), fetched as engine-ut2004
# fetches it (same fork, same branch). The web build renders through it; the native Linux build carries it
# too, as the second binding of the one GL driver (Source/UC2GL4ESDrv: "the native twin"), so every renderer
# test runs against the web code path without a browser. Not on Windows: the twin is a Linux test vehicle.
# Notebook: docs/porting/analysis/web-renderer.md.
if(EMSCRIPTEN OR (UNIX AND NOT APPLE AND NOT MINGW))
  include(FetchContent)
  set(GL4ES_GIT_REPOSITORY "https://github.com/10-p/gl4es.git"
      CACHE STRING "GL4ES git remote (the 10-p fork carries the cubemap/combiner fixes; upstream: ptitSeb/gl4es)")
  set(GL4ES_GIT_TAG "fix/cubemap-bind-active-unit" CACHE STRING "GL4ES git ref")
  FetchContent_Declare(
    GL4ES
    GIT_REPOSITORY ${GL4ES_GIT_REPOSITORY}
    GIT_TAG        ${GL4ES_GIT_TAG}
    GIT_SHALLOW    TRUE
  )
  # Static library, GLES2 back end, no X11/EGL of its own (SDL or Emscripten owns the context and gl4es is
  # handed their GetProcAddress), no constructor (the device initialises it once the context exists).
  set(NOX11 ON CACHE BOOL "" FORCE)
  set(NOEGL ON CACHE BOOL "" FORCE)
  set(STATICLIB ON CACHE BOOL "" FORCE)
  set(DEFAULT_ES 2 CACHE STRING "" FORCE)
  set(NO_INIT_CONSTRUCTOR ON CACHE BOOL "" FORCE)
  # (-m32 needs nothing here: the 32-bit block above already put it into CMAKE_C_FLAGS for every C library.)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
  FetchContent_MakeAvailable(GL4ES)
  unset(CMAKE_POLICY_VERSION_MINIMUM)
  if(TARGET GL)
    # gl4es writes its archive into its own source tree (shared by every build directory): keep each
    # build's copy in its own binary directory.
    set_target_properties(GL PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    # The engine's directory-wide definitions and C++ switches are not gl4es's business; its own -O2 must
    # survive (at -O0 one of its functions exceeds V8's limit of locals once Asyncify has instrumented it:
    # engine-ut2004's finding).
    target_compile_options(GL PRIVATE -O2 -w)
    if(NOT TARGET gl4es::GL)
      add_library(gl4es::GL ALIAS GL)
    endif()
    message(STATUS "gl4es (static, GLES2): ${GL4ES_GIT_REPOSITORY}@${GL4ES_GIT_TAG}")
  else()
    message(FATAL_ERROR "gl4es: expected target 'GL' was not created")
  endif()
endif()
# --- end M11 -------------------------------------------------------------------------------------------------

#-----------------------------------------------------------------------------
# Engine packages. A package joins the build when its CMakeLists.txt lands (docs/porting/PLAN.md lists
# the order); everything else under Source/ is reference and is not compiled.
#-----------------------------------------------------------------------------

set(UC2_PACKAGES
  Core Engine IpDrv UWeb UnrealGame UCElements UCGameplay GUI
  UC2ALAudio UC2Movie
  Editor UCC
  SDL2Drv UC2GLDrv UC2GL4ESDrv WebRTCDrv SDLLaunch)
# (M8: UC2ALAudio comes before UCC, which links it for its check commandlet. M9: UC2Movie likewise.)

# Native-only tools: the commandlet host is not part of the web build. SDL2Drv and SDLLaunch are native-only
# until the web client is wired (M4w: -sUSE_SDL=2, Asyncify yield, preload).
set(UC2_NATIVE_ONLY_PACKAGES UCC Editor SDL2Drv SDLLaunch)
# M11: UC2GLDrv is built everywhere (one driver source); its GL4ES binding, UC2GL4ESDrv, where gl4es is
# (Linux and the web: see the M11 block above and Source/UC2GL4ESDrv/CMakeLists.txt).
# M8: the OpenAL audio device was native-only until the web build adopted it (M11/O-2: openal-soft's Web Audio
# backend, linked into the module by the block above).

# --- M4w: the web build has the SDL2 client and launcher (browser), and ucc-bin (run under Node to check
# the wasm32 layout and load maps without a browser) -------------------------------------------------------
if(EMSCRIPTEN)
  list(REMOVE_ITEM UC2_NATIVE_ONLY_PACKAGES UCC SDL2Drv SDLLaunch)
endif()
# --- end M4w ---------------------------------------------------------------------------------------------

# --- M14: the script compiler (Editor package, `ucc-bin make`) is built at 32-bit pointers only: its hand-written
# UEditorEngine (Editor.h) mirrors script's `int Pad[8]` with four pointers, a layout that only holds at 32 bits.
# 32-bit Linux (M14) and, since S2, MinGW i686 (`ucc.exe make`, proved under wine: analysis/ucc-windows.md).
# ucc-bin / ucc.exe link it when the target exists. ---------------------------------------------------------------
if(UC2_PTR_BYTES EQUAL 8)
  list(REMOVE_ITEM UC2_PACKAGES Editor)
endif()
# --- end M14 ---------------------------------------------------------------------------------------------------

foreach(pkg IN LISTS UC2_PACKAGES)
  if(EMSCRIPTEN AND pkg IN_LIST UC2_NATIVE_ONLY_PACKAGES)
    continue()
  endif()
  if(EXISTS ${CMAKE_SOURCE_DIR}/Source/${pkg}/CMakeLists.txt)
    add_subdirectory(Source/${pkg})
  endif()
endforeach()

# --- T3: the patched script packages (target `scripts`, part of ALL): decompile the shipped .u, apply patches/, compile
# with this build's ucc on the host preset; copy the host build's on the others. Options UC2_SCRIPT_PATCHES (ON),
# UC2_SCRIPT_PATCH_SETS, UC2_ASSETS_DIR, UC2_SCRIPT_PACKAGES_DIR, UC2_SCRIPT_RETAIL_INIS_DIR, UC2_SCRIPTS_DIR: see
# cmake/UC2Scripts.cmake, BUILD.md "Script patches", docs/porting/analysis/script-build-rules.md. -----------------
include(UC2Scripts)
# --- end T3 ------------------------------------------------------------------------------------------------------

# --- T2: `cmake --build build-native --target check-headers`: the generated Source/<Pkg>/Inc/<Pkg>Classes.h must be
# what the header exporter of this build's ucc-bin writes from the script sources (tools/dev/check-headers.sh; fails
# with the diff when stale; regenerate with tools/dev/regen-headers.sh). Only where ucc-bin carries the script
# compiler and runs on the build host: native Linux, 32-bit (the Editor target). Not part of ALL: it compiles
# script packages in a scratch run tree and needs the asset tree. UC2_HEADERS_CLASSES_ROOT: where the script sources
# are. T3: by default the build's patched tree (Scripts/tree, made by the `scripts` target; check-headers.sh then
# compiles the packages the patches rebuild against the shipped ones and checks their headers); with
# UC2_SCRIPT_PATCHES OFF, Source/ while it still holds the .uc. docs/porting/analysis/headers.md. ------------------
set(UC2_HEADERS_CLASSES_ROOT "" CACHE PATH "Script sources (<dir>/<Pkg>/Classes) the check-headers target exports from (default: <build>/Scripts/tree)")
if(TARGET Editor AND TARGET UCC AND NOT WIN32 AND NOT EMSCRIPTEN AND NOT CMAKE_CROSSCOMPILING)
  set(_hdr_root "${UC2_HEADERS_CLASSES_ROOT}")
  set(_hdr_deps UCC)
  # (T2's configure wrote Source/ into the cache as the default: it counts as unset.)
  if(NOT _hdr_root OR _hdr_root STREQUAL "${CMAKE_SOURCE_DIR}/Source")
    if(UC2_SCRIPTS_TREE)
      set(_hdr_root "${UC2_SCRIPTS_TREE}")
      list(APPEND _hdr_deps scripts)
    else()
      set(_hdr_root "${CMAKE_SOURCE_DIR}/Source")
    endif()
  endif()
  add_custom_target(check-headers
    COMMAND ${CMAKE_SOURCE_DIR}/tools/dev/check-headers.sh --build ${CMAKE_BINARY_DIR} --classes-root ${_hdr_root}
    DEPENDS ${_hdr_deps}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL VERBATIM)
endif()
# --- end T2 ------------------------------------------------------------------------------------------------------
