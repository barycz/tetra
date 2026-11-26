#include "screenshot.hpp"

#include "process.hpp"
#include "protocols.hpp"
#include "shell.hpp"
#include "style.hpp"
#include "texture.hpp"
#include "window.hpp"

#include "tetra-shell-unstable-v1-protocol.h"

#include <cairo.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <fstream>
#include <print>
#include <string>
#include <thread>

#include <sys/mman.h>
#include <unistd.h>

namespace tetra::shell {

namespace fs = std::filesystem;

namespace {

constexpr const char* kPngMime = "image/png";

// Pixel layouts we can read: little-endian 32-bit words.
bool is_bgrx(uint32_t format) {
    return format == WL_SHM_FORMAT_ARGB8888 || format == WL_SHM_FORMAT_XRGB8888;
}
bool is_rgbx(uint32_t format) {
    return format == WL_SHM_FORMAT_ABGR8888 || format == WL_SHM_FORMAT_XBGR8888;
}

cairo_status_t append_png(void* closure, const unsigned char* data, unsigned int length) {
    auto* out = static_cast<std::vector<uint8_t>*>(closure);
    out->insert(out->end(), data, data + length); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return CAIRO_STATUS_SUCCESS;
}

std::string home() {
    const char* env = std::getenv("HOME"); // NOLINT(concurrency-mt-unsafe)
    return env != nullptr ? env : "";
}

// XDG_PICTURES_DIR from ~/.config/user-dirs.dirs ("$HOME/Pictures").
fs::path pictures_from_user_dirs() {
    const char* config_env = std::getenv("XDG_CONFIG_HOME"); // NOLINT(concurrency-mt-unsafe)
    const fs::path config = config_env != nullptr && *config_env != '\0'
                                ? fs::path(config_env)
                                : fs::path(home()) / ".config";
    std::ifstream file(config / "user-dirs.dirs");
    std::string line;
    constexpr std::string_view kKey = "XDG_PICTURES_DIR=";
    while (std::getline(file, line)) {
        if (!line.starts_with(kKey)) {
            continue;
        }
        std::string value = line.substr(kKey.size());
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (value.starts_with("$HOME")) {
            value = home() + value.substr(5);
        }
        return value;
    }
    return {};
}

fs::path unique_path(const fs::path& dir) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 32> stamp{};
    const std::size_t len = std::strftime(stamp.data(), stamp.size(), "%Y-%m-%d %H-%M-%S", &local);
    const std::string base = "Screenshot " + std::string(stamp.data(), len);
    fs::path path = dir / (base + ".png");
    for (int i = 2; fs::exists(path); ++i) {
        path = dir / std::format("{} ({}).png", base, i);
    }
    return path;
}

} // namespace

// We never paste: keep only the latest offer (the protocol pairs data_offer
// with a selection or enter event that follows) and drop it after.
struct DataDeviceListener {
    static const wl_data_device_listener listener;
    static void replace(void* data, wl_data_offer* offer) {
        auto*& current = static_cast<Screenshots*>(data)->offer_;
        if (current != nullptr && current != offer) {
            wl_data_offer_destroy(current);
        }
        current = offer;
    }
};

const wl_data_device_listener DataDeviceListener::listener = {
    .data_offer = [](void* data, wl_data_device* /*device*/,
                     wl_data_offer* offer) { DataDeviceListener::replace(data, offer); },
    .enter = [](void* /*data*/, wl_data_device* /*device*/, uint32_t /*serial*/,
                wl_surface* /*surface*/, wl_fixed_t /*x*/, wl_fixed_t /*y*/,
                wl_data_offer* /*offer*/) {},
    .leave = [](void* /*data*/, wl_data_device* /*device*/) {},
    .motion = [](void* /*data*/, wl_data_device* /*device*/, uint32_t /*time*/, wl_fixed_t /*x*/,
                 wl_fixed_t /*y*/) {},
    .drop = [](void* /*data*/, wl_data_device* /*device*/) {},
    .selection = [](void* data, wl_data_device* /*device*/,
                    wl_data_offer* /*offer*/) { DataDeviceListener::replace(data, nullptr); },
};

// --- the region picker -----------------------------------------------------------------

