#include "amoeba/context/context_package.hpp"
#include "amoeba/evidence/evidence_bundle.hpp"
#include "amoeba/evidence/evidence_sufficiency.hpp"
#include "amoeba/parser/code_element.hpp"
#include "amoeba/reasoning/fake_llm_runtime.hpp"
#include "amoeba/reasoning/grounded_answer.hpp"
#include "amoeba/reasoning/prompt_builder.hpp"
#include "amoeba/reasoning/reasoning_service.hpp"

#include <gtest/gtest.h>

namespace {

using namespace amoeba;
using namespace amoeba::context;
using namespace amoeba::evidence;
using namespace amoeba::reasoning;

class GroundedAnswerTest : public ::testing::Test {
protected:
    ContextPackage create_sample_context() {
        ContextPackage pkg;
        pkg.query = "Where is the inverted index implemented?";
        pkg.total_available_items = 1;
        pkg.selected_item_count = 1;

        EvidenceItem item;
        item.primary_result.unit.file_path = "engine/src/index/inverted_index.cpp";
        item.primary_result.unit.primary_element.name = "InvertedIndex";
        item.primary_result.unit.primary_element.kind = parser::ElementKind::Class;
        item.primary_result.unit.primary_element.location.start = {15, 1};
        item.primary_result.unit.primary_element.location.end = {120, 2};

        pkg.selected_items.push_back(item);
        pkg.rendered_markdown =
            "## Result 1: `InvertedIndex`\n- **Kind**: Class\n- **File**: "
            "engine/src/index/inverted_index.cpp\n";
        return pkg;
    }
};

// 1. Contract Enum and formatting
TEST_F(GroundedAnswerTest, GroundedAnswerContractFormatting) {
    GroundedAnswer ans;
    ans.user_question = "Where is the inverted index implemented?";
    ans.status = GroundedAnswerStatus::Grounded;
    ans.answer_text = "The InvertedIndex class is implemented in inverted_index.cpp.";
    ans.citations = {GroundedCitation{
        .file_path = "engine/src/index/inverted_index.cpp",
        .symbol_name = "InvertedIndex",
        .range_description = "L15-L120",
        .element_kind = "Class",
    }};

    EXPECT_TRUE(ans.is_grounded());
    EXPECT_FALSE(ans.is_insufficient());
    EXPECT_EQ(to_string(ans.status), "GROUNDED");

    const std::string formatted = ans.format();
    EXPECT_NE(formatted.find("Answer:\nThe InvertedIndex class is implemented in inverted_index.cpp."),
              std::string::npos);
    EXPECT_NE(formatted.find("Evidence:\n- engine/src/index/inverted_index.cpp (InvertedIndex) [L15-L120]"),
              std::string::npos);
}

// 2. Insufficient Evidence formatting
TEST_F(GroundedAnswerTest, InsufficientEvidenceFormatting) {
    GroundedAnswer ans;
    ans.user_question = "Where is JWT authentication implemented?";
    ans.status = GroundedAnswerStatus::InsufficientEvidence;
    ans.answer_text = "I couldn't establish this from the repository evidence available to Amoeba.";
    ans.refusal_reason = "Missing required query subjects: [\"jwt\", \"authentication\"].";

    EXPECT_FALSE(ans.is_grounded());
    EXPECT_TRUE(ans.is_insufficient());
    EXPECT_EQ(to_string(ans.status), "INSUFFICIENT_EVIDENCE");

    const std::string formatted = ans.format();
    EXPECT_NE(formatted.find("Answer:\nI couldn't establish this from the repository evidence available to Amoeba."),
              std::string::npos);
    EXPECT_NE(formatted.find("Evidence:\nMissing required query subjects: [\"jwt\", \"authentication\"]."),
              std::string::npos);
}

// 3. Fast-path refusal without LLM execution
TEST_F(GroundedAnswerTest, SufficiencyRejectionFastPathBypassesLLM) {
    FakeLLMRuntime runtime("Hallucinated answer from LLM that should never be reached");
    ReasoningService service(runtime);

    const auto pkg = create_sample_context();

    EvidenceSufficiencyResult sufficiency;
    sufficiency.is_sufficient = false;
    sufficiency.missing_subjects = {"jwt", "authentication"};
    sufficiency.reason = "Subject concepts not found in retrieved repository elements.";

    const auto answer = service.answer_grounded("Where is JWT authentication implemented?", pkg, sufficiency);

    EXPECT_EQ(answer.status, GroundedAnswerStatus::InsufficientEvidence);
    EXPECT_TRUE(answer.is_insufficient());
    EXPECT_EQ(runtime.invocation_count(), 0u);  // CRITICAL: 0 LLM tokens consumed
    EXPECT_EQ(answer.model_name, "amoeba::sufficiency_gate");
    EXPECT_NE(answer.refusal_reason.find("jwt"), std::string::npos);
}

// 4. Sufficient evidence invokes LLM and builds grounded citations
TEST_F(GroundedAnswerTest, SufficientEvidenceInvokesLLMAndPopulatesCitations) {
    FakeLLMRuntime runtime("InvertedIndex manages posting lists and term frequencies.");
    ReasoningService service(runtime);

    const auto pkg = create_sample_context();

    EvidenceSufficiencyResult sufficiency;
    sufficiency.is_sufficient = true;
    sufficiency.confidence_score = 0.95;

    const auto answer = service.answer_grounded("Where is the inverted index implemented?", pkg, sufficiency);

    EXPECT_EQ(answer.status, GroundedAnswerStatus::Grounded);
    EXPECT_TRUE(answer.is_grounded());
    EXPECT_EQ(runtime.invocation_count(), 1u);
    EXPECT_EQ(answer.answer_text, "InvertedIndex manages posting lists and term frequencies.");
    ASSERT_EQ(answer.citations.size(), 1u);
    EXPECT_EQ(answer.citations[0].file_path, "engine/src/index/inverted_index.cpp");
    EXPECT_EQ(answer.citations[0].symbol_name, "InvertedIndex");
    EXPECT_EQ(answer.citations[0].range_description, "L15-L120");
}

// 5. Error handling in answer_grounded
TEST_F(GroundedAnswerTest, ErrorHandlingInAnswerGrounded) {
    FakeLLMRuntime runtime;
    runtime.set_error("Context length exceeded token limit");
    ReasoningService service(runtime);

    const auto pkg = create_sample_context();

    EvidenceSufficiencyResult sufficiency;
    sufficiency.is_sufficient = true;

    const auto answer = service.answer_grounded("Where is the inverted index implemented?", pkg, sufficiency);

    EXPECT_EQ(answer.status, GroundedAnswerStatus::Error);
    EXPECT_EQ(answer.answer_text, "Context length exceeded token limit");
    EXPECT_TRUE(answer.citations.empty());
}

}  // namespace
