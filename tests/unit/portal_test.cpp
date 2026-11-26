// Unit tests for tetra-portal's helpers: MIME globs, filters, URIs, places
// and directory listings (on temporary files), and the Settings values.

#include "appearance.hpp"
#include "files.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <source_location>
#include <string>

#include <unistd.h>

namespace {

int failures = 0;

void check(bool ok, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!ok) {
        ++failures;
        std::println(stderr, "FAIL portal_test.cpp line {}: {}", where.line(), what);
    }
}

void write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

using namespace tetra::portal;

void mime_and_filters(const fs::path& root) {
    write(root / "mime/globs2", "# comment\n"
                                "50:image/png:*.png\n"
                                "50:text/x-csrc:*.c\n"
                                "50:application/gzip:*.gz\n"
                                "50:application/x-compressed-tar:*.tar.gz\n"
                                "50:text/x-makefile:Makefile\n"
                                "10:text/x-readme:README*\n");
    write(root / "mime/subclasses", "text/x-csrc text/plain\napplication/x-compressed-tar application/gzip\n");
    MimeGlobs mime;
    mime.load(root / "mime");
    check(mime.type_of("A.PNG") == "image/png", "extension, any case");
    check(mime.type_of("x.tar.gz") == "application/x-compressed-tar", "longest extension first");
    check(mime.type_of("y.gz") == "application/gzip", "short extension");
    check(mime.type_of("Makefile") == "text/x-makefile", "literal name");
    check(mime.type_of("README.md").empty() || mime.type_of("README.md") == "text/x-readme", "glob");
    check(mime.type_of("unknown.zzz").empty(), "unknown");
    check(mime.is_a("text/x-csrc", "text/plain") && mime.is_a("image/png", "image/*"), "is_a");
    check(mime.is_a("application/x-compressed-tar", "application/gzip"), "subclass");
    check(!mime.is_a("image/png", "text/plain"), "not a");

    const Filter text{.name = "Text", .rules = {{0, "*.txt"}, {1, "text/plain"}}};
    check(matches(text, "NOTES.TXT", mime) && matches(text, "main.c", mime), "glob and MIME rules");
    check(!matches(text, "photo.png", mime), "no match");
    check(matches(Filter{.name = "All", .rules = {}}, "anything", mime), "empty filter");
}

void uris_and_bytes() {
    check(file_uri("/tmp/a b/\xc3\xbc#.txt") == "file:///tmp/a%20b/%C3%BC%23.txt", "escaped URI");
    check(file_uri("/x/y-z_1.~") == "file:///x/y-z_1.~", "unreserved kept");
    check(path_from_bytes({'/', 'a', '\0'}) == fs::path("/a"), "NUL-terminated path");
    check(!path_from_bytes({'\0'}).has_value() && !path_from_bytes({'/', '\0', 'x'}).has_value(), "bad paths");
}

void places_and_listing(const fs::path& root) {
    const fs::path home = root / "home";
    const fs::path config = home / ".config";
    write(config / "user-dirs.dirs", "# comment\n"
                                     "XDG_DOCUMENTS_DIR=\"$HOME/Docs\"\n"
                                     "XDG_DESKTOP_DIR=\"$HOME/\"\n"
                                     "XDG_MUSIC_DIR=\"/elsewhere/music\"\n");
    fs::create_directories(home / "Docs");
    fs::create_directories(home / "Downloads");
    fs::create_directories(home / "My Projects");
    write(config / "gtk-3.0/bookmarks",
          std::format("file://{}/My%20Projects Work\nsftp://server/x\nfile:///does/not/exist\n", home.string()));
    const auto dirs = user_dirs(home, config);
    check(dirs.at("DOCUMENTS") == home / "Docs" && dirs.at("MUSIC") == "/elsewhere/music", "user-dirs.dirs");
    const auto list = places(home, config);
    const auto has = [&](std::string_view label, const fs::path& path) {
        return std::ranges::any_of(list, [&](const Place& p) { return p.label == label && p.path == path; });
    };
    check(list.front().label == "Home" && list.back().path == "/", "Home first, Computer last");
    check(has("Documents", home / "Docs") && has("Downloads", home / "Downloads"), "existing user dirs");
    check(!std::ranges::any_of(list, [](const Place& p) { return p.label == "Desktop" || p.label == "Music"; }),
          "missing or home-equal dirs left out");
    check(has("Work", home / "My Projects"), "GTK bookmark with a label");

    const fs::path dir = root / "list";
    write(dir / "a.txt", "hello");
    write(dir / ".hidden", "");
    write(dir / "backup~", "");
    fs::create_directories(dir / "sub");
    auto entries = list_directory(dir);
    check(entries.has_value() && entries->size() == 4, "all entries");
    if (entries) {
        for (const auto& e : *entries) {
            if (e.name == "a.txt") {
                check(!e.directory && !e.hidden && e.size == 5, "file");
            } else if (e.name == "sub") {
                check(e.directory, "folder");
            } else {
                check(e.hidden, "dot file and backup are hidden");
            }
        }
    }
    check(!list_directory(root / "missing").has_value(), "missing folder is an error");
}

void formatting() {
    check(format_size(1) == "1 byte" && format_size(512) == "512 bytes", "bytes");
    check(format_size(4'200'000) == "4.2 MB" && format_size(123'000'000) == "123 MB", "units");
    const auto now = static_cast<int64_t>(std::time(nullptr));
    check(format_time(now, now).size() == 5, "today: time of day");
    check(format_time(now - (400LL * 24 * 3600), now).size() == 10, "long ago: full date");
}

void settings_values() {
    tetra::config::Theme theme;
    const Appearance dark = appearance(theme);
    check(dark.color_scheme == 1 && dark.gnome_color_scheme == "prefer-dark" && dark.gtk_theme == "Adwaita-dark",
          "dark");
    check(dark.font_name == "Inter 10.5", "14 px is 10.5 pt");
    check(dark.gnome_accent == "blue", "default accent is blue");
    theme.dark = false;
    theme.accent = *tetra::config::Color::parse("#e66100");
    theme.font_size = 16;
    const Appearance light = appearance(theme);
    check(light.color_scheme == 2 && light.gtk_theme == "Adwaita" && light.font_name == "Inter 12", "light");
    check(light.gnome_accent == "orange", "orange");
    check(nearest_gnome_accent(*tetra::config::Color::parse("#2ec27e")) == "green", "green");
    check(nearest_gnome_accent(*tetra::config::Color::parse("#c01c28")) == "red", "red, across 0 degrees");
    check(nearest_gnome_accent(*tetra::config::Color::parse("#77767b")) == "slate", "grey");
}

} // namespace

int portal_tests() {
    const auto root = std::filesystem::temp_directory_path() / std::format("tetra-portal-test-{}", ::getpid());
    std::filesystem::remove_all(root);
    mime_and_filters(root);
    uris_and_bytes();
    places_and_listing(root);
    formatting();
    settings_values();
    std::filesystem::remove_all(root);
    return failures;
}
