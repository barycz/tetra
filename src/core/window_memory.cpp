#include "window_memory.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <ranges>

namespace tetra::windows {

using json = nlohmann::json;

namespace {

constexpr int kVersion = 1;

json place_json(const Place& p) {
    return json{{"x", p.x}, {"y", p.y}, {"width", p.width}, {"height", p.height}, {"maximized", p.maximized}};
}

// Anything malformed is skipped rather than failing the whole file.
std::optional<Place> place_from(const json& j) {
    if (!j.is_object()) {
        return std::nullopt;
    }
    auto number = [&](const char* key) -> std::optional<int> {
        auto it = j.find(key);
        if (it == j.end() || !it->is_number_integer()) {
            return std::nullopt;
        }
        return it->get<int>();
    };
    const auto x = number("x");
    const auto y = number("y");
    const auto w = number("width");
    const auto h = number("height");
    if (!x || !y || !w || !h || *w <= 0 || *h <= 0) {
        return std::nullopt;
    }
    Place p{.x = *x, .y = *y, .width = *w, .height = *h, .maximized = false};
    if (auto it = j.find("maximized"); it != j.end() && it->is_boolean()) {
        p.maximized = it->get<bool>();
    }
    return p;
}

} // namespace

Memory Memory::parse(std::string_view text, std::string* error) {
    Memory memory;
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        if (error != nullptr) {
            *error = "not a JSON object";
        }
        return memory;
    }
    auto apps = root.find("apps");
    if (apps == root.end() || !apps->is_object()) {
        return memory;
    }
    for (const auto& [name, app_json] : apps->items()) {
        if (!app_json.is_object()) {
            continue;
        }
        App app;
        if (auto used = app_json.find("used"); used != app_json.end() && used->is_number_integer()) {
            app.used = used->get<std::int64_t>();
        }
        auto windows = app_json.find("windows");
        if (windows == app_json.end() || !windows->is_array()) {
            continue;
        }
        for (const auto& w : *windows) {
            if (app.slots.size() == kMaxSlots) {
                break;
            }
            Slot slot;
            if (w.is_object()) {
                if (auto d = w.find("display"); d != w.end() && d->is_string()) {
                    slot.display = d->get<std::string>();
                }
                if (auto s = w.find("setups"); s != w.end() && s->is_object()) {
                    for (const auto& [setup, display] : s->items()) {
                        if (display.is_string()) {
                            slot.setups[setup] = display.get<std::string>();
                        }
                    }
                }
                if (auto p = w.find("places"); p != w.end() && p->is_object()) {
                    for (const auto& [display, place] : p->items()) {
                        if (auto parsed = place_from(place)) {
                            slot.places[display] = *parsed;
                        }
                    }
                }
            }
            app.slots.push_back(std::move(slot)); // keep numbering even if empty
        }
        memory.apps_[name] = std::move(app);
    }
    return memory;
}

std::string Memory::dump() const {
    json apps = json::object();
    for (const auto& [name, app] : apps_) {
        json windows = json::array();
        for (const auto& slot : app.slots) {
            json places = json::object();
            for (const auto& [display, place] : slot.places) {
                places[display] = place_json(place);
            }
            json setups = json::object();
            for (const auto& [setup, display] : slot.setups) {
                setups[setup] = display;
            }
            windows.push_back(json{{"display", slot.display}, {"setups", setups}, {"places", places}});
        }
        apps[name] = json{{"used", app.used}, {"windows", windows}};
    }
    const json root{{"version", kVersion}, {"apps", apps}};
    return root.dump(2) + "\n";
}

std::optional<std::size_t> Memory::claim(const std::string& app) {
    auto& held = held_[app];
    for (std::size_t i = 0; i < kMaxSlots; ++i) {
        if (i >= held.size()) {
            held.resize(i + 1, false);
        }
        if (!held[i]) {
            held[i] = true;
            return i;
        }
    }
    return std::nullopt;
}

void Memory::release(const std::string& app, std::size_t slot) {
    auto it = held_.find(app);
    if (it != held_.end() && slot < it->second.size()) {
        it->second[slot] = false;
    }
}

const Memory::Slot* Memory::find(const std::string& app, std::size_t slot) const {
    auto it = apps_.find(app);
    if (it == apps_.end() || slot >= it->second.slots.size()) {
        return nullptr;
    }
    return &it->second.slots[slot];
}

std::optional<std::string> Memory::display(const std::string& app, std::size_t slot,
                                           std::span<const std::string> connected) const {
    const Slot* s = find(app, slot);
    if (s == nullptr) {
        return std::nullopt;
    }
    auto is_connected = [&](const std::string& d) { return std::ranges::contains(connected, d); };
    if (auto it = s->setups.find(setup_key(connected)); it != s->setups.end() && is_connected(it->second)) {
        return it->second;
    }
    if (!s->display.empty() && is_connected(s->display)) {
        return s->display;
    }
    return std::nullopt;
}

std::optional<Place> Memory::place(const std::string& app, std::size_t slot, const std::string& display) const {
    const Slot* s = find(app, slot);
    if (s == nullptr) {
        return std::nullopt;
    }
    auto it = s->places.find(display);
    return it == s->places.end() ? std::nullopt : std::optional(it->second);
}

std::optional<Place> Memory::last_place(const std::string& app, std::size_t slot) const {
    const Slot* s = find(app, slot);
    return s == nullptr ? std::nullopt : place(app, slot, s->display);
}

void Memory::remember(const std::string& app, std::size_t slot, std::span<const std::string> connected,
                      const std::string& display, const Place& place, std::int64_t now) {
    if (slot >= kMaxSlots) {
        return;
    }
    App& a = apps_[app];
    a.used = now;
    if (a.slots.size() <= slot) {
        a.slots.resize(slot + 1);
    }
    Slot& s = a.slots[slot];
    s.display = display;
    s.setups[setup_key(connected)] = display;
    s.places[display] = place;

    if (apps_.size() > kMaxApps) {
        auto oldest = std::ranges::min_element(apps_, {}, [](const auto& entry) { return entry.second.used; });
        apps_.erase(oldest);
    }
}

void Memory::replace(Memory other) {
    apps_ = std::move(other.apps_);
}

std::string setup_key(std::span<const std::string> connected) {
    std::vector<std::string> sorted(connected.begin(), connected.end());
    std::ranges::sort(sorted);
    std::string key;
    for (const auto& d : sorted) {
        if (!key.empty()) {
            key += " + ";
        }
        key += d;
    }
    return key;
}

std::filesystem::path state_file() {
    // NOLINTBEGIN(concurrency-mt-unsafe)
    if (const char* state = std::getenv("XDG_STATE_HOME"); state != nullptr && *state == '/') {
        return std::filesystem::path(state) / "tetra" / "windows.json";
    }
    const char* home = std::getenv("HOME");
    // NOLINTEND(concurrency-mt-unsafe)
    return std::filesystem::path(home != nullptr ? home : "/tmp") / ".local" / "state" / "tetra" / "windows.json";
}

} // namespace tetra::windows
