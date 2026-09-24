#include "amoeba/parser/source_parser.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {

using namespace std;
using namespace std::filesystem;
using namespace amoeba::parser;

class SourceParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto timestamp = chrono::steady_clock::now().time_since_epoch().count();
        test_dir = temp_directory_path() / ("amoeba_parser_test_" + to_string(timestamp));
        create_directories(test_dir);
    }

    void TearDown() override {
        error_code ec;
        remove_all(test_dir, ec);
    }

    path create_test_file(const string& filename, string_view content) {
        const path file_path = test_dir / filename;
        ofstream file(file_path, ios::binary);
        file << content;
        return file_path;
    }

    path test_dir;
    SourceParser parser;
};

// =============================================================================
// 1. Basic C / C++ Parsing
// =============================================================================

TEST_F(SourceParserTest, BasicFunctionParsing) {
    const string source = R"(
int add(int a, int b) {
    return a + b;
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "add");
    EXPECT_EQ(functions[0].location.start.line, 2U);
}

TEST_F(SourceParserTest, ClassAndMethodParsing) {
    const string source = R"(
class User {
public:
    void login();
    void logout() {
        cleanup();
    }
};
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "User");
    EXPECT_EQ(classes[0].location.start.line, 2U);

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 2U);
    EXPECT_EQ(methods[0].name, "login");
    EXPECT_EQ(methods[0].parent_context, "User");
    EXPECT_EQ(methods[1].name, "logout");
    EXPECT_EQ(methods[1].parent_context, "User");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_EQ(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "cleanup");
}

TEST_F(SourceParserTest, IncludeExtraction) {
    const string source = R"(
#include "database.h"
#include <iostream>

int main() {
    return 0;
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 2U);
    EXPECT_EQ(includes[0].name, "\"database.h\"");
    EXPECT_EQ(includes[1].name, "<iostream>");
}

TEST_F(SourceParserTest, FunctionCallExtraction) {
    const string source = R"(
void process() {
    init();
    logger.log("processing");
    shutdown();
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 3U);
    EXPECT_EQ(calls[0].name, "init");
    EXPECT_EQ(calls[1].name, "log");
    EXPECT_EQ(calls[2].name, "shutdown");
}

TEST_F(SourceParserTest, NestedStructureTraversal) {
    const string source = R"(
class Outer {
    struct Inner {
        void inner_method();
    };
    void outer_method() {
        helper();
    }
};
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "Outer");

    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "Inner");
    EXPECT_EQ(structs[0].parent_context, "Outer");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 2U);
    EXPECT_EQ(methods[0].name, "inner_method");
    EXPECT_EQ(methods[0].parent_context, "Inner");
    EXPECT_EQ(methods[1].name, "outer_method");
    EXPECT_EQ(methods[1].parent_context, "Outer");
}

TEST_F(SourceParserTest, StructParsing) {
    const string source = R"(
struct Point {
    int x;
    int y;
    void reset();
};
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "Point");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_EQ(methods.size(), 1U);
    EXPECT_EQ(methods[0].name, "reset");
    EXPECT_EQ(methods[0].parent_context, "Point");
}

TEST_F(SourceParserTest, CLanguageParsing) {
    const string source = R"(
#include <stdio.h>

struct Buffer {
    char* data;
    size_t size;
};

void buffer_init(struct Buffer* b) {
    printf("Init buffer\n");
}
)";

    const auto parsed = parser.parse_source(source, "C");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 1U);
    EXPECT_EQ(includes[0].name, "<stdio.h>");

    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "Buffer");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "buffer_init");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_EQ(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "printf");
}

TEST_F(SourceParserTest, RealisticCppFeatures) {
    const string source = R"(
#include <vector>
#include "amoeba/engine.hpp"

namespace amoeba::core {

template <typename T>
class Storage {
public:
    Storage() { init(); }
    ~Storage() { reset(); }

    void add_item(const T& item) {
        items.push_back(item);
    }

private:
    std::vector<T> items;
};

} // namespace amoeba::core
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "Storage");
    EXPECT_EQ(classes[0].parent_context, "amoeba::core");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 3U);
    EXPECT_EQ(methods[0].name, "Storage");
    EXPECT_EQ(methods[0].parent_context, "Storage");
    EXPECT_EQ(methods[1].name, "~Storage");
    EXPECT_EQ(methods[1].parent_context, "Storage");
    EXPECT_EQ(methods[2].name, "add_item");
    EXPECT_EQ(methods[2].parent_context, "Storage");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 3U);
    EXPECT_EQ(calls[0].name, "init");
    EXPECT_EQ(calls[1].name, "reset");
    EXPECT_EQ(calls[2].name, "push_back");
}

