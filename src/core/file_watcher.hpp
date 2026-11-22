#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <utility>

namespace tetra {

// Watches one file through inotify on its parent directory, so atomic saves
// (write to temp + rename, as most editors do) are seen too. Integrate fd()
// into an event loop and call changed() when it becomes readable.
class FileWatcher {
public:
    // Creates the parent directory if it does not exist.
    static std::expected<FileWatcher, std::string> create(std::filesystem::path file);

    FileWatcher(FileWatcher&& other) noexcept;
    FileWatcher& operator=(FileWatcher&& other) noexcept;
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    ~FileWatcher();

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

    // Drains pending events; true if the watched file was written, created,
    // moved into place or deleted.
    bool changed();

private:
    FileWatcher(int fd, std::filesystem::path file) : fd_(fd), file_(std::move(file)) {}

    int fd_ = -1;
    std::filesystem::path file_;
};

} // namespace tetra
