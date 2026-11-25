// Drawing the terminal: fonts, cell metrics, colours and the grid.

#include "terminal.hpp"

#include "style.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace tetra::terminal {

namespace {

constexpr float kPadding = 4;
constexpr char32_t kWideFiller = 0xFFFFFFFF; // the cell after a double-width character

struct Rgb {
    uint8_t r, g, b;
};

// The 16 standard colours, for a dark and a light background.
constexpr std::array<Rgb, 16> kDark = {{
    {0x2e, 0x31, 0x38}, {0xe0, 0x6c, 0x75}, {0x98, 0xc3, 0x79}, {0xe5, 0xc0, 0x7b},
    {0x61, 0xaf, 0xef}, {0xc6, 0x78, 0xdd}, {0x56, 0xb6, 0xc2}, {0xd4, 0xd7, 0xdd},
    {0x5c, 0x63, 0x70}, {0xff, 0x7b, 0x86}, {0xb5, 0xe8, 0x90}, {0xff, 0xd6, 0x8a},
    {0x7c, 0xc4, 0xff}, {0xdc, 0x8e, 0xf0}, {0x6e, 0xd3, 0xdf}, {0xff, 0xff, 0xff},
}};
constexpr std::array<Rgb, 16> kLight = {{
    {0x38, 0x3a, 0x42}, {0xc8, 0x32, 0x3a}, {0x3f, 0x8f, 0x2a}, {0xa2, 0x6a, 0x00},
    {0x2f, 0x6f, 0xd6}, {0xa0, 0x3f, 0xbf}, {0x0f, 0x8a, 0x99}, {0xd0, 0xd3, 0xda},
    {0x69, 0x6c, 0x77}, {0xe0, 0x45, 0x4e}, {0x50, 0xa1, 0x36}, {0xb9, 0x82, 0x00},
    {0x44, 0x82, 0xea}, {0xb4, 0x4f, 0xd3}, {0x1a, 0x9c, 0xad}, {0x1b, 0x1d, 0x22},
}};

ImU32 pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xff) {
    return IM_COL32(r, g, b, a);
}

// xterm's 256 colours above the first 16: a 6x6x6 cube, then 24 greys.
ImU32 indexed_256(int index) {
    if (index < 232) {
        static constexpr std::array<uint8_t, 6> kLevels = {0, 95, 135, 175, 215, 255};
        const int i = index - 16;
        return pack(kLevels.at(static_cast<std::size_t>(i / 36)), kLevels.at(static_cast<std::size_t>((i / 6) % 6)),
                    kLevels.at(static_cast<std::size_t>(i % 6)));
    }
    const auto grey = static_cast<uint8_t>(8 + (10 * (index - 232)));
    return pack(grey, grey, grey);
}


// Box drawing and block characters, drawn to fill the cell so lines join
// across cells (font glyphs are usually shorter than the line height).
// Arms: up, down, left, right; 0 none, 1 light, 2 heavy, 3 double.
struct Arms {
    uint8_t up, down, left, right;
};

