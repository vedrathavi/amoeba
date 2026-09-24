# Phase 1 — Repository Scanner

## 1. Phase Objective

The objective of **Phase 1** is to teach Amoeba how to inspect a local repository, recursively traverse directories, apply centralized inclusion and exclusion rules, and reliably discover source-code files to be processed by future phases.

```text
Repository Path ──► [Repository Scanner] ──► ScanResult (FileInfo[]) ──► Future Phases
```

---

## 2. Problem Statement

Before Amoeba can parse, tokenize, or index source code, it needs a reliable, cross-platform boundary between the local filesystem and the core engine. The scanner identifies which files are relevant source code while filtering out metadata, dependencies, build artifacts, and non-source files without reading or parsing file contents.

---

## 3. Input & Output Contracts

### Input
* **Repository Path**: A valid path to an existing directory on the local filesystem (`std::filesystem::path`).

### Output
* **`ScanResult`**: Structured result object containing:
  * `root_path`: The root directory scanned.
  * `files`: A list of `FileInfo` structures for all included source files.
  * `total_files_discovered`: Total count of all regular files encountered.
  * `total_files_ignored`: Total count of non-source files filtered out.
  * `total_files_included()`: Count of source files admitted into the pipeline.

---

## 4. File Information Representation

The [`FileInfo`](file:///d:/amoeba/engine/include/amoeba/scanner/file_info.hpp) struct encapsulates minimal metadata required for Phase 1:

```cpp
struct FileInfo {
    std::filesystem::path path;
    std::string extension;
    std::uintmax_t size{0};
};
```

> [!NOTE]
> Speculative fields (such as content hashes, modification timestamps, ASTs, tokens, or symbols) are intentionally omitted until required by future phases.

---

## 5. Directory Exclusion Rules

To avoid processing build outputs, vendored dependencies, and version control internal structures, the scanner skips descending into the following directories:

* `.git` (VCS metadata)
* `node_modules` (JavaScript / Node dependencies)
* `build` (Build outputs / CMake artifacts)
* `dist` (Distribution bundles)
* `out` (Compiler / IDE outputs)
* `target` (Rust / build outputs)
* `coverage` (Test coverage reports)

---

## 6. File Inclusion Rules

Source file discovery is driven by centralized file extension matching:

| Language Family | Supported Extensions |
| :--- | :--- |
| **C / C++** | `.c`, `.h`, `.cc`, `.hh`, `.cpp`, `.hpp`, `.cxx`, `.hxx` |
| **Python** | `.py` |
| **JavaScript / TypeScript** | `.js`, `.jsx`, `.ts`, `.tsx` |
| **Java** | `.java` |
| **Go** | `.go` |
| **Rust** | `.rs` |

Unsupported files (e.g., `.md`, `.json`, `.png`, `.zip`, binaries) and extensionless files are ignored.

---

## 7. Module Structure

The scanner is implemented as a dedicated engine submodule:

```text
engine/
├── include/
│   └── amoeba/
│       └── scanner/
│           ├── file_info.hpp            # File metadata struct
│           └── repository_scanner.hpp   # Scanner interface & ScanResult
│
├── src/
│   └── scanner/
│       └── repository_scanner.cpp       # Traversal & filtering implementation
│
└── tests/
    └── scanner/
        └── repository_scanner_test.cpp  # GTest suite
```

---

## 8. CLI Usage

The native CLI exposes repository scanning via the `index` command:

```bash
# Scan a repository
amoeba index <repository-path>
```

### Example Output
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

## 9. Error Handling

* **Missing Repository Argument**: CLI outputs an error message and prints command usage.
* **Non-Existent Path**: `RepositoryScanner::scan` throws `std::invalid_argument`, reported gracefully by the CLI.
* **File Given Instead of Directory**: `RepositoryScanner::scan` throws `std::invalid_argument`, reported gracefully by the CLI.
* **Permission Denied**: `std::filesystem::directory_options::skip_permission_denied` is enabled to prevent crashes on unreadable subdirectories.

---

## 10. Test Suite

Unit tests in [`engine/tests/scanner/repository_scanner_test.cpp`](file:///d:/amoeba/engine/tests/scanner/repository_scanner_test.cpp) verify:
1. `EmptyDirectoryYieldsZeroFiles`: Validates zero counts on empty repositories.
2. `DiscoversSupportedSourceFilesRecursively`: Verifies recursive discovery across nested folders for all supported extensions.
3. `IgnoresUnsupportedFileExtensions`: Verifies exclusion of documentation, images, and configuration files.
4. `SkipsExcludedDirectories`: Verifies that `.git`, `node_modules`, `build`, etc., are not traversed.
5. `CapturesAccurateFileMetadata`: Verifies exact path, extension, and file size extraction.
6. `ThrowsOnNonExistentPath`: Validates error thrown on missing directories.
7. `ThrowsWhenPathIsRegularFile`: Validates error thrown when a single file is passed.
8. `HelperFilters`: Validates static helper routines.

---

## 11. Known Limitations (Phase 1)

* **No `.gitignore` Parsing**: Directory and file exclusions use an explicit internal list rather than dynamically parsing repository `.gitignore` or `.ignore` files.
* **No Content Inspection**: Language detection is purely extension-based; shebang lines or file signatures are not inspected.
* **Single-Threaded**: Traversal runs synchronously on a single thread.
* **No File Content Loading**: File contents are not read into memory.

---

## 12. Intentionally Deferred to Future Phases

* Tokenization and lexing
* Tree-sitter AST parsing
* Inverted index / trigram construction
* Query search & ranking
* Vector embeddings & semantic search
* Incremental change detection
* Database persistence
* Backend HTTP server & Web UI

---

## 13. Phase 1 Checkpoint Criteria

- [x] Repository path can be provided to Amoeba.
- [x] Path is validated (existence, directory check).
- [x] Repository is traversed recursively.
- [x] Known irrelevant directories are skipped.
- [x] Supported source files are identified.
- [x] Unsupported files are ignored.
- [x] `FileInfo` objects are produced.
- [x] Scanner is independent from CLI.
- [x] Scanner has comprehensive unit tests.
- [x] Invalid paths are handled gracefully.
- [x] Existing Phase 0 tests continue to pass.
- [x] CLI can demonstrate scanner functionality.
- [x] Documentation is updated.
- [x] Project builds successfully with C++20.
- [x] Formatting and static analysis pass cleanly.
