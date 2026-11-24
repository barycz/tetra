#include "zones.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace tetra::zones {

namespace {

Layout columns(const char* name, const char* label, std::initializer_list<double> widths) {
    Layout layout{.name = name, .label = label, .zones = {}};
    double x = 0;
    for (double w : widths) {
        layout.zones.push_back({.x = x, .y = 0, .width = w, .height = 1});
        x += w;
    }
    return layout;
}

Layout grid(const char* name, const char* label, int cols, int rows) {
    Layout layout{.name = name, .label = label, .zones = {}};
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            layout.zones.push_back({.x = static_cast<double>(c) / cols,
                                    .y = static_cast<double>(r) / rows,
                                    .width = 1.0 / cols,
                                    .height = 1.0 / rows});
        }
    }
    return layout;
}

double distance(const Box& box, double x, double y) {
    const double dx = std::max({box.x - x, 0.0, x - (box.x + box.width)});
    const double dy = std::max({box.y - y, 0.0, y - (box.y + box.height)});
    return std::hypot(dx, dy);
}

} // namespace

const std::vector<Layout>& builtin() {
    static const std::vector<Layout> layouts = [] {
        constexpr double kThird = 1.0 / 3;
        std::vector<Layout> list;
        list.push_back({.name = "none", .label = "No zones", .zones = {}});
        list.push_back(columns("columns-2", "Two columns", {0.5, 0.5}));
        list.push_back(columns("columns-3", "Three columns", {kThird, kThird, kThird}));
        list.push_back(columns("priority", "Wide middle", {0.25, 0.5, 0.25}));
        list.push_back({.name = "main-left",
                        .label = "Main and side",
                        .zones = {{.x = 0, .y = 0, .width = 0.6, .height = 1},
                                  {.x = 0.6, .y = 0, .width = 0.4, .height = 0.5},
                                  {.x = 0.6, .y = 0.5, .width = 0.4, .height = 0.5}}});
        list.push_back({.name = "main-right",
                        .label = "Side and main",
                        .zones = {{.x = 0, .y = 0, .width = 0.4, .height = 0.5},
                                  {.x = 0, .y = 0.5, .width = 0.4, .height = 0.5},
                                  {.x = 0.4, .y = 0, .width = 0.6, .height = 1}}});
        list.push_back(grid("rows-2", "Two rows", 1, 2));
        list.push_back(grid("grid-4", "Four quarters", 2, 2));
        list.push_back(grid("grid-6", "Six", 3, 2));
        return list;
    }();
    return layouts;
}

std::vector<Layout> all(const config::Config& config) {
    std::vector<Layout> list = builtin();
    for (const auto& custom : config.windows.zone_layouts) {
        Layout layout{.name = custom.name, .label = custom.name, .zones = {}};
        for (const auto& [x, y, w, h] : custom.zones) {
            layout.zones.push_back({.x = x / 100, .y = y / 100, .width = w / 100, .height = h / 100});
        }
        list.push_back(std::move(layout));
    }
    return list;
}

std::optional<Layout> find(const config::Config& config, std::string_view name) {
    for (auto& layout : all(config)) {
        if (layout.name == name) {
            return std::move(layout);
        }
    }
    return std::nullopt;
}

std::string layout_name(const config::Config& config, std::string_view output) {
    if (const config::Output* entry = config.output(output); entry != nullptr && entry->zones) {
        return *entry->zones;
    }
    return config.windows.zones;
}

std::vector<Box> place(const Layout& layout, const Box& area, int gap) {
    // The area less half a gap all round, divided; each zone less half a gap
    // all round: a whole gap between zones and at the edges.
    const double half = gap / 2.0;
    const double inner_x = area.x + half;
    const double inner_y = area.y + half;
    const double inner_w = std::max(0.0, area.width - static_cast<double>(gap));
    const double inner_h = std::max(0.0, area.height - static_cast<double>(gap));
    std::vector<Box> boxes;
    for (const auto& z : layout.zones) {
        const auto left = static_cast<int>(std::lround(inner_x + (z.x * inner_w) + half));
        const auto top = static_cast<int>(std::lround(inner_y + (z.y * inner_h) + half));
        const auto right = static_cast<int>(std::lround(inner_x + ((z.x + z.width) * inner_w) - half));
        const auto bottom = static_cast<int>(std::lround(inner_y + ((z.y + z.height) * inner_h) - half));
        boxes.push_back({.x = left, .y = top, .width = std::max(1, right - left), .height = std::max(1, bottom - top)});
    }
    return boxes;
}

std::vector<std::size_t> pick(std::span<const Box> zones, double x, double y, int radius) {
    std::vector<std::size_t> picked;
    std::size_t nearest = 0;
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < zones.size(); ++i) {
        const double d = distance(zones[i], x, y);
        if (d < best) {
            best = d;
            nearest = i;
        }
    }
    if (zones.empty() || best > radius) {
        return picked;
    }
    picked.push_back(nearest);
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (i != nearest && distance(zones[i], x, y) <= radius) {
            picked.push_back(i);
        }
    }
    return picked;
}

Box span(std::span<const Box> zones, std::span<const std::size_t> picked) {
    if (picked.empty()) {
        return {};
    }
    int left = std::numeric_limits<int>::max();
    int top = std::numeric_limits<int>::max();
    int right = std::numeric_limits<int>::min();
    int bottom = std::numeric_limits<int>::min();
    for (std::size_t i : picked) {
        const Box& b = zones[i];
        left = std::min(left, b.x);
        top = std::min(top, b.y);
        right = std::max(right, b.x + b.width);
        bottom = std::max(bottom, b.y + b.height);
    }
    return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

} // namespace tetra::zones
