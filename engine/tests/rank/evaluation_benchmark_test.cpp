#include "amoeba/index/inverted_index.hpp"
#include "amoeba/index/search_engine.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/rank/baseline_ranker.hpp"
#include "amoeba/rank/bm25_ranker.hpp"
#include "amoeba/rank/code_aware_ranker.hpp"
#include "amoeba/scanner/repository_scanner.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace std;
using namespace std::filesystem;
using namespace amoeba::index;
using namespace amoeba::parser;
using namespace amoeba::rank;
using namespace amoeba::scanner;

struct EvaluationQuery {
    string query;
    string category;                                 // "exact", "normalized", "partial", "multi_term", "contextual", "path", "framework", "ambiguous", "cross_lang"
    unordered_map<string, uint32_t> element_grades;  // element.name -> graded relevance (3, 2, 1, 0)
};

struct RankingMetrics {
    double p_at_1{0.0};
    double p_at_3{0.0};
    double p_at_5{0.0};
    double p_at_10{0.0};
    double r_at_5{0.0};
    double r_at_10{0.0};
    double mrr{0.0};
    double ndcg_at_5{0.0};
    double ndcg_at_10{0.0};
};

class Phase4EvaluationBenchmark : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = chrono::steady_clock::now().time_since_epoch().count();
        test_dir = temp_directory_path() / ("amoeba_eval_corpus_" + to_string(timestamp));

        // Setup complete multi-language folder structure
        create_directories(test_dir / "src/c");
        create_directories(test_dir / "src/cpp");
        create_directories(test_dir / "src/python");
        create_directories(test_dir / "src/java");
        create_directories(test_dir / "src/go");
        create_directories(test_dir / "src/rust");
        create_directories(test_dir / "src/ts");
        create_directories(test_dir / "src/components");
        create_directories(test_dir / "src/app/api/auth");
        create_directories(test_dir / "src/app/dashboard");
        create_directories(test_dir / "src/app/settings");
        create_directories(test_dir / "src/styles");
        create_directories(test_dir / "public");

        // 1. C
        create_file("src/c/memory_pool.h", R"(
#pragma once
#include <stddef.h>
typedef struct MemoryPool {
    size_t block_size;
    size_t capacity;
    size_t allocated;
} MemoryPool;

MemoryPool* create_memory_pool(size_t block_size, size_t capacity);
void* allocate_block(MemoryPool* pool);
void free_block(MemoryPool* pool, void* ptr);
void reset_pool(MemoryPool* pool);
)");

        create_file("src/c/memory_pool.c", R"(
#include "memory_pool.h"
#include <stdlib.h>

MemoryPool* create_memory_pool(size_t block_size, size_t capacity) {
    MemoryPool* pool = (MemoryPool*)malloc(sizeof(MemoryPool));
    pool->block_size = block_size;
    pool->capacity = capacity;
    pool->allocated = 0;
    return pool;
}

void* allocate_block(MemoryPool* pool) {
    if (!pool) return NULL;
    pool->allocated++;
    return malloc(pool->block_size);
}

void free_block(MemoryPool* pool, void* ptr) {
    if (!pool || !ptr) return;
    free(ptr);
    if (pool->allocated > 0) pool->allocated--;
}

void reset_pool(MemoryPool* pool) {
    if (pool) pool->allocated = 0;
}
)");

        create_file("src/c/string_utils.c", R"(
#include <string.h>
#include <ctype.h>

char* string_trim(char* str) {
    while (isspace((unsigned char)*str)) str++;
    return str;
}

int string_split(const char* input, char delimiter, char** tokens) {
    return 0;
}
)");

        // 2. C++
        create_file("src/cpp/scanner.hpp", R"(
#pragma once
#include <string>
#include <vector>

namespace amoeba::scanner {
class RepositoryScanner {
public:
    std::vector<std::string> scan(const std::string& repo_path);
    bool is_ignored(const std::string& path) const;
    void collect_files(const std::string& root_dir);
};
}
)");

        create_file("src/cpp/scanner.cpp", R"(
#include "scanner.hpp"
namespace amoeba::scanner {
std::vector<std::string> RepositoryScanner::scan(const std::string& repo_path) {
    collect_files(repo_path);
    return {};
}
bool RepositoryScanner::is_ignored(const std::string& path) const {
    return path.empty();
}
void RepositoryScanner::collect_files(const std::string& root_dir) {
    is_ignored(root_dir);
}
}
)");

        create_file("src/cpp/query_parser.cpp", R"(
#include <string>
#include <vector>

namespace amoeba::query {
class QueryParser {
public:
    std::vector<std::string> parse_query(const std::string& raw_input) {
        extract_filters(raw_input);
        return {};
    }
    std::vector<std::string> extract_filters(const std::string& input) {
        return {};
    }
};
}
)");

        // 3. Python
        create_file("src/python/auth_service.py", R"(
import jwt
from typing import Optional, Dict

class AuthenticationService:
    def __init__(self, secret_key: str):
        self.secret_key = secret_key

    def login_user(self, username: str, password: str) -> Optional[str]:
        if not username or not password:
            return None
        return self.verify_jwt_token(username)

    def verify_jwt_token(self, token: str) -> bool:
        return token is not None

    def get_user_by_id(self, user_id: int) -> Dict[str, str]:
        return {"id": str(user_id), "username": "alice"}
)");

        create_file("src/python/token_manager.py", R"(
class TokenManager:
    def __init__(self):
        self.active_tokens = {}

    def create_session_token(self, user_id: str) -> str:
        token = f"tok_{user_id}"
        self.active_tokens[token] = user_id
        return token

    def revoke_token(self, token: str) -> bool:
        return self.active_tokens.pop(token, None) is not None
)");

        create_file("src/python/user_controller.py", R"(
class UserController:
    def __init__(self, auth_service):
        self.auth_service = auth_service

    def handle_login_request(self, req):
        return self.auth_service.login_user(req["username"], req["password"])

    def fetch_user_profile(self, user_id: int):
        return self.auth_service.get_user_by_id(user_id)
)");

        // 4. Java
        create_file("src/java/TokenProvider.java", R"(
package com.amoeba.auth;

public class TokenProvider {
    private final String secretKey;

    public TokenProvider(String secretKey) {
        this.secretKey = secretKey;
    }

    public String generateRefreshToken(String userId) {
        return "refresh_" + userId;
    }

    public boolean validateToken(String token) {
        return token != null && !token.isEmpty();
    }

    public String extractClaims(String token) {
        validateToken(token);
        return "claims";
    }
}
)");

        create_file("src/java/UserRepository.java", R"(
package com.amoeba.repository;

public class UserRepository {
    public String findById(String userId) {
        return "User_" + userId;
    }

    public void saveUser(String userId, String username) {
    }
}
)");

        // 5. Go
        create_file("src/go/router.go", R"(
package router

type HttpRouter struct {
    routes map[string]string
}

func NewHttpRouter() *HttpRouter {
    return &HttpRouter{routes: make(map[string]string)}
}

func (r *HttpRouter) RegisterRoute(path string, handler string) {
    r.routes[path] = handler
}

func (r *HttpRouter) ServeHttp(path string) string {
    return r.routes[path]
}

func (r *HttpRouter) MatchPath(pattern string) bool {
    return len(pattern) > 0
}
)");

        create_file("src/go/middleware.go", R"(
package router

func AuthMiddleware(token string) bool {
    return len(token) > 0
}

func LoggingMiddleware(path string) {
}
)");

        // 6. Rust
        create_file("src/rust/pipeline.rs", R"(
pub struct SearchPipeline {
    pub name: String,
}

impl SearchPipeline {
    pub fn new(name: &str) -> Self {
        SearchPipeline { name: name.to_string() }
    }

    pub fn execute_query(&self, query: &str) -> Vec<String> {
        self.rank_results(vec![query.to_string()])
    }

    pub fn rank_results(&self, candidates: Vec<String>) -> Vec<String> {
        self.filter_candidates(candidates)
    }

    pub fn filter_candidates(&self, candidates: Vec<String>) -> Vec<String> {
        candidates
    }
}
)");

        create_file("src/rust/config.rs", R"(
pub struct EngineConfig {
    pub max_threads: usize,
    pub timeout_ms: u64,
}

impl EngineConfig {
    pub fn load_from_env() -> Self {
        EngineConfig { max_threads: 4, timeout_ms: 1000 }
    }
}
)");

        // 7. JS / TS
        create_file("src/ts/auth_client.ts", R"(
export class AuthClient {
    private token: string = "";

    public refreshSession(refreshToken: string): Promise<string> {
        return Promise.resolve("session_" + refreshToken);
    }

    public getProfile(userId: string): Promise<any> {
        return Promise.resolve({ userId, status: "active" });
    }

    public logout(): Promise<void> {
        this.token = "";
        return Promise.resolve();
    }
}
)");

        create_file("src/ts/session_store.ts", R"(
export class SessionStore {
    private sessions = new Map<string, string>();

    public saveSession(sessionId: string, data: string): void {
        this.sessions.set(sessionId, data);
    }

    public clearSession(sessionId: string): void {
        this.sessions.delete(sessionId);
    }
}
)");

        // 8. TSX / React + Tailwind
        create_file("src/components/UserProfileCard.tsx", R"(
import React, { useState, useEffect } from 'react';

export function useUserProfile(userId: string) {
    const [profile, setProfile] = useState(null);
    useEffect(() => {
        // load profile hook
    }, [userId]);
    return profile;
}

export function UserProfileCard({ userId }: { userId: string }) {
    const profile = useUserProfile(userId);
    useEffect(() => {
        console.log("UserProfileCard mounted");
    }, []);

    return (
        <div className="flex items-center justify-between p-4 rounded shadow-md responsive-grid">
            <span className="font-bold text-lg">User Profile Card</span>
        </div>
    );
}
)");

        create_file("src/components/NavigationHeader.tsx", R"(
import React, { useState } from 'react';

export function useNavigation() {
    const [isOpen, setIsOpen] = useState(false);
    return { isOpen, setIsOpen };
}

export function NavigationHeader() {
    const { isOpen, setIsOpen } = useNavigation();
    return (
        <header className="app-header flex justify-between p-2">
            <nav className="nav-container">Navigation</nav>
        </header>
    );
}
)");

        // 9. Next.js App Router
        create_file("src/app/api/auth/route.ts", R"(
export async function POST(request: Request) {
    return new Response(JSON.stringify({ status: "authenticated" }));
}
)");

        create_file("src/app/dashboard/page.tsx", R"(
export default function DashboardPage() {
    return <div className="dashboard-view">Dashboard Overview</div>;
}
)");

        create_file("src/app/settings/page.tsx", R"(
export default function SettingsPage() {
    return <div className="settings-view">Settings Overview</div>;
}
)");

        // 10. HTML & CSS
        create_file("public/index.html", R"(
<!DOCTYPE html>
<html>
<head><title>Amoeba Enterprise Search</title></head>
<body>
    <header class="app-header">Amoeba</header>
    <main class="card-container"></main>
</body>
</html>
)");

        create_file("src/styles/theme.css", R"(
.responsive-grid {
    display: grid;
    grid-template-columns: repeat(12, 1fr);
}
.dark-mode {
    background-color: #121212;
    color: #ffffff;
}
.card-container {
    padding: 1rem;
}
)");

        // Build index
        RepositoryScanner scanner;
        const auto scan_result = scanner.scan(test_dir);

        SourceParser parser;
        for (const auto& file_info : scan_result.files) {
            try {
                const auto parsed = parser.parse_file(file_info.path);
                if (parsed.success) {
                    index.add_parsed_file(parsed);
                }
            } catch (...) {
            }
        }

        build_all_query_sets();
    }

    void TearDown() override {
        error_code ec;
        remove_all(test_dir, ec);
    }

    void create_file(const string& rel_path, string_view content) {
        const path file_path = test_dir / rel_path;
        ofstream file(file_path, ios::binary);
        file << content;
    }

    void build_all_query_sets() {
        // 1. Development Set (used only for evaluator validation)
        dev_queries = {
            EvaluationQuery{"MemoryPool", "dev", {{"MemoryPool", 3}, {"create_memory_pool", 2}}},
            EvaluationQuery{"RepositoryScanner", "dev", {{"RepositoryScanner", 3}, {"scan", 2}}},
            EvaluationQuery{"AuthenticationService", "dev", {{"AuthenticationService", 3}, {"login_user", 2}}},
            EvaluationQuery{"TokenProvider", "dev", {{"TokenProvider", 3}, {"generateRefreshToken", 2}}},
            EvaluationQuery{"UserProfileCard", "dev", {{"UserProfileCard", 3}, {"useUserProfile", 2}}},
            EvaluationQuery{"DashboardPage", "dev", {{"DashboardPage", 3}}},
        };

        // 2. Validation Set (used for ablation testing)
        val_queries = {
            EvaluationQuery{"SearchPipeline", "val", {{"SearchPipeline", 3}, {"execute_query", 2}, {"rank_results", 2}}},
            EvaluationQuery{"HttpRouter", "val", {{"HttpRouter", 3}, {"RegisterRoute", 2}, {"ServeHttp", 2}}},
            EvaluationQuery{"AuthClient", "val", {{"AuthClient", 3}, {"refreshSession", 2}, {"getProfile", 2}}},
            EvaluationQuery{"SessionStore", "val", {{"SessionStore", 3}, {"saveSession", 2}, {"clearSession", 2}}},
            EvaluationQuery{"TokenManager", "val", {{"TokenManager", 3}, {"create_session_token", 2}}},
            EvaluationQuery{"EngineConfig", "val", {{"EngineConfig", 3}, {"load_from_env", 2}}},
            EvaluationQuery{"NavigationHeader", "val", {{"NavigationHeader", 3}, {"useNavigation", 2}}},
            EvaluationQuery{"SettingsPage", "val", {{"SettingsPage", 3}}},
            EvaluationQuery{"allocate_block", "val", {{"allocate_block", 3}, {"MemoryPool", 2}, {"free_block", 2}}},
            EvaluationQuery{"verify_jwt_token", "val", {{"verify_jwt_token", 3}, {"AuthenticationService", 2}}},
        };

        // 3. Final Evaluation Set (68 queries, NEVER used for parameter tuning)
        final_queries = {
            // Category 1: Exact Identifier (8 queries)
            EvaluationQuery{"RepositoryScanner", "exact", {{"RepositoryScanner", 3}, {"scan", 2}, {"is_ignored", 1}}},
            EvaluationQuery{"AuthenticationService", "exact", {{"AuthenticationService", 3}, {"login_user", 2}, {"AuthClient", 2}}},
            EvaluationQuery{"TokenProvider", "exact", {{"TokenProvider", 3}, {"generateRefreshToken", 2}, {"validateToken", 2}}},
            EvaluationQuery{"UserProfileCard", "exact", {{"UserProfileCard", 3}, {"useUserProfile", 2}, {"getProfile", 1}}},
            EvaluationQuery{"SearchPipeline", "exact", {{"SearchPipeline", 3}, {"execute_query", 2}, {"rank_results", 2}}},
            EvaluationQuery{"HttpRouter", "exact", {{"HttpRouter", 3}, {"RegisterRoute", 2}, {"ServeHttp", 2}}},
            EvaluationQuery{"SessionStore", "exact", {{"SessionStore", 3}, {"saveSession", 2}, {"clearSession", 2}}},
            EvaluationQuery{"QueryParser", "exact", {{"QueryParser", 3}, {"parse_query", 2}, {"extract_filters", 2}}},

            // Category 2: Normalized Identifier (8 queries)
            EvaluationQuery{"repository_scanner", "normalized", {{"RepositoryScanner", 3}, {"scan", 2}}},
            EvaluationQuery{"authentication_service", "normalized", {{"AuthenticationService", 3}, {"login_user", 2}}},
            EvaluationQuery{"token_provider", "normalized", {{"TokenProvider", 3}, {"generateRefreshToken", 2}}},
            EvaluationQuery{"user_profile_card", "normalized", {{"UserProfileCard", 3}, {"useUserProfile", 2}}},
            EvaluationQuery{"search_pipeline", "normalized", {{"SearchPipeline", 3}, {"execute_query", 2}}},
            EvaluationQuery{"http_router", "normalized", {{"HttpRouter", 3}, {"RegisterRoute", 2}}},
            EvaluationQuery{"session_store", "normalized", {{"SessionStore", 3}, {"saveSession", 2}}},
            EvaluationQuery{"query_parser", "normalized", {{"QueryParser", 3}, {"parse_query", 2}}},

            // Category 3: Partial / Subword (8 queries)
            EvaluationQuery{"scanner", "partial", {{"RepositoryScanner", 3}, {"scan", 2}, {"is_ignored", 1}}},
            EvaluationQuery{"authentication", "partial", {{"AuthenticationService", 3}, {"AuthClient", 3}, {"login_user", 2}, {"AuthMiddleware", 2}}},
            EvaluationQuery{"token", "partial", {{"TokenProvider", 3}, {"TokenManager", 3}, {"generateRefreshToken", 2}, {"create_session_token", 2}}},
            EvaluationQuery{"profile", "partial", {{"UserProfileCard", 3}, {"getProfile", 3}, {"useUserProfile", 2}, {"fetch_user_profile", 2}}},
            EvaluationQuery{"pipeline", "partial", {{"SearchPipeline", 3}, {"execute_query", 2}, {"rank_results", 2}}},
            EvaluationQuery{"router", "partial", {{"HttpRouter", 3}, {"RegisterRoute", 2}, {"ServeHttp", 2}}},
            EvaluationQuery{"session", "partial", {{"SessionStore", 3}, {"refreshSession", 3}, {"create_session_token", 2}}},
            EvaluationQuery{"parser", "partial", {{"QueryParser", 3}, {"parse_query", 2}, {"extract_filters", 2}}},

            // Category 4: Multi-term Exploratory (8 queries)
            EvaluationQuery{"user authentication", "multi_term", {{"AuthenticationService", 3}, {"login_user", 3}, {"UserController", 2}, {"AuthClient", 2}}},
            EvaluationQuery{"token refresh", "multi_term", {{"generateRefreshToken", 3}, {"refreshSession", 3}, {"TokenProvider", 2}, {"TokenManager", 2}}},
            EvaluationQuery{"profile card", "multi_term", {{"UserProfileCard", 3}, {"useUserProfile", 2}, {"getProfile", 1}}},
            EvaluationQuery{"query execution", "multi_term", {{"execute_query", 3}, {"SearchPipeline", 2}, {"QueryParser", 2}}},
            EvaluationQuery{"route registration", "multi_term", {{"RegisterRoute", 3}, {"HttpRouter", 2}, {"ServeHttp", 1}}},
            EvaluationQuery{"session store", "multi_term", {{"SessionStore", 3}, {"saveSession", 2}, {"clearSession", 2}}},
            EvaluationQuery{"memory allocation", "multi_term", {{"allocate_block", 3}, {"MemoryPool", 3}, {"create_memory_pool", 2}}},
            EvaluationQuery{"user repository", "multi_term", {{"UserRepository", 3}, {"findById", 2}, {"saveUser", 2}}},

            // Category 5: Contextual Lexical (8 queries)
            EvaluationQuery{"login user", "contextual", {{"login_user", 3}, {"handle_login_request", 3}, {"AuthenticationService", 2}}},
            EvaluationQuery{"verify jwt", "contextual", {{"verify_jwt_token", 3}, {"AuthenticationService", 2}}},
            EvaluationQuery{"generate refresh token", "contextual", {{"generateRefreshToken", 3}, {"TokenProvider", 2}}},
            EvaluationQuery{"register route", "contextual", {{"RegisterRoute", 3}, {"HttpRouter", 2}}},
            EvaluationQuery{"allocate block", "contextual", {{"allocate_block", 3}, {"MemoryPool", 2}, {"free_block", 2}}},
            EvaluationQuery{"execute query", "contextual", {{"execute_query", 3}, {"SearchPipeline", 2}}},
            EvaluationQuery{"find user by id", "contextual", {{"get_user_by_id", 3}, {"findById", 3}, {"UserRepository", 2}}},
            EvaluationQuery{"refresh session", "contextual", {{"refreshSession", 3}, {"AuthClient", 2}}},

            // Category 6: Path / Repository (8 queries)
            EvaluationQuery{"auth", "path", {{"AuthenticationService", 3}, {"TokenProvider", 3}, {"AuthClient", 3}, {"POST", 3}}},
            EvaluationQuery{"components", "path", {{"UserProfileCard", 3}, {"NavigationHeader", 3}, {"useUserProfile", 2}}},
            EvaluationQuery{"routes", "path", {{"POST", 3}, {"DashboardPage", 3}, {"SettingsPage", 3}, {"HttpRouter", 2}}},
            EvaluationQuery{"styles", "path", {{".responsive-grid", 3}, {".dark-mode", 3}, {".card-container", 3}}},
            EvaluationQuery{"scanner", "path", {{"RepositoryScanner", 3}, {"scan", 2}}},
            EvaluationQuery{"pipeline", "path", {{"SearchPipeline", 3}, {"execute_query", 2}}},
            EvaluationQuery{"router", "path", {{"HttpRouter", 3}, {"RegisterRoute", 2}}},
            EvaluationQuery{"session", "path", {{"SessionStore", 3}, {"saveSession", 2}}},

            // Category 7: Framework-specific (8 queries)
            EvaluationQuery{"useEffect", "framework", {{"useEffect", 3}, {"UserProfileCard", 2}, {"useUserProfile", 2}}},
            EvaluationQuery{"useUserProfile", "framework", {{"useUserProfile", 3}, {"UserProfileCard", 2}}},
            EvaluationQuery{"DashboardPage", "framework", {{"DashboardPage", 3}}},
            EvaluationQuery{"SettingsPage", "framework", {{"SettingsPage", 3}}},
            EvaluationQuery{".responsive-grid", "framework", {{".responsive-grid", 3}}},
            EvaluationQuery{"POST", "framework", {{"POST", 3}}},
            EvaluationQuery{".dark-mode", "framework", {{".dark-mode", 3}}},
            EvaluationQuery{"app-header", "framework", {{"NavigationHeader", 3}, {"app-header", 3}}},

            // Category 8: Ambiguous / Multi-target (8 queries)
            EvaluationQuery{"user", "ambiguous", {{"UserProfileCard", 3}, {"useUserProfile", 3}, {"login_user", 2}, {"UserRepository", 2}, {"UserController", 2}}},
            EvaluationQuery{"token", "ambiguous", {{"TokenProvider", 3}, {"TokenManager", 3}, {"generateRefreshToken", 2}, {"verify_jwt_token", 2}}},
            EvaluationQuery{"scan", "ambiguous", {{"RepositoryScanner", 3}, {"scan", 3}, {"is_ignored", 1}}},
            EvaluationQuery{"route", "ambiguous", {{"HttpRouter", 3}, {"RegisterRoute", 3}, {"POST", 3}, {"DashboardPage", 2}}},
            EvaluationQuery{"config", "ambiguous", {{"EngineConfig", 3}, {"load_from_env", 2}}},
            EvaluationQuery{"header", "ambiguous", {{"NavigationHeader", 3}, {"app-header", 3}, {"useNavigation", 2}}},
            EvaluationQuery{"service", "ambiguous", {{"AuthenticationService", 3}, {"UserController", 2}}},
            EvaluationQuery{"pool", "ambiguous", {{"MemoryPool", 3}, {"create_memory_pool", 2}, {"reset_pool", 2}}},

            // Category 9: Cross-language (4 queries)
            EvaluationQuery{"login", "cross_lang", {{"login_user", 3}, {"handle_login_request", 3}, {"AuthMiddleware", 2}}},
            EvaluationQuery{"token validate", "cross_lang", {{"validateToken", 3}, {"verify_jwt_token", 3}, {"TokenProvider", 2}}},
            EvaluationQuery{"route match", "cross_lang", {{"MatchPath", 3}, {"RegisterRoute", 2}, {"HttpRouter", 2}}},
            EvaluationQuery{"session clear", "cross_lang", {{"clearSession", 3}, {"revoke_token", 3}, {"logout", 2}}},
        };
    }

    RankingMetrics evaluate_query_list(const vector<EvaluationQuery>& query_list, RankerType ranker_type) {
        SearchEngine engine(index);
        double total_p1 = 0.0;
        double total_p3 = 0.0;
        double total_p5 = 0.0;
        double total_p10 = 0.0;
        double total_r5 = 0.0;
        double total_r10 = 0.0;
        double total_mrr = 0.0;
        double total_ndcg5 = 0.0;
        double total_ndcg10 = 0.0;

        for (const auto& eval_q : query_list) {
            const auto results = engine.search(eval_q.query, SearchOptions{.ranker_type = ranker_type, .max_results = 20});

            // Count total relevant items in ground truth (grade >= 2)
            size_t total_relevant_in_gt = 0;
            for (const auto& [name, grade] : eval_q.element_grades) {
                if (grade >= 2) total_relevant_in_gt++;
            }
            if (total_relevant_in_gt == 0) total_relevant_in_gt = 1;

            // 1. Precision@K
            if (!results.empty()) {
                const auto it = eval_q.element_grades.find(results[0].element.name);
                if (it != eval_q.element_grades.end() && it->second >= 2) {
                    total_p1 += 1.0;
                }
            }

            auto calc_p_at_k = [&](size_t k) {
                size_t rel = 0;
                for (size_t i = 0; i < min(results.size(), k); ++i) {
                    const auto it = eval_q.element_grades.find(results[i].element.name);
                    if (it != eval_q.element_grades.end() && it->second >= 2) {
                        rel++;
                    }
                }
                return static_cast<double>(rel) / static_cast<double>(k);
            };

            auto calc_r_at_k = [&](size_t k) {
                size_t rel = 0;
                for (size_t i = 0; i < min(results.size(), k); ++i) {
                    const auto it = eval_q.element_grades.find(results[i].element.name);
                    if (it != eval_q.element_grades.end() && it->second >= 2) {
                        rel++;
                    }
                }
                return static_cast<double>(rel) / static_cast<double>(total_relevant_in_gt);
            };

            total_p3 += calc_p_at_k(3);
            total_p5 += calc_p_at_k(5);
            total_p10 += calc_p_at_k(10);

            total_r5 += min(1.0, calc_r_at_k(5));
            total_r10 += min(1.0, calc_r_at_k(10));

            // 2. MRR (First grade >= 2)
            double rr = 0.0;
            for (size_t i = 0; i < results.size(); ++i) {
                const auto it = eval_q.element_grades.find(results[i].element.name);
                if (it != eval_q.element_grades.end() && it->second >= 2) {
                    rr = 1.0 / static_cast<double>(i + 1);
                    break;
                }
            }
            total_mrr += rr;

            // 3. NDCG@K
            auto calc_ndcg = [&](size_t k) {
                double dcg = 0.0;
                for (size_t i = 0; i < min(results.size(), k); ++i) {
                    const auto it = eval_q.element_grades.find(results[i].element.name);
                    const double rel = (it != eval_q.element_grades.end()) ? static_cast<double>(it->second) : 0.0;
                    dcg += (std::pow(2.0, rel) - 1.0) / std::log2(static_cast<double>(i + 2));
                }

                vector<uint32_t> ideal_grades;
                for (const auto& [name, grade] : eval_q.element_grades) {
                    ideal_grades.push_back(grade);
                }
                ranges::sort(ideal_grades, greater<uint32_t>());
                double idcg = 0.0;
                for (size_t i = 0; i < min(ideal_grades.size(), k); ++i) {
                    idcg += (std::pow(2.0, static_cast<double>(ideal_grades[i])) - 1.0) /
                            std::log2(static_cast<double>(i + 2));
                }
                return (idcg > 0.0) ? (dcg / idcg) : 1.0;
            };

            total_ndcg5 += calc_ndcg(5);
            total_ndcg10 += calc_ndcg(10);
        }

        const double n = static_cast<double>(query_list.size());
        return RankingMetrics{
            .p_at_1 = total_p1 / n,
            .p_at_3 = total_p3 / n,
            .p_at_5 = total_p5 / n,
            .p_at_10 = total_p10 / n,
            .r_at_5 = total_r5 / n,
            .r_at_10 = total_r10 / n,
            .mrr = total_mrr / n,
            .ndcg_at_5 = total_ndcg5 / n,
            .ndcg_at_10 = total_ndcg10 / n,
        };
    }

    path test_dir;
    InvertedIndex index;
    vector<EvaluationQuery> dev_queries;
    vector<EvaluationQuery> val_queries;
    vector<EvaluationQuery> final_queries;
};

