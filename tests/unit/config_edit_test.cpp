// Unit tests for config::Document (comment-preserving config edits).

#include "config.hpp"
#include "config_edit.hpp"
#include "window_memory.hpp"
#include "zones.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <print>
#include <source_location>
#include <sstream>
#include <string>
#include <vector>

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

void merge_elements() {
    const std::string base = R"({
  "outputs": [
    { "name": "DP-1", "scale": 1.25, "mode": "2560x1440" }, // the big one
    { "name": "*", "scale": 1 }
  ]
}
)";
    Document doc(base);
    check(doc.merge_element({"outputs"}, "name", "DP-1", R"({"scale": 1.5, "mode": null, "position": [0, 0]})")
              .has_value(),
          "merge existing");
    check(doc.merge_element({"outputs"}, "name", "HDMI-A-1", R"({"enabled": false})").has_value(), "append");
    check_text(doc.text(), R"({
  "outputs": [
    { "name": "DP-1", "scale": 1.5, "position": [0, 0] }, // the big one
    { "name": "*", "scale": 1 },
    { "name": "HDMI-A-1", "enabled": false }
  ]
}
)");

    Document fresh("{\n  \"terminal\": \"foot\"\n}\n");
    check(fresh.merge_element({"outputs"}, "name", "Virtual-1", R"({"scale": 2, "mode": null})").has_value(),
          "create array");
    check_text(fresh.text(), R"({
  "terminal": "foot",
  "outputs": [
    { "name": "Virtual-1", "scale": 2 }
  ]
}
)");
    check(tetra::config::parse_text(fresh.text()).has_value(), "result parses");

    Document inline_doc(R"({ "outputs": [] })");
    check(inline_doc.merge_element({"outputs"}, "name", "A", R"({"scale": 2})").has_value(), "inline empty");
    check(inline_doc.merge_element({"outputs"}, "name", "B", R"({})").has_value(), "inline append");
    check_text(inline_doc.text(), R"({ "outputs": [{ "name": "A", "scale": 2 }, { "name": "B" }] })");
}

void color_hex() {
    check(tetra::config::Color::parse("#4c8dff")->hex() == "#4c8dff", "hex round trip");
    check(tetra::config::Color::parse("#00000080")->hex() == "#00000080", "hex alpha");
}

} // namespace

void mouse_and_touchpad() {
    using tetra::config::Touchpad;
    auto config = tetra::config::parse_text("{}");
    check(config.has_value(), "empty config loads");
    check(config->mouse.speed == 0 && config->mouse.acceleration && !config->mouse.natural_scroll &&
              !config->mouse.left_handed,
          "mouse defaults");
    check(config->touchpad.tap_to_click && config->touchpad.natural_scroll &&
              config->touchpad.disable_while_typing && config->touchpad.scroll == Touchpad::Scroll::TwoFinger &&
              !config->touchpad.click,
          "touchpad defaults");

    config = tetra::config::parse_text(R"({
      "mouse": { "speed": -0.5, "acceleration": false, "left_handed": true },
      "touchpad": { "speed": 1, "tap_to_click": false, "scroll": "edge", "click": "fingers" }
    })");
    check(config.has_value(), "mouse and touchpad load");
    check(config->mouse.speed == -0.5 && !config->mouse.acceleration && config->mouse.left_handed, "mouse values");
    check(config->touchpad.speed == 1 && !config->touchpad.tap_to_click &&
              config->touchpad.scroll == Touchpad::Scroll::Edge && config->touchpad.click == Touchpad::Click::Fingers,
          "touchpad values");

    auto error = [](std::string_view text) {
        auto c = tetra::config::parse_text(text);
        return c ? std::string() : c.error();
    };
    check(error(R"({"mouse": {"speed": 2}})").contains("'mouse.speed' must be between -1"), "speed range");
    check(error(R"({"touchpad": {"scroll": "three_finger"}})").contains("'touchpad.scroll' must be"),
          "scroll values");
    check(error(R"({"touchpad": {"click": "buttons"}})").contains("'touchpad.click' must be"), "click values");
    std::vector<std::string> warnings;
    config = tetra::config::parse_text(R"({"mouse": {"sped": 1}})", &warnings);
    check(config.has_value() && warnings.size() == 1 && warnings[0].contains("mouse.sped"), "unknown key warns");
}

