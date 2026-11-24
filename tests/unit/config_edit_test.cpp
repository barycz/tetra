// Unit tests for config::Document (comment-preserving config edits).

#include "config.hpp"
#include "config_edit.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <print>
#include <source_location>
#include <sstream>
#include <string>

#include <unistd.h>

namespace {

int failures = 0;

void check(bool ok, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!ok) {
        ++failures;
        std::println(stderr, "FAIL line {}: {}", where.line(), what);
    }
}

void check_text(const std::string& got, std::string_view want,
                std::source_location where = std::source_location::current()) {
    if (got != want) {
        ++failures;
        std::println(stderr, "FAIL line {}:\n--- got:\n{}--- want:\n{}---", where.line(), got, want);
    }
}

using tetra::config::Document;

void replace_keeps_comments() {
    Document doc(R"(// header
{
  // the theme
  "theme": {
    "dark": true, // always
    "accent": "#4c8dff"
  }
}
)");
    check(doc.set({"theme", "accent"}, "#ff0000").has_value(), "set accent");
    check(doc.set({"theme", "dark"}, false).has_value(), "set dark");
    check_text(doc.text(), R"(// header
{
  // the theme
  "theme": {
    "dark": false, // always
    "accent": "#ff0000"
  }
}
)");
}

void insert_after_trailing_comment() {
    Document doc("{\n  \"terminal\": \"foot\" // mine\n}\n");
    check(doc.set({"shell"}, "x").has_value(), "insert");
    check_text(doc.text(), "{\n  \"terminal\": \"foot\", // mine\n  \"shell\": \"x\"\n}\n");
}

void insert_into_inline_object() {
    Document doc("{\n  \"desktop\": { \"background\": \"#111111\" }\n}\n");
    check(doc.set({"desktop", "x"}, 1).has_value(), "insert inline");
    check_text(doc.text(), "{\n  \"desktop\": { \"background\": \"#111111\", \"x\": 1 }\n}\n");
}

void nested_creation_in_template() {
    Document doc{std::string(Document::kTemplate)};
    check(doc.set({"theme", "accent"}, "#00ff00").has_value(), "nested");
    check(doc.set({"taskbar", "pinned"}, std::vector<std::string>{"foot", "firefox-esr"}).has_value(),
          "array");
    check(doc.set({"idle", "lock_after"}, 0).has_value(), "int");
    // The template's comment lines, then the object.
    const std::string want = std::string(Document::kTemplate.substr(0, Document::kTemplate.size() - 4)) +
                             "{\n"
                             "  \"theme\": {\n"
                             "    \"accent\": \"#00ff00\"\n"
                             "  },\n"
                             "  \"taskbar\": {\n"
                             "    \"pinned\": [\"foot\", \"firefox-esr\"]\n"
                             "  },\n"
                             "  \"idle\": {\n"
                             "    \"lock_after\": 0\n"
                             "  }\n"
                             "}\n";
    check_text(doc.text(), want);
    check(tetra::config::parse_text(doc.text()).has_value(), "result parses");
}

void replace_multiline_array() {
    Document doc("{\n  \"autostart\": [\n    \"a\",\n    \"b\"\n  ]\n}\n");
    check(doc.set({"autostart"}, std::vector<std::string>{"c"}).has_value(), "set array");
    check_text(doc.text(), "{\n  \"autostart\": [\n    \"c\"\n  ]\n}\n");
}

void erase_members() {
    const std::string base = "{\n  \"a\": 1,\n  \"b\": 2, // bee\n  \"c\": 3\n}\n";
    Document middle(base);
    check(middle.erase({"b"}).has_value(), "erase middle");
    check_text(middle.text(), "{\n  \"a\": 1,\n  \"c\": 3\n}\n");

    Document last(base);
    check(last.erase({"c"}).has_value(), "erase last");
    check_text(last.text(), "{\n  \"a\": 1,\n  \"b\": 2 // bee\n}\n");

    Document only("{\n  \"a\": 1\n}\n");
    check(only.erase({"a"}).has_value(), "erase only");
    check_text(only.text(), "{\n}\n");

    Document inline_doc("{ \"a\": 1, \"b\": 2 }");
    check(inline_doc.erase({"a"}).has_value(), "erase inline first");
    check_text(inline_doc.text(), "{ \"b\": 2 }");

    Document missing(base);
    check(missing.erase({"zzz"}).has_value(), "erase missing");
    check_text(missing.text(), base);
}

