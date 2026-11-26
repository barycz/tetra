#pragma once

#include <wayland-client.h>

#include "app.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct wl_data_device;
struct wl_data_device_manager;
struct wl_data_offer;
struct wl_data_source;
struct wl_shm;
struct zwlr_screencopy_frame_v1;
struct zwlr_screencopy_manager_v1;

namespace tetra::ui {
class LayerWindow;
class Texture;
}

namespace tetra::shell {

class Shell;

// Screenshots (Print and friends, sent by the compositor): captures the
// output with wlr-screencopy, crops it (window, or a region the user drags
// on a frozen copy of the screen), saves a PNG under Pictures/Screenshots,
// puts it on the clipboard and shows a notification.
class Screenshots {
public:
    explicit Screenshots(Shell& shell);
    ~Screenshots();

    Screenshots(const Screenshots&) = delete;
    Screenshots& operator=(const Screenshots&) = delete;
    Screenshots(Screenshots&&) = delete;
    Screenshots& operator=(Screenshots&&) = delete;

    // Arguments of tetra_shell_v1.screenshot.
    void take(uint32_t serial, uint32_t mode, wl_output* output, int x, int y, int width, int height);

    // For the screenshot portal (org.tetra.Shell1): asks the compositor for
    // the screenshot Print (or, if interactive, Shift+Print) would take.
    // `done` gets the saved file, or nothing if the user cancelled or it
    // failed. It is not put on the clipboard.
    using Done = std::function<void(std::optional<std::filesystem::path>)>;
    void request(bool interactive, Done done);
    // For the portal's PickColor: the user clicks a pixel of the output under
    // the pointer (frozen, with a magnifier); `done` gets its colour as
    // R, G, B in 0..1, or nothing if cancelled.
    using ColorDone = std::function<void(std::optional<std::array<double, 3>>)>;
    void pick_color(ColorDone done);

    // Where screenshots go: $XDG_PICTURES_DIR (or user-dirs.dirs, or
    // ~/Pictures) + /Screenshots.
    static std::filesystem::path directory();

private:
    friend struct ScreencopyListener;
    friend struct DataSourceListener;
    friend struct DataDeviceListener;

    struct Rect {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };
    // A captured output: 32-bit pixels, B G R X in memory (cairo's RGB24).
    struct Image {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> pixels;
    };
    struct Capture {
        Screenshots* owner = nullptr;
        zwlr_screencopy_frame_v1* frame = nullptr;
        uint32_t format = UINT32_MAX;
        int width = 0;
        int height = 0;
        int stride = 0;
        bool y_invert = false;
        int fd = -1;
        void* data = nullptr;
        std::size_t size = 0;
        wl_buffer* buffer = nullptr;
        // The request.
        uint32_t serial = 0;
        uint32_t mode = 0;
        wl_output* output = nullptr;
        Rect box;        // logical, relative to the output
        Done done;       // a portal request
        ColorDone color; // a portal colour request
    };
    class RegionPicker;

    void copy_frame(Capture& capture);
    void captured(Capture& capture);
    void end_capture();
    void crop_and_save(const Image& image, Rect pixels, uint32_t serial, Done done);
    void fail_pending();
    void set_clipboard(std::shared_ptr<const std::vector<uint8_t>> png, uint32_t serial);
    void close_picker();

    Shell& shell_;
    zwlr_screencopy_manager_v1* screencopy_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_data_device_manager* data_device_manager_ = nullptr;
    wl_data_device* data_device_ = nullptr;
    wl_data_source* source_ = nullptr; // our current clipboard offer
    wl_data_offer* offer_ = nullptr;   // the latest offer to us (never read)
    std::shared_ptr<const std::vector<uint8_t>> clipboard_;
    std::unique_ptr<Capture> capture_;
    std::unique_ptr<RegionPicker> picker_;
    // A portal request waiting for the compositor's screenshot event.
    Done pending_;
    ColorDone color_pending_;
    std::optional<ui::App::TimerId> pending_timer_;
};

} // namespace tetra::shell
