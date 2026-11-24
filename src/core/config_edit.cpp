#include "config_edit.hpp"

#include "config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace tetra::config {

namespace {

using json = nlohmann::ordered_json;

constexpr std::size_t kInlineArrayWidth = 72; // longer scalar arrays go one per line

struct Malformed {
    std::size_t offset;
};

struct Member {
    std::string key;
    std::size_t start = 0;       // the key's opening quote
    std::size_t value_start = 0;
    std::size_t value_end = 0;   // one past the value
};

struct Object {
    std::size_t open = 0;  // '{'
    std::size_t close = 0; // '}'
    std::vector<Member> members;
};

struct Array {
    std::size_t open = 0;  // '['
    std::size_t close = 0; // ']'
    std::vector<std::pair<std::size_t, std::size_t>> elements; // [start, end)
};

// A JSON-with-comments reader that only records where things are.
class Scanner {
public:
    explicit Scanner(std::string_view text) : s_(text) {}

    // Past whitespace and comments.
    [[nodiscard]] std::size_t skip(std::size_t i) const {
        while (i < s_.size()) {
            const char c = s_[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i;
            } else if (c == '/' && i + 1 < s_.size() && s_[i + 1] == '/') {
                i = s_.find('\n', i);
                if (i == std::string_view::npos) {
                    return s_.size();
                }
            } else if (c == '/' && i + 1 < s_.size() && s_[i + 1] == '*') {
                const std::size_t end = s_.find("*/", i + 2);
                if (end == std::string_view::npos) {
                    throw Malformed{i};
                }
                i = end + 2;
            } else {
                break;
            }
        }
        return i;
    }

    // `i` at an opening quote; one past the closing quote.
    [[nodiscard]] std::size_t string_end(std::size_t i) const {
        for (++i; i < s_.size(); ++i) {
            if (s_[i] == '\\') {
                ++i;
            } else if (s_[i] == '"') {
                return i + 1;
            }
        }
        throw Malformed{i};
    }

    [[nodiscard]] std::size_t value_end(std::size_t i) const {
        i = skip(i);
        if (i >= s_.size()) {
            throw Malformed{i};
        }
        const char c = s_[i];
        if (c == '"') {
            return string_end(i);
        }
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++i;
            while (true) {
                i = skip(i);
                if (i >= s_.size()) {
                    throw Malformed{i};
                }
                if (s_[i] == close) {
                    return i + 1;
                }
                if (s_[i] == ',') {
                    ++i;
                    continue;
                }
                if (c == '{') {
                    if (s_[i] != '"') {
                        throw Malformed{i};
                    }
                    i = skip(string_end(i));
                    if (i >= s_.size() || s_[i] != ':') {
                        throw Malformed{i};
                    }
                    ++i;
                }
                i = value_end(i);
            }
        }
        std::size_t j = i;
        while (j < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[j])) != 0 ||
                                 s_[j] == '-' || s_[j] == '+' || s_[j] == '.')) {
            ++j;
        }
        if (j == i) {
            throw Malformed{i};
        }
        return j;
    }

    [[nodiscard]] Object object(std::size_t open) const {
        Object obj;
        obj.open = open;
        std::size_t i = open + 1;
        while (true) {
            i = skip(i);
            if (i >= s_.size()) {
                throw Malformed{i};
            }
            if (s_[i] == '}') {
                obj.close = i;
                return obj;
            }
            if (s_[i] == ',') {
                ++i;
                continue;
            }
            if (s_[i] != '"') {
                throw Malformed{i};
            }
            Member m;
            m.start = i;
            const std::size_t key_end = string_end(i);
            m.key = json::parse(s_.substr(i, key_end - i)).get<std::string>();
            i = skip(key_end);
            if (i >= s_.size() || s_[i] != ':') {
                throw Malformed{i};
            }
            m.value_start = skip(i + 1);
            m.value_end = value_end(m.value_start);
            i = m.value_end;
            obj.members.push_back(std::move(m));
        }
    }

    [[nodiscard]] Array array(std::size_t open) const {
        Array arr;
        arr.open = open;
        std::size_t i = open + 1;
        while (true) {
            i = skip(i);
            if (i >= s_.size()) {
                throw Malformed{i};
            }
            if (s_[i] == ']') {
                arr.close = i;
                return arr;
            }
            if (s_[i] == ',') {
                ++i;
                continue;
            }
            const std::size_t end = value_end(i);
            arr.elements.emplace_back(i, end);
            i = end;
        }
    }

    [[nodiscard]] Object root() const {
        const std::size_t i = skip(0);
        if (i >= s_.size() || s_[i] != '{') {
            throw Malformed{i};
        }
        return object(i);
    }

