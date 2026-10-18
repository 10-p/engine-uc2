#=============================================================================
# UC2GL4ESDrv - the GL4ES binding of UC2GLDrv: UC2GL4ESDrv.UC2GL4ESRenderDevice.
#
# Not a second driver. The renderer is Source/UC2GLDrv, compiled once; this package is a subclass of its
# device that overrides the binding (an OpenGL ES 3 context, ES 2 as the fallback, through SDL/EGL on Linux;
# WebGL 2, WebGL 1 as the fallback, on the web; entry points from gl4es, which translates the driver's
# desktop-GL calls). It is the only code that links gl4es. On Linux it is the -GL4ES device, so the
# renderer tests run the web code path without a browser.
#=============================================================================

if(NOT TARGET gl4es::GL OR NOT TARGET UC2GLDrv)
  # Windows: no gl4es (root CMakeLists.txt).
  return()
endif()

project(UC2GL4ESDrv CXX)

add_library(${PROJECT_NAME} ${LIB_TYPE} Src/UC2GL4ESDrv.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc)
# gl4es' own headers are deliberately not on the include path: the driver's GL header stays the
# system's, and the three gl4es entry points this package calls are declared by hand.

target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=GUC2GL4ESDrvPackage ThisPackage=UC2GL4ESDrv)

target_link_libraries(${PROJECT_NAME} PUBLIC UC2GLDrv Engine PRIVATE gl4es::GL SDL2::SDL2-static)
