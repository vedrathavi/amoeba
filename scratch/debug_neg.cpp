#include "amoeba/retrieval/query_understanding.hpp"
#include <iostream>

int main() {
    auto q1 = amoeba::retrieval::QueryUnderstanding::analyze("Does adk-python provide built-in vector database indexing with Qdrant natively in core?");
    std::cout << "Q1 core: "; for (const auto& s : q1.core_subject_terms) std::cout << s << " "; std::cout << "\n";
    std::cout << "Q1 dist: "; for (const auto& s : q1.distinguishing_subject_terms) std::cout << s << " "; std::cout << "\n";
    std::cout << "Q1 subj: "; for (const auto& s : q1.subject_terms) std::cout << s << " "; std::cout << "\n";
    std::cout << "Q1 generic: "; for (const auto& s : q1.generic_component_terms) std::cout << s << " "; std::cout << "\n";

    auto q2 = amoeba::retrieval::QueryUnderstanding::analyze("Where is the Spring Boot auto-configuration controller implemented in pkl?");
    std::cout << "Q2 core: "; for (const auto& s : q2.core_subject_terms) std::cout << s << " "; std::cout << "\n";
    std::cout << "Q2 dist: "; for (const auto& s : q2.distinguishing_subject_terms) std::cout << s << " "; std::cout << "\n";
    std::cout << "Q2 subj: "; for (const auto& s : q2.subject_terms) std::cout << s << " "; std::cout << "\n";
    std::cout << "Q2 generic: "; for (const auto& s : q2.generic_component_terms) std::cout << s << " "; std::cout << "\n";
    return 0;
}