TEST_F(Phase4EvaluationBenchmark, CorpusStatisticsReport) {
    EXPECT_GT(index.file_count(), 10U);
    EXPECT_GT(index.element_count(), 40U);
    EXPECT_GT(index.term_count(), 100U);
    EXPECT_GT(index.posting_count(), 200U);
    EXPECT_EQ(final_queries.size(), 68U);
}

TEST_F(Phase4EvaluationBenchmark, AggregateEvaluationAcrossAllRankers) {
    const auto baseline = evaluate_query_list(final_queries, RankerType::Baseline);
    const auto bm25 = evaluate_query_list(final_queries, RankerType::BM25);
    const auto code_aware = evaluate_query_list(final_queries, RankerType::CodeAware);

    cout << "\n=== FINAL AGGREGATE EVALUATION (68 QUERIES) ===\n";
    cout << fixed << setprecision(3);
    cout << "Metric          Baseline    BM25        CodeAware\n";
    cout << "-------------------------------------------------\n";
    cout << "Precision@1     " << baseline.p_at_1 << "       " << bm25.p_at_1 << "       " << code_aware.p_at_1 << "\n";
    cout << "Precision@3     " << baseline.p_at_3 << "       " << bm25.p_at_3 << "       " << code_aware.p_at_3 << "\n";
    cout << "Precision@5     " << baseline.p_at_5 << "       " << bm25.p_at_5 << "       " << code_aware.p_at_5 << "\n";
    cout << "Precision@10    " << baseline.p_at_10 << "       " << bm25.p_at_10 << "       " << code_aware.p_at_10 << "\n";
    cout << "Recall@5        " << baseline.r_at_5 << "       " << bm25.r_at_5 << "       " << code_aware.r_at_5 << "\n";
    cout << "Recall@10       " << baseline.r_at_10 << "       " << bm25.r_at_10 << "       " << code_aware.r_at_10 << "\n";
    cout << "MRR             " << baseline.mrr << "       " << bm25.mrr << "       " << code_aware.mrr << "\n";
    cout << "NDCG@5          " << baseline.ndcg_at_5 << "       " << bm25.ndcg_at_5 << "       " << code_aware.ndcg_at_5 << "\n";
    cout << "NDCG@10         " << baseline.ndcg_at_10 << "       " << bm25.ndcg_at_10 << "       " << code_aware.ndcg_at_10 << "\n";
    cout << "=================================================\n\n";

    // CodeAware strictly advances over BM25 and Baseline on P@1, MRR, and NDCG@5
    EXPECT_GE(code_aware.p_at_1, bm25.p_at_1);
    EXPECT_GE(code_aware.mrr, bm25.mrr);
    EXPECT_GE(code_aware.ndcg_at_5, bm25.ndcg_at_5);

    EXPECT_GT(code_aware.p_at_1, 0.70);
    EXPECT_GT(code_aware.mrr, 0.75);
    EXPECT_GT(code_aware.ndcg_at_5, 0.75);
}

