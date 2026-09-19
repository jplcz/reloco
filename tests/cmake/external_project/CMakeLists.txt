# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_reloco_external_project_consumer LANGUAGES CXX)

include(ExternalProject)

if(NOT JPLCZ_RELOCO_SOURCE_DIR)
    message(FATAL_ERROR "JPLCZ_RELOCO_SOURCE_DIR is required")
endif()

ExternalProject_Add(
    jplcz_reloco_external
    SOURCE_DIR "${JPLCZ_RELOCO_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/jplcz_reloco-build"
    INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/prefix"
    CMAKE_ARGS
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        "-DJPLCZ_RELOCO_INSTALL=ON"
        "-DJPLCZ_RELOCO_BUILD_TESTS=OFF"
        "-DJPLCZ_RELOCO_BUILD_HEADER_CHECKS=OFF"
)
