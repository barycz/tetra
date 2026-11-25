#include "terminal.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <print>

#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tetra::terminal {

namespace {

constexpr std::size_t kReadBudget = 1U << 20U; // per wakeup, so the window stays responsive
constexpr auto kFlushRetry = std::chrono::milliseconds(5);

const VTermScreenCallbacks kScreenCallbacks = {
    .damage = &Terminal::on_damage,
    .moverect = nullptr,
    .movecursor = &Terminal::on_move_cursor,
    .settermprop = &Terminal::on_term_prop,
    .bell = &Terminal::on_bell,
    .resize = nullptr,
    .sb_pushline = &Terminal::on_push_line,
    .sb_popline = &Terminal::on_pop_line,
    .sb_clear = &Terminal::on_clear_scrollback,
};

std::string user_shell() {
    if (const char* shell = std::getenv("SHELL"); shell != nullptr && *shell != '\0') { // NOLINT(concurrency-mt-unsafe)
        return shell;
    }
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_shell != nullptr && *pw->pw_shell != '\0') { // NOLINT(concurrency-mt-unsafe)
        return pw->pw_shell;
    }
    return "/bin/sh";
}

} // namespace

Terminal::Terminal(ui::App& app, Options options) : app_(app), options_(std::move(options)) {
    font_size_ = static_cast<float>(app_.config().theme.font_size);

    // A first guess of 80x24 cells; the real grid follows the window.
    const auto width = static_cast<int>((80 * font_size_ * 0.6F) + 8);
    const auto height = static_cast<int>((24 * font_size_ * 1.2F) + 8);
    window_ = std::make_unique<ui::ToplevelWindow>(
        app_, ui::ToplevelWindow::Options{.title = options_.title.empty() ? "Terminal" : options_.title,
                                          .width = width,
                                          .height = height});
    window_->on_load_fonts = [this] { load_fonts(); };
    window_->apply_theme(); // loads the monospace fonts too
    window_->on_draw = [this] { draw(); };
    window_->on_key = [this](const ui::KeyEvent& event) { return handle_key(event); };
    window_->on_close = [this] {
        if (child_ > 0) {
            kill(child_, SIGHUP); // as when a terminal is closed
        }
        app_.quit(0);
    };
    app_.on_config_changed = [this] { window_->request_redraw(); };

    // libvterm's callbacks draw into the window: it exists by now.
    vt_ = vterm_new(rows_, cols_);
    vterm_set_utf8(vt_, 1);
    vterm_output_set_callback(vt_, &Terminal::on_output, this);
    screen_ = vterm_obtain_screen(vt_);
    vterm_screen_set_callbacks(screen_, &kScreenCallbacks, this);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_enable_reflow(screen_, true);
    vterm_screen_reset(screen_, 1);

    if (!spawn(options_.command)) {
        return;
    }
    app_.watch_fd(pty_, [this] { read_pty(); });
}

Terminal::~Terminal() {
    if (flush_timer_) {
        app_.cancel_timer(*flush_timer_);
    }
    if (pty_ >= 0) {
        app_.unwatch_fd(pty_);
        close(pty_);
    }
    if (child_ > 0) {
        kill(child_, SIGHUP);
        waitpid(child_, nullptr, WNOHANG);
    }
    window_.reset();
    vterm_free(vt_);
}

