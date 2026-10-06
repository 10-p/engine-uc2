#=============================================================================
# UC2ALAudio - the audio device: cooked XACT data (tools/cook-audio.py) played through OpenAL Soft.
#
# Natively OpenAL is not linked: the device dlopens libopenal when it starts (the fetched openal-soft, copied
# beside uc2.bin by tools/dev/mkrun-uc2.sh), so only the headers are needed here. On the web it is linked
# (M11). Vorbis is linked statically.
#=============================================================================

project(UC2ALAudio CXX)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "Src/*.cpp")
add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src
)
if(EMSCRIPTEN)
  # M11 (O-2): the web build links openal-soft (static, Web Audio backend; root CMakeLists.txt) and
  # UC2ALBind fills its table with the linked symbols.
  target_link_libraries(${PROJECT_NAME} PRIVATE OpenAL)
elseif(TARGET OpenAL)
  target_include_directories(${PROJECT_NAME} PRIVATE $<TARGET_PROPERTY:OpenAL,INTERFACE_INCLUDE_DIRECTORIES>)
  add_dependencies(${PROJECT_NAME} OpenAL)   # the library itself is copied into run trees beside uc2.bin
endif()

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=GUC2ALAudioPackage ThisPackage=UC2ALAudio)

target_link_libraries(${PROJECT_NAME} PUBLIC Engine PRIVATE vorbis ogg)
