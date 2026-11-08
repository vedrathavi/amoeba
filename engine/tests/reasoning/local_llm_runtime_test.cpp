#include "amoeba/reasoning/local_llm_runtime.hpp"
#include "amoeba/reasoning/reasoning_service.hpp"
#include "amoeba/reasoning/response_sink.hpp"

#include <gtest/gtest.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace amoeba;
using namespace amoeba::context;
using namespace amoeba::reasoning;

class LocalLLMRuntimeTest : public ::testing::Test {
protected:
    ContextPackage create_dummy_context_package() {
        ContextPackage pkg;
        pkg.query = "useCalendar state";
        pkg.total_available_items = 1;
        pkg.selected_item_count = 1;
        pkg.rendered_markdown = "## Result 1: `useCalendar`\n- **Kind**: Hook\n- **File**: "
                                "src/hooks/useCalendar.ts\n\n```ts\nexport function useCalendar() "
                                "{ return {}; }\n```\n";
        pkg.used_characters = pkg.rendered_markdown.size();
        return pkg;
    }
};

/**
 * @brief Deterministic in-process mock Ollama server for offline testing.
 *
 * Uses ephemeral port allocation (binds to port 0) and explicit thread lifecycle
 * to guarantee zero test interference and zero port collisions.
 */
class MockOllamaServer {
public:
    MockOllamaServer() {
        port_ = svr_.bind_to_any_port("127.0.0.1");
        server_thread_ = std::thread([this]() { svr_.listen_after_bind(); });
        while (!svr_.is_running()) {
            std::this_thread::yield();
        }
    }

    ~MockOllamaServer() {
        stop();
    }

    void stop() {
        if (svr_.is_running()) {
            svr_.stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    [[nodiscard]] int port() const noexcept { return port_; }

    [[nodiscard]] std::string endpoint() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    httplib::Server& server() noexcept { return svr_; }

private:
    httplib::Server svr_;
    int port_{0};
    std::thread server_thread_;
};

// =============================================================================
// Existing Baseline Tests (Preserved)
// =============================================================================

// 1. LocalLLMConfig default initialization and mutation
TEST_F(LocalLLMRuntimeTest, ConfigurationDefaultsAndCustomization) {
    LocalLLMConfig config;
    EXPECT_EQ(config.endpoint, "http://127.0.0.1:11434");
    EXPECT_EQ(config.model_name, "qwen2.5-coder:1.5b");
    EXPECT_DOUBLE_EQ(config.temperature, 0.0);
    EXPECT_EQ(config.max_tokens, 1024u);
    EXPECT_EQ(config.timeout_ms, 60000u);

    config.endpoint = "http://127.0.0.1:8080";
    config.model_name = "llama3.2:1b";
    config.temperature = 0.2;
    config.max_tokens = 512;

    LocalLLMRuntime runtime(config);
    EXPECT_EQ(runtime.config().endpoint, "http://127.0.0.1:8080");
    EXPECT_EQ(runtime.config().model_name, "llama3.2:1b");
    EXPECT_DOUBLE_EQ(runtime.config().temperature, 0.2);
    EXPECT_EQ(runtime.config().max_tokens, 512u);
    EXPECT_EQ(runtime.runtime_name(), "local-llm-runtime");
}

// 2. LocalLLMRuntime move semantics
TEST_F(LocalLLMRuntimeTest, MoveSemanticsAndLifecycle) {
    LocalLLMConfig cfg;
    cfg.model_name = "custom-local-model";

    LocalLLMRuntime r1(cfg);
    LocalLLMRuntime r2(std::move(r1));

    EXPECT_EQ(r2.config().model_name, "custom-local-model");
    EXPECT_EQ(r2.runtime_name(), "local-llm-runtime");
}

// 3. Error handling when local server endpoint is unreachable
TEST_F(LocalLLMRuntimeTest, UnreachableEndpointErrorHandling) {
    // Point to an inactive loopback port (e.g. 59999)
    LocalLLMConfig cfg;
    cfg.endpoint = "http://127.0.0.1:59999";
    cfg.timeout_ms = 1000;  // 1s timeout

    LocalLLMRuntime runtime(cfg);
    ReasoningService service(runtime);

    const auto pkg = create_dummy_context_package();

    // Synchronous execution test
    const auto resp = service.answer("Where is useCalendar?", pkg);
    EXPECT_FALSE(resp.ok());
    EXPECT_FALSE(resp.success);
    EXPECT_EQ(resp.finish_reason, "error");
    EXPECT_NE(resp.error_message.find("Failed to connect"), std::string::npos);

    // Streaming execution test
    BufferingResponseSink sink;
    service.answer_stream("Where is useCalendar?", pkg, sink);
    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("Failed to connect"), std::string::npos);
}

// 4. Endpoint reachable probe against inactive port
TEST_F(LocalLLMRuntimeTest, EndpointReachableProbe) {
    LocalLLMConfig cfg;
    cfg.endpoint = "http://127.0.0.1:59998";  // Inactive port
    LocalLLMRuntime runtime(cfg);

    EXPECT_FALSE(runtime.is_endpoint_reachable());
}

// =============================================================================
// Stage 4 Mock Server Tests
// =============================================================================

// 5. Normal streaming generation and successful completion
TEST_F(LocalLLMRuntimeTest, MockServerNormalStreamingAndCompletion) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider(
            "application/x-ndjson", [](std::size_t offset, httplib::DataSink& sink) {
                if (offset == 0) {
                    std::string l1 = "{\"response\":\"Hello\",\"done\":false}\n";
                    sink.write(l1.data(), l1.size());
                    std::string l2 = "{\"response\":\" World\",\"done\":false}\n";
                    sink.write(l2.data(), l2.size());
                    std::string l3 = "{\"response\":\"!\",\"done\":true}\n";
                    sink.write(l3.data(), l3.size());
                    sink.done();
                }
                return true;
            });
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    cfg.timeout_ms = 5000;
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    req.user_question = "Hi";
    runtime.generate_stream(req, sink);

