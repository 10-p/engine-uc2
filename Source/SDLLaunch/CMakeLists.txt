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
target_link_libraries(${PROJECT_NAME} PRIVATE
  -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
  SDL2::SDL2-static zlib dl pthread m
)
target_link_options(${PROJECT_NAME} PRIVATE -Wl,-export-dynamic)

# --- M4w: the browser build (SDLLaunch.js/.wasm/.data + meta.json); see cmake/Web.cmake ---------------------
if(EMSCRIPTEN)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Web.cmake)
endif()
# --- end M4w ---------------------------------------------------------------------------------------------
