#include "screencast.hpp"

#include "protocols.hpp"

#include <pipewire/pipewire.h>
#include <spa/buffer/buffer.h>
#include <spa/param/buffers.h>
#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <spa/utils/type.h>

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <print>

namespace tetra::portal {

namespace {

constexpr const char* kSessionInterface = "org.freedesktop.impl.portal.Session";
constexpr auto kFrameInterval = std::chrono::milliseconds(33); // at most 30 frames a second
constexpr uint32_t kAnyId = 0xffffffffU;                         // PW_ID_ANY

// shm formats whose bytes are B G R X / R G B X: the SPA formats for them.
std::optional<uint32_t> spa_format(uint32_t shm) {
    switch (shm) {
    case WL_SHM_FORMAT_XRGB8888:
    case WL_SHM_FORMAT_ARGB8888:
        return SPA_VIDEO_FORMAT_BGRx;
    case WL_SHM_FORMAT_XBGR8888:
    case WL_SHM_FORMAT_ABGR8888:
        return SPA_VIDEO_FORMAT_RGBx;
    default:
        return std::nullopt;
    }
}

} // namespace

// One display shared as one PipeWire video source. A first capture tells the
// size and format; then the stream is created, and once it streams every
// change of the screen (at most kFrameInterval apart) becomes a frame.
class OutputStream {
public:
    OutputStream(ui::App& app, wl_shm* shm, zwlr_screencopy_manager_v1* screencopy, pw_core* core,
                 const ui::OutputInfo& output, bool cursor, std::function<void()> ready,
                 std::function<void()> failed)
        : app_(app), shm_(shm), screencopy_(screencopy), core_(core), output_(output), cursor_(cursor),
          ready_(std::move(ready)), failed_(std::move(failed)) {
        capture(false);
    }

    ~OutputStream() {
        if (timer_) {
            app_.cancel_timer(*timer_);
        }
        if (frame_ != nullptr) {
            zwlr_screencopy_frame_v1_destroy(frame_);
        }
        if (stream_ != nullptr) {
            spa_hook_remove(&listener_);
            pw_stream_destroy(stream_);
        }
        free_buffer();
    }

    OutputStream(const OutputStream&) = delete;
    OutputStream& operator=(const OutputStream&) = delete;
    OutputStream(OutputStream&&) = delete;
    OutputStream& operator=(OutputStream&&) = delete;

    [[nodiscard]] uint32_t node_id() const noexcept { return node_; }
    [[nodiscard]] const ui::OutputInfo& output() const noexcept { return output_; }
    [[nodiscard]] bool ready() const noexcept { return ready_sent_; }

private:
    // --- capture ---------------------------------------------------------------------
    static const zwlr_screencopy_frame_v1_listener kFrameListener;

    void capture(bool with_damage) {
        with_damage_ = with_damage;
        last_capture_ = std::chrono::steady_clock::now();
        frame_ = zwlr_screencopy_manager_v1_capture_output(screencopy_, cursor_ ? 1 : 0, output_.output);
        zwlr_screencopy_frame_v1_add_listener(frame_, &kFrameListener, this);
    }

    void free_buffer() {
        if (buffer_ != nullptr) {
            wl_buffer_destroy(buffer_);
            buffer_ = nullptr;
        }
        if (data_ != nullptr) {
            munmap(data_, size_);
            data_ = nullptr;
        }
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    bool ensure_buffer() {
        if (buffer_ != nullptr) {
            return true;
        }
        size_ = static_cast<std::size_t>(shm_stride_) * static_cast<std::size_t>(height_);
        fd_ = memfd_create("tetra-screencast", MFD_CLOEXEC);
        if (fd_ < 0 || ftruncate(fd_, static_cast<off_t>(size_)) != 0) {
            return false;
        }
        data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            return false;
        }
        wl_shm_pool* pool = wl_shm_create_pool(shm_, fd_, static_cast<int32_t>(size_));
        buffer_ = wl_shm_pool_create_buffer(pool, 0, width_, height_, shm_stride_, shm_format_);
        wl_shm_pool_destroy(pool);
        return true;
    }