    EXPECT_FALSE(sink.has_error());
    EXPECT_TRUE(sink.is_completed());
    EXPECT_EQ(sink.text(), "Hello World!");

    // Test synchronous generate() wrapper as well
    auto resp = runtime.generate(req);
    EXPECT_TRUE(resp.ok());
    EXPECT_TRUE(resp.success);
    EXPECT_EQ(resp.finish_reason, "stop");
    EXPECT_EQ(resp.content, "Hello World!");
}

// 6. Split records across multiple callback chunks
TEST_F(LocalLLMRuntimeTest, MockServerSplitRecordsAcrossChunks) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider(
            "application/x-ndjson", [](std::size_t offset, httplib::DataSink& sink) {
                if (offset == 0) {
                    // Split record across 3 separate packet writes
                    std::string p1 = "{\"response\":\"split";
                    sink.write(p1.data(), p1.size());
                    std::string p2 = "_token_piece";
                    sink.write(p2.data(), p2.size());
                    std::string p3 = "\",\"done\":false}\n{\"response\":\"_done\",\"done\":true}\n";
                    sink.write(p3.data(), p3.size());
                    sink.done();
                }
                return true;
            });
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_FALSE(sink.has_error());
    EXPECT_TRUE(sink.is_completed());
    EXPECT_EQ(sink.text(), "split_token_piece_done");
}

// 7. Multiple records packed into a single callback chunk
TEST_F(LocalLLMRuntimeTest, MockServerPackedRecordsInSingleChunk) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            "{\"response\":\"Token1\",\"done\":false}\n"
            "{\"response\":\" \",\"done\":false}\n"
            "{\"response\":\"Token2\",\"done\":false}\n"
            "{\"response\":\".\",\"done\":true}\n",
            "application/x-ndjson");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_FALSE(sink.has_error());
    EXPECT_TRUE(sink.is_completed());
    EXPECT_EQ(sink.text(), "Token1 Token2.");
}

// 8. UTF-8 multi-byte characters split across callback boundaries
TEST_F(LocalLLMRuntimeTest, MockServerUtf8SplitAcrossCallbacks) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider(
            "application/x-ndjson", [](std::size_t offset, httplib::DataSink& sink) {
                if (offset == 0) {
                    // "日本語" in UTF-8: \xE6\x97\xA5 \xE6\x9C\xAC \xE8\xAA\x9E
                    // Split right inside the first 3-byte character: \xE6\x97 in chunk 1, \xA5... in chunk 2
                    std::string p1 = "{\"response\":\"\xE6\x97";
                    sink.write(p1.data(), p1.size());
                    std::string p2 = "\xA5\xE6\x9C\xAC\xE8\xAA\x9E\",\"done\":false}\n"
                                     "{\"response\":\"\",\"done\":true}\n";
                    sink.write(p2.data(), p2.size());
                    sink.done();
                }
                return true;
            });
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_FALSE(sink.has_error());
    EXPECT_TRUE(sink.is_completed());
    EXPECT_EQ(sink.text(), "日本語");
}

