#=============================================================================
# SDLLaunch - the game executable, uc2.bin (SDL2 client; null render and audio devices until M5/M8).
#=============================================================================

project(SDLLaunch CXX)

add_executable(${PROJECT_NAME} Src/SDLLaunch.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME uc2.bin)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  ${CMAKE_SOURCE_DIR}/Source/Engine/Src
  ${CMAKE_SOURCE_DIR}/Source/IpDrv/Src
)

# Static link as for ucc-bin (Source/UCC/CMakeLists.txt): every package whole, so the class registrants and
# native tables survive, and exported dynamically, because appGetDllHandle finds a linked-in package by its
# GLoaded<Package> symbol.
set(LINK_PACKAGES SDL2Drv GUI UCGameplay UCElements UnrealGame UWeb IpDrv Engine Core)
# M5: the OpenGL render device, where it is built (native; the web build gets it with GL4ES later).
if(TARGET UC2GLDrv)
  list(PREPEND LINK_PACKAGES UC2GLDrv)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2GLDRV=1)
endif()
# M11: its GL4ES binding (Linux: the native twin of the web renderer; Emscripten: the web renderer).
if(TARGET UC2GL4ESDrv)
  list(PREPEND LINK_PACKAGES UC2GL4ESDrv)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2GL4ESDRV=1)
endif()
# M8: the OpenAL audio device, where it is built (native; the web build adopts it at M11).
if(TARGET UC2ALAudio)
  list(PREPEND LINK_PACKAGES UC2ALAudio)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2ALAUDIO=1)
endif()
# M9: the movie player (factory + `ucc-bin UC2Movie.UC2MovieSelfTest`).
if(TARGET UC2Movie)
  list(PREPEND LINK_PACKAGES UC2Movie)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2MOVIE=1)
endif()
if(MINGW)
  # M12: uc2.exe. A Windows-subsystem program like Epic's Avaris.exe (no console window; the log is
  # System/Avaris.log, -log still echoes to a console if one is attached). MinGW's CRT calls our main()
  # for either subsystem, so SDL2main is not used. Nothing exported (see Source/UCC/CMakeLists.txt).
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME uc2 WIN32_EXECUTABLE TRUE)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
    SDL2::SDL2-static zlib ws2_32 winmm)
else()
target_link_libraries(${PROJECT_NAME} PRIVATE
  -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
  SDL2::SDL2-static zlib dl pthread m
)
target_link_options(${PROJECT_NAME} PRIVATE -Wl,-export-dynamic)
endif()

# --- M4w: the browser build (SDLLaunch.js/.wasm/.data + meta.json); see cmake/Web.cmake ---------------------
if(EMSCRIPTEN)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Web.cmake)
endif()
# --- end M4w ---------------------------------------------------------------------------------------------
