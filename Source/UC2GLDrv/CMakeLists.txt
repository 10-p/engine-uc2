#=============================================================================
# UC2GLDrv - the OpenGL render device (M5, renderer R1: fixed function).
#
# One source tree. The GL binding is chosen at build time (Inc/UC2GLBinding.h): desktop OpenGL whose entry
# points are looked up through SDL_GL_GetProcAddress into the device's pointer table (this build), or, later,
# GL4ES for GLES2/WebGL from the same sources. No GL library is linked: SDL loads libGL at run time.
#=============================================================================

project(UC2GLDrv CXX)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "Src/*.cpp")
add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=GUC2GLDrvPackage ThisPackage=UC2GLDrv)

target_link_libraries(${PROJECT_NAME} PUBLIC Engine PRIVATE SDL2::SDL2-static)