// 9. Final record flushed at EOF without trailing newline
TEST_F(LocalLLMRuntimeTest, MockServerFinalRecordWithoutTrailingNewline) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        // No '\n' at the end of the final JSON object
        res.set_content(
            "{\"response\":\"NoNewline\",\"done\":true}",
            "application/x-ndjson");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_FALSE(sink.has_error());
    EXPECT_TRUE(sink.is_completed());
    EXPECT_EQ(sink.text(), "NoNewline");
}

// 10. Provider error object during streaming
TEST_F(LocalLLMRuntimeTest, MockServerProviderErrorDuringStreaming) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            "{\"response\":\"Beginning\",\"done\":false}\n"
            "{\"error\":\"model 'qwen2.5-coder' ran out of memory\"}\n",
            "application/x-ndjson");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("Provider error"), std::string::npos);
    EXPECT_NE(sink.error_message().find("ran out of memory"), std::string::npos);
}

// 11. HTTP 404 (Model not found) and HTTP 500 (Internal server error)
TEST_F(LocalLLMRuntimeTest, MockServerHttpStatusErrors) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.status = 404;
        res.set_content("Model not found", "text/plain");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    cfg.model_name = "non-existent-model";
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("HTTP status 404"), std::string::npos);
    EXPECT_NE(sink.error_message().find("non-existent-model"), std::string::npos);
}

// 12. Malformed JSON record in stream
TEST_F(LocalLLMRuntimeTest, MockServerMalformedJsonRecord) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            "{\"response\":\"Good\",\"done\":false}\n"
            "{invalid_json_line}\n",
            "application/x-ndjson");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("Malformed JSON record"), std::string::npos);
}

// 13. Invalid field types in JSON records
TEST_F(LocalLLMRuntimeTest, MockServerInvalidFieldTypes) {
    // Sub-case A: 'response' is an integer instead of a string
    {
        MockOllamaServer mock;
        mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(
                "{\"response\":12345,\"done\":false}\n",
                "application/x-ndjson");
        });

        LocalLLMConfig cfg;
        cfg.endpoint = mock.endpoint();
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        EXPECT_NE(sink.error_message().find("Field 'response' in NDJSON record is not a string"),
                  std::string::npos);
    }

    // Sub-case B: 'done' is a string instead of a boolean
    {
        MockOllamaServer mock;
        mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(
                "{\"response\":\"Hi\",\"done\":\"true\"}\n",
                "application/x-ndjson");
        });

        LocalLLMConfig cfg;
        cfg.endpoint = mock.endpoint();
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        EXPECT_NE(sink.error_message().find("Field 'done' in NDJSON record is not a boolean"),
                  std::string::npos);
    }
}

// 14. Missing completion marker (premature EOF)
TEST_F(LocalLLMRuntimeTest, MockServerMissingCompletionMarkerPrematureEof) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        // Stream terminates with done: false
        res.set_content(
            "{\"response\":\"Incomplete generation\",\"done\":false}\n",
            "application/x-ndjson");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("stream terminated prematurely"), std::string::npos);
}

// 15. Read timeout when server stalls
TEST_F(LocalLLMRuntimeTest, MockServerReadTimeout) {
    MockOllamaServer mock;
    std::atomic<bool> test_finished{false};
    mock.server().Post("/api/generate", [&](const httplib::Request&, httplib::Response&) {
        // Stall server with short incremental sleeps until test finishes or 1500 ms max
        for (int i = 0; i < 75 && !test_finished.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    cfg.timeout_ms = 250;  // 250 ms read timeout
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    test_finished = true;

    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("Read timeout"), std::string::npos);
}

// 16. Incomplete record exceeding 1 MB buffer limit
TEST_F(LocalLLMRuntimeTest, MockServerIncompleteRecordExceedsBufferLimit) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        // Stream a chunk without newline that exceeds 1 MB
        std::string giant(1024 * 1024 + 100, 'A');
        res.set_content(giant, "application/x-ndjson");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("exceeds 1 MB limit"), std::string::npos);
}

