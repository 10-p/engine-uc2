#=============================================================================
# libtheora 1.2.0 - vendored Theora video DECODER only (no encoder, no assembly)
#=============================================================================

project(theora C)

set(SRC_FILES
  lib/bitpack.c
  lib/decinfo.c
  lib/decode.c
  lib/dequant.c
  lib/fragment.c
  lib/huffdec.c
  lib/idct.c
  lib/info.c
  lib/internal.c
  lib/quant.c
  lib/state.c
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The vendored libogg is 1.0 (Thirdparty/Vorbis/ogg), which predates the two unsigned types libtheora 1.2 uses.
target_compile_definitions(${PROJECT_NAME}
  PUBLIC "ogg_uint16_t=unsigned short" "ogg_uint64_t=unsigned long long"
)

target_link_libraries(${PROJECT_NAME} PUBLIC ogg)