// A frozen copy of the output, full screen; the user drags a rectangle.
class Screenshots::RegionPicker {
public:
    using Done = std::function<void(const Image& image, std::optional<Rect> pixels)>;

    RegionPicker(Shell& shell, wl_output* output, Image image, Done done)
        : shell_(shell), image_(std::move(image)), done_(std::move(done)) {
        window_ = std::make_unique<ui::LayerWindow>(
            shell_.app(), ui::LayerWindow::Options{
                              .layer = ui::LayerWindow::Layer::Overlay,
                              .anchor = ui::LayerWindow::kTop | ui::LayerWindow::kBottom |
                                        ui::LayerWindow::kLeft | ui::LayerWindow::kRight,
                              .width = 0,
                              .height = 0,
                              .exclusive_zone = -1,
                              .keyboard = ui::LayerWindow::Keyboard::Exclusive,
                              .name_space = "tetra-screenshot",
                              .output = output,
                          });
        window_->on_draw = [this] { draw(); };
        window_->on_closed = [this] { finish(std::nullopt); };
    }

private:
    void finish(std::optional<Rect> pixels) {
        if (done_) {
            auto done = std::move(done_);
            done_ = nullptr;
            done(image_, pixels); // may destroy *this (deferred)
        }
    }

    void draw() {
        const auto& p = shell_.app().palette();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 size = viewport->Size;
        if (!texture_) {
            // B G R X -> R G B A for the GL texture.
            std::vector<uint8_t> rgba(image_.pixels.size());
            for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
                rgba[i] = image_.pixels[i + 2];
                rgba[i + 1] = image_.pixels[i + 1];
                rgba[i + 2] = image_.pixels[i];
                rgba[i + 3] = 255;
            }
            texture_ = std::make_unique<ui::Texture>(image_.width, image_.height, image_.width * 4,
                                                     4, rgba);
        }

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("##screenshot", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddImage(texture_->ref(), {0, 0}, size);

        // Mouse: press, drag, release.
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            start_ = mouse;
            end_ = mouse;
            dragging_ = true;
        } else if (dragging_) {
            end_ = mouse;
        }
        const ImVec2 min{std::round(std::min(start_.x, end_.x)), std::round(std::min(start_.y, end_.y))};
        const ImVec2 max{std::round(std::max(start_.x, end_.x)), std::round(std::max(start_.y, end_.y))};
        const bool has_rect = dragging_ && max.x - min.x >= 1 && max.y - min.y >= 1;

        // Dim everything but the selection.
        const ImU32 dim = ui::to_imu32(config::Color{0, 0, 0, 0.45F});
        if (has_rect) {
            dl->AddRectFilled({0, 0}, {size.x, min.y}, dim);
            dl->AddRectFilled({0, max.y}, {size.x, size.y}, dim);
            dl->AddRectFilled({0, min.y}, {min.x, max.y}, dim);
            dl->AddRectFilled({max.x, min.y}, {size.x, max.y}, dim);
            dl->AddRect({min.x - 1, min.y - 1}, {max.x + 1, max.y + 1}, ui::to_imu32(p.accent),
                        0.0F, 2.0F);
            const float ratio = static_cast<float>(image_.width) / size.x;
            const std::string label =
                std::format("{} × {}", std::lround((max.x - min.x) * ratio),
                            std::lround((max.y - min.y) * ratio));
            const ImVec2 text = ImGui::CalcTextSize(label.c_str());
            ImVec2 at{min.x, max.y + 6};
            if (at.y + text.y + 8 > size.y) {
                at.y = min.y - text.y - 14;
            }
            dl->AddRectFilled({at.x, at.y}, {at.x + text.x + 12, at.y + text.y + 6},
                              ui::to_imu32(p.surface), 4);
            dl->AddText({at.x + 6, at.y + 3}, ui::to_imu32(p.text), label.c_str());
        } else {
            dl->AddRectFilled({0, 0}, size, dim);
        }

        // Hint, top centre.
        const char* hint = "Drag to capture an area · Esc to cancel";
        const ImVec2 hint_size = ImGui::CalcTextSize(hint);
        const ImVec2 hint_at{std::round((size.x - hint_size.x) / 2), 24};
        dl->AddRectFilled({hint_at.x - 14, hint_at.y - 8},
                          {hint_at.x + hint_size.x + 14, hint_at.y + hint_size.y + 8},
                          ui::to_imu32(p.surface), 8);
        dl->AddText(hint_at, ui::to_imu32(p.text), hint);

