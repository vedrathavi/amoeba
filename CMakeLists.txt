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

# 4. Tree-sitter Python grammar
FetchContent_Declare(
    tree_sitter_python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG v0.21.0
    GIT_SHALLOW TRUE
)

# 5. Tree-sitter Java grammar
FetchContent_Declare(
    tree_sitter_java
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-java.git
    GIT_TAG v0.21.0
    GIT_SHALLOW TRUE
)

# 6. Tree-sitter Go grammar
FetchContent_Declare(
    tree_sitter_go
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-go.git
    GIT_TAG v0.21.0
    GIT_SHALLOW TRUE
)

# 7. Tree-sitter Rust grammar
FetchContent_Declare(
    tree_sitter_rust
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-rust.git
    GIT_TAG v0.21.2
    GIT_SHALLOW TRUE
)

# 8. Tree-sitter JavaScript grammar
FetchContent_Declare(
    tree_sitter_javascript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
    GIT_TAG v0.21.4
    GIT_SHALLOW TRUE
)

# 9. Tree-sitter TypeScript / TSX grammar
FetchContent_Declare(
    tree_sitter_typescript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript.git
    GIT_TAG v0.21.2
    GIT_SHALLOW TRUE
)

# 10. Tree-sitter HTML grammar
FetchContent_Declare(
    tree_sitter_html
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-html.git
    GIT_TAG v0.20.3
    GIT_SHALLOW TRUE
)

# 11. Tree-sitter CSS grammar
FetchContent_Declare(
    tree_sitter_css
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-css.git
    GIT_TAG v0.21.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(
    tree_sitter
    tree_sitter_cpp
    tree_sitter_c
    tree_sitter_python
    tree_sitter_java
    tree_sitter_go
    tree_sitter_rust
    tree_sitter_javascript
    tree_sitter_typescript
    tree_sitter_html
    tree_sitter_css
)

function(amoeba_add_c_grammar target_name source_files include_dirs)
    if(NOT TARGET ${target_name})
        add_library(${target_name} STATIC ${source_files})
        target_include_directories(${target_name} PUBLIC
            ${include_dirs}
            ${tree_sitter_SOURCE_DIR}/lib/include
        )
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /w)
        else()
            target_compile_options(${target_name} PRIVATE -w)
        endif()
    endif()
endfunction()

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

amoeba_add_c_grammar(tree_sitter_cpp_lib
    "${tree_sitter_cpp_SOURCE_DIR}/src/parser.c;${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_cpp_SOURCE_DIR}/src"
)

amoeba_add_c_grammar(tree_sitter_c_lib
    "${tree_sitter_c_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_c_SOURCE_DIR}/src"
)

amoeba_add_c_grammar(tree_sitter_python_lib
    "${tree_sitter_python_SOURCE_DIR}/src/parser.c;${tree_sitter_python_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_python_SOURCE_DIR}/src"
)

amoeba_add_c_grammar(tree_sitter_java_lib
    "${tree_sitter_java_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_java_SOURCE_DIR}/src"
)

amoeba_add_c_grammar(tree_sitter_go_lib
    "${tree_sitter_go_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_go_SOURCE_DIR}/src"
)

amoeba_add_c_grammar(tree_sitter_rust_lib
    "${tree_sitter_rust_SOURCE_DIR}/src/parser.c;${tree_sitter_rust_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_rust_SOURCE_DIR}/src"
)

amoeba_add_c_grammar(tree_sitter_javascript_lib
    "${tree_sitter_javascript_SOURCE_DIR}/src/parser.c;${tree_sitter_javascript_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_javascript_SOURCE_DIR}/src"
)

amoeba_add_c_grammar(tree_sitter_typescript_lib
    "${tree_sitter_typescript_SOURCE_DIR}/typescript/src/parser.c;${tree_sitter_typescript_SOURCE_DIR}/typescript/src/scanner.c"
    "${tree_sitter_typescript_SOURCE_DIR}/typescript/src"
)

amoeba_add_c_grammar(tree_sitter_tsx_lib
    "${tree_sitter_typescript_SOURCE_DIR}/tsx/src/parser.c;${tree_sitter_typescript_SOURCE_DIR}/tsx/src/scanner.c"
    "${tree_sitter_typescript_SOURCE_DIR}/tsx/src"
)

amoeba_add_c_grammar(tree_sitter_html_lib
    "${tree_sitter_html_SOURCE_DIR}/src/parser.c;${tree_sitter_html_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_html_SOURCE_DIR}/src"
)

amoeba_add_c_grammar(tree_sitter_css_lib
    "${tree_sitter_css_SOURCE_DIR}/src/parser.c;${tree_sitter_css_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_css_SOURCE_DIR}/src"
)

