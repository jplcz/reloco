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
option(JPLCZ_RELOCO_INSTALL
    "Install jplcz_reloco headers and CMake package metadata"
    ${JPLCZ_RELOCO_IS_TOP_LEVEL})

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
        tests/test_array.cpp
        tests/test_span.cpp
        tests/test_string_view.cpp
        tests/test_expected.cpp
        tests/test_allocator.cpp
        tests/test_concepts.cpp
        tests/test_construction_helpers.cpp
        tests/test_unique_ptr.cpp
        tests/test_string.cpp
        tests/test_relocatable.cpp
        tests/test_shared_ptr.cpp
        tests/test_function.cpp
        tests/test_collection_view.cpp
        tests/test_container_ref.cpp
        tests/test_vector.cpp
        tests/test_flat_set.cpp
        tests/test_stack_allocator.cpp
        tests/test_inline_string.cpp
        tests/test_optional.cpp
        tests/test_inplace_function.cpp
        tests/test_function_ref.cpp)
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
endif()

if(JPLCZ_RELOCO_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(
        TARGETS jplcz_reloco
        EXPORT jplcz_relocoTargets
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT Development
    )

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
