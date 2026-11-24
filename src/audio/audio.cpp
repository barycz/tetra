#include "audio.hpp"

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

#include <sys/eventfd.h>
#include <unistd.h>

namespace tetra {

namespace {

using Publish = std::function<void(const Audio::State&)>;

// In-memory devices for headless tests.
class FakeBackend final : public Audio::Backend {
public:
    explicit FakeBackend(Publish publish) : publish_(std::move(publish)) {
        state_ = {.available = true,
                  .volume = 0.5F,
                  .muted = false,
                  .device = "Simulated output",
                  .output = "fake-output",
                  .outputs = {{.name = "fake-output", .description = "Simulated output"},
                              {.name = "fake-headphones", .description = "Simulated headphones"}},
                  .input_volume = 0.8F,
                  .input_muted = false,
                  .input = "fake-microphone",
                  .inputs = {{.name = "fake-microphone", .description = "Simulated microphone"}}};
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
    void set_output(const std::string& name) override {
        for (const auto& d : state_.outputs) {
            if (d.name == name) {
                state_.output = d.name;
                state_.device = d.description;
            }
        }
        publish_(state_);
    }
    void set_input(const std::string& name) override {
        if (std::ranges::any_of(state_.inputs, [&](const auto& d) { return d.name == name; })) {
            state_.input = name;
        }
        publish_(state_);
    }
    void set_input_volume(float volume) override {
        state_.input_volume = volume;
        publish_(state_);
    }
    void set_input_muted(bool muted) override {
        state_.input_muted = muted;
        publish_(state_);
    }
    void toggle_mic_mute() override { set_input_muted(!state_.input_muted); }

private:
    Publish publish_;
    Audio::State state_;
};

// PulseAudio client on its own thread (pa_threaded_mainloop). Callbacks run
// on that thread with the loop lock held; they gather a snapshot and wake the
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
        if (ready() && !sink_.empty()) {
            const pa_cvolume cvolume = make_volume(volume, channels_.contains(sink_) ? channels_.at(sink_) : 2);
            unref(pa_context_set_sink_volume_by_name(context_, sink_.c_str(), &cvolume, nullptr, nullptr));
        }
    }

    void set_muted(bool muted) override {
        const Lock lock(loop_);
        if (ready() && !sink_.empty()) {
            unref(pa_context_set_sink_mute_by_name(context_, sink_.c_str(), muted ? 1 : 0, nullptr, nullptr));
        }
    }

    void set_output(const std::string& name) override {
        const Lock lock(loop_);
        if (ready()) {
            unref(pa_context_set_default_sink(context_, name.c_str(), nullptr, nullptr));
        }
    }

    void set_input(const std::string& name) override {
        const Lock lock(loop_);
        if (ready()) {
            unref(pa_context_set_default_source(context_, name.c_str(), nullptr, nullptr));
        }
    }

    void set_input_volume(float volume) override {
        const Lock lock(loop_);
        if (ready() && !source_.empty()) {
            const pa_cvolume cvolume =
                make_volume(volume, channels_.contains(source_) ? channels_.at(source_) : 1);
            unref(pa_context_set_source_volume_by_name(context_, source_.c_str(), &cvolume, nullptr, nullptr));
        }
    }

    void set_input_muted(bool muted) override {
        const Lock lock(loop_);
        if (ready() && !source_.empty()) {
            unref(pa_context_set_source_mute_by_name(context_, source_.c_str(), muted ? 1 : 0, nullptr, nullptr));
        }
    }

