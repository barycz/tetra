// Terminal input: keys, mouse (selection, or reported to the program),
// scrolling, the clipboard and zoom.

#include "terminal.hpp"

#include "clipboard.hpp"

#include <imgui.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>

#include <unistd.h>

namespace tetra::terminal {

namespace {

constexpr float kPadding = 4; // as in render.cpp
constexpr int kWheelLines = 3;
constexpr float kMinFontSize = 6;
constexpr float kMaxFontSize = 48;

std::optional<VTermKey> special_key(uint32_t sym) {
    switch (sym) {
    case XKB_KEY_Return: return VTERM_KEY_ENTER;
    case XKB_KEY_KP_Enter: return VTERM_KEY_KP_ENTER;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: return VTERM_KEY_TAB;
    case XKB_KEY_BackSpace: return VTERM_KEY_BACKSPACE;
    case XKB_KEY_Escape: return VTERM_KEY_ESCAPE;
    case XKB_KEY_Up: case XKB_KEY_KP_Up: return VTERM_KEY_UP;
    case XKB_KEY_Down: case XKB_KEY_KP_Down: return VTERM_KEY_DOWN;
    case XKB_KEY_Left: case XKB_KEY_KP_Left: return VTERM_KEY_LEFT;
    case XKB_KEY_Right: case XKB_KEY_KP_Right: return VTERM_KEY_RIGHT;
    case XKB_KEY_Insert: case XKB_KEY_KP_Insert: return VTERM_KEY_INS;
    case XKB_KEY_Delete: case XKB_KEY_KP_Delete: return VTERM_KEY_DEL;
    case XKB_KEY_Home: case XKB_KEY_KP_Home: return VTERM_KEY_HOME;
    case XKB_KEY_End: case XKB_KEY_KP_End: return VTERM_KEY_END;
    case XKB_KEY_Page_Up: case XKB_KEY_KP_Page_Up: return VTERM_KEY_PAGEUP;
    case XKB_KEY_Page_Down: case XKB_KEY_KP_Page_Down: return VTERM_KEY_PAGEDOWN;
    default: break;
    }
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F35) {
        return static_cast<VTermKey>(VTERM_KEY_FUNCTION(static_cast<int>(sym - XKB_KEY_F1) + 1));
    }
    return std::nullopt;
}

VTermModifier modifiers(bool ctrl, bool shift, bool alt) {
    int mod = VTERM_MOD_NONE;
    mod |= ctrl ? VTERM_MOD_CTRL : 0;
    mod |= shift ? VTERM_MOD_SHIFT : 0;
    mod |= alt ? VTERM_MOD_ALT : 0;
    return static_cast<VTermModifier>(mod);
}

bool is_word(char32_t c) {
    return c > ' ' && c != '"' && c != '\'' && c != '`' && c != '(' && c != ')' && c != '[' && c != ']' &&
           c != '{' && c != '}' && c != '<' && c != '>' && c != '|' && c != ';' && c != ',';
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

} // namespace

bool Terminal::handle_key(const ui::KeyEvent& event) {
    const bool ctrl_shift = event.ctrl && event.shift && !event.alt;
    const uint32_t lower = xkb_keysym_to_lower(event.keysym);
    // The terminal's own shortcuts.
    if (ctrl_shift && lower == XKB_KEY_c) {
        copy_selection(true);
        return true;
    }
    if (ctrl_shift && lower == XKB_KEY_v) {
        if (ui::Clipboard* clipboard = app_.clipboard()) {
            paste(clipboard->text(ui::Clipboard::Selection::Clipboard));
        }
        return true;
    }
    if (ctrl_shift && lower == XKB_KEY_n) {
        open_new_window();
        return true;
    }
    if (event.shift && !event.ctrl && event.keysym == XKB_KEY_Insert) {
        if (ui::Clipboard* clipboard = app_.clipboard()) {
            paste(clipboard->text(ui::Clipboard::Selection::Primary));
        }
        return true;
    }
    if (event.shift && !event.ctrl && !alt_screen_ &&
        (event.keysym == XKB_KEY_Page_Up || event.keysym == XKB_KEY_Page_Down)) {
        scroll_by(event.keysym == XKB_KEY_Page_Up ? rows_ - 1 : -(rows_ - 1));
        return true;
    }
    if (event.ctrl && !event.alt &&
        (event.keysym == XKB_KEY_equal || event.keysym == XKB_KEY_plus || event.keysym == XKB_KEY_KP_Add)) {
        zoom(1);
        return true;
    }
    if (event.ctrl && !event.alt && (event.keysym == XKB_KEY_minus || event.keysym == XKB_KEY_KP_Subtract)) {
        zoom(-1);
        return true;
    }
    if (event.ctrl && !event.alt && !event.shift && event.keysym == XKB_KEY_0) {
        zoom(0);
        return true;
    }

    // Everything else goes to the program.
    if (const auto key = special_key(event.keysym)) {
        vterm_keyboard_key(vt_, *key, modifiers(event.ctrl, event.shift, event.alt));
    } else if (event.codepoint != 0) {
        // The character already carries Shift (A, !); Ctrl and Alt still apply.
        const bool printable = event.codepoint >= 0x20;
        vterm_keyboard_unichar(vt_, event.codepoint, modifiers(event.ctrl, event.shift && !printable, event.alt));
    } else {
        return false; // modifiers alone, media keys: nothing to send
    }
    scroll_ = 0; // typing brings the view back to the bottom
    window_->request_redraw();
    return true;
}

std::optional<Terminal::Point> Terminal::cell_at(float x, float y) const {
    const int col = std::clamp(static_cast<int>(std::floor((x - kPadding) / cell_width_)), 0, cols_ - 1);
    const int row = std::clamp(static_cast<int>(std::floor((y - kPadding) / cell_height_)), 0, rows_ - 1);
    return Point{.line = screen_top() - scroll_ + row, .col = col};
}

void Terminal::handle_mouse() {
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsWindowHovered() && !selecting_ && mouse_button_held_ == 0) {
        return;
    }
    const auto at = cell_at(io.MousePos.x, io.MousePos.y);
    if (!at) {
        return;
    }
    const int row = static_cast<int>(at->line - (screen_top() - scroll_));
    const VTermModifier mod = modifiers(io.KeyCtrl, false, io.KeyAlt);

