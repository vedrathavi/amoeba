#include "amoeba/reasoning/local_llm_runtime.hpp"

#include "amoeba/reasoning/prompt_builder.hpp"

#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace amoeba::reasoning {

namespace {

struct ParsedUrl {
    std::string host{"127.0.0.1"};
    int port{11434};
    std::string path{"/api/generate"};
    bool is_https{false};
    bool valid{true};
    std::string error_message;
};

[[nodiscard]] ParsedUrl parse_endpoint_url(std::string_view endpoint,
                                           std::string_view default_path) {
    ParsedUrl res;
    if (endpoint.empty()) {
        res.host = "127.0.0.1";
        res.port = 11434;
        res.path = std::string(default_path);
        res.valid = true;
        return res;
    }

    std::string ep(endpoint);

    // 1. Explicit HTTPS detection and rejection (plain loopback HTTP only in Phase B/B.1)
    if (ep.rfind("https://", 0) == 0) {
        res.is_https = true;
        res.valid = false;
        res.error_message = "HTTPS endpoints are not supported by LocalLLMRuntime (plain loopback HTTP only: " +
                            std::string(endpoint) + ").";
        return res;
    }

    if (ep.rfind("http://", 0) == 0) {
        ep = ep.substr(7);
    } else {
        auto scheme_sep = ep.find("://");
        if (scheme_sep != std::string::npos) {
            res.valid = false;
            res.error_message = "Unsupported URL scheme in endpoint: " + std::string(endpoint);
            return res;
        }
    }

    // 2. Separate host:port from path
    std::string host_port = ep;
    std::string path_part = std::string(default_path);

    auto slash_pos = ep.find('/');
    if (slash_pos != std::string::npos) {
        host_port = ep.substr(0, slash_pos);
        if (slash_pos + 1 < ep.size()) {
            path_part = ep.substr(slash_pos);
        }
    }

    if (host_port.empty()) {
        res.valid = false;
        res.error_message = "Empty host in endpoint: " + std::string(endpoint);
        return res;
    }

    // 3. Parse host and port: Handle IPv6 literals in brackets vs IPv4 / hostname
    if (host_port.front() == '[') {
        auto close_bracket = host_port.find(']');
        if (close_bracket == std::string::npos) {
            res.valid = false;
            res.error_message = "Malformed IPv6 endpoint (missing closing bracket): " + std::string(endpoint);
            return res;
        }
        if (close_bracket == 1) {
            res.valid = false;
            res.error_message = "Empty IPv6 address in brackets: " + std::string(endpoint);
            return res;
        }

        std::string ipv6_host = host_port.substr(1, close_bracket - 1);
        int port_num = 11434;

        if (close_bracket + 1 < host_port.size()) {
            if (host_port[close_bracket + 1] != ':') {
                res.valid = false;
                res.error_message = "Malformed IPv6 endpoint (expected ':' before port): " + std::string(endpoint);
                return res;
            }
            std::string port_str = host_port.substr(close_bracket + 2);
            if (port_str.empty()) {
                res.valid = false;
                res.error_message = "Missing port after colon in endpoint: " + std::string(endpoint);
                return res;
            }
            try {
                std::size_t idx = 0;
                long p = std::stol(port_str, &idx);
                if (idx != port_str.size() || p <= 0 || p > 65535) {
                    res.valid = false;
                    res.error_message = "Invalid port in endpoint: " + std::string(endpoint);
                    return res;
                }
                port_num = static_cast<int>(p);
            } catch (...) {
                res.valid = false;
                res.error_message = "Invalid port in endpoint: " + std::string(endpoint);
                return res;
            }
        }

        res.host = ipv6_host;
        res.port = port_num;
        res.path = path_part.empty() ? "/" : path_part;
        res.valid = true;
        return res;
    }

    // Check for unbracketed IPv6 literal with multiple colons
    auto first_colon = host_port.find(':');
    if (first_colon != std::string::npos) {
        auto second_colon = host_port.find(':', first_colon + 1);
        if (second_colon != std::string::npos) {
            res.valid = false;
            res.error_message = "IPv6 endpoint addresses must be enclosed in brackets (e.g. http://[::1]:11434): " +
                                std::string(endpoint);
            return res;
        }
    }

    // Standard IPv4 or hostname with optional :port
    std::string host_str = host_port;
    int port_num = 11434;

    if (first_colon != std::string::npos) {
        host_str = host_port.substr(0, first_colon);
        std::string port_str = host_port.substr(first_colon + 1);
        if (port_str.empty()) {
            res.valid = false;
            res.error_message = "Missing port after colon in endpoint: " + std::string(endpoint);
            return res;
        }
        try {
            std::size_t idx = 0;
            long p = std::stol(port_str, &idx);
            if (idx != port_str.size() || p <= 0 || p > 65535) {
                res.valid = false;
                res.error_message = "Invalid port in endpoint: " + std::string(endpoint);
                return res;
            }
            port_num = static_cast<int>(p);
        } catch (...) {
            res.valid = false;
            res.error_message = "Invalid port in endpoint: " + std::string(endpoint);
            return res;
        }
    }

    if (host_str.empty()) {
        res.valid = false;
        res.error_message = "Empty host in endpoint: " + std::string(endpoint);
        return res;
    }

    res.host = host_str;
    res.port = port_num;
    res.path = path_part.empty() ? "/" : path_part;
    res.valid = true;
    return res;
}

/**
 * @brief Bounded, line-oriented NDJSON parser for local LLM token streams.
 *
 * Requirements:
 * - Line-buffering: preserves partial lines between chunk callbacks.
 * - Multi-record parsing: processes all complete lines in a chunk.
 * - UTF-8 safety: newlines (0x0A) never occur in multi-byte UTF-8 sequences,
 *   preventing cross-boundary character slicing.
 * - Trailing flush: parses non-newline-terminated final line at EOF.
 * - Validates JSON structure, 'response' string, 'done' boolean.
 * - Handles provider error objects ('error').
 * - Detects malformed records, buffer overflow, and premature EOF.
 * - Rejects unexpected records after terminal 'done: true'.
 * - 1 MB buffer limit per incomplete record (DoS defense).
 * - Allocates dynamically via std::string and nlohmann::json (not zero-allocation).
 */
class NdjsonStreamParser {
public:
    static constexpr std::size_t kMaxRecordBufferSize = 1024 * 1024;  // 1 MB limit per incomplete record

