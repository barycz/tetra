#pragma once

#include "config.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Snap zones: a display is divided by a layout into zones; dragging a window
// with Shift held drops it into the zone (or zones) under the pointer.
namespace tetra::zones {

// A zone as fractions (0..1) of the display's usable area.
struct Fraction {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

struct Layout {
    std::string name;  // as written in the config ("columns-3")
    std::string label; // for people ("Three columns")
    std::vector<Fraction> zones;
};

// Built-in layouts, "none" first.
const std::vector<Layout>& builtin();
// Built-in layouts, then the config's own (windows.zone_layouts).
std::vector<Layout> all(const config::Config& config);
// A layout by name, built-in or the config's; none if unknown.
std::optional<Layout> find(const config::Config& config, std::string_view name);
// The layout name for a display: its outputs entry, else windows.zones.
std::string layout_name(const config::Config& config, std::string_view output);

struct Box {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool operator==(const Box&) const = default;
};

// Zones in pixels for `area`, with `gap` pixels between zones and around
// them.
std::vector<Box> place(const Layout& layout, const Box& area, int gap);

// The zones a window dropped at (x, y) goes into: the one under the point
// (or the nearest, in a gap), plus any other within `radius` (near the edge
// between two zones, both). Empty if the point is outside every zone's reach.
std::vector<std::size_t> pick(std::span<const Box> zones, double x, double y, int radius);

// The box covering the picked zones.
Box span(std::span<const Box> zones, std::span<const std::size_t> picked);

} // namespace tetra::zones
