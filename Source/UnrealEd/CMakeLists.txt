#=============================================================================
# UnrealEd - the level editor, UnrealEd.exe (Windows subsystem): Epic's UnrealEd sources on the Editor package,
# with Window and WinDrv for its user interface and UC2WGLDrv (the GL driver through WGL) for its viewports.
# MinGW only, 32-bit (root CMakeLists.txt). It runs on the game folder's UnrealEd.u and Editor.u; the files it needs
# beyond the game folder's are listed in release-files.txt.
#=============================================================================

project(UnrealEd CXX)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "Src/*.cpp")
add_executable(${PROJECT_NAME} WIN32 ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  ${CMAKE_SOURCE_DIR}/Source/Editor/Src
  ${CMAKE_SOURCE_DIR}/Source/Engine/Src
  ${CMAKE_SOURCE_DIR}/Source/IpDrv/Src
)

# UnrealEd is the executable as well as a script package: Main.cpp defines Core's GPackage (the program's name, as a
# launcher does), so no GPackage=G<Pkg>Package here; IMPLEMENT_PACKAGE(UnrealEd) still defines GUnrealEdPackage.
target_compile_definitions(${PROJECT_NAME} PRIVATE ThisPackage=UnrealEd _EDITOR_)

# Static link as for uc2.exe and ucc.exe: every package whole, so the class registrants and native tables survive
# (Main.cpp registers them). The game's packages are there because the editor loads and plays its maps.
set(LINK_PACKAGES Editor WinDrv Window UC2WGLDrv UC2GLDrv GUI UCGameplay UCElements UnrealGame UWeb IpDrv Engine Core)
if(TARGET UC2ALAudio)
  list(PREPEND LINK_PACKAGES UC2ALAudio)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2ALAUDIO=1)
endif()
if(TARGET UC2Movie)
  list(PREPEND LINK_PACKAGES UC2Movie)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UC2_WITH_UC2MOVIE=1)
endif()

# SDL2: the desktop binding of UC2GLDrv's base device names SDL's GL functions; the WGL device overrides every one of
# them, so SDL is linked but never initialised.
target_link_libraries(${PROJECT_NAME} PRIVATE
  -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
  SDL2::SDL2-static zlib ws2_32 winmm comctl32 comdlg32 shell32 ole32 dinput8 dxguid dxerr8 imm32 gdi32 opengl32)

# The dialogs, menus, bitmaps and cursors of Window, WinDrv and UnrealEd and the application manifest, merged into one
# resource object.
uc2_win32_resources(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/Src/res/UnrealEd.rc)
uc2_link_win32_resources(${PROJECT_NAME} Window WinDrv ${PROJECT_NAME} UC2Manifest)
