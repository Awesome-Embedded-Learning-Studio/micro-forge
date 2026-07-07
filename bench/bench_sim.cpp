// bench_sim.cpp — micro-forge perf bench (PERF-METHODOLOGY.md §3 Phase 0).
//
// Loads a real armcc/AC6 firmware corpus, warms up to the steady-state main
// loop, then times a fixed instruction budget and reports insn/sec. Scenarios
// are a *combination* (PERF-METHODOLOGY §2C): a tight GPIO toggle loop (pure
// CPU + bus hot path), a UART-printf loop (memory/IO heavy), and a TIM
// time-base loop (timer-driven). Built RelWithDebInfo + MF_PERF_STATS so the
// per-instruction cost breakdown is dumped alongside the throughput number.
//
//   bench_sim [warmup_steps=1_000_000] [measure_steps=10_000_000] [reps=5]
//   BENCH_SCENARIOS="gpio_iotoggle;uart_printf"  (subset; default = all 3)

#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "cpu/cpu.hpp"
#include "util/perf_stats.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

namespace {

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

struct Scenario {
    const char* stem; // e.g. "gpio_iotoggle"
    const char* tag;  // short label for output
};

// Default corpus: AC6 -O2 builds (the realistic optimized codegen). The -O0/-Oz
// variants exist under test/firmware/armcc for differential checks.
constexpr Scenario kScenarios[] = {
    {"nucleo_f103rb_gpio_iotoggle.ac6-O2", "gpio_iotoggle-O2"},
    {"nucleo_f103rb_uart_printf.ac6-O2", "uart_printf-O2"},
    {"nucleo_f103rb_tim_timebase.ac6-O2", "tim_timebase-O2"},
};

// Ctrl+C between scenarios → report what we have.
volatile std::sig_atomic_t g_interrupted = 0;
void on_sigint(int) { g_interrupted = 1; }

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    if (v.empty()) {
        return 0.0;
    }
    return v[v.size() / 2];
}

// Returns the median insn/sec, or <0 if the scenario was skipped/failed.
double run_scenario(const Scenario& s, size_t warmup, size_t measure,
                    size_t reps) {
    std::string path = std::string(MF_BENCH_FW_DIR) + "/" + s.stem + ".axf";
    auto data = read_file(path.c_str());
    if (data.empty()) {
        std::fprintf(stderr, "[bench] SKIP %s: missing corpus %s\n", s.tag,
                     path.c_str());
        return -1.0;
    }

    std::vector<double> ips; // insn/sec per rep
    ips.reserve(reps);

    for (size_t r = 0; r < reps && !g_interrupted; ++r) {
        auto soc = Stm32f103Soc::create();
        if (!soc) {
            std::fprintf(stderr, "[bench] SoC create failed for %s\n", s.tag);
            return -1.0;
        }
        if (!(*soc)->load_elf(data)) {
            std::fprintf(stderr, "[bench] ELF load failed for %s\n", s.tag);
            return -1.0;
        }

        // Warm up to steady state (boot + reach main loop). Discard the time.
        auto warm_res = (*soc)->run(warmup);
        if (warm_res != sim::RunResult::Running) {
            std::fprintf(stderr,
                         "[bench] %s did not reach steady state (warmup "
                         "stop=%d); skipping\n",
                         s.tag, static_cast<int>(warm_res));
            return -1.0;
        }

        perf_stats::reset();
        auto t0 = std::chrono::steady_clock::now();
        auto res = (*soc)->run(measure);
        auto t1 = std::chrono::steady_clock::now();

        double sec =
            std::chrono::duration<double>(t1 - t0).count();
        if (sec <= 0.0) {
            continue;
        }
        ips.push_back(static_cast<double>(measure) / sec);

        if (r + 1 == reps) {
            perf_stats::dump();
        }

        if (res != sim::RunResult::Running) {
            std::fprintf(stderr,
                         "[bench] WARN %s stopped early (res=%d) — steady-state "
                         "assumption broken\n",
                         s.tag, static_cast<int>(res));
        }
    }

    if (ips.empty()) {
        return -1.0;
    }
    double med = median(ips);
    double mn = *std::min_element(ips.begin(), ips.end());
    double mx = *std::max_element(ips.begin(), ips.end());

    // Parseable single-line summary. ips == instructions (steps) per second.
    std::printf("[bench] %-18s ips_median=%.0f ips_min=%.0f ips_max=%.0f "
                "reps=%zu\n",
                s.tag, med, mn, mx, ips.size());
    std::fflush(stdout);
    return med;
}