// =============================================================================
// 2. Python Parsing
// =============================================================================

TEST_F(SourceParserTest, PythonParsing) {
    const string source = R"(
import os
from pathlib import Path

class Repository:
    def __init__(self, path):
        self.path = path

    def scan(self):
        print(f"Scanning {self.path}")
        self.discover()

    def discover(self):
        pass

def standalone_helper():
    return Repository(".")
)";

    const auto parsed = parser.parse_source(source, "Python");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_GE(includes.size(), 2U);

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "Repository");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_EQ(methods.size(), 3U);
    EXPECT_EQ(methods[0].name, "__init__");
    EXPECT_EQ(methods[0].parent_context, "Repository");
    EXPECT_EQ(methods[1].name, "scan");
    EXPECT_EQ(methods[1].parent_context, "Repository");
    EXPECT_EQ(methods[2].name, "discover");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "standalone_helper");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 3U);
}

// =============================================================================
// 3. Java Parsing
// =============================================================================

TEST_F(SourceParserTest, JavaParsing) {
    const string source = R"(
package com.amoeba.search;

import java.util.List;
import java.util.ArrayList;

public interface Indexer {
    void index();
}

public class CodeIndex implements Indexer {
    private List<String> documents;

    public CodeIndex() {
        this.documents = new ArrayList<>();
    }

    @Override
    public void index() {
        processDocuments();
    }

    private void processDocuments() {
        System.out.println("indexing");
    }
}
)";

    const auto parsed = parser.parse_source(source, "Java");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto interfaces = parsed.get_elements_by_kind(ElementKind::Interface);
    ASSERT_EQ(interfaces.size(), 1U);
    EXPECT_EQ(interfaces[0].name, "Indexer");

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "CodeIndex");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 4U);
    EXPECT_EQ(methods[0].name, "index");
    EXPECT_EQ(methods[0].parent_context, "Indexer");
    EXPECT_EQ(methods[1].name, "CodeIndex");
    EXPECT_EQ(methods[1].parent_context, "CodeIndex");
    EXPECT_EQ(methods[2].name, "index");
    EXPECT_EQ(methods[2].parent_context, "CodeIndex");
    EXPECT_EQ(methods[3].name, "processDocuments");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 2U);
}

// =============================================================================
// 4. Go Parsing
// =============================================================================

TEST_F(SourceParserTest, GoParsing) {
    const string source = R"(
package main

import (
    "fmt"
    "os"
)

type Scanner interface {
    Scan() error
}

type LocalScanner struct {
    RootPath string
}

func (s *LocalScanner) Scan() error {
    fmt.Println(s.RootPath)
    return nil
}

func NewScanner(path string) *LocalScanner {
    return &LocalScanner{RootPath: path}
}
)";

    const auto parsed = parser.parse_source(source, "Go");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto interfaces = parsed.get_elements_by_kind(ElementKind::Interface);
    ASSERT_EQ(interfaces.size(), 1U);
    EXPECT_EQ(interfaces[0].name, "Scanner");

    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "LocalScanner");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_EQ(methods.size(), 1U);
    EXPECT_EQ(methods[0].name, "Scan");
    EXPECT_NE(methods[0].parent_context, "");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "NewScanner");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "Println");
}

// =============================================================================
// 5. Rust Parsing
// =============================================================================

TEST_F(SourceParserTest, RustParsing) {
    const string source = R"(
use std::path::PathBuf;

pub trait Engine {
    fn run(&self);
}

pub struct SearchEngine {
    root: PathBuf,
}

impl SearchEngine {
    pub fn new(path: PathBuf) -> Self {
        Self { root: path }
    }
}

impl Engine for SearchEngine {
    fn run(&self) {
        println!("Running engine");
    }
}

fn initialize() {
    let _ = SearchEngine::new(PathBuf::from("."));
}
)";

    const auto parsed = parser.parse_source(source, "Rust");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto traits = parsed.get_elements_by_kind(ElementKind::Interface);
    ASSERT_EQ(traits.size(), 1U);
    EXPECT_EQ(traits[0].name, "Engine");

    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "SearchEngine");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 3U);
    EXPECT_EQ(methods[0].name, "run");
    EXPECT_EQ(methods[0].parent_context, "Engine");
    EXPECT_EQ(methods[1].name, "new");
    EXPECT_EQ(methods[1].parent_context, "SearchEngine");
    EXPECT_EQ(methods[2].name, "run");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "initialize");

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 1U);
}

// =============================================================================
// 6. JavaScript & TypeScript Parsing
// =============================================================================

