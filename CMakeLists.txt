# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_reloco VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(JPLCZ_RELOCO_IS_TOP_LEVEL ON)
else()
    set(JPLCZ_RELOCO_IS_TOP_LEVEL OFF)
endif()

set(JPLCZ_RELOCO_DEVELOPMENT_DEFAULT ${JPLCZ_RELOCO_IS_TOP_LEVEL})

option(JPLCZ_RELOCO_ENABLE_STRICT_WARNINGS
    "Enable comprehensive GCC and Clang warnings" ${JPLCZ_RELOCO_DEVELOPMENT_DEFAULT})
option(JPLCZ_RELOCO_ENABLE_UNSAFE_BUFFER_USAGE
    "Enable Clang unsafe buffer usage diagnostics" ${JPLCZ_RELOCO_DEVELOPMENT_DEFAULT})
option(JPLCZ_RELOCO_ENABLE_WERROR
    "Enable build with -Werror" OFF)
option(JPLCZ_RELOCO_BUILD_TESTS
    "Build test suite" ${JPLCZ_RELOCO_DEVELOPMENT_DEFAULT})
option(JPLCZ_RELOCO_BUILD_HEADER_CHECKS
    "Compile all public headers under each supported C++ standard"
    ${JPLCZ_RELOCO_DEVELOPMENT_DEFAULT})
option(JPLCZ_RELOCO_BUILD_DEMOS
    "Build demos/ (small standalone example programs)" ${JPLCZ_RELOCO_DEVELOPMENT_DEFAULT})
option(JPLCZ_RELOCO_INSTALL
    "Install jplcz_reloco headers and CMake package metadata"
    ${JPLCZ_RELOCO_IS_TOP_LEVEL})
set(JPLCZ_RELOCO_PORTING_HEADERS "" CACHE PATH
    "Directory containing target-specific override headers (any of \
mutex.hpp/thread.hpp/spin_lock.hpp/tls_provider.hpp/futex.hpp) copied \
into include/reloco/detail/porting/ and installed alongside reloco's own \
headers, plus the matching RELOCO_*_BACKEND_CUSTOM/RELOCO_TLS_MODEL macro \
defined automatically for consumers of the jplcz_reloco target. See \
include/reloco/detail/porting/*.template.hpp and include/reloco/detail/\
porting/README.md for the exact shape each file must have.")

if(JPLCZ_RELOCO_ENABLE_STRICT_WARNINGS OR JPLCZ_RELOCO_ENABLE_UNSAFE_BUFFER_USAGE OR
   JPLCZ_RELOCO_BUILD_HEADER_CHECKS OR JPLCZ_RELOCO_BUILD_TESTS)
    include(CheckCXXCompilerFlag)

    if(MSVC)
        check_cxx_compiler_flag("/std:c++latest"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_CXX23)
    else()
        check_cxx_compiler_flag("-std=c++23"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_CXX23)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        check_cxx_compiler_flag("-Wdangling"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_WDANGLING)
        check_cxx_compiler_flag("-Wdangling-gsl"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_WDANGLING_GSL)
        check_cxx_compiler_flag("-Wdangling-assignment-gsl"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_WDANGLING_ASSIGNMENT_GSL)
        check_cxx_compiler_flag("-Wdangling-field"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_WDANGLING_FIELD)
        check_cxx_compiler_flag("-Wreturn-stack-address"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_WRETURN_STACK_ADDRESS)
        check_cxx_compiler_flag("-Wunsafe-buffer-usage"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_WUNSAFE_BUFFER_USAGE)
        check_cxx_compiler_flag("-Wconsumed"
            JPLCZ_RELOCO_COMPILER_SUPPORTS_WCONSUMED)
    endif()
endif()

# Header-only interface target
add_library(jplcz_reloco INTERFACE)
target_compile_features(jplcz_reloco INTERFACE cxx_std_17)
target_compile_options(jplcz_reloco INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
)
set_target_properties(jplcz_reloco PROPERTIES EXPORT_NAME reloco)

