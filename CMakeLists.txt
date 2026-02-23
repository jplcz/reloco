cmake_minimum_required(VERSION 3.14)
project(reloco VERSION 0.1.0 LANGUAGES CXX)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(RELOCO_BUILD_TESTS "Build reloco unit tests" ON)
option(RELOCO_ENABLE_INSTALL "Enable project install" ON)

add_library(reloco INTERFACE)
add_library(reloco::reloco ALIAS reloco)

target_compile_features(reloco INTERFACE cxx_std_20)

target_include_directories(reloco INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/relocoConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/relocoConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/relocoConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/reloco
)

install(
    FILES
    "${CMAKE_CURRENT_BINARY_DIR}/relocoConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/relocoConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/reloco
)

if(RELOCO_BUILD_TESTS)
    find_package(GTest QUIET)
    find_package(Boost 1.65 CONFIG)
    find_package(Threads REQUIRED)

    if(NOT GTest_FOUND)
        message(STATUS "GTest not found on host. Fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/heads/main.zip
        )

        # Prevent GTest from overriding our compiler settings on Windows
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)

        # Create aliases to match find_package output if necessary
        if(NOT TARGET GTest::gtest_main)
            add_library(GTest::gtest_main ALIAS gtest_main)
        endif()
    else()
        message(STATUS "Found GTest on host: ${GTest_VERSION}")
    endif()

    enable_testing()
    add_executable(reloco_tests
        tests/test_posix_allocator.cpp
        tests/test_make_unique.cpp
        tests/test_stl_bridge_allocator.cpp
        tests/test_string.cpp
        tests/test_vector.cpp
        tests/test_stack_allocator.cpp
        tests/test_shared_ptr.cpp
        tests/test_span.cpp
        tests/test_array.cpp
        tests/test_mutex.cpp
        tests/test_fallible_constructed.cpp
        tests/test_fallible_singleton.cpp
        tests/test_collection_view.cpp
        tests/test_function.cpp
        tests/test_flat_set.cpp
    )

    if(Boost_FOUND)
        target_sources(reloco_tests PRIVATE
            tests/test_intrusive_ptr.cpp
            tests/test_map.cpp)
        target_link_libraries(reloco_tests PRIVATE Boost::boost)
    else()
        message(STATUS "Boost library not found")
    endif()

    target_link_libraries(reloco_tests PRIVATE reloco::reloco GTest::gtest_main Threads::Threads)
    target_compile_features(reloco_tests INTERFACE cxx_std_20)

    include(GoogleTest)
    gtest_discover_tests(reloco_tests)
endif()

if(RELOCO_ENABLE_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS reloco EXPORT relocoTargets)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT relocoTargets
        FILE relocoTargets.cmake
        NAMESPACE reloco::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/reloco
    )
endif()