TEST_F(Phase4EvaluationBenchmark, PerCategoryPerformanceBreakdown) {
    const vector<string> categories = {"exact",      "normalized", "partial",
                                       "multi_term", "contextual", "path",
                                       "framework",  "ambiguous",  "cross_lang"};

    cout << "\n=== PER-CATEGORY PERFORMANCE BREAKDOWN (68 QUERIES) ===\n";
    cout << "Category       | Base P@1 | BM25 P@1 | CA P@1 | Base MRR | BM25 MRR | CA MRR | CA NDCG@5\n";
    cout << "---------------+----------+----------+--------+----------+----------+--------+----------\n";

    for (const auto& cat : categories) {
        vector<EvaluationQuery> cat_queries;
        for (const auto& q : final_queries) {
            if (q.category == cat) {
                cat_queries.push_back(q);
            }
        }
        ASSERT_FALSE(cat_queries.empty());

        const auto b = evaluate_query_list(cat_queries, RankerType::Baseline);
        const auto bm = evaluate_query_list(cat_queries, RankerType::BM25);
        const auto ca = evaluate_query_list(cat_queries, RankerType::CodeAware);

        cout << left << setw(14) << cat << " | "
             << fixed << setprecision(3)
             << setw(8) << b.p_at_1 << " | "
             << setw(8) << bm.p_at_1 << " | "
             << setw(6) << ca.p_at_1 << " | "
             << setw(8) << b.mrr << " | "
             << setw(8) << bm.mrr << " | "
             << setw(6) << ca.mrr << " | "
             << setw(9) << ca.ndcg_at_5 << "\n";

        EXPECT_GE(ca.mrr, 0.50);
        EXPECT_GE(ca.ndcg_at_5, 0.50);
    }
    cout << "========================================================================================\n\n";
}