// ── Regression guard (PERF-METHODOLOGY.md §4, soft/advisory gate) ──
//
// Baseline file format (one directive per line; '#' comments):
//   tolerance: 0.10        # regression if now/baseline < 1 - tolerance
//   <tag> <ips_median>     # e.g. gpio_iotoggle-O2 21800000
// The comparison is advisory: it prints PASS/REGRESS and exits 0 so noise
// never flakes a hard CI gate (a real gate requires a pinned toolchain,
// see PERF-METHODOLOGY §4).

struct Baseline {
    double tolerance = 0.10;
    std::vector<std::pair<std::string, double>> entries;
};

Baseline parse_baseline(const char* path) {
    Baseline b;
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "[guard] cannot open baseline %s\n", path);
        return b;
    }
    std::string line;
    while (std::getline(f, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key)) {
            continue;
        }
        if (key == "tolerance:") {
            ss >> b.tolerance;
            continue;
        }
        double v;
        if (ss >> v) {
            b.entries.emplace_back(key, v);
        }
    }
    return b;
}

// Prints the advisory report; returns the count of regressions (caller decides
// exit code — soft gate stays 0).
int compare_baseline(const Baseline& b,
                     const std::vector<std::pair<std::string, double>>& now) {
    if (b.entries.empty()) {
        std::fprintf(stderr, "[guard] baseline empty — nothing to compare\n");
        return 0;
    }
    int regressions = 0;
    std::printf("[guard] tolerance=%.0f%% (regression if now/baseline below)\n",
                b.tolerance * 100.0);
    for (const auto& [tag, now_ips] : now) {
        double base = -1.0;
        for (const auto& [bt, bv] : b.entries) {
            if (bt == tag) {
                base = bv;
                break;
            }
        }
        if (base < 0.0) {
            std::printf("[guard] %-18s baseline=--- (no entry)\n", tag.c_str());
            continue;
        }
        double ratio = now_ips / base;
        bool pass = ratio >= (1.0 - b.tolerance);
        if (!pass) {
            ++regressions;
        }
        std::printf("[guard] %-18s baseline=%.0f now=%.0f ratio=%.1f%% %s\n",
                    tag.c_str(), base, now_ips, ratio * 100.0,
                    pass ? "PASS" : "*** REGRESSION ***");
    }
    std::printf("[guard] %s (%d regression(s))\n",
                regressions == 0 ? "advisory: within tolerance"
                                 : "advisory: REGRESSION(S) DETECTED",
                regressions);
    std::fflush(stdout);
    return regressions;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_sigint);

    // Positional: [warmup] [measure] [reps]. Flag: --baseline <file>.
    size_t warmup = 1'000'000;
    size_t measure = 10'000'000;
    size_t reps = 5;
    const char* baseline_path = nullptr;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--baseline" && i + 1 < argc) {
            baseline_path = argv[++i];
        } else if (!a.empty() && a[0] != '-') {
            size_t v = std::strtoull(a.c_str(), nullptr, 0);
            if (positional == 0) {
                warmup = v;
            } else if (positional == 1) {
                measure = v;
            } else if (positional == 2) {
                reps = v;
            }
            ++positional;
        }
    }

    // Optional scenario subset via env var (semicolon-separated stems).
    const char* subset_env = std::getenv("BENCH_SCENARIOS");
    std::string subset = subset_env ? subset_env : "";

    std::fprintf(stderr,
                 "[bench] warmup=%zu measure=%zu reps=%zu MF_BENCH_FW_DIR=%s%s\n",
                 warmup, measure, reps, MF_BENCH_FW_DIR,
                 baseline_path ? " [guard]" : "");

    std::vector<std::pair<std::string, double>> results;
    for (const auto& s : kScenarios) {
        if (g_interrupted) {
            break;
        }
        if (!subset.empty()) {
            // Match if the env-provided subset token appears within the
            // scenario's full stem or short tag (subset is the substring).
            if (std::string(s.stem).find(subset) == std::string::npos &&
                std::string(s.tag).find(subset) == std::string::npos) {
                continue;
            }
        }
        double med = run_scenario(s, warmup, measure, reps);
        if (med >= 0.0) {
            results.emplace_back(s.tag, med);
        }
    }

    if (baseline_path) {
        Baseline b = parse_baseline(baseline_path);
        // Advisory soft gate: report regressions but never hard-fail (perf is
        // noise-sensitive; a real CI gate needs a pinned toolchain — §4).
        (void)compare_baseline(b, results);
    }
    return 0;
}
