# Phase 0 — Project Foundation

## Objective

The objective of **Phase 0** is to establish a clean, robust, cross-platform build, testing, formatting, and documentation foundation for the Amoeba project. 

Phase 0 sets up all development workflows and structural guardrails without prematurely introducing domain logic or speculative abstractions.

---

## Scope

1. **Build System & Toolchain**:
   * CMake 3.20+ setup enforcing modern C++20 standards.
   * Standardized `CMakePresets.json` configurations (Debug, Release, vcpkg, IDE).
   * Dependency management via `vcpkg.json` manifest mode.
2. **Project Architecture Targets**:
   * Reusable library target: `amoeba_engine`.
   * Executable CLI target: `amoeba_cli`.
   * Unit test target: `amoeba_engine_tests`.
3. **Quality & Formatting Guardrails**:
   * Modern `.clang-format` configuration.
   * Practical `.clang-tidy` static analysis rules.
   * GoogleTest integration with automated test discovery.
4. **CI/CD Automation**:
   * GitHub Actions workflow covering checkout, configure, build, and test steps.
5. **Documentation Baseline**:
   * Architecture overview, Architecture Decision Records (ADRs), phase specifications, and research guidelines.

---

## Non-Goals (Explicitly Out of Scope)

The following components and capabilities are deliberately **excluded** from Phase 0:

* ❌ Source-code scanning and filesystem crawling
* ❌ Tokenization, lexing, and grammar parsing
* ❌ AST generation and Tree-sitter bindings
* ❌ Inverted index, trigram index, or document indexing
* ❌ Text search, regex search, or fuzzy retrieval
* ❌ Ranking pipelines or scoring algorithms
* ❌ Embeddings generation and semantic vector search
* ❌ AI / LLM integration or prompt construction
* ❌ Persistent databases (SQLite, PostgreSQL, etc.)
* ❌ Backend server APIs (Node.js, TypeScript, REST, gRPC)
* ❌ Frontend web UI (React, Vite, Tailwind CSS)
* ❌ Authentication, authorization, or multi-user tenancy
* ❌ Distributed clustering, caching, or cloud deployment

---

## Technology Stack (Phase 0)

| Layer | Technology | Details |
| :--- | :--- | :--- |
| **Language** | C++20 | ISO C++20 (`-std=c++20` / `/std:c++20`) |
| **Build System** | CMake 3.20+ | Target-based modern CMake with Presets |
| **Dependency Manager** | vcpkg | Manifest mode (`vcpkg.json`) |
| **Unit Testing** | GoogleTest (v1.14.0) | Integrated via CMake / vcpkg / FetchContent |
| **Code Formatter** | clang-format | Modern C++ style (LLVM-derived) |
| **Static Analyzer** | clang-tidy | Practical checks for bugs, readability, modern idioms |
| **CI Automation** | GitHub Actions | Automated build and test pipeline |

---

## Repository Structure

```text
amoeba/
│
├── engine/
│   ├── include/
│   │   └── amoeba/
│   │       └── engine.hpp
│   ├── src/
│   │   └── engine.cpp
│   └── tests/
│       └── smoke_test.cpp
│
├── apps/
│   └── cli/
│       └── main.cpp
│
├── docs/
│   ├── README.md
│   ├── architecture/
│   │   └── overview.md
│   ├── decisions/
│   │   └── ADR-001-cpp.md
│   ├── phases/
│   │   └── phase-00-foundation.md
│   └── research/
│       └── README.md
│
├── scripts/
├── benchmarks/
├── tests/
│
├── .github/
│   └── workflows/
│       └── ci.yml
│
├── .gitignore
├── .clang-format
├── .clang-tidy
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── README.md
└── LICENSE
```

---

## Checkpoint Criteria

Phase 0 is complete when all of the following criteria are satisfied:

- [x] Repository builds successfully.
- [x] C++20 standard is enforced across all CMake targets.
- [x] Engine library target (`amoeba_engine`) exists and exports include directories.
- [x] CLI executable (`amoeba_cli`) builds and links against `amoeba_engine`.
- [x] CLI runs and outputs official Amoeba information.
- [x] GoogleTest runs and passes smoke tests.
- [x] `.clang-format` configuration is established.
- [x] `.clang-tidy` configuration is established.
- [x] `CMakePresets.json` provides standard build and test configurations.
- [x] `vcpkg.json` manifest is configured.
- [x] GitHub Actions CI workflow is defined.
- [x] Documentation structure and initial ADRs are written.
- [x] No future or speculative functionality is prematurely implemented.