# -----------------------------------------------------------------------------
# 1. amoeba_engine (Core Library Target)
# -----------------------------------------------------------------------------
add_library(amoeba_engine
    engine/src/engine.cpp
    engine/src/scanner/repository_scanner.cpp
    engine/src/parser/source_parser.cpp
    engine/src/parser/extractors/cpp_extractor.cpp
    engine/src/parser/extractors/python_extractor.cpp
    engine/src/parser/extractors/java_extractor.cpp
    engine/src/parser/extractors/go_extractor.cpp
    engine/src/parser/extractors/rust_extractor.cpp
    engine/src/parser/extractors/js_ts_extractor.cpp
    engine/src/parser/extractors/web_extractor.cpp
    engine/src/parser/extractors/nextjs_extractor.cpp
    engine/src/index/code_tokenizer.cpp
    engine/src/index/inverted_index.cpp
    engine/src/index/search_engine.cpp
    engine/src/rank/baseline_ranker.cpp
    engine/src/rank/bm25_ranker.cpp
    engine/src/rank/code_aware_ranker.cpp
    engine/src/graph/relationship_graph.cpp
    engine/src/graph/import_extractor.cpp
    engine/src/graph/inheritance_extractor.cpp
    engine/src/graph/call_extractor.cpp
    engine/src/graph/repository_graph_builder.cpp
    engine/src/graph/incremental_graph_updater.cpp
    engine/src/graph/graph_query_service.cpp
    engine/src/graph/relationship_evidence_resolver.cpp
    engine/src/search/relationship_aware_search.cpp
    engine/src/semantic/similarity.cpp
    engine/src/semantic/semantic_document.cpp
    engine/src/semantic/deterministic_embedding_provider.cpp
    engine/src/semantic/pretrained_embedding_provider.cpp
    engine/src/semantic/semantic_index.cpp
    engine/src/semantic/semantic_retriever.cpp
    engine/src/eval/ranking_metrics.cpp
    engine/src/eval/benchmark_result.cpp
    engine/src/hybrid/score_normalizer.cpp
    engine/src/hybrid/fusion_strategy.cpp
    engine/src/hybrid/hybrid_retriever.cpp
    engine/src/retrieval/retrieval_unit.cpp
    engine/src/retrieval/supporting_evidence_resolver.cpp
    engine/src/retrieval/query_understanding.cpp
    engine/src/retrieval/primary_retrieval_pipeline.cpp
    engine/src/source/source_snippet_reader.cpp
    engine/src/evidence/evidence_assembler.cpp
    engine/src/evidence/evidence_sufficiency.cpp
    engine/src/context/context_builder.cpp
    engine/src/reasoning/prompt_builder.cpp
    engine/src/reasoning/fake_llm_runtime.cpp
    engine/src/reasoning/reasoning_service.cpp
    engine/src/reasoning/local_llm_runtime.cpp
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
        tree_sitter_python_lib
        tree_sitter_java_lib
        tree_sitter_go_lib
        tree_sitter_rust_lib
        tree_sitter_javascript_lib
        tree_sitter_typescript_lib
        tree_sitter_tsx_lib
        tree_sitter_html_lib
        tree_sitter_css_lib
        $<$<PLATFORM_ID:Windows>:winhttp>
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
        engine/tests/index/code_tokenizer_test.cpp
        engine/tests/index/inverted_index_test.cpp
        engine/tests/index/search_engine_test.cpp
        engine/tests/index/index_benchmark_test.cpp
        engine/tests/index/retrieval_integration_test.cpp
        engine/tests/rank/baseline_ranker_test.cpp
        engine/tests/rank/bm25_ranker_test.cpp
        engine/tests/rank/code_aware_ranker_test.cpp
        engine/tests/rank/evaluation_benchmark_test.cpp
        engine/tests/graph/relationship_graph_test.cpp
        engine/tests/graph/import_extractor_test.cpp
        engine/tests/graph/inheritance_extractor_test.cpp
        engine/tests/graph/call_extractor_test.cpp
        engine/tests/graph/repository_graph_builder_test.cpp
        engine/tests/graph/incremental_graph_updater_test.cpp
        engine/tests/graph/graph_query_service_test.cpp
        engine/tests/graph/relationship_evidence_resolver_test.cpp
        engine/tests/search/relationship_aware_search_test.cpp
        engine/tests/audit/phase5_audit_validation_test.cpp
        engine/tests/semantic/similarity_test.cpp
        engine/tests/semantic/embedding_provider_test.cpp
        engine/tests/semantic/pretrained_embedding_provider_test.cpp
        engine/tests/semantic/semantic_index_test.cpp
        engine/tests/semantic/semantic_retriever_test.cpp
        engine/tests/semantic/semantic_retrieval_benchmark_test.cpp
        engine/tests/eval/ranking_metrics_test.cpp
        engine/tests/hybrid/score_normalizer_test.cpp
        engine/tests/hybrid/fusion_strategy_test.cpp
        engine/tests/hybrid/hybrid_retriever_test.cpp
        engine/tests/retrieval/retrieval_unit_test.cpp
        engine/tests/retrieval/query_understanding_test.cpp
        engine/tests/retrieval/primary_retrieval_pipeline_test.cpp
        engine/tests/retrieval/retrieval_hardening_test.cpp
        engine/tests/retrieval/cli_retrieval_integration_test.cpp
        engine/tests/source/source_snippet_reader_test.cpp
        engine/tests/evidence/evidence_assembler_test.cpp
        engine/tests/evidence/evidence_sufficiency_test.cpp
        engine/tests/context/context_builder_test.cpp
        engine/tests/context/end_to_end_regression_test.cpp
        engine/tests/reasoning/reasoning_service_test.cpp
        engine/tests/reasoning/local_llm_runtime_test.cpp
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

# -----------------------------------------------------------------------------
# 4. amoeba_phase_06_5_benchmark (Phase 6.5 Native Benchmark Target)
# -----------------------------------------------------------------------------
add_executable(amoeba_phase_06_5_benchmark
    benchmarks/phase-06.5/benchmark_main.cpp
)

target_link_libraries(amoeba_phase_06_5_benchmark
    PRIVATE
        amoeba_engine
)

target_compile_features(amoeba_phase_06_5_benchmark PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(amoeba_phase_06_5_benchmark PRIVATE /W4 /permissive-)
else()
    target_compile_options(amoeba_phase_06_5_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
