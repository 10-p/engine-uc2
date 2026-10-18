#=============================================================================
# UC2Movie - the movie player: cooked Ogg Theora/Vorbis movies (tools/cook-movies.py), behind the engine's
# portable movie interface (Engine/Inc/UnMovie.h). Built on every target, the web included.
#=============================================================================

project(UC2Movie CXX)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "Src/*.cpp")
add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=GUC2MoviePackage ThisPackage=UC2Movie)

target_link_libraries(${PROJECT_NAME} PUBLIC Engine PRIVATE theora vorbis ogg)
