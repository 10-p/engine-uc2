#=============================================================================
# UC2Scripts.cmake -- the patched script packages as part of the build.
#
# The game's logic is UnrealScript compiled into the 17 .u packages of the game's System folder. This repo holds no
# script, only patch sets (patches/, patches/README.md). With UE_SCRIPT_PATCHES ON (the default) the build turns
# the user's shipped .u into text, applies the chosen sets and compiles the packages they change:
#
#   host preset (the build runs its own ucc: native Linux; MinGW i686 on a Windows host)
#     Scripts/base/<Pkg>/Classes   tools/scripts/uc-decompile.py of the shipped <Pkg>.u (UCMenu: its shipped text)
#     Scripts/tree/<Pkg>/Classes   tools/scripts/uc-patches.py apply (the sets, in patches/sets.txt order)
#     Scripts/work/                tools/scripts/uc-build.sh's work tree (ucc make + stripsource, its logs)
#     Scripts/System/<Pkg>.u       the result: only the packages the sets rebuild
#   other presets (wasm*, windows/windows64 from Linux, native64*, arm64): Scripts/System/ is a verified copy of the
#     host build's (UE_SCRIPTS_DIR); the web artifact carries it (Source/SDLLaunch/cmake/Web.cmake).
#
# OFF: nothing is produced (no Scripts/ folder) and the game runs the shipped .u (plus Epic's load-time patches).
# Run trees take Scripts/System/*.u (tools/dev/mkrun-uc2.sh). BUILD.md ("Script patches").
#=============================================================================

option(UE_SCRIPT_PATCHES "Build the patched script packages (patches/) into <build>/Scripts/System; OFF: the shipped .u only" ON)
set(UE_SCRIPT_PATCH_SETS "default" CACHE STRING
    "Script patch sets to apply, a ;- or ,-separated list of patches/sets.txt names (default: the sets marked on there)")
set(UE_ASSETS_DIR "" CACHE PATH
    "The game folder: System/ (the shipped .u and Epic's inis) and the content folders (Textures/, StaticMeshes/, ...)")
set(UE_SCRIPT_PACKAGES_DIR "" CACHE PATH
    "The 17 shipped script packages (default: UE_ASSETS_DIR/System)")
set(UE_SCRIPT_RETAIL_INIS_DIR "" CACHE PATH
    "Optional, the pipeline proof's only: the System/ with the Characters.ini, Gametypes.ini and Maplists.ini the shipped packages were stripped with (target scripts-proof; empty: the proof's own default). The build strips with the game folder's")
set(UE_SCRIPTS_DIR "" CACHE PATH
    "Non-host presets: the host build's Scripts/System (default: build-native/Scripts/System; build-windows/... on a Windows host)")

set(UC2_SCRIPTS_OUT "${CMAKE_BINARY_DIR}/Scripts")
set(UC2_SCRIPTS_SYSTEM "${UC2_SCRIPTS_OUT}/System")
# The stamp the web artifact relinks on (Web.cmake): touched whenever Scripts/System changes.
set(UC2_SCRIPTS_STAMP "${UC2_SCRIPTS_OUT}/stamp/System.stamp")
# Set below when this build makes a patched tree (the host preset): check-headers' default classes root.
set(UC2_SCRIPTS_TREE "")
include(${CMAKE_SOURCE_DIR}/cmake/UC2ScriptsCheck.cmake)

# The 17 script packages in EditPackages order (tools/scripts/uc-patches.py ORDER).
set(UC2_SCRIPT_PACKAGES_ALL Core Engine UCAnnouncements Editor UnrealEd IpDrv UWeb GamePlay UnrealGame UCElements
    UCGameplay UCEffects UCCharacters UCInventory UCMutators GUI UCMenu)

# Host: this build has the script compiler (the Editor package in ucc, 32-bit pointers) and runs on this machine.
set(UC2_SCRIPTS_HOST OFF)
if(TARGET Editor AND TARGET UCC AND NOT EMSCRIPTEN)
  if((CMAKE_HOST_WIN32 AND WIN32) OR (CMAKE_HOST_SYSTEM_NAME STREQUAL CMAKE_SYSTEM_NAME AND NOT CMAKE_HOST_WIN32))
    set(UC2_SCRIPTS_HOST ON)
  endif()
endif()