void strings_and_escapes() {
    Document doc("{\n  \"terminal\": \"say \\\"hi\\\" /* not a comment */\"\n}\n");
    check(doc.set({"terminal"}, "foot -e \"htop\"").has_value(), "escaped");
    check_text(doc.text(), "{\n  \"terminal\": \"foot -e \\\"htop\\\"\"\n}\n");
}

void block_comments_are_skipped() {
    Document doc("{ /* a { tricky } comment */ \"shell\": \"\" }\n");
    check(doc.set({"shell"}, "tetra-shell").has_value(), "after block comment");
    check_text(doc.text(), "{ /* a { tricky } comment */ \"shell\": \"tetra-shell\" }\n");
}

void malformed_is_an_error() {
    Document doc("{\n  \"a\": \n}\n");
    check(!doc.set({"a"}, 1).has_value(), "malformed rejected");
    Document not_object("[1, 2]");
    check(!not_object.set({"a"}, 1).has_value(), "non-object root rejected");
}

void edit_file_round_trip() {
    const auto dir = std::filesystem::temp_directory_path() / ("tetra-unit-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    const auto path = dir / "config.json";
    {
        std::ofstream out(path);
        out << "// mine\n{\n  \"terminal\": \"foot\" // the best\n}\n";
    }
    auto ok = tetra::config::edit_file(path, [](Document& doc) { return doc.set({"theme", "dark"}, false); });
    check(ok.has_value(), "edit_file ok");
    std::stringstream text;
    text << std::ifstream(path).rdbuf();
    check_text(text.str(),
               "// mine\n{\n  \"terminal\": \"foot\", // the best\n  \"theme\": {\n    \"dark\": false\n  }\n}\n");

    // A value the loader rejects is never written.
    auto bad = tetra::config::edit_file(path, [](Document& doc) { return doc.set({"theme", "font_size"}, 500); });
    check(!bad.has_value(), "invalid config rejected");
    std::stringstream after;
    after << std::ifstream(path).rdbuf();
    check_text(after.str(), text.str());

    // A file that is already broken is left alone.
    const std::string broken_text = R"({
  "terminal": "foot",,
}
)";
    {
        std::ofstream out(path);
        out << broken_text;
    }
    auto broken = tetra::config::edit_file(path, [](Document& doc) { return doc.set({"theme", "dark"}, true); });
    check(!broken.has_value() && broken.error().find("fix it first") != std::string::npos, "broken file refused");
    std::stringstream untouched;
    untouched << std::ifstream(path).rdbuf();
    check_text(untouched.str(), broken_text);

    // A missing file starts from the template.
    const auto fresh = dir / "sub" / "new.json";
    check(tetra::config::edit_file(fresh, [](Document& doc) { return doc.set({"terminal"}, "xterm"); })
              .has_value(),
          "new file");
    check(tetra::config::load(fresh).has_value() && tetra::config::load(fresh)->terminal == "xterm",
          "new file loads");
    std::filesystem::remove_all(dir);
}

void color_hex() {
    check(tetra::config::Color::parse("#4c8dff")->hex() == "#4c8dff", "hex round trip");
    check(tetra::config::Color::parse("#00000080")->hex() == "#00000080", "hex alpha");
}

} // namespace

int main() {
    replace_keeps_comments();
    insert_after_trailing_comment();
    insert_into_inline_object();
    nested_creation_in_template();
    replace_multiline_array();
    erase_members();
    strings_and_escapes();
    block_comments_are_skipped();
    malformed_is_an_error();
    edit_file_round_trip();
    color_hex();
    if (failures == 0) {
        std::println("tetra-unit-tests: all passed");
    }
    return failures == 0 ? 0 : 1;
}
