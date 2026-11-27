#include "desktop_entry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include <unistd.h>

namespace tetra {

namespace {

namespace fs = std::filesystem;

const char* env(const char* name) {
    return std::getenv(name); // NOLINT(concurrency-mt-unsafe)
}

std::vector<std::string> split(std::string_view text, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(sep, start);
        const auto part = text.substr(start, end == std::string_view::npos ? end : end - start);
        if (!part.empty()) {
            parts.emplace_back(part);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

// $XDG_DATA_DIRS, or its default.
std::vector<std::string> data_dirs() {
    const char* value = env("XDG_DATA_DIRS");
    return split(value != nullptr && *value != '\0' ? value : "/usr/local/share:/usr/share", ':');
}

// Directories holding .desktop files, highest priority first.
std::vector<fs::path> application_dirs() {
    std::vector<fs::path> dirs;
    if (const char* home = env("XDG_DATA_HOME"); home != nullptr && *home == '/') {
        dirs.emplace_back(fs::path(home) / "applications");
    } else if (const char* user = env("HOME"); user != nullptr) {
        dirs.emplace_back(fs::path(user) / ".local/share/applications");
    }
    for (const auto& dir : data_dirs()) {
        dirs.emplace_back(fs::path(dir) / "applications");
    }
    return dirs;
}

// The user's language, e.g. {"de_AT", "de"}, for localized keys.
std::vector<std::string> locale_names() {
    const char* value = nullptr;
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        value = env(var);
        if (value != nullptr && *value != '\0') {
            break;
        }
    }
    if (value == nullptr) {
        return {};
    }
    std::string locale = value;
    locale = locale.substr(0, locale.find_first_of(".@"));
    if (locale.empty() || locale == "C" || locale == "POSIX") {
        return {};
    }
    std::vector<std::string> names{locale};
    if (const auto underscore = locale.find('_'); underscore != std::string::npos) {
        names.push_back(locale.substr(0, underscore));
    }
    return names;
}

// Unescapes a desktop-file string value (\s \n \t \r \\).
std::string unescape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 == value.size()) {
            out += value[i];
            continue;
        }
        switch (value[++i]) {
        case 's':
            out += ' ';
            break;
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case '\\':
            out += '\\';
            break;
        default:
            out += '\\';
            out += value[i];
        }
    }
    return out;
}

bool in_path(const std::string& program) {
    if (program.find('/') != std::string::npos) {
        return access(program.c_str(), X_OK) == 0;
    }
    const char* path = env("PATH");
    for (const auto& dir : split(path != nullptr ? path : "/usr/bin:/bin", ':')) {
        if (access((fs::path(dir) / program).c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<DesktopEntry> parse(const fs::path& path, std::string id,
                                  const std::vector<std::string>& locales) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }
    // key -> value; localized keys are kept as "Name[de]".
    std::unordered_map<std::string, std::string> values;
    bool in_main_group = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            in_main_group = line == "[Desktop Entry]";
            continue;
        }
        if (!in_main_group) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        auto key = line.substr(0, eq);
        auto value = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ') {
            key.pop_back();
        }
        const auto first = value.find_first_not_of(' ');
        value = first == std::string::npos ? "" : value.substr(first);
        values.emplace(std::move(key), std::move(value));
    }

    auto get = [&](const std::string& key) -> std::string {
        auto it = values.find(key);
        return it == values.end() ? std::string{} : it->second;
    };
    auto localized = [&](const std::string& key) {
        for (const auto& locale : locales) {
            if (auto it = values.find(key + "[" + locale + "]"); it != values.end()) {
                return unescape(it->second);
            }
        }
        return unescape(get(key));
    };
    auto boolean = [&](const std::string& key) { return get(key) == "true"; };

    if (get("Type") != "Application" || boolean("Hidden")) {
        return std::nullopt;
    }
    if (const auto try_exec = unescape(get("TryExec")); !try_exec.empty() && !in_path(try_exec)) {
        return std::nullopt;
    }

    DesktopEntry entry;
    entry.id = std::move(id);
    entry.path = path;
    entry.name = localized("Name");
    entry.generic_name = localized("GenericName");
    entry.comment = localized("Comment");
    entry.icon = unescape(get("Icon"));
    entry.exec = unescape(get("Exec"));
    entry.wm_class = unescape(get("StartupWMClass"));
    entry.keywords = split(localized("Keywords"), ';');
    entry.categories = split(get("Categories"), ';');
    entry.desktop_names = split(get("DesktopNames"), ';');
    entry.terminal = boolean("Terminal");
    entry.autostart_enabled = get("X-GNOME-Autostart-enabled") != "false";

    const auto only = split(get("OnlyShowIn"), ';');
    const auto not_in = split(get("NotShowIn"), ';');
    const auto is_tetra = [](const std::string& d) { return iequals(d, "tetra"); };
    entry.no_display = boolean("NoDisplay");
    entry.not_for_tetra =
        (!only.empty() && std::ranges::none_of(only, is_tetra)) || std::ranges::any_of(not_in, is_tetra);
    entry.hidden_from_menus = entry.no_display || entry.not_for_tetra;
    if (entry.name.empty() || entry.exec.empty()) {
        return std::nullopt;
    }
    return entry;
}

} // namespace