# The web artifact (Source/SDLLaunch/cmake/Web.cmake) copies Scripts/System when SDLLaunch links: relink when the
# packages change (the stamp) or the mode does (this file is rewritten only when its content changes). Every branch
# below defines the target `scripts`.
set(_mode "patches=${UE_SCRIPT_PATCHES} sets=${UE_SCRIPT_PATCH_SETS} host=${UC2_SCRIPTS_HOST} dir=${UE_SCRIPTS_DIR}\n")
set(_mode_file "${CMAKE_BINARY_DIR}/CMakeFiles/uc2-scripts-mode.txt")
set(_old_mode "")
if(EXISTS "${_mode_file}")
  file(READ "${_mode_file}" _old_mode)
endif()
if(NOT _old_mode STREQUAL _mode)
  file(WRITE "${_mode_file}" "${_mode}")
endif()
if(EMSCRIPTEN AND TARGET SDLLaunch)
  add_dependencies(SDLLaunch scripts)
  set_property(TARGET SDLLaunch APPEND PROPERTY LINK_DEPENDS "${_mode_file}")
  if(UE_SCRIPT_PATCHES)
    set_property(TARGET SDLLaunch APPEND PROPERTY LINK_DEPENDS "${UC2_SCRIPTS_STAMP}")
  endif()
endif()

if(NOT UE_SCRIPT_PATCHES)
  # Vanilla: nothing produced, and nothing left over from an earlier patched configure of this build directory
  # (a run tree or the web artifact would pick it up).
  if(EXISTS "${UC2_SCRIPTS_OUT}")
    file(REMOVE_RECURSE "${UC2_SCRIPTS_OUT}")
  endif()
  message(STATUS "Script patches: OFF (the game runs the shipped .u)")
  add_custom_target(scripts COMMAND ${CMAKE_COMMAND} -E echo "scripts: UE_SCRIPT_PATCHES is OFF, nothing to build")
  return()
endif()

file(MAKE_DIRECTORY "${UC2_SCRIPTS_OUT}/stamp")

#-----------------------------------------------------------------------------
# Not the host: copy (and check) the host build's packages.
#-----------------------------------------------------------------------------
if(NOT UC2_SCRIPTS_HOST)
  set(_dir "${UE_SCRIPTS_DIR}")
  if(NOT _dir)
    if(CMAKE_HOST_WIN32)
      set(_dir "${CMAKE_SOURCE_DIR}/build-windows/Scripts/System")
    else()
      set(_dir "${CMAKE_SOURCE_DIR}/build-native/Scripts/System")
    endif()
  endif()
  file(GLOB _u LIST_DIRECTORIES false "${_dir}/*.u")
  if(NOT _u)
    message(FATAL_ERROR
      "Script patches are ON, and this preset cannot compile scripts itself: it takes the packages the host preset\n"
      "built, and there are none in ${_dir}.\n"
      "Run the native preset first (cmake --preset native && cmake --build build-native; on a Windows host the\n"
      "windows preset), or pass -DUE_SCRIPTS_DIR=<a host build's Scripts/System>, or -DUE_SCRIPT_PATCHES=OFF.")
  endif()
  set(_info "")
  if(EXISTS "${_dir}/../build-info.txt")
    file(STRINGS "${_dir}/../build-info.txt" _info REGEX "^sets=")
  endif()
  list(LENGTH _u _n)
  message(STATUS "Script patches: ON, ${_n} packages from the host build ${_dir} (${_info})")
  if(NOT EXISTS "${UC2_SCRIPTS_STAMP}")
    file(TOUCH "${UC2_SCRIPTS_STAMP}")
  endif()
  # Always runs (the host build may have changed); the stamp moves only when the copy changed.
  add_custom_target(scripts ALL
    COMMAND ${CMAKE_COMMAND} -DUE_CHECK=copy -DSRC=${_dir} -DDST=${UC2_SCRIPTS_SYSTEM} -DSTAMP=${UC2_SCRIPTS_STAMP}
            -DREQUIRE=1 -P ${CMAKE_SOURCE_DIR}/cmake/UC2ScriptsCheck.cmake
    BYPRODUCTS ${UC2_SCRIPTS_STAMP}
    COMMENT "Script packages from the host build"
    VERBATIM)
  return()
endif()

