#include "amoeba/reasoning/local_llm_runtime.hpp"

#include "amoeba/reasoning/prompt_builder.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace amoeba::reasoning {

namespace {

[[nodiscard]] std::string escape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

[[nodiscard]] std::string unescape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[++i];
            switch (next) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
                if (i + 4 < s.size()) {
                    // Simple hex decode for basic ASCII range
                    std::string hex_str(s.substr(i + 1, 4));
                    try {
                        auto val = static_cast<unsigned long>(std::stoul(hex_str, nullptr, 16));
                        if (val < 128) {
                            out += static_cast<char>(val);
                        } else {
                            out += '?';
                        }
                    } catch (...) {
                        out += '?';
                    }
                    i += 4;
                }
                break;
            default:
                out += next;
                break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

struct ParsedUrl {
    std::wstring host{L"127.0.0.1"};
    INTERNET_PORT port{11434};
    std::wstring path{L"/api/generate"};
    bool is_https{false};
};

[[nodiscard]] ParsedUrl parse_endpoint_url(std::string_view endpoint,
                                           std::string_view default_path) {
    ParsedUrl res;
    std::string ep(endpoint);

    bool https = false;
    if (ep.rfind("https://", 0) == 0) {
        https = true;
        ep = ep.substr(8);
    } else if (ep.rfind("http://", 0) == 0) {
        ep = ep.substr(7);
    }
    res.is_https = https;

    std::string host_port = ep;
    std::string path_part = std::string(default_path);

    auto slash_pos = ep.find('/');
    if (slash_pos != std::string::npos) {
        host_port = ep.substr(0, slash_pos);
        if (slash_pos + 1 < ep.size()) {
            path_part = ep.substr(slash_pos);
        }
    }

    std::string host_str = host_port;
    INTERNET_PORT port_num = https ? 443 : 11434;

    auto colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        host_str = host_port.substr(0, colon_pos);
        try {
            port_num = static_cast<INTERNET_PORT>(std::stoi(host_port.substr(colon_pos + 1)));
        } catch (...) {
        }
    }

    res.host = std::wstring(host_str.begin(), host_str.end());
    res.port = port_num;
    res.path = std::wstring(path_part.begin(), path_part.end());
    return res;
}

// Parses JSON key value: "key": "value" or "key": true/false
[[nodiscard]] std::string extract_json_string(std::string_view json, std::string_view key) {
    std::string pattern = "\"" + std::string(key) + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) {
        return "";
    }
    pos += pattern.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return "";
    }
    ++pos;
    std::size_t start = pos;
    while (pos < json.size()) {
        if (json[pos] == '"' && json[pos - 1] != '\\') {
            break;
        }
        ++pos;
    }
    return unescape_json(json.substr(start, pos - start));
}

[[nodiscard]] bool extract_json_bool(std::string_view json, std::string_view key) {
    std::string pattern = "\"" + std::string(key) + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) {
        return false;
    }
    pos += pattern.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) {
        ++pos;
    }
    if (pos + 4 <= json.size() && json.substr(pos, 4) == "true") {
        return true;
    }
    return false;
}

}  // namespace

struct LocalLLMRuntime::Impl {
#if defined(_WIN32)
    HINTERNET session{nullptr};