// 17. Large callback containing many valid records exceeding 1 MB in total
TEST_F(LocalLLMRuntimeTest, MockServerLargeCallbackWithManyValidRecordsExceeding1MB) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        // Construct 2,500 valid records of ~500 bytes each (total ~1.25 MB)
        std::string batch;
        batch.reserve(2500 * 520);
        std::string padding(450, 'x');
        for (int i = 0; i < 2499; ++i) {
            batch += "{\"response\":\"t\",\"padding\":\"" + padding + "\",\"done\":false}\n";
        }
        batch += "{\"response\":\"end\",\"done\":true}\n";
        res.set_content(batch, "application/x-ndjson");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    cfg.timeout_ms = 10000;
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    // Because each individual record is small, the 1 MB buffer limit is not exceeded!
    EXPECT_FALSE(sink.has_error());
    EXPECT_TRUE(sink.is_completed());
    EXPECT_EQ(sink.text().size(), 2499 + 3);  // 2499 't's + "end"
}

// 18. Unexpected records after completion record
TEST_F(LocalLLMRuntimeTest, MockServerUnexpectedRecordsAfterCompletion) {
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            "{\"response\":\"Finished\",\"done\":true}\n"
            "{\"response\":\"Ghost\",\"done\":false}\n",
            "application/x-ndjson");
    });

    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_TRUE(sink.has_error());
    EXPECT_FALSE(sink.is_completed());
    EXPECT_NE(sink.error_message().find("unexpected record after terminal completion"),
              std::string::npos);
}

// 19. Endpoint reachability against live server (both 200 and 404/405)
TEST_F(LocalLLMRuntimeTest, MockServerEndpointReachability) {
    MockOllamaServer mock;
    // Server has no GET / handler by default, so it returns 404 Not Found on GET /
    LocalLLMConfig cfg;
    cfg.endpoint = mock.endpoint();
    LocalLLMRuntime runtime(cfg);

    // Any HTTP response (including 404) confirms the HTTP daemon is listening
    EXPECT_TRUE(runtime.is_endpoint_reachable());

    // Register a 200 OK handler on GET /
    mock.server().Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content("OK", "text/plain");
    });
    EXPECT_TRUE(runtime.is_endpoint_reachable());

    // Stop mock server; now it must be unreachable
    mock.stop();
    EXPECT_FALSE(runtime.is_endpoint_reachable());
}

// 20. IPv6 endpoint parsing and validation
TEST_F(LocalLLMRuntimeTest, IPv6EndpointParsingAndDiagnostics) {
    // A. Valid IPv6 endpoints targeting an inactive loopback port (59997)
    // Should parse cleanly and attempt connection, reporting "Failed to connect" rather than a URL error.
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "http://[::1]:59997";
        cfg.timeout_ms = 1000;
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        // Must report network connection failure, confirming URL parsing succeeded!
        EXPECT_NE(sink.error_message().find("Failed to connect"), std::string::npos);
    }

    // B. Malformed IPv6: missing closing bracket
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "http://[::1:11434";
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        EXPECT_NE(sink.error_message().find("missing closing bracket"), std::string::npos);
        EXPECT_FALSE(runtime.is_endpoint_reachable());
    }

    // C. Malformed IPv6: unbracketed IPv6 literal
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "http://::1:11434";
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        EXPECT_NE(sink.error_message().find("must be enclosed in brackets"), std::string::npos);
        EXPECT_FALSE(runtime.is_endpoint_reachable());
    }

    // D. Malformed IPv6: empty brackets
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "http://[]:11434";
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        EXPECT_NE(sink.error_message().find("Empty IPv6 address in brackets"), std::string::npos);
    }

    // E. Invalid ports
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "http://[::1]:99999";  // > 65535
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        EXPECT_NE(sink.error_message().find("Invalid port in endpoint"), std::string::npos);
    }
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "http://[::1]:";  // missing port after colon
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        EXPECT_NE(sink.error_message().find("Missing port after colon"), std::string::npos);
    }
}