std::optional<Arms> box_arms(char32_t c) {
    switch (c) {
    case 0x2500: case 0x2504: case 0x2508: case 0x254C: return Arms{0, 0, 1, 1};
    case 0x2501: case 0x2505: case 0x2509: case 0x254D: return Arms{0, 0, 2, 2};
    case 0x2502: case 0x2506: case 0x250A: case 0x254E: return Arms{1, 1, 0, 0};
    case 0x2503: case 0x2507: case 0x250B: case 0x254F: return Arms{2, 2, 0, 0};
    case 0x250C: case 0x256D: return Arms{0, 1, 0, 1};
    case 0x2510: case 0x256E: return Arms{0, 1, 1, 0};
    case 0x2514: case 0x2570: return Arms{1, 0, 0, 1};
    case 0x2518: case 0x256F: return Arms{1, 0, 1, 0};
    case 0x251C: return Arms{1, 1, 0, 1};
    case 0x2524: return Arms{1, 1, 1, 0};
    case 0x252C: return Arms{0, 1, 1, 1};
    case 0x2534: return Arms{1, 0, 1, 1};
    case 0x253C: return Arms{1, 1, 1, 1};
    case 0x250F: return Arms{0, 2, 0, 2};
    case 0x2513: return Arms{0, 2, 2, 0};
    case 0x2517: return Arms{2, 0, 0, 2};
    case 0x251B: return Arms{2, 0, 2, 0};
    case 0x2523: return Arms{2, 2, 0, 2};
    case 0x252B: return Arms{2, 2, 2, 0};
    case 0x2533: return Arms{0, 2, 2, 2};
    case 0x253B: return Arms{2, 0, 2, 2};
    case 0x254B: return Arms{2, 2, 2, 2};
    case 0x2550: return Arms{0, 0, 3, 3};
    case 0x2551: return Arms{3, 3, 0, 0};
    case 0x2554: return Arms{0, 3, 0, 3};
    case 0x2557: return Arms{0, 3, 3, 0};
    case 0x255A: return Arms{3, 0, 0, 3};
    case 0x255D: return Arms{3, 0, 3, 0};
    case 0x2560: return Arms{3, 3, 0, 3};
    case 0x2563: return Arms{3, 3, 3, 0};
    case 0x2566: return Arms{0, 3, 3, 3};
    case 0x2569: return Arms{3, 0, 3, 3};
    case 0x256C: return Arms{3, 3, 3, 3};
    case 0x2574: return Arms{0, 0, 1, 0};
    case 0x2575: return Arms{1, 0, 0, 0};
    case 0x2576: return Arms{0, 0, 0, 1};
    case 0x2577: return Arms{0, 1, 0, 0};
    default: return std::nullopt;
    }
}

bool draw_special(ImDrawList* dl, char32_t c, float x, float y, float w, float h, ImU32 col) {
    // Blocks: full, halves, eighths, shades.
    if (c >= 0x2580 && c <= 0x2593) {
        ImVec2 a{x, y};
        ImVec2 b{x + w, y + h};
        if (c == 0x2580) {
            b.y = y + std::round(h / 2);
        } else if (c >= 0x2581 && c <= 0x2588) { // lower 1/8 .. full
            a.y = y + h - std::round(h * static_cast<float>(c - 0x2580) / 8);
        } else if (c >= 0x2589 && c <= 0x258F) { // left 7/8 .. 1/8
            b.x = x + std::round(w * static_cast<float>(0x2590 - c) / 8);
        } else if (c == 0x2590) {
            a.x = x + std::round(w / 2);
        } else if (c >= 0x2591) { // shades
            const float alpha = c == 0x2591 ? 0.25F : (c == 0x2592 ? 0.5F : 0.75F);
            const auto a8 = static_cast<ImU32>(static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
            col = (col & ~IM_COL32_A_MASK) | (a8 << IM_COL32_A_SHIFT);
        }
        dl->AddRectFilled(a, b, col);
        return true;
    }
    const auto arms = box_arms(c);
    if (!arms) {
        return false;
    }
    const float t = std::max(1.0F, std::round(w / 8));
    const float cx = x + std::floor((w - t) / 2);
    const float cy = y + std::floor((h - t) / 2);
    auto vertical = [&](float from, float to, uint8_t weight) {
        if (weight == 3) {
            dl->AddRectFilled({cx - t, from}, {cx, to}, col);
            dl->AddRectFilled({cx + t, from}, {cx + (2 * t), to}, col);
        } else {
            const float k = weight == 2 ? t : 0;
            dl->AddRectFilled({cx - (k / 2), from}, {cx + t + (k / 2), to}, col);
        }
    };
    auto horizontal = [&](float from, float to, uint8_t weight) {
        if (weight == 3) {
            dl->AddRectFilled({from, cy - t}, {to, cy}, col);
            dl->AddRectFilled({from, cy + t}, {to, cy + (2 * t)}, col);
        } else {
            const float k = weight == 2 ? t : 0;
            dl->AddRectFilled({from, cy - (k / 2)}, {to, cy + t + (k / 2)}, col);
        }
    };
    if (arms->up != 0) {
        vertical(y, cy + t, arms->up);
    }
    if (arms->down != 0) {
        vertical(cy, y + h, arms->down);
    }
    if (arms->left != 0) {
        horizontal(x, cx + t, arms->left);
    }
    if (arms->right != 0) {
        horizontal(cx, x + w, arms->right);
    }
    return true;
}

} // namespace