void window_memory() {
    using tetra::windows::Memory;
    using tetra::windows::Place;
    const std::vector<std::string> laptop{"eDP-1"};
    const std::vector<std::string> docked{"eDP-1", "Dell U2720Q 1ABC"};
    const Place left{.x = 10, .y = 20, .width = 800, .height = 600, .maximized = false};
    const Place right{.x = 900, .y = 40, .width = 700, .height = 500, .maximized = true};

    Memory memory;
    // Slots: lowest free one, per app.
    check(memory.claim("foot") == 0 && memory.claim("foot") == 1 && memory.claim("firefox") == 0, "claim slots");
    memory.release("foot", 0);
    check(memory.claim("foot") == 0, "released slot is reused");
    check(!memory.display("foot", 0, laptop) && !memory.place("foot", 0, "eDP-1"), "nothing remembered yet");

    // On the laptop, then docked on the external monitor.
    memory.remember("foot", 0, laptop, "eDP-1", left, 100);
    memory.remember("foot", 0, docked, "Dell U2720Q 1ABC", right, 200);
    check(memory.display("foot", 0, docked) == "Dell U2720Q 1ABC", "docked: external");
    check(memory.display("foot", 0, laptop) == "eDP-1", "undocked: laptop");
    check(memory.place("foot", 0, "eDP-1") == left && memory.place("foot", 0, "Dell U2720Q 1ABC") == right,
          "a place per display");
    check(memory.last_place("foot", 0) == right, "last place");
    // A new setup with the last display connected uses it.
    const std::vector<std::string> other{"Dell U2720Q 1ABC", "HDMI-A-1"};
    check(memory.display("foot", 0, other) == "Dell U2720Q 1ABC", "last display when setup is new");
    check(!memory.display("foot", 0, std::vector<std::string>{"HDMI-A-1"}), "unknown displays only");
    check(tetra::windows::setup_key(std::vector<std::string>{"b", "a"}) == "a + b", "setup key sorted");

    // Round trip through the file; held slots are not saved.
    const std::string text = memory.dump();
    Memory loaded = Memory::parse(text);
    check(loaded.dump() == text, "dump/parse round trip");
    check(loaded.place("foot", 0, "eDP-1") == left && loaded.display("foot", 0, docked) == "Dell U2720Q 1ABC",
          "loaded places");
    check(loaded.claim("foot") == 0, "slots start free after loading");

    // Bad input: nothing, or the good parts only.
    std::string error;
    check(Memory::parse("not json", &error).empty() && !error.empty(), "malformed file");
    Memory partial = Memory::parse(R"({"apps": {"foot": {"windows": [
        {"places": {"A": {"x": 1, "y": 2, "width": 0, "height": 5}, "B": {"x": 1, "y": 2, "width": 3, "height": 4}}},
        "junk"]}, "bad": 7}})");
    check(!partial.place("foot", 0, "A") && partial.place("foot", 0, "B") == Place{1, 2, 3, 4, false},
          "invalid places skipped");

    // Least recently used apps are forgotten past the limit.
    Memory many;
    for (std::size_t i = 0; i <= Memory::kMaxApps; ++i) {
        many.remember("app" + std::to_string(i), 0, laptop, "eDP-1", left, static_cast<std::int64_t>(1000 + i));
    }
    check(!many.place("app0", 0, "eDP-1") && many.place("app1", 0, "eDP-1"), "oldest app forgotten");
    // Slots past the limit are not remembered.
    Memory slots;
    slots.remember("foot", Memory::kMaxSlots, laptop, "eDP-1", left, 1);
    check(slots.empty(), "slot beyond limit ignored");
}