// 21. Explicit rejection of HTTPS endpoints
TEST_F(LocalLLMRuntimeTest, HttpsEndpointExplicitRejection) {
    // A. Streaming generation rejection
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "https://127.0.0.1:11434";
        LocalLLMRuntime runtime(cfg);

        BufferingResponseSink sink;
        LLMRequest req;
        req.user_question = "Hello";
        runtime.generate_stream(req, sink);

        EXPECT_TRUE(sink.has_error());
        EXPECT_FALSE(sink.is_completed());
        EXPECT_NE(sink.error_message().find("HTTPS endpoints are not supported by LocalLLMRuntime"),
                  std::string::npos);
    }

    // B. Synchronous generate rejection
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "https://localhost:8080";
        LocalLLMRuntime runtime(cfg);

        LLMRequest req;
        req.user_question = "Hello";
        auto resp = runtime.generate(req);

        EXPECT_FALSE(resp.ok());
        EXPECT_FALSE(resp.success);
        EXPECT_EQ(resp.finish_reason, "error");
        EXPECT_NE(resp.error_message.find("HTTPS endpoints are not supported by LocalLLMRuntime"),
                  std::string::npos);
    }

    // C. Reachability probe returns false immediately without network call
    {
        LocalLLMConfig cfg;
        cfg.endpoint = "https://example.com:11434";
        LocalLLMRuntime runtime(cfg);

        EXPECT_FALSE(runtime.is_endpoint_reachable());
    }
}

// 22. Safe configuration snapshotting prevents data races during concurrent set_config
TEST_F(LocalLLMRuntimeTest, ConcurrentConfigurationMutationDuringGeneration) {
    std::atomic<bool> server_streaming{false};
    MockOllamaServer mock;
    mock.server().Post("/api/generate", [&](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider(
            "application/x-ndjson", [&](std::size_t offset, httplib::DataSink& sink) {
                if (offset == 0) {
                    server_streaming = true;
                    for (int i = 0; i < 5; ++i) {
                        std::string chunk = "{\"response\":\"tok" + std::to_string(i) + "\",\"done\":false}\n";
                        sink.write(chunk.data(), chunk.size());
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    std::string end = "{\"response\":\"!\",\"done\":true}\n";
                    sink.write(end.data(), end.size());
                    sink.done();
                }
                return true;
            });
    });

    LocalLLMConfig initial_cfg;
    initial_cfg.endpoint = mock.endpoint();
    initial_cfg.model_name = "initial-model";
    initial_cfg.timeout_ms = 5000;
    LocalLLMRuntime runtime(initial_cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    req.user_question = "Test question";

    std::atomic<bool> generation_finished{false};

    // Thread 1: Executes generation
    std::thread gen_thread([&]() {
        runtime.generate_stream(req, sink);
        generation_finished = true;
    });

    // Wait until mock server is actively streaming chunks to the in-flight request
    while (!server_streaming.load()) {
        std::this_thread::yield();
    }

    // Thread 2 (Main Thread): Rapidly mutates configuration while generation is in flight
    for (int i = 0; i < 50 && !generation_finished.load(); ++i) {
        LocalLLMConfig mutated_cfg;
        mutated_cfg.endpoint = "http://127.0.0.1:" + std::to_string(60000 + i);
        mutated_cfg.model_name = "mutated-model-" + std::to_string(i);
        mutated_cfg.temperature = 0.5;
        mutated_cfg.timeout_ms = 3000;
        runtime.set_config(mutated_cfg);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    gen_thread.join();

    // Generation must have completed cleanly using the snapshotted initial configuration
    EXPECT_FALSE(sink.has_error());
    EXPECT_TRUE(sink.is_completed());
    EXPECT_EQ(sink.text(), "tok0tok1tok2tok3tok4!");
}

// 23. Mock server live streaming over IPv6 [::1] (if OS supports IPv6 loopback)
TEST_F(LocalLLMRuntimeTest, MockServerIPv6LiveEndpoint) {
    httplib::Server svr;
    int port = svr.bind_to_any_port("::1");
    if (port <= 0) {
        // OS environment does not support IPv6 loopback; skip gracefully
        GTEST_SKIP() << "IPv6 loopback [::1] is not supported or not enabled in this environment.";
    }

    svr.Post("/api/generate", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"response\":\"ipv6-ok\",\"done\":true}\n", "application/x-ndjson");
    });

    std::thread th([&]() { svr.listen_after_bind(); });
    while (!svr.is_running()) {
        std::this_thread::yield();
    }

    LocalLLMConfig cfg;
    cfg.endpoint = "http://[::1]:" + std::to_string(port);
    LocalLLMRuntime runtime(cfg);

    BufferingResponseSink sink;
    LLMRequest req;
    runtime.generate_stream(req, sink);

    EXPECT_FALSE(sink.has_error());
    EXPECT_TRUE(sink.is_completed());
    EXPECT_EQ(sink.text(), "ipv6-ok");

    svr.stop();
    if (th.joinable()) {
        th.join();
    }
}

}  // namespace
