# Technical Research & Investigations

This directory houses research notes, technical investigations, benchmark findings, and exploratory prototyping reports conducted for the **Amoeba** project.

## Purpose

As Amoeba progresses through its planned phases, architectural and algorithmic decisions will be backed by structured research and empirical measurements.

Future areas of investigation stored in this section will include:
* **Parsing Benchmarks**: Comparative evaluation of parsing throughput, memory consumption, and AST fidelity using Tree-sitter and alternative grammars.
* **Indexing Structures**: Research into inverted indexes, roaring bitmaps, trigram representations, suffix arrays, and compact serialization formats.
* **Vector Indexing & ANN**: Performance and memory characteristics of Approximate Nearest Neighbor algorithms (e.g., HNSW, ScaNN, FAISS) for code embedding retrieval.
* **Ranking & Scoring Models**: Hybrid retrieval algorithms combining exact symbol matches, structural relevance (BM25 / BM25F), and dense vector similarity.
* **Memory & Caching Architectures**: Cache-efficient data layouts, memory-mapped I/O (`mmap`), and zero-copy tokenization buffers.

## Contributing Research Notes

When contributing research:
1. Clearly document the problem statement and hypothesis.
2. Detail reproducible methodology, hardware specifications, and datasets.
3. Summarize key findings with actionable recommendations for the engine design.
