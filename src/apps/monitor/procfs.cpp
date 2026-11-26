#include "procfs.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <ranges>

namespace tetra::monitor {

namespace {

std::optional<std::string> read_file(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::nullopt;
    }
    std::string text;
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t n = ::read(fd, buffer.data(), buffer.size());
        if (n < 0) {
            ::close(fd);
            return std::nullopt;
        }
        if (n == 0) {
            break;
        }
        text.append(buffer.data(), static_cast<std::size_t>(n));
    }
    ::close(fd);
    return text;
}

// Whitespace-separated fields of one line.
std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            ++i;
        }
        if (i > start) {
            out.push_back(line.substr(start, i - start));
        }
    }
    return out;
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    for (auto part : std::views::split(text, '\n')) {
        out.emplace_back(part.begin(), part.end());
    }
    return out;
}

template <typename T>
T number(std::string_view text, T fallback = 0) {
    T value = fallback;
    const auto* end = text.data() + text.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (std::from_chars(text.data(), end, value).ec != std::errc()) {
        return fallback;
    }
    return value;
}

bool all_digits(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

bool has_entries(const std::string& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) {
        return false;
    }
    bool found = false;
    while (const dirent* entry = ::readdir(d)) {
        const std::string_view name = entry->d_name; // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if (name != "." && name != "..") {
            found = true;
            break;
        }
    }
    ::closedir(d);
    return found;
}

uint64_t page_size() {
    static const auto size = static_cast<uint64_t>(::sysconf(_SC_PAGESIZE));
    return size;
}

std::string_view base_name(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

} // namespace

std::optional<CpuStat> read_cpu(const std::string& root) {
    const auto text = read_file(root + "/proc/stat");
    if (!text) {
        return std::nullopt;
    }
    CpuStat stat;
    bool found = false;
    for (const auto line : lines(*text)) {
        if (!line.starts_with("cpu")) {
            continue;
        }
        const auto f = fields(line);
        if (f.size() < 5) {
            continue;
        }
        // user nice system idle iowait irq softirq steal (guest time is
        // already part of user and nice)
        std::array<uint64_t, 8> v{};
        for (std::size_t i = 0; i < v.size() && i + 1 < f.size(); ++i) {
            v.at(i) = number<uint64_t>(f[i + 1]);
        }
        const uint64_t idle = v[3] + v[4];
        const uint64_t busy = v[0] + v[1] + v[2] + v[5] + v[6] + v[7];
        const CpuTimes times{.busy = busy, .total = busy + idle};
        if (f[0] == "cpu") {
            stat.all = times;
            found = true;
        } else {
            stat.cores.push_back(times);
        }
    }
    if (!found) {
        return std::nullopt;
    }
    return stat;
}

std::optional<MemoryStat> read_memory(const std::string& root) {
    const auto text = read_file(root + "/proc/meminfo");
    if (!text) {
        return std::nullopt;
    }
    MemoryStat m;
    bool have_available = false;
    uint64_t free = 0;
    for (const auto line : lines(*text)) {
        const auto f = fields(line);
        if (f.size() < 2) {
            continue;
        }
        const uint64_t bytes = number<uint64_t>(f[1]) * 1024;
        if (f[0] == "MemTotal:") {
            m.total = bytes;
        } else if (f[0] == "MemAvailable:") {
            m.available = bytes;
            have_available = true;
        } else if (f[0] == "MemFree:") {
            free = bytes;
        } else if (f[0] == "Cached:" || f[0] == "Buffers:" || f[0] == "SReclaimable:") {
            m.cache += bytes;
        } else if (f[0] == "SwapTotal:") {
            m.swap_total = bytes;
        } else if (f[0] == "SwapFree:") {
            m.swap_free = bytes;
        }
    }
    if (m.total == 0) {
        return std::nullopt;
    }
    if (!have_available) {
        m.available = std::min(m.total, free + m.cache);
    }
    return m;
}

IoTotals read_network(const std::string& root) {
    IoTotals totals;
    const auto text = read_file(root + "/proc/net/dev");
    if (!text) {
        return totals;
    }
    for (const auto line : lines(*text)) {
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue; // the two header lines
        }
        const auto name = fields(line.substr(0, colon));
        if (name.size() != 1 || name[0] == "lo") {
            continue;
        }
        const auto f = fields(line.substr(colon + 1));
        if (f.size() >= 9) {
            totals.in += number<uint64_t>(f[0]);
            totals.out += number<uint64_t>(f[8]);
        }
    }
    return totals;
}

IoTotals read_disks(const std::string& root) {
    IoTotals totals;
    const auto text = read_file(root + "/proc/diskstats");
    if (!text) {
        return totals;
    }
    for (const auto line : lines(*text)) {
        const auto f = fields(line);
        if (f.size() < 10) {
            continue;
        }
        const std::string name(f[2]);
        if (name.starts_with("loop") || name.starts_with("ram") || name.starts_with("zram")) {
            continue;
        }
        const std::string block = root + "/sys/block/" + name;
        if (::access(block.c_str(), F_OK) != 0 || has_entries(block + "/slaves")) {
            continue; // a partition, or stacked on other disks (dm, md): counted there
        }
        totals.in += number<uint64_t>(f[5]) * 512; // sectors are always 512 bytes here
        totals.out += number<uint64_t>(f[9]) * 512;
    }
    return totals;
}

