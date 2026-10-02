#include "amoeba/tools/read_file_tool.hpp"

#include "amoeba/source/source_snippet_reader.hpp"
#include "amoeba/tools/repository_access_policy.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace amoeba::tools {

namespace {

constexpr std::size_t kMaxLineSpan = 150;
constexpr std::size_t kMaxOutputCharacters = 8000;

}  // namespace

ToolDescription ReadFileTool::description() const {
    return ToolDescription{
        .name = "read_file",
        .description = "Reads a bounded line range of a source file strictly within the repository.",
        .parameters = {
            ToolParameter{
                .name = "path",
                .type = "string",
                .description = "Repository-relative path of the file to read.",
                .required = true,
                .default_value = "",
            },
            ToolParameter{
                .name = "start_line",
                .type = "integer",
                .description = "Starting line number (1-indexed, default 1).",
                .required = false,
                .default_value = "1",
            },
            ToolParameter{
                .name = "end_line",
                .type = "integer",
                .description = "Ending line number (inclusive, max 150 lines from start_line).",
                .required = false,
                .default_value = "50",
            },
        },
    };
}

ToolResult ReadFileTool::execute(const ToolRequest& request,
                                 const ToolExecutionContext& context) const {
    ToolResult res;
    res.tool_name = name();

    if (context.repository_root.empty()) {
        res.status = ToolResultStatus::Error;
        res.error_code = "CAPABILITY_UNAVAILABLE";
        res.summary = "Repository root directory is not configured in execution context.";
        return res;
    }

    const auto path_opt = request.get_arg("path");
    if (!path_opt.has_value() || path_opt->empty()) {
        res.status = ToolResultStatus::InvalidRequest;
        res.error_code = "MISSING_ARGUMENT";
        res.summary = "Missing or empty required argument 'path'.";
        return res;
    }

    const std::filesystem::path req_path(*path_opt);

    // 1. Centralized security validation
    RepositoryAccessPolicy default_policy;
    const auto& policy = (context.access_policy != nullptr) ? *context.access_policy : default_policy;

    const auto decision = policy.check_read(context.repository_root, req_path);
    if (decision != AccessDecision::Allowed) {
        res.status = ToolResultStatus::PermissionDenied;
        res.error_code = std::string(to_string(decision));
        if (decision == AccessDecision::PathOutsideRepository) {
            res.summary = "Access denied: Path escapes repository boundary or is absolute.";
        } else if (decision == AccessDecision::SensitiveFile) {
            res.summary = "Access denied: Requested path is a sensitive credentials or key file.";
        } else {
            res.summary = "Access denied: Invalid path format.";
        }
        return res;
    }

    // 2. Resolve safe absolute path
    const auto safe_path_opt = policy.resolve_safe_path(context.repository_root, req_path);
    if (!safe_path_opt.has_value()) {
        res.status = ToolResultStatus::PermissionDenied;
        res.error_code = "PERMISSION_DENIED";
        res.summary = "Failed to resolve safe repository path.";
        return res;
    }
    const auto abs_path = *safe_path_opt;

    // 3. Line limits validation
    const std::string start_line_str = request.get_arg_or("start_line", "1");
    const std::string end_line_str = request.get_arg_or("end_line", "50");

    uint32_t start_line = 1;
    uint32_t end_line = 50;
    try {
        start_line = static_cast<uint32_t>(std::stoul(start_line_str));
        end_line = static_cast<uint32_t>(std::stoul(end_line_str));
    } catch (...) {
        res.status = ToolResultStatus::InvalidRequest;
        res.error_code = "INVALID_LINE_ARGUMENT";
        res.summary = "Invalid line numbers provided: start_line or end_line is not a valid integer.";
        return res;
    }

    if (start_line < 1) {
        res.status = ToolResultStatus::InvalidRequest;
        res.error_code = "LINE_RANGE_INVALID";
        res.summary = "Invalid start_line (" + std::to_string(start_line) + "): line numbers are 1-indexed.";
        return res;
    }

    if (end_line < start_line) {
        res.status = ToolResultStatus::InvalidRequest;
        res.error_code = "LINE_RANGE_INVALID";
        res.summary = "Invalid line range: end_line (" + std::to_string(end_line) +
                      ") is less than start_line (" + std::to_string(start_line) + ").";
        return res;
    }

    if ((end_line - start_line + 1) > kMaxLineSpan) {
        res.status = ToolResultStatus::InvalidRequest;
        res.error_code = "LINE_RANGE_TOO_LARGE";
        res.summary = "Requested line range (" + std::to_string(end_line - start_line + 1) +
                      " lines) exceeds maximum allowed span of " + std::to_string(kMaxLineSpan) +
                      " lines.";
        return res;
    }

    // 4. Known repository files check (if provided in context)
    if (context.known_indexed_files != nullptr && !context.known_indexed_files->empty()) {
        const std::string norm_rel = req_path.lexically_normal().generic_string();
        if (!context.known_indexed_files->contains(norm_rel)) {
            if (!std::filesystem::exists(abs_path) || !std::filesystem::is_regular_file(abs_path)) {
                res.status = ToolResultStatus::NotFound;
                res.error_code = "FILE_NOT_FOUND";
                res.summary = "File not found in repository: \"" + req_path.generic_string() + "\".";
                return res;
            }
        }
    } else {
        if (!std::filesystem::exists(abs_path) || !std::filesystem::is_regular_file(abs_path)) {
            res.status = ToolResultStatus::NotFound;
            res.error_code = "FILE_NOT_FOUND";
            res.summary = "File not found in repository: \"" + req_path.generic_string() + "\".";
            return res;
        }
    }

    // 5. Read source snippet
    source::SourceSnippetReader default_reader;
    const auto& reader = (context.snippet_reader != nullptr) ? *context.snippet_reader : default_reader;

    parser::SourceRange range;
    range.start.line = start_line;
    range.start.column = 1;
    range.end.line = end_line;
    range.end.column = 1000;

    const auto excerpt_opt = reader.try_read_range(abs_path, range, 0);
    if (!excerpt_opt.has_value()) {
        res.status = ToolResultStatus::NotFound;
        res.error_code = "LINE_RANGE_OUT_OF_BOUNDS";
        res.summary = "Line range L" + std::to_string(start_line) + "-L" + std::to_string(end_line) +
                      " is out of bounds for file \"" + req_path.generic_string() + "\".";
        return res;
    }

    std::string text = excerpt_opt->text;
    bool truncated = false;
    if (text.size() > kMaxOutputCharacters) {
        text.resize(kMaxOutputCharacters);
        text += "\n// ... [output truncated at 8,000 characters]";
        truncated = true;
    }

    res.status = ToolResultStatus::Success;
    res.summary = "Read " + std::to_string(excerpt_opt->end_line - excerpt_opt->start_line + 1) +
                  " lines from " + req_path.generic_string() + " (L" +
                  std::to_string(excerpt_opt->start_line) + "-L" +
                  std::to_string(excerpt_opt->end_line) + ").";

    res.metadata.push_back({"file_path", req_path.generic_string()});
    res.metadata.push_back({"start_line", std::to_string(excerpt_opt->start_line)});
    res.metadata.push_back({"end_line", std::to_string(excerpt_opt->end_line)});
    if (truncated) {
        res.metadata.push_back({"truncated", "true"});
    }

    std::ostringstream ss;
    ss << "File: " << req_path.generic_string() << " (Lines " << excerpt_opt->start_line << "-"
       << excerpt_opt->end_line << "):\n";
    ss << "```\n";
    ss << text << "\n";
    ss << "```\n";

    res.content = ss.str();
    return res;
}

}  // namespace amoeba::tools
