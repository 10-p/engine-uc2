# toolchain-aarch64-linux-gnu.cmake -- cross-compile the Linux arm64 (aarch64, LP64) build (preset arm64).
#
# A build-only target: the code is kept 64-bit clean with the same LP64 data model as native64 (x86_64), the
# 64-bit build that is run. Needs, on Debian/Ubuntu:
#   sudo apt install g++-aarch64-linux-gnu            # the cross compiler (+ binutils)
#   sudo apt install qemu-user qemu-user-binfmt       # to run ucc-bin under emulation
# and for the client (SDL2's X11/GL probes, fetched SDL2 is built from source): the arm64 X11/GL development
# packages through multiarch (`dpkg --add-architecture arm64`; libx11-dev:arm64 libxext-dev:arm64
# libgl-dev:arm64 ...). char is unsigned on arm: the build keeps
# -fsigned-char (CMakeLists.txt), and the x86_64 -funsigned-char audit build (UE_UNSIGNED_CHAR_AUDIT) is what
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
# pkg-config sees the target's .pc files only: the host's (x86_64 PipeWire, PulseAudio, sndio, libsamplerate) would
# turn on SDL2's and OpenAL Soft's audio backends and put host libraries on the arm64 link.
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/${_TRIPLE}/pkgconfig:/usr/${_TRIPLE}/lib/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
# The cross compiler also searches the host's /usr/include, where SDL2 finds samplerate.h with no arm64 libsamplerate
# to link: SDL2 resamples with its own code here.
set(SDL_LIBSAMPLERATE OFF CACHE BOOL "SDL2 audio resampling through libsamplerate (no arm64 library)")
# Run test programs (none at configure time today) under qemu when it is there.
find_program(_QEMU_AARCH64 qemu-aarch64)
if(_QEMU_AARCH64)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${_QEMU_AARCH64} -L /usr/${_TRIPLE})
endif()
