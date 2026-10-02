import time
import math
import json
import torch
import os
from sentence_transformers import SentenceTransformer

def cosine_sim(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    norm_a = math.sqrt(sum(x * x for x in a))
    norm_b = math.sqrt(sum(x * x for x in b))
    if norm_a == 0 or norm_b == 0:
        return 0.0
    return dot / (norm_a * norm_b)

def compute_metrics(rankings, relevant_set, k=5):
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

def evaluate_model(model_name):
    print(f"Loading real model: {model_name}...")
    t0 = time.perf_counter()
    model = SentenceTransformer(model_name)
    load_time_sec = time.perf_counter() - t0
    
    # Warmup
    _ = model.encode("Warmup query string")
    
    # Measure single query latency over 20 runs
    latencies = []
    for _ in range(20):
        t_start = time.perf_counter()
        _ = model.encode("How does the calendar navigate between months?")
        latencies.append((time.perf_counter() - t_start) * 1000.0)
    avg_latency_ms = sum(latencies) / len(latencies)
    
    # Measure batch throughput (50 documents)
    docs = [
        f"Document {i}: function handleNavigation() {{ navigateMonth(direction); }} with calendar state store"
        for i in range(50)
    ]
    t_batch_start = time.perf_counter()
    doc_embeddings = model.encode(docs, batch_size=16)
    batch_time_sec = time.perf_counter() - t_batch_start
    throughput = len(docs) / batch_time_sec
    
    # Benchmark retrieval dataset
    test_corpus = [
        {"id": "useCalendar", "text": "useCalendar hook managing currentMonth, selectedDate, and month navigation logic"},
        {"id": "CalendarMonthView", "text": "CalendarMonthView component rendering calendar grid days and weeks"},
        {"id": "NotesStorage", "text": "NotesStorage class persisting user notes to local SQLite database and disk"},
        {"id": "InvertedIndex", "text": "InvertedIndex class indexing terms and posting lists for full-text search"},
        {"id": "CodeTokenizer", "text": "CodeTokenizer class extracting subwords, camelCase terms, and syntax symbols"},
        {"id": "AuthService", "text": "AuthService class handling user login, credential verification, and sessions"},
        {"id": "OAuthClient", "text": "OAuthClient class configuring OAuth2 providers, tokens, and scopes"},
        {"id": "PaymentService", "text": "PaymentService handling credit card billing and checkout transactions"},
        {"id": "AppRouter", "text": "AppRouter routing HTTP requests to controllers and middleware"},
        {"id": "RepositoryScanner", "text": "RepositoryScanner scanning source code directories for C++ and TS files"}
    ]
    
    corpus_texts = [d["text"] for d in test_corpus]
    corpus_ids = [d["id"] for d in test_corpus]
    corpus_embs = model.encode(corpus_texts)
    
    benchmark_queries = [
        {"query": "How does the calendar navigate between months?", "relevant": ["useCalendar", "CalendarMonthView"]},
        {"query": "Where are notes persisted?", "relevant": ["NotesStorage"]},
        {"query": "Where does the calendar store state?", "relevant": ["useCalendar"]},
        {"query": "Where is token extraction performed?", "relevant": ["CodeTokenizer"]},
        {"query": "Where is the inverted index implemented?", "relevant": ["InvertedIndex"]},
        {"query": "Where is authentication handled?", "relevant": ["AuthService"]},
        {"query": "Where is OAuth configured?", "relevant": ["OAuthClient"]}
    ]
    
    p1_list, p5_list, mrr_list, ndcg_list, rec_list = [], [], [], [], []
    vocab_gap_results = []
    
    for q_item in benchmark_queries:
        q_emb = model.encode(q_item["query"])
        scores = []
        for d_id, d_emb in zip(corpus_ids, corpus_embs):
            sim = cosine_sim(q_emb, d_emb)
            scores.append((d_id, sim))
        scores.sort(key=lambda x: x[1], reverse=True)
        rankings = [x[0] for x in scores]
        
        p1, p5, mrr, ndcg, rec = compute_metrics(rankings, q_item["relevant"])
        p1_list.append(p1)
        p5_list.append(p5)
        mrr_list.append(mrr)
        ndcg_list.append(ndcg)
        rec_list.append(rec)
        
        top_id, top_sim = scores[0]
        vocab_gap_results.append({
            "query": q_item["query"],
            "expected": q_item["relevant"],
            "top_result": top_id,
            "top_similarity": float(top_sim),
            "rank": int(rankings.index(q_item["relevant"][0]) + 1) if q_item["relevant"][0] in rankings else -1,
            "is_relevant": top_id in q_item["relevant"],
            "all_scores": {k: float(round(float(v), 4)) for k, v in scores}
        })
        
    dim = int(len(corpus_embs[0]))
    
    return {
        "model_name": model_name,
        "dimension": dim,
        "load_time_sec": float(load_time_sec),
        "single_query_latency_ms": float(avg_latency_ms),
        "batch_throughput_docs_sec": float(throughput),
        "metrics": {
            "P@1": float(sum(p1_list) / len(p1_list)),
            "P@5": float(sum(p5_list) / len(p5_list)),
            "MRR": float(sum(mrr_list) / len(mrr_list)),
            "NDCG@5": float(sum(ndcg_list) / len(ndcg_list)),
            "Recall@10": float(sum(rec_list) / len(rec_list))
        },
        "vocab_gap_results": vocab_gap_results
    }

if __name__ == "__main__":
    print("Evaluating real MiniLM vs real Qwen3-Embedding-0.6B...")
    minilm_res = evaluate_model("sentence-transformers/all-MiniLM-L6-v2")
    qwen_res = evaluate_model("Qwen/Qwen3-Embedding-0.6B")
    
    out = {
        "minilm": minilm_res,
        "qwen": qwen_res
    }
    with open("benchmarks/real_qwen_vs_minilm_evaluation.json", "w") as f:
        json.dump(out, f, indent=2)
    print("Evaluation complete. Results saved to benchmarks/real_qwen_vs_minilm_evaluation.json")
