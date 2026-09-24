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
    EXPECT_EQ(includes[0].location.start.line, 2U);
    EXPECT_EQ(includes[1].name, "<iostream>");
    EXPECT_EQ(includes[1].location.start.line, 3U);
}

TEST_F(SourceParserTest, FunctionCallExtraction) {
    const string source = R"(
void process() {
    authenticate(user);
    logger.log("done");
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 2U);
    EXPECT_EQ(calls[0].name, "authenticate");
    EXPECT_EQ(calls[0].location.start.line, 3U);
    EXPECT_EQ(calls[1].name, "log");
}

TEST_F(SourceParserTest, NestedStructureTraversal) {
    const string source = R"(
void processOrders(bool check) {
    if (check) {
        for (int i = 0; i < 10; ++i) {
            executeOrder(i);
        }
    }
}
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "processOrders");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_EQ(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "executeOrder");
}

TEST_F(SourceParserTest, StructParsing) {
    const string source = R"(
struct Point {
    int x;
    int y;
};
)";

    const auto parsed = parser.parse_source(source, "C++");

    EXPECT_TRUE(parsed.success);
    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "Point");
    EXPECT_EQ(structs[0].location.start.line, 2U);
}

TEST_F(SourceParserTest, HandlesIncompleteOrInvalidSourceGracefully) {
    const string incomplete_source = R"(
class User {
public:
    void login(
};
)";

    const auto parsed = parser.parse_source(incomplete_source, "C++");

    EXPECT_TRUE(parsed.success);
    EXPECT_TRUE(parsed.has_syntax_errors);

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "User");
}

TEST_F(SourceParserTest, ParseFileFromDisk) {
    const string source = R"(
#include "header.h"
int run() {
    return 42;
}
)";
    const path file_path = create_test_file("sample.cpp", source);

    const auto parsed = parser.parse_file(file_path);

    EXPECT_TRUE(parsed.success);
    EXPECT_EQ(parsed.language, "C++");
    EXPECT_EQ(parsed.file_path, file_path);

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "run");
}

TEST_F(SourceParserTest, ParseFileThrowsOnInvalidPaths) {
    const path non_existent = test_dir / "missing.cpp";
    EXPECT_THROW((void)parser.parse_file(non_existent), invalid_argument);
    EXPECT_THROW((void)parser.parse_file(test_dir), invalid_argument);
}

TEST_F(SourceParserTest, CLanguageParsing) {
    const string source = R"(
#include <stdio.h>

struct Buffer {
    char* data;
    int size;
};

int compute_sum(int a, int b) {
    return a + b;
}

int main(void) {
    printf("hello\n");
    return compute_sum(1, 2);
}
)";

    const auto parsed = parser.parse_source(source, "C");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);
    EXPECT_EQ(parsed.language, "C");

    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "Buffer");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 2U);
    EXPECT_EQ(functions[0].name, "compute_sum");
    EXPECT_EQ(functions[1].name, "main");

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 2U);
    EXPECT_EQ(calls[0].name, "printf");
    EXPECT_EQ(calls[1].name, "compute_sum");
}

TEST_F(SourceParserTest, PythonParsing) {
    const string source = R"(
import os
from math import sqrt

class Calculator:
    def add(self, a, b):
        return a + b

def compute(val):
    calc = Calculator()
    res = calc.add(val, 10)
    print(res)
)";

    const auto parsed = parser.parse_source(source, "Python");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);
    EXPECT_EQ(parsed.language, "Python");

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "Calculator");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_EQ(methods.size(), 1U);
    EXPECT_EQ(methods[0].name, "add");
    EXPECT_EQ(methods[0].parent_context, "Calculator");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "compute");

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 2U);

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 2U);
}

TEST_F(SourceParserTest, JavaParsing) {
    const string source = R"(
package com.example;

import java.util.List;

public interface Service {
    void execute();
}

public class UserService implements Service {
    public void execute() {
        validate();
    }
}
)";

    const auto parsed = parser.parse_source(source, "Java");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto interfaces = parsed.get_elements_by_kind(ElementKind::Interface);
    ASSERT_EQ(interfaces.size(), 1U);
    EXPECT_EQ(interfaces[0].name, "Service");

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "UserService");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 2U);

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 1U);

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "validate");
}

