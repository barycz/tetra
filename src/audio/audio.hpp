#pragma once

#include "app.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tetra {

// Sound devices through the PulseAudio API (served by pipewire-pulse): the
// outputs and microphones, which ones are the defaults, and their volume
// and mute state. Reconnects if the sound server goes away. Used by the
// shell (bar, volume keys, popup) and Settings (Sound).
//
// TETRA_AUDIO=fake uses in-memory devices instead (headless tests must not
// change the real volume; tests that need a real server run a private one).
class Audio {
public:
    struct Device {
        std::string name;        // the sound server's name for it
        std::string description; // for people
        bool operator==(const Device&) const = default;
    };

    struct State {
        bool available = false;
        // The default output.
        float volume = 0; // 0..1 (100%)
        bool muted = false;
        std::string device; // its description
        std::string output; // its name
        std::vector<Device> outputs;
        // The default input (microphone); empty `input` if there is none.
        float input_volume = 0;
        bool input_muted = false;
        std::string input;
        std::vector<Device> inputs; // microphones and line-ins, not output monitors
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
    void set_output(const std::string& name);
    void set_input(const std::string& name);
    void set_input_volume(float volume);
    void set_input_muted(bool muted);
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
        virtual void set_output(const std::string& name) = 0;
        virtual void set_input(const std::string& name) = 0;
        virtual void set_input_volume(float volume) = 0;
        virtual void set_input_muted(bool muted) = 0;
        virtual void toggle_mic_mute() = 0;
    };

private:
    void update(const State& state);

    ui::App& app_;
    State state_;
    std::unique_ptr<Backend> backend_;
};

} // namespace tetra
