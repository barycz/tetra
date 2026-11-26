#pragma once

// Readers for the kernel's /proc and /sys files the system monitor shows.
// Every function takes the file system root ("/" normally; a directory of
// fake files in the unit tests) and returns raw counters; rates come from
// the Sampler (sampler.hpp).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tetra::monitor {

// Jiffies spent by one CPU (or all), from /proc/stat.
struct CpuTimes {
    uint64_t busy = 0;
    uint64_t total = 0;
};

struct CpuStat {
    CpuTimes all;
    std::vector<CpuTimes> cores; // cpu0, cpu1, ...
};

// Bytes, from /proc/meminfo.
struct MemoryStat {
    uint64_t total = 0;
    uint64_t available = 0;
    uint64_t cache = 0; // page cache and reclaimable slab
    uint64_t swap_total = 0;
    uint64_t swap_free = 0;
    [[nodiscard]] uint64_t used() const noexcept { return total > available ? total - available : 0; }
    [[nodiscard]] uint64_t swap_used() const noexcept {
        return swap_total > swap_free ? swap_total - swap_free : 0;
    }
};

// Byte counters summed over devices.
struct IoTotals {
    uint64_t in = 0;  // received / read
    uint64_t out = 0; // sent / written
};

struct Process {
    int pid = 0;
    int ppid = 0;
    uint64_t start_time = 0; // jiffies after boot; with pid, identifies the process
    std::string name;        // comm, or argv[0]'s base name when comm is truncated
    std::string command;     // the command line, arguments separated by spaces
    char state = '?';        // R, S, D, T, t, Z, I, ...
    uint32_t uid = 0;
    uint64_t cpu_time = 0;   // utime + stime, jiffies
    uint64_t memory = 0;     // private resident memory (RssAnon), bytes
    uint64_t resident = 0;   // all resident memory (RSS), bytes
    int nice = 0;
    int threads = 0;
    std::optional<IoTotals> io; // bytes read/written to storage; unreadable for others' processes
};

struct Mount {
    std::string device;
    std::string mount_point;
    std::string type;
    uint64_t total = 0;     // bytes
    uint64_t free = 0;      // free, including what only root may use
    uint64_t available = 0; // free for unprivileged users
};

[[nodiscard]] std::optional<CpuStat> read_cpu(const std::string& root);
[[nodiscard]] std::optional<MemoryStat> read_memory(const std::string& root);
// Network traffic of all interfaces except loopback (/proc/net/dev).
[[nodiscard]] IoTotals read_network(const std::string& root);
// Reads and writes of whole disks (/proc/diskstats; disks are the entries
// of /sys/block, without loop and ram devices). Partitions would count twice.
[[nodiscard]] IoTotals read_disks(const std::string& root);
// All processes; ones that exit while being read are left out. `command`
// is read only for processes not in `known` (pid and start time), since a
// process's command line rarely changes.
struct KnownProcess {
    int pid;
    uint64_t start_time;
};
[[nodiscard]] std::vector<Process> read_processes(const std::string& root,
                                                  const std::vector<KnownProcess>& known = {});
// Mounted file systems backed by a device (one entry per device: the
// shortest mount point), with their sizes (statvfs of `root` + mount point).
[[nodiscard]] std::vector<Mount> read_mounts(const std::string& root);

// --- parsing, exposed for the unit tests --------------------------------------
[[nodiscard]] std::optional<Process> parse_stat(std::string_view stat);
// /proc/mounts escapes spaces and the like as \040 (octal).
[[nodiscard]] std::string unescape_mount_path(std::string_view path);

} // namespace tetra::monitor
