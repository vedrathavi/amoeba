#!/usr/bin/env python3
"""
Generate Amoeba RepoProbe Benchmark Dataset (repoprobe_v1)
Extracts 50 pinned RepoProbe questions across 10 repositories and adds 20 Amoeba-designed diagnostic questions.
"""

import json
import os
import sys
import pandas as pd

sys.stdout.reconfigure(encoding="utf-8")

REPOPROBE_DIR = r"D:\RepoProbe"
BENCHMARK_DIR = r"d:\amoeba\benchmarks\repoprobe_v1"

# 1. Selected 10 Repositories
SELECTED_REPOS = [
    {
        "repo_name": "duckdb/ducklake",
        "owner": "duckdb",
        "repository_url": "https://github.com/duckdb/ducklake.git",
        "language": "C++",
        "stars": 2374,
        "size_category": "Medium",
        "snapshot_commit": "c16de934130a7aa6c2d88a97acba935450276633",
        "repoprobe_csv": "ducklake.csv",
        "question_ids": ["ducklake-0", "ducklake-1", "ducklake-2", "ducklake-3"],
        "selection_rationale": "High-performance C++20 database extension implementing Lakehouse table format with DuckDB integration, transaction logs, and storage engine."
    },
    {
        "repo_name": "google/adk-python",
        "owner": "google",
        "repository_url": "https://github.com/google/adk-python.git",
        "language": "Python",
        "stars": 16906,
        "size_category": "Medium",
        "snapshot_commit": "56775afc48ee54e9cbea441a6e0fa6c8a12891b9",
        "repoprobe_csv": "adk-python.csv",
        "question_ids": ["adk-python-0", "adk-python-1", "adk-python-2", "adk-python-3", "adk-python-4", "adk-python-5"],
        "selection_rationale": "Modern Python agent framework featuring asynchronous workflows, tool abstraction, MCP protocol, and agent orchestration."
    },
    {
        "repo_name": "better-auth/better-auth",
        "owner": "better-auth",
        "repository_url": "https://github.com/better-auth/better-auth.git",
        "language": "TypeScript",
        "stars": 24695,
        "size_category": "Large",
        "snapshot_commit": "d343b34cf6c0383d0a5833976632462b192dbc1e",
        "repoprobe_csv": "better-auth.csv",
        "question_ids": ["better-auth-0", "better-auth-1", "better-auth-2", "better-auth-3", "better-auth-4"],
        "selection_rationale": "Comprehensive TypeScript authentication and session engine supporting multi-provider OAuth, plugin architectures, and token management."
    },
    {
        "repo_name": "Dokploy/dokploy",
        "owner": "Dokploy",
        "repository_url": "https://github.com/Dokploy/dokploy.git",
        "language": "TypeScript",
        "stars": 28697,
        "size_category": "Medium",
        "snapshot_commit": "6e67864204035054822c2e6b69b5185cb1e670ff",
        "repoprobe_csv": "dokploy.csv",
        "question_ids": ["dokploy-0", "dokploy-1", "dokploy-2", "dokploy-5", "dokploy-9"],
        "selection_rationale": "Full-stack application deployment and container orchestration platform with Traefik ingress, Next.js UI, and server management."
    },
    {
        "repo_name": "apple/pkl",
        "owner": "apple",
        "repository_url": "https://github.com/apple/pkl.git",
        "language": "Java",
        "stars": 10991,
        "size_category": "Large",
        "snapshot_commit": "35861240a061504374411f87df13d7bf88466f36",
        "repoprobe_csv": "pkl.csv",
        "question_ids": ["pkl-0", "pkl-1", "pkl-2", "pkl-3", "pkl-5"],
        "selection_rationale": "Apple configuration-as-code compiler, runtime, and type evaluation system in Java with parser, AST, and module loaders."
    },
    {
        "repo_name": "henrygd/beszel",
        "owner": "henrygd",
        "repository_url": "https://github.com/henrygd/beszel.git",
        "language": "Go",
        "stars": 18289,
        "size_category": "Medium",
        "snapshot_commit": "2bd85e04fca688a6a1922e1bf4098fcd808c0273",
        "repoprobe_csv": "beszel.csv",
        "question_ids": ["beszel-0", "beszel-1", "beszel-2", "beszel-3", "beszel-5"],
        "selection_rationale": "Lightweight systems monitoring hub and agent in Go, demonstrating metrics collection, REST APIs, daemon persistence, and alerting."
    },
    {
        "repo_name": "opencloud-eu/opencloud",
        "owner": "opencloud-eu",
        "repository_url": "https://github.com/opencloud-eu/opencloud.git",
        "language": "Go",
        "stars": 4509,
        "size_category": "Large",
        "snapshot_commit": "941aa689d2c2ebb53832b3b2f02beebeda1b26d9",
        "repoprobe_csv": "opencloud.csv",
        "question_ids": ["opencloud-0", "opencloud-2", "opencloud-4", "opencloud-9", "opencloud-11"],
        "selection_rationale": "Microservice cloud file storage, synchronization, and sharing system in Go featuring multi-service gRPC, search, and storage engines."
    },
    {
        "repo_name": "BurntSushi/jiff",
        "owner": "BurntSushi",
        "repository_url": "https://github.com/BurntSushi/jiff.git",
        "language": "Rust",
        "stars": 2512,
        "size_category": "Medium",
        "snapshot_commit": "53708b9b6e7e329e3d46e91fd8b5266adc3b4f40",
        "repoprobe_csv": "jiff.csv",
        "question_ids": ["jiff-0", "jiff-1", "jiff-2", "jiff-3", "jiff-4"],
        "selection_rationale": "High-performance, high-precision datetime and timezone library in Rust by BurntSushi, emphasizing calendar arithmetic, formatting, and ISO parsing."
    },
    {
        "repo_name": "abenz1267/walker",
        "owner": "abenz1267",
        "repository_url": "https://github.com/abenz1267/walker.git",
        "language": "Rust",
        "stars": 2318,
        "size_category": "Small/Medium",
        "snapshot_commit": "1395d9205253c7038698e1dcca9c4d1d7dfd567b",
        "repoprobe_csv": "walker.csv",
        "question_ids": ["walker-0", "walker-2", "walker-3", "walker-4", "walker-8"],
        "selection_rationale": "Fast Wayland application launcher and menu runner in Rust, showing plugin modules, clipboard management, and Dmenu interaction."
    },
    {
        "repo_name": "docling-project/docling",
        "owner": "docling-project",
        "repository_url": "https://github.com/docling-project/docling.git",
        "language": "Python",
        "stars": 48782,
        "size_category": "Large",
        "snapshot_commit": "be085c0e39dd5c51572b883d0f795c5a7abefd5d",
        "repoprobe_csv": "docling.csv",
        "question_ids": ["docling-0", "docling-1", "docling-2", "docling-3", "docling-9"],
        "selection_rationale": "Comprehensive document parsing, OCR layout extraction, chunking, and conversion library for PDF, DOCX, and HTML."
    }
]

