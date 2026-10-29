# UC2Win32Resources.cmake — the Win32 resources of the Windows programs: the dialogs, menus, bitmaps, icons and cursors
# of the editor's packages, and the application manifest.
#
# uc2_win32_resources(<target> <file.rc>): compiles the target's resource script to a .res with windres.
# uc2_link_win32_resources(<executable> <target>...): merges the .res of those targets into one object for the
#   executable (tools/merge-res.py: each script was written for its own DLL, so their numbers clash in one module)
#   and links it.
# UC2Manifest: the application manifest of the Windows programs (win32/uc2.manifest: the process's DPI awareness and
#   execution level), a target that uc2_link_win32_resources takes like a package's resources.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(uc2_win32_resources target rc)
  get_filename_component(_dir ${rc} DIRECTORY)
  get_filename_component(_name ${rc} NAME_WE)
  set(_res ${CMAKE_CURRENT_BINARY_DIR}/${_name}.res)
  # The bitmaps, icons, cursors and manifests the script names, and its headers.
  file(GLOB _deps CONFIGURE_DEPENDS ${_dir}/*.h ${_dir}/*.[bB][mM][pP] ${_dir}/*.[iI][cC][oO] ${_dir}/*.[cC][uU][rR]
       ${_dir}/*.manifest)
  # -I: windres looks for the files the script names in the include directories too ("../Res/Unreal.ico" is
  # relative to the script's folder).
  add_custom_command(OUTPUT ${_res}
    COMMAND ${CMAKE_RC_COMPILER} -I${_dir} -O res -i ${rc} -o ${_res}
    DEPENDS ${rc} ${_deps}
    COMMENT "Compiling the resources of ${target} (${_name}.rc)"
    VERBATIM)
  add_custom_target(${target}_res DEPENDS ${_res})
  add_dependencies(${target} ${target}_res)
  set_property(TARGET ${target} PROPERTY UC2_WIN32_RES ${_res})
endfunction()

function(uc2_link_win32_resources exe)
  set(_inputs "")
  set(_targets "")
  foreach(pkg IN LISTS ARGN)
    get_target_property(_res ${pkg} UC2_WIN32_RES)
    if(NOT _res)
      message(FATAL_ERROR "uc2_link_win32_resources: ${pkg} has no resources (uc2_win32_resources)")
    endif()
    list(APPEND _inputs ${_res})
    list(APPEND _targets ${pkg}_res)
  endforeach()
  set(_merged ${CMAKE_CURRENT_BINARY_DIR}/${exe}Resources.res)
  set(_obj ${CMAKE_CURRENT_BINARY_DIR}/${exe}Resources.o)
  add_custom_command(OUTPUT ${_obj}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/merge-res.py -o ${_merged} ${_inputs}
    COMMAND ${CMAKE_RC_COMPILER} -J res -O coff -i ${_merged} -o ${_obj}
    DEPENDS ${_inputs} ${_targets} ${CMAKE_SOURCE_DIR}/tools/merge-res.py
    COMMENT "Merging the resources of ${ARGN} into ${exe}"
    VERBATIM)
  target_sources(${exe} PRIVATE ${_obj})
endfunction()

add_custom_target(UC2Manifest)
uc2_win32_resources(UC2Manifest ${CMAKE_CURRENT_LIST_DIR}/win32/Manifest.rc)