    void on_buffer(uint32_t format, int width, int height, int stride) {
        if (!spa_format(format)) {
            return; // an unusable one; another may follow
        }
        if (stream_ != nullptr && (width != width_ || height != height_ || format != shm_format_)) {
            return; // the display changed mode: keep what the stream was made for
        }
        if (width != width_ || height != height_ || stride != shm_stride_ || format != shm_format_) {
            free_buffer();
        }
        shm_format_ = format;
        width_ = width;
        height_ = height;
        shm_stride_ = stride;
    }

    void on_buffer_done() {
        if (shm_format_ == UINT32_MAX || !ensure_buffer()) {
            on_failed();
            return;
        }
        if (with_damage_) {
            zwlr_screencopy_frame_v1_copy_with_damage(frame_, buffer_);
        } else {
            zwlr_screencopy_frame_v1_copy(frame_, buffer_);
        }
    }

    void on_ready() {
        zwlr_screencopy_frame_v1_destroy(frame_);
        frame_ = nullptr;
        // Keep the picture top-down with tight rows, as the stream sends it.
        const std::size_t row = static_cast<std::size_t>(width_) * 4;
        pixels_.resize(row * static_cast<std::size_t>(height_));
        const auto* src = static_cast<const uint8_t*>(data_);
        for (int y = 0; y < height_; ++y) {
            const int from = y_invert_ ? height_ - 1 - y : y;
            std::memcpy(pixels_.data() + (row * static_cast<std::size_t>(y)),             // NOLINT
                        src + (static_cast<std::size_t>(from) * static_cast<std::size_t>(shm_stride_)), // NOLINT
                        row);
        }
        fresh_ = true;
        if (stream_ == nullptr) {
            create_stream();
            return;
        }
        if (streaming_) {
            pw_stream_trigger_process(stream_);
            schedule();
        }
    }

    void on_failed() {
        if (frame_ != nullptr) {
            zwlr_screencopy_frame_v1_destroy(frame_);
            frame_ = nullptr;
        }
        if (stream_ == nullptr) {
            failed_(); // could not even start
            return;
        }
        schedule(); // try again
    }

    // The next capture, waiting for the screen to change, not before
    // kFrameInterval after the last one.
    void schedule() {
        if (!streaming_ || frame_ != nullptr || timer_) {
            return;
        }
        const auto since = std::chrono::steady_clock::now() - last_capture_;
        const auto wait = std::max(std::chrono::milliseconds(0),
                                   std::chrono::duration_cast<std::chrono::milliseconds>(kFrameInterval - since));
        timer_ = app_.add_timer(wait, [this] {
            timer_.reset();
            if (streaming_ && frame_ == nullptr) {
                capture(true);
            }
        });
    }

    // --- PipeWire ----------------------------------------------------------------------
    static const pw_stream_events kStreamEvents;

