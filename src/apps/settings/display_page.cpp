// The Displays page: what the compositor reports (wlr-output-management),
// changed through "outputs" in config.json like everything else. A display
// change can be taken back for a few seconds, and is undone by itself if
// nobody confirms it, in case it left the screen unreadable.

#include "settings.hpp"

#include "config.hpp"
#include "style.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>

namespace tetra::settings {

namespace {

constexpr auto kRevertAfter = std::chrono::seconds(15);
constexpr float kCardPadding = 18; // as in settings.cpp
constexpr float kArrangementHeight = 210;
constexpr float kArrangementMargin = 24;
constexpr std::array kScales = {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};

struct Size {
    int width;
    int height;
};
// For displays that take any size (headless, nested).
constexpr std::array kAnySizes = {Size{1280, 720},  Size{1366, 768},  Size{1600, 900},
                                  Size{1920, 1080}, Size{2560, 1440}, Size{3840, 2160}};

std::string refresh_text(int mhz) {
    std::string text = std::format("{:.3f}", mhz / 1000.0);
    while (text.back() == '0') {
        text.pop_back();
    }
    if (text.back() == '.') {
        text.pop_back();
    }
    return text;
}

std::string size_text(int width, int height) {
    return std::format("{} × {}", width, height);
}

std::string scale_text(double scale) {
    return std::format("{}%", std::lround(scale * 100));
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// Where a display dropped at `moving` goes: against a side of one of the
// others, as close as possible to where it was dropped, overlapping none.
// Edges within a short distance line up.
std::pair<int, int> snap(const std::vector<Rect>& others, const Rect& moving) {
    if (others.empty()) {
        return {0, 0};
    }
    auto along = [](double v, double size, double start, double length) {
        // Keep touching: overlap the other display by at least one pixel.
        v = std::clamp(v, start - size + 1, start + length - 1);
        const double threshold = std::max(32.0, std::min(size, length) * 0.08);
        if (std::abs(v - start) < threshold) {
            return start;
        }
        if (std::abs((v + size) - (start + length)) < threshold) {
            return start + length - size;
        }
        return v;
    };
    std::optional<Rect> best;
    double best_distance = 0;
    for (const Rect& o : others) {
        const double y = along(moving.y, moving.h, o.y, o.h);
        const double x = along(moving.x, moving.w, o.x, o.w);
        for (const auto& [cx, cy] : {std::pair{o.x + o.w, y}, std::pair{o.x - moving.w, y},
                                     std::pair{x, o.y - moving.h}, std::pair{x, o.y + o.h}}) {
            const Rect candidate{std::round(cx), std::round(cy), moving.w, moving.h};
            if (std::ranges::any_of(others, [&](const Rect& r) { return overlap(candidate, r); })) {
                continue;
            }
            const double distance = std::hypot(candidate.x - moving.x, candidate.y - moving.y);
            if (!best || distance < best_distance) {
                best = candidate;
                best_distance = distance;
            }
        }
    }
    if (!best) {
        return {static_cast<int>(moving.x), static_cast<int>(moving.y)};
    }
    return {static_cast<int>(best->x), static_cast<int>(best->y)};
}

} // namespace

// --- writing -------------------------------------------------------------------------------

std::string Settings::display_members(const std::string& name, const DisplayChanges& changes) const {
    // A display without its own entry follows "*"; its new entry starts
    // from that, so only what was changed changes.
    DisplayChanges members;
    const auto& outputs = app_.config().outputs;
    if (std::ranges::none_of(outputs, [&](const auto& o) { return o.name == name; })) {
        if (const config::Output* fallback = app_.config().output(name)) {
            if (!fallback->enabled) {
                members.emplace_back("enabled", "false");
            }
            if (fallback->mode) {
                const auto& m = *fallback->mode;
                members.emplace_back("mode", m.refresh > 0 ? std::format("\"{}x{}@{}\"", m.width, m.height, m.refresh)
                                                           : std::format("\"{}x{}\"", m.width, m.height));
            }
            if (fallback->scale != 1) {
                members.emplace_back("scale", std::format("{}", fallback->scale));
            }
            if (fallback->position) {
                members.emplace_back("position",
                                     std::format("[{}, {}]", fallback->position->first, fallback->position->second));
            }
        }
    }
    for (const auto& [key, value] : changes) {
        std::erase_if(members, [&](const auto& m) { return m.first == key; });
        members.emplace_back(key, value);
    }
    std::string json = "{";
    for (const auto& [key, value] : members) {
        json += std::format("{}\"{}\": {}", json.size() > 1 ? ", " : "", key, value);
    }
    return json + "}";
}

void Settings::change_displays(const std::vector<std::pair<std::string, DisplayChanges>>& changes) {
    std::vector<std::pair<std::string, std::string>> edits;
    edits.reserve(changes.size());
    for (const auto& [name, members] : changes) {
        edits.emplace_back(name, display_members(name, members));
    }
    const auto path = config::active_file();
    const auto before = read_text(path);
    if (!save([&](config::Document& doc) -> std::expected<void, std::string> {
            for (const auto& [name, members] : edits) {
                if (auto result = doc.merge_element({"outputs"}, "name", name, members); !result) {
                    return result;
                }
            }
            return {};
        })) {
        return;
    }
    const auto after = read_text(path);
    if (after == before) {
        return;
    }
    if (!revert_) {
        // What to go back to: the file before the first change.
        revert_ = Revert{.before = before, .after = {}, .deadline = {}};
    }
    revert_->after = after.value_or("");
    revert_->deadline = std::chrono::steady_clock::now() + kRevertAfter;
    tick_revert();
}

void Settings::tick_revert() {
    if (revert_timer_) {
        return;
    }
    revert_timer_ = app_.add_timer(std::chrono::milliseconds(250), [this] {
        revert_timer_.reset();
        if (!revert_) {
            return;
        }
        if (std::chrono::steady_clock::now() >= revert_->deadline) {
            revert_display_changes();
            return;
        }
        window_->request_redraw();
        tick_revert();
    });
}

void Settings::keep_display_changes() {
    revert_.reset();
    if (revert_timer_) {
        app_.cancel_timer(*revert_timer_);
        revert_timer_.reset();
    }
    window_->request_redraw();
}

void Settings::revert_display_changes() {
    auto pending = std::exchange(revert_, std::nullopt);
    if (revert_timer_) {
        app_.cancel_timer(*revert_timer_);
        revert_timer_.reset();
    }
    window_->request_redraw();
    if (!pending) {
        return;
    }
    const auto path = config::active_file();
    if (read_text(path) != pending->after) {
        error_ = "config.json was changed meanwhile, so the display settings were kept.";
        return;
    }
    if (pending->before) {
        auto result = config::write_file(path, *pending->before);
        error_ = result ? std::string() : result.error();
    } else {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

// --- drawing -------------------------------------------------------------------------------

void Settings::draw_revert_banner() {
    if (!revert_) {
        return;
    }
    const auto& p = app_.palette();
    const auto left = std::chrono::ceil<std::chrono::seconds>(revert_->deadline - std::chrono::steady_clock::now());
    ImGui::Dummy({0, 8});
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::Indent(16);
    ImGui::Dummy({0, 6});
    ImGui::TextUnformatted("Keep these display settings?");
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::Text("They go back to how they were in %d s.", static_cast<int>(std::max<int64_t>(0, left.count())));
    ImGui::PopStyleColor();
    const float buttons_y = start.y + 14;
    const float button_width = 110;
    ImGui::SetCursorScreenPos({start.x + width - 16 - (2 * button_width) - 8, buttons_y});
    ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(p.accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::to_imvec4(p.accent));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.accent_text));
    if (ImGui::Button("Keep", {button_width, 0})) {
        keep_display_changes();
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 8);
    if (ImGui::Button("Revert", {button_width, 0})) {
        revert_display_changes();
    }
    ImGui::SetCursorScreenPos({start.x, std::max(ImGui::GetCursorScreenPos().y, start.y + 58)});
    ImGui::Unindent(16);
    const ImVec2 end{start.x + width, ImGui::GetCursorScreenPos().y};
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(start, end, ui::to_imu32(p.surface), 10);
    dl->AddRect(start, end, ui::to_imu32(p.accent), 10, 1.5F);
    dl->ChannelsMerge();
    ImGui::Dummy({0, 2});
}

void Settings::display_arrangement() {
    const auto& p = app_.palette();
    const auto& list = displays_.list();
    struct Box {
        const Display* display;
        int number;
        Rect rect;
    };
    std::vector<Box> boxes;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Display& d = *list[i];
        if (d.enabled && d.current != nullptr) {
            boxes.push_back({&d, static_cast<int>(i + 1),
                             {static_cast<double>(d.x), static_cast<double>(d.y),
                              static_cast<double>(d.logical_width()), static_cast<double>(d.logical_height())}});
        }
    }

