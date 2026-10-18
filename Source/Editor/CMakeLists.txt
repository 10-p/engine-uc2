#=============================================================================
# Editor - the UnrealScript compiler (`ucc-bin make`), the C++ header exporter and the editor commandlets.
# Built at 32-bit pointers only (root CMakeLists.txt), never on the web. The sources of Editor.vcproj plus
# UC2EditorRegistrants.cpp (the glob below).
#=============================================================================

project(Editor CXX)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "Src/*.cpp")
add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
# UC2_STATIC_CLASS_ACCESSORS: one C accessor per class (Core/Inc/UnObjBas.h), used by Src/UC2EditorRegistrants.cpp
# because most Editor classes are declared inside their .cpp files.
target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=GEditorPackage ThisPackage=Editor UC2_STATIC_CLASS_ACCESSORS=1)

target_link_libraries(${PROJECT_NAME} PUBLIC Engine)