    void create_stream() {
        pw_properties* props = pw_properties_new(PW_KEY_MEDIA_CLASS, "Video/Source", PW_KEY_NODE_NAME,
                                                 "tetra-screencast", nullptr);
        pw_properties_set(props, PW_KEY_NODE_DESCRIPTION, std::format("Screen {}", output_.name).c_str());
        stream_ = pw_stream_new(core_, "tetra-screencast", props);
        if (stream_ == nullptr) {
            failed_();
            return;
        }
        pw_stream_add_listener(stream_, &listener_, &kStreamEvents, this);

        std::array<uint8_t, 1024> storage{};
        spa_pod_builder b{};
        spa_pod_builder_init(&b, storage.data(), storage.size());
        spa_pod_frame f{};
        spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
        spa_pod_builder_prop(&b, SPA_FORMAT_mediaType, 0);
        spa_pod_builder_id(&b, SPA_MEDIA_TYPE_video);
        spa_pod_builder_prop(&b, SPA_FORMAT_mediaSubtype, 0);
        spa_pod_builder_id(&b, SPA_MEDIA_SUBTYPE_raw);
        spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_format, 0);
        spa_pod_builder_id(&b, *spa_format(shm_format_));
        spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_size, 0);
        spa_pod_builder_rectangle(&b, static_cast<uint32_t>(width_), static_cast<uint32_t>(height_));
        spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_framerate, 0);
        spa_pod_builder_fraction(&b, 0, 1); // variable: a frame when something changed
        spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_maxFramerate, 0);
        spa_pod_frame range{};
        spa_pod_builder_push_choice(&b, &range, SPA_CHOICE_Range, 0);
        spa_pod_builder_fraction(&b, 30, 1);
        spa_pod_builder_fraction(&b, 1, 1);
        spa_pod_builder_fraction(&b, 30, 1);
        spa_pod_builder_pop(&b, &range);
        std::array<const spa_pod*, 1> params{static_cast<const spa_pod*>(spa_pod_builder_pop(&b, &f))};
        const auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_MAP_BUFFERS);
        if (pw_stream_connect(stream_, PW_DIRECTION_OUTPUT, kAnyId, flags, params.data(), 1) < 0) {
            failed_();
        }
    }

    void on_state(pw_stream_state state, const char* error) {
        if (state == PW_STREAM_STATE_ERROR) {
            std::println(stderr, "tetra-portal: screencast stream: {}", error != nullptr ? error : "error");
            streaming_ = false;
            if (!ready_sent_) {
                failed_();
            }
            return;
        }
        if (state == PW_STREAM_STATE_PAUSED && !ready_sent_) {
            node_ = pw_stream_get_node_id(stream_);
            ready_sent_ = true;
            ready_();
            return;
        }
        const bool was = streaming_;
        streaming_ = state == PW_STREAM_STATE_STREAMING;
        if (streaming_ && !was) {
            // The newest picture first, then whatever changes.
            fresh_ = true;
            pw_stream_trigger_process(stream_);
            schedule();
        } else if (!streaming_ && timer_) {
            app_.cancel_timer(*timer_);
            timer_.reset();
        }
    }

    void on_param(uint32_t id, const spa_pod* param) {
        if (param == nullptr || id != SPA_PARAM_Format) {
            return;
        }
        const int stride = width_ * 4;
        std::array<uint8_t, 1024> storage{};
        spa_pod_builder b{};
        spa_pod_builder_init(&b, storage.data(), storage.size());
        spa_pod_frame f{};
        spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers);
        spa_pod_builder_prop(&b, SPA_PARAM_BUFFERS_buffers, 0);
        spa_pod_frame range{};
        spa_pod_builder_push_choice(&b, &range, SPA_CHOICE_Range, 0);
        spa_pod_builder_int(&b, 4);
        spa_pod_builder_int(&b, 2);
        spa_pod_builder_int(&b, 8);
        spa_pod_builder_pop(&b, &range);
        spa_pod_builder_prop(&b, SPA_PARAM_BUFFERS_blocks, 0);
        spa_pod_builder_int(&b, 1);
        spa_pod_builder_prop(&b, SPA_PARAM_BUFFERS_size, 0);
        spa_pod_builder_int(&b, stride * height_);
        spa_pod_builder_prop(&b, SPA_PARAM_BUFFERS_stride, 0);
        spa_pod_builder_int(&b, stride);
        spa_pod_builder_prop(&b, SPA_PARAM_BUFFERS_dataType, 0);
        spa_pod_frame types{};
        spa_pod_builder_push_choice(&b, &types, SPA_CHOICE_Flags, 0);
        spa_pod_builder_int(&b, (1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr));
        spa_pod_builder_pop(&b, &types);
        std::array<const spa_pod*, 1> params{static_cast<const spa_pod*>(spa_pod_builder_pop(&b, &f))};
        pw_stream_update_params(stream_, params.data(), 1);
    }

    void on_process() {
        if (!fresh_) {
            return;
        }
        pw_buffer* b = pw_stream_dequeue_buffer(stream_);
        if (b == nullptr) {
            return; // none free: the next change sends again
        }
        spa_buffer* buffer = b->buffer;
        if (buffer->n_datas > 0 && buffer->datas[0].data != nullptr) { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            spa_data& d = buffer->datas[0]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const auto bytes = std::min<std::size_t>(pixels_.size(), d.maxsize);
            std::memcpy(d.data, pixels_.data(), bytes);
            d.chunk->offset = 0;
            d.chunk->size = static_cast<uint32_t>(bytes);
            d.chunk->stride = width_ * 4;
            d.chunk->flags = 0;
            fresh_ = false;
        }
        pw_stream_queue_buffer(stream_, b);
    }

    ui::App& app_;
    wl_shm* shm_;
    zwlr_screencopy_manager_v1* screencopy_;
    pw_core* core_;
    ui::OutputInfo output_;
    bool cursor_;
    std::function<void()> ready_;
    std::function<void()> failed_;

    zwlr_screencopy_frame_v1* frame_ = nullptr;
    bool with_damage_ = false;
    uint32_t shm_format_ = UINT32_MAX;
    int width_ = 0;
    int height_ = 0;
    int shm_stride_ = 0;
    bool y_invert_ = false;
    int fd_ = -1;
    void* data_ = nullptr;
    std::size_t size_ = 0;
    wl_buffer* buffer_ = nullptr;
    std::vector<uint8_t> pixels_; // the latest picture, B G R X / R G B X
    bool fresh_ = false;          // not sent yet
    std::chrono::steady_clock::time_point last_capture_;
    std::optional<ui::App::TimerId> timer_;

    pw_stream* stream_ = nullptr;
    spa_hook listener_{};
    bool streaming_ = false;
    bool ready_sent_ = false;
    uint32_t node_ = 0;
};

