#=============================================================================
# WinDrv - the Win32 client and viewport (WinDrv.WindowsClient, WinDrv.WindowsViewport: windows, keyboard
# and mouse, DirectInput) and the script debugger's glue; the editor's viewports. MinGW only (root
# CMakeLists.txt). Its resources (Src/Res/WinDrvRes.rc: the editor's cursors) go into the executable that
# links it, as for Window.
#=============================================================================

project(WinDrv CXX)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "Src/*.cpp")
add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
target_compile_definitions(${PROJECT_NAME}
  PRIVATE GPackage=GWinDrvPackage ThisPackage=WinDrv
  PUBLIC WINDRV_API=
)

target_link_libraries(${PROJECT_NAME} PUBLIC Window Engine dinput8 dxguid dxerr8 imm32 winmm user32 gdi32)

uc2_win32_resources(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/Src/Res/WinDrvRes.rc)
