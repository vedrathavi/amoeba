# Amoeba

> **Source Code Search & Indexing Engine**

---

## Current Status

> [!IMPORTANT]
> **Amoeba is currently in Phase 1 (Repository Scanner).**
> It provides repository traversal, filtering, and source-code file discovery. Full tokenization, parsing, indexing, and search functionality remain planned for subsequent phases.

---

## Project Vision

Amoeba is an open, high-performance source-code search and indexing engine designed to scale across large codebases. The long-term vision is to progressively evolve through systematic milestones:

$$\text{Source-Code Indexing} \longrightarrow \text{Retrieval} \longrightarrow \text{Code Understanding} \longrightarrow \text{Semantic Search} \longrightarrow \text{Code Intelligence}$$

---

## Engineering Philosophy

> **Build the core, reuse mature infrastructure, measure bottlenecks, and replace components only when there is a good engineering reason.**

* **Build the core**: Implement indexing, retrieval, ranking, and code representation internally to deeply understand and optimize core algorithms.
* **Reuse mature tools**: Leverage established ecosystems (such as Tree-sitter for AST parsing, FAISS/HNSW for ANN vector search, and SQLite/PostgreSQL for relational persistence) when appropriate.
* **Measure and iterate**: Ground all optimizations and replacements in empirical profiling data.

---

## Technology Stack

* **Language**: C++20 (ISO/IEC 14882:2020)
* **Build System**: CMake 3.20+ with CMake Presets
* **Dependency Manager**: vcpkg (manifest mode)
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
│   ├── include/        # Public C++ API headers (amoeba/*.hpp, amoeba/scanner/*.hpp)
│   ├── src/            # Engine implementation files
│   └── tests/          # Engine unit tests (GoogleTest)
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

Scan a local repository and inspect discovered source files using the `index` command:

```bash
# Run scan on a repository
./build/debug/apps/cli/amoeba_cli index <repository-path>
```

**Example Output:**
```text
Amoeba
Source Code Search & Indexing Engine

Repository:
  ./sample-project

Scan complete.

Files discovered: 42
Files included:   27
Files ignored:    15

Language Breakdown:
  C++:         12
  TypeScript:   8
  Python:       7
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