const zwlr_screencopy_frame_v1_listener OutputStream::kFrameListener = {
    .buffer =
        [](void* data, zwlr_screencopy_frame_v1* /*frame*/, uint32_t format, uint32_t width, uint32_t height,
           uint32_t stride) {
            static_cast<OutputStream*>(data)->on_buffer(format, static_cast<int>(width), static_cast<int>(height),
                                                        static_cast<int>(stride));
        },
    .flags =
        [](void* data, zwlr_screencopy_frame_v1* /*frame*/, uint32_t flags) {
            static_cast<OutputStream*>(data)->y_invert_ = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
        },
    .ready = [](void* data, zwlr_screencopy_frame_v1* /*frame*/, uint32_t /*sec_hi*/, uint32_t /*sec_lo*/,
                uint32_t /*nsec*/) { static_cast<OutputStream*>(data)->on_ready(); },
    .failed = [](void* data, zwlr_screencopy_frame_v1* /*frame*/) { static_cast<OutputStream*>(data)->on_failed(); },
    .damage = [](void* /*data*/, zwlr_screencopy_frame_v1* /*frame*/, uint32_t /*x*/, uint32_t /*y*/,
                 uint32_t /*width*/, uint32_t /*height*/) {},
    .linux_dmabuf = [](void* /*data*/, zwlr_screencopy_frame_v1* /*frame*/, uint32_t /*format*/,
                       uint32_t /*width*/, uint32_t /*height*/) {},
    .buffer_done = [](void* data, zwlr_screencopy_frame_v1* /*frame*/) {
        static_cast<OutputStream*>(data)->on_buffer_done();
    },
};