private:
    std::string_view s_;
};

std::size_t line_start(std::string_view s, std::size_t pos) {
    const std::size_t nl = s.rfind('\n', pos == 0 ? 0 : pos - 1);
    return pos == 0 || nl == std::string_view::npos ? 0 : nl + 1;
}

bool only_blank_before(std::string_view s, std::size_t pos) {
    for (std::size_t i = line_start(s, pos); i < pos; ++i) {
        if (s[i] != ' ' && s[i] != '\t') {
            return false;
        }
    }
    return true;
}

std::string indent_of(std::string_view s, std::size_t pos) {
    std::size_t i = line_start(s, pos);
    const std::size_t start = i;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return std::string(s.substr(start, i - start));
}

// After a value: if the rest of the line is blank or a // comment, the
// position of the line's end (its '\n' or the text's end); otherwise npos.
std::size_t rest_of_line_end(std::string_view s, std::size_t pos) {
    std::size_t i = pos;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) {
        ++i;
    }
    if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
        const std::size_t nl = s.find('\n', i);
        return nl == std::string_view::npos ? s.size() : nl;
    }
    if (i == s.size() || s[i] == '\n') {
        return i;
    }
    return std::string_view::npos;
}

bool has_newline(std::string_view s, std::size_t from, std::size_t to) {
    return s.substr(from, to - from).find('\n') != std::string_view::npos;
}

bool all_scalars(const json& value) {
    return std::ranges::none_of(value, [](const json& v) { return v.is_structured(); });
}

// One line where it reads well: scalars, short arrays of scalars.
bool prefers_one_line(const json& value) {
    if (!value.is_structured() || value.empty()) {
        return true;
    }
    return value.is_array() && all_scalars(value) && value.dump().size() <= kInlineArrayWidth;
}

std::string serialize(const json& value, bool multiline, const std::string& indent);

// Objects in arrays read best one per line, e.g. { "name": "DP-1", "scale": 2 }.
bool fits_inline(const json& value) {
    return value.is_object() && serialize(value, false, "").size() <= kInlineArrayWidth + 8;
}

std::string serialize(const json& value, bool multiline, const std::string& indent) {
    if (!value.is_structured() || value.empty()) {
        return value.dump();
    }
    const bool object = value.is_object();
    const std::string inner = indent + "  ";
    std::string out(1, object ? '{' : '[');
    bool first = true;
    for (auto it = value.begin(); it != value.end(); ++it) {
        out += first ? "" : ",";
        first = false;
        out += multiline ? "\n" + inner : (object || it != value.begin() ? " " : "");
        if (object) {
            out += json(it.key()).dump() + ": ";
        }
        const json& child = it.value();
        const bool inline_element = !object && fits_inline(child);
        out += serialize(child, multiline && !prefers_one_line(child) && !inline_element, inner);
    }
    if (multiline) {
        out += "\n" + indent;
    } else if (object) {
        out += " ";
    }
    out += object ? '}' : ']';
    return out;
}

// {"a", "b"} + v -> {"b": v} nested for the keys after the first.
json nest(Path path, std::size_t from, const json& value) {
    json out = value;
    for (std::size_t k = path.size(); k > from + 1; --k) {
        json wrapper = json::object();
        wrapper[std::string(*(path.begin() + k - 1))] = std::move(out);
        out = std::move(wrapper);
    }
    return out;
}

void replace_value(std::string& text, const Member& m, const json& value) {
    const bool was_multiline = has_newline(text, m.value_start, m.value_end);
    const bool multiline = value.is_structured() && !value.empty() &&
                           (was_multiline || !prefers_one_line(value));
    text.replace(m.value_start, m.value_end - m.value_start,
                 serialize(value, multiline, indent_of(text, m.start)));
}

