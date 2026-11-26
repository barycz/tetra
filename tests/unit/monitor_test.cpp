// Unit tests for the system monitor's /proc readers and rates, on a fake
// /proc tree in a temporary directory.

#include "procfs.hpp"
#include "sampler.hpp"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <source_location>
#include <string>

#include <unistd.h>

namespace {

int failures = 0;

void check(bool ok, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!ok) {
        ++failures;
        std::println(stderr, "FAIL monitor_test.cpp line {}: {}", where.line(), what);
    }
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-6;
}

void write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

using namespace tetra::monitor;

void parse_stat_odd_names() {
    // comm with spaces and a ')' of its own; f[i] is field i + 3.
    const auto p = parse_stat("42 (my (odd) prog) S 1 42 42 0 -1 4194304 10 0 0 0 "
                              "70 30 0 0 20 5 3 0 123456 1000000 25 18446744073709551615");
    check(p.has_value(), "parses");
    if (p) {
        check(p->pid == 42 && p->ppid == 1, "pid and parent");
        check(p->name == "my (odd) prog", "name with parentheses");
        check(p->state == 'S', "state");
        check(p->cpu_time == 100, "utime + stime");
        check(p->nice == 5 && p->threads == 3, "nice and threads");
        check(p->start_time == 123456, "start time");
        check(p->resident == 25 * static_cast<uint64_t>(::sysconf(_SC_PAGESIZE)), "rss in bytes");
    }
    check(!parse_stat("garbage").has_value(), "garbage is rejected");
    check(!parse_stat("1 (short) S 0").has_value(), "too few fields");
}

void mount_paths() {
    check(unescape_mount_path("/mnt/my\\040disk") == "/mnt/my disk", "space");
    check(unescape_mount_path("/a\\134b") == "/a\\b", "backslash");
    check(unescape_mount_path("/end\\04") == "/end\\04", "cut escape stays");
}