TEST_F(SourceParserTest, JavaScriptAndTypeScriptParsing) {
    const string source = R"(
import { useEffect, useState } from 'react';

export interface UserConfig {
    id: string;
    name: string;
}

export type Status = 'active' | 'inactive';

export class SessionManager {
    constructor() {
        this.init();
    }
    init() {}
}

export function calculateMetrics(data: number[]): number {
    return data.reduce((a, b) => a + b, 0);
}
)";

    const auto parsed = parser.parse_source(source, "TypeScript");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 1U);

    const auto interfaces = parsed.get_elements_by_kind(ElementKind::Interface);
    ASSERT_EQ(interfaces.size(), 2U);
    EXPECT_EQ(interfaces[0].name, "UserConfig");
    EXPECT_EQ(interfaces[1].name, "Status");

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "SessionManager");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 2U);
    EXPECT_EQ(methods[0].name, "constructor");
    EXPECT_EQ(methods[1].name, "init");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "calculateMetrics");
}

// =============================================================================
// 7. React, TSX, Next.js & Tailwind Extraction
// =============================================================================

TEST_F(SourceParserTest, TSXReactAndTailwindExtraction) {
    const string source = R"(
import React, { useState, useEffect } from 'react';
import { Button } from '@/components/ui/button';

export function UserCard({ user }) {
    const [active, setActive] = useState(false);
    useEffect(() => {
        console.log("mounted");
    }, []);

    return (
        <div className="flex items-center justify-between px-4 py-2">
            <span className="font-bold">{user.name}</span>
            <Button onClick={() => setActive(!active)}>Toggle</Button>
        </div>
    );
}

const ProfileHeader = () => (
    <header className="bg-white shadow">
        <h1>Profile</h1>
    </header>
);
)";

    const auto parsed = parser.parse_source(source, "TSX");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto components = parsed.get_elements_by_kind(ElementKind::Component);
    ASSERT_GE(components.size(), 2U);
    EXPECT_EQ(components[0].name, "UserCard");
    EXPECT_EQ(components[1].name, "ProfileHeader");

    const auto hooks = parsed.get_elements_by_kind(ElementKind::Hook);
    ASSERT_EQ(hooks.size(), 2U);
    EXPECT_EQ(hooks[0].name, "useState");
    EXPECT_EQ(hooks[1].name, "useEffect");

    const auto jsx_components = parsed.get_elements_by_kind(ElementKind::JSXComponent);
    ASSERT_GE(jsx_components.size(), 1U);
    EXPECT_EQ(jsx_components[0].name, "Button");

    const auto jsx_elements = parsed.get_elements_by_kind(ElementKind::JSXElement);
    ASSERT_GE(jsx_elements.size(), 3U);

    const auto attributes = parsed.get_elements_by_kind(ElementKind::Attribute);
    ASSERT_GE(attributes.size(), 3U);

    const auto utility_classes = parsed.get_elements_by_kind(ElementKind::UtilityClass);
    ASSERT_GE(utility_classes.size(), 7U);
    EXPECT_EQ(utility_classes[0].name, "flex");
    EXPECT_EQ(utility_classes[1].name, "items-center");
    EXPECT_EQ(utility_classes[2].name, "justify-between");
    EXPECT_EQ(utility_classes[3].name, "px-4");
    EXPECT_EQ(utility_classes[4].name, "py-2");
}

TEST_F(SourceParserTest, NextJSRouteAwareness) {
    const string source = "export default function Page() { return <div>Home</div>; }";
    const auto parsed_page = parser.parse_source(source, "TSX", "app/dashboard/page.tsx");

    EXPECT_TRUE(parsed_page.success);
    const auto routes_page = parsed_page.get_elements_by_kind(ElementKind::Route);
    ASSERT_EQ(routes_page.size(), 1U);
    EXPECT_EQ(routes_page[0].detail, "Next.js Page");

    const auto parsed_dynamic = parser.parse_source(source, "TSX", "app/users/[id]/page.tsx");
    const auto routes_dynamic = parsed_dynamic.get_elements_by_kind(ElementKind::Route);
    ASSERT_EQ(routes_dynamic.size(), 1U);
    EXPECT_EQ(routes_dynamic[0].detail, "Next.js Dynamic Page");

    const auto parsed_layout = parser.parse_source(source, "TSX", "app/layout.tsx");
    const auto routes_layout = parsed_layout.get_elements_by_kind(ElementKind::Route);
    ASSERT_EQ(routes_layout.size(), 1U);
    EXPECT_EQ(routes_layout[0].detail, "Next.js Layout");

    const auto parsed_api = parser.parse_source(source, "TypeScript", "pages/api/auth.ts");
    const auto routes_api = parsed_api.get_elements_by_kind(ElementKind::Route);
    ASSERT_EQ(routes_api.size(), 1U);
    EXPECT_EQ(routes_api[0].detail, "Next.js API Route");

    const auto parsed_app_api = parser.parse_source(source, "TypeScript", "app/api/users/route.ts");
    const auto routes_app_api = parsed_app_api.get_elements_by_kind(ElementKind::Route);
    ASSERT_EQ(routes_app_api.size(), 1U);
    EXPECT_EQ(routes_app_api[0].detail, "Next.js API Route");

    const auto parsed_loading = parser.parse_source(source, "TSX", "app/loading.tsx");
    const auto routes_loading = parsed_loading.get_elements_by_kind(ElementKind::Route);
    ASSERT_EQ(routes_loading.size(), 1U);
    EXPECT_EQ(routes_loading[0].detail, "Next.js Loading");

    const auto parsed_error = parser.parse_source(source, "TSX", "app/error.tsx");
    const auto routes_error = parsed_error.get_elements_by_kind(ElementKind::Route);
    ASSERT_EQ(routes_error.size(), 1U);
    EXPECT_EQ(routes_error[0].detail, "Next.js Error Boundary");
}

