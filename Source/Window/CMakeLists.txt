#=============================================================================
# Window - Epic's Win32 widget library (WWindow, WDialog, the property grid and the browsers' controls),
# the user interface of UnrealEd. MinGW only (root CMakeLists.txt). Its resources (Src/Res/WindowRes.rc) go
# into the executable that links it: Source/UnrealEd/CMakeLists.txt merges them with the other packages'.
#=============================================================================

project(Window CXX)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "Src/*.cpp")
add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
# WINDOW_API is empty: the package is a static library, never a DLL (Window.h would make it dllimport).
target_compile_definitions(${PROJECT_NAME}
  PRIVATE GPackage=GWindowPackage ThisPackage=Window
  PUBLIC WINDOW_API= _WINDOWS _WIN32_IE=0x0200
)

target_link_libraries(${PROJECT_NAME} PUBLIC Engine comctl32 comdlg32 shell32 ole32 gdi32)

uc2_win32_resources(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/Src/Res/WindowRes.rc)
