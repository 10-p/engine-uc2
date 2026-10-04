#=============================================================================
# UC2Web.cmake -- what the Emscripten build adds to every target (M4w, early web smoke).
#
# Included once from the root CMakeLists.txt, inside `if(EMSCRIPTEN)`, after the shared compile flags and
# before any library is added. Kept in its own file so the root CMakeLists only carries the include line.
# Notebook: docs/porting/analysis/web-smoke.md.
#=============================================================================

# C++ exceptions are CAUGHT, as on every native target. UC2's loaders report a missing or incompatible
# package or object by throwing (appThrowf) and catch it a few frames up (UObject::StaticLoadObject,
# GetPackageLinker, LoadPackage, UGameEngine::LoadMap...). Natively this is ordinary control flow: 56 of
# the 58 maps import the editor's Editor.TransBuffer, which is not built, and load because that throw is
# caught. Emscripten's default (-sDISABLE_EXCEPTION_CATCHING=1) compiles every catch away, so the first
# such throw would abort the module. Setting it at COMPILE time is what keeps the catch blocks; the link
# of every executable must say the same (UC2_WEB_EXCEPTION_FLAGS below).
#
# Why JavaScript-based exceptions and not -fwasm-exceptions: the browser client uses Asyncify for its
# per-frame yield, and Emscripten 5.0.7 refuses -fwasm-exceptions with ASYNCIFY=1 (wasm-opt: "Module::
# getFunction: __asyncify_get_call_index does not exist"). JS exceptions do work under Asyncify in this
# version (it lists env.invoke_* as Asyncify imports): measured with a throw, a catch and an
# emscripten_sleep inside the try (web-smoke.md, W0). The cost is an invoke_ trampoline around calls that
# can throw; engine-ut2004 instead compiles its engine with catching off and patches every loader to
# fail soft. That is a performance question for M11, not one for a smoke test.
add_compile_options(-sNO_DISABLE_EXCEPTION_CATCHING)
set(UC2_WEB_EXCEPTION_FLAGS -sNO_DISABLE_EXCEPTION_CATCHING)

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