bool iequals(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) ==
               std::tolower(static_cast<unsigned char>(y));
    });
}

std::vector<DesktopEntry> read_entries(const std::vector<fs::path>& dirs) {
    std::vector<DesktopEntry> entries;
    const auto locales = locale_names();
    std::unordered_set<std::string> seen; // IDs from higher-priority dirs win
    for (const auto& dir : dirs) {
        std::error_code ec;
        std::vector<fs::path> paths;
        for (auto it = fs::recursive_directory_iterator(dir, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->path().extension() == ".desktop") {
                paths.push_back(it->path());
            }
        }
        std::ranges::sort(paths); // the same order every time
        for (const auto& path : paths) {
            // ID: path below the directory with '/' replaced by '-'.
            std::string id = path.lexically_relative(dir).replace_extension().string();
            std::ranges::replace(id, '/', '-');
            if (!seen.insert(id).second) {
                continue;
            }
            if (auto entry = parse(path, id, locales)) {
                entries.push_back(std::move(*entry));
            }
        }
    }
    return entries;
}

std::vector<fs::path> autostart_dirs() {
    std::vector<fs::path> dirs;
    if (const char* home = env("XDG_CONFIG_HOME"); home != nullptr && *home == '/') {
        dirs.emplace_back(fs::path(home) / "autostart");
    } else if (const char* user = env("HOME"); user != nullptr) {
        dirs.emplace_back(fs::path(user) / ".config/autostart");
    }
    const char* config_dirs = env("XDG_CONFIG_DIRS");
    for (const auto& dir : split(config_dirs != nullptr && *config_dirs != '\0' ? config_dirs : "/etc/xdg", ':')) {
        dirs.emplace_back(fs::path(dir) / "autostart");
    }
    return dirs;
}

std::vector<DesktopEntry> autostart_entries(const std::vector<fs::path>& dirs) {
    auto entries = read_entries(dirs);
    std::erase_if(entries, [](const DesktopEntry& e) { return e.not_for_tetra || !e.autostart_enabled; });
    return entries;
}

std::vector<fs::path> session_dirs() {
    std::vector<fs::path> dirs;
    for (const auto& dir : data_dirs()) {
        dirs.emplace_back(fs::path(dir) / "wayland-sessions");
    }
    return dirs;
}

std::vector<DesktopEntry> session_entries(const std::vector<fs::path>& dirs) {
    auto entries = read_entries(dirs);
    std::erase_if(entries, [](const DesktopEntry& e) { return e.no_display; });
    std::ranges::sort(entries, [](const DesktopEntry& a, const DesktopEntry& b) {
        return std::ranges::lexicographical_compare(a.name, b.name, [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
        });
    });
    return entries;
}

void DesktopDatabase::load() {
    entries_ = read_entries(application_dirs());
}

