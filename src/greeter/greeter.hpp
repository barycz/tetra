#pragma once

#include "app.hpp"
#include "dbus.hpp"
#include "desktop_entry.hpp"
#include "greetd.hpp"
#include "power.hpp"
#include "users.hpp"
#include "window.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tetra::greeter {

// The login screen: a full-screen layer surface per output with the clock,
// the user (from a list, or typed), a password field, the session to start
// and power buttons. Authentication goes through greetd; once greetd has
// started the session, the greeter exits (and the compositor with it).
class Greeter {
public:
    explicit Greeter(ui::App& app);
    ~Greeter();

    Greeter(const Greeter&) = delete;
    Greeter& operator=(const Greeter&) = delete;
    Greeter(Greeter&&) = delete;
    Greeter& operator=(Greeter&&) = delete;

private:
    // Where the conversation with greetd stands.
    enum class Stage {
        Idle,           // waiting for the user
        Authenticating, // create_session sent, or an answer posted
        Prompt,         // PAM asks something beyond the password (2FA, ...)
        Starting,       // start_session sent
        Cancelling,     // cancel_session sent after a failure
    };

    void add_output(const ui::OutputInfo& output);
    void draw();
    void draw_user_picker(float x, float width);
    void draw_footer(float width, float height);
    void redraw();
    void schedule_clock();

    [[nodiscard]] std::string username() const;
    void submit();
    void handle_reply(const Reply& reply);
    void start_session();
    void fail(std::string message);
    void wipe();

    ui::App& app_;
    std::map<uint32_t, std::unique_ptr<ui::LayerWindow>> windows_; // by wl_output global
    GreetdClient greetd_;
    std::unique_ptr<ui::DBusConnection> system_bus_;
    std::unique_ptr<ui::Power> power_;

    std::vector<User> users_;
    std::size_t user_ = 0; // == users_.size(): "Other user" (typed name)
    std::array<char, 128> typed_name_{};
    std::vector<DesktopEntry> sessions_;
    std::size_t session_ = 0;

    Stage stage_ = Stage::Idle;
    std::array<char, 256> answer_{}; // the password field
    std::string pending_password_;   // typed before greetd asked for it
    bool password_pending_ = false;
    std::string prompt_; // PAM's question in Stage::Prompt
    bool prompt_secret_ = true;
    std::string info_;
    std::string error_;
    bool focus_name_ = false; // put the keyboard in the user name field

    std::filesystem::path state_path_;
    State state_;
    ui::App::TimerId clock_timer_ = 0;
};

} // namespace tetra::greeter
