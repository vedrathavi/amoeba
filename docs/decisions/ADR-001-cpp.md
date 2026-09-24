# ADR-001: Selection of C++20 for the Core Engine

* **Status**: Accepted
* **Deciders**: Amoeba Core Team
* **Date**: 2026-09-24

---

## Context

Amoeba is a source-code search and indexing engine. The core engine is responsible for high-throughput file scanning, AST parsing integration, in-memory index construction, inverted index traversal, vector indexing interoperability, and low-latency query retrieval.

To achieve these goals across diverse codebases containing millions of lines of code, the core engine requires:
1. Predictable, low-latency execution without non-deterministic garbage collection pauses.
2. Fine-grained control over memory layout, cache locality, and zero-copy string/buffer manipulation.
3. Seamless zero-overhead Foreign Function Interface (FFI) and native interoperability with high-performance C and C++ libraries (e.g., Tree-sitter, FAISS, SQLite, SIMD vector libraries).
4. Mature tooling, stable compilers, and battle-tested cross-platform support (Windows, Linux, macOS).

---

## Requirements

* **Performance & Predictability**: Sub-millisecond index query latency and high-throughput streaming token ingestion.
* **Direct Hardware & Memory Control**: Ability to optimize memory structures (e.g., contiguous buffer indexing, memory-mapped files, custom allocators).
* **Interoperability**: First-class integration with native C/C++ parser runtimes (Tree-sitter) and accelerated linear algebra / vector search backends.
* **Cross-Platform Portability**: Native compilation across major desktop and server operating systems.

---

## Alternatives Considered

### 1. Rust
* **Pros**: Strong memory safety guarantees without garbage collection; modern package manager (`cargo`); expressive type system.
* **Cons**: Interoperability with extensive C/C++ libraries (such as certain SIMD or native vector search libraries) requires custom unsafe FFI bindings; build times can be significant; team familiarity and specific C++ tooling alignment.
* **Assessment**: A very strong alternative. While Rust excels in memory safety, C++20 was chosen for its direct native alignment with the planned C/C++ parser and numerical library ecosystems, plus existing team core competencies.

### 2. Go
* **Pros**: Rapid development velocity, clean concurrency primitives (`goroutines`), robust standard library, fast compilation.
* **Cons**: Garbage collection introduces non-deterministic latency spikes during high-allocation indexing passes; CGo bridge incurs a non-trivial invocation overhead on fine-grained FFI calls (e.g., calling Tree-sitter thousands of times per file); limited low-level control over memory layouts and manual SIMD vectorization.
* **Assessment**: Go is well-suited for distributed network services, but suboptimal for high-frequency inner-loop indexing and parsing tasks.

### 3. Python
* **Pros**: Fast prototyping, rich data science and machine learning ecosystem.
* **Cons**: Substantial interpreter overhead, Global Interpreter Lock (GIL) limitations on multicore parallelism, high memory overhead per object, and inadequate throughput for raw text tokenization and index traversal.
* **Assessment**: Unsuitable for the core indexing engine, though valuable for future offline analysis or scripting.

---

## Advantages of C++20

1. **Deterministic Performance & Resource Control**: Zero-cost abstractions, explicit lifetime management, and manual memory alignment for cache efficiency.
2. **Modern Language Features**: C++20 concepts, `std::span`, `std::string_view`, coroutines, ranges, and constexpr improvements enable expressive, type-safe, and clean codebases without legacy boilerplate.
3. **Ecosystem & Library Interoperability**: Direct, zero-overhead integration with C libraries (Tree-sitter), vector libraries, and platform-specific SIMD intrinsics (AVX2, NEON).
4. **Tooling & Platform Maturity**: Ubiquitous compiler support (GCC, Clang, MSVC), robust debugging tools, profilers (Perf, VTune), and CMake build ecosystem.

---

## Trade-offs and Mitigations

* **Memory Safety & Undefined Behavior**:
  * *Trade-off*: C++ does not provide automatic compile-time memory safety.
  * *Mitigation*: Enforce modern C++20 conventions (RAII, smart pointers, value semantics, `std::span`/`std::string_view`), strict compiler warnings (`-Wall -Wextra -Wpedantic` / `/W4`), sanitizers (ASan, UBSan), and static analysis via `clang-tidy`.
* **Build System Complexity**:
  * *Trade-off*: C++ lacks a single unified official package manager.
  * *Mitigation*: Standardize on CMake 3.20+ with `CMakePresets.json` and `vcpkg` for deterministic cross-platform dependency management.

---

## Decision

We select **C++20** as the implementation language for the Amoeba core engine (`amoeba_engine`) and native CLI (`amoeba_cli`).

---

## Consequences

* **Positive**:
  * The engine will achieve maximum theoretical throughput and predictable query latency.
  * Native C/C++ libraries can be linked directly without foreign language bridge overhead.
  * Memory footprint during index building can be tightly controlled and profiled.
* **Negative / Challenges**:
  * Strict discipline, rigorous code reviews, automated linting (`clang-tidy`), and continuous testing are required to uphold safety and maintainability.
  * Future web and Node.js integrations will require clear boundary interfaces (e.g., C APIs, Node-API / N-API addons, or lightweight HTTP/IPC bridges).
