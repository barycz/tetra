#include "auth.hpp"

#include "app.hpp"

#include <security/pam_appl.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <print>
#include <span>

#include <pwd.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace tetra::shell {

namespace {

constexpr const char* kService = "tetra";

void wipe(std::string& secret) {
    explicit_bzero(secret.data(), secret.size());
    secret.clear();
}

// PAM conversation: answer every prompt with the password (pam_unix asks
// once, with echo off); informational messages are ignored.
int converse(int count, const pam_message** messages, pam_response** responses, void* data) {
    if (count <= 0) {
        return PAM_CONV_ERR;
    }
    const auto* password = static_cast<const std::string*>(data);
    auto* replies = static_cast<pam_response*>(std::calloc(static_cast<std::size_t>(count),
                                                           sizeof(pam_response))); // NOLINT
    if (replies == nullptr) {
        return PAM_BUF_ERR;
    }
    const auto items = std::span(messages, static_cast<std::size_t>(count));
    const auto out = std::span(replies, static_cast<std::size_t>(count));
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int style = items[i]->msg_style;
        if (style == PAM_PROMPT_ECHO_OFF || style == PAM_PROMPT_ECHO_ON) {
            out[i].resp = strdup(password->c_str()); // PAM frees it
            if (out[i].resp == nullptr) {
                for (std::size_t j = 0; j < i; ++j) {
                    std::free(out[j].resp); // NOLINT
                }
                std::free(replies); // NOLINT
                return PAM_BUF_ERR;
            }
        }
    }
    *responses = replies;
    return PAM_SUCCESS;
}

bool check_password(const std::string& password) {
    const passwd* pw = getpwuid(getuid()); // NOLINT(concurrency-mt-unsafe): only this thread
    if (pw == nullptr) {
        std::println(stderr, "tetra-shell: lock screen: unknown user {}", getuid());
        return false;
    }
    const pam_conv conversation{.conv = &converse,
                                .appdata_ptr = const_cast<std::string*>(&password)}; // NOLINT
    pam_handle_t* handle = nullptr;
    int status = pam_start(kService, pw->pw_name, &conversation, &handle);
    if (status != PAM_SUCCESS) {
        std::println(stderr, "tetra-shell: lock screen: pam_start failed: {}",
                     pam_strerror(handle, status));
        return false;
    }
    status = pam_authenticate(handle, 0);
    if (status == PAM_SUCCESS) {
        status = pam_setcred(handle, PAM_REFRESH_CRED); // e.g. renew Kerberos tickets
        if (status != PAM_SUCCESS) {
            std::println(stderr, "tetra-shell: lock screen: pam_setcred: {}",
                         pam_strerror(handle, status));
            status = PAM_SUCCESS; // the password was right; do not keep the user out
        }
    }
    pam_end(handle, status);
    return status == PAM_SUCCESS;
}

} // namespace

Authenticator::Authenticator(ui::App& app) : app_(app) {
    event_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (event_fd_ >= 0) {
        app_.watch_fd(event_fd_, [this] { on_wakeup(); });
    }
}

Authenticator::~Authenticator() {
    if (worker_.joinable()) {
        worker_.join(); // PAM cannot be cancelled; at most a few seconds
    }
    if (event_fd_ >= 0) {
        app_.unwatch_fd(event_fd_);
        close(event_fd_);
    }
}

void Authenticator::start(std::string password, std::function<void(bool ok)> done) {
    if (busy_ || event_fd_ < 0) {
        wipe(password);
        return;
    }
    if (worker_.joinable()) {
        worker_.join(); // the previous attempt has already reported
    }
    busy_ = true;
    done_ = std::move(done);
    worker_ = std::jthread([this, secret = std::move(password)]() mutable {
        result_ = check_password(secret);
        wipe(secret);
        const uint64_t one = 1;
        [[maybe_unused]] const auto n = write(event_fd_, &one, sizeof(one));
    });
}

void Authenticator::on_wakeup() {
    uint64_t count = 0;
    [[maybe_unused]] const auto n = read(event_fd_, &count, sizeof(count));
    if (!busy_) {
        return;
    }
    busy_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    if (auto done = std::move(done_)) {
        done_ = nullptr;
        done(result_);
    }
}

} // namespace tetra::shell