    Impl() {
        session = WinHttpOpen(L"Amoeba-Local-LLM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }

    ~Impl() {
        if (session) {
            WinHttpCloseHandle(session);
            session = nullptr;
        }
    }
#endif
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
    config_ = std::move(config);
}

bool LocalLLMRuntime::is_endpoint_reachable() const noexcept {
#if defined(_WIN32)
    if (!impl_ || !impl_->session) {
        return false;
    }
    ParsedUrl parsed = parse_endpoint_url(config_.endpoint, "/api/tags");
    HINTERNET connect = WinHttpConnect(impl_->session, parsed.host.c_str(), parsed.port, 0);
    if (!connect) {
        return false;
    }
    HINTERNET req =
        WinHttpOpenRequest(connect, L"GET", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, parsed.is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(connect);
        return false;
    }

    DWORD timeout = 2000;  // 2s quick probe
    WinHttpSetTimeouts(req, timeout, timeout, timeout, timeout);

    BOOL sent =
        WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    BOOL received = sent ? WinHttpReceiveResponse(req, nullptr) : FALSE;

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    return (received == TRUE);
#else
    return false;
#endif
}

LLMResponse LocalLLMRuntime::generate(const LLMRequest& request) {
    BufferingResponseSink sink;
    generate_stream(request, sink);

    LLMResponse resp;
    resp.model_name = config_.model_name;
    resp.content = sink.text();
    resp.success = !sink.has_error();
    resp.error_message = sink.error_message();
    resp.finish_reason = sink.has_error() ? "error" : "stop";
    return resp;
}

void LocalLLMRuntime::generate_stream(const LLMRequest& request, ResponseSink& sink) {
#if defined(_WIN32)
    if (!impl_ || !impl_->session) {
        sink.on_event(ReasoningEvent::error("Local HTTP client session initialization failed."));
        return;
    }

    // 1. Prepare prompts
    const std::string system_prompt =
        request.system_prompt_override.has_value() && !request.system_prompt_override->empty()
            ? *request.system_prompt_override
            : PromptBuilder::build_system_prompt();

    const std::string user_prompt =
        PromptBuilder::build_user_prompt(request.user_question, request.context_package);

    // 2. Build JSON payload for Ollama /api/generate
    std::ostringstream body_ss;
    body_ss << "{\n"
            << "  \"model\": \"" << escape_json(config_.model_name) << "\",\n"
            << "  \"system\": \"" << escape_json(system_prompt) << "\",\n"
            << "  \"prompt\": \"" << escape_json(user_prompt) << "\",\n"
            << "  \"stream\": true,\n"
            << "  \"options\": {\n"
            << "    \"temperature\": " << config_.temperature << ",\n"
            << "    \"num_predict\": " << (request.max_tokens.value_or(config_.max_tokens)) << "\n"
            << "  }\n"
            << "}";

    const std::string json_body = body_ss.str();

    // 3. Connect to local endpoint
    ParsedUrl parsed = parse_endpoint_url(config_.endpoint, "/api/generate");
    HINTERNET connect = WinHttpConnect(impl_->session, parsed.host.c_str(), parsed.port, 0);
    if (!connect) {
        const DWORD err = GetLastError();
        std::string err_msg = "Failed to connect to local LLM at " + config_.endpoint +
                              " (Error code " + std::to_string(err) +
                              "). Please verify your local model runner is running.";
        sink.on_event(ReasoningEvent::error(err_msg));
        return;
    }

    HINTERNET http_req =
        WinHttpOpenRequest(connect, L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, parsed.is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!http_req) {
        WinHttpCloseHandle(connect);
        sink.on_event(ReasoningEvent::error("Failed to open local HTTP request."));
        return;
    }

    DWORD timeout = config_.timeout_ms;
    WinHttpSetTimeouts(http_req, timeout, timeout, timeout, timeout);

    const std::wstring headers = L"Content-Type: application/json\r\n";
    BOOL send_ok = WinHttpSendRequest(http_req, headers.c_str(), static_cast<DWORD>(headers.size()),
                                      const_cast<char*>(json_body.data()),
                                      static_cast<DWORD>(json_body.size()),
                                      static_cast<DWORD>(json_body.size()), 0);
    if (!send_ok) {
        const DWORD err = GetLastError();
        WinHttpCloseHandle(http_req);
        WinHttpCloseHandle(connect);
        std::string err_msg = "Failed to connect or send request to local LLM at " +
                              config_.endpoint + " (Error " + std::to_string(err) +
                              "). Check that local model server is reachable.";
        sink.on_event(ReasoningEvent::error(err_msg));
        return;
    }

    BOOL recv_ok = WinHttpReceiveResponse(http_req, nullptr);
    if (!recv_ok) {
        WinHttpCloseHandle(http_req);
        WinHttpCloseHandle(connect);
        sink.on_event(
            ReasoningEvent::error("Local LLM request failed to receive response header."));
        return;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(http_req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                        WINHTTP_NO_HEADER_INDEX);

    if (status_code != 200) {
        WinHttpCloseHandle(http_req);
        WinHttpCloseHandle(connect);
        std::string err_msg = "Local LLM endpoint returned HTTP status " +
                              std::to_string(status_code) + " (Model '" + config_.model_name +
                              "' may not be pulled or loaded. Run 'ollama pull " +
                              config_.model_name + "').";
        sink.on_event(ReasoningEvent::error(err_msg));
        return;
    }

    // 4. Stream response tokens
    std::string stream_buffer;
    std::string accumulated_response;
    char buffer[4096];
    DWORD bytes_read = 0;

    while (WinHttpReadData(http_req, buffer, sizeof(buffer) - 1, &bytes_read) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        stream_buffer.append(buffer, bytes_read);

        std::size_t newline_pos = 0;
        while ((newline_pos = stream_buffer.find('\n')) != std::string::npos) {
            std::string line = stream_buffer.substr(0, newline_pos);
            stream_buffer.erase(0, newline_pos + 1);

            if (line.empty() || line == "\r") {
                continue;
            }

            // Extract "response" from Ollama JSON line
            std::string chunk = extract_json_string(line, "response");
            if (!chunk.empty()) {
                accumulated_response += chunk;
                sink.on_event(ReasoningEvent::text(chunk));
            }

            bool done = extract_json_bool(line, "done");
            if (done) {
                break;
            }
        }
    }

    WinHttpCloseHandle(http_req);
    WinHttpCloseHandle(connect);

    sink.on_event(ReasoningEvent::completed(accumulated_response));
#else
    sink.on_event(ReasoningEvent::error("Local LLM runtime currently requires Windows WinHTTP."));
#endif
}

}  // namespace amoeba::reasoning
