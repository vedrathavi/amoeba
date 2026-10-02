# Phase 8.2 — Extensible, Deterministic & Secure Repository Exploration Tools

## 1. Motivation & Context

In Phases 8.0 and 8.1, Amoeba introduced grounded local LLM reasoning over pre-assembled evidence bundles. However, initial retrieval pipelines operate on a single-pass paradigm: if the initial retrieval query misses an adjacent helper function, an import, or an exact line span, the evidence sufficiency gate rightfully triggers a grounded refusal (`RefusalReason::InsufficientEvidence`).

Phase 8.2 establishes a **secure, deterministic, and extensible repository tool subsystem** allowing the reasoning layer to request additional repository evidence through strictly controlled tools without giving arbitrary filesystem access or execution privileges to the LLM.

```text
User Query
  ↓
Query Understanding
  ↓
Primary Retrieval
  ↓
Evidence Assembly
  ↓
Evidence Sufficiency Gate
  ├── Sufficient → ContextBuilder → LocalLLMRuntime → Grounded Answer
  │
  └── Insufficient → [Future Tool Loop (Phase 8.3+)]
                           ↓
                     Tool Request
                           ↓
                   Tool Dispatcher / Registry
                           ↓
                   Security Policy & Validation
                           ↓
                     Repository Tool
                           ↓
                      Tool Result
                           ↓
                    ToolEvidenceAdapter
                           ↓
                     EvidenceBundle
                           ↓
                   Evidence Sufficiency Gate
```

---

## 2. Scope Boundaries & Constraints

This phase is **NOT** an autonomous agent implementation. The following capabilities are explicitly out of scope:
- Autonomous multi-step agent loops
- Ollama-specific or OpenAI-specific tool calling JSON formats
- Conversational memory
- Multi-agent collaboration systems
- Shell or command execution
- Network or web access
- File modification or deletion
- Code compilation or evaluation
- Cloud/IDE plugin integration

Amoeba remains a code-understanding engine where the reasoning layer requests facts, but deterministic C++ validation gates govern what information is accessed, checked, and exposed.

---

## 3. Core Architecture & SOLID Design

```text
amoeba::tools
  ├── RepositoryTool (Abstract Tool Base)
  ├── ToolRegistry (Dispatcher & Collection Owner)
  ├── RepositoryAccessPolicy (Centralized Security Gatekeeper)
  ├── ToolExecutionContext (Least-Privilege Capability Bundle)
  ├── ToolRequest / ToolResult / ToolDescription (Immutable Value Types)
  ├── ToolEvidenceAdapter (Bridge to EvidenceBundle / EvidenceItem)
  └── Built-in Tool Adapters:
        ├── search_code (Adapts PrimaryRetrievalPipeline)
        ├── find_symbol (Adapts InvertedIndex & ParsedFiles)
        ├── read_file (Adapts SourceSnippetReader)
        └── get_relationships (Adapts RelationshipEvidenceResolver & Graph)
```

### OOP / SOLID Principles Applied
1. **Single Responsibility**: Each tool is a narrow adapter over an existing Amoeba subsystem. `RepositoryAccessPolicy` is the single authority for filesystem safety.
2. **Open/Closed Principle**: Adding a new tool (e.g. `get_callers`, `get_callees`, `get_tests`) requires writing one class implementing `RepositoryTool` and registering it in `ToolRegistry`. No central `switch` statements or conditional chains in reasoning or retrieval code are modified.
3. **Liskov Substitution**: Any `RepositoryTool` can be registered and dispatched through `ToolRegistry::execute` uniformly.
4. **Interface Segregation**: The tool interface contains only `name()`, `description()`, and `execute()`. Tools receive only read-only capabilities in `ToolExecutionContext`.
5. **Dependency Inversion**: Higher-level reasoning services depend on the `ToolRegistry` and `RepositoryTool` abstractions, not on concrete tool implementations. Concrete tools depend on existing low-level capabilities (`SourceSnippetReader`, `InvertedIndex`, `PrimaryRetrievalPipeline`).

---

## 4. Key Data Structures

### ToolRequest & ToolResult
- **`ToolRequest`**: Immutable value type containing `tool_name` and key-value string `arguments`.
- **`ToolResult`**: Structured result with explicit status enumeration:
  - `Success`: Executed cleanly with valid output.
  - `InvalidRequest`: Missing or malformed parameters.
  - `PermissionDenied`: Path outside repository root or sensitive credential match.
  - `NotFound`: Symbol or file missing.
  - `Error`: Subsystem failure or unavailable capability.

