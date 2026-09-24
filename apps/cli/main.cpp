#include "amoeba/engine.hpp"
#include "amoeba/parser/source_parser.hpp"
#include "amoeba/scanner/repository_scanner.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std;
using namespace std::filesystem;

void print_header() {
    cout << amoeba::get_name() << "\n";
    cout << amoeba::get_description() << "\n\n";
}

void print_usage(string_view program_name) {
    cout << "Usage:\n";
    cout << "  " << program_name
         << " index <repository-path>   Scan a repository for source files\n";
    cout << "  " << program_name
         << " parse <file-path>         Parse a source file and display structure\n";
}

void handle_index_command(const path& repo_path) {
    amoeba::scanner::RepositoryScanner scanner;

    try {
        const auto result = scanner.scan(repo_path);

        cout << "Repository:\n";
        cout << "  " << repo_path.string() << "\n\n";
        cout << "Scan complete.\n\n";
        cout << "Files discovered: " << result.total_files_discovered << "\n";
        cout << "Files included:   " << result.total_files_included() << "\n";
        cout << "Files ignored:    " << result.total_files_ignored << "\n";

        if (!result.files.empty()) {
            map<string_view, size_t> language_counts;
            for (const auto& file : result.files) {
                const auto lang =
                    amoeba::scanner::RepositoryScanner::get_language_name(file.extension);
                language_counts[lang]++;
            }

            vector<pair<string_view, size_t>> sorted_counts(language_counts.begin(),
                                                            language_counts.end());
            ranges::sort(sorted_counts, [](const auto& a, const auto& b) {
                if (a.second != b.second) {
                    return a.second > b.second;
                }
                return a.first < b.first;
            });

            cout << "\nLanguage Breakdown:\n";
            for (const auto& [lang, count] : sorted_counts) {
                cout << "  " << lang << ": " << count << "\n";
            }
        }
    } catch (const invalid_argument& ex) {
        cerr << "Error: " << ex.what() << "\n";
    } catch (const exception& ex) {
        cerr << "Unexpected error during scanning: " << ex.what() << "\n";
    }
}

void handle_parse_command(const path& file_path) {
    amoeba::parser::SourceParser parser;

    try {
        const auto parsed = parser.parse_file(file_path);

        cout << "File:\n";
        cout << "  " << file_path.string() << "\n\n";
        cout << "Language:\n";
        cout << "  " << parsed.language << "\n\n";
        cout << "Parsing:\n";
        cout << "  " << (parsed.success ? "success" : "failed");
        if (parsed.has_syntax_errors) {
            cout << " (with syntax errors)";
        }
        cout << "\n\n";

        if (parsed.elements.empty()) {
            cout << "No structural elements identified.\n";
            return;
        }

        cout << "Structural elements:\n";

        auto print_kind_group = [&](amoeba::parser::ElementKind kind, string_view label) {
            const auto group = parsed.get_elements_by_kind(kind);
            if (group.empty()) {
                return;
            }
            cout << "  " << label << ":\n";
            for (const auto& elem : group) {
                cout << "    " << elem.name;
                if (!elem.parent_context.empty()) {
                    cout << " (in " << elem.parent_context << ")";
                }
                if (!elem.detail.empty()) {
                    cout << " [" << elem.detail << "]";
                }
                cout << " (Line " << elem.location.start.line << ")\n";
            }
            cout << "\n";
        };

        print_kind_group(amoeba::parser::ElementKind::Route, "Route / Page");
        print_kind_group(amoeba::parser::ElementKind::Class, "Class");
        print_kind_group(amoeba::parser::ElementKind::Struct, "Struct");
        print_kind_group(amoeba::parser::ElementKind::Interface, "Interface / Type");
        print_kind_group(amoeba::parser::ElementKind::Component, "Component");
        print_kind_group(amoeba::parser::ElementKind::Function, "Function");
        print_kind_group(amoeba::parser::ElementKind::Method, "Method");
        print_kind_group(amoeba::parser::ElementKind::Hook, "Hook");
        print_kind_group(amoeba::parser::ElementKind::Include, "Include / Import");
        print_kind_group(amoeba::parser::ElementKind::Call, "Call");
        print_kind_group(amoeba::parser::ElementKind::JSXComponent, "JSX Component");
        print_kind_group(amoeba::parser::ElementKind::JSXElement, "JSX / HTML Element");
        print_kind_group(amoeba::parser::ElementKind::Attribute, "Attribute / Prop");
        print_kind_group(amoeba::parser::ElementKind::UtilityClass, "Utility Class");
        print_kind_group(amoeba::parser::ElementKind::Selector, "CSS Selector");
        print_kind_group(amoeba::parser::ElementKind::Property, "CSS Property");

    } catch (const invalid_argument& ex) {
        cerr << "Error: " << ex.what() << "\n";
    } catch (const exception& ex) {
        cerr << "Unexpected error during parsing: " << ex.what() << "\n";
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        print_header();

        if (argc < 2) {
            print_usage(argc > 0 ? argv[0] : "amoeba");
            return 0;
        }

        const string_view command = argv[1];

        if (command == "help" || command == "--help" || command == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        if (command == "index") {
            if (argc < 3) {
                cerr << "Error: Repository path is required.\n\n";
                print_usage(argv[0]);
                return 1;
            }

            const path repo_path(argv[2]);
            handle_index_command(repo_path);
            return 0;
        }

        if (command == "parse" || command == "inspect") {
            if (argc < 3) {
                cerr << "Error: File path is required.\n\n";
                print_usage(argv[0]);
                return 1;
            }

            const path file_path(argv[2]);
            handle_parse_command(file_path);
            return 0;
        }

        cerr << "Error: Unknown command '" << command << "'.\n\n";
        print_usage(argv[0]);
        return 1;
    } catch (const exception& ex) {
        cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        cerr << "Unknown fatal error occurred.\n";
        return 1;
    }
}
