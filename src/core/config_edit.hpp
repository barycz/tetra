#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// Editing config.json without losing the user's comments and layout: the
// Settings app is an editor for that file, never a second source of truth.
namespace tetra::config {

// A key path from the root object, e.g. {"theme", "accent"}.
using Path = std::initializer_list<std::string_view>;

// The file's text with surgical edits: setting a key replaces only that
// value's text (or adds the member, matching the surrounding indentation);
// everything else, comments included, stays byte for byte.
class Document {
public:
    // What a new file starts as.
    static constexpr std::string_view kTemplate =
        "// tetra configuration: see docs/config.md. Settings edits this file\n"
        "// in place and keeps your comments.\n"
        "{\n"
        "}\n";

    explicit Document(std::string text) : text_(std::move(text)) {}

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    std::expected<void, std::string> set(Path path, bool value);
    std::expected<void, std::string> set(Path path, int value);
    std::expected<void, std::string> set(Path path, double value);
    std::expected<void, std::string> set(Path path, std::string_view value);
    std::expected<void, std::string> set(Path path, const char* value) {
        return set(path, std::string_view(value));
    }
    std::expected<void, std::string> set(Path path, const std::vector<std::string>& value);
    // `json` must be one JSON value (object, array, ...); it is reformatted.
    std::expected<void, std::string> set_json(Path path, std::string_view json);
    // Removes the member (and its comma); missing members are fine.
    std::expected<void, std::string> erase(Path path);
    // One object in an array of objects, picked by a string member: in
    // {"outputs"}, the object whose "name" is "DP-1". `members` (a JSON
    // object) is merged into it; null removes a member. A missing object is
    // appended as {key: id, members...}, a missing array created.
    std::expected<void, std::string> merge_element(Path array, std::string_view key, std::string_view id,
                                                   std::string_view members);

private:
    std::expected<void, std::string> set_value(Path path, const void* value); // nlohmann::json*

    std::string text_;
};

// Replaces the file's contents atomically (temporary file + rename).
std::expected<void, std::string> write_file(const std::filesystem::path& path, std::string_view text);

// Reads `path` (or the template if it does not exist), applies `edit`,
// checks that the result is still a valid configuration, and writes it
// atomically (temporary file + rename, which the live reload picks up).
// Nothing is written if the edit fails or changes nothing.
std::expected<void, std::string> edit_file(
    const std::filesystem::path& path,
    const std::function<std::expected<void, std::string>(Document&)>& edit);

} // namespace tetra::config
