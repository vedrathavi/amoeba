#include "amoeba/tools/search_code_tool.hpp"

#include "amoeba/parser/code_element.hpp"
#include "amoeba/retrieval/primary_retrieval_pipeline.hpp"

#include <algorithm>
#include <sstream>

namespace amoeba::tools {

ToolDescription SearchCodeTool::description() const {
    return ToolDescription{
        .name = "search_code",
        .description = "Searches the repository for relevant code elements using hybrid lexical "
                       "and semantic retrieval.",
        .parameters = {
            ToolParameter{
                .name = "query",
                .type = "string",
                .description = "Search query, symbol name, or technical question.",
                .required = true,
                .default_value = "",
            },
            ToolParameter{
                .name = "mode",
                .type = "string",
                .description =
                    "Retrieval mode ('hybrid', 'code_aware', 'bm25', 'semantic', 'baseline').",
                .required = false,
                .default_value = "hybrid",
            },
            ToolParameter{
                .name = "limit",
                .type = "integer",
                .description = "Maximum number of primary units to return (1-20).",
                .required = false,
                .default_value = "5",
            },
        },
    };
}

ToolResult SearchCodeTool::execute(const ToolRequest& request,
                                   const ToolExecutionContext& context) const {
    ToolResult res;
    res.tool_name = name();

    const auto query_opt = request.get_arg("query");
    if (!query_opt.has_value() || query_opt->empty()) {
        res.status = ToolResultStatus::InvalidRequest;
        res.error_code = "MISSING_ARGUMENT";
        res.summary = "Missing or empty required argument 'query'.";
        return res;
    }

    if (context.retrieval_pipeline == nullptr) {
        res.status = ToolResultStatus::Error;
        res.error_code = "CAPABILITY_UNAVAILABLE";
        res.summary = "PrimaryRetrievalPipeline is not available in the execution context.";
        return res;
    }

    const std::string_view query = *query_opt;
    const std::string mode_str = request.get_arg_or("mode", "hybrid");
    const std::string limit_str = request.get_arg_or("limit", "5");

    std::size_t limit = 5;
    try {
        limit = std::stoul(limit_str);
    } catch (...) {
        limit = 5;
    }
    limit = std::clamp(limit, std::size_t{1}, std::size_t{20});

    retrieval::PrimarySearchOptions opts;
    opts.max_results = limit;

    if (mode_str == "code_aware") {
        opts.alpha = 1.0;
        opts.lexical_ranker = index::RankerType::CodeAware;
        opts.adaptive_fusion = false;
    } else if (mode_str == "bm25") {
        opts.alpha = 1.0;
        opts.lexical_ranker = index::RankerType::BM25;
        opts.adaptive_fusion = false;
    } else if (mode_str == "baseline") {
        opts.alpha = 1.0;
        opts.lexical_ranker = index::RankerType::Baseline;
        opts.adaptive_fusion = false;
    } else if (mode_str == "semantic") {
        opts.alpha = 0.0;
        opts.adaptive_fusion = false;
    } else {
        // hybrid default
        opts.alpha = 0.5;
        opts.lexical_ranker = index::RankerType::CodeAware;
        opts.adaptive_fusion = true;
    }

    const auto results = context.retrieval_pipeline->search(query, opts);

    if (results.empty()) {
        res.status = ToolResultStatus::NotFound;
        res.error_code = "NO_MATCHES_FOUND";
        res.summary = "No matching code elements found for query: \"" + std::string(query) + "\".";
        return res;
    }

    res.status = ToolResultStatus::Success;
    res.summary = "Found " + std::to_string(results.size()) + " primary code units.";
    res.metadata.push_back({"query", std::string(query)});
    res.metadata.push_back({"mode", mode_str});
    res.metadata.push_back({"result_count", std::to_string(results.size())});

    std::ostringstream ss;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        const auto& elem = r.unit.primary_element;
        ss << "[" << (i + 1) << "] " << parser::to_string(elem.kind) << ": `" << elem.name
           << "`\n";
        std::filesystem::path fpath = r.unit.file_path;
        if (!context.repository_root.empty()) {
            std::error_code ec;
            auto rel = std::filesystem::relative(fpath, context.repository_root, ec);
            if (!ec && !rel.empty() && !rel.string().starts_with("..")) {
                fpath = rel;
            }
        }
        ss << "    File: " << fpath.generic_string();
        if (elem.location.start.line > 0) {
            ss << ":" << elem.location.start.line << ":" << elem.location.start.column;
        }
        ss << "\n";
        if (!elem.parent_context.empty()) {
            ss << "    Parent: `" << elem.parent_context << "`\n";
        }
        if (!elem.detail.empty()) {
            ss << "    Detail: `" << elem.detail << "`\n";
        }
        ss << "    Score: " << r.hybrid_score << " (provenance: " << retrieval::to_string(r.provenance)
           << ")\n";

        if (!r.unit.supporting_elements.empty()) {
            ss << "    Supporting Elements (" << r.unit.supporting_elements.size() << "):\n";
            for (const auto& supp : r.unit.supporting_elements) {
                ss << "      - " << parser::to_string(supp.kind) << " `" << supp.name << "` (L"
                   << supp.location.start.line << ")\n";
            }
        }
        ss << "\n";
    }

    res.content = ss.str();
    return res;
}

}  // namespace amoeba::tools
