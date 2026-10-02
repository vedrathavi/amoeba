import time
import math
import json
import torch
import os
import numpy as np
from sentence_transformers import SentenceTransformer

def setup_coderank_model():
    print("Loading nomic-ai/CodeRankEmbed...")
    t0 = time.perf_counter()
    model = SentenceTransformer('nomic-ai/CodeRankEmbed', trust_remote_code=True)
    load_time = time.perf_counter() - t0
    
    # Compatibility fix for newer transformers versions
    transformer_module = model[0].auto_model
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
        
    return model, load_time

def setup_minilm_model():
    print("Loading sentence-transformers/all-MiniLM-L6-v2...")
    t0 = time.perf_counter()
    model = SentenceTransformer('sentence-transformers/all-MiniLM-L6-v2')
    load_time = time.perf_counter() - t0
    return model, load_time

def cosine_sim(a, b):
    dot = np.dot(a, b)
    norm_a = np.linalg.norm(a)
    norm_b = np.linalg.norm(b)
    if norm_a == 0 or norm_b == 0:
        return 0.0
    return float(dot / (norm_a * norm_b))

def compute_ranking_metrics(rankings, relevant_set, k=5):
    # P@1
    p1 = 1.0 if rankings and rankings[0] in relevant_set else 0.0
    # P@k
    pk = sum(1.0 for r in rankings[:k] if r in relevant_set) / float(k)
    # MRR
    mrr = 0.0
    for idx, r in enumerate(rankings):
        if r in relevant_set:
            mrr = 1.0 / (idx + 1)
            break
    # NDCG@k
    dcg = 0.0
    for idx, r in enumerate(rankings[:k]):
        if r in relevant_set:
            dcg += 1.0 / math.log2(idx + 2)
    idcg = sum(1.0 / math.log2(i + 2) for i in range(min(len(relevant_set), k)))
    ndcg = (dcg / idcg) if idcg > 0 else 0.0
    # Recall@10
    rec10 = sum(1.0 for r in rankings[:10] if r in relevant_set) / float(len(relevant_set)) if relevant_set else 0.0
    return p1, pk, mrr, ndcg, rec10