// =============================================================================
// 8. HTML & CSS Parsing
// =============================================================================

TEST_F(SourceParserTest, HTMLParsing) {
    const string source = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Dashboard</title>
</head>
<body>
    <header id="main-header" class="top-nav flex items-center">
        <a href="/home" class="text-blue-500">Home</a>
    </header>
</body>
</html>
)";

    const auto parsed = parser.parse_source(source, "HTML");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto elements = parsed.get_elements_by_kind(ElementKind::JSXElement);
    ASSERT_GE(elements.size(), 4U);

    const auto attributes = parsed.get_elements_by_kind(ElementKind::Attribute);
    ASSERT_GE(attributes.size(), 4U);

    const auto utility_classes = parsed.get_elements_by_kind(ElementKind::UtilityClass);
    ASSERT_GE(utility_classes.size(), 4U);
    EXPECT_EQ(utility_classes[0].name, "top-nav");
    EXPECT_EQ(utility_classes[1].name, "flex");
    EXPECT_EQ(utility_classes[2].name, "items-center");
    EXPECT_EQ(utility_classes[3].name, "text-blue-500");
}

TEST_F(SourceParserTest, CSSParsing) {
    const string source = R"(
@import url('fonts.css');

.card {
    display: flex;
    padding: 1rem;
    color: #333;
}

#header {
    background-color: white;
}
)";

    const auto parsed = parser.parse_source(source, "CSS");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 1U);

    const auto selectors = parsed.get_elements_by_kind(ElementKind::Selector);
    ASSERT_EQ(selectors.size(), 2U);
    EXPECT_EQ(selectors[0].name, ".card");
    EXPECT_EQ(selectors[1].name, "#header");

    const auto properties = parsed.get_elements_by_kind(ElementKind::Property);
    ASSERT_GE(properties.size(), 4U);
    EXPECT_EQ(properties[0].name, "display");
    EXPECT_EQ(properties[1].name, "padding");
}

// =============================================================================
// 9. Source Location & Byte Offset Precision
// =============================================================================

TEST_F(SourceParserTest, SourceLocationPrecision) {
    const string source = "int sum(int a, int b) {\n    return a + b;\n}\n";
    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);

    const auto& loc = functions[0].location;
    EXPECT_EQ(loc.start.line, 1U);
    EXPECT_EQ(loc.start.column, 1U);
    EXPECT_EQ(loc.start.byte_offset, 0U);
    EXPECT_EQ(loc.end.line, 3U);
    EXPECT_EQ(loc.end.column, 2U);
    EXPECT_EQ(loc.end.byte_offset, source.size() - 1);
}

// =============================================================================
// 10. Fault Tolerance & Malformed Source Handling
// =============================================================================

TEST_F(SourceParserTest, HandlesIncompleteOrInvalidSourceGracefully) {
    const string broken_cpp = "class User { void login(";
    const auto parsed_cpp = parser.parse_source(broken_cpp, "C++");
    EXPECT_TRUE(parsed_cpp.success);
    EXPECT_TRUE(parsed_cpp.has_syntax_errors);

    const string broken_tsx = "function App() { return (<div className=\"test\"";
    const auto parsed_tsx = parser.parse_source(broken_tsx, "TSX");
    EXPECT_TRUE(parsed_tsx.success);
    EXPECT_TRUE(parsed_tsx.has_syntax_errors);

    const string broken_py = "def calculate(x, :";
    const auto parsed_py = parser.parse_source(broken_py, "Python");
    EXPECT_TRUE(parsed_py.success);
    EXPECT_TRUE(parsed_py.has_syntax_errors);

    const string broken_rs = "struct Incomplete { x: ";
    const auto parsed_rs = parser.parse_source(broken_rs, "Rust");
    EXPECT_TRUE(parsed_rs.success);
    EXPECT_TRUE(parsed_rs.has_syntax_errors);
}