    enum class ErrorType {
        None,
        BufferOverflow,
        MalformedJson,
        InvalidFieldType,
        ProviderError,
        UnexpectedRecordAfterDone
    };

    explicit NdjsonStreamParser(ResponseSink& sink) : sink_(sink) {}

    bool feed(const char* data, std::size_t length) {
        if (has_fatal_error_) {
            return false;
        }

        std::size_t offset = 0;
        while (offset < length) {
            // Find next newline in the unconsumed slice
            const char* nl = static_cast<const char*>(std::memchr(data + offset, '\n', length - offset));
            if (nl) {
                std::size_t piece_len = static_cast<std::size_t>(nl - (data + offset));
                if (stream_buffer_.size() + piece_len > kMaxRecordBufferSize) {
                    record_error(ErrorType::BufferOverflow, "Individual NDJSON record exceeds 1 MB limit.");
                    return false;
                }
                stream_buffer_.append(data + offset, piece_len);
                offset += piece_len + 1;  // skip past '\n'

                if (!process_line(stream_buffer_)) {
                    return false;
                }
                stream_buffer_.clear();
            } else {
                // No newline in remaining data
                std::size_t remaining = length - offset;
                if (stream_buffer_.size() + remaining > kMaxRecordBufferSize) {
                    record_error(ErrorType::BufferOverflow, "Individual NDJSON record exceeds 1 MB limit.");
                    return false;
                }
                stream_buffer_.append(data + offset, remaining);
                offset += remaining;
            }
        }
        return true;
    }