bool Terminal::spawn(const std::vector<std::string>& command) {
    winsize size{.ws_row = static_cast<unsigned short>(rows_), .ws_col = static_cast<unsigned short>(cols_),
                 .ws_xpixel = 0, .ws_ypixel = 0};
    const pid_t pid = forkpty(&pty_, nullptr, nullptr, &size);
    if (pid < 0) {
        std::println(stderr, "tetra-terminal: cannot create a terminal: {}", std::strerror(errno));
        return false;
    }
    if (pid == 0) {
        // The child: a fresh session on the new terminal.
        setenv("TERM", "xterm-256color", 1);   // NOLINT(concurrency-mt-unsafe)
        setenv("COLORTERM", "truecolor", 1);  // NOLINT(concurrency-mt-unsafe)
        setenv("TERM_PROGRAM", "tetra-terminal", 1); // NOLINT(concurrency-mt-unsafe)
        unsetenv("COLUMNS"); // NOLINT(concurrency-mt-unsafe)
        unsetenv("LINES");   // NOLINT(concurrency-mt-unsafe)
        signal(SIGPIPE, SIG_DFL); // NOLINT(cert-err33-c)
        std::vector<std::string> args = command;
        if (args.empty()) {
            args.push_back(user_shell());
        }
        std::vector<char*> argv;
        for (auto& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        std::println(stderr, "tetra-terminal: cannot run {}: {}", args[0], std::strerror(errno));
        _exit(127);
    }
    child_ = pid;
    fcntl(pty_, F_SETFL, fcntl(pty_, F_GETFL) | O_NONBLOCK); // NOLINT(hicpp-signed-bitwise)
    return true;
}

void Terminal::read_pty() {
    std::array<char, 65536> buffer{};
    std::size_t total = 0;
    while (total < kReadBudget) {
        const ssize_t n = read(pty_, buffer.data(), buffer.size());
        if (n > 0) {
            vterm_input_write(vt_, buffer.data(), static_cast<std::size_t>(n));
            total += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            break;
        }
        child_exited(); // EIO: the last process on the terminal is gone
        return;
    }
    if (total > 0) {
        if (scroll_ > 0) {
            // New output while scrolled back: stay on the same lines.
            scroll_ = std::min(scroll_, static_cast<int>(scrollback_.size()));
        }
        window_->request_redraw();
    }
    flush_pty(); // output may have been waiting for the child to read
}

void Terminal::write_pty(const char* data, std::size_t size) {
    pending_output_.append(data, size);
    flush_pty();
}

void Terminal::flush_pty() {
    while (!pending_output_.empty() && pty_ >= 0) {
        const ssize_t n = write(pty_, pending_output_.data(), pending_output_.size());
        if (n > 0) {
            pending_output_.erase(0, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break; // full (EAGAIN) or gone: try again shortly
    }
    if (!pending_output_.empty() && !flush_timer_) {
        flush_timer_ = app_.add_timer(kFlushRetry, [this] {
            flush_timer_.reset();
            flush_pty();
        });
    }
}

void Terminal::child_exited() {
    app_.unwatch_fd(pty_);
    close(pty_);
    pty_ = -1;
    int status = 0;
    int code = 0;
    if (child_ > 0 && waitpid(child_, &status, 0) == child_) {
        code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    child_ = -1;
    app_.quit(code);
}

void Terminal::resize_grid() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    constexpr float kPadding = 4;
    const int cols = std::max(2, static_cast<int>((size.x - (2 * kPadding)) / cell_width_));
    const int rows = std::max(1, static_cast<int>((size.y - (2 * kPadding)) / cell_height_));
    if (cols == cols_ && rows == rows_) {
        return;
    }
    cols_ = cols;
    rows_ = rows;
    vterm_set_size(vt_, rows_, cols_);
    if (pty_ >= 0) {
        const winsize ws{.ws_row = static_cast<unsigned short>(rows_), .ws_col = static_cast<unsigned short>(cols_),
                         .ws_xpixel = static_cast<unsigned short>(cols_ * cell_width_),
                         .ws_ypixel = static_cast<unsigned short>(rows_ * cell_height_)};
        ioctl(pty_, TIOCSWINSZ, &ws); // the kernel tells the child (SIGWINCH)
    }
    scroll_ = std::min(scroll_, static_cast<int>(scrollback_.size()));
}

// --- libvterm callbacks ------------------------------------------------------------------

int Terminal::on_damage(VTermRect /*rect*/, void* user) {
    static_cast<Terminal*>(user)->window_->request_redraw();
    return 1;
}

int Terminal::on_move_cursor(VTermPos pos, VTermPos /*old*/, int visible, void* user) {
    auto* self = static_cast<Terminal*>(user);
    self->cursor_ = pos;
    self->cursor_visible_ = visible != 0;
    self->window_->request_redraw();
    return 1;
}

int Terminal::on_term_prop(VTermProp prop, VTermValue* value, void* user) {
    auto* self = static_cast<Terminal*>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        self->cursor_visible_ = value->boolean != 0;
        break;
    case VTERM_PROP_CURSORSHAPE:
        self->cursor_shape_ = value->number;
        break;
    case VTERM_PROP_ALTSCREEN:
        self->alt_screen_ = value->boolean != 0;
        self->scroll_ = 0;
        break;
    case VTERM_PROP_MOUSE:
        self->mouse_mode_ = value->number;
        break;
    case VTERM_PROP_TITLE:
        // OSC 0/2, possibly in fragments; a fixed --title wins.
        if (value->string.initial) {
            self->title_.clear();
        }
        self->title_.append(value->string.str, value->string.len);
        if (value->string.final && self->options_.title.empty()) {
            self->window_->set_title(self->title_.empty() ? "Terminal" : self->title_);
        }
        break;
    default:
        break;
    }
    self->window_->request_redraw();
    return 1;
}

int Terminal::on_bell(void* /*user*/) {
    return 1; // quiet
}

int Terminal::on_push_line(int cols, const VTermScreenCell* cells, void* user) {
    auto* self = static_cast<Terminal*>(user);
    Line line;
    int used = cols;
    // Trailing blanks take no room in the scrollback.
    while (used > 0 && cells[used - 1].chars[0] == 0 && VTERM_COLOR_IS_DEFAULT_BG(&cells[used - 1].bg)) {
        --used;
    }
    line.reserve(static_cast<std::size_t>(used));
    for (int i = 0; i < used; ++i) {
        const VTermScreenCell& c = cells[i];
        uint8_t attrs = 0;
        attrs |= c.attrs.bold != 0 ? kBold : 0;
        attrs |= c.attrs.italic != 0 ? kItalic : 0;
        attrs |= c.attrs.underline != 0 ? kUnderline : 0;
        attrs |= c.attrs.strike != 0 ? kStrike : 0;
        attrs |= c.attrs.reverse != 0 ? kReverse : 0;
        attrs |= c.attrs.conceal != 0 ? kConceal : 0;
        line.push_back({.ch = c.chars[0], .width = static_cast<uint8_t>(c.width), .attrs = attrs, .fg = c.fg, .bg = c.bg});
    }
    self->scrollback_.push_back(std::move(line));
    if (self->scrollback_.size() > self->scrollback_limit_) {
        self->scrollback_.pop_front();
        ++self->dropped_;
    }
    if (self->scroll_ > 0) {
        ++self->scroll_; // keep showing the same lines
        self->scroll_ = std::min(self->scroll_, static_cast<int>(self->scrollback_.size()));
    }
    return 1;
}

int Terminal::on_pop_line(int cols, VTermScreenCell* cells, void* user) {
    auto* self = static_cast<Terminal*>(user);
    if (self->scrollback_.empty()) {
        return 0;
    }
    const Line line = std::move(self->scrollback_.back());
    self->scrollback_.pop_back();
    VTermColor fg{};
    VTermColor bg{};
    vterm_state_get_default_colors(vterm_obtain_state(self->vt_), &fg, &bg);
    for (int i = 0; i < cols; ++i) {
        VTermScreenCell& c = cells[i];
        c = {};
        if (static_cast<std::size_t>(i) < line.size()) {
            const Cell& s = line[static_cast<std::size_t>(i)];
            c.chars[0] = s.ch;
            c.width = static_cast<char>(s.width);
            c.attrs.bold = (s.attrs & kBold) != 0 ? 1 : 0;
            c.attrs.italic = (s.attrs & kItalic) != 0 ? 1 : 0;
            c.attrs.underline = (s.attrs & kUnderline) != 0 ? 1 : 0;
            c.attrs.strike = (s.attrs & kStrike) != 0 ? 1 : 0;
            c.attrs.reverse = (s.attrs & kReverse) != 0 ? 1 : 0;
            c.attrs.conceal = (s.attrs & kConceal) != 0 ? 1 : 0;
            c.fg = s.fg;
            c.bg = s.bg;
        } else {
            c.width = 1;
            c.fg = fg;
            c.bg = bg;
        }
    }
    self->scroll_ = std::min(self->scroll_, static_cast<int>(self->scrollback_.size()));
    return 1;
}

int Terminal::on_clear_scrollback(void* user) {
    auto* self = static_cast<Terminal*>(user);
    self->dropped_ += static_cast<int64_t>(self->scrollback_.size());
    self->scrollback_.clear();
    self->scroll_ = 0;
    return 1;
}

void Terminal::on_output(const char* bytes, std::size_t size, void* user) {
    static_cast<Terminal*>(user)->write_pty(bytes, size);
}

} // namespace tetra::terminal