    // Programs that ask for the mouse get it, unless Shift is held.
    const bool report = mouse_mode_ != VTERM_PROP_MOUSE_NONE && !io.KeyShift && scroll_ == 0;
    if (report) {
        vterm_mouse_move(vt_, row, at->col, mod);
        for (int button = 0; button < 3; ++button) {
            // ImGui: 0 left, 1 right, 2 middle; terminals: 1 left, 2 middle, 3 right.
            const int vterm_button = button == 0 ? 1 : (button == 1 ? 3 : 2);
            if (ImGui::IsMouseClicked(button)) {
                vterm_mouse_button(vt_, vterm_button, true, mod);
                mouse_button_held_ = vterm_button;
            }
            if (ImGui::IsMouseReleased(button)) {
                vterm_mouse_button(vt_, vterm_button, false, mod);
                mouse_button_held_ = 0;
            }
        }
        if (io.MouseWheel != 0) {
            const int clicks = static_cast<int>(std::lround(std::abs(io.MouseWheel)));
            for (int i = 0; i < std::max(1, clicks); ++i) {
                vterm_mouse_button(vt_, io.MouseWheel > 0 ? 4 : 5, true, mod);
            }
        }
        return;
    }

    // The wheel: through the history, or as arrow keys in full-screen programs.
    if (io.MouseWheel != 0) {
        const int lines = static_cast<int>(std::lround(io.MouseWheel * kWheelLines));
        if (alt_screen_) {
            for (int i = 0; i < std::abs(lines); ++i) {
                vterm_keyboard_key(vt_, lines > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
            }
        } else {
            scroll_by(lines);
        }
    }

    // Selecting text.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
        if (clicks == 2) {
            select_word(*at);
            copy_selection(false);
        } else if (clicks >= 3) {
            anchor_ = Point{.line = at->line, .col = 0};
            head_ = Point{.line = at->line, .col = cols_ - 1};
            copy_selection(false);
        } else {
            anchor_ = *at;
            head_.reset();
            selecting_ = true;
        }
        window_->request_redraw();
    }
    if (selecting_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) {
        head_ = *at;
        // Dragging past the top or bottom scrolls.
        if (io.MousePos.y < 0) {
            scroll_by(1);
        } else if (io.MousePos.y > io.DisplaySize.y) {
            scroll_by(-1);
        }
        window_->request_redraw();
    }
    if (selecting_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        selecting_ = false;
        if (head_) {
            copy_selection(false); // selected text is the primary selection
        } else {
            anchor_.reset(); // a plain click clears the selection
        }
        window_->request_redraw();
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        if (ui::Clipboard* clipboard = app_.clipboard()) {
            paste(clipboard->text(ui::Clipboard::Selection::Primary));
        }
    }
}

