#=============================================================================
# UC2Web.cmake -- what the Emscripten build adds to every target (M4w, early web smoke).
#
# Included once from the root CMakeLists.txt, inside `if(EMSCRIPTEN)`, after the shared compile flags and
# before any library is added. Kept in its own file so the root CMakeLists only carries the include line.
# Notebook: docs/porting/analysis/web-smoke.md.
#=============================================================================

# C++ exception CATCHING is OFF for the engine's own code (Emscripten's default), as engine-ut2004 builds its
# engine (owner, 2026-10-09: "Ok, lets disable exception handling. Lets do like UT2004."). So the engine's
# objects carry no invoke_ trampolines and Asyncify instruments none of them; a throw from engine code is an
# abort. The loaders therefore fail SOFT on the web instead of throwing: engine-ut2004's patches, ported
# under __EMSCRIPTEN__ in Core/Src/UnObj.cpp (SafeLoadError, GetPackageLinker, StaticLoadObject),
# Core/Src/UnLinker.cpp (ULinkerLoad's constructor, VerifyImport) and Engine/Src/UnGame.cpp (LoadMap, the
# start-up map). Native builds keep catching, as engine-ut2004's do. A fatal appErrorf logs "appError
# called:" and its message, then the module aborts (engine-ut2004's shipping build does the same).
# Measured before the change (gdb, `catch throw`, native client and server boots): the only throws in a
# normal boot are a script DynamicLoadObject of a name with no package and ucc's commandlet-name lookup;
# the Editor.TransBuffer import of 56 of 58 maps takes VerifyImport's "Missing" path and never threw.
# openal-soft is the exception to the rule: it reports every AL error by throwing and catching it inside
# the library, so it alone is compiled with -fexceptions (root CMakeLists.txt) and the executables link
# with -sNO_DISABLE_EXCEPTION_CATCHING (SDLLaunch/cmake/Web.cmake, UCC/CMakeLists.txt), exactly as
# engine-ut2004 links SDLLaunch. Notebook: docs/porting/analysis/platform-f4.md (F-4a).

# Stores a destructor makes to its own object are KEPT. Epic's code calls destructors explicitly and then
# reuses the object: UArrayProperty::DestroyValue runs `DestArray->~FArray()`, whose `Data=NULL;
# ArrayNum=ArrayMax=0;` is what lets UObject::LoadConfig AddZeroed into the same FArray afterwards. Clang
# (LLVM 23 in Emscripten 5.0.7) deletes those stores as dead after the end of the object's lifetime, so the
# array kept its freed buffer and ArrayMax and the next AddZeroed wrote into freed memory (AddressSanitizer:
# heap-use-after-free in LoadConfig, while loading Engine.u; web-smoke.md, W0). GCC has the same
# optimisation and the root CMakeLists already turns it off there; this is the Clang spelling of that flag.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-lifetime-dse>)

# SDL2 comes from Emscripten's own port (-sUSE_SDL=2), not from FetchContent. SDL2Drv and SDLLaunch link
# the target name the native build defines (SDL2::SDL2-static), so the port is presented under that name
# and their CMakeLists stay identical on both toolchains.
if(NOT TARGET SDL2::SDL2-static)
  add_library(SDL2::SDL2-static INTERFACE IMPORTED GLOBAL)
  set_target_properties(SDL2::SDL2-static PROPERTIES
    INTERFACE_COMPILE_OPTIONS "-sUSE_SDL=2"
    INTERFACE_LINK_OPTIONS "-sUSE_SDL=2")
endif()
