#=============================================================================
# UC2WGLDrv - the WGL binding of UC2GLDrv: UC2WGLDrv.UC2WGLRenderDevice, the editor's render device.
#
# Not a second driver. The renderer is Source/UC2GLDrv, compiled once; this package is a subclass of its device
# that overrides the binding (Inc/UC2GLBinding.h): desktop OpenGL on WinDrv's Win32 windows through WGL, one
# context for every viewport. MinGW only (root CMakeLists.txt).
#=============================================================================

project(UC2WGLDrv CXX)

add_library(${PROJECT_NAME} ${LIB_TYPE} Src/UC2WGLDrv.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc)

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=GUC2WGLDrvPackage ThisPackage=UC2WGLDrv)

# opengl32: the WGL functions and the OpenGL 1.1 entry points; everything newer comes from wglGetProcAddress.
target_link_libraries(${PROJECT_NAME} PUBLIC UC2GLDrv Engine PRIVATE opengl32 gdi32 user32)
