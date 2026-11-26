#pragma once

#include "app.hpp"
#include "icons.hpp"
#include "sampler.hpp"
#include "window.hpp"

#include <imgui.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tetra::monitor {

// tetra-monitor: processes (with ending and killing them), CPU, memory,
// network and disk use over time, and file system usage.
class Monitor {
public:
    explicit Monitor(ui::App& app);

    // "processes", "resources" or "filesystems".
    void show_page(std::string_view id);
    // Starts with this text in the process search.
    void set_search(std::string_view text);

private:
    enum class Page { Processes, Resources, FileSystems };
    struct PageInfo {
        Page page;
        const char* id;
        const char* title;
        const char* icon;
    };
    static const std::vector<PageInfo>& pages();

    // A process the user picked: pid and start time (pids are reused).
    struct Pick {
        int pid = 0;
        uint64_t start_time = 0;
        std::string name;
        bool operator==(const Pick& o) const { return pid == o.pid && start_time == o.start_time; }
    };
    // A signal waiting for the user to confirm it.
    struct Confirm {
        Pick target;
        int signal = 0;
    };

    void tick();
    void schedule();
    void draw();
    void draw_sidebar(float width, float height);
    void draw_message();
    void show_message(std::string text);

    // Processes (processes_page.cpp).
    void page_processes();
    void process_toolbar();
    void process_table(float height);
    void process_menu(const ProcessRow& row);
    void process_footer();
    void confirm_popup();
    void send_signal(const Pick& target, int signal);
    [[nodiscard]] bool matches(const ProcessRow& row) const;
    [[nodiscard]] const ProcessRow* selected_row() const;

    // Resources and file systems (resources_page.cpp).
    void page_resources();
    void page_filesystems();
    struct Series {
        const History* history;
        ImU32 color;
    };
    // A graph of the last History::kLength samples, newest on the right;
    // `max` is the top of the scale.
    void graph(const char* id, ImVec2 size, const std::vector<Series>& series, double max, const char* top_label);
    void legend(ImU32 color, const char* label, const std::string& value);
    void begin_card(const char* title, const std::string& summary = {});
    void end_card();

    ui::App& app_;
    ui::IconCache icons_;
    Sampler sampler_;
    std::unique_ptr<ui::ToplevelWindow> window_;
    Page page_ = Page::Processes;

    // Sampling.
    static constexpr std::array<int, 4> kIntervals = {500, 1000, 2000, 5000}; // ms
    int interval_ = 1000;                                                    // 0: paused
    std::optional<ui::App::TimerId> timer_;
    std::chrono::steady_clock::time_point last_sample_;

    // Processes.
    std::array<char, 256> search_{};
    bool all_users_ = true;
    bool tree_ = false;
    std::optional<Pick> selected_;
    std::optional<Confirm> confirm_;
    std::vector<Pick> order_; // the rows as last drawn, for Up/Down
    int shown_ = 0;           // rows matching the search
    bool scroll_to_selected_ = false;
    bool focus_search_ = false;
    bool search_active_ = false; // the search field has the keyboard
    std::string typed_;          // typed before the search field took the keyboard

    // A message shown for a few seconds (e.g. "not allowed").
    std::string message_;
    std::optional<ui::App::TimerId> message_timer_;
};

// Byte sizes and rates for people: "1.5 GB", "12 kB/s".
[[nodiscard]] std::string format_bytes(double bytes);
[[nodiscard]] std::string format_rate(double bytes_per_second);

} // namespace tetra::monitor