    const float width = ImGui::GetContentRegionAvail().x - kCardPadding;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##arrangement", {width, kArrangementHeight});
    const bool can_move = boxes.size() > 1;

    // Layout -> screen. Fixed while dragging, so the view holds still.
    Mapping mapping;
    if (drag_) {
        mapping = drag_->mapping;
    } else if (!boxes.empty()) {
        double min_x = boxes[0].rect.x;
        double min_y = boxes[0].rect.y;
        double max_x = min_x + boxes[0].rect.w;
        double max_y = min_y + boxes[0].rect.h;
        for (const auto& b : boxes) {
            min_x = std::min(min_x, b.rect.x);
            min_y = std::min(min_y, b.rect.y);
            max_x = std::max(max_x, b.rect.x + b.rect.w);
            max_y = std::max(max_y, b.rect.y + b.rect.h);
        }
        const double room_w = width - (2.0 * kArrangementMargin);
        const double room_h = kArrangementHeight - (2.0 * kArrangementMargin);
        // Leave room around a single display: it is not the whole canvas.
        const double fill = can_move ? 1.0 : 0.6;
        mapping.scale = std::min(room_w / (max_x - min_x), room_h / (max_y - min_y)) * fill;
        mapping.x = origin.x + ((width - ((max_x - min_x) * mapping.scale)) / 2) - (min_x * mapping.scale);
        mapping.y = origin.y + ((kArrangementHeight - ((max_y - min_y) * mapping.scale)) / 2) -
                    (min_y * mapping.scale);
    }
    auto to_screen = [&](const Rect& r) {
        return std::pair{ImVec2{static_cast<float>(mapping.x + (r.x * mapping.scale)),
                                static_cast<float>(mapping.y + (r.y * mapping.scale))},
                         ImVec2{static_cast<float>(mapping.x + ((r.x + r.w) * mapping.scale)),
                                static_cast<float>(mapping.y + ((r.y + r.h) * mapping.scale))}};
    };

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto box_at = [&](ImVec2 at) -> const Box* {
        for (const auto& b : boxes) {
            const auto [a, z] = to_screen(b.rect);
            if (at.x >= a.x && at.x < z.x && at.y >= a.y && at.y < z.y) {
                return &b;
            }
        }
        return nullptr;
    };
    if (ImGui::IsItemActivated() && can_move) {
        if (const Box* hit = box_at(mouse)) {
            drag_ = Drag{.name = hit->display->name, .start_mouse = mouse, .start = hit->rect, .mapping = mapping};
        }
    }
    std::optional<Rect> dragged;
    if (drag_) {
        const ImVec2 delta{mouse.x - drag_->start_mouse.x, mouse.y - drag_->start_mouse.y};
        dragged = Rect{drag_->start.x + (delta.x / mapping.scale), drag_->start.y + (delta.y / mapping.scale),
                       drag_->start.w, drag_->start.h};
        if (ImGui::IsItemDeactivated()) {
            const bool moved = std::hypot(delta.x, delta.y) > 4;
            const std::string name = drag_->name;
            drag_.reset();
            if (moved) {
                place_display(name, *dragged);
            }
            dragged.reset();
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, {origin.x + width, origin.y + kArrangementHeight}, true);
    if (boxes.empty()) {
        const char* text = "No display is in use.";
        const ImVec2 size = ImGui::CalcTextSize(text);
        dl->AddText({origin.x + ((width - size.x) / 2), origin.y + ((kArrangementHeight - size.y) / 2)},
                    ui::to_imu32(p.text_dim), text);
    }
    const Box* hovered = ImGui::IsItemHovered() && !drag_ ? box_at(mouse) : nullptr;
    for (const auto& b : boxes) {
        const bool moving = dragged && drag_ && drag_->name == b.display->name;
        const auto [a, z] = to_screen(moving ? *dragged : b.rect);
        const bool lit = moving || &b == hovered;
        dl->AddRectFilled(a, z, ui::to_imu32(lit ? p.pressed : p.hover), 6);
        dl->AddRect(a, z, ui::to_imu32(lit ? p.accent : p.separator), 6, lit ? 2.0F : 1.0F);
        const std::string number = std::to_string(b.number);
        ImGui::PushFont(nullptr, 26);
        const ImVec2 number_size = ImGui::CalcTextSize(number.c_str());
        const ImVec2 center{(a.x + z.x) / 2, (a.y + z.y) / 2};
        dl->AddText({center.x - (number_size.x / 2), center.y - number_size.y + 4}, ui::to_imu32(p.text),
                    number.c_str());
        ImGui::PopFont();
        ImGui::PushFont(nullptr, 13);
        const ImVec2 name_size = ImGui::CalcTextSize(b.display->name.c_str());
        dl->AddText({center.x - (name_size.x / 2), center.y + 6}, ui::to_imu32(p.text_dim),
                    b.display->name.c_str());
        ImGui::PopFont();
    }
    dl->PopClipRect();
    if (hovered != nullptr && can_move) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

void Settings::place_display(const std::string& name, const Rect& dropped) {
    std::vector<Rect> others;
    std::vector<std::pair<std::string, Rect>> placed;
    for (const auto& d : displays_.list()) {
        if (!d->enabled || d->current == nullptr) {
            continue;
        }
        const Rect r{static_cast<double>(d->x), static_cast<double>(d->y), static_cast<double>(d->logical_width()),
                     static_cast<double>(d->logical_height())};
        if (d->name != name) {
            others.push_back(r);
        }
        placed.emplace_back(d->name, r);
    }
    const auto [x, y] = snap(others, dropped);
    for (auto& [n, r] : placed) {
        if (n == name) {
            r.x = x;
            r.y = y;
        }
    }
    // The top-left corner of the whole layout stays at 0,0.
    double min_x = placed.front().second.x;
    double min_y = placed.front().second.y;
    for (const auto& [n, r] : placed) {
        min_x = std::min(min_x, r.x);
        min_y = std::min(min_y, r.y);
    }
    std::vector<std::pair<std::string, DisplayChanges>> changes;
    for (const auto& [n, r] : placed) {
        changes.push_back({n, {{"position", std::format("[{}, {}]", std::lround(r.x - min_x), std::lround(r.y - min_y))}}});
    }
    change_displays(changes);
}

void Settings::page_displays() {
    if (!displays_.available()) {
        begin_card(nullptr);
        row("Displays are not available", "The compositor does not report its displays.");
        end_card();
        return;
    }
    const auto& list = displays_.list();

    begin_card(list.size() > 1 ? "Arrangement" : nullptr);
    display_arrangement();
    if (list.size() > 1) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(app_.palette().text_dim));
        ImGui::PushFont(nullptr, 13);
        ImGui::TextUnformatted("Drag the displays to match where they stand on your desk.");
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    end_card();

    const auto in_use = std::ranges::count_if(list, [](const auto& d) { return d->enabled; });
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Display& d = *list[i];
        ImGui::PushID(d.name.c_str());
        const std::string title = list.size() > 1 ? std::format("{} · {} ({})", i + 1, d.title(), d.name)
                                                  : std::format("{} ({})", d.title(), d.name);
        begin_card(title.c_str());

        const bool last_one = d.enabled && in_use == 1;
        row("Use this display", last_one ? "It is the only display in use." : nullptr);
        ImGui::BeginDisabled(last_one);
        if (toggle("##enabled", d.enabled)) {
            change_displays({{d.name, {{"enabled", d.enabled ? "false" : "true"}}}});
        }
        ImGui::EndDisabled();

        if (d.enabled && d.current != nullptr) {
            const DisplayMode& current = *d.current;
            row("Resolution", d.any_size() ? "A virtual display: any size works." : nullptr);
            if (ImGui::BeginCombo("##resolution", size_text(current.width, current.height).c_str())) {
                std::vector<Size> sizes;
                std::vector<bool> recommended;
                if (d.any_size()) {
                    sizes.assign(kAnySizes.begin(), kAnySizes.end());
                    if (std::ranges::none_of(sizes, [&](Size s) {
                            return s.width == current.width && s.height == current.height;
                        })) {
                        sizes.push_back({current.width, current.height});
                    }
                } else {
                    for (const auto& m : d.modes) {
                        if (std::ranges::none_of(sizes, [&](Size s) { return s.width == m->width && s.height == m->height; })) {
                            sizes.push_back({m->width, m->height});
                        }
                    }
                }
                std::ranges::sort(sizes, [](Size a, Size b) {
                    return a.width * a.height != b.width * b.height ? a.width * a.height > b.width * b.height
                                                                    : a.width > b.width;
                });
                for (const Size s : sizes) {
                    const bool preferred = std::ranges::any_of(d.modes, [&](const auto& m) {
                        return m->preferred && m->width == s.width && m->height == s.height;
                    });
                    std::string label = size_text(s.width, s.height);
                    if (preferred) {
                        label += "  (recommended)";
                    }
                    const bool selected = s.width == current.width && s.height == current.height;
                    if (ImGui::Selectable(label.c_str(), selected) && !selected) {
                        // The highest refresh rate at that size.
                        change_displays({{d.name, {{"mode", std::format("\"{}x{}\"", s.width, s.height)}}}});
                    }
                }
                ImGui::EndCombo();
            }

            std::vector<int> rates;
            for (const auto& m : d.modes) {
                if (m->width == current.width && m->height == current.height && m->refresh > 0 &&
                    !std::ranges::contains(rates, m->refresh)) {
                    rates.push_back(m->refresh);
                }
            }
            std::ranges::sort(rates, std::greater<>());
            if (!d.any_size() && rates.size() > 1) {
                row("Refresh rate");
                if (ImGui::BeginCombo("##refresh", std::format("{} Hz", refresh_text(current.refresh)).c_str())) {
                    for (const int rate : rates) {
                        const bool selected = rate == current.refresh;
                        if (ImGui::Selectable(std::format("{} Hz", refresh_text(rate)).c_str(), selected) &&
                            !selected) {
                            change_displays({{d.name,
                                              {{"mode", std::format("\"{}x{}@{}\"", current.width, current.height,
                                                                    refresh_text(rate))}}}});
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            row("Scale", "Makes text, apps and the taskbar bigger.");
            std::vector<double> scales(kScales.begin(), kScales.end());
            if (std::ranges::none_of(scales, [&](double s) { return std::abs(s - d.scale) < 0.001; })) {
                scales.push_back(d.scale);
                std::ranges::sort(scales);
            }
            if (ImGui::BeginCombo("##scale", scale_text(d.scale).c_str())) {
                for (const double s : scales) {
                    const bool selected = std::abs(s - d.scale) < 0.001;
                    if (ImGui::Selectable(scale_text(s).c_str(), selected) && !selected) {
                        change_displays({{d.name, {{"scale", std::format("{}", s)}}}});
                    }
                }
                ImGui::EndCombo();
            }
        }
        end_card();
        ImGui::PopID();
    }
}

} // namespace tetra::settings