#-----------------------------------------------------------------------------
# Host: decompile, apply, build.
#-----------------------------------------------------------------------------
set(_help "Pass the game folder: -DUE_ASSETS_DIR=<dir> (its System/ holds the shipped .u and Epic's inis, beside it ")
string(APPEND _help "the content folders Textures/, StaticMeshes/, ...: tools/dev/mkassets-uc2.sh makes one), or build ")
string(APPEND _help "without the script patches: -DUE_SCRIPT_PATCHES=OFF.")

# The game folder: the cache variable, else the environment variable the dev tools read (tools/dev/mkrun-uc2.sh),
# else the cooked tree beside the repo (<repo>/../assets/uc2-assets) when it is there.
set(_assets "${UE_ASSETS_DIR}")
set(_from "UE_ASSETS_DIR")
if(NOT _assets AND DEFINED ENV{UC2_ASSETS_DIR})
  set(_assets "$ENV{UC2_ASSETS_DIR}")
  set(_from "the UC2_ASSETS_DIR environment variable")
endif()
if(NOT _assets AND EXISTS "${CMAKE_SOURCE_DIR}/../assets/uc2-assets/System")
  get_filename_component(_assets "${CMAKE_SOURCE_DIR}/../assets/uc2-assets" ABSOLUTE)
  set(_from "found beside the repo")
endif()
if(NOT _assets)
  message(FATAL_ERROR "Script patches are ON (the default) and no game folder was given.\n${_help}")
endif()
foreach(_f DefaultXbox.ini DefUserXbox.ini Default.ini DefUser.ini)
  if(NOT EXISTS "${_assets}/System/${_f}")
    message(FATAL_ERROR "Script patches: ${_assets} (${_from}) is not a game folder: no System/${_f}.\n${_help}")
  endif()
endforeach()
set(_shipped "${UE_SCRIPT_PACKAGES_DIR}")
if(NOT _shipped)
  set(_shipped "${_assets}/System")
endif()
# The per-class inis the strip loads are the game folder's, the ones the game runs with. The retail disc's set
# (UE_SCRIPT_RETAIL_INIS_DIR) reaches only the pipeline proof.
set(_retail "${UE_SCRIPT_RETAIL_INIS_DIR}")
set(_proof_retail "")
if(_retail)
  foreach(_f Characters.ini Gametypes.ini Maplists.ini)
    if(NOT EXISTS "${_retail}/${_f}")
      message(FATAL_ERROR "Script patches: UE_SCRIPT_RETAIL_INIS_DIR=${_retail} has no ${_f}")
    endif()
  endforeach()
  set(_proof_retail --retail-inis ${_retail})
else()
  set(_retail "the proof's default")
endif()

# Every package present and shipped. All 17: the build loads every one of them (the compiler resolves against them,
# the strip re-saves them, the decompiler reads them all).
uc2_scripts_check_shipped("${_shipped}" "${CMAKE_SOURCE_DIR}/patches/shipped.sha256" "${UC2_SCRIPT_PACKAGES_ALL}" _bad)
if(_bad)
  message(FATAL_ERROR "Script patches: ${_shipped}\n${_bad}")
endif()

find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
  message(FATAL_ERROR "Script patches need Python 3 (tools/scripts/*.py): install it, or -DUE_SCRIPT_PATCHES=OFF.")
endif()
find_program(UE_BASH bash)
if(NOT UE_BASH)
  message(FATAL_ERROR "Script patches need bash (tools/scripts/uc-build.sh; on Windows: MSYS2), or -DUE_SCRIPT_PATCHES=OFF.")
endif()
set(_tools ${CMAKE_SOURCE_DIR}/tools/scripts)

# The sets, resolved and checked by uc-patches.py (order and dependencies are patches/sets.txt's).
string(REPLACE ";" "," _req "${UE_SCRIPT_PATCH_SETS}")
string(STRIP "${_req}" _req)
if(_req STREQUAL "default" OR _req STREQUAL "")
  set(_sel --default)
else()
  set(_sel --sets ${_req})
