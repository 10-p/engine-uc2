#=============================================================================
# UC2Web.cmake -- what the Emscripten build adds to every target.
#
# Included once from the root CMakeLists.txt, inside `if(EMSCRIPTEN)`, after the shared compile flags and
# before any library is added.
#=============================================================================

# C++ exception catching is off for the engine's own code (Emscripten's default), so the engine's objects
# carry no invoke_ trampolines and Asyncify instruments none of them; a throw from engine code is an abort.
# The loaders therefore fail soft on the web instead of throwing, under __EMSCRIPTEN__ in Core/Src/UnObj.cpp
# (SafeLoadError, GetPackageLinker, StaticLoadObject), Core/Src/UnLinker.cpp (ULinkerLoad's constructor,
# VerifyImport) and Engine/Src/UnGame.cpp (LoadMap, the start-up map, Listen). Native builds keep catching.
# A fatal appErrorf logs "appError called:" and its message, then the module aborts. In a normal boot the
# only throws are a script DynamicLoadObject of a name with no package and ucc's commandlet-name lookup.
# openal-soft reports every AL error by throwing and catching it inside the library, so it alone is compiled
# with -fexceptions (root CMakeLists.txt) and the executables link with -sNO_DISABLE_EXCEPTION_CATCHING
# (Source/SDLLaunch/cmake/Web.cmake, Source/UCC/CMakeLists.txt).

# Stores a destructor makes to its own object are kept. Epic's code calls destructors explicitly and then
# reuses the object: UArrayProperty::DestroyValue runs `DestArray->~FArray()`, whose `Data=NULL;
# ArrayNum=ArrayMax=0;` is what lets UObject::LoadConfig AddZeroed into the same FArray afterwards. Clang
# deletes those stores as dead after the end of the object's lifetime, which leaves the array its freed buffer
# and ArrayMax, and the next AddZeroed writes into freed memory. GCC has the same optimisation and the root
# CMakeLists turns it off there; this is the Clang spelling of that flag.
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
