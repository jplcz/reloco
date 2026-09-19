# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_reloco_cpm_consumer LANGUAGES CXX)

if(NOT DEFINED CPM_CMAKE_PATH)
    message(FATAL_ERROR "CPM_CMAKE_PATH must identify CPM.cmake")
endif()
if(NOT DEFINED JPLCZ_RELOCO_SOURCE_DIR)
    message(FATAL_ERROR
        "JPLCZ_RELOCO_SOURCE_DIR must identify the project checkout")
endif()

include("${CPM_CMAKE_PATH}")
CPMAddPackage(
    NAME jplcz_reloco
    SOURCE_DIR "${JPLCZ_RELOCO_SOURCE_DIR}"
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "JPLCZ_RELOCO_BUILD_TESTS OFF"
        "JPLCZ_RELOCO_BUILD_HEADER_CHECKS OFF"
        "JPLCZ_RELOCO_INSTALL OFF"
)

add_executable(cpm_consumer main.cpp)
target_link_libraries(cpm_consumer PRIVATE jplcz_reloco::reloco)