    bool finish() {
        if (has_fatal_error_) {
            return false;
        }

        // Flush final record if non-empty (trailing record without trailing newline)
        if (!stream_buffer_.empty()) {
            if (!process_line(stream_buffer_)) {
                return false;
            }
            stream_buffer_.clear();
        }

        // Detect premature EOF without completion
        if (!is_done_) {
            record_error(ErrorType::MalformedJson,
                         "Local LLM stream terminated prematurely before generation completed.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool has_fatal_error() const noexcept { return has_fatal_error_; }
    [[nodiscard]] bool is_done() const noexcept { return is_done_; }
    [[nodiscard]] const std::string& accumulated_response() const noexcept { return accumulated_response_; }
    [[nodiscard]] const std::string& error_message() const noexcept { return error_message_; }
    [[nodiscard]] ErrorType error_type() const noexcept { return error_type_; }

private:
    bool process_line(std::string_view line) {
        // Strip trailing '\r' if present
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            return true;
        }

        // Reject unexpected records after terminal completion record
        if (is_done_) {
            record_error(ErrorType::UnexpectedRecordAfterDone,
                         "Received unexpected record after terminal completion record.");
            return false;
        }

        // Parse JSON non-throwingly
        auto j = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded()) {
            record_error(ErrorType::MalformedJson, "Malformed JSON record received in NDJSON stream.");
            return false;
        }

        if (!j.is_object()) {
            record_error(ErrorType::MalformedJson, "NDJSON record is not a JSON object.");
            return false;
        }

        // Handle provider error objects independently
        if (j.contains("error")) {
            std::string err_desc;
            if (j["error"].is_string()) {
                err_desc = j["error"].get<std::string>();
            } else if (j["error"].is_object() && j["error"].contains("message") &&
                       j["error"]["message"].is_string()) {
                err_desc = j["error"]["message"].get<std::string>();
            } else {
                err_desc = j["error"].dump();
            }
            record_error(ErrorType::ProviderError, "Provider error: " + err_desc);
            return false;
        }

        // Validate and extract response field
        if (j.contains("response")) {
            if (!j["response"].is_string()) {
                record_error(ErrorType::InvalidFieldType,
                             "Field 'response' in NDJSON record is not a string.");
                return false;
            }
            std::string token = j["response"].get<std::string>();
            if (!token.empty()) {
                accumulated_response_ += token;
                sink_.on_event(ReasoningEvent::text(token));
            }
        }

        // Validate and extract done field
        if (j.contains("done")) {
            if (!j["done"].is_boolean()) {
                record_error(ErrorType::InvalidFieldType, "Field 'done' in NDJSON record is not a boolean.");
                return false;
            }
            if (j["done"].get<bool>()) {
                is_done_ = true;
            }
        }

        return true;
    }

    void record_error(ErrorType type, std::string message) {
        has_fatal_error_ = true;
        error_type_ = type;
        error_message_ = std::move(message);
        sink_.on_event(ReasoningEvent::error(error_message_));
    }

    ResponseSink& sink_;
    std::string stream_buffer_;
    std::string accumulated_response_;
    std::string error_message_;
    ErrorType error_type_{ErrorType::None};
    bool is_done_{false};
    bool has_fatal_error_{false};
};

}  // namespace

struct LocalLLMRuntime::Impl {
    mutable std::mutex config_mutex;
};

LocalLLMRuntime::LocalLLMRuntime() : LocalLLMRuntime(LocalLLMConfig{}) {}

LocalLLMRuntime::LocalLLMRuntime(LocalLLMConfig config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

LocalLLMRuntime::~LocalLLMRuntime() = default;

LocalLLMRuntime::LocalLLMRuntime(LocalLLMRuntime&&) noexcept = default;
LocalLLMRuntime& LocalLLMRuntime::operator=(LocalLLMRuntime&&) noexcept = default;

std::string_view LocalLLMRuntime::runtime_name() const noexcept {
    return "local-llm-runtime";
}

const LocalLLMConfig& LocalLLMRuntime::config() const noexcept {
    return config_;
}

void LocalLLMRuntime::set_config(LocalLLMConfig config) {
    std::lock_guard lock(impl_->config_mutex);
    config_ = std::move(config);
}

bool LocalLLMRuntime::is_endpoint_reachable() const noexcept {
    try {
        LocalLLMConfig cfg;
        {
            std::lock_guard lock(impl_->config_mutex);
            cfg = config_;
        }
        ParsedUrl parsed = parse_endpoint_url(cfg.endpoint, "/");
        if (!parsed.valid) {
            return false;
        }
        httplib::Client cli(parsed.host, parsed.port);
        cli.set_connection_timeout(std::chrono::milliseconds(2000));
        cli.set_read_timeout(std::chrono::milliseconds(2000));
        auto res = cli.Get(parsed.path);
        return (res != nullptr);
    } catch (...) {
        return false;
    }
}

LLMResponse LocalLLMRuntime::generate(const LLMRequest& request) {
    BufferingResponseSink sink;
    generate_stream(request, sink);

    LLMResponse resp;
    {
        std::lock_guard lock(impl_->config_mutex);
        resp.model_name = config_.model_name;
    }
    resp.content = sink.text();
    resp.success = !sink.has_error();
    resp.error_message = sink.error_message();
    resp.finish_reason = sink.has_error() ? "error" : "stop";
    return resp;
}

void LocalLLMRuntime::generate_stream(const LLMRequest& request, ResponseSink& sink) {
    // 0. Safely snapshot configuration under lock; release before any network operations
    LocalLLMConfig cfg;
    {
        std::lock_guard lock(impl_->config_mutex);
        cfg = config_;
    }

    // 1. Parse and validate endpoint URL (rejecting HTTPS and malformed formats early)
    ParsedUrl parsed = parse_endpoint_url(cfg.endpoint, "/api/generate");
    if (!parsed.valid) {
        sink.on_event(ReasoningEvent::error(parsed.error_message));
        return;
    }

    // 2. Prepare prompts
    const std::string system_prompt =
        request.system_prompt_override.has_value() && !request.system_prompt_override->empty()
            ? *request.system_prompt_override
            : PromptBuilder::build_system_prompt();

    const std::string user_prompt =
        PromptBuilder::build_user_prompt(request.user_question, request.context_package);

    // 3. Build JSON payload for Ollama /api/generate using nlohmann::json
    nlohmann::json payload;
    payload["model"] = cfg.model_name;
    payload["system"] = system_prompt;
    payload["prompt"] = user_prompt;
    payload["stream"] = true;
    payload["options"] = {
        {"temperature", cfg.temperature},
        {"num_predict", request.max_tokens.value_or(cfg.max_tokens)}
    };

    const std::string json_body = payload.dump();

    // 4. Connect to local endpoint via cpp-httplib
    httplib::Client cli(parsed.host, parsed.port);

    const auto timeout_duration = std::chrono::milliseconds(cfg.timeout_ms);
    // Connection timeout floor (at least 5s) prevents slow CI runner scheduling
    // from triggering a premature connection failure during read timeout checks.
    const auto conn_timeout = std::chrono::milliseconds(std::max<uint32_t>(cfg.timeout_ms, 5000));
    cli.set_connection_timeout(conn_timeout);
    cli.set_read_timeout(timeout_duration);
    cli.set_write_timeout(conn_timeout);

    httplib::Headers headers = {
        {"Content-Type", "application/json"}
    };

    NdjsonStreamParser parser(sink);
    bool http_status_error = false;

    httplib::Request req;
    req.method = "POST";
    req.path = parsed.path;
    req.headers = headers;
    req.body = json_body;
    req.set_header("Content-Type", "application/json");

    req.response_handler = [&](const httplib::Response& response) -> bool {
        if (response.status != 200) {
            http_status_error = true;
            std::string err_msg = "Local LLM endpoint returned HTTP status " +
                                  std::to_string(response.status) + " (Model '" + cfg.model_name +
                                  "' may not be pulled or loaded. Run 'ollama pull " +
                                  cfg.model_name + "').";
            sink.on_event(ReasoningEvent::error(err_msg));
            return false;  // Abort reading body
        }
        return true;
    };

    req.content_receiver = [&](const char* data, size_t data_length, size_t /*offset*/,
                               size_t /*total*/) -> bool {
        return parser.feed(data, data_length);
    };

    auto res = cli.send(req);

    // If response handler aborted due to non-200 HTTP status, error event was already emitted
    if (http_status_error) {
        return;
    }

    // If parser encountered fatal error (e.g. malformed JSON, provider error, buffer limit),
    // preserve the parser's specific error event rather than overwriting with generic transport failure
    if (parser.has_fatal_error()) {
        return;
    }

    // Handle transport errors reported by cpp-httplib
    if (!res) {
        std::string err_msg;
        auto err = res.error();
        if (err == httplib::Error::Connection) {
            err_msg = "Failed to connect to local LLM at " + cfg.endpoint +
                      " (Connection refused or host unreachable). Please verify your local model runner is running.";
        } else if (err == httplib::Error::Read) {
            err_msg = "Read timeout waiting for response from local LLM at " + cfg.endpoint +
                      " (" + std::to_string(cfg.timeout_ms) + " ms).";
        } else if (err == httplib::Error::Write) {
            err_msg = "Failed to send request to local LLM at " + cfg.endpoint +
                      " (Write error). Check that local model server is reachable.";
        } else {
            err_msg = "Failed to connect or communicate with local LLM at " + cfg.endpoint +
                      " (" + httplib::to_string(err) + ").";
        }
        sink.on_event(ReasoningEvent::error(err_msg));
        return;
    }

    // 5. Flush trailing record at EOF and verify completion
    if (!parser.finish()) {
        // If finish() failed (e.g. premature EOF without 'done: true'), error event was emitted
        return;
    }

    // 6. Successful completion: only reached if all records parsed and done: true was received
    sink.on_event(ReasoningEvent::completed(parser.accumulated_response()));
}

}  // namespace amoeba::reasoning
