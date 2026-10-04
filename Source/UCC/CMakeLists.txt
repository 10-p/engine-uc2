#=============================================================================
# UCC - commandlet host (ucc-bin): `ucc-bin server <map>`, `batchexport`, and from M14 `make`.
#=============================================================================

project(UCC CXX)

add_executable(${PROJECT_NAME} Src/UCC.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ucc-bin)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  ${CMAKE_SOURCE_DIR}/Source/Engine/Src
  ${CMAKE_SOURCE_DIR}/Source/IpDrv/Src
)

# Static link: every package whole, so the class registrants and native tables survive; exported dynamically
# because appGetDllHandle finds a linked-in package by looking up its GLoaded<Package> symbol in the program.
set(LINK_PACKAGES GUI UCGameplay UCElements UnrealGame UWeb IpDrv Engine Core)
target_link_libraries(${PROJECT_NAME} PRIVATE
  -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive
  zlib dl pthread m
)
target_link_options(${PROJECT_NAME} PRIVATE -Wl,-export-dynamic)
