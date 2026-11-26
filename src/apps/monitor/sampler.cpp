#include "sampler.hpp"

#include <pwd.h>

#include <algorithm>
#include <array>
#include <utility>

namespace tetra::monitor {

void History::push(double value) {
    values_.push_back(value);
    while (values_.size() > kLength) {
        values_.pop_front();
    }
}

double History::max() const noexcept {
    return values_.empty() ? 0 : *std::ranges::max_element(values_);
}

Sampler::Sampler(std::string root) : root_(std::move(root)) {}

const std::string& Sampler::user_name(uint32_t uid) {
    auto it = users_.find(uid);
    if (it == users_.end()) {
        std::string name = std::to_string(uid);
        passwd pw{};
        passwd* found = nullptr;
        std::array<char, 1024> buffer{};
        if (::getpwuid_r(uid, &pw, buffer.data(), buffer.size(), &found) == 0 && found != nullptr) {
            name = found->pw_name;
        }
        it = users_.emplace(uid, std::move(name)).first;
    }
    return it->second;
}

void Sampler::sample_io(IoRates& rates, const IoTotals& now, double elapsed) const {
    if (samples_ > 0 && elapsed > 0) {
        // Counters go back when a device goes away; that is not traffic.
        rates.in = now.in >= rates.total.in ? static_cast<double>(now.in - rates.total.in) / elapsed : 0;
        rates.out = now.out >= rates.total.out ? static_cast<double>(now.out - rates.total.out) / elapsed : 0;
        rates.in_history.push(rates.in);
        rates.out_history.push(rates.out);
    }
    rates.total = now;
}

void Sampler::sample(double elapsed) {
    // CPU
    uint64_t total_delta = 0;
    if (auto cpu = read_cpu(root_)) {
        if (last_cpu_) {
            const auto percent = [](const CpuTimes& now, const CpuTimes& before) {
                const uint64_t total = now.total - before.total;
                const uint64_t busy = now.busy - before.busy;
                return total == 0 || now.total < before.total || now.busy < before.busy
                           ? 0.0
                           : 100.0 * static_cast<double>(busy) / static_cast<double>(total);
            };
            cpu_.push(percent(cpu->all, last_cpu_->all));
            cores_.resize(cpu->cores.size());
            for (std::size_t i = 0; i < cpu->cores.size(); ++i) {
                cores_[i].push(i < last_cpu_->cores.size() ? percent(cpu->cores[i], last_cpu_->cores[i]) : 0);
            }
            total_delta = cpu->all.total >= last_cpu_->all.total ? cpu->all.total - last_cpu_->all.total : 0;
        } else {
            cores_.resize(cpu->cores.size());
        }
        last_cpu_ = std::move(cpu);
    }

    // Memory
    memory_ = read_memory(root_);
    if (memory_) {
        memory_history_.push(100.0 * static_cast<double>(memory_->used()) / static_cast<double>(memory_->total));
        swap_history_.push(memory_->swap_total == 0 ? 0
                                                    : 100.0 * static_cast<double>(memory_->swap_used()) /
                                                          static_cast<double>(memory_->swap_total));
    }

    sample_io(network_, read_network(root_), elapsed);
    sample_io(disks_, read_disks(root_), elapsed);
    mounts_ = read_mounts(root_);

    // Processes: CPU and I/O from the difference to the previous reading of
    // the same process (pid and start time).
    std::unordered_map<int, const ProcessRow*> before;
    std::vector<KnownProcess> known;
    before.reserve(processes_.size());
    known.reserve(processes_.size());
    for (const auto& row : processes_) {
        before.emplace(row.process.pid, &row);
        known.push_back({row.process.pid, row.process.start_time});
    }
    std::vector<ProcessRow> rows;
    for (auto& p : read_processes(root_, known)) {
        ProcessRow row;
        auto it = before.find(p.pid);
        const ProcessRow* prev =
            it != before.end() && it->second->process.start_time == p.start_time ? it->second : nullptr;
        if (prev != nullptr) {
            if (total_delta > 0 && p.cpu_time >= prev->process.cpu_time) {
                row.cpu = std::min(100.0, 100.0 * static_cast<double>(p.cpu_time - prev->process.cpu_time) /
                                              static_cast<double>(total_delta));
            }
            if (p.io && prev->process.io && elapsed > 0) {
                const auto delta = [&](uint64_t now, uint64_t then) {
                    return now >= then ? static_cast<double>(now - then) / elapsed : 0.0;
                };
                row.read_rate = delta(p.io->in, prev->process.io->in);
                row.write_rate = delta(p.io->out, prev->process.io->out);
            }
            if (p.command.empty()) { // known: not read again
                p.command = prev->process.command;
                if (prev->process.name.starts_with(p.name)) { // the full name found for a cut comm
                    p.name = prev->process.name;
                }
            }
        }
        row.user = user_name(p.uid);
        row.process = std::move(p);
        rows.push_back(std::move(row));
    }
    processes_ = std::move(rows);
    ++samples_;
}

} // namespace tetra::monitor