### ToolDescription & ToolParameter
- Self-describing schema metadata enabling future LLM runtime tool discovery without hardcoding tool schemas into the reasoning engine.

---

## 5. Centralized Security Model (`RepositoryAccessPolicy`)

The LLM is treated as an untrusted client. The tool execution boundary enforces three levels of security:

### 1. Strict Repository-Root Containment
- Rejects absolute paths (`C:\...`, `D:\...`, `/etc/...`, `/var/...`).
- Rejects path traversal (`../`, `..\`, `src/../../secret.txt`).
- Resolves normalized canonical paths and checks that target files reside strictly within `context.repository_root`.
- Reparse-point and symlink checks: canonical filesystem resolution prevents escaping repository roots via directory junctions or symbolic links.

### 2. Sensitive File & Credential Protection
Blocks reading common secret, certificate, and configuration files regardless of path:
- `.env`, `.env.*` (e.g., `.env.local`, `.env.production`)
- `*.pem`, `*.key`, `*.p12`, `*.pfx`, `*.jks`, `*.kdbx`
- `id_rsa`, `id_ed25519`
- `.ssh/`, `.git/` directories
- `secrets/`, `secret/`, `credentials/` paths
- Legitimate source files (such as `src/auth_service.cpp`, `src/token_manager.cpp`, `src/configuration_service.cpp`) are strictly permitted because matching targets secret filenames rather than arbitrary substrings.

### 3. Bounded Output Limits
- `read_file` enforces a hard cap of 150 lines per request and a maximum character payload of 8,000 characters to prevent context window exhaustion.

---

## 6. Built-in Repository Tools

| Tool Name | Parameters | Purpose | Adapted Subsystem |
| :--- | :--- | :--- | :--- |
| **`search_code`** | `query` (required), `mode` (`hybrid`, `code_aware`, `bm25`, `semantic`), `limit` (1-20) | Hybrid lexical + semantic code search | `PrimaryRetrievalPipeline` |
| **`find_symbol`** | `name` (required), `kind` (optional filter), `limit` (1-20) | Exact and partial symbol lookup | `InvertedIndex`, `ParsedFile` AST |
| **`read_file`** | `path` (required, repo-relative), `start_line` (default 1), `end_line` (default 50) | Read bounded source excerpt | `SourceSnippetReader` |
| **`get_relationships`** | `symbol` (required), `kind` (optional), `direction` (`both`, `incoming`, `outgoing`), `limit` (1-25) | 1-hop AST relationship discovery | `RelationshipEvidenceResolver`, `RelationshipGraph` |

---

## 7. Tool Evidence Identity

`ToolEvidenceAdapter` ensures that all tool-derived findings are converted into first-class `evidence::EvidenceItem` structures with unambiguous repository identity:

1. **`search_code`**: Preserves symbol name, element kind, repository-relative file path, exact `SourceRange`, hybrid/lexical score, and supporting relationships.
2. **`find_symbol`**: Preserves primary symbol name, element kind, parent context, repository-relative file path, and start/end source coordinates.
3. **`read_file`**: Preserves repository-relative path, explicit `start_line` and `end_line`, and exact `SourceExcerpt` text.
4. **`get_relationships`**: Preserves source symbol, relationship kind (`Calls`, `Imports`, `Defines`, `Implements`), edge direction, target symbol, and target location.

Output paths are strictly repository-relative (e.g. `src/hooks/useCalendar.ts:1-35`), preventing host machine path leakage.

---

## 8. Evidence Sufficiency After Tool Execution

Tool execution and evidence sufficiency are intentionally decoupled:

> **Critical Principle: Tool execution success ≠ evidence sufficiency.**
> **Critical Principle: Tool result ≠ proof.**
> **Critical Principle: LLM request ≠ authorization.**

A tool execution may succeed technically (e.g., `read_file("src/utils/format.ts")` returns HTTP 200 / Status `Success`), but when integrated into the `EvidenceBundle`, `EvidenceSufficiencyChecker` evaluates the combined evidence against query constraints. If the query asks for `"Where is JWT authentication implemented?"`, reading an unrelated utility does not grant sufficiency. The deterministic gate flags `is_sufficient = false` and enforces grounded refusal.

---

## 9. Security Invariants & Secret Protection

1. **The LLM is a caller, never an authorization authority**: Every tool request is validated by `RepositoryAccessPolicy` in C++ before any filesystem or index operation.
2. **Path Traversal Protection**: Relative escapes (`../../secret.txt`, `src/../.env`, `..\..\secret.txt`) and absolute paths (`C:\Users\...`, `/etc/passwd`) are rejected at the policy layer.
3. **Secret Isolation**: Sensitive config files (`.env*`, `id_rsa`, `*.pem`, `*.key`, `credentials`, `.git/config`) return `ToolResultStatus::PermissionDenied` immediately without disk reads.
4. **Output Sanitization**: Tools emit repository-relative paths only, ensuring no host usernames, absolute directories, internal memory addresses, or raw pointers leak to the prompt context.

---

## 10. Ambiguous Tool Results & Determinism

- **Deterministic Ambiguity in `find_symbol`**: When a symbol matches multiple declarations (e.g., function definition in `useCalendar.ts` and import in `page.tsx`), results are returned as a bounded, deterministically ordered list sorted by exact match first, then alphabetically by name, file path, and start line. No arbitrary first-element selection or hash map iteration order is permitted.
- **Kind Filtering**: Callers can supply `kind` (e.g. `hook`, `class`, `function`) to disambiguate identical identifiers.
- **Repeated Invocations**: Across repeated executions with identical parameters, all tools produce identical `ToolResult` payloads, status codes, and evidence bundle representations.

---

## 11. Checkpoint Validation & Regression Guarantees

The Phase 8.2 checkpoint verifies:
- **Total Test Suite**: **445 / 445 tests passing** (63 test suites, 0 warnings).
- **Phase 7.6 / 7.6.1 Grounding Regressions**: 100% passing.
  - Generic term hardening: Query `"Where is the GraphQL resolver implemented?"` does not accept `SupportingEvidenceResolver`.
  - Negative cases: Query `"Where is RBAC implemented?"` and `"Where is JWT authentication implemented?"` remain `RefusalReason::InsufficientEvidence`.
- **Vocabulary-Gap Recovery**: A natural language query (`"date picker hook logic"`) that is initially insufficient successfully recovers the concrete `useCalendar` implementation after tool lookup and excerpt reading.
- **Real Repositories**: Validated against `demo_test_projects/calendar` (TSX/Next.js) and Amoeba engine repository.
- **Extensibility Without Central Modification**: New tools register and execute dynamically through `ToolRegistry` without modifying `ReasoningService`, `PromptBuilder`, or `EvidenceSufficiencyChecker`.

---

## 12. Benchmark Latency Results

Measured on `demo_test_projects/calendar`:

| Subsystem / Operation | Target | Latency |
| :--- | :--- | :--- |
| **`search_code`** (Hybrid Retrieval) | `"calendar navigation"` | **115.169 ms** |
| **`find_symbol`** (Symbol Lookup) | `"useCalendar"` | **0.539 ms** (539 µs) |
| **`read_file`** (Excerpt Reader) | `"src/hooks/useCalendar.ts"` (L1-30) | **0.553 ms** (553 µs) |
| **`get_relationships`** (Graph Traversal) | `"useCalendar"` (1-hop edges) | **0.072 ms** (72 µs) |
| **`ToolEvidenceAdapter`** (Adaptation) | Tool result to `EvidenceBundle` | **0.012 ms** (12 µs) |
| **`EvidenceSufficiencyChecker`** (Evaluation) | Bundle sufficiency check | **0.024 ms** (24 µs) |

---

## 13. Known Limitations

1. **Autonomous Loop Deferred**: Multi-step tool-calling loops and budget managers are deferred to Phase 8.3.
2. **Prompt Serialization**: Tool schemas are exposed via `list_descriptions()` but not yet embedded in the LLM system prompt.
3. **Read-Only Capability**: Tools only inspect the repository and cannot alter files or execute binaries.

---

## 14. Future Tool Loop Contract (Phase 8.3)

The intended future contract for Phase 8.3:

```text
Initial retrieval
      ↓
Evidence Sufficiency Gate
      ↓
if insufficient
      ↓
LLM generates ToolRequest
      ↓
ToolRegistry validates & executes request
      ↓
ToolResult produced
      ↓
ToolEvidenceAdapter translates to EvidenceBundle
      ↓
Evidence Sufficiency Gate evaluated again
      ↓
sufficient   → PromptBuilder & Grounded Answer
insufficient → Bounded next request (up to max K tool iterations / budget)
```

Enforced limits for Phase 8.3 will include:
- Maximum tool invocations per query (e.g., $K \le 3$).
- Total evidence char budget limit.
- Loop termination on cycle or repeated invalid tool requests.

---

## 15. Recommendation

**Proceed to Phase 8.3 — Bounded Reasoning & Tool Calling Loop.**
All tool validation, evidence identity, deterministic sufficiency interaction, security isolation, and regression guarantees are fully verified.