void Terminal::load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    auto add = [&](const char* pattern) -> ImFont* {
        const std::string path = ui::find_font(pattern);
        return path.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(path.c_str(), font_size_);
    };
    regular_ = add("monospace");
    bold_ = add("monospace:bold");
    italic_ = add("monospace:italic");
    if (regular_ == nullptr) {
        regular_ = io.Fonts->AddFontDefault();
    }
    measure();
}

void Terminal::measure() {
    ImFontBaked* baked = regular_->GetFontBaked(font_size_);
    // Whole device pixels, so every cell starts on a pixel boundary.
    const float scale = std::max(1.0F, ImGui::GetIO().DisplayFramebufferScale.x);
    cell_width_ = std::round(baked->GetCharAdvance('M') * scale) / scale;
    cell_height_ = std::ceil((baked->Ascent - baked->Descent) * scale) / scale;
    ascent_ = baked->Ascent;
}

uint32_t Terminal::color(const VTermColor& c, bool foreground) const {
    const auto& p = app_.palette();
    if (foreground && VTERM_COLOR_IS_DEFAULT_FG(&c)) {
        return ui::to_imu32(p.text);
    }
    if (!foreground && VTERM_COLOR_IS_DEFAULT_BG(&c)) {
        return ui::to_imu32(p.window);
    }
    if (VTERM_COLOR_IS_INDEXED(&c)) {
        if (c.indexed.idx < 16) {
            const auto& table = app_.config().theme.dark ? kDark : kLight;
            const Rgb rgb = table.at(c.indexed.idx);
            return pack(rgb.r, rgb.g, rgb.b);
        }
        return indexed_256(c.indexed.idx);
    }
    return pack(c.rgb.red, c.rgb.green, c.rgb.blue);
}

Terminal::Line Terminal::screen_line(int row) const {
    Line line(static_cast<std::size_t>(cols_));
    for (int col = 0; col < cols_; ++col) {
        VTermScreenCell c{};
        vterm_screen_get_cell(screen_, {.row = row, .col = col}, &c);
        uint8_t attrs = 0;
        attrs |= c.attrs.bold != 0 ? kBold : 0;
        attrs |= c.attrs.italic != 0 ? kItalic : 0;
        attrs |= c.attrs.underline != 0 ? kUnderline : 0;
        attrs |= c.attrs.strike != 0 ? kStrike : 0;
        attrs |= c.attrs.reverse != 0 ? kReverse : 0;
        attrs |= c.attrs.conceal != 0 ? kConceal : 0;
        line[static_cast<std::size_t>(col)] = {.ch = c.chars[0], .width = static_cast<uint8_t>(c.width),
                                               .attrs = attrs, .fg = c.fg, .bg = c.bg};
    }
    return line;
}

// A line of the history: a scrollback line, or (into scratch) a screen row.
const Terminal::Line* Terminal::history_line(int64_t line, Line& scratch) const {
    if (line < first_line()) {
        return nullptr;
    }
    if (line < screen_top()) {
        return &scrollback_[static_cast<std::size_t>(line - first_line())];
    }
    const auto row = static_cast<int>(line - screen_top());
    if (row >= rows_) {
        return nullptr;
    }
    scratch = screen_line(row);
    return &scratch;
}

