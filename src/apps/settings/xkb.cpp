#include "xkb.hpp"

#include <algorithm>
#include <fstream>
#include <string_view>

namespace tetra::settings {

namespace {

constexpr const char* kRules = "/usr/share/X11/xkb/rules/evdev.lst";

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

void Xkb::load() {
    layouts_.clear();
    variants_.clear();
    options_.clear();
    std::ifstream file(kRules);
    std::string line;
    std::vector<Entry>* section = nullptr;
    while (std::getline(file, line)) {
        if (line.starts_with("! ")) {
            const std::string_view name = trim(std::string_view(line).substr(2));
            section = name == "layout"    ? &layouts_
                      : name == "variant" ? &variants_
                      : name == "option"  ? &options_
                                          : nullptr;
            continue;
        }
        const std::string_view text = trim(line);
        if (section == nullptr || text.empty()) {
            continue;
        }
        // "  name   description"; variants: "  name   layout: description".
        const auto space = text.find_first_of(" \t");
        if (space == std::string_view::npos) {
            continue; // option group headers have no description we need
        }
        Entry entry{std::string(text.substr(0, space)), std::string(trim(text.substr(space))), {}};
        if (section == &variants_) {
            const auto colon = entry.description.find(": ");
            if (colon == std::string::npos) {
                continue;
            }
            entry.layout = entry.description.substr(0, colon);
            entry.description = entry.description.substr(colon + 2);
        }
        section->push_back(std::move(entry));
    }
    std::ranges::sort(layouts_, {}, &Entry::description);
}

std::vector<const Xkb::Entry*> Xkb::variants(const std::string& layout) const {
    std::vector<const Entry*> out;
    for (const auto& v : variants_) {
        if (v.layout == layout) {
            out.push_back(&v);
        }
    }
    return out;
}

const Xkb::Entry* Xkb::layout(const std::string& name) const {
    auto it = std::ranges::find(layouts_, name, &Entry::name);
    return it != layouts_.end() ? &*it : nullptr;
}

const Xkb::Entry* Xkb::option(const std::string& name) const {
    auto it = std::ranges::find(options_, name, &Entry::name);
    return it != options_.end() ? &*it : nullptr;
}

} // namespace tetra::settings