endif()
execute_process(COMMAND ${Python3_EXECUTABLE} ${_tools}/uc-patches.py sets ${_sel}
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _sets ERROR_VARIABLE _err OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "UE_SCRIPT_PATCH_SETS=${UE_SCRIPT_PATCH_SETS}: ${_err}")
endif()
execute_process(COMMAND ${Python3_EXECUTABLE} ${_tools}/uc-patches.py packages --sets ${_sets} --compile-list ${_shipped}
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "uc-patches.py packages: ${_err}")
endif()
string(REPLACE "\n" ";" _out "${_out}")
set(_compile "")
set(_keep "")
foreach(_l IN LISTS _out)
  if(_l MATCHES "^([A-Za-z]+)( \\(.*)?$")
    list(APPEND _compile ${CMAKE_MATCH_1})
    if(NOT CMAKE_MATCH_2)
      list(APPEND _keep ${CMAKE_MATCH_1})
    endif()
  endif()
endforeach()
string(REPLACE "," ";" _setlist "${_sets}")
message(STATUS "Script patches: ON, sets ${_sets}; rebuilds ${_keep} (compiles ${_compile}); shipped .u from ${_shipped}; "
               "game folder ${_assets} (${_from}), whose inis the strip loads; the proof's retail inis: ${_retail}")
file(WRITE "${UC2_SCRIPTS_OUT}/build-info.txt" "sets=${_sets}\npackages=${_keep}\nshipped=${_shipped}\nassets=${_assets}\n")

# Nothing from an earlier selection may survive: a stale .u in Scripts/System would be installed by run trees.
file(GLOB _old LIST_DIRECTORIES false "${UC2_SCRIPTS_SYSTEM}/*")
foreach(_f IN LISTS _old)
  get_filename_component(_n "${_f}" NAME_WE)
  if(NOT _n IN_LIST _keep)
    file(REMOVE "${_f}")
  endif()
endforeach()
file(GLOB _old LIST_DIRECTORIES true "${UC2_SCRIPTS_OUT}/base/*")
foreach(_f IN LISTS _old)
  get_filename_component(_n "${_f}" NAME)
  if(NOT _n IN_LIST _compile)
    file(REMOVE_RECURSE "${_f}")
  endif()
endforeach()

if(NOT _keep)
  message(STATUS "Script patches: the chosen sets rebuild no package (Epic's fixes alone are applied at load time)")
  file(REMOVE_RECURSE "${UC2_SCRIPTS_SYSTEM}")
  file(TOUCH "${UC2_SCRIPTS_STAMP}")
  add_custom_target(scripts COMMAND ${CMAKE_COMMAND} -E echo "scripts: the chosen sets rebuild no package")
  return()
endif()

set(_stamps "${UC2_SCRIPTS_OUT}/stamp")
set(_shipped_files "")
foreach(_p IN LISTS UC2_SCRIPT_PACKAGES_ALL)
  list(APPEND _shipped_files "${_shipped}/${_p}.u")
endforeach()

# 1. The shipped input, checked again at build time (a file replaced after the configure).
add_custom_command(OUTPUT ${_stamps}/shipped.stamp
  COMMAND ${CMAKE_COMMAND} -DUE_CHECK=shipped -DDIR=${_shipped} -DSUMS=${CMAKE_SOURCE_DIR}/patches/shipped.sha256
          "-DPACKAGES=${UC2_SCRIPT_PACKAGES_ALL}" -P ${CMAKE_SOURCE_DIR}/cmake/UC2ScriptsCheck.cmake
  COMMAND ${CMAKE_COMMAND} -E touch ${_stamps}/shipped.stamp
  DEPENDS ${_shipped_files} ${CMAKE_SOURCE_DIR}/patches/shipped.sha256 ${CMAKE_SOURCE_DIR}/cmake/UC2ScriptsCheck.cmake
  COMMENT "Scripts: checking the shipped packages"
  VERBATIM)

# 2. The base: one decompile per package (UCMenu: its shipped text + decompiled defaults).
set(_base_stamps "")
foreach(_p IN LISTS _compile)
  set(_x "")
  if(_p STREQUAL "UCMenu")
    set(_x --shipped-text)
  endif()
  add_custom_command(OUTPUT ${_stamps}/base-${_p}.stamp
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${UC2_SCRIPTS_OUT}/base/${_p}
    COMMAND ${Python3_EXECUTABLE} ${_tools}/uc-decompile.py ${_shipped}/${_p}.u --out ${UC2_SCRIPTS_OUT}/base/${_p}/Classes
            --content ${_assets} ${_x}
    COMMAND ${CMAKE_COMMAND} -E touch ${_stamps}/base-${_p}.stamp
    DEPENDS ${_stamps}/shipped.stamp ${_tools}/uc-decompile.py ${_tools}/ucpkg.py
    COMMENT "Scripts: decompiling ${_p}.u"
    VERBATIM)
  list(APPEND _base_stamps ${_stamps}/base-${_p}.stamp)