void snap_zones() {
    using tetra::zones::Box;
    auto config = tetra::config::parse_text("{}");
    check(config.has_value() && config->windows.zones == "columns-2" && config->windows.zone_gap == 8,
          "zone defaults");

    // Two columns of 1000x600 with an 8 px gap between and around them.
    const auto two = tetra::zones::find(*config, "columns-2");
    check(two.has_value(), "built-in layout");
    const auto boxes = tetra::zones::place(*two, {.x = 0, .y = 0, .width = 1000, .height = 600}, 8);
    check(boxes.size() == 2 && boxes[0] == Box{8, 8, 488, 584} && boxes[1] == Box{504, 8, 488, 584},
          "columns with gaps");

    // Picking: inside a zone, in the gap between (both), outside (none).
    check(tetra::zones::pick(boxes, 200, 300, 16) == std::vector<std::size_t>{0}, "pick inside");
    check(tetra::zones::pick(boxes, 500, 300, 16) == std::vector<std::size_t>{0, 1}, "pick at the edge");
    check(tetra::zones::pick(boxes, 490, 300, 16) == std::vector<std::size_t>{0, 1}, "pick near the edge");
    check(tetra::zones::pick(boxes, 400, 300, 16) == std::vector<std::size_t>{0}, "pick away from the edge");
    check(tetra::zones::pick(boxes, 2000, 300, 16).empty(), "pick far outside");
    const std::vector<std::size_t> both{0, 1};
    check(tetra::zones::span(boxes, both) == Box{8, 8, 984, 584}, "span");

    // Per display, else windows.zones; custom layouts.
    config = tetra::config::parse_text(R"({
      "windows": { "zones": "grid-4", "zone_layouts": { "coding": [[0, 0, 60, 100], [60, 0, 40, 100]] } },
      "outputs": [ { "name": "DP-1", "zones": "coding" } ]
    })");
    check(config.has_value(), "zones config loads");
    check(tetra::zones::layout_name(*config, "DP-1") == "coding" &&
              tetra::zones::layout_name(*config, "HDMI-A-1") == "grid-4",
          "layout per display");
    const auto coding = tetra::zones::find(*config, "coding");
    check(coding && coding->zones.size() == 2 && coding->zones[1].x == 0.6, "custom layout");
    check(tetra::zones::all(*config).size() == tetra::zones::builtin().size() + 1, "all layouts");

    auto error = [](std::string_view text) {
        auto c = tetra::config::parse_text(text);
        return c ? std::string() : c.error();
    };
    check(error(R"({"windows": {"zones": "nope"}})").contains("no layout called \"nope\""), "unknown layout");
    check(error(R"({"outputs": [{"name": "DP-1", "zones": "nope"}]})").contains("no layout called"),
          "unknown output layout");
    check(error(R"({"windows": {"zone_layouts": {"grid-4": [[0, 0, 50, 50]]}}})").contains("built-in"),
          "custom name clashes");
    check(error(R"({"windows": {"zone_layouts": {"x": [[60, 0, 50, 100]]}}})").contains("within 0..100"),
          "zone outside");
    check(error(R"({"windows": {"zone_layouts": {"x": [[0, 0, 50]]}}})").contains("[x, y, width, height]"),
          "zone shape");
    check(error(R"({"windows": {"zone_gap": 100}})").contains("zone_gap"), "gap range");
}

int monitor_tests(); // monitor_test.cpp
int portal_tests();  // portal_test.cpp
int greeter_tests(); // greeter_test.cpp

int main() {
    failures += monitor_tests();
    failures += portal_tests();
    failures += greeter_tests();
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
    merge_elements();
    color_hex();
    mouse_and_touchpad();
    window_memory();
    snap_zones();
    if (failures == 0) {
        std::println("tetra-unit-tests: all passed");
    }
    return failures == 0 ? 0 : 1;
}
