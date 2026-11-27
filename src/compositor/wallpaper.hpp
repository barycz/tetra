#pragma once

#include "config.hpp"
#include "wlr.hpp"

#include <cairo.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace tetra {

class Server;

// What the desktop shows behind the windows (desktop.wallpaper): the tetra
// mark drawn for each display in the accent colour, a picture, or nothing
// but the background colour (the outputs' scene rect).
//
// Pictures are decoded (gdk-pixbuf) on a worker thread, scaled down to what
// the largest display needs, so a big photo never stalls input; until it is
// ready the colour shows.
class Wallpaper {
public:
    explicit Wallpaper(Server& server);
    ~Wallpaper();

    Wallpaper(const Wallpaper&) = delete;
    Wallpaper& operator=(const Wallpaper&) = delete;
    Wallpaper(Wallpaper&&) = delete;
    Wallpaper& operator=(Wallpaper&&) = delete;

    // Re-reads the config (desktop.*, theme.accent); redraws every display
    // when what it shows changed.
    void update();
    // A display of `width` x `height` pixels shows it: loads the picture
    // (again, larger) if it does not cover that yet.
    void ensure_covers(int width, int height);
    // Draws it for an area of `width` x `height` layout pixels at `scale`.
    // Null when the colour alone is shown (or the picture is not ready);
    // otherwise the caller owns the returned reference.
    [[nodiscard]] wlr_buffer* render(int width, int height, float scale) const;
    // Changes whenever render() would draw something else.
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

private:
    struct Request {
        std::uint64_t id = 0;
        std::string path;
        int width = 0; // pixels the decoded picture has to cover
        int height = 0;
        config::Desktop::Fit fit = config::Desktop::Fit::Fill;
    };
    struct Result {
        std::uint64_t id = 0;
        cairo_surface_t* image = nullptr; // null: failed
        std::string error;
    };

    void request(int width, int height);
    void take_result();
    void redraw_outputs();
    void drop_picture();
    void worker_loop(const std::stop_token& stop);
    static cairo_surface_t* load(const Request& request, std::string& error);

    Server& server_;
    config::Desktop desktop_;
    config::Color accent_;
    std::uint64_t generation_ = 1;

    // The picture shown (main thread only).
    cairo_surface_t* picture_ = nullptr;
    std::string picture_path_;            // what picture_ is (or is being loaded)
    config::Desktop::Fit picture_fit_{};  // ... for which fit
    int target_width_ = 0;                // ... decoded to cover this many pixels
    int target_height_ = 0;
    std::uint64_t request_id_ = 0;        // the latest request

    // Shared with the worker.
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Request> pending_;
    std::optional<Result> result_;
    int event_fd_ = -1; // the worker's "result ready" for the event loop
    wl_event_source* event_source_ = nullptr;
    std::jthread worker_; // last: started after everything above exists
};

} // namespace tetra