const pw_stream_events OutputStream::kStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed =
        [](void* data, pw_stream_state /*old*/, pw_stream_state state, const char* error) {
            static_cast<OutputStream*>(data)->on_state(state, error);
        },
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = [](void* data, uint32_t id,
                        const spa_pod* param) { static_cast<OutputStream*>(data)->on_param(id, param); },
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = [](void* data) { static_cast<OutputStream*>(data)->on_process(); },
    .drained = nullptr,
    .command = nullptr,
    .trigger_done = nullptr,
};

// --- ScreenCast ----------------------------------------------------------------------------

ScreenCast::ScreenCast(ui::App& app, sdbus::IConnection& bus) : app_(app), bus_(bus) {
    notifications_ = sdbus::createProxy(bus_, sdbus::ServiceName{"org.freedesktop.Notifications"},
                                        sdbus::ObjectPath{"/org/freedesktop/Notifications"});
}

ScreenCast::~ScreenCast() {
    for (auto& [path, session] : sessions_) {
        session->streams.clear();
    }
    sessions_.clear();
    if (core_ != nullptr) {
        pw_core_disconnect(core_);
    }
    if (context_ != nullptr) {
        pw_context_destroy(context_);
    }
    if (loop_ != nullptr) {
        app_.unwatch_fd(loop_fd_);
        pw_loop_leave(loop_);
        pw_loop_destroy(loop_);
    }
    if (screencopy_ != nullptr) {
        zwlr_screencopy_manager_v1_destroy(screencopy_);
    }
    if (shm_ != nullptr) {
        wl_shm_destroy(shm_);
    }
}

bool ScreenCast::ensure_pipewire() {
    if (screencopy_ == nullptr) {
        screencopy_ = static_cast<zwlr_screencopy_manager_v1*>(
            app_.bind_global(&zwlr_screencopy_manager_v1_interface, 3));
        shm_ = static_cast<wl_shm*>(app_.bind_global(&wl_shm_interface, 1));
    }
    if (screencopy_ == nullptr || shm_ == nullptr) {
        std::println(stderr, "tetra-portal: screencast: the compositor offers no screencopy");
        return false;
    }
    if (core_ != nullptr) {
        return true;
    }
    static const bool initialized = [] {
        pw_init(nullptr, nullptr);
        return true;
    }();
    (void)initialized;
    if (loop_ == nullptr) {
        // PipeWire's loop runs inside ours: its fd is watched, and every
        // wakeup dispatches what is due (no thread).
        loop_ = pw_loop_new(nullptr);
        if (loop_ == nullptr) {
            return false;
        }
        pw_loop_enter(loop_);
        loop_fd_ = pw_loop_get_fd(loop_);
        app_.watch_fd(loop_fd_, [this] { pw_loop_iterate(loop_, 0); });
        context_ = pw_context_new(loop_, nullptr, 0);
    }
    if (context_ == nullptr) {
        return false;
    }
    core_ = pw_context_connect(context_, nullptr, 0);
    if (core_ == nullptr) {
        std::println(stderr, "tetra-portal: screencast: cannot connect to PipeWire");
        return false;
    }
    return true;
}

void ScreenCast::create_session(const sdbus::ObjectPath& path, const std::string& app_id) {
    auto session = std::make_unique<Session>();
    session->app_id = app_id;
    session->object = sdbus::createObject(bus_, path);
    const std::string key = path;
    session->object
        ->addVTable(sdbus::registerMethod("Close").implementedAs([this, key] {
                        app_.add_timer(std::chrono::milliseconds(0), [this, key] { close(key, false); });
                    }),
                    sdbus::registerSignal("Closed").withParameters<std::map<std::string, sdbus::Variant>>("details"),
                    sdbus::registerProperty("version").withGetter([] { return uint32_t{1}; }))
        .forInterface(sdbus::InterfaceName{kSessionInterface});
    sessions_[key] = std::move(session);
}

