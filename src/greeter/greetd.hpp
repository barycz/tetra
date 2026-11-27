#pragma once

#include "app.hpp"
#include "protocol.hpp"

#include <expected>
#include <functional>
#include <string>

namespace tetra::greeter {

// The connection to greetd ($GREETD_SOCK). Replies are read in the App's
// event loop, so a slow PAM stack (the delay after a wrong password) never
// blocks drawing.
class GreetdClient {
public:
    explicit GreetdClient(ui::App& app);
    ~GreetdClient();

    GreetdClient(const GreetdClient&) = delete;
    GreetdClient& operator=(const GreetdClient&) = delete;
    GreetdClient(GreetdClient&&) = delete;
    GreetdClient& operator=(GreetdClient&&) = delete;

    std::expected<void, std::string> connect(const std::string& socket_path);
    [[nodiscard]] bool connected() const noexcept { return fd_ >= 0; }
    // Sends one request (JSON from protocol.hpp). The text is wiped
    // afterwards: it may hold a password.
    bool send(std::string&& request);

    std::function<void(const Reply&)> on_reply;
    std::function<void()> on_disconnected;

private:
    void read_replies();
    void disconnect();

    ui::App& app_;
    int fd_ = -1;
    std::string buffer_;
};

} // namespace tetra::greeter
