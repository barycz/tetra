#include "audio.hpp"

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

#include <sys/eventfd.h>
#include <unistd.h>

namespace tetra::shell {

namespace {

using Publish = std::function<void(const Audio::State&)>;

// In-memory device for headless tests.
class FakeBackend final : public Audio::Backend {
public:
    explicit FakeBackend(Publish publish) : publish_(std::move(publish)) {
        state_ = {.available = true, .volume = 0.5F, .muted = false, .device = "Simulated output"};
        publish_(state_);
    }
    void set_volume(float volume) override {
        state_.volume = volume;
        publish_(state_);
    }
    void set_muted(bool muted) override {
        state_.muted = muted;
        publish_(state_);
    }
    void toggle_mic_mute() override {}

private:
    Publish publish_;
    Audio::State state_;
};

// PulseAudio client on its own thread (pa_threaded_mainloop). Callbacks run
// on that thread with the loop lock held; they store a snapshot and wake the
// UI thread through an eventfd. Commands from the UI thread take the loop lock.
class PulseBackend final : public Audio::Backend {
public:
    PulseBackend(ui::App& app, Publish publish) : app_(app), publish_(std::move(publish)) {
        event_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        app_.watch_fd(event_fd_, [this] { on_wakeup(); });
        loop_ = pa_threaded_mainloop_new();
        connect(); // the loop is not running yet: no lock needed
        pa_threaded_mainloop_start(loop_);
    }

    ~PulseBackend() override {
        if (reconnect_timer_) {
            app_.cancel_timer(*reconnect_timer_);
        }
        app_.unwatch_fd(event_fd_);
        pa_threaded_mainloop_stop(loop_);
        disconnect();
        pa_threaded_mainloop_free(loop_);
        close(event_fd_);
    }

    PulseBackend(const PulseBackend&) = delete;
    PulseBackend& operator=(const PulseBackend&) = delete;
    PulseBackend(PulseBackend&&) = delete;
    PulseBackend& operator=(PulseBackend&&) = delete;

    void set_volume(float volume) override {
        const Lock lock(loop_);
        if (!ready() || sink_.empty()) {
            return;
        }
        pa_cvolume cvolume;
        pa_cvolume_set(&cvolume, channels_,
                       static_cast<pa_volume_t>(std::lround(std::clamp(volume, 0.0F, 1.0F) * PA_VOLUME_NORM)));
        unref(pa_context_set_sink_volume_by_name(context_, sink_.c_str(), &cvolume, nullptr, nullptr));
    }

    void set_muted(bool muted) override {
        const Lock lock(loop_);
        if (ready() && !sink_.empty()) {
            unref(pa_context_set_sink_mute_by_name(context_, sink_.c_str(), muted ? 1 : 0, nullptr, nullptr));
        }
    }

    void toggle_mic_mute() override {
        const Lock lock(loop_);
        if (ready() && !source_.empty()) {
            unref(pa_context_get_source_info_by_name(
                context_, source_.c_str(),
                [](pa_context* context, const pa_source_info* info, int eol, void* /*data*/) {
                    if (eol == 0 && info != nullptr) {
                        unref(pa_context_set_source_mute_by_index(context, info->index,
                                                                  info->mute != 0 ? 0 : 1, nullptr,
                                                                  nullptr));
                    }
                },
                this));
        }
    }

private:
    struct Lock {
        explicit Lock(pa_threaded_mainloop* loop) : loop_(loop) { pa_threaded_mainloop_lock(loop_); }
        ~Lock() { pa_threaded_mainloop_unlock(loop_); }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
        Lock(Lock&&) = delete;
        Lock& operator=(Lock&&) = delete;
        pa_threaded_mainloop* loop_;
    };

    static void unref(pa_operation* operation) {
        if (operation != nullptr) {
            pa_operation_unref(operation);
        }
    }

    [[nodiscard]] bool ready() const {
        return context_ != nullptr && pa_context_get_state(context_) == PA_CONTEXT_READY;
    }