void insert_member(std::string& text, const Object& obj, std::string_view key, const json& value) {
    const bool inline_object = !has_newline(text, obj.open, obj.close);
    auto member = [&](const std::string& indent) {
        const bool multiline = !inline_object && !prefers_one_line(value);
        return json(std::string(key)).dump() + ": " + serialize(value, multiline, indent);
    };
    if (obj.members.empty()) {
        if (inline_object) {
            text.replace(obj.open + 1, obj.close - obj.open - 1, " " + member("") + " ");
            return;
        }
        const std::string outer = indent_of(text, obj.open);
        const std::string indent = outer + "  ";
        if (only_blank_before(text, obj.close)) {
            text.insert(line_start(text, obj.close), indent + member(indent) + "\n");
        } else {
            text.insert(obj.close, "\n" + indent + member(indent) + "\n" + outer);
        }
        return;
    }
    const Member& last = obj.members.back();
    if (inline_object) {
        text.insert(last.value_end, ", " + member(""));
        return;
    }
    const std::string indent = only_blank_before(text, last.start)
                                   ? indent_of(text, last.start)
                                   : indent_of(text, obj.open) + "  ";
    const std::size_t eol = rest_of_line_end(text, last.value_end);
    if (eol != std::string::npos) {
        // Keep a trailing comment with the member it belongs to.
        text.insert(eol, "\n" + indent + member(indent));
        text.insert(last.value_end, ",");
    } else {
        text.insert(last.value_end, ",\n" + indent + member(indent));
    }
}

void insert_element(std::string& text, const Array& arr, const json& value) {
    const bool inline_array = !has_newline(text, arr.open, arr.close);
    auto element = [&](const std::string& indent) {
        return serialize(value, !inline_array && !prefers_one_line(value) && !fits_inline(value), indent);
    };
    if (arr.elements.empty()) {
        if (inline_array) {
            text.replace(arr.open + 1, arr.close - arr.open - 1, element(""));
            return;
        }
        const std::string outer = indent_of(text, arr.open);
        const std::string indent = outer + "  ";
        if (only_blank_before(text, arr.close)) {
            text.insert(line_start(text, arr.close), indent + element(indent) + "\n");
        } else {
            text.insert(arr.close, "\n" + indent + element(indent) + "\n" + outer);
        }
        return;
    }
    const auto [last_start, last_end] = arr.elements.back();
    if (inline_array) {
        text.insert(last_end, ", " + element(""));
        return;
    }
    const std::string indent = only_blank_before(text, last_start) ? indent_of(text, last_start)
                                                                    : indent_of(text, arr.open) + "  ";
    const std::size_t eol = rest_of_line_end(text, last_end);
    if (eol != std::string::npos) {
        text.insert(eol, "\n" + indent + element(indent));
        text.insert(last_end, ",");
    } else {
        text.insert(last_end, ",\n" + indent + element(indent));
    }
}

// The member at `path`, if every step exists and leads through objects.
std::optional<Member> find_member(const Scanner& scanner, const std::string& text, Path path) {
    Object obj = scanner.root();
    for (std::size_t k = 0; k < path.size(); ++k) {
        const std::string_view key = *(path.begin() + k);
        auto it = std::ranges::find_if(obj.members, [&](const Member& m) { return m.key == key; });
        if (it == obj.members.end()) {
            return std::nullopt;
        }
        if (k + 1 == path.size()) {
            return *it;
        }
        if (text[it->value_start] != '{') {
            return std::nullopt;
        }
        obj = scanner.object(it->value_start);
    }
    return std::nullopt;
}