        ImGui::End();
        ImGui::PopStyleVar(2);

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            finish(std::nullopt);
        } else if (dragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            dragging_ = false;
            if (max.x - min.x >= 4 && max.y - min.y >= 4) {
                const float ratio = static_cast<float>(image_.width) / size.x;
                finish(Rect{.x = static_cast<int>(std::lround(min.x * ratio)),
                            .y = static_cast<int>(std::lround(min.y * ratio)),
                            .width = static_cast<int>(std::lround((max.x - min.x) * ratio)),
                            .height = static_cast<int>(std::lround((max.y - min.y) * ratio))});
            }
            // A plain click selects nothing: keep picking.
        }
    }

    Shell& shell_;
    Image image_;
    Done done_;
    std::unique_ptr<ui::Texture> texture_;
    std::unique_ptr<ui::LayerWindow> window_;
    ImVec2 start_{};
    ImVec2 end_{};
    bool dragging_ = false;
};

// --- screencopy ------------------------------------------------------------------------

struct ScreencopyListener {
    static const zwlr_screencopy_frame_v1_listener listener;
};

const zwlr_screencopy_frame_v1_listener ScreencopyListener::listener = {
    .buffer =
        [](void* data, zwlr_screencopy_frame_v1* /*frame*/, uint32_t format, uint32_t width,
           uint32_t height, uint32_t stride) {
            auto* c = static_cast<Screenshots::Capture*>(data);
            const bool usable = is_bgrx(format) || is_rgbx(format);
            // Prefer B G R X (no conversion); take R G B X otherwise.
            if (usable && (c->format == UINT32_MAX || (is_bgrx(format) && !is_bgrx(c->format)))) {
                c->format = format;
                c->width = static_cast<int>(width);
                c->height = static_cast<int>(height);
                c->stride = static_cast<int>(stride);
            }
            if (zwlr_screencopy_frame_v1_get_version(c->frame) < 3) {
                c->owner->copy_frame(*c); // no buffer_done before version 3
            }
        },
    .flags =
        [](void* data, zwlr_screencopy_frame_v1* /*frame*/, uint32_t flags) {
            static_cast<Screenshots::Capture*>(data)->y_invert =
                (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
        },
    .ready =
        [](void* data, zwlr_screencopy_frame_v1* /*frame*/, uint32_t /*sec_hi*/,
           uint32_t /*sec_lo*/, uint32_t /*nsec*/) {
            auto* c = static_cast<Screenshots::Capture*>(data);
            c->owner->captured(*c);
        },
    .failed =
        [](void* data, zwlr_screencopy_frame_v1* /*frame*/) {
            auto* c = static_cast<Screenshots::Capture*>(data);
            std::println(stderr, "tetra-shell: screenshot: the compositor could not capture");
            c->owner->end_capture();
        },
    .damage = [](void* /*data*/, zwlr_screencopy_frame_v1* /*frame*/, uint32_t /*x*/,
                 uint32_t /*y*/, uint32_t /*width*/, uint32_t /*height*/) {},
    .linux_dmabuf = [](void* /*data*/, zwlr_screencopy_frame_v1* /*frame*/, uint32_t /*format*/,
                       uint32_t /*width*/, uint32_t /*height*/) {},
    .buffer_done =
        [](void* data, zwlr_screencopy_frame_v1* /*frame*/) {
            auto* c = static_cast<Screenshots::Capture*>(data);
            c->owner->copy_frame(*c);
        },
};

struct DataSourceListener {
    static const wl_data_source_listener listener;
};

const wl_data_source_listener DataSourceListener::listener = {
    .target = [](void* /*data*/, wl_data_source* /*source*/, const char* /*mime*/) {},
    .send =
        [](void* data, wl_data_source* /*source*/, const char* mime, int32_t fd) {
            auto* self = static_cast<Screenshots*>(data);
            if (std::strcmp(mime, kPngMime) != 0 || !self->clipboard_) {
                close(fd);
                return;
            }
            // The reader may be slow or stall: write from a thread.
            std::thread([png = self->clipboard_, fd] {
                const uint8_t* at = png->data();
                std::size_t left = png->size();
                while (left > 0) {
                    const ssize_t n = write(fd, at, left);
                    if (n < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        break; // reader gone (SIGPIPE is ignored)
                    }
                    at += n; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                    left -= static_cast<std::size_t>(n);
                }
                close(fd);
            }).detach();
        },
    .cancelled =
        [](void* data, wl_data_source* source) {
            // Someone else copied something: our offer is over.
            auto* self = static_cast<Screenshots*>(data);
            if (self->source_ == source) {
                self->source_ = nullptr;
                self->clipboard_.reset();
            }
            wl_data_source_destroy(source);
        },
    .dnd_drop_performed = [](void* /*data*/, wl_data_source* /*source*/) {},
    .dnd_finished = [](void* /*data*/, wl_data_source* /*source*/) {},
    .action = [](void* /*data*/, wl_data_source* /*source*/, uint32_t /*action*/) {},
};

// --- Screenshots -----------------------------------------------------------------------

Screenshots::Screenshots(Shell& shell) : shell_(shell) {
    ui::App& app = shell_.app();
    screencopy_ = static_cast<zwlr_screencopy_manager_v1*>(
        app.bind_global(&zwlr_screencopy_manager_v1_interface, 3));
    shm_ = static_cast<wl_shm*>(app.bind_global(&wl_shm_interface, 1));
    data_device_manager_ = static_cast<wl_data_device_manager*>(
        app.bind_global(&wl_data_device_manager_interface, 3));
    if (data_device_manager_ != nullptr && app.seat() != nullptr) {
        data_device_ = wl_data_device_manager_get_data_device(data_device_manager_, app.seat());
        wl_data_device_add_listener(data_device_, &DataDeviceListener::listener, this);
    }
}

Screenshots::~Screenshots() {
    picker_.reset();
    end_capture();
    fail_pending();
    if (source_ != nullptr) {
        wl_data_source_destroy(source_);
    }
    if (offer_ != nullptr) {
        wl_data_offer_destroy(offer_);
    }
    if (data_device_ != nullptr) {
        wl_data_device_release(data_device_);
    }
    if (data_device_manager_ != nullptr) {
        wl_data_device_manager_destroy(data_device_manager_);
    }
    if (shm_ != nullptr) {
        wl_shm_destroy(shm_);
    }
    if (screencopy_ != nullptr) {
        zwlr_screencopy_manager_v1_destroy(screencopy_);
    }
}

fs::path Screenshots::directory() {
    fs::path pictures;
    if (const char* env = std::getenv("XDG_PICTURES_DIR"); env != nullptr && *env != '\0') { // NOLINT
        pictures = env;
    } else {
        pictures = pictures_from_user_dirs();
    }
    if (pictures.empty()) {
        pictures = fs::path(home()) / "Pictures";
    }
    return pictures / "Screenshots";
}

void Screenshots::request(bool interactive, Done done) {
    const uint32_t mode =
        interactive ? TETRA_SHELL_V1_SCREENSHOT_MODE_REGION : TETRA_SHELL_V1_SCREENSHOT_MODE_OUTPUT;
    if (pending_ || capture_ || picker_ || screencopy_ == nullptr || !shell_.request_screenshot(mode)) {
        done(std::nullopt); // one at a time
        return;
    }
    pending_ = std::move(done);
    // The compositor sends nothing when there is no output to capture.
    pending_timer_ = shell_.app().add_timer(std::chrono::seconds(3), [this] {
        pending_timer_.reset();
        fail_pending();
    });
}

void Screenshots::fail_pending() {
    if (pending_timer_) {
        shell_.app().cancel_timer(*pending_timer_);
        pending_timer_.reset();
    }
    if (pending_) {
        Done done = std::move(pending_);
        pending_ = nullptr;
        done(std::nullopt);
    }
}

void Screenshots::take(uint32_t serial, uint32_t mode, wl_output* output, int x, int y,
                       int width, int height) {
    if (screencopy_ == nullptr || shm_ == nullptr) {
        std::println(stderr, "tetra-shell: screenshot: the compositor offers no screencopy");
        fail_pending();
        return;
    }
    if (capture_ || picker_) {
        return; // one at a time
    }
    capture_ = std::make_unique<Capture>();
    if (pending_) { // the portal asked for this one
        if (pending_timer_) {
            shell_.app().cancel_timer(*pending_timer_);
            pending_timer_.reset();
        }
        capture_->done = std::move(pending_);
        pending_ = nullptr;
    }
    capture_->owner = this;
    capture_->serial = serial;
    capture_->mode = mode;
    capture_->output = output;
    capture_->box = Rect{.x = x, .y = y, .width = width, .height = height};
    // Without the cursor, like most screenshot tools.
    capture_->frame = zwlr_screencopy_manager_v1_capture_output(screencopy_, 0, output);
    zwlr_screencopy_frame_v1_add_listener(capture_->frame, &ScreencopyListener::listener,
                                          capture_.get());
}

void Screenshots::copy_frame(Capture& c) {
    if (c.buffer != nullptr) {
        return;
    }
    if (c.format == UINT32_MAX || c.width <= 0 || c.height <= 0) {
        std::println(stderr, "tetra-shell: screenshot: no usable pixel format");
        end_capture();
        return;
    }
    c.size = static_cast<std::size_t>(c.stride) * static_cast<std::size_t>(c.height);
    c.fd = memfd_create("tetra-screenshot", MFD_CLOEXEC);
    if (c.fd < 0 || ftruncate(c.fd, static_cast<off_t>(c.size)) != 0) {
        end_capture();
        return;
    }
    c.data = mmap(nullptr, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, c.fd, 0);
    if (c.data == MAP_FAILED) {
        c.data = nullptr;
        end_capture();
        return;
    }
    wl_shm_pool* pool = wl_shm_create_pool(shm_, c.fd, static_cast<int32_t>(c.size));
    c.buffer = wl_shm_pool_create_buffer(pool, 0, c.width, c.height, c.stride, c.format);
    wl_shm_pool_destroy(pool);
    zwlr_screencopy_frame_v1_copy(c.frame, c.buffer);
}

void Screenshots::end_capture() {
    if (!capture_) {
        return;
    }
    Capture& c = *capture_;
    if (c.frame != nullptr) {
        zwlr_screencopy_frame_v1_destroy(c.frame);
    }
    if (c.buffer != nullptr) {
        wl_buffer_destroy(c.buffer);
    }
    if (c.data != nullptr) {
        munmap(c.data, c.size);
    }
    if (c.fd >= 0) {
        close(c.fd);
    }
    Done done = std::move(c.done);
    capture_.reset();
    if (done) { // ended without a picture
        done(std::nullopt);
    }
}

void Screenshots::captured(Capture& c) {
    // Into B G R X rows, top to bottom.
    Image image{.width = c.width,
                .height = c.height,
                .pixels = std::vector<uint8_t>(static_cast<std::size_t>(c.width) * c.height * 4)};
    const auto* src = static_cast<const uint8_t*>(c.data);
    const std::size_t row_bytes = static_cast<std::size_t>(c.width) * 4;
    for (int y = 0; y < c.height; ++y) {
        const int from = c.y_invert ? c.height - 1 - y : y;
        const uint8_t* in = src + (static_cast<std::size_t>(from) * c.stride); // NOLINT
        uint8_t* out = image.pixels.data() + (static_cast<std::size_t>(y) * row_bytes); // NOLINT
        std::memcpy(out, in, row_bytes);
        for (std::size_t i = 0; i < row_bytes; i += 4) {
            if (is_rgbx(c.format)) {
                std::swap(out[i], out[i + 2]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            }
            out[i + 3] = 255; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        }
    }
    const uint32_t serial = c.serial;
    const uint32_t mode = c.mode;
    wl_output* output = c.output;
    const Rect box = c.box;
    Done done = std::move(c.done);
    c.done = nullptr;
    end_capture(); // `c` is gone from here on

    if (mode == TETRA_SHELL_V1_SCREENSHOT_MODE_REGION) {
        picker_ = std::make_unique<RegionPicker>(
            shell_, output, std::move(image),
            [this, serial, done = std::move(done)](const Image& picked, std::optional<Rect> pixels) {
                if (pixels) {
                    crop_and_save(picked, *pixels, serial, done);
                } else if (done) {
                    done(std::nullopt);
                }
                close_picker();
            });
        return;
    }
    Rect pixels{.x = 0, .y = 0, .width = image.width, .height = image.height};
    if (mode == TETRA_SHELL_V1_SCREENSHOT_MODE_WINDOW && box.width > 0 && box.height > 0) {
        // Logical -> buffer pixels (the output's scale).
        double ratio = 1;
        if (const ui::OutputInfo* info = shell_.output_info(output);
            info != nullptr && info->width > 0 && info->scale > 0) {
            ratio = static_cast<double>(image.width) * info->scale / info->width;
        }
        pixels = Rect{.x = static_cast<int>(std::lround(box.x * ratio)),
                      .y = static_cast<int>(std::lround(box.y * ratio)),
                      .width = static_cast<int>(std::lround(box.width * ratio)),
                      .height = static_cast<int>(std::lround(box.height * ratio))};
    }
    crop_and_save(image, pixels, serial, std::move(done));
}

void Screenshots::close_picker() {
    if (picker_) {
        // Called from the picker's own draw: destroy it on the next turn.
        std::shared_ptr<RegionPicker> closing(std::move(picker_));
        shell_.app().add_timer(std::chrono::milliseconds(0), [closing] {});
    }
}

void Screenshots::crop_and_save(const Image& image, Rect r, uint32_t serial, Done done) {
    r.x = std::clamp(r.x, 0, image.width);
    r.y = std::clamp(r.y, 0, image.height);
    r.width = std::min(r.width, image.width - r.x);
    r.height = std::min(r.height, image.height - r.y);
    if (r.width <= 0 || r.height <= 0) {
        if (done) {
            done(std::nullopt);
        }
        return;
    }
    const int stride = r.width * 4;
    std::vector<uint8_t> pixels(static_cast<std::size_t>(stride) * r.height);
    for (int y = 0; y < r.height; ++y) {
        std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * stride), // NOLINT
                    image.pixels.data() +                                     // NOLINT
                        ((static_cast<std::size_t>(r.y + y) * image.width) + r.x) * 4,
                    static_cast<std::size_t>(stride));
    }

    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        pixels.data(), CAIRO_FORMAT_RGB24, r.width, r.height, stride);
    auto png = std::make_shared<std::vector<uint8_t>>();
    const cairo_status_t status = cairo_surface_write_to_png_stream(surface, &append_png, png.get());
    cairo_surface_destroy(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        std::println(stderr, "tetra-shell: screenshot: PNG encoding failed");
        if (done) {
            done(std::nullopt);
        }
        return;
    }

    const fs::path dir = directory();
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path path = unique_path(dir);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png->data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
               static_cast<std::streamsize>(png->size()));
    file.close();
    const bool saved = !file.fail();
    if (!saved) {
        std::println(stderr, "tetra-shell: screenshot: cannot write {}", path.string());
    }

    // A picture for an app (the portal) is the app's business: not on the
    // clipboard, but the notification still tells the user one was taken.
    if (!done) {
        set_clipboard(png, serial);
    }

    if (Notifications* notifications = shell_.notifications()) {
        const std::string body =
            done    ? (saved ? std::format("{} · for an app", path.filename().string())
                             : std::string("For an app (saving failed)"))
            : saved ? std::format("{} · copied to the clipboard", path.filename().string())
                    : std::string("Copied to the clipboard (saving failed)");
        notifications->post("Screenshot taken", body, saved ? path.string() : std::string(),
                            [path, saved] {
                                if (saved) {
                                    spawn_detached({"xdg-open", path.string()});
                                }
                            });
    }
    if (done) {
        done(saved ? std::optional<fs::path>(path) : std::nullopt);
    }
}

void Screenshots::set_clipboard(std::shared_ptr<const std::vector<uint8_t>> png, uint32_t serial) {
    if (data_device_manager_ == nullptr || data_device_ == nullptr) {
        return;
    }
    if (source_ != nullptr) {
        wl_data_source_destroy(source_);
        source_ = nullptr;
    }
    clipboard_ = std::move(png);
    source_ = wl_data_device_manager_create_data_source(data_device_manager_);
    wl_data_source_add_listener(source_, &DataSourceListener::listener, this);
    wl_data_source_offer(source_, kPngMime);
    wl_data_device_set_selection(data_device_, source_, serial);
}

} // namespace tetra::shell
