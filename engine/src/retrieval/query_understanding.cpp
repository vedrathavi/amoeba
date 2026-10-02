#include "amoeba/retrieval/query_understanding.hpp"

#include "amoeba/index/code_tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace amoeba::retrieval {

namespace {

const std::unordered_set<std::string_view> kStopwords = {
    "a",       "an",      "the",   "in",      "on",    "at",          "to",    "for",    "of",
    "with",    "by",      "from",  "up",      "down",  "into",        "over",  "after",  "is",
    "are",     "was",     "were",  "be",      "been",  "being",       "have",  "has",    "had",
    "do",      "does",    "did",   "can",     "could", "should",      "would", "may",    "might",
    "must",    "where",   "what",  "when",    "why",   "how",         "which", "who",    "whom",
    "each",    "every",   "all",   "any",     "both",  "and",         "or",    "not",    "inside",
    "outside", "between", "about", "above",   "below", "rendered",    "does",  "show",   "this",
    "that",    "it",      "its",   "another", "other", "application", "app",   "system", "codebase",
    "project"};

const std::unordered_set<std::string_view> kInterrogatives = {"where", "what",  "when", "why",
                                                              "how",   "which", "who",  "whom"};

const std::unordered_set<std::string_view> kGenericActionBases = {
    "implement", "define",  "declare", "manage",  "save",     "store",      "locate",
    "render",    "create",  "handle",  "use",     "call",     "configure",  "generate",
    "load",      "display", "update",  "delete",  "navigate", "initialize", "init",
    "build",     "format",  "parse",   "process", "dispatch", "trigger",    "execute",
    "run",       "send",    "receive", "fetch",   "get",      "set",        "read",
    "write",     "show",    "view",    "find",    "obtain",   "check",      "contain",
    "hold",      "perform"};

const std::unordered_set<std::string_view> kGenericComponentTerms = {
    "resolver",      "handler",       "manager",        "service",      "client",
    "provider",      "controller",    "parser",         "processor",    "factory",
    "adapter",       "builder",       "helper",         "utility",      "validator",
    "middleware",    "component",     "hook",           "store",        "storage",
    "model",         "view",          "router",         "route",        "worker",
    "runner",        "listener",      "emitter",        "consumer",     "producer",
    "engine",        "pipeline",      "deployment",     "endpoint",     "driver",
    "connector",     "serializer",    "deserializer",   "indexer",      "index",
    "searcher",      "extractor",     "checker",        "inspector",    "evaluator",
    "authenticator", "authorizer",    "auth",           "authentication","login",
    "checkout",      "wrapper",       "registry",       "module",       "element"};

const std::unordered_set<std::string_view> kConversationalNoise = {
    "hello", "hi", "hey", "thanks", "thank", "please", "help", "anyone",
    "idea", "ideas", "advice", "wondering", "curious", "trying", "attempt",
    "attempting", "question", "questions", "issue", "issues", "problem",
    "problems", "bug", "bugs", "error", "errors", "failed", "failing",
    "failure", "works", "working", "work", "possible", "impossible",
    "feasible", "approach", "approaches", "tradeoff", "tradeoffs", "trade-off",
    "trade-offs", "difference", "differences", "pros", "cons", "vs", "versus",
    "best", "better", "good", "bad", "way", "ways", "setup",
    "step", "steps", "guide", "tutorial", "example", "examples", "sample",
    "samples", "perspective", "thought", "thoughts", "opinion", "opinions",
    "direction", "promising", "testing", "installation", "scenario",
    "scenarios", "case", "cases", "behavior", "behaviour", "option", "options",
    "notice", "noticed", "seeing", "seen", "looks",
    "looking", "like", "current", "currently", "latest", "new", "old",
    "first", "second", "simple", "complex", "e.g", "eg", "ie", "i.e",
    "someone", "anybody", "everybody", "everyone", "community", "folks",
    "team", "guys", "description", "summary", "file", "files", "code", "codes",
    "line", "lines"};

}  // namespace


