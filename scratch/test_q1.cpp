#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/retrieval/query_understanding.hpp"
#include <iostream>

int main() {
    auto q1_text = "Does adk-python provide built-in vector database indexing with Qdrant natively in core?";
    auto q1 = amoeba::retrieval::QueryUnderstanding::analyze(q1_text);
    
    amoeba::evidence::EvidenceBundle bundle;
    bundle.query = q1_text;
    
    amoeba::evidence::EvidenceItem item;
    item.primary_result.unit.file_path = "d:/amoeba/benchmarks/repoprobe_v1/repos/adk-python/tests/unittests/tools/spanner/test_spanner_toolset.py";
    item.primary_result.unit.primary_element.name = "test_spanner_toolset_with_vector_store_search";
    item.primary_result.unit.language = "Python";
    item.primary_result.hybrid_score = 0.35;
    bundle.items.push_back(item);

    auto suff = amoeba::evidence::EvidenceSufficiencyChecker::check(q1, bundle);
    std::cout << "is_sufficient: " << (suff.is_sufficient ? "true" : "false") << "\n";
    std::cout << "reason: " << suff.reason << "\n";
    std::cout << "matched_subjects: "; for (auto& s : suff.matched_subjects) std::cout << s << " "; std::cout << "\n";
    std::cout << "missing_subjects: "; for (auto& s : suff.missing_subjects) std::cout << s << " "; std::cout << "\n";
    return 0;
}
