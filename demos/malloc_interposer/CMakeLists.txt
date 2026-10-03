# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# Deliberately a standalone CMake project, NOT an add_subdirectory() of the
# main reloco build: it builds a glibc malloc/free/calloc/realloc
# *interposer* shared object (see interposer.cpp for the full rationale),
# which is a fundamentally different kind of artifact (an LD_PRELOAD-able
# .so with C-linkage symbols deliberately colliding with libc's own) than
# anything else in demos/, and is not meant to ever be linked into, or
# built as part of, reloco's own test/demo suite. Configure and build it on
# its own, pointed at reloco's header-only include/ directory:
#
#   cmake -S demos/malloc_interposer -B demos/malloc_interposer/build
#   cmake --build demos/malloc_interposer/build
#
# reloco is header-only, so nothing besides -I is needed; RELOCO_INCLUDE_DIR
# defaults to ../../include (i.e. this works out of the box from inside a
# reloco checkout) but can be pointed at any other copy of reloco's
# include/ directory, so this directory can be copied out of the reloco
# tree and built independently.
cmake_minimum_required(VERSION 3.16)
project(reloco_malloc_interposer LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(RELOCO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../include" CACHE PATH
    "Path to reloco's include/ directory (the one directly containing reloco/*.hpp)")

if(NOT EXISTS "${RELOCO_INCLUDE_DIR}/reloco/malloc_allocator.hpp")
    message(FATAL_ERROR
        "RELOCO_INCLUDE_DIR (${RELOCO_INCLUDE_DIR}) does not contain "
        "reloco/malloc_allocator.hpp -- point it at reloco's include/ directory.")
endif()

find_package(Threads REQUIRED)

add_library(reloco_malloc_interposer SHARED interposer.cpp)
set_target_properties(reloco_malloc_interposer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "reloco_malloc_interposer"
)
target_include_directories(reloco_malloc_interposer PRIVATE "${RELOCO_INCLUDE_DIR}")
target_link_libraries(reloco_malloc_interposer PRIVATE Threads::Threads)
target_compile_options(reloco_malloc_interposer PRIVATE -Wall -Wextra)
