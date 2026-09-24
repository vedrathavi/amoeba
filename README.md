# Amoeba

> **Source Code Search & Indexing Engine**

---

## Current Status

> [!IMPORTANT]
> **Amoeba is currently in Phase 2 (Parsing & Code Structure).**
> It provides repository scanning, file discovery, and Tree-sitter powered syntax parsing and structural code extraction (classes, structs, functions, methods, includes, calls) for C/C++. Indexing and search functionality remain planned for subsequent phases.

---

## Project Vision

Amoeba is an open, high-performance source-code search and indexing engine designed to scale across large codebases. The long-term vision is to progressively evolve through systematic milestones:

$$\text{Source-Code Indexing} \longrightarrow \text{Retrieval} \longrightarrow \text{Code Understanding} \longrightarrow \text{Semantic Search} \longrightarrow \text{Code Intelligence}$$

---

## Engineering Philosophy

> **Build the core, reuse mature infrastructure, measure bottlenecks, and replace components only when there is a good engineering reason.**

* **Build the core**: Implement indexing, retrieval, ranking, and code representation internally to deeply understand and optimize core algorithms.
* **Reuse mature tools**: Leverage established ecosystems (such as Tree-sitter for AST/CST parsing, FAISS/HNSW for ANN vector search, and SQLite/PostgreSQL for relational persistence) when appropriate.
* **Measure and iterate**: Ground all optimizations and replacements in empirical profiling data.

---

## Technology Stack

* **Language**: C++20 (ISO/IEC 14882:2020)
* **Build System**: CMake 3.20+ with CMake Presets
* **Parsing Runtime**: Tree-sitter (C/C++ grammars)
* **Dependency Manager**: vcpkg / CMake FetchContent
* **Testing Framework**: GoogleTest
* **Code Formatting**: clang-format
* **Static Analysis**: clang-tidy
* **Continuous Integration**: GitHub Actions

---

## Repository Structure

```text
amoeba/
│
├── engine/             # Core C++ engine library
│   ├── include/        # Public C++ API headers (amoeba/*.hpp, scanner/*.hpp, parser/*.hpp)
│   ├── src/            # Engine implementation files (scanner/, parser/)
│   └── tests/          # Unit tests (GoogleTest)
│
├── apps/               # Application entry points
│   └── cli/            # Native command-line interface executable
│
├── docs/               # Project documentation
│   ├── architecture/   # System architecture and module boundaries
│   ├── decisions/      # Architecture Decision Records (ADRs)
│   ├── phases/         # Phase roadmaps and scope specifications
│   └── research/       # Research notes, benchmarks, and explorations
│
├── scripts/            # Utility and automation scripts
├── benchmarks/         # Performance benchmarks (future)
├── tests/              # Integration and end-to-end test suites
│
├── .github/            # GitHub Actions workflows and CI automation
│   └── workflows/
│
├── .clang-format       # Code formatting rules
├── .clang-tidy         # Static analysis rules
├── .gitignore          # Git exclusion rules
├── CMakeLists.txt      # Root CMake build specification
├── CMakePresets.json   # Standardized build and test presets
├── vcpkg.json          # vcpkg dependency manifest
└── README.md           # Project overview and quickstart
```

---

## Getting Started

### Prerequisites

* A C++20 compliant compiler (GCC 11+, Clang 13+, or MSVC 2019/2022)
* CMake 3.20 or newer
* Ninja or your platform's default build generator
* (Optional) [vcpkg](https://github.com/microsoft/vcpkg) for dependency management

### Build Instructions

#### Using CMake Presets (Recommended)

```bash
# Configure debug build
cmake --preset debug

# Build all targets (amoeba_engine, amoeba_cli, amoeba_engine_tests)
cmake --build --preset debug
```

#### Manual Build (Standard CMake)

```bash
# Generate build files
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Compile
cmake --build build
```

---

## Running the CLI

### 1. Scan a Repository (`index` command)

```bash
./build/debug/apps/cli/amoeba_cli index <repository-path>
```

### 2. Parse a Source File (`parse` command)

```bash
./build/debug/apps/cli/amoeba_cli parse <file-path>
```

**Example Output:**
```text
Amoeba
Source Code Search & Indexing Engine

File:
  ./engine/include/amoeba/scanner/repository_scanner.hpp

Language:
  C++

Parsing:
  success

Structural elements:
  Class:
    RepositoryScanner (Line 29)

  Struct:
    ScanResult (Line 17)

  Method:
    total_files_included (in ScanResult) (Line 23)
    RepositoryScanner (in RepositoryScanner) (Line 31)
    scan (in RepositoryScanner) (Line 39)

  Include:
    "amoeba/scanner/file_info.hpp" (Line 3)
    <filesystem> (Line 5)
```

---

## Running Tests

Run the GoogleTest test suite using CTest:

#### Using Presets
```bash
ctest --preset debug
```

#### Manual CTest
```bash
ctest --test-dir build --output-on-failure
```

---

## License

The project license selection is currently pending.
