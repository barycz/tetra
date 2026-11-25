#pragma once

#include "app.hpp"
#include "window.hpp"

#include <vterm.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sys/types.h>

struct ImFont;

namespace tetra::terminal {

// One terminal window: a child process on a pseudo-terminal, its screen
// kept by libvterm, drawn with ImGui on a fixed cell grid.
class Terminal {
public:
    struct Options {
        std::vector<std::string> command; // empty: the user's shell
        std::string title;                // empty: the program's own
    };

    Terminal(ui::App& app, Options options);
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;

    // False if the child could not be started (the reason is printed).
    [[nodiscard]] bool started() const noexcept { return child_ > 0; }

    // libvterm callbacks.
    static int on_damage(VTermRect rect, void* user);
    static int on_move_cursor(VTermPos pos, VTermPos old, int visible, void* user);
    static int on_term_prop(VTermProp prop, VTermValue* value, void* user);
    static int on_bell(void* user);
    static int on_push_line(int cols, const VTermScreenCell* cells, void* user);
    static int on_pop_line(int cols, VTermScreenCell* cells, void* user);
    static int on_clear_scrollback(void* user);
    static void on_output(const char* bytes, std::size_t size, void* user);

private:
    // A cell kept in the scrollback (the screen itself stays in libvterm).
    struct Cell {
        char32_t ch = 0;
        uint8_t width = 1;
        uint8_t attrs = 0; // Attr bits
        VTermColor fg{};
        VTermColor bg{};
    };
    enum Attr : uint8_t {
        kBold = 1, kItalic = 2, kUnderline = 4, kStrike = 8, kReverse = 16, kConceal = 32,
    };
    using Line = std::vector<Cell>;
    // A position in the whole history: scrollback lines first, then the
    // screen; counted from the first line ever (so it survives trimming).
    struct Point {
        int64_t line = 0;
        int col = 0;
        auto operator<=>(const Point&) const = default;
    };

    bool spawn(const std::vector<std::string>& command);
    void read_pty();
    void write_pty(const char* data, std::size_t size);
    void flush_pty();
    void child_exited();
    void resize_grid();

    // Drawing (render.cpp).
    void draw();
    void measure();
    [[nodiscard]] Line screen_line(int row) const;
    [[nodiscard]] const Line* history_line(int64_t line, Line& scratch) const;
    [[nodiscard]] uint32_t color(const VTermColor& c, bool foreground) const;
    void load_fonts();

    // Input (input.cpp).
    bool handle_key(const ui::KeyEvent& event);
    void handle_mouse();
    void paste(const std::string& text);
    void scroll_by(int lines);
    [[nodiscard]] std::optional<Point> cell_at(float x, float y) const;
    [[nodiscard]] std::string selected_text() const;
    void select_word(Point at);
    void copy_selection(bool to_clipboard);
    [[nodiscard]] bool selected(int64_t line, int col) const;
    void zoom(int steps);
    void open_new_window() const;

    [[nodiscard]] int64_t first_line() const noexcept { return dropped_; }
    [[nodiscard]] int64_t screen_top() const noexcept {
        return dropped_ + static_cast<int64_t>(scrollback_.size());
    }

    ui::App& app_;
    Options options_;
    std::unique_ptr<ui::ToplevelWindow> window_;

    // The child.
    pid_t child_ = -1;
    int pty_ = -1;
    std::string pending_output_; // to the child, not yet written
    std::optional<ui::App::TimerId> flush_timer_;

    // libvterm.
    VTerm* vt_ = nullptr;
    VTermScreen* screen_ = nullptr;
    int rows_ = 24;
    int cols_ = 80;
    VTermPos cursor_{};
    bool cursor_visible_ = true;
    int cursor_shape_ = VTERM_PROP_CURSORSHAPE_BLOCK;
    bool alt_screen_ = false;
    int mouse_mode_ = VTERM_PROP_MOUSE_NONE;
    std::string title_; // being assembled from fragments
    bool focused_ = false;

    // Scrollback.
    std::deque<Line> scrollback_;
    std::size_t scrollback_limit_ = 10000;
    int64_t dropped_ = 0; // lines trimmed off the top so far
    int scroll_ = 0;      // lines scrolled back from the bottom

    // Selection, in history coordinates.
    std::optional<Point> anchor_;
    std::optional<Point> head_;
    bool selecting_ = false;
    int mouse_button_held_ = 0; // a button reported to the program

    // Fonts and the cell grid.
    ImFont* regular_ = nullptr;
    ImFont* bold_ = nullptr;
    ImFont* italic_ = nullptr;
    float font_size_ = 14;
    float cell_width_ = 8;
    float cell_height_ = 16;
    float ascent_ = 12;
};

} // namespace tetra::terminal
