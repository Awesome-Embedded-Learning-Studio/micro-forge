/**
 * @file bench.cpp
 * @brief End-to-end ips bench: runs one guest loop image on the executor
 *        and reports the median instructions-per-second over repetitions.
 * @version 0.1.0
 *
 * Usage: mf-bench <name> <guest.bin> [warmup] [measure] [reps]
 *                 [--baseline bench/baseline.txt]
 *
 * Method inherited from the v1 baseline (PERF-METHODOLOGY.md §4): pinned
 * cores, RelWithDebInfo build, warmup/measure/rep counts, reported value
 * is the median. The gate is advisory: perf is noise-sensitive, so this
 * prints PASS/REGRESS per scenario and exits 0 (re-run to separate a
 * noise dip from a real regression; bump the baseline after intentional
 * wins). Regression is flagged at now/baseline < 1 - tolerance, with the
 * stored values pinned at ~median*0.85 so the 15% tolerance absorbs
 * scheduler jitter.
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <print>
#include <string>
#include <vector>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/kernel/bus.hpp>
#include <micro_forge/kernel/ram_region.hpp>
#include <micro_forge/kernel/scheduler.hpp>

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint64_t kCodeBase = 0x100000;
constexpr uint64_t kStackTop = 0x90000;
constexpr uint64_t kLoopCell = 0x80000;

struct RepResult {
    micro_forge::kernel::Tick instructions = 0;
    double seconds = 0.0;
};

/// One full run of a guest image; returns retired instructions and wall
/// time. The guest exits via syscall, which is the run's natural end.
RepResult run_once(const std::vector<uint8_t>& image, uint64_t trips) {
    micro_forge::kernel::Bus bus;
    micro_forge::kernel::RamRegion ram(0x200000);
    micro_forge::kernel::Scheduler scheduler;
    micro_forge::arch::x86_64::Cpu cpu(bus, scheduler);
    if (!bus.map(0, ram.size(), ram).has_value()) {
        std::println(stderr, "mapping RAM failed");
        std::exit(2); // NOLINT(concurrency-mt-unsafe)
    }
    std::ranges::copy(image, ram.data() + kCodeBase);
    // Program the trip count through the bus so the guest reads it.
    if (!bus.write_u64(kLoopCell, trips).has_value()) {
        std::println(stderr, "programming loop cell failed");
        std::exit(2); // NOLINT(concurrency-mt-unsafe)
    }
    cpu.reset(kCodeBase, kStackTop);

    const auto begin = Clock::now();
    const auto exit = cpu.run(std::numeric_limits<uint64_t>::max()); // no budget
    const auto end = Clock::now();
    if (exit.kind != micro_forge::arch::x86_64::ExitStatus::Kind::kExit) {
        std::println(stderr, "guest stopped unexpectedly: kind={} rip={:#x}",
                     static_cast<unsigned>(exit.kind), exit.rip);
        std::exit(2); // NOLINT(concurrency-mt-unsafe)
    }
    return RepResult{
        .instructions = cpu.icount(),
        .seconds = std::chrono::duration<double>(end - begin).count(),
    };
}

/// Parses baseline.txt: "#"-comments, "tolerance: X", "name ips" rows.
std::map<std::string, double> load_baseline(const std::string& path, double& tolerance) {
    std::map<std::string, double> rows;
    std::ifstream f(path);
    if (!f) {
        std::println(stderr, "cannot open baseline {}", path);
        std::exit(2); // NOLINT(concurrency-mt-unsafe)
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.starts_with("tolerance:")) {
            tolerance = std::stod(line.substr(10));
            continue;
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto split = line.find(' ');
        if (split != std::string::npos) {
            rows[line.substr(0, split)] = std::stod(line.substr(split + 1));
        }
    }
    return rows;
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char* const* argv) {
    if (argc < 3) {
        std::println(stderr, "usage: mf-bench <name> <guest.bin> [warmup] [measure] [reps] "
                             "[--baseline file]");
        return 2;
    }
    const std::string name = argv[1];
    uint64_t warmup = 800000;
    uint64_t measure = 8000000;
    unsigned reps = 11;
    std::string baseline_path;
    std::vector<std::string> positional;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--baseline" && i + 1 < argc) {
            baseline_path = argv[++i];
            continue;
        }
        positional.push_back(arg);
    }
    if (positional.size() > 3) {
        std::println(stderr, "too many positional arguments");
        return 2;
    }
    if (!positional.empty()) {
        warmup = std::stoull(positional[0]);
    }
    if (positional.size() > 1) {
        measure = std::stoull(positional[1]);
    }
    if (positional.size() > 2) {
        reps = static_cast<unsigned>(std::stoul(positional[2]));
    }

    std::ifstream image_file(argv[2], std::ios::binary);
    if (!image_file) {
        std::println(stderr, "cannot open guest image {}", argv[2]);
        return 2;
    }
    const std::vector<uint8_t> image((std::istreambuf_iterator<char>(image_file)),
                                     std::istreambuf_iterator<char>());

    // Warmup run outside the measured set.
    run_once(image, warmup);

    std::vector<double> ips;
    micro_forge::kernel::Tick sample_instructions = 0;
    ips.reserve(reps);
    for (unsigned r = 0; r < reps; ++r) {
        const RepResult rep = run_once(image, measure);
        sample_instructions = rep.instructions;
        ips.push_back(static_cast<double>(rep.instructions) / rep.seconds);
    }
    std::ranges::sort(ips);
    const double median = ips[ips.size() / 2];
    std::println("{} median {:.0f} ips ({:.2f}M insns/rep, reps={})", name, median,
                 static_cast<double>(sample_instructions) / 1e6, reps);

    if (!baseline_path.empty()) {
        double tolerance = 0.15;
        const auto rows = load_baseline(baseline_path, tolerance);
        const auto it = rows.find(name);
        if (it == rows.end()) {
            std::println("baseline has no row for {}", name);
            return 0;
        }
        const double floor = it->second * (1.0 - tolerance);
        std::println("{} {} (now {:.0f}, baseline {:.0f}, floor {:.0f}, tolerance {:.0f}%)", name,
                     median >= floor ? "PASS" : "REGRESS", median, it->second, floor,
                     tolerance * 100.0);
    }
    return 0; // advisory: never hard-fail on perf noise (v1 precedent)
}