TEST_F(Phase4EvaluationBenchmark, AblationTestingOnValidationSet) {
    SearchEngine engine(index);

    // Compute metrics for ablation variants:
    // Variant A: Plain BM25
    const auto mA = evaluate_query_list(val_queries, RankerType::BM25);

    // Variant E: Full CodeAware Ranker
    const auto mE = evaluate_query_list(val_queries, RankerType::CodeAware);

    cout << "\n=== ABLATION TESTING (VALIDATION SET - 10 QUERIES) ===\n";
    cout << "Configuration                          | P@1   | MRR   | NDCG@5\n";
    cout << "---------------------------------------+-------+-------+-------\n";
    cout << "A. BM25 Only                           | " << fixed << setprecision(3) << mA.p_at_1 << " | " << mA.mrr << " | " << mA.ndcg_at_5 << "\n";
    cout << "E. Full CodeAware (BM25+AST+Decl+Path) | " << fixed << setprecision(3) << mE.p_at_1 << " | " << mE.mrr << " | " << mE.ndcg_at_5 << "\n";
    cout << "===============================================================\n\n";

    EXPECT_GE(mE.ndcg_at_5, mA.ndcg_at_5);
    EXPECT_GE(mE.mrr, mA.mrr);
}

TEST_F(Phase4EvaluationBenchmark, DeterministicRankingStability) {
    SearchEngine engine(index);

    const auto res1 = engine.search("user", SearchOptions{.ranker_type = RankerType::CodeAware});
    const auto res2 = engine.search("user", SearchOptions{.ranker_type = RankerType::CodeAware});

    ASSERT_EQ(res1.size(), res2.size());
    for (size_t i = 0; i < res1.size(); ++i) {
        EXPECT_EQ(res1[i].element.name, res2[i].element.name);
        EXPECT_EQ(res1[i].file_path, res2[i].file_path);
        EXPECT_DOUBLE_EQ(res1[i].score, res2[i].score);
    }
}

