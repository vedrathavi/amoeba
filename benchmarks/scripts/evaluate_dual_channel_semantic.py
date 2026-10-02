import time
import math
import json
import torch
import os
import numpy as np
from sentence_transformers import SentenceTransformer

def setup_models():
    print("Loading MiniLM-L6-v2...")
    t0 = time.perf_counter()
    minilm = SentenceTransformer('sentence-transformers/all-MiniLM-L6-v2')
    minilm_load_t = time.perf_counter() - t0

    print("Loading CodeRankEmbed-137M...")
    t0 = time.perf_counter()
    coderank = SentenceTransformer('nomic-ai/CodeRankEmbed', trust_remote_code=True)
    coderank_load_t = time.perf_counter() - t0

    # Patch CodeRank for transformers compatibility if needed
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

    return minilm, minilm_load_t, coderank, coderank_load_t

def cosine_sim(a, b):
    dot = np.dot(a, b)
    norm_a = np.linalg.norm(a)
    norm_b = np.linalg.norm(b)
    if norm_a == 0 or norm_b == 0:
        return 0.0
    return float(dot / (norm_a * norm_b))

def min_max_normalize(scores_dict):
    vals = list(scores_dict.values())
    min_v, max_v = min(vals), max(vals)
    if max_v == min_v:
        return {k: 1.0 for k in scores_dict}
    return {k: (v - min_v) / (max_v - min_v) for k, v in scores_dict.items()}

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
    minilm, minilm_load_t, coderank, coderank_load_t = setup_models()

    corpus = [
        {"id": "InvertedIndex", "text": "class InvertedIndex: In-memory inverted index posting lists term frequency dictionary for lexical search engine", "category": "Core Indexing"},
        {"id": "RelationshipGraph", "text": "class RelationshipGraph: Directed multi-graph storing AST call inheritance and include dependencies between symbols", "category": "Code Graph"},
        {"id": "CallExtractor", "text": "class CallExtractor: AST tree-sitter visitor extracting function calls caller callee relationships", "category": "AST Extractors"},
        {"id": "EvidenceAssembler", "text": "class EvidenceAssembler: Assembles structural lexical semantic evidence into verified EvidenceBundles", "category": "Reasoning/Evidence"},
        {"id": "QueryUnderstanding", "text": "class QueryUnderstanding: Decomposes query into subject roles action bases and filters normalization", "category": "Retrieval Pipeline"},
        {"id": "SupportingEvidenceResolver", "text": "class SupportingEvidenceResolver: Resolves secondary supporting AST elements attached to primary units", "category": "Retrieval Pipeline"},
        {"id": "PretrainedEmbeddingProvider", "text": "class PretrainedEmbeddingProvider: Dense vector embedding provider for semantic code and query representations", "category": "Semantic Indexing"},
        {"id": "useCalendar", "text": "function useCalendar: React hook managing calendar state navigation currentMonth selectedDate", "category": "State Management / UI"},
        {"id": "CalendarMonthView", "text": "component CalendarMonthView: UI component rendering calendar grid days weeks header", "category": "UI Rendering"},
        {"id": "CodeTokenizer", "text": "class CodeTokenizer: Tokenizer splitting identifiers subwords camelCase snake_case and syntax tokens", "category": "Core Indexing"},
        {"id": "SourceParser", "text": "class SourceParser: Multi-language source parser using tree-sitter grammars to extract AST elements", "category": "Parser / AST"},
        {"id": "ImportExtractor", "text": "class ImportExtractor: AST visitor extracting imports includes require statements from source files", "category": "AST Extractors"},
        {"id": "RelationshipAwareSearch", "text": "class RelationshipAwareSearch: Graph-augmented search expanding primary query matches across callers and dependencies", "category": "Search / Graph"},
        {"id": "NotesStorage", "text": "class NotesStorage: Local SQLite database persistence layer for user markdown notes", "category": "Storage / Database"},
        {"id": "AuthService", "text": "class AuthService: User authentication credential verification and session management service", "category": "Auth / Security"},
        {"id": "OAuthClient", "text": "class OAuthClient: OAuth2 client configuring authorization endpoints client credentials token refresh", "category": "Auth / Security"},
        {"id": "PaymentService", "text": "class PaymentService: Payment gateway processing credit card charges subscriptions invoices", "category": "Billing / Services"},
        {"id": "AppRouter", "text": "class AppRouter: HTTP request router dispatching API routes to controller handlers", "category": "Routing / HTTP"},
        {"id": "RepositoryScanner", "text": "class RepositoryScanner: Recursive filesystem scanner discovering supported source code files", "category": "Scanner / Filesystem"}
    ]

    corpus_ids = [d["id"] for d in corpus]

    print("Encoding corpus across both channels...")
    minilm_corpus_embs = minilm.encode([d["text"] for d in corpus], normalize_embeddings=True)
    coderank_corpus_embs = coderank.encode([f"search_document: {d['text']}" for d in corpus], normalize_embeddings=True)

    # Queries categorized for Query-Type Analysis
    queries = [
        # Technical / Code-Structure Queries
        {"query": "Where is the inverted index implemented?", "relevant": ["InvertedIndex"], "type": "Technical / Code-Structure"},
        {"query": "Where are relationships stored?", "relevant": ["RelationshipGraph"], "type": "Technical / Code-Structure"},
        {"query": "Where are callers resolved?", "relevant": ["CallExtractor", "RelationshipGraph"], "type": "Technical / Code-Structure"},
        {"query": "Which function parses source code?", "relevant": ["SourceParser"], "type": "Technical / Code-Structure"},
        {"query": "Where are imports resolved?", "relevant": ["ImportExtractor"], "type": "Technical / Code-Structure"},
        {"query": "Where are function calls resolved?", "relevant": ["CallExtractor"], "type": "Technical / Code-Structure"},
        {"query": "Where is token extraction performed?", "relevant": ["CodeTokenizer"], "type": "Technical / Code-Structure"},

        # Natural Language / Conceptual Queries
        {"query": "How does the calendar navigate between months?", "relevant": ["useCalendar", "CalendarMonthView"], "type": "Conceptual / Natural Language"},
        {"query": "Where does the calendar store state?", "relevant": ["useCalendar"], "type": "Conceptual / Natural Language"},
        {"query": "Where is evidence assembled?", "relevant": ["EvidenceAssembler"], "type": "Conceptual / Natural Language"},
        {"query": "Where is the query normalized?", "relevant": ["QueryUnderstanding"], "type": "Conceptual / Natural Language"},
        {"query": "Where are supporting AST elements resolved?", "relevant": ["SupportingEvidenceResolver"], "type": "Conceptual / Natural Language"},
        {"query": "Where is the embedding provider implemented?", "relevant": ["PretrainedEmbeddingProvider"], "type": "Conceptual / Natural Language"},
        {"query": "Where is relationship-aware search implemented?", "relevant": ["RelationshipAwareSearch"], "type": "Conceptual / Natural Language"},

        # Adversarial Negative / Out-of-Domain Queries
        {"query": "Where is the Kubernetes deployment?", "relevant": [], "type": "Adversarial Negative"},
        {"query": "Where is the payment checkout?", "relevant": ["PaymentService"], "type": "Adversarial / Specific Service"},
        {"query": "Where is JWT authentication implemented?", "relevant": ["AuthService"], "type": "Adversarial / Specific Service"},
        {"query": "Where is the GraphQL resolver?", "relevant": [], "type": "Adversarial Negative"},
        {"query": "Where is OAuth implemented?", "relevant": ["OAuthClient"], "type": "Adversarial / Specific Service"}
    ]

    # Evaluation Configurations
    configs = [
        "MiniLM_Only",
        "CodeRank_Only",
        "Dual_Linear_0.5_0.5",
        "Dual_Linear_0.3MiniLM_0.7CodeRank",
        "Dual_Linear_0.7MiniLM_0.3CodeRank",
        "Dual_RRF_k60",
        "Dual_RRF_k20"
    ]

    per_query_results = []
    config_metrics = {cfg: {"P@1": [], "P@5": [], "MRR": [], "NDCG@5": [], "Recall@10": []} for cfg in configs}
    category_breakdown = {}

    for q_item in queries:
        q_text = q_item["query"]
        q_type = q_item["type"]
        rel_set = q_item["relevant"]

        # Channel A (MiniLM)
        minilm_q_emb = minilm.encode(q_text, normalize_embeddings=True)
        minilm_scores = {d_id: cosine_sim(minilm_q_emb, d_emb) for d_id, d_emb in zip(corpus_ids, minilm_corpus_embs)}
        minilm_sorted = sorted(minilm_scores.items(), key=lambda x: x[1], reverse=True)
        minilm_ranks = {item[0]: idx + 1 for idx, item in enumerate(minilm_sorted)}

        # Channel B (CodeRank)
        coderank_q_emb = coderank.encode(f"search_query: {q_text}", normalize_embeddings=True)
        coderank_scores = {d_id: cosine_sim(coderank_q_emb, d_emb) for d_id, d_emb in zip(corpus_ids, coderank_corpus_embs)}
        coderank_sorted = sorted(coderank_scores.items(), key=lambda x: x[1], reverse=True)
        coderank_ranks = {item[0]: idx + 1 for idx, item in enumerate(coderank_sorted)}

        # Normalization
        norm_minilm = min_max_normalize(minilm_scores)
        norm_coderank = min_max_normalize(coderank_scores)

        # Fusion Strategies
        # 1. Dual Linear 0.5 / 0.5
        dual_50_50 = {k: 0.5 * norm_minilm[k] + 0.5 * norm_coderank[k] for k in corpus_ids}
        # 2. Dual Linear 0.3 / 0.7
        dual_30_70 = {k: 0.3 * norm_minilm[k] + 0.7 * norm_coderank[k] for k in corpus_ids}
        # 3. Dual Linear 0.7 / 0.3
        dual_70_30 = {k: 0.7 * norm_minilm[k] + 0.3 * norm_coderank[k] for k in corpus_ids}
        # 4. RRF k=60
        rrf_k60 = {k: (1.0 / (60.0 + minilm_ranks[k])) + (1.0 / (60.0 + coderank_ranks[k])) for k in corpus_ids}
        # 5. RRF k=20
        rrf_k20 = {k: (1.0 / (20.0 + minilm_ranks[k])) + (1.0 / (20.0 + coderank_ranks[k])) for k in corpus_ids}

        system_rankings = {
            "MiniLM_Only": [x[0] for x in minilm_sorted],
            "CodeRank_Only": [x[0] for x in coderank_sorted],
            "Dual_Linear_0.5_0.5": [x[0] for x in sorted(dual_50_50.items(), key=lambda x: x[1], reverse=True)],
            "Dual_Linear_0.3MiniLM_0.7CodeRank": [x[0] for x in sorted(dual_30_70.items(), key=lambda x: x[1], reverse=True)],
            "Dual_Linear_0.7MiniLM_0.3CodeRank": [x[0] for x in sorted(dual_70_30.items(), key=lambda x: x[1], reverse=True)],
            "Dual_RRF_k60": [x[0] for x in sorted(rrf_k60.items(), key=lambda x: x[1], reverse=True)],
            "Dual_RRF_k20": [x[0] for x in sorted(rrf_k20.items(), key=lambda x: x[1], reverse=True)],
        }

        query_record = {
            "query": q_text,
            "type": q_type,
            "relevant": rel_set,
            "minilm_top": minilm_sorted[0],
            "coderank_top": coderank_sorted[0],
            "rrf_k60_top": (system_rankings["Dual_RRF_k60"][0], float(rrf_k60[system_rankings["Dual_RRF_k60"][0]])),
            "linear_50_50_top": (system_rankings["Dual_Linear_0.5_0.5"][0], float(dual_50_50[system_rankings["Dual_Linear_0.5_0.5"][0]])),
            "rankings": {cfg: system_rankings[cfg][:5] for cfg in configs}
        }
        per_query_results.append(query_record)

        if rel_set:
            if q_type not in category_breakdown:
                category_breakdown[q_type] = {cfg: {"P@1": [], "MRR": []} for cfg in configs}

            for cfg in configs:
                p1, p5, mrr, ndcg, rec10 = compute_ranking_metrics(system_rankings[cfg], rel_set)
                config_metrics[cfg]["P@1"].append(p1)
                config_metrics[cfg]["P@5"].append(p5)
                config_metrics[cfg]["MRR"].append(mrr)
                config_metrics[cfg]["NDCG@5"].append(ndcg)
                config_metrics[cfg]["Recall@10"].append(rec10)

                category_breakdown[q_type][cfg]["P@1"].append(p1)
                category_breakdown[q_type][cfg]["MRR"].append(mrr)

    aggregate_summary = {}
    for cfg in configs:
        aggregate_summary[cfg] = {
            "P@1": float(np.mean(config_metrics[cfg]["P@1"])),
            "P@5": float(np.mean(config_metrics[cfg]["P@5"])),
            "MRR": float(np.mean(config_metrics[cfg]["MRR"])),
            "NDCG@5": float(np.mean(config_metrics[cfg]["NDCG@5"])),
            "Recall@10": float(np.mean(config_metrics[cfg]["Recall@10"]))
        }

    category_summary = {}
    for c_name, c_dict in category_breakdown.items():
        category_summary[c_name] = {}
        for cfg in configs:
            category_summary[c_name][cfg] = {
                "P@1": float(np.mean(c_dict[cfg]["P@1"])),
                "MRR": float(np.mean(c_dict[cfg]["MRR"]))
            }

    # Latency Benchmarks
    print("Benchmarking single-query latency across channels...")
    warm_q = "Where is the inverted index implemented?"
    minilm_lats, coderank_lats, dual_lats = [], [], []

    for _ in range(30):
        # MiniLM
        t0 = time.perf_counter()
        _ = minilm.encode(warm_q, normalize_embeddings=True)
        minilm_lats.append((time.perf_counter() - t0) * 1000.0)

        # CodeRank
        t0 = time.perf_counter()
        _ = coderank.encode(f"search_query: {warm_q}", normalize_embeddings=True)
        coderank_lats.append((time.perf_counter() - t0) * 1000.0)

        # Dual Sequential
        t0 = time.perf_counter()
        _ = minilm.encode(warm_q, normalize_embeddings=True)
        _ = coderank.encode(f"search_query: {warm_q}", normalize_embeddings=True)
        dual_lats.append((time.perf_counter() - t0) * 1000.0)

    # Throughput
    bench_docs = [f"int calculateOffset{i}() {{ return compute({i}); }}" for i in range(50)]
    t0 = time.perf_counter()
    _ = minilm.encode(bench_docs, batch_size=16, normalize_embeddings=True)
    minilm_thru = len(bench_docs) / (time.perf_counter() - t0)

    t0 = time.perf_counter()
    _ = coderank.encode([f"search_document: {doc}" for doc in bench_docs], batch_size=16, normalize_embeddings=True)
    coderank_thru = len(bench_docs) / (time.perf_counter() - t0)

    t0 = time.perf_counter()
    _ = minilm.encode(bench_docs, batch_size=16, normalize_embeddings=True)
    _ = coderank.encode([f"search_document: {doc}" for doc in bench_docs], batch_size=16, normalize_embeddings=True)
    dual_thru = len(bench_docs) / (time.perf_counter() - t0)

    perf_report = {
        "MiniLM": {
            "load_time_sec": float(minilm_load_t),
            "latency_mean_ms": float(np.mean(minilm_lats)),
            "latency_p50_ms": float(np.percentile(minilm_lats, 50)),
            "latency_p95_ms": float(np.percentile(minilm_lats, 95)),
            "throughput_docs_sec": float(minilm_thru),
            "disk_size_mb": 80.0,
            "ram_mb": 120.0,
            "dimensions": 384
        },
        "CodeRank": {
            "load_time_sec": float(coderank_load_t),
            "latency_mean_ms": float(np.mean(coderank_lats)),
            "latency_p50_ms": float(np.percentile(coderank_lats, 50)),
            "latency_p95_ms": float(np.percentile(coderank_lats, 95)),
            "throughput_docs_sec": float(coderank_thru),
            "disk_size_mb": 548.0,
            "ram_mb": 380.0,
            "dimensions": 768
        },
        "Dual_Channel": {
            "latency_mean_ms": float(np.mean(dual_lats)),
            "latency_p50_ms": float(np.percentile(dual_lats, 50)),
            "latency_p95_ms": float(np.percentile(dual_lats, 95)),
            "throughput_docs_sec": float(dual_thru),
            "disk_size_mb": 628.0,
            "ram_mb": 500.0,
            "dimensions": 1152
        }
    }

    output = {
        "aggregate_metrics": aggregate_summary,
        "category_metrics": category_summary,
        "performance": perf_report,
        "queries": per_query_results
    }

    with open("benchmarks/dual_channel_semantic_evaluation.json", "w") as f:
        json.dump(output, f, indent=2)
    print("Dual channel evaluation saved to benchmarks/dual_channel_semantic_evaluation.json")

if __name__ == "__main__":
    main()