void erase_member(std::string& text, const Object& obj, std::size_t index) {
    const Member& m = obj.members[index];
    const Scanner scanner(text);
    // The member's own region: whole lines when it has them to itself.
    std::size_t start = m.start;
    std::size_t end = m.value_end;
    const bool own_line = only_blank_before(text, m.start);
    auto whole_lines = [&](std::size_t after) {
        const std::size_t eol = rest_of_line_end(text, after);
        if (own_line && eol != std::string::npos) {
            start = line_start(text, m.start);
            end = eol < text.size() ? eol + 1 : eol;
        }
    };
    if (index + 1 < obj.members.size()) {
        // Followed by a comma: take it along.
        const std::size_t comma = scanner.skip(m.value_end);
        end = comma + 1;
        whole_lines(end);
        if (end == comma + 1) {
            while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
                ++end;
            }
        }
        text.erase(start, end - start);
        return;
    }
    whole_lines(m.value_end);
    if (obj.members.size() == 1) {
        text.erase(start, end - start);
        return;
    }
    // The last member: also drop the comma after the previous one.
    const std::size_t comma = scanner.skip(obj.members[index - 1].value_end);
    if (start == m.start && comma < text.size() && text[comma] == ',') {
        text.erase(comma, end - comma); // on the previous member's line: from its comma on
        return;
    }
    if (start == line_start(text, m.start) && end > 0 && text[end - 1] == '\n') {
        end -= 1; // keep the line break before the closing brace
        start = start > 0 ? start - 1 : start;
    }
    text.erase(start, end - start);
    if (comma < text.size() && text[comma] == ',') {
        text.erase(comma, 1);
    }
}

std::string error_at(const std::string& text, std::size_t offset) {
    const auto line = std::count(text.begin(),
                                 text.begin() + static_cast<std::ptrdiff_t>(std::min(offset, text.size())),
                                 '\n') + 1;
    return std::format("the file is not valid JSON near line {}", line);
}

} // namespace

std::expected<void, std::string> Document::set_value(Path path, const void* raw) {
    const json& value = *static_cast<const json*>(raw);
    if (path.size() == 0) {
        return std::unexpected("empty key path");
    }
    try {
        const Scanner scanner(text_);
        Object obj = scanner.root();
        for (std::size_t k = 0; k < path.size(); ++k) {
            const std::string_view key = *(path.begin() + k);
            const bool last = k + 1 == path.size();
            auto it = std::ranges::find_if(obj.members, [&](const Member& m) { return m.key == key; });
            if (it == obj.members.end()) {
                insert_member(text_, obj, key, nest(path, k, value));
                return {};
            }
            if (last) {
                replace_value(text_, *it, value);
                return {};
            }
            if (text_[it->value_start] == '{') {
                obj = scanner.object(it->value_start);
                continue;
            }
            // Not an object (yet): replace it with one holding the rest.
            replace_value(text_, *it, nest(path, k, value));
            return {};
        }
    } catch (const Malformed& e) {
        return std::unexpected(error_at(text_, e.offset));
    }
    return {};
}

std::expected<void, std::string> Document::set(Path path, bool value) {
    const json v = value;
    return set_value(path, &v);
}
std::expected<void, std::string> Document::set(Path path, int value) {
    const json v = value;
    return set_value(path, &v);
}
std::expected<void, std::string> Document::set(Path path, double value) {
    const json v = value;
    return set_value(path, &v);
}
std::expected<void, std::string> Document::set(Path path, std::string_view value) {
    const json v = std::string(value);
    return set_value(path, &v);
}
std::expected<void, std::string> Document::set(Path path, const std::vector<std::string>& value) {
    const json v = value;
    return set_value(path, &v);
}
std::expected<void, std::string> Document::set_json(Path path, std::string_view text) {
    json v;
    try {
        v = json::parse(text);
    } catch (const json::parse_error& e) {
        return std::unexpected(e.what());
    }
    return set_value(path, &v);
}

std::expected<void, std::string> Document::erase(Path path) {
    if (path.size() == 0) {
        return std::unexpected("empty key path");
    }
    try {
        const Scanner scanner(text_);
        Object obj = scanner.root();
        for (std::size_t k = 0; k < path.size(); ++k) {
            const std::string_view key = *(path.begin() + k);
            auto it = std::ranges::find_if(obj.members, [&](const Member& m) { return m.key == key; });
            if (it == obj.members.end()) {
                return {}; // nothing to remove
            }
            if (k + 1 == path.size()) {
                erase_member(text_, obj, static_cast<std::size_t>(it - obj.members.begin()));
                return {};
            }
            if (text_[it->value_start] != '{') {
                return {};
            }
            obj = scanner.object(it->value_start);
        }
    } catch (const Malformed& e) {
        return std::unexpected(error_at(text_, e.offset));
    }
    return {};
}

