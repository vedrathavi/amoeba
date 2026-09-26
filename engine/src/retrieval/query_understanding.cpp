#include "amoeba/retrieval/query_understanding.hpp"

#include "amoeba/index/code_tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace amoeba::retrieval {

namespace {

const std::unordered_set<std::string_view> kStopwords = {
    "a",       "an",      "the",   "in",    "on",    "at",       "to",    "for",   "of",
    "with",    "by",      "from",  "up",    "down",  "into",     "over",  "after", "is",
    "are",     "was",     "were",  "be",    "been",  "being",    "have",  "has",   "had",
    "do",      "does",    "did",   "can",   "could", "should",   "would", "may",   "might",
    "must",    "where",   "what",  "when",  "why",   "how",      "which", "who",   "whom",
    "each",    "every",   "all",   "any",   "both",  "and",      "or",    "not",   "inside",
    "outside", "between", "about", "above", "below", "rendered", "does",  "show"};

const std::unordered_set<std::string_view> kInterrogatives = {"where", "what",  "when", "why",
                                                              "how",   "which", "who",  "whom"};

}  // namespace

std::string QueryUnderstanding::normalize_text(std::string_view text) {
    if (text.empty()) {
        return "";
    }

    std::string result;
    result.reserve(text.size());

    bool in_space = false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!in_space && !result.empty()) {
                result.push_back(' ');
                in_space = true;
            }
        } else {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            in_space = false;
        }
    }

    // Trim trailing space if present
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

std::string QueryUnderstanding::collapse_identifier(std::string_view text) {
    std::string result;
    result.reserve(text.size());

    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }

    return result;
}

std::vector<std::string>
QueryUnderstanding::synthesize_compounds(const std::vector<std::string>& terms) {
    if (terms.empty()) {
        return {};
    }

    std::vector<std::string> compounds;
    std::unordered_set<std::string> seen;

    // Full sequence collapsed
    if (terms.size() >= 2 && terms.size() <= 4) {
        std::string full_collapsed;
        for (const auto& t : terms) {
            full_collapsed += t;
        }
        if (!full_collapsed.empty() && seen.insert(full_collapsed).second) {
            compounds.push_back(full_collapsed);
        }
    }

    // Adjacent pairs
    if (terms.size() >= 2) {
        for (std::size_t i = 0; i + 1 < terms.size(); ++i) {
            const std::string pair_collapsed = terms[i] + terms[i + 1];
            if (!pair_collapsed.empty() && seen.insert(pair_collapsed).second) {
                compounds.push_back(pair_collapsed);
            }
        }
    }

    return compounds;
}

bool QueryUnderstanding::is_stopword(std::string_view term) noexcept {
    return kStopwords.contains(term);
}

QueryRepresentation QueryUnderstanding::analyze(std::string_view raw_query) {
    QueryRepresentation rep;
    rep.raw_query = std::string(raw_query);
    rep.normalized_query = normalize_text(raw_query);
    rep.collapsed_query = collapse_identifier(raw_query);

    // If query contains no alphanumeric characters, return empty representation immediately
    bool has_alnum = false;
    for (char c : raw_query) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            has_alnum = true;
            break;
        }
    }
    if (!has_alnum) {
        return rep;
    }

    // Tokenize terms
    rep.raw_terms = index::CodeTokenizer::tokenize_query(raw_query);

    // Synthesize compounds
    rep.synthesized_identifiers = synthesize_compounds(rep.raw_terms);

    // Build deduplicated all_search_terms
    std::unordered_set<std::string> seen_terms;
    for (const auto& t : rep.raw_terms) {
        if (seen_terms.insert(t).second) {
            rep.all_search_terms.push_back(t);
        }
    }
    for (const auto& c : rep.synthesized_identifiers) {
        if (seen_terms.insert(c).second) {
            rep.all_search_terms.push_back(c);
        }
    }

    // Inspect code syntax in raw query
    // 1. camelCase or PascalCase transitions
    bool has_camel_case = false;
    for (std::size_t i = 0; i + 1 < raw_query.size(); ++i) {
        const char c1 = raw_query[i];
        const char c2 = raw_query[i + 1];
        if (std::islower(static_cast<unsigned char>(c1)) &&
            std::isupper(static_cast<unsigned char>(c2))) {
            has_camel_case = true;
            break;
        }
    }

    // 2. Code punctuation & extensions
    const bool has_code_punct = (raw_query.find("::") != std::string_view::npos ||
                                 raw_query.find("->") != std::string_view::npos ||
                                 raw_query.find("()") != std::string_view::npos ||
                                 raw_query.find(".tsx") != std::string_view::npos ||
                                 raw_query.find(".ts") != std::string_view::npos ||
                                 raw_query.find(".cpp") != std::string_view::npos ||
                                 raw_query.find(".h") != std::string_view::npos ||
                                 raw_query.find(".jsx") != std::string_view::npos ||
                                 raw_query.find(".js") != std::string_view::npos);

    rep.has_code_syntax = has_camel_case || has_code_punct;

    // 3. Stopwords & Interrogatives
    std::size_t stopword_count = 0;
    bool has_interrogative = false;

    for (const auto& term : rep.raw_terms) {
        if (is_stopword(term)) {
            stopword_count++;
        }
        if (kInterrogatives.contains(term)) {
            has_interrogative = true;
        }
    }

    if (raw_query.find('?') != std::string_view::npos) {
        has_interrogative = true;
    }

    rep.has_interrogative = has_interrogative;

    // Intent determination:
    // NaturalLanguage: has interrogative OR (3+ stopwords and word count >= 6 and not pure code
    // syntax)
    if (has_interrogative ||
        (stopword_count >= 3 && rep.raw_terms.size() >= 6 && !has_code_punct)) {
        rep.intent = QueryIntent::NaturalLanguage;
        rep.recommended_alpha = 0.25;  // Semantic dominant
    } else if (rep.has_code_syntax || rep.raw_terms.size() <= 3 ||
               !rep.synthesized_identifiers.empty()) {
        rep.intent = QueryIntent::IdentifierOrTechnical;
        rep.recommended_alpha = 0.75;  // Lexical dominant
    } else {
        rep.intent = QueryIntent::GeneralSearch;
        rep.recommended_alpha = 0.50;  // Balanced
    }

    return rep;
}

}  // namespace amoeba::retrieval
