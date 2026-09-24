#include "amoeba/engine.hpp"
#include "amoeba/scanner/repository_scanner.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_header() {
    std::cout << amoeba::get_name() << "\n";
    std::cout << amoeba::get_description() << "\n\n";
}

void print_usage(std::string_view program_name) {
    std::cout << "Usage:\n";
    std::cout << "  " << program_name << " index <repository-path>\n";
}

void handle_index_command(const std::filesystem::path& repo_path) {
    amoeba::scanner::RepositoryScanner scanner;

    try {
        const auto result = scanner.scan(repo_path);

        std::cout << "Repository:\n";
        std::cout << "  " << repo_path.string() << "\n\n";
        std::cout << "Scan complete.\n\n";
        std::cout << "Files discovered: " << result.total_files_discovered << "\n";
        std::cout << "Files included:   " << result.total_files_included() << "\n";
        std::cout << "Files ignored:    " << result.total_files_ignored << "\n";

        if (!result.files.empty()) {
            std::map<std::string_view, std::size_t> language_counts;
            for (const auto& file : result.files) {
                const auto lang =
                    amoeba::scanner::RepositoryScanner::get_language_name(file.extension);
                language_counts[lang]++;
            }

            std::vector<std::pair<std::string_view, std::size_t>> sorted_counts(
                language_counts.begin(), language_counts.end());
            std::ranges::sort(sorted_counts, [](const auto& a, const auto& b) {
                if (a.second != b.second) {
                    return a.second > b.second;
                }
                return a.first < b.first;
            });

            std::cout << "\nLanguage Breakdown:\n";
            for (const auto& [lang, count] : sorted_counts) {
                std::cout << "  " << lang << ": " << count << "\n";
            }
        }
    } catch (const std::invalid_argument& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Unexpected error during scanning: " << ex.what() << "\n";
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

        const std::string_view command = argv[1];

        if (command == "index") {
            if (argc < 3) {
                std::cerr << "Error: Repository path is required.\n\n";
                print_usage(argv[0]);
                return 1;
            }

            const std::filesystem::path repo_path(argv[2]);
            handle_index_command(repo_path);
            return 0;
        }

        std::cerr << "Error: Unknown command '" << command << "'.\n\n";
        print_usage(argv[0]);
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred.\n";
        return 1;
    }
}
