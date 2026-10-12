#=============================================================================
# WebRTCDrv - the browser's network driver (E-2): engine packets over the frontend's WebRTC data channels
# (vendor/glue/webrtc-shim.js forwards the webrtc_* imports to @ufront/glue). Web build only, as in
# engine-ut2004 (its root CMakeLists adds Source/WebRTCDrv inside if(EMSCRIPTEN)): natively the game keeps
# IpDrv.TcpNetDriver, and the driver has no transport outside a browser. Notebook:
# docs/porting/analysis/webrtc.md.
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

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Inc)
# netplay: the AUdpLink hook table (Source/IpDrv/Inc/UdpLinkWeb.h) that Src/WebRTCLan.cpp fills in. Header only:
# IpDrv calls through the table, so neither library links the other.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Source/IpDrv/Inc)

# GPackage names the symbol IMPLEMENT_PACKAGE defines under __STATIC_LINK (see Source/Core/CMakeLists.txt).
target_compile_definitions(${PROJECT_NAME} PRIVATE GPackage=GWebRTCDrvPackage ThisPackage=WebRTCDrv)

target_link_libraries(${PROJECT_NAME} PUBLIC Engine)
