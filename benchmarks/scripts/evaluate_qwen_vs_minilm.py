"""
Amoeba — Phase 6.x.1: General Embedding Model Evaluation
MiniLM-L6-v2 vs Qwen3-Embedding-0.6B Evaluation Script
"""

import json
import time
import math
import numpy as np
from pathlib import Path
from sentence_transformers import SentenceTransformer

def dcg_at_k(r, k=5):
    r = np.asarray(r, dtype=float)[:k]
    if r.size:
        return np.sum(r / np.log2(np.arange(2, r.size + 2)))
    return 0.0

def ndcg_at_k(r, k=5):
    dcg_max = dcg_at_k(sorted(r, reverse=True), k)
    if not dcg_max:
        return 0.0
    return dcg_at_k(r, k) / dcg_max

def evaluate_retrieval(corpus, queries, model, top_k=10):
    # corpus: list of dicts with 'id', 'text', 'name'
    # queries: list of dicts with 'query', 'relevant_ids' (set of ids) or 'relevant_names'
    corpus_texts = [doc['text'] for doc in corpus]
    doc_embs = model.encode(corpus_texts, batch_size=16, normalize_embeddings=True)
    
    p_at_1_list = []
    p_at_5_list = []
    mrr_list = []
    ndcg_5_list = []
    recall_10_list = []
    
    results_detail = []
    
    for q_item in queries:
        q_text = q_item['query']
        rel_set = set(q_item.get('relevant_ids', []))
        rel_names = set(q_item.get('relevant_names', []))
        
        q_emb = model.encode(q_text, normalize_embeddings=True)
        scores = np.dot(doc_embs, q_emb)
        ranked_indices = np.argsort(scores)[::-1][:top_k]
        
        relevance_vector = []
        ranked_items = []
        found_rel_count = 0
        mrr = 0.0
        
        for rank, idx in enumerate(ranked_indices):
            doc = corpus[idx]
            is_rel = (doc['id'] in rel_set) or (doc.get('name') in rel_names)
            relevance_vector.append(1.0 if is_rel else 0.0)
            ranked_items.append({
                'rank': rank + 1,
                'name': doc.get('name', doc['id']),
                'score': float(scores[idx]),
                'is_relevant': is_rel
            })
            if is_rel and mrr == 0.0:
                mrr = 1.0 / (rank + 1)
            if is_rel:
                found_rel_count += 1
                
        p_at_1 = relevance_vector[0] if len(relevance_vector) > 0 else 0.0
        p_at_5 = np.mean(relevance_vector[:5]) if len(relevance_vector) >= 5 else np.mean(relevance_vector)
        ndcg_5 = ndcg_at_k(relevance_vector, k=5)
        
        total_rel = max(1, len(rel_set) if rel_set else len(rel_names))
        recall_10 = min(1.0, found_rel_count / total_rel) if total_rel > 0 else 0.0
        
        p_at_1_list.append(p_at_1)
        p_at_5_list.append(p_at_5)
        mrr_list.append(mrr)
        ndcg_5_list.append(ndcg_5)
        recall_10_list.append(recall_10)
        
        results_detail.append({
            'query': q_text,
            'p_at_1': p_at_1,
            'mrr': mrr,
            'ndcg_5': ndcg_5,
            'top_result': ranked_items[0] if ranked_items else None,
            'ranked_items': ranked_items
        })
        
    metrics = {
        'p_at_1': float(np.mean(p_at_1_list)),
        'p_at_5': float(np.mean(p_at_5_list)),
        'mrr': float(np.mean(mrr_list)),
        'ndcg_5': float(np.mean(ndcg_5_list)),
        'recall_10': float(np.mean(recall_10_list)),
        'details': results_detail
    }
    return metrics

