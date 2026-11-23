#pragma once

#include "app.hpp"

#include <functional>
#include <memory>
#include <string>

namespace tetra::shell {

// The default output's volume and mute state, through the PulseAudio API
// (served by pipewire-pulse). Reconnects if the sound server goes away.
// TETRA_AUDIO=fake uses an in-memory device instead (headless tests must
// not change the real volume).
class Audio {
public:
    struct State {
        bool available = false;
        float volume = 0; // 0..1 (100%)
        bool muted = false;
        std::string device; // description of the default output
        bool operator==(const State&) const = default;
    };

    explicit Audio(ui::App& app);
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) = delete;
    Audio& operator=(Audio&&) = delete;

    [[nodiscard]] const State& state() const noexcept { return state_; }
    void set_volume(float volume);
    void change_volume(float delta);
    void set_muted(bool muted);
    void toggle_mute() { set_muted(!state_.muted); }
    void toggle_mic_mute();

    // Called on the UI thread whenever state() changed.
    std::function<void()> on_changed;

    // Implementation interface; the PulseAudio and fake backends live in
    // audio.cpp.
    class Backend {
    public:
        virtual ~Backend() = default;
        virtual void set_volume(float volume) = 0;
        virtual void set_muted(bool muted) = 0;
        virtual void toggle_mic_mute() = 0;
    };

private:
    void update(const State& state);

    ui::App& app_;
    State state_;
    std::unique_ptr<Backend> backend_;
};

} // namespace tetra::shell
