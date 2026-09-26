# Amoeba — Phase 8.0: Reasoning Architecture & LLM Runtime Foundation

## 1. Why Phase 8 Exists

Through Phases 3–7, Amoeba established a deterministic code intelligence pipeline:
- **Lexical & Semantic Retrieval** (`retrieval/`): Identifies relevant primary code units.
- **Source Snippet Extraction** (`source/`): Recovers verbatim source ranges.
- **Relationship Graph** (`graph/`): Resolves structural code relationships (`Calls`, `Imports`, `InheritsFrom`).
- **Evidence Assembly** (`evidence/`): Combines factual information into an `EvidenceBundle`.
- **Context Construction & Budgeting** (`context/`): Selects, orders, and truncates evidence into a bounded `ContextPackage`.

Phase 8 introduces the **Reasoning Layer**, connecting Amoeba's structured context to Large Language Models:
```
ContextPackage  ──►  ReasoningService  ──►  LLMRuntime  ──►  LLMResponse / Stream
```

---

## 2. Core Architectural Boundary

> **The Fundamental Principle**:
> - **Amoeba owns repository knowledge and truth.**
> - **The LLM owns reasoning and explanation over Amoeba's evidence.**
> - The LLM must **NOT** become the source of repository truth, and must **NOT** independently parse, scan, or search the repository.

```
                         USER
                           │
                           ▼
                    ReasoningService
                           │
             ┌─────────────┼─────────────┐
             │             │             │
             ▼             ▼             ▼
         Context       Conversation    Tools
         Package         Context      (FUTURE)
             │          (FUTURE)
             └──────┬──────┘
                    ▼
                LLMRuntime  (DIP Interface)
                /         \
               /           \
          Local Model     API Model
         (llama.cpp,     (OpenAI,
          ONNX GenAI)     Anthropic)
                    │
                    ▼
              ReasoningEvent Stream
               (TextChunk, Completed, Error)
                    │
               ┌────┴────┐
               ▼         ▼
              CLI      Future UI / IDE
```

---

## 3. Package & Module Structure

The reasoning subsystem lives under dedicated, modular directories:
- `engine/include/amoeba/reasoning/`
  - `llm_types.hpp` — Minimal request and response value types.
  - `response_sink.hpp` — Stream event types (`ReasoningEvent`) and sink interfaces (`ResponseSink`, `CallbackResponseSink`, `BufferingResponseSink`).
  - `prompt_builder.hpp` — Deterministic grounding prompt constructor.
  - `llm_runtime.hpp` — Abstract runtime interface for execution backends.
  - `fake_llm_runtime.hpp` — Deterministic runtime for offline testing and verification.
  - `reasoning_service.hpp` — Orchestration service connecting context to runtime.
- `engine/src/reasoning/`
  - `prompt_builder.cpp`
  - `fake_llm_runtime.cpp`
  - `reasoning_service.cpp`
- `engine/tests/reasoning/`
  - `reasoning_service_test.cpp`

---

## 4. Components & Responsibilities

### 4.1. LLM Request & Response Value Types (`llm_types.hpp`)
Minimal, focused value types containing only what is currently necessary:
- `LLMRequest`: Contains `user_question`, `context_package` (`context::ContextPackage`), optional `system_prompt_override`, `temperature`, and `max_tokens`.
- `LLMResponse`: Contains `content`, `model_name`, `success`, `error_message`, and `finish_reason`.

### 4.2. Streaming-Ready Event Boundary (`response_sink.hpp`)
- `ReasoningEventType`: `TextChunk`, `Completed`, `Error`.
- `ReasoningEvent`: Discrete stream event representing an incremental token chunk, completion state, or runtime error.
- `ResponseSink`: Interface for stream consumers (e.g. CLI interactive typing, GUI token streams).

### 4.3. PromptBuilder (`prompt_builder.hpp`)
Enforces strict factual grounding through standard instructions:
1. Base all answers strictly on the supplied `Repository Evidence Context`.
2. Do **NOT** invent, assume, or hallucinate missing files, symbols, or relationships.
3. If the evidence is insufficient or the requested component does not exist in the codebase, explicitly state:
   > *"I couldn't find sufficient evidence in the repository to determine this."*
4. Cite verbatim file paths and symbol names from the context.
5. Clearly distinguish verbatim facts from inference.

### 4.4. LLMRuntime Abstraction (`llm_runtime.hpp`)
Defines the backend contract:
```cpp
class LLMRuntime {
public:
    virtual ~LLMRuntime() = default;
    virtual LLMResponse generate(const LLMRequest& request) = 0;
    virtual void generate_stream(const LLMRequest& request, ResponseSink& sink) = 0;
    virtual std::string_view runtime_name() const noexcept = 0;
};
```

### 4.5. ReasoningService (`reasoning_service.hpp`)
Coordinates the reasoning pipeline without coupling to specific backends:
```cpp
LLMResponse answer(const std::string& question, const context::ContextPackage& context_package);
void answer_stream(const std::string& question, const context::ContextPackage& context_package, ResponseSink& sink);
```

---

## 5. OOP & SOLID Design Decisions

| Principle | Implementation |
| :--- | :--- |
| **SRP (Single Responsibility)** | `PromptBuilder` formats prompts; `ReasoningService` orchestrates execution; `LLMRuntime` executes inference; `ResponseSink` handles streaming consumption. |
| **OCP (Open-Closed)** | New backends (Local `llama.cpp`, Cloud OpenAI/Anthropic/Gemini) can be added by implementing `LLMRuntime` without changing `ReasoningService`, `ContextBuilder`, or retrieval components. |
| **LSP (Liskov Substitution)** | Any `LLMRuntime` implementation satisfies synchronous and streaming execution contracts identically. |
| **ISP (Interface Segregation)** | `LLMRuntime` exposes only 2 essential generation methods; `ResponseSink` exposes a single `on_event` method. |
| **DIP (Dependency Inversion)** | `ReasoningService` depends on abstract `LLMRuntime`, never on concrete provider classes. |

---

## 6. Implemented Now vs. Designed for Future

### Implemented in Phase 8.0:
- [x] Dedicated `reasoning/` subsystem and clean namespace `amoeba::reasoning`.
- [x] Value types `LLMRequest` and `LLMResponse`.
- [x] `ReasoningEvent` and `ResponseSink` streaming event flow.
- [x] `PromptBuilder` with strict grounding and insufficient-evidence policies.
- [x] `LLMRuntime` abstract interface.
- [x] `FakeLLMRuntime` deterministic test mock.
- [x] `ReasoningService` orchestration.
- [x] Unit test suite covering synchronous execution, streaming chunks, error propagation, and runtime substitution.

### Explicitly Deferred to Subsequent Checkpoints:
- **Conversation Memory & Context**: Multi-turn history, session context compression, and conversation-level references.
- **Repository Tools**: Controlled repository operations (`read_file`, `find_symbol`, `get_relationships`) with path-canonicalization security bounds.
- **Agents & Multi-Agent Coordination**: Tool loops, autonomous planning, and multi-agent supervisors.
- **Concrete Cloud / Local Provider SDKs**: libcurl HTTP client, ONNX GenAI bindings, llama.cpp bindings.

---

## 7. Verification & Quality Gates

```
Total Test Suites: 53
Total Tests: 356 / 356 (100% passing)
Compiler Warnings: 0
clang-format: clean
git diff --check: clean
```

The reasoning subsystem cleanly consumes `ContextPackage` with zero backwards dependency leakage into Tree-sitter, `InvertedIndex`, `SemanticIndex`, or `RelationshipGraph`.