void Terminal::draw() {
    ImGuiIO& io = ImGui::GetIO();
    const auto& p = app_.palette();
    resize_grid();

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.window));
    ImGui::Begin("##terminal", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNav);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused != focused_) {
        focused_ = focused;
        VTermState* state = vterm_obtain_state(vt_);
        focused ? vterm_state_focus_in(state) : vterm_state_focus_out(state);
    }
    handle_mouse();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 default_bg = ui::to_imu32(p.window);
    const ImU32 selection = ui::to_imu32(p.accent.with_alpha(0.35F));
    const float glyph_top = std::round((cell_height_ - (ascent_ - regular_->GetFontBaked(font_size_)->Descent)) / 2);
    const int64_t top = screen_top() - scroll_;
    Line scratch;
    for (int row = 0; row < rows_; ++row) {
        const int64_t index = top + row;
        const Line* line = history_line(index, scratch);
        if (line == nullptr) {
            continue;
        }
        const float y = kPadding + (static_cast<float>(row) * cell_height_);
        for (int col = 0; col < cols_ && static_cast<std::size_t>(col) < line->size(); ++col) {
            const Cell& cell = (*line)[static_cast<std::size_t>(col)];
            if (cell.ch == kWideFiller) {
                continue;
            }
            const float x = kPadding + (static_cast<float>(col) * cell_width_);
            const float w = cell_width_ * static_cast<float>(std::max<uint8_t>(1, cell.width));
            ImU32 fg = color(cell.fg, true);
            ImU32 bg = color(cell.bg, false);
            if ((cell.attrs & kReverse) != 0) {
                std::swap(fg, bg);
            }
            if ((cell.attrs & kConceal) != 0) {
                fg = bg;
            }
            if (bg != default_bg) {
                dl->AddRectFilled({x, y}, {x + w, y + cell_height_}, bg);
            }
            if (selected(index, col)) {
                dl->AddRectFilled({x, y}, {x + w, y + cell_height_}, selection);
            }
            if (draw_special(dl, cell.ch, x, y, w, cell_height_, fg)) {
                // drawn to fill the cell
            } else if (cell.ch > ' ' && cell.ch <= 0xFFFF) {
                ImFont* font = (cell.attrs & kBold) != 0 && bold_ != nullptr       ? bold_
                               : (cell.attrs & kItalic) != 0 && italic_ != nullptr ? italic_
                                                                                   : regular_;
                font->RenderChar(dl, font_size_, {x, y + glyph_top}, fg, static_cast<ImWchar>(cell.ch));
            }
            if ((cell.attrs & kUnderline) != 0) {
                const float uy = y + glyph_top + ascent_ + 1;
                dl->AddLine({x, uy}, {x + w, uy}, fg);
            }
            if ((cell.attrs & kStrike) != 0) {
                const float sy = std::round(y + (cell_height_ / 2));
                dl->AddLine({x, sy}, {x + w, sy}, fg);
            }
        }
    }

    // The cursor, unless scrolled back.
    if (cursor_visible_ && scroll_ == 0 && cursor_.row < rows_ && cursor_.col < cols_) {
        const float x = kPadding + (static_cast<float>(cursor_.col) * cell_width_);
        const float y = kPadding + (static_cast<float>(cursor_.row) * cell_height_);
        const ImU32 cursor = ui::to_imu32(p.text);
        if (!focused_) {
            dl->AddRect({x, y}, {x + cell_width_, y + cell_height_}, cursor);
        } else if (cursor_shape_ == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
            dl->AddRectFilled({x, y + cell_height_ - 2}, {x + cell_width_, y + cell_height_}, cursor);
        } else if (cursor_shape_ == VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
            dl->AddRectFilled({x, y}, {x + 2, y + cell_height_}, cursor);
        } else {
            dl->AddRectFilled({x, y}, {x + cell_width_, y + cell_height_}, cursor);
            VTermScreenCell c{};
            vterm_screen_get_cell(screen_, cursor_, &c);
            if (c.chars[0] > ' ' && c.chars[0] <= 0xFFFF) {
                regular_->RenderChar(dl, font_size_, {x, y + glyph_top}, default_bg, static_cast<ImWchar>(c.chars[0]));
            }
        }
    }

    // Where in the history the view is, while scrolled back.
    if (scroll_ > 0) {
        const float total = static_cast<float>(scrollback_.size()) + static_cast<float>(rows_);
        const float height = io.DisplaySize.y;
        const float thumb = std::max(24.0F, height * static_cast<float>(rows_) / total);
        const float at = (height - thumb) * static_cast<float>(top - first_line()) /
                         std::max(1.0F, static_cast<float>(scrollback_.size()));
        dl->AddRectFilled({io.DisplaySize.x - 5, at}, {io.DisplaySize.x - 1, at + thumb},
                          ui::to_imu32(p.text_dim.with_alpha(0.6F)), 2);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

} // namespace tetra::terminal
