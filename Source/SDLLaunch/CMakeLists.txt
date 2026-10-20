#=============================================================================
# SDLLaunch - the game executable, uc2.bin (uc2.exe on Windows; SDLLaunch.js/.wasm on the web; libUC2.so on
# Android, the library SDLActivity loads and whose SDL_main it calls).
#=============================================================================

project(SDLLaunch CXX)

if(ANDROID)
  # The name GameActivity.getLibraries() lists after SDL2.
  add_library(${PROJECT_NAME} SHARED Src/SDLLaunch.cpp)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME UC2)
else()
  add_executable(${PROJECT_NAME} Src/SDLLaunch.cpp)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME uc2.bin)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  ${CMAKE_SOURCE_DIR}/Source/Engine/Src
  ${CMAKE_SOURCE_DIR}/Source/IpDrv/Src
)

# Static link as for ucc-bin (Source/UCC/CMakeLists.txt): every package whole, so the class registrants and
# native tables survive, and exported dynamically, because appGetDllHandle finds a linked-in package by its
# GLoaded<Package> symbol.
set(LINK_PACKAGES SDL2Drv GUI UCGameplay UCElements UnrealGame UWeb IpDrv Engine Core)
# The OpenGL render device (also the parent class of the GL4ES device on the web).
if(TARGET UC2GLDrv)
  list(PREPEND LINK_PACKAGES UC2GLDrv)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2GLDRV=1)
endif()
# Its GL4ES binding (Linux: the -GL4ES device; Emscripten: the web renderer).
if(TARGET UC2GL4ESDrv)
  list(PREPEND LINK_PACKAGES UC2GL4ESDrv)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2GL4ESDRV=1)
endif()
# The OpenAL audio device.
if(TARGET UC2ALAudio)
  list(PREPEND LINK_PACKAGES UC2ALAudio)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2ALAUDIO=1)
endif()
# The movie player (factory + `ucc-bin UC2Movie.UC2MovieSelfTest`).
if(TARGET UC2Movie)
  list(PREPEND LINK_PACKAGES UC2Movie)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2MOVIE=1)
endif()
# The browser's network driver (web build only; Source/WebRTCDrv/CMakeLists.txt).
if(TARGET WebRTCDrv)
  list(PREPEND LINK_PACKAGES WebRTCDrv)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_WEBRTCDRV=1)
endif()
if(MINGW)
  # uc2.exe. A Windows-subsystem program like Epic's Avaris.exe (no console window; the log is
  # System/Avaris.log, -log still echoes to a console if one is attached). MinGW's CRT calls our main()
  # for either subsystem, so SDL2main is not used. Nothing exported (see Source/UCC/CMakeLists.txt).
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME uc2 WIN32_EXECUTABLE TRUE)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
    SDL2::SDL2-static zlib ws2_32 winmm)
elseif(ANDROID)
  # bionic has pthread and libm in libc. libopenal.so is packaged beside it but not linked: UC2ALAudio dlopens it
  # when the audio device starts, as natively. OpenGL ES and EGL are SDL's (it looks them up at run time); log is
  # the logcat sink, android the native activity API SDL's video uses.
  target_link_libraries(${PROJECT_NAME} PRIVATE
    -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
    SDL2::SDL2-static zlib log android dl m)
  # appGetDllHandle finds a linked-in package by its GLoaded<Package> symbol: keep every symbol in the
  # dynamic table.
  target_link_options(${PROJECT_NAME} PRIVATE -Wl,-export-dynamic)
else()
target_link_libraries(${PROJECT_NAME} PRIVATE
  -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
  SDL2::SDL2-static zlib dl pthread m
)
target_link_options(${PROJECT_NAME} PRIVATE -Wl,-export-dynamic)
endif()

# The browser build (SDLLaunch.js/.wasm/.data + meta.json): cmake/Web.cmake.
if(EMSCRIPTEN)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Web.cmake)
endif()
