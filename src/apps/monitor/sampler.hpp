#pragma once

#include "procfs.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tetra::monitor {

// The latest values of one quantity, oldest first; at most kLength.
class History {
public:
    static constexpr std::size_t kLength = 120;
    void push(double value);
    [[nodiscard]] const std::deque<double>& values() const noexcept { return values_; }
    [[nodiscard]] double last() const noexcept { return values_.empty() ? 0 : values_.back(); }
    [[nodiscard]] double max() const noexcept;

private:
    std::deque<double> values_;
};

struct ProcessRow {
    Process process;
    std::string user;
    double cpu = 0; // percent of the whole machine (all cores together: 0..100)
    std::optional<double> read_rate;  // bytes per second
    std::optional<double> write_rate;
};

// Rates per second of a pair of byte counters.
struct IoRates {
    double in = 0;
    double out = 0;
    IoTotals total;
    History in_history;
    History out_history;
};

// Reads /proc periodically (the UI calls sample() on a timer) and keeps the
// values derived from two readings: CPU use, transfer rates, and a history
// for the graphs.
class Sampler {
public:
    explicit Sampler(std::string root = "/");

    // `elapsed`: seconds since the previous call (for the rates).
    void sample(double elapsed);

    [[nodiscard]] const std::vector<ProcessRow>& processes() const noexcept { return processes_; }
    [[nodiscard]] const History& cpu() const noexcept { return cpu_; }
    [[nodiscard]] const std::vector<History>& cores() const noexcept { return cores_; }
    [[nodiscard]] const std::optional<MemoryStat>& memory() const noexcept { return memory_; }
    [[nodiscard]] const History& memory_history() const noexcept { return memory_history_; } // percent
    [[nodiscard]] const History& swap_history() const noexcept { return swap_history_; }     // percent
    [[nodiscard]] const IoRates& network() const noexcept { return network_; }
    [[nodiscard]] const IoRates& disks() const noexcept { return disks_; }
    [[nodiscard]] const std::vector<Mount>& mounts() const noexcept { return mounts_; }
    [[nodiscard]] int samples() const noexcept { return samples_; }

private:
    const std::string& user_name(uint32_t uid);
    void sample_io(IoRates& rates, const IoTotals& now, double elapsed) const;

    std::string root_;
    int samples_ = 0;
    std::optional<CpuStat> last_cpu_;
    History cpu_;
    std::vector<History> cores_;
    std::optional<MemoryStat> memory_;
    History memory_history_;
    History swap_history_;
    IoRates network_;
    IoRates disks_;
    std::vector<Mount> mounts_;
    std::vector<ProcessRow> processes_;
    std::unordered_map<uint32_t, std::string> users_;
};

} // namespace tetra::monitor
