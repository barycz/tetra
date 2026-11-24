#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace tetra::ui {
class App;
}

namespace tetra::shell {

// Checks the user's password with PAM (service "tetra"; without
// /etc/pam.d/tetra, PAM falls back to "other", which on Debian is the
// common-auth stack). PAM may block for seconds (pam_unix delays failures),
// so it runs on a worker thread; the result comes back on the UI thread.
class Authenticator {
public:
    explicit Authenticator(ui::App& app);
    ~Authenticator();

    Authenticator(const Authenticator&) = delete;
    Authenticator& operator=(const Authenticator&) = delete;
    Authenticator(Authenticator&&) = delete;
    Authenticator& operator=(Authenticator&&) = delete;

    [[nodiscard]] bool busy() const noexcept { return busy_; }
    // Takes the password (wiped after use); calls `done` on the UI thread.
    void start(std::string password, std::function<void(bool ok)> done);

private:
    void on_wakeup();

    ui::App& app_;
    int event_fd_ = -1;
    bool busy_ = false;
    std::atomic<bool> result_ = false;
    std::function<void(bool)> done_;
    std::jthread worker_;
};

} // namespace tetra::shell
