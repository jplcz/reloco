# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# CMake helpers for reloco-tidy.
#
#   include(<reloco>/tools/tidy/RelocoTidy.cmake)
#   reloco_tidy_attach(my_target)                        # run during every build of my_target
#   reloco_tidy_add_target(my_lint TARGETS a b)          # separate `ninja my_lint` target
#
# The plugin must be built first (add_subdirectory(<reloco>/tools/tidy) or the JPLCZ_RELOCO_BUILD_TIDY
# option of reloco itself) and clang-tidy must be the same LLVM major version as the plugin.

set(RELOCO_TIDY_PLUGIN "" CACHE FILEPATH "Path to libreloco-tidy.so (default: the reloco-tidy target)")
set(RELOCO_TIDY_EXECUTABLE "" CACHE FILEPATH "clang-tidy matching the plugin's LLVM major version")
set(RELOCO_TIDY_PROFILE "strict" CACHE STRING "Profile from tools/tidy/profiles: strict or library")

set(_reloco_tidy_dir "${CMAKE_CURRENT_LIST_DIR}")

function(_reloco_tidy_command out)
  if(RELOCO_TIDY_PLUGIN)
    set(_plugin "${RELOCO_TIDY_PLUGIN}")
  elseif(TARGET reloco-tidy)
    set(_plugin "$<TARGET_FILE:reloco-tidy>")
  else()
    message(FATAL_ERROR "reloco-tidy: set RELOCO_TIDY_PLUGIN or add_subdirectory(tools/tidy) first")
  endif()
  if(RELOCO_TIDY_EXECUTABLE)
    set(_bin "${RELOCO_TIDY_EXECUTABLE}")
  elseif(RELOCO_TIDY_DEFAULT_EXECUTABLE)
    set(_bin "${RELOCO_TIDY_DEFAULT_EXECUTABLE}")
  else()
    find_program(_bin NAMES clang-tidy-${RELOCO_TIDY_LLVM_VERSION} clang-tidy REQUIRED)
  endif()
  set(_cfg "${_reloco_tidy_dir}/profiles/${RELOCO_TIDY_PROFILE}.clang-tidy")
  if(NOT EXISTS "${_cfg}")
    message(FATAL_ERROR "reloco-tidy: unknown profile '${RELOCO_TIDY_PROFILE}'")
  endif()
  # --config-file wins over any .clang-tidy found next to the sources.
  set(${out} "${_bin}" "--load=${_plugin}" "--config-file=${_cfg}" PARENT_SCOPE)
endfunction()

# Runs reloco-tidy as part of compiling TARGET (CMake's CXX_CLANG_TIDY hook).
function(reloco_tidy_attach target)
  _reloco_tidy_command(_cmd)
  set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${_cmd}")
endfunction()

# Adds a standalone target NAME that checks the sources of every target in TARGETS (and
# any extra FILES, compiled with EXTRA_ARGS) using compile_commands.json, without slowing the normal build.
function(reloco_tidy_add_target name)
  cmake_parse_arguments(A "" "" "TARGETS;FILES;EXTRA_ARGS" ${ARGN})
  _reloco_tidy_command(_cmd)
  set(_files ${A_FILES})
  foreach(t IN LISTS A_TARGETS)
    get_target_property(_src ${t} SOURCES)
    get_target_property(_dir ${t} SOURCE_DIR)
    foreach(s IN LISTS _src)
      cmake_path(ABSOLUTE_PATH s BASE_DIRECTORY "${_dir}" OUTPUT_VARIABLE _abs)
      list(APPEND _files "${_abs}")
    endforeach()
  endforeach()
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON PARENT_SCOPE)
  # Compiler flags for FILES that no target compiles (they have no compile_commands.json entry).
  set(_extra "")
  foreach(a IN LISTS A_EXTRA_ARGS)
    list(APPEND _extra "--extra-arg=${a}")
  endforeach()
  add_custom_target(${name}
      COMMAND ${_cmd} -p "${CMAKE_BINARY_DIR}" --quiet ${_extra} ${_files}
      COMMENT "reloco-tidy (${RELOCO_TIDY_PROFILE} profile)"
      VERBATIM)
  if(TARGET reloco-tidy)
    add_dependencies(${name} reloco-tidy)
  endif()
endfunction()