bool ScreenCast::select_sources(const std::string& session, const Options& options) {
    auto it = sessions_.find(session);
    if (it == sessions_.end() || it->second->started) {
        return false;
    }
    Session& s = *it->second;
    if (auto m = options.find("multiple"); m != options.end() && m->second.containsValueOfType<bool>()) {
        s.multiple = m->second.get<bool>();
    }
    if (auto c = options.find("cursor_mode"); c != options.end() && c->second.containsValueOfType<uint32_t>()) {
        s.cursor_mode = c->second.get<uint32_t>() == kCursorEmbedded ? kCursorEmbedded : kCursorHidden;
    }
    return true;
}

void ScreenCast::start(const std::string& session, const std::string& parent, StartDone done) {
    auto it = sessions_.find(session);
    const auto outputs = app_.outputs();
    if (it == sessions_.end() || it->second->started || outputs.empty() || !ensure_pipewire()) {
        done(2, {});
        return;
    }
    Session& s = *it->second;
    s.started = true;
    s.done = std::move(done);

    // Which display: an access dialog with the displays as a choice.
    Choice display{.id = "display", .label = "Display", .options = {}, .selected = outputs.front()->name};
    for (const auto* o : outputs) {
        display.options.emplace_back(o->name, std::format("{} ({}×{})", o->description.empty() ? o->name
                                                                                             : o->description,
                                                          o->width, o->height));
    }
    if (s.multiple && outputs.size() > 1) {
        display.options.emplace_back("*", "All displays");
    }
    AccessOptions access{
        .title = "Share your screen?",
        .subtitle = s.app_id.empty() ? std::string("An app wants to see a display.")
                                     : std::format("“{}” wants to see a display.", s.app_id),
        .body = s.cursor_mode == kCursorEmbedded
                    ? "It sees everything on the display you pick, pointer included, until it stops."
                    : "It sees everything on the display you pick until it stops.",
        .icon = "video-display-symbolic",
        .grant_label = "Share",
        .deny_label = "Cancel",
        .choices = {display},
        .parent = parent,
    };
    s.dialog = std::make_unique<AccessDialog>(
        app_, std::move(access), [this, session](bool granted, std::vector<Choice> choices) {
            auto found = sessions_.find(session);
            if (found == sessions_.end()) {
                return;
            }
            // Not from inside the dialog's own draw.
            app_.add_timer(std::chrono::milliseconds(0), [this, session] {
                if (auto f = sessions_.find(session); f != sessions_.end()) {
                    f->second->dialog.reset();
                }
            });
            if (!granted) {
                auto reply = std::move(found->second->done);
                found->second->done = nullptr;
                if (reply) {
                    reply(1, {});
                }
                return;
            }
            const std::string picked = choices.empty() ? std::string() : choices.front().selected;
            std::vector<const ui::OutputInfo*> chosen;
            for (const auto* o : app_.outputs()) {
                if (picked == "*" || o->name == picked) {
                    chosen.push_back(o);
                }
            }
            begin_streams(session, chosen);
        });
}

void ScreenCast::cancel_start(const std::string& session) {
    auto it = sessions_.find(session);
    if (it != sessions_.end()) {
        if (it->second->dialog) {
            it->second->dialog->cancel();
            app_.add_timer(std::chrono::milliseconds(0), [this, session] {
                if (auto f = sessions_.find(session); f != sessions_.end()) {
                    f->second->dialog.reset();
                }
            });
        }
        it->second->done = nullptr;
    }
}

void ScreenCast::begin_streams(const std::string& session, const std::vector<const ui::OutputInfo*>& outputs) {
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
        return;
    }
    Session& s = *it->second;
    if (outputs.empty()) {
        if (auto done = std::move(s.done)) {
            done(2, {});
        }
        return;
    }
    for (const auto* o : outputs) {
        s.streams.push_back(std::make_unique<OutputStream>(
            app_, shm_, screencopy_, core_, *o, s.cursor_mode == kCursorEmbedded,
            [this, session] { stream_ready(session); },
            [this, session] {
                app_.add_timer(std::chrono::milliseconds(0), [this, session] {
                    auto f = sessions_.find(session);
                    if (f == sessions_.end()) {
                        return;
                    }
                    if (auto done = std::move(f->second->done)) {
                        done(2, {});
                    }
                    close(session, true);
                });
            }));
    }
}

