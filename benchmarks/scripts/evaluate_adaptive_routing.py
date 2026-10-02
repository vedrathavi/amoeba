import time
import math
import json
import torch
import os
import numpy as np
from sentence_transformers import SentenceTransformer

# Replicate exact C++ QueryUnderstanding logic in Python for deterministic feature extraction
STOPWORDS = {
    "a", "an", "the", "in", "on", "at", "for", "to", "of", "with", "by", "from",
    "is", "are", "was", "were", "be", "been", "being", "have", "has", "had",
    "do", "does", "did", "and", "or", "not", "this", "that", "these", "those",
    "it", "its", "as", "if", "into", "about", "between", "through", "after",
    "before", "above", "below", "up", "down", "out", "off", "over", "under",
    "again", "then", "once", "here", "there", "all", "any", "both", "each",
    "few", "more", "most", "other", "some", "such", "no", "nor", "too", "very",
    "can", "will", "just", "should", "now", "where", "how", "what", "which",
    "who", "whom", "whose", "why", "when"
}

INTERROGATIVES = {"where", "how", "what", "which", "who", "whom", "why", "when"}

ACTION_BASES = {
    "implement", "defin", "declar", "manag", "handl", "stor", "sav", "persist",
    "creat", "construct", "initializ", "build", "render", "display", "navig",
    "rout", "dispatch", "execut", "run", "process", "comput", "calculat",
    "extract", "pars", "token", "serializ", "deserializ", "fetch", "load",
    "read", "writ", "send", "receiv", "updat", "delet", "remov", "insert",
    "validat", "verif", "check", "authenticat", "authoriz", "resolv",
    "normal", "scan", "index", "look", "search", "match", "rank", "assembl",
    "aggreg", "perform"
}

TECHNICAL_CODE_KEYWORDS = {
    "function", "class", "method", "hook", "component", "struct", "interface",
    "ast", "syntax", "parser", "parse", "token", "tokenizer", "import", "call",
    "caller", "callee", "identifier", "type", "file", "index", "inverted"
}

def analyze_query(query_str):
    q_lower = query_str.lower()
    raw_words = [w.strip("?!.,:;()[]{}'\"") for w in q_lower.split() if w.strip("?!.,:;()[]{}'\"")]
    
    has_camel = any(c1.islower() and c2.isupper() for c1, c2 in zip(query_str[:-1], query_str[1:]))
    has_code_punct = any(p in query_str for p in ["::", "->", "()", ".tsx", ".ts", ".cpp", ".h", ".js"])
    has_code_syntax = has_camel or has_code_punct
    
    stopword_count = sum(1 for w in raw_words if w in STOPWORDS)
    has_interrogative = any(w in INTERROGATIVES for w in raw_words) or ("?" in query_str)
    
    # Intent
    if has_interrogative or (stopword_count >= 3 and len(raw_words) >= 6 and not has_code_punct):
        intent = "NaturalLanguage"
    elif has_code_syntax or len(raw_words) <= 3:
        intent = "IdentifierOrTechnical"
    else:
        intent = "GeneralSearch"
        
    # Technical code subject/action presence
    has_technical_concept = any(w in TECHNICAL_CODE_KEYWORDS for w in raw_words)
    
    return {
        "intent": intent,
        "has_code_syntax": has_code_syntax,
        "has_interrogative": has_interrogative,
        "has_technical_concept": has_technical_concept,
        "word_count": len(raw_words),
        "raw_words": raw_words
    }

def setup_models():
    print("Loading MiniLM-L6-v2...")
    minilm = SentenceTransformer('sentence-transformers/all-MiniLM-L6-v2')
    print("Loading CodeRankEmbed-137M...")
    coderank = SentenceTransformer('nomic-ai/CodeRankEmbed', trust_remote_code=True)
    
    transformer_module = coderank[0].auto_model
    if not hasattr(transformer_module, 'get_extended_attention_mask'):
        def get_extended_attention_mask(self, attention_mask, input_shape):
            if attention_mask.dim() == 3:
                extended_attention_mask = attention_mask[:, None, :, :]
            elif attention_mask.dim() == 2:
                extended_attention_mask = attention_mask[:, None, None, :]
            else:
                raise ValueError(f'Wrong shape: {attention_mask.shape}')
            extended_attention_mask = extended_attention_mask.to(dtype=self.dtype)
            extended_attention_mask = (1.0 - extended_attention_mask) * -10000.0
            return extended_attention_mask
        transformer_module.__class__.get_extended_attention_mask = get_extended_attention_mask
        
    return minilm, coderank