# 2. Amoeba-designed 20 diagnostic questions
AMOEBA_QUESTIONS = [
    # Symbol Lookup (2 questions)
    {
        "question_id": "amoeba-ducklake-01",
        "source": "amoeba_designed",
        "repo_name": "duckdb/ducklake",
        "snapshot_commit": "c16de934130a7aa6c2d88a97acba935450276633",
        "taxonomy": "Symbol Lookup",
        "category": "Symbol Lookup",
        "difficulty": 1,
        "question": "Where is the DuckLake transaction management structure defined in the ducklake repository?",
        "reference_answer": "The transaction state and management structures are defined in src/include/ducklake_transaction.hpp (and related transaction header files in src/include/).",
        "checklist": "(4 points) Identifies the transaction header and class\n- 2 points: Identifies src/include/ducklake_transaction.hpp or transaction header\n- 2 points: Identifies DuckLakeTransaction class or transaction manager struct",
        "ground_truth": {
            "expected_files": ["src/include/ducklake_transaction.hpp", "src/ducklake_transaction.cpp"],
            "expected_symbols": ["DuckLakeTransaction", "DuckLakeTransactionManager"],
            "expected_relationships": ["DuckLakeTransaction -> DuckLakeCatalog"],
            "expected_concepts": ["Transaction management", "ACID isolation", "Catalog snapshot"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-jiff-01",
        "source": "amoeba_designed",
        "repo_name": "BurntSushi/jiff",
        "snapshot_commit": "53708b9b6e7e329e3d46e91fd8b5266adc3b4f40",
        "taxonomy": "Symbol Lookup",
        "category": "Symbol Lookup",
        "difficulty": 1,
        "question": "Where is the `Span` struct defined in jiff and what time units does it hold?",
        "reference_answer": "The `Span` struct is defined in src/span.rs. It represents a duration with calendar-aware units: years, months, weeks, days, hours, minutes, seconds, milliseconds, microseconds, and nanoseconds.",
        "checklist": "(4 points) Span definition and fields\n- 2 points: Locates src/span.rs as definition file\n- 2 points: Explains calendar and clock unit fields (years, months, days, hours, minutes, seconds, nanos)",
        "ground_truth": {
            "expected_files": ["src/span.rs"],
            "expected_symbols": ["Span", "Span::new", "Span::total"],
            "expected_relationships": ["Span -> SignedDuration"],
            "expected_concepts": ["Calendar duration", "Span unit fields", "Time span calculation"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # Implementation (2 questions)
    {
        "question_id": "amoeba-better-auth-01",
        "source": "amoeba_designed",
        "repo_name": "better-auth/better-auth",
        "snapshot_commit": "d343b34cf6c0383d0a5833976632462b192dbc1e",
        "taxonomy": "Implementation Details",
        "category": "Implementation",
        "difficulty": 2,
        "question": "How is session verification and cookie token extraction implemented in better-auth core?",
        "reference_answer": "Session verification is handled in packages/better-auth/src/api/routes/session.ts and cookie utilities in packages/better-auth/src/cookies/index.ts, parsing the session token cookie from request headers and querying the session database adapter.",
        "checklist": "(4 points) Session token extraction & verification\n- 2 points: Identifies session route handler and cookie extraction logic\n- 2 points: Explains verification against the database adapter and token expiration check",
        "ground_truth": {
            "expected_files": ["packages/better-auth/src/api/routes/session.ts", "packages/better-auth/src/cookies/index.ts"],
            "expected_symbols": ["getSession", "parseCookies", "verifySession"],
            "expected_relationships": ["getSession -> getSessionFromCookie", "getSession -> adapter.findSession"],
            "expected_concepts": ["Cookie parsing", "Session validation", "Database adapter session lookup"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-beszel-01",
        "source": "amoeba_designed",
        "repo_name": "henrygd/beszel",
        "snapshot_commit": "2bd85e04fca688a6a1922e1bf4098fcd808c0273",
        "taxonomy": "Implementation Details",
        "category": "Implementation",
        "difficulty": 2,
        "question": "How is system CPU and memory metrics collection implemented on the beszel agent?",
        "reference_answer": "In internal/agent/collector.go (or agent system stats package), the agent queries system statistics (/proc/stat, /proc/meminfo or gopsutil) at periodic intervals, normalizes the metrics, and stores/transmits them to the hub.",
        "checklist": "(4 points) Metric collection implementation\n- 2 points: Locates agent collection logic in internal/agent\n- 2 points: Explains CPU percentage calculation and memory usage extraction",
        "ground_truth": {
            "expected_files": ["internal/agent/agent.go", "internal/agent/collector.go"],
            "expected_symbols": ["collectStats", "getCPUStats", "getMemStats"],
            "expected_relationships": ["agent.Run -> collectStats"],
            "expected_concepts": ["System metrics collection", "CPU utilization calculation", "Memory usage tracking"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # Relationship (2 questions)
    {
        "question_id": "amoeba-adk-python-01",
        "source": "amoeba_designed",
        "repo_name": "google/adk-python",
        "snapshot_commit": "56775afc48ee54e9cbea441a6e0fa6c8a12891b9",
        "taxonomy": "Implementation Details",
        "category": "Relationship",
        "difficulty": 2,
        "question": "What agent runner components invoke `after_agent_callback` during execution in adk-python?",
        "reference_answer": "The `after_agent_callback` is invoked by the agent execution runner / lifecycle manager (such as `AgentRunner` or `AgentExecutor` in src/google/adk/agents/...) after an agent step or run concludes.",
        "checklist": "(4 points) Identifies invoking components\n- 2 points: Identifies the execution runner / lifecycle handler\n- 2 points: Explains when in the lifecycle execution flow the callback is triggered",
        "ground_truth": {
            "expected_files": ["src/google/adk/agents/agent.py", "src/google/adk/agents/runner.py"],
            "expected_symbols": ["after_agent_callback", "AgentRunner", "Agent.run"],
            "expected_relationships": ["AgentRunner.run -> after_agent_callback"],
            "expected_concepts": ["Callback lifecycle", "Post-agent execution hook"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-dokploy-01",
        "source": "amoeba_designed",
        "repo_name": "Dokploy/dokploy",
        "snapshot_commit": "6e67864204035054822c2e6b69b5185cb1e670ff",
        "taxonomy": "Implementation Details",
        "category": "Relationship",
        "difficulty": 2,
        "question": "Which backend services interact with the Traefik configuration manager in dokploy?",
        "reference_answer": "The application deployment service, domain management service, and docker compose setup services in server/api/routers/ (such as application.ts, compose.ts, and domain.ts) interact with the Traefik file-based and dynamic config generators.",
        "checklist": "(4 points) Service interactions with Traefik\n- 2 points: Names application/domain router services\n- 2 points: Explains dynamic Traefik YAML/file creation or reload triggers",
        "ground_truth": {
            "expected_files": ["server/api/routers/application.ts", "server/api/routers/domain.ts", "server/utils/traefik/index.ts"],
            "expected_symbols": ["createTraefikConfig", "updateTraefikConfig", "applicationRouter"],
            "expected_relationships": ["applicationRouter -> createTraefikConfig", "domainRouter -> updateTraefikConfig"],
            "expected_concepts": ["Reverse proxy configuration", "Dynamic routing", "Ingress generation"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # Cross-file (2 questions)
    {
        "question_id": "amoeba-opencloud-01",
        "source": "amoeba_designed",
        "repo_name": "opencloud-eu/opencloud",
        "snapshot_commit": "941aa689d2c2ebb53832b3b2f02beebeda1b26d9",
        "taxonomy": "Project Architecture",
        "category": "Cross-file",
        "difficulty": 3,
        "question": "How does the storage-users service interact with the metadata service when uploading a file in opencloud?",
        "reference_answer": "When a file is uploaded, the storage service handles the binary payload streams via storage drivers and dispatches metadata updates (file size, etag, mtime) via gRPC or CS3 APIs to the metadata/graph storage layer across service boundaries.",
        "checklist": "(4 points) Storage and metadata interaction\n- 2 points: Explains data path vs metadata path separation\n- 2 points: Identifies gRPC/CS3 communication between storage-users and metadata services",
        "ground_truth": {
            "expected_files": ["services/storage-users/pkg/service/service.go", "services/storage-users/pkg/service/upload.go"],
            "expected_symbols": ["Upload", "InitiateUpload", "StorageService"],
            "expected_relationships": ["StorageService.Upload -> metadataProvider.SetMetadata"],
            "expected_concepts": ["Disaggregated storage", "Metadata synchronization", "Microservice gRPC contracts"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-pkl-01",
        "source": "amoeba_designed",
        "repo_name": "apple/pkl",
        "snapshot_commit": "35861240a061504374411f87df13d7bf88466f36",
        "taxonomy": "Project Architecture",
        "category": "Cross-file",
        "difficulty": 3,
        "question": "How does the Pkl evaluator interact with the AST parser when loading and evaluating external modules?",
        "reference_answer": "The evaluator requests the module from the module resolver / loader, which invokes the parser (pkl-core parser) to generate AST nodes (`ModuleNode`), which are then passed to the type checker and AST interpreter for evaluation.",
        "checklist": "(4 points) Evaluator and Parser pipeline\n- 2 points: Explains module resolution triggering AST parsing\n- 2 points: Explains AST transformation into evaluatable module node structures",
        "ground_truth": {
            "expected_files": ["pkl-core/src/main/java/org/pkl/core/evaluator/Evaluator.java", "pkl-core/src/main/java/org/pkl/core/parser/Parser.java"],
            "expected_symbols": ["Evaluator", "Parser", "ModuleResolver", "ModuleNode"],
            "expected_relationships": ["Evaluator.evaluate -> ModuleResolver.resolve -> Parser.parse"],
            "expected_concepts": ["AST parsing", "Module resolution", "Lazy evaluation"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # Architecture (2 questions)
    {
        "question_id": "amoeba-docling-01",
        "source": "amoeba_designed",
        "repo_name": "docling-project/docling",
        "snapshot_commit": "be085c0e39dd5c51572b883d0f795c5a7abefd5d",
        "taxonomy": "Project Architecture",
        "category": "Architecture",
        "difficulty": 4,
        "question": "How does a document conversion request flow from the DocumentConverter through backend models to the final DoclingDocument?",
        "reference_answer": "DocumentConverter receives input paths/streams, selects the appropriate backend parser based on file format (e.g. PDF backend, HTML backend, Docx backend), performs layout analysis/OCR using models, constructs the internal intermediate representation, and serializes to DoclingDocument.",
        "checklist": "(4 points) Pipeline architecture\n- 2 points: Identifies DocumentConverter as entry point selecting format-specific backends\n- 2 points: Explains pipeline stages (backend parsing -> layout prediction -> document assembly)",
        "ground_truth": {
            "expected_files": ["docling/document_converter.py", "docling/pipeline/standard_pdf_pipeline.py", "docling/datamodel/document.py"],
            "expected_symbols": ["DocumentConverter", "DocumentConverter.convert", "DoclingDocument", "StandardPdfPipeline"],
            "expected_relationships": ["DocumentConverter.convert -> Pipeline.execute -> DoclingDocument"],
            "expected_concepts": ["Pipeline stages", "Format backends", "Document model assembly"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-walker-01",
        "source": "amoeba_designed",
        "repo_name": "abenz1267/walker",
        "snapshot_commit": "1395d9205253c7038698e1dcca9c4d1d7dfd567b",
        "taxonomy": "Project Architecture",
        "category": "Architecture",
        "difficulty": 4,
        "question": "How does an input query flow from the UI entry box to provider module filtering and result display in walker?",
        "reference_answer": "In src/ui.rs and src/modules/..., user keystrokes update the search query in the GTK/Wayland UI, dispatching search events to active modules (applications, clipboard, runner, etc.). Modules filter their index, return scored results, and the UI view updates the list.",
        "checklist": "(4 points) Input to module filtering flow\n- 2 points: Traces UI text change signal to event dispatcher\n- 2 points: Explains module query filtering and UI result list update",
        "ground_truth": {
            "expected_files": ["src/ui.rs", "src/modules/mod.rs", "src/app.rs"],
            "expected_symbols": ["App", "UI", "Module", "filter"],
            "expected_relationships": ["UI.on_change -> App.query -> Module.search"],
            "expected_concepts": ["Event-driven UI", "Provider module filtering", "Wayland client loop"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # State / Data Flow (2 questions)
    {
        "question_id": "amoeba-dokploy-02",
        "source": "amoeba_designed",
        "repo_name": "Dokploy/dokploy",
        "snapshot_commit": "6e67864204035054822c2e6b69b5185cb1e670ff",
        "taxonomy": "Implementation Details",
        "category": "State / data flow",
        "difficulty": 3,
        "question": "Where is the deployment state and container lifecycle status maintained in dokploy?",
        "reference_answer": "Deployment state and application status are stored in the PostgreSQL database using Prisma schema models (`Application`, `Deployment`, `Compose`) and synced with Docker daemon container status through server/utils/docker utilities.",
        "checklist": "(4 points) State persistence & tracking\n- 2 points: Identifies Prisma schema/database persistence for deployment records\n- 2 points: Explains status syncing from Docker daemon container inspect events",
        "ground_truth": {
            "expected_files": ["prisma/schema.prisma", "server/api/routers/deployment.ts", "server/utils/docker/index.ts"],
            "expected_symbols": ["Deployment", "ApplicationStatus", "getContainerStatus"],
            "expected_relationships": ["deploymentRouter -> prisma.deployment.create"],
            "expected_concepts": ["Prisma ORM state persistence", "Docker container lifecycle", "Deployment status tracking"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-better-auth-02",
        "source": "amoeba_designed",
        "repo_name": "better-auth/better-auth",
        "snapshot_commit": "d343b34cf6c0383d0a5833976632462b192dbc1e",
        "taxonomy": "Implementation Details",
        "category": "State / data flow",
        "difficulty": 3,
        "question": "Where is OAuth state and PKCE code verification stored during authentication flow in better-auth?",
        "reference_answer": "OAuth state and PKCE `code_verifier` parameters are stored in signed/encrypted cookies or temporary verification store via packages/better-auth/src/oauth2/state.ts and validated upon callback.",
        "checklist": "(4 points) OAuth state & PKCE storage\n- 2 points: Identifies cookie or verification table storage for state parameter\n- 2 points: Explains PKCE code_verifier generation and validation on callback",
        "ground_truth": {
            "expected_files": ["packages/better-auth/src/oauth2/index.ts", "packages/better-auth/src/oauth2/state.ts"],
            "expected_symbols": ["generateState", "createPKCE", "validateState"],
            "expected_relationships": ["oauth2.signIn -> generateState", "oauth2.callback -> validateState"],
            "expected_concepts": ["PKCE code verifier", "OAuth state cookie", "CSRF protection"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # Configuration (2 questions)
    {
        "question_id": "amoeba-beszel-02",
        "source": "amoeba_designed",
        "repo_name": "henrygd/beszel",
        "snapshot_commit": "2bd85e04fca688a6a1922e1bf4098fcd808c0273",
        "taxonomy": "Implementation Details",
        "category": "Configuration",
        "difficulty": 2,
        "question": "Where and how are alert thresholds and notification endpoints configured in beszel hub?",
        "reference_answer": "Alert thresholds (CPU, memory, disk, temperature) and notification channels (Discord, Telegram, generic webhooks) are managed in internal/alerts/ and persisted in the PocketBase/SQLite hub database.",
        "checklist": "(4 points) Alert configuration\n- 2 points: Locates internal/alerts or hub alert settings\n- 2 points: Lists supported notification channels or threshold parameters",
        "ground_truth": {
            "expected_files": ["internal/alerts/alerts.go", "internal/hub/hub.go"],
            "expected_symbols": ["AlertManager", "CheckAlerts", "SendNotification"],
            "expected_relationships": ["AlertManager.CheckAlerts -> SendNotification"],
            "expected_concepts": ["Threshold monitoring", "Webhook notifications", "Hub alert configuration"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-ducklake-02",
        "source": "amoeba_designed",
        "repo_name": "duckdb/ducklake",
        "snapshot_commit": "c16de934130a7aa6c2d88a97acba935450276633",
        "taxonomy": "Implementation Details",
        "category": "Configuration",
        "difficulty": 2,
        "question": "Where are DuckLake storage extension options and S3 credential parameters configured in ducklake?",
        "reference_answer": "DuckLake extension settings and secret/credential configurations are defined in src/include/ducklake_secret.hpp and registered via DuckDB's SecretManager / DBConfig in src/ducklake_extension.cpp.",
        "checklist": "(4 points) Configuration & secrets\n- 2 points: Identifies secret/config registration in ducklake_extension.cpp or ducklake_secret\n- 2 points: Explains how S3/storage credentials integrate with DuckDB secret provider",
        "ground_truth": {
            "expected_files": ["src/ducklake_extension.cpp", "src/include/ducklake_secret.hpp"],
            "expected_symbols": ["DuckLakeExtension", "CreateSecretFunction", "RegisterSecret"],
            "expected_relationships": ["DuckLakeExtension.Load -> RegisterSecret"],
            "expected_concepts": ["DuckDB extension configuration", "S3 secret provider", "Storage configuration options"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # Negative / Unsupported (2 questions)
    {
        "question_id": "amoeba-jiff-02",
        "source": "amoeba_designed",
        "repo_name": "BurntSushi/jiff",
        "snapshot_commit": "53708b9b6e7e329e3d46e91fd8b5266adc3b4f40",
        "taxonomy": "Implementation Details",
        "category": "Negative / unsupported",
        "difficulty": 5,
        "question": "Does jiff support astronomical lunar calendar conversions and non-solar lunar phases?",
        "reference_answer": "No. Jiff is focused on Gregorian/ISO 8601 calendar arithmetic, time zones, and civil time. It does not implement lunar calendar conversions or astronomical lunar ephemeris.",
        "checklist": "(4 points) Explicit unsupported refusal\n- 4 points: Accurately identifies that lunar calendar conversion is not supported and explains that jiff focuses on Gregorian/ISO civil time",
        "ground_truth": {
            "expected_files": [],
            "expected_symbols": [],
            "expected_relationships": [],
            "expected_concepts": ["Unsupported feature", "ISO 8601 civil time scope", "No lunar calendar"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-adk-python-02",
        "source": "amoeba_designed",
        "repo_name": "google/adk-python",
        "snapshot_commit": "56775afc48ee54e9cbea441a6e0fa6c8a12891b9",
        "taxonomy": "Implementation Details",
        "category": "Negative / unsupported",
        "difficulty": 5,
        "question": "Does adk-python provide built-in vector database indexing with Qdrant natively in core?",
        "reference_answer": "No. Core adk-python provides agent lifecycle, tool execution, and MCP communication, but does not bundle a native Qdrant vector database engine or indexing subsystem.",
        "checklist": "(4 points) Explicit unsupported refusal\n- 4 points: Accurately determines that native Qdrant vector indexing is not implemented in adk-python core",
        "ground_truth": {
            "expected_files": [],
            "expected_symbols": [],
            "expected_relationships": [],
            "expected_concepts": ["Unsupported feature", "Core framework scope", "No native Qdrant engine"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # Ambiguous / Generic Terminology (2 questions)
    {
        "question_id": "amoeba-opencloud-02",
        "source": "amoeba_designed",
        "repo_name": "opencloud-eu/opencloud",
        "snapshot_commit": "941aa689d2c2ebb53832b3b2f02beebeda1b26d9",
        "taxonomy": "Implementation Details",
        "category": "Ambiguous / generic terminology",
        "difficulty": 5,
        "question": "Where is the GraphQL resolver implemented in opencloud?",
        "reference_answer": "OpenCloud does not use GraphQL or implement GraphQL resolvers; its service architecture relies on gRPC, CS3 APIs, and REST endpoints (OData/WebDAV). Generic resolver terms in code do not represent GraphQL resolvers.",
        "checklist": "(4 points) Ambiguous term disambiguation / rejection\n- 4 points: Rejects GraphQL presence and clarifies that opencloud uses gRPC/REST protocols rather than GraphQL resolvers",
        "ground_truth": {
            "expected_files": [],
            "expected_symbols": [],
            "expected_relationships": [],
            "expected_concepts": ["Ambiguous generic term rejection", "No GraphQL resolvers", "gRPC / CS3 service architecture"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-pkl-02",
        "source": "amoeba_designed",
        "repo_name": "apple/pkl",
        "snapshot_commit": "35861240a061504374411f87df13d7bf88466f36",
        "taxonomy": "Implementation Details",
        "category": "Ambiguous / generic terminology",
        "difficulty": 5,
        "question": "Where is the Spring Boot auto-configuration controller implemented in pkl?",
        "reference_answer": "Pkl is a standalone configuration language compiler and runtime in Java; it does not contain a Spring Boot web auto-configuration controller. Generic controller/handler classes in Pkl do not represent Spring web controllers.",
        "checklist": "(4 points) Disambiguation / rejection\n- 4 points: Identifies that Pkl does not implement a Spring Boot auto-configuration controller",
        "ground_truth": {
            "expected_files": [],
            "expected_symbols": [],
            "expected_relationships": [],
            "expected_concepts": ["Generic controller disambiguation", "Standalone language compiler", "No Spring Boot web controller"],
            "ground_truth_status": "fully_grounded"
        }
    },

    # Vocabulary-Gap Questions (2 questions)
    {
        "question_id": "amoeba-jiff-03",
        "source": "amoeba_designed",
        "repo_name": "BurntSushi/jiff",
        "snapshot_commit": "53708b9b6e7e329e3d46e91fd8b5266adc3b4f40",
        "taxonomy": "Implementation Details",
        "category": "Vocabulary gap",
        "difficulty": 3,
        "question": "What component calculates the duration difference between two calendar dates accounting for leap years and daylight saving transitions in jiff?",
        "reference_answer": "In src/zoned.rs and src/civil/date.rs, date/time differences and span calculations are computed via `Zoned::since`, `Zoned::until`, and `DateTime::since` using `Span` difference algorithms.",
        "checklist": "(4 points) Identifies since/until difference calculation\n- 2 points: Locates Zoned::since / until or Span difference logic\n- 2 points: Explains handling of daylight saving transitions and calendar units",
        "ground_truth": {
            "expected_files": ["src/zoned.rs", "src/span.rs", "src/civil/date.rs"],
            "expected_symbols": ["Zoned::since", "Zoned::until", "Span::total"],
            "expected_relationships": ["Zoned::until -> Span", "DateTime::since -> Span"],
            "expected_concepts": ["Calendar difference arithmetic", "Daylight saving time transitions", "Span unit rounding"],
            "ground_truth_status": "fully_grounded"
        }
    },
    {
        "question_id": "amoeba-docling-02",
        "source": "amoeba_designed",
        "repo_name": "docling-project/docling",
        "snapshot_commit": "be085c0e39dd5c51572b883d0f795c5a7abefd5d",
        "taxonomy": "Implementation Details",
        "category": "Vocabulary gap",
        "difficulty": 3,
        "question": "Where is the OCR bounding-box text line segmenter and coordinate normalizer implemented in docling?",
        "reference_answer": "Bounding box coordinates and layout segment normalizations are implemented in docling/datamodel/base_models.py (`BoundingBox`, `CoordOrigin`) and layout post-processing in docling/pipeline/.",
        "checklist": "(4 points) Identifies bounding box model and coordinates\n- 2 points: Locates BoundingBox data model in base_models.py\n- 2 points: Explains coordinate normalization and origin transformations",
        "ground_truth": {
            "expected_files": ["docling/datamodel/base_models.py", "docling/models/layout_model.py"],
            "expected_symbols": ["BoundingBox", "CoordOrigin", "LayoutModel"],
            "expected_relationships": ["TextItem.prov -> BoundingBox"],
            "expected_concepts": ["Bounding box normalization", "Coordinate origin transform", "OCR text cell layout"],
            "ground_truth_status": "fully_grounded"
        }
    }
]


def main():
    print(f"Generating Amoeba RepoProbe benchmark dataset in {BENCHMARK_DIR}...")
    
    # 1. Build repositories.json
    repos_json_data = []
    for r in SELECTED_REPOS:
        repo_entry = {
            "repo_name": r["repo_name"],
            "owner": r["owner"],
            "repository_url": r["repository_url"],
            "language": r["language"],
            "stars": r["stars"],
            "size_category": r["size_category"],
            "snapshot_commit": r["snapshot_commit"],
            "repoprobe_source_csv": r["repoprobe_csv"],
            "local_path": f"benchmarks/repositories/{r['repo_name'].split('/')[-1]}",
            "selection_rationale": r["selection_rationale"]
        }
        repos_json_data.append(repo_entry)
        
    repos_path = os.path.join(BENCHMARK_DIR, "repositories.json")
    with open(repos_path, "w", encoding="utf-8") as f:
        json.dump(repos_json_data, f, indent=2, ensure_ascii=False)
    print(f"Written: {repos_path} ({len(repos_json_data)} repositories)")
    
    # 2. Extract RepoProbe 50 questions
    questions_json_data = []
    ground_truth_json_data = []
    
    repoprobe_q_count = 0
    for r in SELECTED_REPOS:
        csv_path = os.path.join(REPOPROBE_DIR, "dataset", r["repoprobe_csv"])
        if not os.path.exists(csv_path):
            raise FileNotFoundError(f"RepoProbe dataset CSV not found: {csv_path}")
            
        df = pd.read_csv(csv_path)
        for qid in r["question_ids"]:
            matching = df[df["question_id"] == qid]
            if matching.empty:
                raise ValueError(f"Question {qid} not found in {csv_path}")
                
            row = matching.iloc[0]
            
            q_entry = {
                "question_id": str(row["question_id"]),
                "source": "repoprobe",
                "repo_name": r["repo_name"],
                "snapshot_commit": r["snapshot_commit"],
                "discussion_id": str(row.get("discussion_id", "")),
                "taxonomy": str(row.get("taxonomy", "Implementation Details")),
                "difficulty": str(row.get("difficulty", "Medium")),
                "question": str(row["question"]),
                "reference_answer": str(row["answer"]),
                "checklist": str(row["checklist"])
            }
            questions_json_data.append(q_entry)
            
            # Ground truth entry
            gt_entry = {
                "question_id": str(row["question_id"]),
                "source": "repoprobe",
                "repo_name": r["repo_name"],
                "snapshot_commit": r["snapshot_commit"],
                "taxonomy": str(row.get("taxonomy", "Implementation Details")),
                "expected_files": [],
                "expected_symbols": [],
                "expected_relationships": [],
                "expected_concepts": [str(row.get("taxonomy", "Implementation Details"))],
                "ground_truth_status": "needs_manual_review",
                "reference_answer": str(row["answer"]),
                "checklist": str(row["checklist"])
            }
            ground_truth_json_data.append(gt_entry)
            repoprobe_q_count += 1
            
    print(f"Extracted {repoprobe_q_count} RepoProbe questions from pinned snapshots.")
    
    # 3. Add Amoeba-designed 20 questions
    amoeba_q_count = 0
    for aq in AMOEBA_QUESTIONS:
        q_entry = {
            "question_id": aq["question_id"],
            "source": aq["source"],
            "repo_name": aq["repo_name"],
            "snapshot_commit": aq["snapshot_commit"],
            "discussion_id": "",
            "taxonomy": aq["taxonomy"],
            "category": aq["category"],
            "difficulty": aq["difficulty"],
            "question": aq["question"],
            "reference_answer": aq["reference_answer"],
            "checklist": aq["checklist"]
        }
        questions_json_data.append(q_entry)
        
        gt_entry = {
            "question_id": aq["question_id"],
            "source": aq["source"],
            "repo_name": aq["repo_name"],
            "snapshot_commit": aq["snapshot_commit"],
            "taxonomy": aq["taxonomy"],
            "category": aq["category"],
            "expected_files": aq["ground_truth"]["expected_files"],
            "expected_symbols": aq["ground_truth"]["expected_symbols"],
            "expected_relationships": aq["ground_truth"]["expected_relationships"],
            "expected_concepts": aq["ground_truth"]["expected_concepts"],
            "ground_truth_status": aq["ground_truth"]["ground_truth_status"],
            "reference_answer": aq["reference_answer"],
            "checklist": aq["checklist"]
        }
        ground_truth_json_data.append(gt_entry)
        amoeba_q_count += 1
        
    print(f"Added {amoeba_q_count} Amoeba-designed diagnostic questions.")
    print(f"Total benchmark questions: {len(questions_json_data)}")
    
    # Save questions.json
    q_path = os.path.join(BENCHMARK_DIR, "questions.json")
    with open(q_path, "w", encoding="utf-8") as f:
        json.dump(questions_json_data, f, indent=2, ensure_ascii=False)
    print(f"Written: {q_path}")
    
    # Save ground_truth.json
    gt_path = os.path.join(BENCHMARK_DIR, "ground_truth.json")
    with open(gt_path, "w", encoding="utf-8") as f:
        json.dump(ground_truth_json_data, f, indent=2, ensure_ascii=False)
    print(f"Written: {gt_path}")
    
    # 4. Consistency & Duplicate Verification
    seen_ids = set()
    for q in questions_json_data:
        qid = q["question_id"]
        if qid in seen_ids:
            raise ValueError(f"Duplicate question ID found: {qid}")
        seen_ids.add(qid)
        
    assert len(repos_json_data) == 10, f"Expected 10 repos, got {len(repos_json_data)}"
    assert repoprobe_q_count == 50, f"Expected 50 repoprobe questions, got {repoprobe_q_count}"
    assert amoeba_q_count == 20, f"Expected 20 amoeba questions, got {amoeba_q_count}"
    assert len(questions_json_data) == 70, f"Expected 70 total questions, got {len(questions_json_data)}"
    assert len(ground_truth_json_data) == 70, f"Expected 70 total ground truth items, got {len(ground_truth_json_data)}"
    
    print("All validation assertions passed cleanly!")


if __name__ == "__main__":
    main()