bool Terminal::selected(int64_t line, int col) const {
    if (!anchor_ || !head_) {
        return false;
    }
    const Point a = std::min(*anchor_, *head_);
    const Point b = std::max(*anchor_, *head_);
    const Point p{.line = line, .col = col};
    return p >= a && p <= b;
}

std::string Terminal::selected_text() const {
    if (!anchor_ || !head_) {
        return {};
    }
    const Point a = std::min(*anchor_, *head_);
    const Point b = std::max(*anchor_, *head_);
    std::string text;
    Line scratch;
    for (int64_t index = a.line; index <= b.line; ++index) {
        const Line* line = history_line(index, scratch);
        std::string row;
        if (line != nullptr) {
            const int from = index == a.line ? a.col : 0;
            const int to = index == b.line ? b.col : cols_ - 1;
            for (int col = from; col <= to && static_cast<std::size_t>(col) < line->size(); ++col) {
                const char32_t c = (*line)[static_cast<std::size_t>(col)].ch;
                if (c == 0xFFFFFFFF) {
                    continue; // second half of a wide character
                }
                append_utf8(row, c == 0 ? U' ' : c);
            }
        }
        // Trailing blanks are layout, not text.
        while (!row.empty() && row.back() == ' ') {
            row.pop_back();
        }
        text += row;
        if (index != b.line) {
            text += '\n';
        }
    }
    return text;
}

void Terminal::select_word(Point at) {
    Line scratch;
    const Line* line = history_line(at.line, scratch);
    if (line == nullptr) {
        return;
    }
    auto char_at = [&](int col) {
        return static_cast<std::size_t>(col) < line->size() ? (*line)[static_cast<std::size_t>(col)].ch : U' ';
    };
    if (!is_word(char_at(at.col))) {
        anchor_ = at;
        head_ = at;
        return;
    }
    int from = at.col;
    int to = at.col;
    while (from > 0 && is_word(char_at(from - 1))) {
        --from;
    }
    while (to + 1 < cols_ && is_word(char_at(to + 1))) {
        ++to;
    }
    anchor_ = Point{.line = at.line, .col = from};
    head_ = Point{.line = at.line, .col = to};
}

void Terminal::copy_selection(bool to_clipboard) {
    ui::Clipboard* clipboard = app_.clipboard();
    const std::string text = selected_text();
    if (clipboard == nullptr || text.empty()) {
        return;
    }
    clipboard->set(to_clipboard ? ui::Clipboard::Selection::Clipboard : ui::Clipboard::Selection::Primary, text,
                   app_.input_serial());
}

void Terminal::paste(const std::string& text) {
    if (text.empty()) {
        return;
    }
    // Lines end in CR, as if typed; bracketed paste markers if the program
    // asked for them (so a pasted line does not run by itself).
    std::string data;
    data.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        data += text[i] == '\n' ? '\r' : text[i];
    }
    vterm_keyboard_start_paste(vt_);
    write_pty(data.data(), data.size());
    vterm_keyboard_end_paste(vt_);
    scroll_ = 0;
    window_->request_redraw();
}

void Terminal::scroll_by(int lines) {
    scroll_ = std::clamp(scroll_ + lines, 0, static_cast<int>(scrollback_.size()));
    window_->request_redraw();
}

void Terminal::zoom(int steps) {
    const auto base = static_cast<float>(app_.config().theme.font_size);
    font_size_ = steps == 0 ? base : std::clamp(font_size_ + static_cast<float>(steps), kMinFontSize, kMaxFontSize);
    measure();
    window_->request_redraw();
}

void Terminal::open_new_window() const {
    // Another tetra-terminal, in the directory the shell is in now.
    const pid_t pid = fork();
    if (pid != 0) {
        return;
    }
    setsid();
    if (child_ > 0) {
        const std::string cwd = "/proc/" + std::to_string(child_) + "/cwd";
        [[maybe_unused]] const int ignored = chdir(cwd.c_str());
    }
    std::array<char, PATH_MAX> self{};
    const ssize_t n = readlink("/proc/self/exe", self.data(), self.size() - 1);
    if (n > 0) {
        execl(self.data(), self.data(), static_cast<char*>(nullptr));
    }
    _exit(127);
}

} // namespace tetra::terminal