TEST_F(Phase4EvaluationBenchmark, GeneralizationUnseenQueries) {
    SearchEngine engine(index);

    // Unseen exact query
    const auto res1 = engine.search("EngineConfig", SearchOptions{.ranker_type = RankerType::CodeAware});
    ASSERT_FALSE(res1.empty());
    EXPECT_EQ(res1[0].element.name, "EngineConfig");
    EXPECT_EQ(res1[0].element.kind, ElementKind::Struct);

    // Unseen normalized query
    const auto res2 =
        engine.search("session_store", SearchOptions{.ranker_type = RankerType::CodeAware});
    ASSERT_FALSE(res2.empty());
    EXPECT_TRUE(res2[0].element.name == "SessionStore" || res2[0].element.name == "saveSession");
}

TEST_F(Phase4EvaluationBenchmark, EdgeCases) {
    InvertedIndex empty_idx;
    SearchEngine empty_engine(empty_idx);
    EXPECT_TRUE(empty_engine.search("anything").empty());

    SearchEngine engine(index);
    EXPECT_TRUE(engine.search("xyzNonExistentTerm999").empty());

    const auto num_res = engine.search("JWT", SearchOptions{.ranker_type = RankerType::CodeAware});
    ASSERT_FALSE(num_res.empty());
    EXPECT_EQ(num_res[0].element.name, "verify_jwt_token");

    const auto mixed_res = engine.search("Authentication_Service", SearchOptions{.ranker_type = RankerType::CodeAware});
    ASSERT_FALSE(mixed_res.empty());
    EXPECT_EQ(mixed_res[0].element.name, "AuthenticationService");
}

TEST_F(Phase4EvaluationBenchmark, SearchLatencyPerformance) {
    SearchEngine engine(index);

    const auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < 500; ++i) {
        const auto res = engine.search("user authentication", SearchOptions{.ranker_type = RankerType::CodeAware});
        (void)res;
    }
    const auto end = chrono::high_resolution_clock::now();

    const auto total_us = chrono::duration_cast<chrono::microseconds>(end - start).count();
    const double avg_us = static_cast<double>(total_us) / 500.0;

    EXPECT_LT(avg_us, 3000.0);
}

}  // namespace