void fake_proc() {
    const auto root = std::filesystem::temp_directory_path() / std::format("tetra-monitor-test-{}", ::getpid());
    std::filesystem::remove_all(root);
    const auto page = static_cast<uint64_t>(::sysconf(_SC_PAGESIZE));

    write(root / "proc/stat", "cpu  100 0 100 700 100 0 0 0 0 0\n"
                              "cpu0 50 0 50 350 50 0 0 0 0 0\n"
                              "cpu1 50 0 50 350 50 0 0 0 0 0\n"
                              "intr 1 2 3\n");
    write(root / "proc/meminfo", "MemTotal:        8000000 kB\n"
                                 "MemFree:         1000000 kB\n"
                                 "MemAvailable:    6000000 kB\n"
                                 "Buffers:          100000 kB\n"
                                 "Cached:          2000000 kB\n"
                                 "SwapTotal:       2000000 kB\n"
                                 "SwapFree:        1500000 kB\n"
                                 "SReclaimable:     200000 kB\n");
    write(root / "proc/net/dev",
          "Inter-|   Receive                                                |  Transmit\n"
          " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls "
          "carrier compressed\n"
          "    lo: 5000 10 0 0 0 0 0 0 5000 10 0 0 0 0 0 0\n"
          "  eth0: 1000 10 0 0 0 0 0 0 2000 20 0 0 0 0 0 0\n"
          "wlan0: 500 5 0 0 0 0 0 0 100 1 0 0 0 0 0 0\n");
    // sda (a disk), sda1 (its partition), dm-0 (stacked on sda), loop0.
    write(root / "proc/diskstats", "   8       0 sda 10 0 100 0 20 0 200 0 0 0 0\n"
                                   "   8       1 sda1 10 0 100 0 20 0 200 0 0 0 0\n"
                                   " 253       0 dm-0 10 0 100 0 20 0 200 0 0 0 0\n"
                                   "   7       0 loop0 10 0 100 0 20 0 200 0 0 0 0\n");
    std::filesystem::create_directories(root / "sys/block/sda");
    std::filesystem::create_directories(root / "sys/block/loop0");
    write(root / "sys/block/dm-0/slaves/sda", "");
    // One process with a cut comm, one kernel thread.
    write(root / "proc/42/stat", "42 (a-very-long-pro) R 1 42 42 0 -1 0 0 0 0 0 10 10 0 0 20 0 1 0 500 0 3 0");
    write(root / "proc/42/statm", "100 30 10 1 0 20 0\n");
    write(root / "proc/42/io", "rchar: 1\nwchar: 1\nread_bytes: 4096\nwrite_bytes: 0\n");
    write(root / "proc/42/cmdline", std::string_view("/usr/bin/a-very-long-program\0--flag\0", 36));
    write(root / "proc/7/stat", "7 (kworker/0:1) I 2 0 0 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 5 0 0 0");
    write(root / "proc/self/mounts", "/dev/sda1 / ext4 rw 0 0\n"
                                     "proc /proc proc rw 0 0\n"
                                     "/dev/sda1 /mnt/bind\\040mount ext4 rw 0 0\n");

    const std::string r = root.string();
    const auto cpu = read_cpu(r);
    check(cpu && cpu->cores.size() == 2 && cpu->all.busy == 200 && cpu->all.total == 1000, "cpu times");
    const auto mem = read_memory(r);
    check(mem && mem->total == 8'192'000'000ULL && mem->used() == 2'048'000'000ULL, "memory used");
    check(mem && mem->cache == 2'300'000ULL * 1024 && mem->swap_used() == 500'000ULL * 1024, "cache and swap");
    const auto net = read_network(r);
    check(net.in == 1500 && net.out == 2100, "network without loopback");
    const auto disks = read_disks(r);
    check(disks.in == 100 * 512 && disks.out == 200 * 512, "whole disks only");
    const auto mounts = read_mounts(r);
    check(mounts.size() == 1 && mounts[0].device == "/dev/sda1" && mounts[0].mount_point == "/" &&
              mounts[0].type == "ext4" && mounts[0].total > 0,
          "one mount per device, the shortest path");

    auto processes = read_processes(r);
    check(processes.size() == 2, "both processes");
    for (const auto& p : processes) {
        if (p.pid == 42) {
            check(p.name == "a-very-long-program", "full name for a cut comm");
            check(p.command == "/usr/bin/a-very-long-program --flag", "command line");
            check(p.memory == 20 * page, "private memory: resident - shared");
            check(p.io && p.io->in == 4096, "io");
        } else {
            check(p.name == "kworker/0:1" && p.command.empty() && !p.io, "kernel thread");
        }
    }
    const auto known = read_processes(r, {{42, 500}});
    check(known.size() == 2 && std::ranges::all_of(known, [](const Process& p) { return p.command.empty(); }),
          "known processes: command line not read again");

    // Rates: a second reading one second later.
    Sampler sampler(r);
    sampler.sample(0);
    check(sampler.cpu().values().empty() && sampler.network().in_history.values().empty(), "no rates yet");
    write(root / "proc/stat", "cpu  200 0 200 1200 100 0 0 0 0 0\n"
                              "cpu0 150 0 150 350 50 0 0 0 0 0\n"
                              "cpu1 50 0 50 850 50 0 0 0 0 0\n");
    write(root / "proc/net/dev", "h\nh\n  eth0: 3000 10 0 0 0 0 0 0 2500 20 0 0 0 0 0 0\n");
    write(root / "proc/42/stat", "42 (a-very-long-pro) R 1 42 42 0 -1 0 0 0 0 0 80 40 0 0 20 0 1 0 500 0 3 0");
    write(root / "proc/42/io", "read_bytes: 14096\nwrite_bytes: 2000\n");
    sampler.sample(2);
    check(near(sampler.cpu().last(), 100.0 * 200 / 700), "machine cpu %");
    check(sampler.cores().size() == 2 && near(sampler.cores()[0].last(), 100) && near(sampler.cores()[1].last(), 0),
          "per-core cpu %");
    check(near(sampler.network().in, (3000 - 1500) / 2.0) && near(sampler.network().out, (2500 - 2100) / 2.0),
          "network rates per second");
    bool found = false;
    for (const auto& row : sampler.processes()) {
        if (row.process.pid == 42) {
            found = true;
            check(near(row.cpu, 100.0 * 100 / 700), "process cpu: share of the whole machine");
            check(row.read_rate && near(*row.read_rate, 5000) && row.write_rate && near(*row.write_rate, 1000),
                  "process io rates");
            check(row.process.command == "/usr/bin/a-very-long-program --flag" &&
                      row.process.name == "a-very-long-program",
                  "command and name kept");
        }
    }
    check(found, "process 42 sampled");

    // A new process with the same pid (start time differs): no rates from the old one.
    write(root / "proc/42/stat", "42 (sh) S 1 42 42 0 -1 0 0 0 0 0 500 500 0 0 20 0 1 0 900 0 3 0");
    write(root / "proc/42/cmdline", std::string_view("sh\0", 3));
    sampler.sample(1);
    for (const auto& row : sampler.processes()) {
        if (row.process.pid == 42) {
            check(row.cpu == 0 && !row.read_rate && row.process.name == "sh" && row.process.command == "sh",
                  "reused pid starts fresh");
        }
    }
    std::filesystem::remove_all(root);
}

} // namespace

int monitor_tests() {
    parse_stat_odd_names();
    mount_paths();
    fake_proc();
    return failures;
}
