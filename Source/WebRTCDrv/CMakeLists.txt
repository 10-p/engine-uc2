#=============================================================================
# WebRTCDrv - the browser's network driver: engine packets over the frontend's WebRTC data channels
# (vendor/glue/net-shim.js forwards the ue_net_* imports of vendor/glue/ue_abi.h to @ufront/glue). Web build only: natively the
# game keeps IpDrv.TcpNetDriver, and the driver has no transport outside a browser.
#=============================================================================

if(NOT EMSCRIPTEN)
  return()
endif()

project(WebRTCDrv CXX)

add_library(${PROJECT_NAME} ${LIB_TYPE}
  Src/WebRTCDrv.cpp
  Src/WebRTCNetDriver.cpp
  Src/WebRTCConnection.cpp
  Src/WebRTCLan.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc ${CMAKE_SOURCE_DIR}/vendor/glue)
# The AUdpLink hook table (Source/IpDrv/Inc/UdpLinkWeb.h) that Src/WebRTCLan.cpp fills in. Header only:
# IpDrv calls through the table, so neither library links the other.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Source/IpDrv/Inc)

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=GWebRTCDrvPackage ThisPackage=WebRTCDrv)

target_link_libraries(${PROJECT_NAME} PUBLIC Engine)