endforeach()

# 3. The patched tree.
set(_set_files ${CMAKE_SOURCE_DIR}/patches/sets.txt)
foreach(_s IN LISTS _setlist)
  file(GLOB_RECURSE _sf CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/patches/${_s}/*.diff ${CMAKE_SOURCE_DIR}/patches/${_s}/new/*)
  list(APPEND _set_files ${_sf})
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/patches/sets.txt ${CMAKE_SOURCE_DIR}/patches/shipped.sha256)
add_custom_command(OUTPUT ${_stamps}/tree.stamp
  COMMAND ${Python3_EXECUTABLE} ${_tools}/uc-patches.py apply --base ${UC2_SCRIPTS_OUT}/base --sets ${_sets}
          --out ${UC2_SCRIPTS_OUT}/tree
  COMMAND ${CMAKE_COMMAND} -E touch ${_stamps}/tree.stamp
  DEPENDS ${_base_stamps} ${_set_files} ${_tools}/uc-patches.py
  COMMENT "Scripts: applying ${_sets}"
  VERBATIM)

# 4. The build (ucc make against the shipped packages, stripsource), then the kept packages into Scripts/System.
string(REPLACE ";" "," _compile_c "${_compile}")
string(REPLACE ";" "," _keep_c "${_keep}")
# bin/capped (the workspace's memory ceiling wrapper) when it is there; a plain run elsewhere.
set(_capped none)
if(EXISTS "${CMAKE_SOURCE_DIR}/../bin/capped" AND NOT CMAKE_HOST_WIN32)
  get_filename_component(_capped "${CMAKE_SOURCE_DIR}/../bin/capped" ABSOLUTE)
endif()
set(_outputs "")
set(_copies "")
foreach(_p IN LISTS _keep)
  list(APPEND _outputs ${UC2_SCRIPTS_SYSTEM}/${_p}.u)
  list(APPEND _copies COMMAND ${CMAKE_COMMAND} -E copy ${UC2_SCRIPTS_OUT}/work/System/${_p}.u ${UC2_SCRIPTS_SYSTEM}/${_p}.u)
endforeach()
set(_inis "")
foreach(_f DefaultXbox.ini DefUserXbox.ini Default.ini DefUser.ini Characters.ini Gametypes.ini Maplists.ini)
  list(APPEND _inis ${_assets}/System/${_f})
endforeach()
add_custom_command(OUTPUT ${_stamps}/build.stamp ${_outputs}
  COMMAND ${UE_BASH} ${_tools}/uc-build.sh --classes ${UC2_SCRIPTS_OUT}/tree --out ${UC2_SCRIPTS_OUT}/work
          --packages ${_compile_c} --keep ${_keep_c} --shipped ${_shipped} --assets ${_assets}
          --ucc $<TARGET_FILE:UCC> --capped ${_capped}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${UC2_SCRIPTS_SYSTEM}
  ${_copies}
  COMMAND ${CMAKE_COMMAND} -E touch ${_stamps}/build.stamp ${UC2_SCRIPTS_STAMP}
  DEPENDS ${_stamps}/tree.stamp ${_stamps}/shipped.stamp ${_tools}/uc-build.sh UCC ${_inis}
  COMMENT "Scripts: compiling ${_compile_c} (ucc make, stripsource) -> Scripts/System/{${_keep_c}}.u"
  VERBATIM)
add_custom_target(scripts ALL DEPENDS ${_stamps}/build.stamp ${_outputs})
set(UC2_SCRIPTS_TREE "${UC2_SCRIPTS_OUT}/tree")

# The bytecode proofs of the pipeline (tools/scripts/uc-patches-proof.sh; minutes; not part of ALL).
if(NOT CMAKE_HOST_WIN32)
  add_custom_target(scripts-proof
    COMMAND ${UE_BASH} ${_tools}/uc-patches-proof.sh --work ${UC2_SCRIPTS_OUT}/proof --shipped ${_shipped}
            --assets ${_assets} ${_proof_retail} --ucc $<TARGET_FILE:UCC> --capped ${_capped}
    DEPENDS UCC
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL VERBATIM)
endif()
