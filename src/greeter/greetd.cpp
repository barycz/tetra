#include "greetd.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <print>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace tetra::greeter {

GreetdClient::GreetdClient(ui::App& app) : app_(app) {}

GreetdClient::~GreetdClient() {
    disconnect();
}

std::expected<void, std::string> GreetdClient::connect(const std::string& socket_path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(address.sun_path)) {
        return std::unexpected("socket path too long");
    }
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd_ < 0) {
        return std::unexpected(std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
    }
    if (::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) { // NOLINT
        const std::string error = std::format("{}: {}", socket_path, std::strerror(errno)); // NOLINT
        close(fd_);
        fd_ = -1;
        return std::unexpected(error);
    }
    app_.watch_fd(fd_, [this] { read_replies(); });
    return {};
}

bool GreetdClient::send(std::string&& request) {
    std::string message = frame(request);
    explicit_bzero(request.data(), request.size());
    bool ok = fd_ >= 0;
    std::size_t sent = 0;
    while (ok && sent < message.size()) {
        const ssize_t n = ::send(fd_, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            // Requests are tiny; a full socket buffer means greetd is stuck.
            pollfd pfd{.fd = fd_, .events = POLLOUT, .revents = 0};
            ok = poll(&pfd, 1, 2000) > 0;
        } else {
            ok = false;
        }
    }
    explicit_bzero(message.data(), message.size());
    if (!ok) {
        std::println(stderr, "tetra-greeter: lost the connection to greetd");
        disconnect();
        if (on_disconnected) {
            on_disconnected();
        }
    }
    return ok;
}

void GreetdClient::read_replies() {
    std::array<char, 4096> chunk{};
    bool closed = false;
    while (true) {
        const ssize_t n = recv(fd_, chunk.data(), chunk.size(), 0);
        if (n > 0) {
            buffer_.append(chunk.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            break;
        }
        closed = true; // greetd went away (after its last answer, maybe)
        break;
    }
    for (const auto& text : unframe(buffer_)) {
        const Reply reply = parse_reply(text);
        if (reply.type == Reply::Type::Invalid) {
            std::println(stderr, "tetra-greeter: unexpected message from greetd: {}", text);
            continue;
        }
        if (on_reply) {
            on_reply(reply);
        }
        if (fd_ < 0) {
            return;
        }
    }
    if (closed) {
        disconnect();
        if (on_disconnected) {
            on_disconnected();
        }
    }
}

void GreetdClient::disconnect() {
    if (fd_ >= 0) {
        app_.unwatch_fd(fd_);
        close(fd_);
        fd_ = -1;
    }
    buffer_.clear();
}

} // namespace tetra::greeter
