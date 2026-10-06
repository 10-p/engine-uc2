# toolchain-aarch64-linux-gnu.cmake -- cross-compile the Linux arm64 (aarch64, LP64) build (M13, preset arm64).
#
# Build-only for now (owner, 2026-10-04): the code is kept 64-bit clean and the same LP64 data model as
# native64 (x86_64), which is the build that is actually run. Needs, on Debian/Ubuntu:
#   sudo apt install g++-aarch64-linux-gnu            # the cross compiler (+ binutils)
#   sudo apt install qemu-user qemu-user-binfmt       # to run ucc-bin under emulation
# and for the client (SDL2's X11/GL probes, fetched SDL2 is built from source): the arm64 X11/GL development
# packages through multiarch (`dpkg --add-architecture arm64`; libx11-dev:arm64 libxext-dev:arm64
# libgl-dev:arm64 ...). docs/porting/BLOCKERS.md has the list. char is unsigned on arm: the build keeps
# -fsigned-char (CMakeLists.txt), and the x86_64 -funsigned-char audit build (UC2_UNSIGNED_CHAR_AUDIT) is what
# shows the code does not depend on it.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(_TRIPLE aarch64-linux-gnu)
set(CMAKE_C_COMPILER   ${_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${_TRIPLE}-g++)
set(CMAKE_FIND_ROOT_PATH /usr/${_TRIPLE} /usr/lib/${_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Run test programs (none at configure time today) under qemu when it is there.
find_program(_QEMU_AARCH64 qemu-aarch64)
if(_QEMU_AARCH64)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${_QEMU_AARCH64} -L /usr/${_TRIPLE})
endif()
