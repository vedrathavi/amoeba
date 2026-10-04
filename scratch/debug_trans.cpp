#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/retrieval/query_understanding.hpp"
#include <iostream>
#include <vector>

using namespace amoeba::evidence;
using namespace amoeba::retrieval;
using namespace amoeba::parser;
using namespace amoeba::source;

void test_query(const std::string& name, const std::string& query, const std::string& item_name, const std::string& item_path, const std::string& excerpt = "") {
    EvidenceBundle bundle;
    bundle.query = query;
    EvidenceItem item;
    item.primary_result.unit.primary_element.name = item_name;
    item.primary_result.unit.primary_element.kind = ElementKind::Function;
    item.primary_result.unit.file_path = item_path;
    item.primary_result.hybrid_score = 0.90;
    item.primary_result.normalized_lexical_score = 0.90;
    item.primary_result.normalized_semantic_score = 0.90;
    item.primary_result.provenance = RetrievalProvenance::HybridBoth;
    if (!excerpt.empty()) {
        SourceExcerpt sc;
        sc.text = excerpt;
        item.source_excerpt = sc;
    }
    bundle.items.push_back(item);

    auto rep = QueryUnderstanding::analyze(query);
    std::cout << "=== " << name << " ===\n";
    std::cout << "Query: " << query << "\n";
    std::cout << "Core subjects: ";
    for (auto& s : rep.core_subject_terms) std::cout << s << ", ";
    std::cout << "\nDistinguishing subjects: ";
    for (auto& s : rep.distinguishing_subject_terms) std::cout << s << ", ";
    std::cout << "\nGeneric components: ";
    for (auto& s : rep.generic_component_terms) std::cout << s << ", ";
    std::cout << "\nTechnical entities: ";
    for (auto& s : rep.technical_entities) std::cout << s << ", ";
    std::cout << "\nConversational noise: ";
    for (auto& s : rep.conversational_noise_terms) std::cout << s << ", ";
    std::cout << "\nAction terms: ";
    for (auto& s : rep.action_terms) std::cout << s << ", ";
    std::cout << "\n";

    auto res = EvidenceSufficiencyChecker::check(bundle.query, bundle);
    std::cout << "Result: " << (res.is_sufficient ? "SUFFICIENT" : "INSUFFICIENT") << " (conf=" << res.confidence_score << ")\n";
    std::cout << "Reason: " << res.reason << "\n";
    std::cout << "Matched subjects: ";
    for (auto& s : res.matched_subjects) std::cout << s << ", ";
    std::cout << "\nMissing subjects: ";
    for (auto& s : res.missing_subjects) std::cout << s << ", ";
    std::cout << "\n\n";
}

int main() {
    test_query("DuckLake", "How is transaction management structured?", "DuckLakeTransactionManager", "src/transaction/ducklake_transaction_manager.cpp");
    return 0;
}