    void toggle_mic_mute() override {
        const Lock lock(loop_);
        if (ready() && !source_.empty()) {
            // Ask the server: our snapshot may be a moment old.
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

    // One refresh: the server info, then every sink and source; published
    // once both lists are complete.
    struct Snapshot {
        PulseBackend* self;
        Audio::State state;
        std::map<std::string, uint8_t> channels;
        int pending = 2;
    };

    static void unref(pa_operation* operation) {
        if (operation != nullptr) {
            pa_operation_unref(operation);
        }
    }

    static pa_cvolume make_volume(float volume, uint8_t channels) {
        pa_cvolume cvolume;
        pa_cvolume_set(&cvolume, std::max<uint8_t>(1, channels),
                       static_cast<pa_volume_t>(std::lround(std::clamp(volume, 0.0F, 1.0F) * PA_VOLUME_NORM)));
        return cvolume;
    }

    static float level(const pa_cvolume& volume) {
        return static_cast<float>(pa_cvolume_avg(&volume)) / static_cast<float>(PA_VOLUME_NORM);
    }

    [[nodiscard]] bool ready() const {
        return context_ != nullptr && pa_context_get_state(context_) == PA_CONTEXT_READY;
    }

    // Loop locked (or not running).
    void connect() {
        pa_proplist* props = pa_proplist_new();
        const char* name = app_.app_id().c_str();
        pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, name);
        pa_proplist_sets(props, PA_PROP_APPLICATION_ID, name);
        context_ = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(loop_), name, props);
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
        if (facility == PA_SUBSCRIPTION_EVENT_SINK || facility == PA_SUBSCRIPTION_EVENT_SOURCE ||
            facility == PA_SUBSCRIPTION_EVENT_SERVER) {
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
                auto* snapshot = new Snapshot{.self = self, .state = {}, .channels = {}, .pending = 2};
                snapshot->state.available = true;
                snapshot->state.output = info->default_sink_name != nullptr ? info->default_sink_name : "";
                snapshot->state.input = info->default_source_name != nullptr ? info->default_source_name : "";
                pa_operation* sinks = pa_context_get_sink_info_list(context, &PulseBackend::on_sink, snapshot);
                pa_operation* sources =
                    pa_context_get_source_info_list(context, &PulseBackend::on_source, snapshot);
                // A list that cannot be asked for counts as done.
                for (pa_operation* op : {sinks, sources}) {
                    if (op == nullptr) {
                        finish(snapshot);
                    }
                    unref(op);
                }
            },
            this));
    }

    static void on_sink(pa_context* /*context*/, const pa_sink_info* info, int eol, void* data) {
        auto* snapshot = static_cast<Snapshot*>(data);
        if (eol != 0 || info == nullptr) {
            finish(snapshot);
            return;
        }
        auto& state = snapshot->state;
        const std::string description = info->description != nullptr ? info->description : info->name;
        state.outputs.push_back({.name = info->name, .description = description});
        snapshot->channels[info->name] = info->volume.channels;
        if (state.output == info->name) {
            state.volume = level(info->volume);
            state.muted = info->mute != 0;
            state.device = description;
        }
    }

    static void on_source(pa_context* /*context*/, const pa_source_info* info, int eol, void* data) {
        auto* snapshot = static_cast<Snapshot*>(data);
        if (eol != 0 || info == nullptr) {
            finish(snapshot);
            return;
        }
        if (info->monitor_of_sink != PA_INVALID_INDEX) {
            return; // what an output plays, not a microphone
        }
        auto& state = snapshot->state;
        state.inputs.push_back(
            {.name = info->name, .description = info->description != nullptr ? info->description : info->name});
        snapshot->channels[info->name] = info->volume.channels;
        if (state.input == info->name) {
            state.input_volume = level(info->volume);
            state.input_muted = info->mute != 0;
        }
    }

    static void finish(Snapshot* snapshot) {
        if (--snapshot->pending > 0) {
            return;
        }
        PulseBackend* self = snapshot->self;
        auto& state = snapshot->state;
        // Without a sink of that name (none yet), there is no output.
        if (std::ranges::none_of(state.outputs, [&](const auto& d) { return d.name == state.output; })) {
            state.output.clear();
            state.device.clear();
        }
        // A monitor as the default input is no microphone.
        if (std::ranges::none_of(state.inputs, [&](const auto& d) { return d.name == state.input; })) {
            state.input.clear();
        }
        self->sink_ = state.output;
        self->source_ = state.input;
        self->channels_ = std::move(snapshot->channels);
        {
            const std::scoped_lock lock(self->mutex_);
            self->shared_ = std::move(state);
        }
        self->wake();
        delete snapshot; // NOLINT(cppcoreguidelines-owning-memory)
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
    pa_context* context_ = nullptr;          // loop lock
    std::string sink_;                       // loop lock: the default output
    std::string source_;                     // loop lock: the default input
    std::map<std::string, uint8_t> channels_; // loop lock: per sink/source
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

void Audio::set_output(const std::string& name) {
    backend_->set_output(name);
}

void Audio::set_input(const std::string& name) {
    backend_->set_input(name);
}

void Audio::set_input_volume(float volume) {
    volume = std::clamp(volume, 0.0F, 1.0F);
    State next = state_;
    next.input_volume = volume;
    const bool unmute = next.input_muted && volume > 0;
    next.input_muted = next.input_muted && !unmute;
    update(next);
    backend_->set_input_volume(volume);
    if (unmute) {
        backend_->set_input_muted(false);
    }
}

void Audio::set_input_muted(bool muted) {
    State next = state_;
    next.input_muted = muted;
    update(next);
    backend_->set_input_muted(muted);
}

void Audio::toggle_mic_mute() {
    backend_->toggle_mic_mute();
}

} // namespace tetra