std::expected<void, std::string> Document::merge_element(Path array, std::string_view key,
                                                         std::string_view id, std::string_view members) {
    if (array.size() == 0) {
        return std::unexpected("empty key path");
    }
    json patch;
    try {
        patch = json::parse(members);
    } catch (const json::parse_error& e) {
        return std::unexpected(e.what());
    }
    if (!patch.is_object()) {
        return std::unexpected("members must be a JSON object");
    }
    try {
        // The element's object; looked up again after every edit.
        auto locate = [&]() -> std::optional<Object> {
            const Scanner scanner(text_);
            const auto member = find_member(scanner, text_, array);
            if (!member || text_[member->value_start] != '[') {
                return std::nullopt;
            }
            for (const auto& [start, end] : scanner.array(member->value_start).elements) {
                if (text_[start] != '{') {
                    continue;
                }
                Object obj = scanner.object(start);
                auto it = std::ranges::find_if(obj.members, [&](const Member& m) { return m.key == key; });
                if (it != obj.members.end() && text_[it->value_start] == '"' &&
                    json::parse(std::string_view(text_).substr(it->value_start, it->value_end - it->value_start)) ==
                        id) {
                    return obj;
                }
            }
            return std::nullopt;
        };

        if (!locate()) {
            json fresh = json::object();
            fresh[std::string(key)] = std::string(id);
            for (const auto& [k, v] : patch.items()) {
                if (!v.is_null() && k != key) {
                    fresh[k] = v;
                }
            }
            const Scanner scanner(text_);
            const auto member = find_member(scanner, text_, array);
            if (!member || text_[member->value_start] != '[') {
                const json list = json::array({fresh});
                return set_value(array, &list);
            }
            insert_element(text_, scanner.array(member->value_start), fresh);
            return {};
        }
        for (const auto& [k, v] : patch.items()) {
            if (k == key) {
                continue;
            }
            const Object obj = *locate();
            auto it = std::ranges::find_if(obj.members, [&](const Member& m) { return m.key == k; });
            if (v.is_null()) {
                if (it != obj.members.end()) {
                    erase_member(text_, obj, static_cast<std::size_t>(it - obj.members.begin()));
                }
            } else if (it != obj.members.end()) {
                replace_value(text_, *it, v);
            } else {
                insert_member(text_, obj, k, v);
            }
        }
    } catch (const Malformed& e) {
        return std::unexpected(error_at(text_, e.offset));
    } catch (const json::exception& e) {
        return std::unexpected(e.what());
    }
    return {};
}

std::expected<void, std::string> write_file(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temporary = path.string() + ".tetra-new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, ec);
            return std::unexpected(std::format("{} cannot be written", temporary.string()));
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return std::unexpected(std::format("{} cannot be replaced: {}", path.string(), ec.message()));
    }
    return {};
}

std::expected<void, std::string> edit_file(
    const std::filesystem::path& path,
    const std::function<std::expected<void, std::string>(Document&)>& edit) {
    std::string original;
    if (std::ifstream in(path); in) {
        std::stringstream buffer;
        buffer << in.rdbuf();
        original = buffer.str();
    } else if (std::filesystem::exists(path)) {
        return std::unexpected(std::format("{} cannot be read", path.string()));
    } else {
        original = std::string(Document::kTemplate);
    }

    // A file that does not load as it is stays exactly as the user left it.
    if (auto parsed = parse_text(original); !parsed) {
        return std::unexpected(std::format("{} has an error; fix it first ({})", path.filename().string(),
                                           parsed.error()));
    }
    Document document(original);
    if (auto result = edit(document); !result) {
        return result;
    }
    if (document.text() == original) {
        return {};
    }
    // Never write something the compositor would reject.
    if (auto parsed = parse_text(document.text()); !parsed) {
        return std::unexpected(parsed.error());
    }
    return write_file(path, document.text());
}

} // namespace tetra::config