    // Loop locked (or not running).
    void connect() {
        pa_proplist* props = pa_proplist_new();
        pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "tetra-shell");
        pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "tetra-shell");
        context_ = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(loop_), "tetra-shell", props);
        pa_proplist_free(props);
        pa_context_set_state_callback(context_, &PulseBackend::on_context_state, this);
        pa_context_set_subscribe_callback(context_, &PulseBackend::on_subscription, this);
        if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
            mark_failed();
        }
    }

    void disconnect() {
        if (context_ != nullptr) {
            pa_context_set_state_callback(context_, nullptr, nullptr);
            pa_context_set_subscribe_callback(context_, nullptr, nullptr);
            pa_context_disconnect(context_);
            pa_context_unref(context_);
            context_ = nullptr;
        }
    }

    void mark_failed() {
        {
            const std::scoped_lock lock(mutex_);
            shared_.available = false;
            failed_ = true;
        }
        wake();
    }

    void wake() const {
        const uint64_t one = 1;
        [[maybe_unused]] auto n = write(event_fd_, &one, sizeof(one));
    }

    // --- PulseAudio thread ----------------------------------------------------------

    static void on_context_state(pa_context* context, void* data) {
        auto* self = static_cast<PulseBackend*>(data);
        switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY:
            unref(pa_context_subscribe(
                context,
                static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
                                                    PA_SUBSCRIPTION_MASK_SERVER),
                nullptr, nullptr));
            self->refresh();
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            self->mark_failed();
            break;
        default:
            break;
        }
    }

    static void on_subscription(pa_context* /*context*/, pa_subscription_event_type_t type,
                                uint32_t /*index*/, void* data) {
        const auto facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        if (facility == PA_SUBSCRIPTION_EVENT_SINK || facility == PA_SUBSCRIPTION_EVENT_SERVER) {
            static_cast<PulseBackend*>(data)->refresh();
        }
    }

    void refresh() {
        unref(pa_context_get_server_info(
            context_,
            [](pa_context* context, const pa_server_info* info, void* data) {
                auto* self = static_cast<PulseBackend*>(data);
                if (info == nullptr) {
                    return;
                }
                self->sink_ = info->default_sink_name != nullptr ? info->default_sink_name : "";
                self->source_ = info->default_source_name != nullptr ? info->default_source_name : "";
                if (self->sink_.empty()) {
                    {
                        const std::scoped_lock lock(self->mutex_);
                        self->shared_ = {};
                    }
                    self->wake();
                    return;
                }
                unref(pa_context_get_sink_info_by_name(context, self->sink_.c_str(),
                                                       &PulseBackend::on_sink_info, self));
            },
            this));
    }

    static void on_sink_info(pa_context* /*context*/, const pa_sink_info* info, int eol, void* data) {
        auto* self = static_cast<PulseBackend*>(data);
        if (eol != 0 || info == nullptr) {
            return;
        }
        self->channels_ = info->volume.channels;
        Audio::State state{
            .available = true,
            .volume = static_cast<float>(pa_cvolume_avg(&info->volume)) / static_cast<float>(PA_VOLUME_NORM),
            .muted = info->mute != 0,
            .device = info->description != nullptr ? info->description : info->name,
        };
        {
            const std::scoped_lock lock(self->mutex_);
            self->shared_ = std::move(state);
        }
        self->wake();
    }

    // --- UI thread -------------------------------------------------------------------

    void on_wakeup() {
        uint64_t count = 0;
        [[maybe_unused]] auto n = read(event_fd_, &count, sizeof(count));
        Audio::State state;
        bool failed = false;
        {
            const std::scoped_lock lock(mutex_);
            state = shared_;
            failed = std::exchange(failed_, false);
        }
        publish_(state);
        if (failed && !reconnect_timer_) {
            // No sound server (yet): try again in a while.
            reconnect_timer_ = app_.add_timer(std::chrono::seconds(5), [this] {
                reconnect_timer_.reset();
                const Lock lock(loop_);
                disconnect();
                connect();
            });
        }
    }

    ui::App& app_;
    Publish publish_;
    pa_threaded_mainloop* loop_ = nullptr;
    pa_context* context_ = nullptr; // loop lock
    std::string sink_;              // loop lock
    std::string source_;            // loop lock
    uint8_t channels_ = 2;          // loop lock
    int event_fd_ = -1;
    std::mutex mutex_;
    Audio::State shared_; // mutex_
    bool failed_ = false; // mutex_
    std::optional<ui::App::TimerId> reconnect_timer_;
};

} // namespace

Audio::Audio(ui::App& app) : app_(app) {
    auto publish = [this](const State& state) { update(state); };
    const char* mode = std::getenv("TETRA_AUDIO"); // NOLINT(concurrency-mt-unsafe)
    if (mode != nullptr && std::string_view(mode) == "fake") {
        backend_ = std::make_unique<FakeBackend>(publish);
    } else {
        backend_ = std::make_unique<PulseBackend>(app_, publish);
    }
}

Audio::~Audio() = default;

void Audio::update(const State& state) {
    if (state == state_) {
        return;
    }
    state_ = state;
    if (on_changed) {
        on_changed();
    }
}

void Audio::set_volume(float volume) {
    volume = std::clamp(volume, 0.0F, 1.0F);
    // Show the new value at once; the sound server confirms it shortly.
    State next = state_;
    next.volume = volume;
    const bool unmute = next.muted && volume > 0;
    next.muted = next.muted && !unmute;
    update(next);
    backend_->set_volume(volume);
    if (unmute) {
        backend_->set_muted(false); // like Windows: changing the volume unmutes
    }
}

void Audio::change_volume(float delta) {
    // Snap to 5% steps.
    set_volume(std::round((state_.volume + delta) * 20.0F) / 20.0F);
}

void Audio::set_muted(bool muted) {
    State next = state_;
    next.muted = muted;
    update(next);
    backend_->set_muted(muted);
}

void Audio::toggle_mic_mute() {
    backend_->toggle_mic_mute();
}

} // namespace tetra::shell
