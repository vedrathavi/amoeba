# Phase 8.1 — First Concrete Local LLM Provider Integration

## 1. Overview & Objective

Phase 8.1 completes the integration of Amoeba's first concrete local LLM runtime (`LocalLLMRuntime`), connecting the upstream context and reasoning pipeline with real local inference models while preserving all strict architectural boundaries.

The end-to-end question-answering path is:

```
User Question
    ↓
Amoeba Retrieval Pipeline (Lexical + Semantic Search)
    ↓
EvidenceAssembler (Primary AST + Excerpts + Relationships)
    ↓
EvidenceBundle
    ↓
ContextBuilder (Budgeting & Markdown Packing)
    ↓
ContextPackage
    ↓
ReasoningService
    ↓
LocalLLMRuntime (Concrete LLMRuntime)
    ↓
Local Inference Runner (e.g., Ollama / llama.cpp server / LM Studio)
    ↓
ReasoningEvent Stream (Incremental Text Chunks)
    ↓
Terminal CLI Output (`amoeba ask <repo> "<question>"`)
```

---

## 2. Selected Local Runtime & Rationale

### Selection: Local HTTP Streaming Engine (`LocalLLMRuntime`)
- **Protocol**: HTTP/1.1 JSON streaming over loopback (`http://127.0.0.1:11434` for Ollama or `http://127.0.0.1:8080` for `llama-server`).
- **Implementation**: Native Windows HTTP client (`WinHTTP`) linked via `winhttp.lib`, with no heavy third-party networking or vendored transformer C++ libraries.

### Why Selected
1. **Zero Bloat & Maximum Portability**: Avoids vendoring gigabytes of CUDA/ROCm/Metal toolchains or multi-target submodules inside the Amoeba build graph.
2. **True Token Streaming**: Native chunk-by-chunk event reading via HTTP streaming payloads (`"response": "..."`, `"done": bool`).
3. **Decoupled Lifecycle**: The model server runs in its own process, preventing inference segfaults or out-of-memory errors from crashing Amoeba.
4. **Developer Choice**: Developers can run Ollama, `llama.cpp` (`llama-server`), LM Studio, or vLLM locally on their GPU or CPU without changing a single line of Amoeba code.

---

## 3. Selected Model Strategy

### Default Model: `qwen2.5-coder:1.5b` (or `qwen2.5-coder:7b` / `llama3.2:1b`)
- **Model Family**: Qwen2.5-Coder / Llama 3.2
- **Format**: GGUF / Ollama Model
- **Approximate Size**: ~1.0 GB to 1.9 GB (1.5B 4-bit/8-bit quant)
- **Context Length**: 32k tokens supported (Amoeba budgets context safely within 4k-8k tokens)
- **Local Requirements**:
  - CPU: Runs at >20-30 tokens/sec on modern x86_64 CPUs.
  - GPU (Optional): Fast sub-second responses with Vulkan/DirectML/CUDA.
  - RAM: Requires <2.5 GB RAM.
- **Acquisition**:
  ```bash
  ollama pull qwen2.5-coder:1.5b
  ```
- **Repository Boundary**: Model weights are strictly external; no model files or binaries are committed to Git.

---

## 4. Architecture & Component Responsibilities

```
+-------------------------------------------------------------------------+
|                              ReasoningService                           |
+-------------------------------------------------------------------------+
                                    |
                            (LLMRuntime interface)
                                    v
+-------------------------------------------------------------------------+
|                              LocalLLMRuntime                            |
|  - config: LocalLLMConfig (endpoint, model_name, max_tokens, temp)      |
|  - generate_stream(request, sink) -> ReasoningEvent stream              |
|  - generate(request) -> LLMResponse                                     |
+-------------------------------------------------------------------------+
                                    |
                            (WinHTTP stream)
                                    v
+-------------------------------------------------------------------------+
|                  Local Model Server (Ollama / llama-server)             |
|                        http://127.0.0.1:11434                           |
+-------------------------------------------------------------------------+
```

