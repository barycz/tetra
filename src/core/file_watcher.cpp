#include "file_watcher.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <system_error>
#include <utility>

#include <sys/inotify.h>
#include <unistd.h>

namespace tetra {

std::expected<FileWatcher, std::string> FileWatcher::create(std::filesystem::path file) {
    const auto dir = file.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(std::format("cannot create {}: {}", dir.string(), ec.message()));
    }

    const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(std::format("inotify_init1: {}", std::strerror(errno))); // NOLINT
    }
    constexpr uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE;
    if (inotify_add_watch(fd, dir.c_str(), mask) < 0) {
        const int err = errno;
        close(fd);
        return std::unexpected(
            std::format("cannot watch {}: {}", dir.string(), std::strerror(err))); // NOLINT
    }
    return FileWatcher(fd, std::move(file));
}

FileWatcher::FileWatcher(FileWatcher&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), file_(std::move(other.file_)) {}

FileWatcher& FileWatcher::operator=(FileWatcher&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = std::exchange(other.fd_, -1);
        file_ = std::move(other.file_);
    }
    return *this;
}

FileWatcher::~FileWatcher() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool FileWatcher::changed() {
    const std::string name = file_.filename().string();
    bool hit = false;
    alignas(inotify_event) std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t len = read(fd_, buffer.data(), buffer.size());
        if (len <= 0) {
            break;
        }
        for (ssize_t offset = 0; offset < len;) {
            const auto* event =
                reinterpret_cast<const inotify_event*>(buffer.data() + offset); // NOLINT
            if (event->len > 0 && name == event->name) {
                hit = true;
            }
            offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
        }
    }
    return hit;
}

} // namespace tetra