std::string QueryUnderstanding::conservative_stem(std::string_view term) {
    if (term.empty()) {
        return "";
    }
    std::string s;
    s.reserve(term.size());
    for (char c : term) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (s.size() <= 3) {
        return s;
    }

    // 1. Plural / 3rd person -s forms
    if (s.size() > 4 && s.ends_with("ies")) {
        s.pop_back();
        s.pop_back();
        s.pop_back();
        s.push_back('y');
        return s;
    }

    if (s.size() > 4 && s.ends_with("es")) {
        if (s.ends_with("sses") || s.ends_with("shes") || s.ends_with("ches") ||
            s.ends_with("xes") || s.ends_with("zes")) {
            s.pop_back();
            s.pop_back();
            return s;
        }
        if (s.ends_with("ates") || s.ends_with("izes") || s.ends_with("ages") ||
            s.ends_with("aves") || s.ends_with("utes") || s.ends_with("udes")) {
            s.pop_back();
            return s;
        }
    }

    if (s.ends_with('s') && s.size() > 3) {
        const char prev = s[s.size() - 2];
        if (prev != 's' && prev != 'i' && prev != 'u' && prev != 'a' && prev != 'o') {
            s.pop_back();
            return s;
        }
    }

    // 2. Verb -ing forms
    if (s.size() > 5 && s.ends_with("ing")) {
        if (s.ends_with("ating") || s.ends_with("izing") || s.ends_with("aging") ||
            s.ends_with("iving") || s.ends_with("uting") || s.ends_with("uding") ||
            s.ends_with("aving") || s.ends_with("oring") || s.ends_with("osing") ||
            s.ends_with("uring") || s.ends_with("oving") || s.ends_with("uing")) {
            s.pop_back();
            s.pop_back();
            s.pop_back();
            s.push_back('e');
            return s;
        }
        if (s.size() > 6) {
            const char c1 = s[s.size() - 4];
            const char c2 = s[s.size() - 5];
            if (c1 == c2 && (c1 == 'n' || c1 == 't' || c1 == 'p' || c1 == 'm' || c1 == 'g' ||
                             c1 == 'b' || c1 == 'r')) {
                s.pop_back();
                s.pop_back();
                s.pop_back();
                s.pop_back();
                return s;
            }
        }
        s.pop_back();
        s.pop_back();
        s.pop_back();
        return s;
    }

    // 3. Verb -ed forms
    if (s.size() > 4 && s.ends_with("ed")) {
        if (s.ends_with("ied")) {
            s.pop_back();
            s.pop_back();
            s.pop_back();
            s.push_back('y');
            return s;
        }
        if (s.ends_with("ated") || s.ends_with("ized") || s.ends_with("aged") ||
            s.ends_with("ived") || s.ends_with("uted") || s.ends_with("uded") ||
            s.ends_with("ured") || s.ends_with("aved") || s.ends_with("ored") ||
            s.ends_with("osed") || s.ends_with("oved")) {
            s.pop_back();
            return s;
        }
        if (s.size() > 5) {
            const char c1 = s[s.size() - 3];
            const char c2 = s[s.size() - 4];
            if (c1 == c2 && (c1 == 'p' || c1 == 't' || c1 == 'n' || c1 == 'g' || c1 == 'm' ||
                             c1 == 'b' || c1 == 'r')) {
                s.pop_back();
                s.pop_back();
                s.pop_back();
                return s;
            }
        }
        if (s.ends_with("ented") || s.ends_with("arted") || s.ends_with("orted") ||
            s.ends_with("ected") || s.ends_with("acted") || s.ends_with("isted") ||
            s.ends_with("usted") || s.ends_with("pted")) {
            s.pop_back();
            s.pop_back();
            return s;
        }
        if (s.ends_with("ined") || s.ends_with("ared") || s.ends_with("ored") ||
            s.ends_with("osed")) {
            s.pop_back();
            return s;
        }
        if (s.ends_with("ved") || s.ends_with("led") || s.ends_with("zed") || s.ends_with("sed")) {
            s.pop_back();
            return s;
        }
        s.pop_back();
        s.pop_back();
        return s;
    }

    return s;
}

bool QueryUnderstanding::is_action_term(std::string_view term) noexcept {
    const std::string stem = conservative_stem(term);
    return kGenericActionBases.contains(term) || kGenericActionBases.contains(stem);
}

bool QueryUnderstanding::is_interrogative(std::string_view term) noexcept {
    return kInterrogatives.contains(term);
}