### Core Responsibilities
- `LocalLLMRuntime`:
  - Translates `LLMRequest` (user query + `ContextPackage`) into local engine prompts via `PromptBuilder`.
  - Sends streaming request over loopback HTTP.
  - Parses streaming JSON chunks and emits `ReasoningEvent::text(chunk)` to the `ResponseSink`.
  - Emits `ReasoningEvent::completed(text)` or `ReasoningEvent::error(msg)`.
- **Isolation**:
  - `LocalLLMRuntime` has NO access to Tree-sitter, AST nodes, indexing structures, or graph internals.
  - Consumes only `ContextPackage`.

---

## 5. CLI Usage & Experience

### Command
```bash
amoeba ask <repository-path> "<question>" [--model=<model_name>] [--endpoint=<url>]
```

### Example
```bash
amoeba ask demo_test_projects/calendar "Where is calendar state managed?"
```

### Terminal Output Example
```text
Amoeba
Source Code Search & Indexing Engine

Repository:
  demo_test_projects/calendar

Question:
  "Where is calendar state managed?"

Grounding Context:
  Retrieved 3 primary code units (1932 chars) in 108 ms
  Local LLM Model: qwen2.5-coder:1.5b (http://127.0.0.1:11434)

Answer:
Calendar state is managed primarily within `useCalendar.ts` (specifically in the `useCalendar` hook).
The hook initializes and manages the active month, selected date, and view mode state.

[Inference time: 420 ms]
```

---

## 6. Error Handling

The runtime provides clear, human-readable error messages without crashing:

| Failure Scenario | Output / Handled Behavior |
| :--- | :--- |
| Local runner offline / unreachable | `[Error] Failed to connect or send request to local LLM at http://127.0.0.1:11434 (Error 12029). Check that local model server is reachable.` |
| Model not pulled | `[Error] Local LLM endpoint returned HTTP status 404 (Model 'qwen2.5-coder:1.5b' may not be pulled or loaded. Run 'ollama pull qwen2.5-coder:1.5b').` |
| Invalid / empty query | Cleanly validated before pipeline invocation. |
| Non-existent repository | Repository scanner throws and reports missing path cleanly. |

---

## 7. Latency & Resource Observation

Amoeba explicitly separates retrieval/context preparation latency from LLM inference latency:

- **Amoeba Retrieval + Evidence + Context Latency**: ~50ms - 120ms (on calendar project).
- **LLM Time-to-First-Token (TTFT)**: ~50ms - 200ms (GPU/CPU).
- **LLM Generation Latency**: ~300ms - 1500ms depending on token count.
- **Memory Footprint**: Amoeba engine adds < 10 MB overhead. Local model server memory managed independently in external process.

---

## 8. Test Coverage

- **Automated Unit Tests**: 360/360 tests passing (100%).
  - Deterministic reasoning tests continue using `FakeLLMRuntime`.
  - `LocalLLMRuntimeTest` tests configuration, lifecycle, and unreachable endpoint error handling deterministically.
- **0 Compiler Warnings**: Clean compilation with LLVM Clang++.
- **Code Formatting**: Fully formatted with `clang-format`.

---

## 9. Implemented vs. Deferred

### IMPLEMENTED
- `LocalLLMConfig` and `LocalLLMRuntime` implementing `LLMRuntime`.
- Streaming token parser and `ResponseSink` integration.
- Native loopback HTTP streaming client with error diagnosis.
- CLI `amoeba ask` command with real-time streaming terminal output.
- Latency breakdown reporting.
- 360 unit/integration tests passing.

### EXPLICITLY DEFERRED (Future Phases)
- Remote cloud API providers (OpenAI, Anthropic, Gemini) -> Phase 8.2+
- Multi-turn conversation memory and session state -> Future Phase
- Tool calling / Agentic execution loops -> Future Phase
- Autonomous file modification / patch application -> Future Phase
