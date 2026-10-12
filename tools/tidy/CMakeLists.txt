# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# reloco-tidy: a clang-tidy plugin enforcing reloco's safe-coding rules.
# Build standalone:   cmake -S tools/tidy -B build-tidy -G Ninja && ninja -C build-tidy
# Use:                clang-tidy-N --load=build-tidy/libreloco-tidy.so --checks='-*,reloco-*' file.cpp

cmake_minimum_required(VERSION 3.20)
project(reloco_tidy LANGUAGES CXX)

set(RELOCO_TIDY_LLVM_VERSION "" CACHE STRING
    "LLVM major version to build against (empty = newest installed with clang-tidy development headers)")
set(RELOCO_TIDY_LLVM_ROOT "" CACHE PATH "Explicit LLVM install prefix (overrides version search)")

# Newest first; the plugin must match the clang-tidy binary that loads it, so
# the matching clang-tidy-N is reported to the user.
set(_reloco_tidy_candidates ${RELOCO_TIDY_LLVM_VERSION} 30 29 28 27 26 25 24 23 22 21 20 19 18)
set(_reloco_tidy_root "${RELOCO_TIDY_LLVM_ROOT}")
if(NOT _reloco_tidy_root)
  foreach(_v IN LISTS _reloco_tidy_candidates)
    set(_p "/usr/lib/llvm-${_v}")
    if(EXISTS "${_p}/lib/cmake/clang/ClangConfig.cmake" AND EXISTS "${_p}/include/clang-tidy/ClangTidy.h")
      set(_reloco_tidy_root "${_p}")
      set(RELOCO_TIDY_LLVM_VERSION "${_v}")
      break()
    endif()
  endforeach()
endif()
if(NOT _reloco_tidy_root)
  message(FATAL_ERROR "reloco-tidy: no LLVM with clang-tidy development headers found "
                      "(install libclang-N-dev / llvm-N-dev, or set RELOCO_TIDY_LLVM_ROOT)")
endif()

find_package(LLVM REQUIRED CONFIG PATHS "${_reloco_tidy_root}/lib/cmake/llvm" NO_DEFAULT_PATH)
find_package(Clang REQUIRED CONFIG PATHS "${_reloco_tidy_root}/lib/cmake/clang" NO_DEFAULT_PATH)
message(STATUS "reloco-tidy: LLVM ${LLVM_PACKAGE_VERSION} at ${_reloco_tidy_root}")

add_library(reloco-tidy MODULE
    src/RelocoTidyModule.cpp
    src/RelocoCheckBase.cpp
    src/RefQualifiedMemberCheck.cpp
    src/NoRawPointerCheck.cpp
    src/NoCArrayCheck.cpp
    src/NoexceptCheck.cpp
    src/EasyChecks.cpp
    src/UseAfterMoveCheck.cpp
    src/SendSyncCheck.cpp
    src/NarrowingFlowCheck.cpp
    src/LockGuardCheck.cpp
    src/LeakCheck.cpp
    src/IteratorInvalidationCheck.cpp
    src/OutParamCheck.cpp
    src/UninitLocalCheck.cpp
    src/ImplicitCopyCheck.cpp
    src/LifetimeAnnotationsCheck.cpp
    src/PointerAnnotationCheck.cpp
    src/BannedStdTypeCheck.cpp
    src/BannedConstructCheck.cpp
    src/BorrowCheck.cpp)
set_target_properties(reloco-tidy PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
    PREFIX "lib" OUTPUT_NAME "reloco-tidy")
target_include_directories(reloco-tidy SYSTEM PRIVATE "${LLVM_INCLUDE_DIRS}" "${CLANG_INCLUDE_DIRS}"
                           "${_reloco_tidy_root}/include")
target_include_directories(reloco-tidy PRIVATE src)
target_compile_options(reloco-tidy PRIVATE -fno-rtti -Wall -Wextra)
# The clang-tidy executable already provides the clangTidy*, clangAST and LLVM symbols, so the
# module deliberately leaves them undefined and binds to the loading process (the same ABI the
# plugin was built against; hence the version match with the clang-tidy-N binary).
target_compile_definitions(reloco-tidy PRIVATE RELOCO_TIDY_LLVM_MAJOR=${LLVM_VERSION_MAJOR})

# Fixture tests: tools/tidy/test/*.cpp carry `// expect: <check> <line-offset>` markers.
enable_testing()
find_program(_reloco_tidy_bin NAMES clang-tidy-${RELOCO_TIDY_LLVM_VERSION} clang-tidy
             PATHS "${_reloco_tidy_root}/bin" NO_DEFAULT_PATH)
find_package(Python3 COMPONENTS Interpreter)
if(_reloco_tidy_bin AND Python3_FOUND)
  add_test(NAME reloco_tidy_fixtures
           COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/run_tests.py"
                   --clang-tidy "${_reloco_tidy_bin}" --plugin "$<TARGET_FILE:reloco-tidy>"
                   --include "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
endif()
