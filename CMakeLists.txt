cmake_minimum_required(VERSION 3.20)

project(
    Amoeba
    VERSION 0.1.0
    DESCRIPTION "Source-code search and indexing engine"
    LANGUAGES C CXX
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
# Tree-sitter & Language Grammars
# -----------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Don't check git remotes on each build" FORCE)

# 1. Tree-sitter core runtime
FetchContent_Declare(
    tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.22.6
    GIT_SHALLOW TRUE
)

# 2. Tree-sitter C++ grammar
FetchContent_Declare(
    tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.22.3
    GIT_SHALLOW TRUE
)

# 3. Tree-sitter C grammar
FetchContent_Declare(
    tree_sitter_c
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c.git
    GIT_TAG v0.21.4
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(tree_sitter tree_sitter_cpp tree_sitter_c)

if(NOT TARGET tree_sitter_lib)
    add_library(tree_sitter_lib STATIC
        ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
    )
    target_include_directories(tree_sitter_lib PUBLIC
        ${tree_sitter_SOURCE_DIR}/lib/include
        ${tree_sitter_SOURCE_DIR}/lib/src
    )
    if(MSVC)
        target_compile_options(tree_sitter_lib PRIVATE /w)
    else()
        target_compile_options(tree_sitter_lib PRIVATE -w)
    endif()
endif()

if(NOT TARGET tree_sitter_cpp_lib)
    add_library(tree_sitter_cpp_lib STATIC
        ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
        ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c
    )
    target_include_directories(tree_sitter_cpp_lib PUBLIC
        ${tree_sitter_cpp_SOURCE_DIR}/src
        ${tree_sitter_SOURCE_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree_sitter_cpp_lib PRIVATE /w)
    else()
        target_compile_options(tree_sitter_cpp_lib PRIVATE -w)
    endif()
endif()

if(NOT TARGET tree_sitter_c_lib)
    add_library(tree_sitter_c_lib STATIC
        ${tree_sitter_c_SOURCE_DIR}/src/parser.c
    )
    target_include_directories(tree_sitter_c_lib PUBLIC
        ${tree_sitter_c_SOURCE_DIR}/src
        ${tree_sitter_SOURCE_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree_sitter_c_lib PRIVATE /w)
    else()
        target_compile_options(tree_sitter_c_lib PRIVATE -w)
    endif()
endif()

# -----------------------------------------------------------------------------
# 1. amoeba_engine (Core Library Target)
# -----------------------------------------------------------------------------
add_library(amoeba_engine
    engine/src/engine.cpp
    engine/src/scanner/repository_scanner.cpp
    engine/src/parser/source_parser.cpp
)

target_include_directories(amoeba_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/src
)

target_link_libraries(amoeba_engine
    PRIVATE
        tree_sitter_lib
        tree_sitter_cpp_lib
        tree_sitter_c_lib
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
            GIT_SHALLOW TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(amoeba_engine_tests
        engine/tests/smoke_test.cpp
        engine/tests/scanner/repository_scanner_test.cpp
        engine/tests/parser/source_parser_test.cpp
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
