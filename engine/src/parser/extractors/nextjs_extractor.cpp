#include "extractors.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace amoeba::parser::internal {

using namespace std;
using namespace std::filesystem;

void check_nextjs_conventions(const path& file_path, vector<CodeElement>& out_elements) {
    if (file_path.empty()) {
        return;
    }

    string path_str = file_path.generic_string();
    const bool is_app_dir = path_str.find("app/") != string::npos || path_str.starts_with("app/");
    const bool is_pages_dir =
        path_str.find("pages/") != string::npos || path_str.starts_with("pages/");

    if (!is_app_dir && !is_pages_dir) {
        return;
    }

    string filename = file_path.filename().string();
    string route_type;

    if (is_app_dir) {
        if (filename.starts_with("page.")) {
            route_type =
                path_str.find('[') != string::npos ? "Next.js Dynamic Page" : "Next.js Page";
        } else if (filename.starts_with("layout.")) {
            route_type = "Next.js Layout";
        } else if (filename.starts_with("route.")) {
            route_type = "Next.js API Route";
        } else if (filename.starts_with("loading.")) {
            route_type = "Next.js Loading";
        } else if (filename.starts_with("error.")) {
            route_type = "Next.js Error Boundary";
        }
    } else if (is_pages_dir) {
        if (path_str.find("/api/") != string::npos || path_str.starts_with("pages/api/")) {
            route_type = "Next.js API Route";
        } else {
            route_type =
                path_str.find('[') != string::npos ? "Next.js Dynamic Page" : "Next.js Page";
        }
    }

    if (!route_type.empty()) {
        out_elements.insert(
            out_elements.begin(),
            CodeElement{
                .kind = ElementKind::Route,
                .name = path_str,
                .location = SourceRange{.start = {.line = 1, .column = 1, .byte_offset = 0},
                                        .end = {.line = 1, .column = 1, .byte_offset = 0}},
                .parent_context = "Next.js",
                .detail = route_type,
            });
    }
}

}  // namespace amoeba::parser::internal
