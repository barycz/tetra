#pragma once

#include "app.hpp"
#include "auth.hpp"
#include "window.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

struct ext_session_lock_manager_v1;
struct ext_session_lock_v1;

namespace tetra::shell {

class Shell;

// The lock screen (ext-session-lock-v1): one surface per output with the
// clock, the user's name and a password field checked through PAM. The
// compositor covers everything else until we unlock; if this process dies,
// the session stays locked and the restarted shell locks again.
class LockScreen {
public:
    LockScreen(Shell& shell, ext_session_lock_manager_v1* manager);
    ~LockScreen();

    LockScreen(const LockScreen&) = delete;
    LockScreen& operator=(const LockScreen&) = delete;
    LockScreen(LockScreen&&) = delete;
    LockScreen& operator=(LockScreen&&) = delete;

    // Requested or locked.
    [[nodiscard]] bool active() const noexcept { return lock_ != nullptr; }
    [[nodiscard]] bool locked() const noexcept { return locked_; }

    void lock(const std::map<uint32_t, ui::OutputInfo>& outputs);
    void add_output(const ui::OutputInfo& output);
    void remove_output(const ui::OutputInfo& output);
    void redraw();

    // Locked (the compositor confirmed it) or unlocked/abandoned.
    std::function<void(bool locked)> on_changed;

private:
    friend struct LockScreenListener;
    void draw();
    void submit();
    void unlock();
    void finish();
    void clear_password();

    Shell& shell_;
    ext_session_lock_manager_v1* manager_;
    ext_session_lock_v1* lock_ = nullptr;
    bool locked_ = false;
    bool unlock_pending_ = false; // password accepted before "locked" arrived
    std::map<uint32_t, std::unique_ptr<ui::LockWindow>> windows_; // by wl_output global
    Authenticator auth_;
    std::array<char, 256> password_{};
    std::string error_;
    std::string user_name_;
};

} // namespace tetra::shell
