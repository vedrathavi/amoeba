# Phase 3.0 — Searchable Representation & Code Tokenization

## 1. Objective

Define the searchable representation derived from `ParsedFile` and `CodeElement[]`, and implement a deterministic, code-aware tokenizer that splits compound identifiers into searchable terms while preserving full identifiers.

---

## 2. Searchable Elements & Information Policy

Amoeba extracts searchable terms from:
1. **Element Names (`elem.name`)**: The primary identifier (classes, functions, methods, components, structs, interfaces, hooks, calls, selectors).
2. **Parent Context (`elem.parent_context`)**: Enclosing namespace, class, or rule name.
3. **Detail Metadata (`elem.detail`)**: Route patterns (e.g. Next.js routes) and CSS properties/utilities.
4. **File Paths (`parsed_file.file_path`)**: Path components and file stems so file-level context is searchable.

---

## 3. Code Tokenization Rules

The `CodeTokenizer` splits identifiers based on code conventions:
* **`camelCase` & `PascalCase`**: `getUserById` ──► `{"getuserbyid", "get", "user", "by", "id"}`
* **`snake_case`**: `user_service_handler` ──► `{"user_service_handler", "user", "service", "handler"}`
* **`SCREAMING_SNAKE_CASE`**: `MAX_BUFFER_SIZE` ──► `{"max_buffer_size", "max", "buffer", "size"}`
* **`kebab-case`**: `btn-primary-active` ──► `{"btn-primary-active", "btn", "primary", "active"}`
* **Acronyms in CamelCase**: `XMLReaderFactory` ──► `{"xmlreaderfactory", "xml", "reader", "factory"}`
* **Alphanumeric**: `User123Service` ──► `{"user123service", "user", "123", "service"}`

---

## 4. Normalization Policy

* **Case-Insensitive Dictionary**: All terms inserted into posting keys are lowercased.
* **Exact Identifiers Preserved**: Both the full normalized identifier and individual sub-words are indexed.
