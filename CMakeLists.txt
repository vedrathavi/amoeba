cmake_minimum_required(VERSION 3.20)

project(
    Amoeba
    VERSION 0.1.0
    DESCRIPTION "Source-code search and indexing engine"
    LANGUAGES CXX
)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for tools like clangd and clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project options
option(AMOEBA_BUILD_TESTS "Build Amoeba test suite" ON)
option(AMOEBA_BUILD_CLI "Build Amoeba CLI application" ON)

# -----------------------------------------------------------------------------
# 1. amoeba_engine (Core Library Target)
# -----------------------------------------------------------------------------
add_library(amoeba_engine
    engine/src/engine.cpp
    engine/src/scanner/repository_scanner.cpp
)

target_include_directories(amoeba_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/src
)

target_compile_features(amoeba_engine PUBLIC cxx_std_20)

# Compiler warnings configuration
if(MSVC)
    target_compile_options(amoeba_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(amoeba_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# 2. amoeba_cli (CLI Executable Target)
# -----------------------------------------------------------------------------
if(AMOEBA_BUILD_CLI)
    add_executable(amoeba_cli
        apps/cli/main.cpp
    )

    target_link_libraries(amoeba_cli
        PRIVATE
            amoeba_engine
    )

    target_compile_features(amoeba_cli PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(amoeba_cli PRIVATE /W4 /permissive-)
    else()
        target_compile_options(amoeba_cli PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# -----------------------------------------------------------------------------
# 3. amoeba_engine_tests (Testing Target)
# -----------------------------------------------------------------------------
if(AMOEBA_BUILD_TESTS)
    enable_testing()

    # Find via vcpkg/system if explicitly requested or configured, otherwise fetch from source
    if(VCPKG_TARGET_TRIPLET OR CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        find_package(GTest CONFIG REQUIRED)
    else()
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(amoeba_engine_tests
        engine/tests/smoke_test.cpp
        engine/tests/scanner/repository_scanner_test.cpp
    )

    target_link_libraries(amoeba_engine_tests
        PRIVATE
            amoeba_engine
            $<$<TARGET_EXISTS:GTest::gtest_main>:GTest::gtest_main>
            $<$<NOT:$<TARGET_EXISTS:GTest::gtest_main>>:gtest_main>
    )

    target_compile_features(amoeba_engine_tests PRIVATE cxx_std_20)

    include(GoogleTest)
    gtest_discover_tests(amoeba_engine_tests)
endif()