std::optional<Process> parse_stat(std::string_view stat) {
    // pid (comm) state ppid ... ; comm may itself contain spaces and ')'.
    const auto open = stat.find('(');
    const auto close = stat.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
        return std::nullopt;
    }
    Process p;
    const auto head = fields(stat.substr(0, open));
    p.pid = head.size() == 1 ? number<int>(head[0], -1) : -1;
    p.name = std::string(stat.substr(open + 1, close - open - 1));
    const auto f = fields(stat.substr(close + 1));
    // f[i] is field i + 3 of proc_pid_stat(5).
    if (p.pid < 0 || f.size() < 22) {
        return std::nullopt;
    }
    p.state = f[0].empty() ? '?' : f[0][0];
    p.ppid = number<int>(f[1]);
    p.cpu_time = number<uint64_t>(f[11]) + number<uint64_t>(f[12]);
    p.nice = number<int>(f[16]);
    p.threads = number<int>(f[17]);
    p.start_time = number<uint64_t>(f[19]);
    p.resident = number<uint64_t>(f[21]) * page_size();
    return p;
}

std::vector<Process> read_processes(const std::string& root, const std::vector<KnownProcess>& known) {
    std::vector<Process> out;
    const std::string proc = root + "/proc";
    DIR* dir = ::opendir(proc.c_str());
    if (dir == nullptr) {
        return out;
    }
    while (const dirent* entry = ::readdir(dir)) {
        const std::string_view name = entry->d_name; // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if (!all_digits(name)) {
            continue;
        }
        const std::string base = proc + "/" + std::string(name);
        const auto stat_text = read_file(base + "/stat");
        if (!stat_text) {
            continue; // exited meanwhile
        }
        auto p = parse_stat(*stat_text);
        if (!p) {
            continue;
        }
        struct stat st {};
        if (::stat(base.c_str(), &st) == 0) {
            p->uid = st.st_uid;
        }
        if (const auto statm = read_file(base + "/statm")) {
            const auto f = fields(*statm);
            if (f.size() >= 3) {
                const auto resident = number<uint64_t>(f[1]);
                const auto shared = number<uint64_t>(f[2]);
                p->memory = (resident > shared ? resident - shared : 0) * page_size();
            }
        }
        if (const auto io = read_file(base + "/io")) {
            IoTotals totals;
            for (const auto line : lines(*io)) {
                const auto f = fields(line);
                if (f.size() == 2 && f[0] == "read_bytes:") {
                    totals.in = number<uint64_t>(f[1]);
                } else if (f.size() == 2 && f[0] == "write_bytes:") {
                    totals.out = number<uint64_t>(f[1]);
                }
            }
            p->io = totals;
        }
        const bool is_known = std::ranges::any_of(known, [&](const KnownProcess& k) {
            return k.pid == p->pid && k.start_time == p->start_time;
        });
        if (!is_known) {
            if (auto cmdline = read_file(base + "/cmdline")) {
                while (!cmdline->empty() && cmdline->back() == '\0') {
                    cmdline->pop_back();
                }
                const std::string argv0 = cmdline->substr(0, cmdline->find('\0'));
                std::ranges::replace(*cmdline, '\0', ' ');
                p->command = std::move(*cmdline);
                // comm is cut at 15 characters; the program's file name is not.
                const auto program = base_name(argv0);
                if (p->name.size() == 15 && program.starts_with(p->name)) {
                    p->name = std::string(program.substr(0, program.find(' ')));
                }
            }
        }
        out.push_back(std::move(*p));
    }
    ::closedir(dir);
    return out;
}

std::string unescape_mount_path(std::string_view path) {
    std::string out;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '\\' && i + 3 < path.size() &&
            std::ranges::all_of(path.substr(i + 1, 3), [](char c) { return c >= '0' && c <= '7'; })) {
            const int value = ((path[i + 1] - '0') * 64) + ((path[i + 2] - '0') * 8) + (path[i + 3] - '0');
            out += static_cast<char>(value);
            i += 3;
        } else {
            out += path[i];
        }
    }
    return out;
}

std::vector<Mount> read_mounts(const std::string& root) {
    std::vector<Mount> out;
    const auto text = read_file(root + "/proc/self/mounts");
    if (!text) {
        return out;
    }
    for (const auto line : lines(*text)) {
        const auto f = fields(line);
        if (f.size() < 3 || !f[0].starts_with('/')) {
            continue; // not backed by a device (proc, tmpfs, cgroup2, ...)
        }
        Mount m{.device = unescape_mount_path(f[0]),
                .mount_point = unescape_mount_path(f[1]),
                .type = std::string(f[2])};
        auto same = std::ranges::find(out, m.device, &Mount::device);
        if (same != out.end()) {
            if (m.mount_point.size() < same->mount_point.size()) {
                same->mount_point = m.mount_point;
            }
            continue;
        }
        out.push_back(std::move(m));
    }
    std::erase_if(out, [&](Mount& m) {
        struct statvfs vfs {};
        if (::statvfs((root + m.mount_point).c_str(), &vfs) != 0) {
            return true;
        }
        m.total = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
        m.free = static_cast<uint64_t>(vfs.f_bfree) * vfs.f_frsize;
        m.available = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
        return m.total == 0;
    });
    std::ranges::sort(out, {}, &Mount::mount_point);
    return out;
}

} // namespace tetra::monitor