// =============================================================================
// 11. File IO & Error Handling
// =============================================================================

TEST_F(SourceParserTest, ParseFileFromDisk) {
    const path file_path = create_test_file("user.hpp", "class User { void init(); };");
    const auto parsed = parser.parse_file(file_path);

    EXPECT_TRUE(parsed.success);
    EXPECT_EQ(parsed.language, "C++");
    EXPECT_EQ(parsed.file_path, file_path);

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "User");
}

TEST_F(SourceParserTest, ParseFileThrowsOnInvalidPaths) {
    EXPECT_THROW((void)parser.parse_file(test_dir / "non_existent.cpp"), invalid_argument);
    EXPECT_THROW((void)parser.parse_file(test_dir), invalid_argument);
}

// =============================================================================
// 12. Language Detection & Unsupported Extension Rejection
// =============================================================================

TEST_F(SourceParserTest, UnsupportedLanguageRejection) {
    const string source = "some code";
    const auto parsed = parser.parse_source(source, "UnsupportedLang");

    EXPECT_FALSE(parsed.success);
    EXPECT_TRUE(parsed.elements.empty());
    EXPECT_EQ(parsed.language, "UnsupportedLang");
}

TEST_F(SourceParserTest, DetectsLanguageFromExtension) {
    EXPECT_EQ(SourceParser::detect_language("src/main.c"), "C");
    EXPECT_EQ(SourceParser::detect_language("include/utils.h"), "C");
    EXPECT_EQ(SourceParser::detect_language("src/main.cpp"), "C++");
    EXPECT_EQ(SourceParser::detect_language("include/header.hpp"), "C++");
    EXPECT_EQ(SourceParser::detect_language("src/file.cc"), "C++");
    EXPECT_EQ(SourceParser::detect_language("src/file.cxx"), "C++");
    EXPECT_EQ(SourceParser::detect_language("include/file.hh"), "C++");
    EXPECT_EQ(SourceParser::detect_language("include/file.hxx"), "C++");
    EXPECT_EQ(SourceParser::detect_language("script.py"), "Python");
    EXPECT_EQ(SourceParser::detect_language("App.java"), "Java");
    EXPECT_EQ(SourceParser::detect_language("main.go"), "Go");
    EXPECT_EQ(SourceParser::detect_language("lib.rs"), "Rust");
    EXPECT_EQ(SourceParser::detect_language("index.js"), "JavaScript");
    EXPECT_EQ(SourceParser::detect_language("App.jsx"), "JSX");
    EXPECT_EQ(SourceParser::detect_language("types.ts"), "TypeScript");
    EXPECT_EQ(SourceParser::detect_language("Component.tsx"), "TSX");
    EXPECT_EQ(SourceParser::detect_language("index.html"), "HTML");
    EXPECT_EQ(SourceParser::detect_language("styles.css"), "CSS");
    EXPECT_EQ(SourceParser::detect_language("README.md"), "Unknown");
    EXPECT_EQ(SourceParser::detect_language("config.json"), "Unknown");
    EXPECT_EQ(SourceParser::detect_language("data.bin"), "Unknown");
}

// =============================================================================
// 13. Parser Contract & CodeElement Value Semantics Verification
// =============================================================================

TEST_F(SourceParserTest, RepresentationContractVerification) {
    const string source = "class Service { void start() { run(); } };";
    const auto parsed = parser.parse_source(source, "C++", "src/service.cpp");

    EXPECT_TRUE(parsed.success);
    EXPECT_EQ(parsed.language, "C++");
    EXPECT_EQ(parsed.file_path, "src/service.cpp");

    // Verify copyable, comparable, standard-library-only CodeElements
    for (const auto& elem : parsed.elements) {
        EXPECT_NE(elem.kind, ElementKind::Unknown);
        EXPECT_FALSE(elem.name.empty());
        EXPECT_GE(elem.location.start.line, 1U);
        EXPECT_GE(elem.location.start.column, 1U);
        EXPECT_GE(elem.location.end.line, elem.location.start.line);

        // Verify value equality semantics
        CodeElement copy = elem;
        EXPECT_EQ(copy, elem);
    }
}

}  // namespace