target_include_directories(jplcz_reloco INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_library(jplcz_reloco::reloco ALIAS jplcz_reloco)

# JPLCZ_RELOCO_PORTING_HEADERS: copy whichever of mutex.hpp/thread.hpp/
# spin_lock.hpp/tls_provider.hpp/futex.hpp exist in the given directory
# into a staging tree (never into reloco's own source include/ tree, so
# reloco's own headers are never touched/replaced) and define the
# matching RELOCO_*_BACKEND_CUSTOM/RELOCO_TLS_MODEL macro on
# jplcz_reloco's INTERFACE so every consumer (including a downstream
# find_package(jplcz_reloco) consumer, via the exported target's own
# INTERFACE properties) picks it up automatically.
# See include/reloco/detail/porting/README.md for the full mechanism.
#
# The build-tree staging layout is "porting_include/detail/porting/*.hpp"
# (no "reloco/" prefix) added as its own extra -I directory: each reloco
# header's own #include "detail/porting/<name>.hpp" is a quoted include,
# so the compiler first tries it relative to that header's own directory
# (include/reloco/), finds nothing there (only the *.template.hpp
# scaffolds live in the real source tree), then falls back to treating it
# like #include <detail/porting/<name>.hpp> against every -I directory --
# which resolves against this staging directory's root, without a
# "reloco/" prefix to account for. The installed layout is different (see
# the JPLCZ_RELOCO_INSTALL block below): there, the same quoted include
# resolves directly against <prefix>/include/reloco/detail/porting/
# (install-tree-relative to reloco/mutex.hpp itself), so the installed
# copy is placed at that "reloco/"-prefixed path instead, matching the
# *.template.hpp scaffolds already installed there.
if(JPLCZ_RELOCO_PORTING_HEADERS)
    set(JPLCZ_RELOCO_PORTING_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/porting_include")
    set(JPLCZ_RELOCO_PORTING_DEST_DIR "${JPLCZ_RELOCO_PORTING_STAGING_DIR}/detail/porting")
    file(MAKE_DIRECTORY "${JPLCZ_RELOCO_PORTING_DEST_DIR}")

    set(JPLCZ_RELOCO_PORTING_HEADER_NAMES mutex.hpp thread.hpp spin_lock.hpp tls_provider.hpp futex.hpp)
    set(JPLCZ_RELOCO_PORTING_HEADER_DEFINES
        "RELOCO_MUTEX_BACKEND_CUSTOM"
        "RELOCO_THREAD_BACKEND_CUSTOM"
        "RELOCO_SPIN_LOCK_BACKEND_CUSTOM"
        "RELOCO_TLS_MODEL=RELOCO_TLS_MODEL_OS"
        "RELOCO_FUTEX_BACKEND_CUSTOM"
    )

    list(LENGTH JPLCZ_RELOCO_PORTING_HEADER_NAMES JPLCZ_RELOCO_PORTING_HEADER_COUNT)
    math(EXPR JPLCZ_RELOCO_PORTING_HEADER_LAST "${JPLCZ_RELOCO_PORTING_HEADER_COUNT} - 1")

    set(JPLCZ_RELOCO_PORTING_ACTIVE_DEFINES "")
    foreach(JPLCZ_RELOCO_PORTING_I RANGE ${JPLCZ_RELOCO_PORTING_HEADER_LAST})
        list(GET JPLCZ_RELOCO_PORTING_HEADER_NAMES ${JPLCZ_RELOCO_PORTING_I} JPLCZ_RELOCO_PORTING_HEADER_NAME)
        list(GET JPLCZ_RELOCO_PORTING_HEADER_DEFINES ${JPLCZ_RELOCO_PORTING_I} JPLCZ_RELOCO_PORTING_HEADER_DEFINE)
        set(JPLCZ_RELOCO_PORTING_SRC "${JPLCZ_RELOCO_PORTING_HEADERS}/${JPLCZ_RELOCO_PORTING_HEADER_NAME}")
        if(EXISTS "${JPLCZ_RELOCO_PORTING_SRC}")
            configure_file(
                "${JPLCZ_RELOCO_PORTING_SRC}"
                "${JPLCZ_RELOCO_PORTING_DEST_DIR}/${JPLCZ_RELOCO_PORTING_HEADER_NAME}"
                COPYONLY
            )
            list(APPEND JPLCZ_RELOCO_PORTING_ACTIVE_DEFINES "${JPLCZ_RELOCO_PORTING_HEADER_DEFINE}")
            message(STATUS
                "jplcz_reloco: using custom porting header '${JPLCZ_RELOCO_PORTING_HEADER_NAME}' "
                "from ${JPLCZ_RELOCO_PORTING_SRC} (defining ${JPLCZ_RELOCO_PORTING_HEADER_DEFINE})")
        endif()
    endforeach()

    if(JPLCZ_RELOCO_PORTING_ACTIVE_DEFINES)
        target_compile_definitions(jplcz_reloco INTERFACE ${JPLCZ_RELOCO_PORTING_ACTIVE_DEFINES})
    endif()

    # reloco's own source tree never ships a same-named, non-`.template.hpp`
    # file under detail/porting/ (only *.template.hpp scaffolds do), so
    # there is no real file to collide with here regardless of search
    # order -- this include directory only ever adds files, never shadows
    # one already found via the plain source include/ tree above.
    target_include_directories(jplcz_reloco INTERFACE
        $<BUILD_INTERFACE:${JPLCZ_RELOCO_PORTING_STAGING_DIR}>
    )
endif()

# reloco is header-only and primarily targets embedded/single-binary
# consumers, so these are plain INTERFACE forwarders for the
# RELOCO_SHARED/RELOCO_SHARED_BUILD compile definitions (see
# detail/compat.hpp and docs/shared-library.md) -- they do not build or
# export an actual shared object themselves. A caller that wants a real
# `.so`/`.dll` remains responsible for its own build target (typically a
# single translation unit including reloco_compile.hpp) linked against
# jplcz_reloco::reloco-shared-export, and every ordinary consumer of that
# library links against jplcz_reloco::reloco-shared instead.
add_library(jplcz_reloco_shared INTERFACE)
target_link_libraries(jplcz_reloco_shared INTERFACE jplcz_reloco)
target_compile_definitions(jplcz_reloco_shared INTERFACE RELOCO_SHARED)
set_target_properties(jplcz_reloco_shared PROPERTIES EXPORT_NAME reloco-shared)

add_library(jplcz_reloco::reloco-shared ALIAS jplcz_reloco_shared)

add_library(jplcz_reloco_shared_export INTERFACE)
target_link_libraries(jplcz_reloco_shared_export INTERFACE jplcz_reloco_shared)
target_compile_definitions(jplcz_reloco_shared_export INTERFACE RELOCO_SHARED_BUILD)
set_target_properties(jplcz_reloco_shared_export PROPERTIES EXPORT_NAME reloco-shared-export)

add_library(jplcz_reloco::reloco-shared-export ALIAS jplcz_reloco_shared_export)

add_library(jplcz_reloco_warnings INTERFACE)
if(JPLCZ_RELOCO_ENABLE_UNSAFE_BUFFER_USAGE AND
   NOT JPLCZ_RELOCO_COMPILER_SUPPORTS_WUNSAFE_BUFFER_USAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "JPLCZ_RELOCO_ENABLE_UNSAFE_BUFFER_USAGE requires a compiler that supports "
            "-Wunsafe-buffer-usage")
    else()
        # Enabled by default only for Clang; silently skip on other compilers
        # that lack -Wunsafe-buffer-usage instead of failing the build.
        set(JPLCZ_RELOCO_ENABLE_UNSAFE_BUFFER_USAGE OFF)
    endif()
endif()

if(JPLCZ_RELOCO_ENABLE_STRICT_WARNINGS)
    target_compile_options(jplcz_reloco_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU>:
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wcast-align
            -Wcast-qual
            -Wundef
            -Wnull-dereference
            -Wdouble-promotion
            -Wimplicit-fallthrough
            -Woverloaded-virtual
            -Wnon-virtual-dtor
            -Wduplicated-cond
            -Wlogical-op
            -Wno-psabi
            $<$<BOOL:${JPLCZ_RELOCO_ENABLE_WERROR}>:-Werror>
        >
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wcast-align
            -Wcast-qual
            -Wundef
            -Wnull-dereference
            -Wdouble-promotion
            -Wimplicit-fallthrough
            -Woverloaded-virtual
            -Wnon-virtual-dtor
            -Wextra-semi
            -Wimplicit-int-conversion
            -Wshorten-64-to-32
            $<$<BOOL:${JPLCZ_RELOCO_COMPILER_SUPPORTS_WDANGLING}>:-Wdangling>
            $<$<BOOL:${JPLCZ_RELOCO_COMPILER_SUPPORTS_WDANGLING_GSL}>:-Wdangling-gsl>
            $<$<BOOL:${JPLCZ_RELOCO_COMPILER_SUPPORTS_WDANGLING_ASSIGNMENT_GSL}>:-Wdangling-assignment-gsl>
            $<$<BOOL:${JPLCZ_RELOCO_COMPILER_SUPPORTS_WDANGLING_FIELD}>:-Wdangling-field>
            $<$<BOOL:${JPLCZ_RELOCO_COMPILER_SUPPORTS_WRETURN_STACK_ADDRESS}>:-Wreturn-stack-address>
            $<$<BOOL:${JPLCZ_RELOCO_COMPILER_SUPPORTS_WCONSUMED}>:-Wconsumed>
            $<$<BOOL:${JPLCZ_RELOCO_ENABLE_WERROR}>:-Werror>
        >
    )
endif()

if(JPLCZ_RELOCO_ENABLE_UNSAFE_BUFFER_USAGE)
    target_compile_options(jplcz_reloco_warnings INTERFACE
        -Wunsafe-buffer-usage
        $<$<NOT:$<BOOL:${JPLCZ_RELOCO_ENABLE_WERROR}>>:
            -Wno-error=unsafe-buffer-usage
        >
    )
endif()

function(jplcz_reloco_enable_warnings TARGET)
    target_link_libraries(${TARGET} PRIVATE jplcz_reloco_warnings)
endfunction()

if(JPLCZ_RELOCO_BUILD_HEADER_CHECKS)
    function(add_jplcz_reloco_header_check STANDARD)
        add_library(jplcz_reloco_headers_cxx${STANDARD} OBJECT
            tests/compile_all_headers.cpp)
        target_link_libraries(jplcz_reloco_headers_cxx${STANDARD} PRIVATE jplcz_reloco)
        jplcz_reloco_enable_warnings(jplcz_reloco_headers_cxx${STANDARD})
        set_target_properties(jplcz_reloco_headers_cxx${STANDARD} PROPERTIES
            CXX_STANDARD ${STANDARD}
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
    endfunction()

    add_jplcz_reloco_header_check(17)
    add_jplcz_reloco_header_check(20)

    set(JPLCZ_RELOCO_HEADER_CHECK_TARGETS
        jplcz_reloco_headers_cxx17
        jplcz_reloco_headers_cxx20
    )
    if(JPLCZ_RELOCO_COMPILER_SUPPORTS_CXX23)
        add_jplcz_reloco_header_check(23)
        list(APPEND JPLCZ_RELOCO_HEADER_CHECK_TARGETS jplcz_reloco_headers_cxx23)
    endif()

    add_custom_target(jplcz_reloco_check_public_headers
        DEPENDS ${JPLCZ_RELOCO_HEADER_CHECK_TARGETS}
    )
endif()

if(JPLCZ_RELOCO_BUILD_TESTS)
    include(CheckIncludeFileCXX)
    check_include_file_cxx("pthread.h" JPLCZ_RELOCO_HAVE_PTHREAD_H)
    if(JPLCZ_RELOCO_HAVE_PTHREAD_H)
        find_package(Threads QUIET)
    endif()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.zip
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)

    add_executable(jplcz_reloco_tests
        tests/test_atomic_ops.cpp
        tests/test_array.cpp
        tests/test_span.cpp
        tests/test_string_view.cpp
        tests/test_expected.cpp
        tests/test_error_std.cpp
        tests/test_allocator.cpp
        tests/test_concepts.cpp
        tests/test_construction_helpers.cpp
        tests/test_unique_ptr.cpp
        tests/test_string.cpp
        tests/test_relocatable.cpp
        tests/test_relocatable_std.cpp
        tests/test_send_sync.cpp
        tests/test_thread.cpp
        tests/test_channel.cpp
        tests/test_once_lock.cpp
        tests/test_once.cpp
        tests/test_lazy_lock.cpp
        tests/test_hint.cpp
        tests/test_park.cpp
        tests/test_scope.cpp
        tests/test_fallible_singleton.cpp
        tests/test_duration.cpp
        tests/test_instant.cpp
        tests/test_futex.cpp
        tests/test_tls_provider.cpp
        tests/test_mutex.cpp
        tests/test_guarded_mutex.cpp
        tests/test_rw_lock.cpp
        tests/test_barrier.cpp
        tests/test_wait_group.cpp
        tests/test_spin_lock.cpp
        tests/test_shared_ptr.cpp
        tests/test_rc.cpp
        tests/test_bytes.cpp
        tests/test_binary_heap.cpp
        tests/test_boxed_slice.cpp
        tests/test_cow.cpp
        tests/test_function.cpp
        tests/test_any.cpp
        tests/test_type_id.cpp
        tests/test_collection_view.cpp
        tests/test_container_ref.cpp
        tests/test_vector.cpp
        tests/test_inline_vector.cpp
        tests/test_sso_vector.cpp
        tests/test_outline_vector.cpp
        tests/test_type_metadata.cpp
        tests/test_type_operations.cpp
        tests/test_node_base.cpp
        tests/test_tree_base.cpp
        tests/test_tree_set.cpp
        tests/test_tree_map.cpp
        tests/test_flat_hash_base.cpp
        tests/test_flat_hash_set.cpp
        tests/test_flat_hash_map.cpp
        tests/test_alignment.cpp
        tests/test_flat_set.cpp
        tests/test_flat_map.cpp
        tests/test_inline_flat_set.cpp
        tests/test_inline_flat_map.cpp
        tests/test_sso_flat_set.cpp
        tests/test_sso_flat_map.cpp
        tests/test_stack_allocator.cpp
        tests/test_inline_string.cpp
        tests/test_sso_string.cpp
        tests/test_optional.cpp
        tests/test_variant.cpp
        tests/test_inplace_function.cpp
        tests/test_function_ref.cpp
        tests/test_cell.cpp
        tests/test_non_zero.cpp
        tests/test_wrapping.cpp
        tests/test_saturating.cpp
        tests/test_checked.cpp
        tests/test_int_ops.cpp
        tests/test_vec_deque.cpp
        tests/test_inline_vec_deque.cpp
        tests/test_sso_vec_deque.cpp
        tests/test_outline_vec_deque.cpp
        tests/test_ring_buffer.cpp
        tests/test_atomic_ring_buffer.cpp
    )

    target_link_libraries(jplcz_reloco_tests PRIVATE jplcz_reloco GTest::gtest_main)
    jplcz_reloco_enable_warnings(jplcz_reloco_tests)
    gtest_discover_tests(jplcz_reloco_tests)

    # checked_value/value_ref exercise Clang's -Wconsumed typestate tracking;
    # keep them as their own targets so a probed -Wconsumed flag only ever
    # applies to the translation units it is actually verifying.
    add_executable(jplcz_reloco_value_ref_tests tests/test_value_ref.cpp)
    target_link_libraries(jplcz_reloco_value_ref_tests PRIVATE
        jplcz_reloco GTest::gtest_main)
    jplcz_reloco_enable_warnings(jplcz_reloco_value_ref_tests)
    gtest_discover_tests(jplcz_reloco_value_ref_tests
        TEST_PREFIX "ValueRef.")

    add_executable(jplcz_reloco_checked_value_tests tests/test_checked_value.cpp)
    target_link_libraries(jplcz_reloco_checked_value_tests PRIVATE
        jplcz_reloco GTest::gtest_main)
    jplcz_reloco_enable_warnings(jplcz_reloco_checked_value_tests)
    gtest_discover_tests(jplcz_reloco_checked_value_tests
        TEST_PREFIX "CheckedValue.")

    # RELOCO_IMPLICIT_TYPEID is defined only for this translation unit (see
    # that file), so it never affects type_id::name() anywhere else in the
    # suite.
    add_executable(jplcz_reloco_implicit_typeid_tests tests/test_type_id_implicit_typeid.cpp)
    target_link_libraries(jplcz_reloco_implicit_typeid_tests PRIVATE
        jplcz_reloco GTest::gtest_main)
    jplcz_reloco_enable_warnings(jplcz_reloco_implicit_typeid_tests)
    gtest_discover_tests(jplcz_reloco_implicit_typeid_tests
        TEST_PREFIX "ImplicitTypeId.")

    # tests/test_tls_provider_models.cpp is compiled once per
    # RELOCO_TLS_MODEL, each forced via its own -DRELOCO_TLS_MODEL=...
    # define, so every tls_provider<T, Tag> backend is actually exercised
    # rather than just whichever one the host/default happens to select.
    # The test file itself names its gtest suite after RELOCO_TLS_MODEL
    # (TlsProviderModelTest_ThreadLocal/_Single/_Pthread), so no
    # TEST_PREFIX is needed here to keep the three binaries' cases apart.
    add_executable(jplcz_reloco_tls_thread_local_tests tests/test_tls_provider_models.cpp)
    target_link_libraries(jplcz_reloco_tls_thread_local_tests PRIVATE
        jplcz_reloco GTest::gtest_main)
    target_compile_definitions(jplcz_reloco_tls_thread_local_tests PRIVATE
        RELOCO_TLS_MODEL=RELOCO_TLS_MODEL_THREAD_LOCAL)
    jplcz_reloco_enable_warnings(jplcz_reloco_tls_thread_local_tests)
    gtest_discover_tests(jplcz_reloco_tls_thread_local_tests)

    add_executable(jplcz_reloco_tls_single_tests tests/test_tls_provider_models.cpp)
    target_link_libraries(jplcz_reloco_tls_single_tests PRIVATE
        jplcz_reloco GTest::gtest_main)
    target_compile_definitions(jplcz_reloco_tls_single_tests PRIVATE
        RELOCO_TLS_MODEL=RELOCO_TLS_MODEL_SINGLE)
    jplcz_reloco_enable_warnings(jplcz_reloco_tls_single_tests)
    gtest_discover_tests(jplcz_reloco_tls_single_tests)

    # RELOCO_TLS_MODEL_PTHREAD needs <pthread.h>; only build/register this
    # target on hosts that actually have it (matching
    # RELOCO_THREAD_BACKEND_PTHREAD's own RELOCO_HAS_INCLUDE(<pthread.h>)
    # auto-detection, just performed at CMake-configure time instead of
    # preprocessor time since a dedicated target needs to exist -- or
    # not -- before compilation).
    if(JPLCZ_RELOCO_HAVE_PTHREAD_H)
        add_executable(jplcz_reloco_tls_pthread_tests tests/test_tls_provider_models.cpp)
        target_link_libraries(jplcz_reloco_tls_pthread_tests PRIVATE
            jplcz_reloco GTest::gtest_main)
        if(Threads_FOUND)
            target_link_libraries(jplcz_reloco_tls_pthread_tests PRIVATE Threads::Threads)
        endif()
        target_compile_definitions(jplcz_reloco_tls_pthread_tests PRIVATE
            RELOCO_TLS_MODEL=RELOCO_TLS_MODEL_PTHREAD)
        jplcz_reloco_enable_warnings(jplcz_reloco_tls_pthread_tests)
        gtest_discover_tests(jplcz_reloco_tls_pthread_tests)
    endif()
endif()

if(JPLCZ_RELOCO_BUILD_DEMOS)
    include(CheckIncludeFileCXX)
    check_include_file_cxx("pthread.h" JPLCZ_RELOCO_HAVE_PTHREAD_H)
    if(JPLCZ_RELOCO_HAVE_PTHREAD_H)
        find_package(Threads QUIET)
    endif()

    add_subdirectory(demos)
endif()

if(JPLCZ_RELOCO_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(
        TARGETS jplcz_reloco jplcz_reloco_shared jplcz_reloco_shared_export
        EXPORT jplcz_relocoTargets
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT Development
    )

    # Installed after (never before) the plain include/ tree above so its
    # contents -- always just the *.template.hpp scaffolds under
    # detail/porting/ -- exist first; this only ever adds the real,
    # non-template files a JPLCZ_RELOCO_PORTING_HEADERS build staged
    # alongside them, never replaces anything.
    if(JPLCZ_RELOCO_PORTING_HEADERS)
        install(
            DIRECTORY "${JPLCZ_RELOCO_PORTING_DEST_DIR}/"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/reloco/detail/porting"
            COMPONENT Development
        )
    endif()

    install(
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT Documentation
    )

    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs"
        COMPONENT Documentation
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/jplcz_relocoConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/jplcz_relocoConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/jplcz_reloco"
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    # Header-only INTERFACE library: the package config is architecture
    # independent (no compiled binaries to match ABI/bitness against), so
    # skip the platform check and install under share/, not lib/.
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/jplcz_relocoConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    install(
        EXPORT jplcz_relocoTargets
        FILE jplcz_relocoTargets.cmake
        NAMESPACE jplcz_reloco::
        DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/jplcz_reloco"
        COMPONENT Development
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/jplcz_relocoConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/jplcz_relocoConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/jplcz_reloco"
        COMPONENT Development
    )
endif()

if(JPLCZ_RELOCO_IS_TOP_LEVEL AND JPLCZ_RELOCO_INSTALL)
    set(CPACK_PACKAGE_NAME "jplcz-reloco")
    set(CPACK_PACKAGE_VENDOR "Jaroslaw Pelczar")
    set(CPACK_PACKAGE_CONTACT "Jaroslaw Pelczar <jarek@jpelczar.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Header-only, allocation-free hardened containers and lifetime-safety primitives")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jplcz/reloco")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "jplcz_reloco")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_VERBATIM_VARIABLES ON)
    set(CPACK_THREADS 0)

    set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
    set(CPACK_SOURCE_IGNORE_FILES
        "/\\.git/"
        "/\\.idea/"
        "/\\.vscode/"
        "/build[^/]*/"
        "/cmake-build-[^/]*/"
        "/_deps/"
    )

    # Header-only package: archives always work everywhere; DEB/RPM are added
    # only when their packaging tools are available on the host.
    set(CPACK_GENERATOR "TGZ;ZIP")

    find_program(JPLCZ_RELOCO_DPKG_DEB_EXECUTABLE dpkg-deb)
    if(JPLCZ_RELOCO_DPKG_DEB_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
    endif()

    find_program(JPLCZ_RELOCO_RPMBUILD_EXECUTABLE rpmbuild)
    if(JPLCZ_RELOCO_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
    endif()

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "all")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

    set(CPACK_RPM_PACKAGE_LICENSE "BSD-2-Clause")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "noarch")
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")

    include(CPack)
endif()