std::vector<const DesktopEntry*> DesktopDatabase::visible() const {
    std::vector<const DesktopEntry*> result;
    for (const auto& entry : entries_) {
        if (!entry.hidden_from_menus) {
            result.push_back(&entry);
        }
    }
    std::ranges::sort(result, [](const DesktopEntry* a, const DesktopEntry* b) {
        return std::ranges::lexicographical_compare(a->name, b->name, [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) <
                   std::tolower(static_cast<unsigned char>(y));
        });
    });
    return result;
}

const DesktopEntry* DesktopDatabase::find(std::string_view id) const {
    auto it = std::ranges::find_if(entries_, [&](const DesktopEntry& e) { return e.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}

const DesktopEntry* DesktopDatabase::match_app_id(std::string_view app_id) const {
    if (app_id.empty()) {
        return nullptr;
    }
    if (const DesktopEntry* exact = find(app_id)) {
        return exact;
    }
    // Then: same ID ignoring case, StartupWMClass, last component of a
    // reverse-DNS ID ("nautilus" -> "org.gnome.Nautilus").
    const auto by = [&](auto&& predicate) -> const DesktopEntry* {
        auto it = std::ranges::find_if(entries_, predicate);
        return it == entries_.end() ? nullptr : &*it;
    };
    if (const auto* e = by([&](const DesktopEntry& d) { return iequals(d.id, app_id); })) {
        return e;
    }
    if (const auto* e = by([&](const DesktopEntry& d) { return iequals(d.wm_class, app_id); })) {
        return e;
    }
    return by([&](const DesktopEntry& d) {
        const auto dot = d.id.rfind('.');
        return dot != std::string::npos && iequals(std::string_view(d.id).substr(dot + 1), app_id);
    });
}

std::vector<std::string> exec_argv(const DesktopEntry& entry, const std::string& terminal) {
    // Tokenize per the Desktop Entry spec: whitespace-separated, double
    // quotes group, backslash escapes inside quotes; then expand field codes.
    std::vector<std::string> argv;
    std::string current;
    bool in_token = false;
    bool quoted = false;
    const std::string& exec = entry.exec;
    for (std::size_t i = 0; i < exec.size(); ++i) {
        const char c = exec[i];
        if (quoted) {
            if (c == '\\' && i + 1 < exec.size()) {
                current += exec[++i];
            } else if (c == '"') {
                quoted = false;
            } else {
                current += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            in_token = true;
        } else if (c == ' ' || c == '\t') {
            if (in_token) {
                argv.push_back(std::move(current));
                current.clear();
                in_token = false;
            }
        } else {
            current += c;
            in_token = true;
        }
    }
    if (in_token) {
        argv.push_back(std::move(current));
    }

    std::vector<std::string> expanded;
    for (const auto& arg : argv) {
        if (arg.size() == 2 && arg[0] == '%') {
            switch (arg[1]) {
            case 'i':
                if (!entry.icon.empty()) {
                    expanded.emplace_back("--icon");
                    expanded.push_back(entry.icon);
                }
                continue;
            case 'c':
                expanded.push_back(entry.name);
                continue;
            case 'k':
                expanded.push_back(entry.path.string());
                continue;
            case '%':
                expanded.emplace_back("%");
                continue;
            default:
                continue; // %f %F %u %U (no files to open) and deprecated codes
            }
        }
        std::string out;
        for (std::size_t i = 0; i < arg.size(); ++i) {
            if (arg[i] == '%' && i + 1 < arg.size()) {
                if (arg[++i] == '%') {
                    out += '%';
                }
                continue; // embedded field codes are dropped
            }
            out += arg[i];
        }
        expanded.push_back(std::move(out));
    }

    if (entry.terminal && !expanded.empty()) {
        std::vector<std::string> wrapped = split(terminal, ' ');
        if (wrapped.empty()) {
            wrapped.emplace_back("tetra-terminal");
        }
        wrapped.insert(wrapped.end(), expanded.begin(), expanded.end());
        return wrapped;
    }
    return expanded;
}

} // namespace tetra
