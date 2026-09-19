# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_reloco_add_subdirectory_consumer LANGUAGES CXX)

if(NOT JPLCZ_RELOCO_SOURCE_DIR)
    message(FATAL_ERROR "JPLCZ_RELOCO_SOURCE_DIR is required")
endif()

set(CMAKE_CXX_STANDARD 23)
add_subdirectory("${JPLCZ_RELOCO_SOURCE_DIR}" jplcz_reloco)

if(NOT CMAKE_CXX_STANDARD EQUAL 23)
    message(FATAL_ERROR "jplcz_reloco changed the parent C++ standard")
endif()

foreach(DEVELOPMENT_OPTION
        JPLCZ_RELOCO_BUILD_TESTS
        JPLCZ_RELOCO_BUILD_HEADER_CHECKS
        JPLCZ_RELOCO_ENABLE_STRICT_WARNINGS
        JPLCZ_RELOCO_INSTALL)
    if(${DEVELOPMENT_OPTION})
        message(FATAL_ERROR
            "Unexpected embedded-build option: ${DEVELOPMENT_OPTION}")
    endif()
endforeach()

foreach(DEVELOPMENT_TARGET
        jplcz_reloco_tests
        jplcz_reloco_check_public_headers)
    if(TARGET "${DEVELOPMENT_TARGET}")
        message(FATAL_ERROR
            "Unexpected embedded-build target: ${DEVELOPMENT_TARGET}")
    endif()
endforeach()

add_executable(add_subdirectory_consumer main.cpp)
target_link_libraries(add_subdirectory_consumer
    PRIVATE jplcz_reloco::reloco)
