#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace amoeba::reasoning {

/**
 * @brief Types of events emitted during LLM response generation and streaming.
 */
enum class ReasoningEventType {
    TextChunk,  ///< Incremental piece of generated text.
    Completed,  ///< Generation finished successfully.
    Error       ///< Runtime or network error occurred during generation.
};

[[nodiscard]] constexpr std::string_view to_string(ReasoningEventType type) noexcept {
    switch (type) {
    case ReasoningEventType::TextChunk:
        return "TextChunk";
    case ReasoningEventType::Completed:
        return "Completed";
    case ReasoningEventType::Error:
        return "Error";
    }
    return "Unknown";
}

/**
 * @brief Discrete event in the reasoning response stream.
 */
struct ReasoningEvent {
    ReasoningEventType type{ReasoningEventType::TextChunk};
    std::string text_chunk;
    std::string error_message;

    [[nodiscard]] static ReasoningEvent text(std::string_view chunk) {
        return ReasoningEvent{
            .type = ReasoningEventType::TextChunk,
            .text_chunk = std::string(chunk),
            .error_message = "",
        };
    }

    [[nodiscard]] static ReasoningEvent completed(std::string_view final_text = "") {
        return ReasoningEvent{
            .type = ReasoningEventType::Completed,
            .text_chunk = std::string(final_text),
            .error_message = "",
        };
    }

    [[nodiscard]] static ReasoningEvent error(std::string_view message) {
        return ReasoningEvent{
            .type = ReasoningEventType::Error,
            .text_chunk = "",
            .error_message = std::string(message),
        };
    }

    bool operator==(const ReasoningEvent& other) const = default;
};

/**
 * @brief Interface for consuming streamed reasoning events.
 */
class ResponseSink {
public:
    virtual ~ResponseSink() = default;

    /**
     * @brief Receive an event from the LLM runtime.
     * @param event The reasoning event (text chunk, completion, or error).
     */
    virtual void on_event(const ReasoningEvent& event) = 0;
};

/**
 * @brief Standard callback-based response sink.
 */
class CallbackResponseSink final : public ResponseSink {
public:
    using Callback = std::function<void(const ReasoningEvent&)>;

    explicit CallbackResponseSink(Callback callback) : callback_(std::move(callback)) {}

    void on_event(const ReasoningEvent& event) override {
        if (callback_) {
            callback_(event);
        }
    }

private:
    Callback callback_;
};

/**
 * @brief Accumulating sink that captures all events and builds the aggregate text.
 */
class BufferingResponseSink final : public ResponseSink {
public:
    BufferingResponseSink() = default;

    void on_event(const ReasoningEvent& event) override {
        events_.push_back(event);
        if (event.type == ReasoningEventType::TextChunk) {
            accumulated_text_ += event.text_chunk;
        } else if (event.type == ReasoningEventType::Error) {
            has_error_ = true;
            error_message_ = event.error_message;
        } else if (event.type == ReasoningEventType::Completed) {
            is_completed_ = true;
        }
    }

    [[nodiscard]] const std::string& text() const noexcept { return accumulated_text_; }

    [[nodiscard]] const std::vector<ReasoningEvent>& events() const noexcept { return events_; }

    [[nodiscard]] bool has_error() const noexcept { return has_error_; }

    [[nodiscard]] const std::string& error_message() const noexcept { return error_message_; }

    [[nodiscard]] bool is_completed() const noexcept { return is_completed_; }

private:
    std::string accumulated_text_;
    std::vector<ReasoningEvent> events_;
    bool has_error_{false};
    std::string error_message_;
    bool is_completed_{false};
};

}  // namespace amoeba::reasoning
