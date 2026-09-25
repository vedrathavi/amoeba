# Phase 3.1 — In-Memory Inverted Index

## 1. Objective

Implement an in-memory inverted index data structure using an efficient integer-based ID strategy to avoid duplicating large symbol and path objects in posting lists.

---

## 2. ID Strategy & Ownership

```text
InvertedIndex
  ├── vector<IndexedFile> files_        // FileId: Index in files_
  ├── vector<IndexedElement> elements_  // ElementId: Index in elements_
  └── unordered_map<string, vector<ElementId>> postings_
```

* **`FileId`**: `uint32_t` representing the file index.
* **`ElementId`**: `uint32_t` representing the element index.
* **`PostingList`**: `vector<ElementId>` mapping a normalized term to all matching element IDs.

---

## 3. Invariants & Guarantees

1. **Deterministic Element References**: Every `ElementId` in a posting list references a valid entry in `elements_`.
2. **Deterministic File References**: Every `IndexedElement` contains a valid `FileId` pointing to an entry in `files_`.
3. **Posting Deduplication**: A term does not contain duplicate `ElementId` entries for the same code element.
4. **Const Lookup**: `InvertedIndex::lookup` does not mutate the index dictionary or posting lists.