bool QueryUnderstanding::is_generic_component_term(std::string_view term) noexcept {
    const std::string stem = conservative_stem(term);
    return kGenericComponentTerms.contains(term) || kGenericComponentTerms.contains(stem);
}

bool QueryUnderstanding::is_conversational_noise(std::string_view term) noexcept {
    if (term.empty()) {
        return false;
    }
    std::string lower_t;
    lower_t.reserve(term.size());
    for (char c : term) {
        if (!std::ispunct(static_cast<unsigned char>(c)) || c == '-') {
            lower_t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (kConversationalNoise.contains(term) || kConversationalNoise.contains(lower_t)) {
        return true;
    }
    std::string no_punct;
    no_punct.reserve(lower_t.size());
    for (char c : lower_t) {
        if (c != '-') {
            no_punct.push_back(c);
        }
    }
    return kConversationalNoise.contains(no_punct);
}

bool QueryUnderstanding::is_technical_entity(std::string_view term) noexcept {
    if (term.empty() || term.size() < 2) {
        return false;
    }

    // 1. camelCase / PascalCase transitions:
    // Must have an internal uppercase character after position 0, OR multiple uppercase characters with lowercase.
    bool has_internal_upper = false;
    int upper_count = 0;
    int lower_count = 0;
    for (std::size_t i = 0; i < term.size(); ++i) {
        char c = term[i];
        if (std::isupper(static_cast<unsigned char>(c))) {
            upper_count++;
            if (i > 0) {
                has_internal_upper = true;
            }
        } else if (std::islower(static_cast<unsigned char>(c))) {
            lower_count++;
        }
    }
    if ((has_internal_upper || (upper_count > 1 && lower_count > 0)) && lower_count > 0) {
        return true;
    }

    // 2. Snake_case with alphanumeric on both sides
    if (term.find('_') != std::string_view::npos && term.size() >= 3) {
        return true;
    }

    // 3. Code syntax, dotted paths, file extensions, or scope operators
    if (term.find("::") != std::string_view::npos || term.find("->") != std::string_view::npos ||
        term.find('.') != std::string_view::npos || term.find("()") != std::string_view::npos ||
        term.find('/') != std::string_view::npos || term.find('\\') != std::string_view::npos) {
        return true;
    }

    // 4. Uppercase technical acronyms (e.g. JWT, RBAC, OAuth, MCP, SQL, JDBC, TLS, WAF)
    if (term.size() >= 3 && term.size() <= 8) {
        bool all_upper_alnum = true;
        for (char c : term) {
            if (!std::isupper(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c))) {
                all_upper_alnum = false;
                break;
            }
        }
        if (all_upper_alnum) {
            return true;
        }
    }

    return false;
}

QueryTermRole QueryUnderstanding::classify_term_role(std::string_view term) noexcept {
    if (term.empty() || term.size() < 2) {
        return QueryTermRole::Context;
    }
    std::string lower_t;
    lower_t.reserve(term.size());
    for (char c : term) {
        lower_t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    while (!lower_t.empty() &&
           (lower_t.back() == '?' || lower_t.back() == '!' || lower_t.back() == '.' ||
            lower_t.back() == ',' || lower_t.back() == ':' || lower_t.back() == ';')) {
        lower_t.pop_back();
    }
    if (lower_t.size() < 2 || is_stopword(lower_t) || is_interrogative(lower_t)) {
        return QueryTermRole::Context;
    }
    if (is_conversational_noise(lower_t) || is_conversational_noise(term)) {
        return QueryTermRole::ConversationalFraming;
    }
    if (is_technical_entity(term)) {
        return QueryTermRole::TechnicalEntity;
    }
    if (is_action_term(lower_t)) {
        return QueryTermRole::Action;
    }
    return QueryTermRole::Subject;
}


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

    // Extract and categorize primary query words
    std::unordered_set<std::string> seen_subjects;
    std::unordered_set<std::string> seen_actions;
    std::unordered_set<std::string> seen_raw_terms;
    std::unordered_set<std::string> seen_technical;

    // Split raw query into whitespace-separated tokens
    std::size_t pos = 0;
    while (pos < raw_query.size()) {
        while (pos < raw_query.size() && std::isspace(static_cast<unsigned char>(raw_query[pos]))) {
            pos++;
        }
        if (pos >= raw_query.size()) {
            break;
        }
        std::size_t end_pos = pos;
        while (end_pos < raw_query.size() &&
               !std::isspace(static_cast<unsigned char>(raw_query[end_pos]))) {
            end_pos++;
        }

        std::string word(raw_query.substr(pos, end_pos - pos));
        pos = end_pos;

        while (!word.empty() &&
               (word.back() == '?' || word.back() == '!' || word.back() == '.' ||
                word.back() == ',' || word.back() == ':' || word.back() == ';' ||
                word.back() == ')' || word.back() == ']' || word.back() == '}')) {
            word.pop_back();
        }
        while (!word.empty() && (word.front() == '(' || word.front() == '[' || word.front() == '{' ||
                                 word.front() == '"' || word.front() == '\'')) {
            word.erase(word.begin());
        }
        if (word.empty()) {
            continue;
        }

        const auto role = classify_term_role(word);
        const auto stem = conservative_stem(word);

        rep.categorized_terms.push_back(CategorizedQueryTerm{
            .raw_term = word,
            .normalized_stem = stem,
            .role = role,
        });

        if (role == QueryTermRole::TechnicalEntity) {
            if (seen_technical.insert(word).second) {
                rep.technical_entities.push_back(word);
            }
            if (seen_subjects.insert(stem).second) {
                rep.subject_terms.push_back(stem);
                rep.distinguishing_subject_terms.push_back(stem);
            }
        } else if (role == QueryTermRole::Subject) {
            rep.meaningful_domain_terms.push_back(word);
            if (seen_subjects.insert(stem).second) {
                rep.subject_terms.push_back(stem);
                if (is_generic_component_term(stem)) {
                    rep.generic_component_terms.push_back(stem);
                } else {
                    rep.distinguishing_subject_terms.push_back(stem);
                }
            }
        } else if (role == QueryTermRole::ConversationalFraming) {
            std::string lower_word;
            lower_word.reserve(word.size());
            for (char c : word) {
                lower_word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            rep.conversational_noise_terms.push_back(std::move(lower_word));
        } else if (role == QueryTermRole::Action) {

            if (seen_actions.insert(stem).second) {
                rep.action_terms.push_back(stem);
            }
        }

        // Add word and its sub-word components to raw_terms for search expansion
        const auto sub_tokens = index::CodeTokenizer::tokenize_identifier(word);
        for (const auto& tok : sub_tokens) {
            if (seen_raw_terms.insert(tok).second) {
                rep.raw_terms.push_back(tok);
            }
        }
    }

    // Synthesize compounds
    rep.synthesized_identifiers = synthesize_compounds(rep.raw_terms);

    // Build clean_technical_query
    std::string clean_q;
    std::unordered_set<std::string> seen_clean;
    for (const auto& te : rep.technical_entities) {
        if (seen_clean.insert(te).second) {
            if (!clean_q.empty()) clean_q.push_back(' ');
            clean_q.append(te);
        }
    }
    for (const auto& term : rep.categorized_terms) {
        if (term.role == QueryTermRole::Subject) {
            if (seen_clean.insert(term.raw_term).second) {
                if (!clean_q.empty()) clean_q.push_back(' ');
                clean_q.append(term.raw_term);
            }
        }
    }
    if (clean_q.empty()) {
        for (const auto& term : rep.categorized_terms) {
            if (term.role != QueryTermRole::Context && term.role != QueryTermRole::ConversationalFraming) {
                if (seen_clean.insert(term.raw_term).second) {
                    if (!clean_q.empty()) clean_q.push_back(' ');
                    clean_q.append(term.raw_term);
                }
            }
        }
    }
    if (clean_q.empty()) {
        clean_q = rep.normalized_query;
    }
    rep.clean_technical_query = clean_q;

    // Build deduplicated all_search_terms
    std::unordered_set<std::string> seen_terms;
    for (const auto& t : rep.raw_terms) {
        if (seen_terms.insert(t).second) {
            rep.all_search_terms.push_back(t);
        }
        const auto stem = conservative_stem(t);
        if (!stem.empty() && seen_terms.insert(stem).second) {
            rep.all_search_terms.push_back(stem);
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

    rep.has_code_syntax = has_camel_case || has_code_punct || !rep.technical_entities.empty();

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