TEST_F(SourceParserTest, GoParsing) {
    const string source = R"(
package main

import "fmt"

type Reader interface {
    Read(p []byte) (n int, err error)
}

type Config struct {
    Port int
}

func (c *Config) GetPort() int {
    return c.Port
}

func Start() {
    fmt.Println("started")
}
)";

    const auto parsed = parser.parse_source(source, "Go");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto interfaces = parsed.get_elements_by_kind(ElementKind::Interface);
    ASSERT_EQ(interfaces.size(), 1U);
    EXPECT_EQ(interfaces[0].name, "Reader");

    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "Config");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_EQ(methods.size(), 1U);
    EXPECT_EQ(methods[0].name, "GetPort");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "Start");

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 1U);

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "Println");
}

TEST_F(SourceParserTest, RustParsing) {
    const string source = R"(
use std::collections::HashMap;

pub trait Greeter {
    fn greet(&self);
}

pub struct User {
    pub name: String,
}

impl User {
    pub fn new(name: String) -> Self {
        User { name }
    }
}

fn main() {
    println!("hello");
}
)";

    const auto parsed = parser.parse_source(source, "Rust");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto traits = parsed.get_elements_by_kind(ElementKind::Interface);
    ASSERT_EQ(traits.size(), 1U);
    EXPECT_EQ(traits[0].name, "Greeter");

    const auto structs = parsed.get_elements_by_kind(ElementKind::Struct);
    ASSERT_EQ(structs.size(), 1U);
    EXPECT_EQ(structs[0].name, "User");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_GE(methods.size(), 2U);

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "main");

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 1U);
}

TEST_F(SourceParserTest, JavaScriptAndTypeScriptParsing) {
    const string source = R"(
import { auth } from './auth';

interface UserProps {
    id: string;
}

type UserID = string;

class Account {
    getBalance() {
        return 100;
    }
}

function fetchUser(props) {
    auth.login();
}
)";

    const auto parsed = parser.parse_source(source, "TypeScript");

    EXPECT_TRUE(parsed.success);
    EXPECT_FALSE(parsed.has_syntax_errors);

    const auto interfaces = parsed.get_elements_by_kind(ElementKind::Interface);
    ASSERT_GE(interfaces.size(), 2U);
    EXPECT_EQ(interfaces[0].name, "UserProps");
    EXPECT_EQ(interfaces[1].name, "UserID");

    const auto classes = parsed.get_elements_by_kind(ElementKind::Class);
    ASSERT_EQ(classes.size(), 1U);
    EXPECT_EQ(classes[0].name, "Account");

    const auto methods = parsed.get_elements_by_kind(ElementKind::Method);
    ASSERT_EQ(methods.size(), 1U);
    EXPECT_EQ(methods[0].name, "getBalance");

    const auto functions = parsed.get_elements_by_kind(ElementKind::Function);
    ASSERT_EQ(functions.size(), 1U);
    EXPECT_EQ(functions[0].name, "fetchUser");

    const auto includes = parsed.get_elements_by_kind(ElementKind::Include);
    ASSERT_EQ(includes.size(), 1U);

    const auto calls = parsed.get_elements_by_kind(ElementKind::Call);
    ASSERT_GE(calls.size(), 1U);
    EXPECT_EQ(calls[0].name, "login");
}

TEST_F(SourceParserTest, TSXReactAndTailwindExtraction) {
    const string source = R"(
import React, { useState, useEffect } from 'react';

export function UserCard({ user }) {
    const [count, setCount] = useState(0);

    useEffect(() => {
        setup();
    }, []);

    return (
        <div className="flex items-center justify-between px-4 py-2">
            <Button onClick={handleClick}>
                <span>{user.name}</span>
            </Button>
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

TEST_F(SourceParserTest, HTMLParsing) {
    const string source = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Dashboard</title>
</head>
<body>
    <header id="main-header" class="top-nav flex">
        <a href="/home">Home</a>
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
    ASSERT_GE(attributes.size(), 3U);

    const auto utility_classes = parsed.get_elements_by_kind(ElementKind::UtilityClass);
    ASSERT_GE(utility_classes.size(), 2U);
    EXPECT_EQ(utility_classes[0].name, "top-nav");
    EXPECT_EQ(utility_classes[1].name, "flex");
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
}

TEST_F(SourceParserTest, UnsupportedLanguageRejection) {
    const string source = "some code";
    const auto parsed = parser.parse_source(source, "Brainfuck");

    EXPECT_FALSE(parsed.success);
    EXPECT_TRUE(parsed.elements.empty());
    EXPECT_EQ(parsed.language, "Brainfuck");
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
}

}  // namespace