def run_evaluation():
    # 1. Setup Models
    minilm, minilm_load_t = setup_minilm_model()
    coderank, coderank_load_t = setup_coderank_model()
    
    # 2. Benchmark Corpus (Amoeba Core Retrieval Units)
    corpus = [
        {"id": "InvertedIndex", "text": "class InvertedIndex: In-memory inverted index posting lists term frequency dictionary for lexical search engine", "kind": "Class", "file": "engine/src/index/inverted_index.cpp"},
        {"id": "RelationshipGraph", "text": "class RelationshipGraph: Directed multi-graph storing AST call inheritance and include dependencies between symbols", "kind": "Class", "file": "engine/src/graph/relationship_graph.cpp"},
        {"id": "CallExtractor", "text": "class CallExtractor: AST tree-sitter visitor extracting function calls caller callee relationships", "kind": "Class", "file": "engine/src/graph/call_extractor.cpp"},
        {"id": "EvidenceAssembler", "text": "class EvidenceAssembler: Assembles structural lexical semantic evidence into verified EvidenceBundles", "kind": "Class", "file": "engine/src/evidence/evidence_assembler.cpp"},
        {"id": "QueryUnderstanding", "text": "class QueryUnderstanding: Decomposes query into subject roles action bases and filters normalization", "kind": "Class", "file": "engine/src/retrieval/query_understanding.cpp"},
        {"id": "SupportingEvidenceResolver", "text": "class SupportingEvidenceResolver: Resolves secondary supporting AST elements attached to primary units", "kind": "Class", "file": "engine/src/retrieval/supporting_evidence_resolver.cpp"},
        {"id": "PretrainedEmbeddingProvider", "text": "class PretrainedEmbeddingProvider: Dense vector embedding provider for semantic code and query representations", "kind": "Class", "file": "engine/src/semantic/pretrained_embedding_provider.cpp"},
        {"id": "useCalendar", "text": "function useCalendar: React hook managing calendar state navigation currentMonth selectedDate", "kind": "Hook", "file": "apps/web/src/hooks/useCalendar.ts"},
        {"id": "CalendarMonthView", "text": "component CalendarMonthView: UI component rendering calendar grid days weeks header", "kind": "Component", "file": "apps/web/src/components/CalendarMonthView.tsx"},
        {"id": "CodeTokenizer", "text": "class CodeTokenizer: Tokenizer splitting identifiers subwords camelCase snake_case and syntax tokens", "kind": "Class", "file": "engine/src/index/code_tokenizer.cpp"},
        {"id": "SourceParser", "text": "class SourceParser: Multi-language source parser using tree-sitter grammars to extract AST elements", "kind": "Class", "file": "engine/src/parser/source_parser.cpp"},
        {"id": "ImportExtractor", "text": "class ImportExtractor: AST visitor extracting imports includes require statements from source files", "kind": "Class", "file": "engine/src/graph/import_extractor.cpp"},
        {"id": "RelationshipAwareSearch", "text": "class RelationshipAwareSearch: Graph-augmented search expanding primary query matches across callers and dependencies", "kind": "Class", "file": "engine/src/search/relationship_aware_search.cpp"},
        {"id": "NotesStorage", "text": "class NotesStorage: Local SQLite database persistence layer for user markdown notes", "kind": "Class", "file": "apps/desktop/src/storage/notes_storage.cpp"},
        {"id": "AuthService", "text": "class AuthService: User authentication credential verification and session management service", "kind": "Class", "file": "services/auth/auth_service.ts"},
        {"id": "OAuthClient", "text": "class OAuthClient: OAuth2 client configuring authorization endpoints client credentials token refresh", "kind": "Class", "file": "services/auth/oauth_client.ts"},
        {"id": "PaymentService", "text": "class PaymentService: Payment gateway processing credit card charges subscriptions invoices", "kind": "Class", "file": "services/billing/payment_service.ts"},
        {"id": "AppRouter", "text": "class AppRouter: HTTP request router dispatching API routes to controller handlers", "kind": "Class", "file": "server/router.ts"},
        {"id": "RepositoryScanner", "text": "class RepositoryScanner: Recursive filesystem scanner discovering supported source code files", "kind": "Class", "file": "engine/src/scanner/repository_scanner.cpp"}
    ]
    
    corpus_ids = [d["id"] for d in corpus]
    
    # 3. Encode Corpus
    # MiniLM uses raw text
    minilm_corpus_embs = minilm.encode([d["text"] for d in corpus], normalize_embeddings=True)
    # CodeRank uses "search_document: " prefix
    coderank_corpus_embs = coderank.encode([f"search_document: {d['text']}" for d in corpus], normalize_embeddings=True)
    
    # 4. Latency Measurements (Warm Single Query)
    query_str = "Where is the inverted index implemented?"
    
    # MiniLM latency
    minilm_latencies = []
    for _ in range(30):
        t_s = time.perf_counter()
        _ = minilm.encode(query_str, normalize_embeddings=True)
        minilm_latencies.append((time.perf_counter() - t_s) * 1000.0)
        
    # CodeRank latency
    coderank_latencies = []
    coderank_q_str = f"search_query: {query_str}"
    for _ in range(30):
        t_s = time.perf_counter()
        _ = coderank.encode(coderank_q_str, normalize_embeddings=True)
        coderank_latencies.append((time.perf_counter() - t_s) * 1000.0)
        
    # Batch Throughput (50 documents)
    bench_docs = [f"int computeMetric{i}() {{ return calculateOffset({i}); }}" for i in range(50)]
    
    t_m_start = time.perf_counter()
    _ = minilm.encode(bench_docs, batch_size=16, normalize_embeddings=True)
    minilm_throughput = len(bench_docs) / (time.perf_counter() - t_m_start)
    
    t_c_start = time.perf_counter()
    _ = coderank.encode([f"search_document: {doc}" for doc in bench_docs], batch_size=16, normalize_embeddings=True)
    coderank_throughput = len(bench_docs) / (time.perf_counter() - t_c_start)
    
    # 5. Code-Specific Test Queries (Section 10)
    code_queries = [
        {"query": "Where is the inverted index implemented?", "relevant": ["InvertedIndex"]},
        {"query": "Where are relationships stored?", "relevant": ["RelationshipGraph"]},
        {"query": "Where are callers resolved?", "relevant": ["CallExtractor", "RelationshipGraph"]},
        {"query": "Where is evidence assembled?", "relevant": ["EvidenceAssembler"]},
        {"query": "Where is the query normalized?", "relevant": ["QueryUnderstanding"]},
        {"query": "Where are supporting AST elements resolved?", "relevant": ["SupportingEvidenceResolver"]},
        {"query": "Where is the embedding provider implemented?", "relevant": ["PretrainedEmbeddingProvider"]},
        {"query": "How does the calendar navigate between months?", "relevant": ["useCalendar", "CalendarMonthView"]},
        {"query": "Where does the calendar store state?", "relevant": ["useCalendar"]},
        {"query": "Where is token extraction performed?", "relevant": ["CodeTokenizer"]},
        {"query": "Which function parses source code?", "relevant": ["SourceParser"]},
        {"query": "Where are imports resolved?", "relevant": ["ImportExtractor"]},
        {"query": "Where are function calls resolved?", "relevant": ["CallExtractor"]},
        {"query": "Where is relationship-aware search implemented?", "relevant": ["RelationshipAwareSearch"]}
    ]
    
    # 6. Adversarial Negative Queries (Section 11)
    negative_queries = [
        {"query": "Where is the Kubernetes deployment?", "relevant": []},
        {"query": "Where is the payment checkout?", "relevant": ["PaymentService"]},
        {"query": "Where is JWT authentication?", "relevant": ["AuthService"]},
        {"query": "Where is the GraphQL resolver?", "relevant": []},
        {"query": "Where is the OAuth implementation?", "relevant": ["OAuthClient"]}
    ]
    
    all_eval_queries = code_queries + negative_queries
    
    def evaluate_query_set(queries_list, model, corpus_embs, is_coderank=False):
        results = []
        p1_list, p5_list, mrr_list, ndcg_list, rec_list = [], [], [], [], []
        
        for q_item in queries_list:
            q_text = f"search_query: {q_item['query']}" if is_coderank else q_item['query']
            q_emb = model.encode(q_text, normalize_embeddings=True)
            
            scores = []
            for d_id, d_emb in zip(corpus_ids, corpus_embs):
                sim = cosine_sim(q_emb, d_emb)
                scores.append((d_id, sim))
            scores.sort(key=lambda x: x[1], reverse=True)
            rankings = [x[0] for x in scores]
            
            rel_set = q_item["relevant"]
            if rel_set:
                p1, p5, mrr, ndcg, rec = compute_ranking_metrics(rankings, rel_set)
                p1_list.append(p1)
                p5_list.append(p5)
                mrr_list.append(mrr)
                ndcg_list.append(ndcg)
                rec_list.append(rec)
                top_rank = rankings.index(rel_set[0]) + 1 if rel_set[0] in rankings else -1
            else:
                top_rank = -1
                
            top_id, top_sim = scores[0]
            second_id, second_sim = scores[1] if len(scores) > 1 else ("", 0.0)
            
            results.append({
                "query": q_item["query"],
                "expected": rel_set,
                "top_result": top_id,
                "top_similarity": float(top_sim),
                "second_result": second_id,
                "second_similarity": float(second_sim),
                "separation_margin": float(top_sim - second_sim),
                "rank": int(top_rank),
                "is_relevant": top_id in rel_set if rel_set else False,
                "top_3_scores": [(k, float(round(v, 4))) for k, v in scores[:3]]
            })
            
        metrics = {
            "P@1": float(np.mean(p1_list)) if p1_list else 0.0,
            "P@5": float(np.mean(p5_list)) if p5_list else 0.0,
            "MRR": float(np.mean(mrr_list)) if mrr_list else 0.0,
            "NDCG@5": float(np.mean(ndcg_list)) if ndcg_list else 0.0,
            "Recall@10": float(np.mean(rec_list)) if rec_list else 0.0
        }
        return metrics, results
        
    minilm_metrics, minilm_results = evaluate_query_set(code_queries, minilm, minilm_corpus_embs, False)
    coderank_metrics, coderank_results = evaluate_query_set(code_queries, coderank, coderank_corpus_embs, True)
    
    _, minilm_neg_results = evaluate_query_set(negative_queries, minilm, minilm_corpus_embs, False)
    _, coderank_neg_results = evaluate_query_set(negative_queries, coderank, coderank_corpus_embs, True)
    
    out = {
        "minilm": {
            "model_name": "sentence-transformers/all-MiniLM-L6-v2",
            "parameters": 33_000_000,
            "dimension": 384,
            "load_time_sec": float(minilm_load_t),
            "warm_query_latency_ms": {
                "mean": float(np.mean(minilm_latencies)),
                "p50": float(np.percentile(minilm_latencies, 50)),
                "p95": float(np.percentile(minilm_latencies, 95))
            },
            "batch_throughput_docs_sec": float(minilm_throughput),
            "metrics": minilm_metrics,
            "query_results": minilm_results,
            "negative_results": minilm_neg_results
        },
        "coderank": {
            "model_name": "nomic-ai/CodeRankEmbed",
            "parameters": 137_000_000,
            "dimension": 768,
            "load_time_sec": float(coderank_load_t),
            "warm_query_latency_ms": {
                "mean": float(np.mean(coderank_latencies)),
                "p50": float(np.percentile(coderank_latencies, 50)),
                "p95": float(np.percentile(coderank_latencies, 95))
            },
            "batch_throughput_docs_sec": float(coderank_throughput),
            "metrics": coderank_metrics,
            "query_results": coderank_results,
            "negative_results": coderank_neg_results
        }
    }
    
    with open("benchmarks/real_coderank_vs_minilm_evaluation.json", "w") as f:
        json.dump(out, f, indent=2)
    print("Benchmark completed. Results saved to benchmarks/real_coderank_vs_minilm_evaluation.json")

if __name__ == "__main__":
    run_evaluation()