void ScreenCast::stream_ready(const std::string& session) {
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
        return;
    }
    Session& s = *it->second;
    if (!std::ranges::all_of(s.streams, [](const auto& stream) { return stream->ready(); })) {
        return;
    }
    using StreamProps = std::map<std::string, sdbus::Variant>;
    std::vector<sdbus::Struct<uint32_t, StreamProps>> streams;
    std::string names;
    for (const auto& stream : s.streams) {
        const ui::OutputInfo& o = stream->output();
        const int scale = std::max(1, o.scale);
        StreamProps props{
            {"size", sdbus::Variant(sdbus::Struct<int32_t, int32_t>{o.width / scale, o.height / scale})},
            {"source_type", sdbus::Variant(kMonitor)},
            {"id", sdbus::Variant(o.name)},
        };
        streams.emplace_back(stream->node_id(), props);
        names += names.empty() ? o.name : ", " + o.name;
    }
    notify(s, names);
    if (auto done = std::move(s.done)) {
        done(0, Options{{"streams", sdbus::Variant(streams)}, {"persist_mode", sdbus::Variant(uint32_t{0})}});
    }
}

void ScreenCast::notify(Session& session, const std::string& displays) {
    std::map<std::string, sdbus::Variant> hints{{"urgency", sdbus::Variant(uint8_t{1})}};
    const std::string who = session.app_id.empty() ? std::string("An app") : session.app_id;
    const std::string key = session.object->getObjectPath();
    try {
        notifications_->callMethodAsync("Notify")
            .onInterface(sdbus::InterfaceName{"org.freedesktop.Notifications"})
            .withArguments(std::string("Screen sharing"), uint32_t{0}, std::string("video-display-symbolic"),
                           std::string("Your screen is being shared"),
                           std::format("{} sees {} until it stops sharing.", who, displays),
                           std::vector<std::string>{}, hints, int32_t{0})
            .uponReplyInvoke([this, key](std::optional<sdbus::Error> error, uint32_t id) {
                if (error) {
                    return;
                }
                if (auto it = sessions_.find(key); it != sessions_.end()) {
                    it->second->notification = id;
                } else {
                    // Already over: take the notification down again.
                    notifications_->callMethodAsync("CloseNotification")
                        .onInterface(sdbus::InterfaceName{"org.freedesktop.Notifications"})
                        .withArguments(id)
                        .uponReplyInvoke([](std::optional<sdbus::Error> /*error*/) {});
                }
            });
    } catch (const sdbus::Error&) {
        // no notification server: share anyway
    }
}

void ScreenCast::close(const std::string& session, bool signal) {
    auto it = sessions_.find(session);
    if (it == sessions_.end()) {
        return;
    }
    std::unique_ptr<Session> s = std::move(it->second);
    sessions_.erase(it);
    if (s->dialog) {
        s->dialog->cancel();
    }
    if (s->notification) {
        try {
            notifications_->callMethodAsync("CloseNotification")
                .onInterface(sdbus::InterfaceName{"org.freedesktop.Notifications"})
                .withArguments(*s->notification)
                .uponReplyInvoke([](std::optional<sdbus::Error> /*error*/) {});
        } catch (const sdbus::Error&) {
            // gone
        }
    }
    if (signal) {
        s->object->emitSignal("Closed")
            .onInterface(sdbus::InterfaceName{kSessionInterface})
            .withArguments(std::map<std::string, sdbus::Variant>{});
    }
    s->streams.clear();
}

} // namespace tetra::portal