def run_all_evaluations():
    print("Loading models...")
    model_minilm = SentenceTransformer('all-MiniLM-L6-v2')
    model_qwen = SentenceTransformer('Qwen/Qwen3-Embedding-0.6B')
    
    # 1. Phase 6.1 Evaluation Benchmark Corpus
    p61_corpus = [
        {"id": "cpp_auth_validate", "name": "validateCredentials", "text": "Language: C++\nFile: src/auth/auth_service.cpp\nKind: Method\nContext: AuthService\nName: validateCredentials\nDetail: validates credentials and creates session token\nCode: bool validateCredentials(const Credentials& creds) { return verify(creds); }"},
        {"id": "ts_auth_authenticate", "name": "authenticateUser", "text": "Language: TypeScript\nFile: src/auth/authController.ts\nKind: Method\nContext: AuthController\nName: authenticateUser\nDetail: authenticates user with password and returns jwt\nCode: async authenticateUser(req: Request, res: Response) { const token = await this.service.login(req.body); }"},
        {"id": "py_token_refresh", "name": "refresh_session_token", "text": "Language: Python\nFile: services/token_service.py\nKind: Function\nContext: token_service\nName: refresh_session_token\nDetail: refreshes expired jwt session tokens for active users\nCode: def refresh_session_token(token: str) -> str: return jwt.refresh(token)"},
        {"id": "go_payment_process", "name": "ProcessTransaction", "text": "Language: Go\nFile: pkg/payment/processor.go\nKind: Method\nContext: PaymentProcessor\nName: ProcessTransaction\nDetail: processes credit card transactions and generates invoice records\nCode: func (p *PaymentProcessor) ProcessTransaction(tx Transaction) (*Invoice, error) { return p.gateway.Charge(tx) }"},
        {"id": "tsx_user_profile", "name": "UserProfileCard", "text": "Language: TSX\nFile: components/UserProfile.tsx\nKind: Component\nContext: UserProfile\nName: UserProfileCard\nDetail: renders user profile card with avatar and status badge\nCode: export const UserProfileCard = ({ user }: Props) => <div className=\"profile-card\"><Avatar user={user}/></div>;"},
        {"id": "cpp_db_repo", "name": "UserRepository", "text": "Language: C++\nFile: src/db/user_repository.cpp\nKind: Class\nContext: db\nName: UserRepository\nDetail: database repository for querying and persisting user records\nCode: class UserRepository { public: User findById(int64_t id); void save(const User& u); };"},
        {"id": "py_hash_password", "name": "hash_password", "text": "Language: Python\nFile: utils/crypto.py\nKind: Function\nContext: crypto\nName: hash_password\nDetail: hashes plain text passwords using bcrypt with salt\nCode: def hash_password(password: str) -> str: return bcrypt.hashpw(password.encode(), bcrypt.gensalt())"}
    ]
    
    p61_queries = [
        {"query": "authenticateUser", "relevant_ids": ["ts_auth_authenticate"]},
        {"query": "find user by id", "relevant_ids": ["cpp_db_repo"]},
        {"query": "where do we check user login credentials?", "relevant_ids": ["cpp_auth_validate", "ts_auth_authenticate"]},
        {"query": "how to verify user identity and session", "relevant_ids": ["ts_auth_authenticate", "py_token_refresh", "cpp_auth_validate"]},
        {"query": "process payment transactions and charge invoices", "relevant_ids": ["go_payment_process"]},
        {"query": "component that renders user profile", "relevant_ids": ["tsx_user_profile"]},
        {"query": "http middleware for checking auth requests", "relevant_ids": ["cpp_auth_validate", "ts_auth_authenticate"]},
        {"query": "save user record to database", "relevant_ids": ["cpp_db_repo"]},
        {"query": "hash plain text password using bcrypt", "relevant_ids": ["py_hash_password"]},
        {"query": "refresh expired token for user", "relevant_ids": ["py_token_refresh"]}
    ]
    
    # 2. Vocabulary Gap & Quality Exploration Cases
    vocab_gap_corpus = [
        {"id": "cal_grid", "name": "CalendarGrid", "text": "CalendarGrid renders monthly 7-column grid layout for dates in month with week header"},
        {"id": "cal_day", "name": "CalendarDay", "text": "CalendarDay renders individual day cell with long press handlers and date intervals"},
        {"id": "on_prev_month", "name": "onPreviousMonth", "text": "onPreviousMonth navigates calendar to the previous month and updates currentDate state"},
        {"id": "on_next_month", "name": "onNextMonth", "text": "onNextMonth navigates calendar to next month and updates currentDate state"},
        {"id": "use_cal", "name": "useCalendar", "text": "useCalendar React hook for calendar navigation and month state management"},
        {"id": "notes_sec", "name": "NotesSection", "text": "NotesSection renders notes panel and handles note editing for calendar days"},
        {"id": "use_local_storage", "name": "useLocalStorage", "text": "useLocalStorage custom hook for persisting saved notes and preferences to localStorage"},
        {"id": "format_date", "name": "formatDate", "text": "formatDate formats Date objects to display strings using MONTH_NAMES and DAYS_OF_WEEK"},
        {"id": "tok_tokenize", "name": "CodeTokenizer::tokenize", "text": "CodeTokenizer tokenize extracts code tokens, identifiers, and subwords from source files"},
        {"id": "hash_token_seed", "name": "hash_token_seed", "text": "hash_token_seed computes fnv-1a hash seed for deterministic pseudo-random projection"},
        {"id": "inv_index", "name": "InvertedIndex", "text": "InvertedIndex maintains postings lists and term dictionary for inverted index lexical lookup"},
        {"id": "rel_graph", "name": "RelationshipGraph", "text": "RelationshipGraph manages AST relationship edges, calls, and inheritance hierarchies"},
        {"id": "float_tb", "name": "FloatingToolbar", "text": "FloatingToolbar floating quick-action toolbar button for calendar navigation"},
        {"id": "handle_action", "name": "handleAction", "text": "handleAction handles click event actions on the floating toolbar"}
    ]
    
    vocab_queries = [
        {"query": "How does the calendar navigate between months?", "expected": ["onPreviousMonth", "onNextMonth", "useCalendar"]},
        {"query": "Where are notes persisted?", "expected": ["useLocalStorage", "NotesSection"]},
        {"query": "Where does the calendar store state?", "expected": ["useCalendar", "useLocalStorage", "NotesSection"]},
        {"query": "Where is token extraction performed?", "expected": ["CodeTokenizer::tokenize"], "spurious": ["hash_token_seed"]},
        {"query": "Where is the inverted index implemented?", "expected": ["InvertedIndex"]},
        {"query": "Where is authentication handled?", "expected": []},
        {"query": "Where is OAuth configured?", "expected": []}
    ]
    
    print("Evaluating Phase 6.1 Benchmark with MiniLM...")
    m_p61 = evaluate_retrieval(p61_corpus, p61_queries, model_minilm)
    
    print("Evaluating Phase 6.1 Benchmark with Qwen3-0.6B...")
    q_p61 = evaluate_retrieval(p61_corpus, p61_queries, model_qwen)
    
    # Vocabulary Gap Detailed Ranking
    print("\n--- Vocabulary Gap & Quality Cases ---")
    vocab_results = []
    
    for vq in vocab_queries:
        q_text = vq['query']
        exp_names = vq['expected']
        spur_names = vq.get('spurious', [])
        
        # MiniLM
        q_emb_m = model_minilm.encode(q_text, normalize_embeddings=True)
        doc_embs_m = model_minilm.encode([d['text'] for d in vocab_gap_corpus], normalize_embeddings=True)
        scores_m = np.dot(doc_embs_m, q_emb_m)
        rank_m = np.argsort(scores_m)[::-1]
        
        # Qwen
        q_emb_q = model_qwen.encode(q_text, normalize_embeddings=True)
        doc_embs_q = model_qwen.encode([d['text'] for d in vocab_gap_corpus], normalize_embeddings=True)
        scores_q = np.dot(doc_embs_q, q_emb_q)
        rank_q = np.argsort(scores_q)[::-1]
        
        top_m = vocab_gap_corpus[rank_m[0]]
        top_q = vocab_gap_corpus[rank_q[0]]
        
        # Find expected ranks
        exp_rank_m = [(vocab_gap_corpus[r]['name'], int(r+1), float(scores_m[r])) for r in rank_m if vocab_gap_corpus[r]['name'] in exp_names]
        exp_rank_q = [(vocab_gap_corpus[r]['name'], int(r+1), float(scores_q[r])) for r in rank_q if vocab_gap_corpus[r]['name'] in exp_names]
        
        spur_rank_m = [(vocab_gap_corpus[r]['name'], int(r+1), float(scores_m[r])) for r in rank_m if vocab_gap_corpus[r]['name'] in spur_names]
        spur_rank_q = [(vocab_gap_corpus[r]['name'], int(r+1), float(scores_q[r])) for r in rank_q if vocab_gap_corpus[r]['name'] in spur_names]
        
        vocab_results.append({
            'query': q_text,
            'expected': exp_names,
            'minilm_top': {'name': top_m['name'], 'score': float(scores_m[rank_m[0]]), 'is_expected': top_m['name'] in exp_names},
            'qwen_top': {'name': top_q['name'], 'score': float(scores_q[rank_q[0]]), 'is_expected': top_q['name'] in exp_names},
            'minilm_expected_ranks': exp_rank_m,
            'qwen_expected_ranks': exp_rank_q,
            'minilm_spurious_ranks': spur_rank_m,
            'qwen_spurious_ranks': spur_rank_q,
        })
        
    out = {
        'p61_metrics_minilm': m_p61,
        'p61_metrics_qwen': q_p61,
        'vocab_gap_cases': vocab_results
    }
    
    with open('benchmarks/phase-06.x.1_eval_results.json', 'w') as f:
        json.dump(out, f, indent=2)
        
    print("\nBenchmark Evaluation Complete. Results written to benchmarks/phase-06.x.1_eval_results.json")
    print("\nSummary Table:")
    print(f"Phase 6.1 Corpus (10 Queries, 7 Elements):")
    print(f"P@1:    MiniLM={m_p61['p_at_1']:.4f}, Qwen3={q_p61['p_at_1']:.4f}")
    print(f"P@5:    MiniLM={m_p61['p_at_5']:.4f}, Qwen3={q_p61['p_at_5']:.4f}")
    print(f"MRR:    MiniLM={m_p61['mrr']:.4f}, Qwen3={q_p61['mrr']:.4f}")
    print(f"NDCG@5: MiniLM={m_p61['ndcg_5']:.4f}, Qwen3={q_p61['ndcg_5']:.4f}")
    print(f"R@10:   MiniLM={m_p61['recall_10']:.4f}, Qwen3={q_p61['recall_10']:.4f}")

if __name__ == '__main__':
    run_all_evaluations()
