#=============================================================================
# Engine - actors, levels, rendering front end, networking, script natives.
#=============================================================================

project(Engine CXX)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "Src/*.cpp")

# In no Visual Studio project, and not buildable: the vehicle classes UnSVehicle.cpp implements do not exist in
# UC2's script, and palette.cpp is a leftover tool source.
list(REMOVE_ITEM SRC_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/Src/UnSVehicle.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Src/palette.cpp")

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# See Source/Core/CMakeLists.txt for why GPackage is G<pkg>Package.
target_compile_definitions(${PROJECT_NAME} PRIVATE
  GPackage=GEnginePackage
  ThisPackage=Engine
)

target_link_libraries(${PROJECT_NAME} PUBLIC Core)