def cosine_sim(a, b):
    dot = np.dot(a, b)
    norm_a = np.linalg.norm(a)
    norm_b = np.linalg.norm(b)
    if norm_a == 0 or norm_b == 0:
        return 0.0
    return float(dot / (norm_a * norm_b))

def compute_ranking_metrics(rankings, relevant_set, k=5):
    p1 = 1.0 if rankings and rankings[0] in relevant_set else 0.0
    pk = sum(1.0 for r in rankings[:k] if r in relevant_set) / float(k)
    mrr = 0.0
    for idx, r in enumerate(rankings):
        if r in relevant_set:
            mrr = 1.0 / (idx + 1)
            break
    dcg = 0.0
    for idx, r in enumerate(rankings[:k]):
        if r in relevant_set:
            dcg += 1.0 / math.log2(idx + 2)
    idcg = sum(1.0 / math.log2(i + 2) for i in range(min(len(relevant_set), k)))
    ndcg = (dcg / idcg) if idcg > 0 else 0.0
    rec10 = sum(1.0 for r in rankings[:10] if r in relevant_set) / float(len(relevant_set)) if relevant_set else 0.0
    return p1, pk, mrr, ndcg, rec10

def main():
    minilm, coderank = setup_models()
    
    corpus = [
        {"id": "InvertedIndex", "text": "class InvertedIndex: In-memory inverted index posting lists term frequency dictionary for lexical search engine"},
        {"id": "RelationshipGraph", "text": "class RelationshipGraph: Directed multi-graph storing AST call inheritance and include dependencies between symbols"},
        {"id": "CallExtractor", "text": "class CallExtractor: AST tree-sitter visitor extracting function calls caller callee relationships"},
        {"id": "EvidenceAssembler", "text": "class EvidenceAssembler: Assembles structural lexical semantic evidence into verified EvidenceBundles"},
        {"id": "QueryUnderstanding", "text": "class QueryUnderstanding: Decomposes query into subject roles action bases and filters normalization"},
        {"id": "SupportingEvidenceResolver", "text": "class SupportingEvidenceResolver: Resolves secondary supporting AST elements attached to primary units"},
        {"id": "PretrainedEmbeddingProvider", "text": "class PretrainedEmbeddingProvider: Dense vector embedding provider for semantic code and query representations"},
        {"id": "useCalendar", "text": "function useCalendar: React hook managing calendar state navigation currentMonth selectedDate"},
        {"id": "CalendarMonthView", "text": "component CalendarMonthView: UI component rendering calendar grid days weeks header"},
        {"id": "CodeTokenizer", "text": "class CodeTokenizer: Tokenizer splitting identifiers subwords camelCase snake_case and syntax tokens"},
        {"id": "SourceParser", "text": "class SourceParser: Multi-language source parser using tree-sitter grammars to extract AST elements"},
        {"id": "ImportExtractor", "text": "class ImportExtractor: AST visitor extracting imports includes require statements from source files"},
        {"id": "RelationshipAwareSearch", "text": "class RelationshipAwareSearch: Graph-augmented search expanding primary query matches across callers and dependencies"},
        {"id": "NotesStorage", "text": "class NotesStorage: Local SQLite database persistence layer for user markdown notes"},
        {"id": "AuthService", "text": "class AuthService: User authentication credential verification and session management service"},
        {"id": "OAuthClient", "text": "class OAuthClient: OAuth2 client configuring authorization endpoints client credentials token refresh"},
        {"id": "PaymentService", "text": "class PaymentService: Payment gateway processing credit card charges subscriptions invoices"},
        {"id": "AppRouter", "text": "class AppRouter: HTTP request router dispatching API routes to controller handlers"},
        {"id": "RepositoryScanner", "text": "class RepositoryScanner: Recursive filesystem scanner discovering supported source code files"}
    ]
    corpus_ids = [d["id"] for d in corpus]
    
    minilm_corpus_embs = minilm.encode([d["text"] for d in corpus], normalize_embeddings=True)
    coderank_corpus_embs = coderank.encode([f"search_document: {d['text']}" for d in corpus], normalize_embeddings=True)
    
    # Latency assumptions from empirical measurements
    LATENCY_MINILM = 7.70
    LATENCY_CODERANK = 18.63
    LATENCY_DUAL = 27.04
    
    queries = [
        # Technical / Code-Structure
        {"query": "Where is the inverted index implemented?", "relevant": ["InvertedIndex"], "category": "Technical / Code-Structure"},
        {"query": "Where are relationships stored?", "relevant": ["RelationshipGraph"], "category": "Technical / Code-Structure"},
        {"query": "Where are callers resolved?", "relevant": ["CallExtractor", "RelationshipGraph"], "category": "Technical / Code-Structure"},
        {"query": "Which function parses source code?", "relevant": ["SourceParser"], "category": "Technical / Code-Structure"},
        {"query": "Where are imports resolved?", "relevant": ["ImportExtractor"], "category": "Technical / Code-Structure"},
        {"query": "Where are function calls resolved?", "relevant": ["CallExtractor"], "category": "Technical / Code-Structure"},
        {"query": "Where is token extraction performed?", "relevant": ["CodeTokenizer"], "category": "Technical / Code-Structure"},
        
        # Conceptual / Natural Language
        {"query": "How does the calendar navigate between months?", "relevant": ["useCalendar", "CalendarMonthView"], "category": "Conceptual / Natural Language"},
        {"query": "Where does the calendar store state?", "relevant": ["useCalendar"], "category": "Conceptual / Natural Language"},
        {"query": "Where is evidence assembled?", "relevant": ["EvidenceAssembler"], "category": "Conceptual / Natural Language"},
        {"query": "Where is the query normalized?", "relevant": ["QueryUnderstanding"], "category": "Conceptual / Natural Language"},
        {"query": "Where are supporting AST elements resolved?", "relevant": ["SupportingEvidenceResolver"], "category": "Conceptual / Natural Language"},
        {"query": "Where is the embedding provider implemented?", "relevant": ["PretrainedEmbeddingProvider"], "category": "Conceptual / Natural Language"},
        {"query": "Where is relationship-aware search implemented?", "relevant": ["RelationshipAwareSearch"], "category": "Conceptual / Natural Language"},
        
        # Domain Services
        {"query": "Where is the payment checkout?", "relevant": ["PaymentService"], "category": "Domain Service"},
        {"query": "Where is JWT authentication implemented?", "relevant": ["AuthService"], "category": "Domain Service"},
        {"query": "Where is OAuth implemented?", "relevant": ["OAuthClient"], "category": "Domain Service"},
        
        # Adversarial Negative (No relevant match)
        {"query": "Where is the Kubernetes deployment?", "relevant": [], "category": "Adversarial Negative"},
        {"query": "Where is the GraphQL resolver?", "relevant": [], "category": "Adversarial Negative"}
    ]
    
    # Policies to test:
    # 1. Oracle Upper Bound: Choose best mode (MiniLM if correct, else CodeRank if correct, else Dual RRF)
    # 2. Policy A (Categorical Intent):
    #    - IdentifierOrTechnical -> CodeRank
    #    - NaturalLanguage -> MiniLM
    #    - GeneralSearch -> MiniLM
    # 3. Policy B (Intent + Technical Concept Detection):
    #    - IdentifierOrTechnical -> CodeRank
    #    - NaturalLanguage with technical code keywords (function, parse, ast, token) -> CodeRank
    #    - Pure NaturalLanguage -> MiniLM
    #    - GeneralSearch -> MiniLM
    # 4. Policy C (Selective Dual RRF for Ambiguous / Technical Questions):
    #    - Pure NaturalLanguage without technical keywords -> MiniLM
    #    - IdentifierOrTechnical -> CodeRank
    #    - NaturalLanguage with technical keywords -> Dual RRF
    
    per_query_data = []
    
    policy_names = [
        "Always_MiniLM",
        "Always_CodeRank",
        "Always_Dual_RRF",
        "Oracle_Router",
        "Policy_A_IntentOnly",
        "Policy_B_IntentAndKeywords",
        "Policy_C_SelectiveDual"
    ]
    
    policy_metrics = {p: {"P@1": [], "MRR": [], "NDCG@5": [], "Recall@10": [], "latencies": [], "mode_counts": {"MiniLM": 0, "CodeRank": 0, "Dual_RRF": 0}} for p in policy_names}
    
    for q_item in queries:
        q_text = q_item["query"]
        rel_set = q_item["relevant"]
        q_analysis = analyze_query(q_text)
        
        # MiniLM channel
        minilm_q_emb = minilm.encode(q_text, normalize_embeddings=True)
        minilm_scores = {d_id: cosine_sim(minilm_q_emb, d_emb) for d_id, d_emb in zip(corpus_ids, minilm_corpus_embs)}
        minilm_sorted = sorted(minilm_scores.items(), key=lambda x: x[1], reverse=True)
        minilm_ranks = {item[0]: idx + 1 for idx, item in enumerate(minilm_sorted)}
        minilm_rankings = [x[0] for x in minilm_sorted]
        
        # CodeRank channel
        coderank_q_emb = coderank.encode(f"search_query: {q_text}", normalize_embeddings=True)
        coderank_scores = {d_id: cosine_sim(coderank_q_emb, d_emb) for d_id, d_emb in zip(corpus_ids, coderank_corpus_embs)}
        coderank_sorted = sorted(coderank_scores.items(), key=lambda x: x[1], reverse=True)
        coderank_ranks = {item[0]: idx + 1 for idx, item in enumerate(coderank_sorted)}
        coderank_rankings = [x[0] for x in coderank_sorted]
        
        # Dual RRF (k=60)
        rrf_scores = {k: (1.0 / (60.0 + minilm_ranks[k])) + (1.0 / (60.0 + coderank_ranks[k])) for k in corpus_ids}
        dual_sorted = sorted(rrf_scores.items(), key=lambda x: x[1], reverse=True)
        dual_rankings = [x[0] for x in dual_sorted]
        
        # Measure individual success
        minilm_correct = (minilm_rankings[0] in rel_set) if rel_set else True
        coderank_correct = (coderank_rankings[0] in rel_set) if rel_set else True
        dual_correct = (dual_rankings[0] in rel_set) if rel_set else True
        
        # 1. Oracle Routing Decision
        if minilm_correct:
            oracle_mode = "MiniLM"
            oracle_rankings = minilm_rankings
            oracle_latency = LATENCY_MINILM
        elif coderank_correct:
            oracle_mode = "CodeRank"
            oracle_rankings = coderank_rankings
            oracle_latency = LATENCY_CODERANK
        else:
            oracle_mode = "Dual_RRF"
            oracle_rankings = dual_rankings
            oracle_latency = LATENCY_DUAL
            
        # 2. Policy A Decision (Intent Only)
        if q_analysis["intent"] == "IdentifierOrTechnical":
            policy_a_mode = "CodeRank"
            policy_a_rankings = coderank_rankings
            policy_a_latency = LATENCY_CODERANK
        else:
            policy_a_mode = "MiniLM"
            policy_a_rankings = minilm_rankings
            policy_a_latency = LATENCY_MINILM
            
        # 3. Policy B Decision (Intent + Technical Concept Keywords)
        if q_analysis["intent"] == "IdentifierOrTechnical" or q_analysis["has_technical_concept"]:
            policy_b_mode = "CodeRank"
            policy_b_rankings = coderank_rankings
            policy_b_latency = LATENCY_CODERANK
        else:
            policy_b_mode = "MiniLM"
            policy_b_rankings = minilm_rankings
            policy_b_latency = LATENCY_MINILM
            
        # 4. Policy C Decision (Selective Dual RRF on Technical NL Questions)
        if q_analysis["intent"] == "IdentifierOrTechnical":
            policy_c_mode = "CodeRank"
            policy_c_rankings = coderank_rankings
            policy_c_latency = LATENCY_CODERANK
        elif q_analysis["intent"] == "NaturalLanguage" and q_analysis["has_technical_concept"]:
            policy_c_mode = "Dual_RRF"
            policy_c_rankings = dual_rankings
            policy_c_latency = LATENCY_DUAL
        else:
            policy_c_mode = "MiniLM"
            policy_c_rankings = minilm_rankings
            policy_c_latency = LATENCY_MINILM
            
        decisions = {
            "Always_MiniLM": ("MiniLM", minilm_rankings, LATENCY_MINILM),
            "Always_CodeRank": ("CodeRank", coderank_rankings, LATENCY_CODERANK),
            "Always_Dual_RRF": ("Dual_RRF", dual_rankings, LATENCY_DUAL),
            "Oracle_Router": (oracle_mode, oracle_rankings, oracle_latency),
            "Policy_A_IntentOnly": (policy_a_mode, policy_a_rankings, policy_a_latency),
            "Policy_B_IntentAndKeywords": (policy_b_mode, policy_b_rankings, policy_b_latency),
            "Policy_C_SelectiveDual": (policy_c_mode, policy_c_rankings, policy_c_latency),
        }
        
        per_query_data.append({
            "query": q_text,
            "category": q_item["category"],
            "analysis": q_analysis,
            "relevant": rel_set,
            "minilm_top": minilm_sorted[0],
            "coderank_top": coderank_sorted[0],
            "dual_top": dual_sorted[0],
            "decisions": {p: decisions[p][0] for p in policy_names}
        })
        
        for p in policy_names:
            mode_chosen, ranks_chosen, lat_chosen = decisions[p]
            policy_metrics[p]["latencies"].append(lat_chosen)
            policy_metrics[p]["mode_counts"][mode_chosen] += 1
            
            if rel_set:
                p1, pk, mrr, ndcg, rec10 = compute_ranking_metrics(ranks_chosen, rel_set)
                policy_metrics[p]["P@1"].append(p1)
                policy_metrics[p]["MRR"].append(mrr)
                policy_metrics[p]["NDCG@5"].append(ndcg)
                policy_metrics[p]["Recall@10"].append(rec10)
                
    summary_report = {}
    for p in policy_names:
        total_queries = len(queries)
        counts = policy_metrics[p]["mode_counts"]
        summary_report[p] = {
            "P@1": float(np.mean(policy_metrics[p]["P@1"])),
            "MRR": float(np.mean(policy_metrics[p]["MRR"])),
            "NDCG@5": float(np.mean(policy_metrics[p]["NDCG@5"])),
            "Recall@10": float(np.mean(policy_metrics[p]["Recall@10"])),
            "mean_latency_ms": float(np.mean(policy_metrics[p]["latencies"])),
            "p50_latency_ms": float(np.percentile(policy_metrics[p]["latencies"], 50)),
            "p95_latency_ms": float(np.percentile(policy_metrics[p]["latencies"], 95)),
            "mode_distribution_pct": {
                "MiniLM": float(counts["MiniLM"] / total_queries * 100.0),
                "CodeRank": float(counts["CodeRank"] / total_queries * 100.0),
                "Dual_RRF": float(counts["Dual_RRF"] / total_queries * 100.0)
            }
        }
        
    out = {
        "summary": summary_report,
        "queries": per_query_data
    }
    
    with open("benchmarks/adaptive_routing_evaluation.json", "w") as f:
        json.dump(out, f, indent=2)
    print("Adaptive routing evaluation saved to benchmarks/adaptive_routing_evaluation.json")

if __name__ == "__main__":
    main()
