/// @file jit_stats.cpp
/// @brief Coverage diagnostics: how often each failed rip falls back.
///
/// Enabled only when MICRO_FORGE_JIT_FAILSTATS is set; the counters are
/// process-wide and single-threaded like the rest of the core.
#include <algorithm>
#include <cstdlib>
#include <print>
#include <unordered_map>
#include <vector>

namespace {

bool stats_enabled() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CPU
    static const bool on = std::getenv("MICRO_FORGE_JIT_FAILSTATS") != nullptr;
    return on;
}

std::unordered_map<std::uint64_t, std::uint64_t>& hits() {
    static std::unordered_map<std::uint64_t, std::uint64_t> table;
    return table;
}

} // namespace

namespace micro_forge::arch::x86_64 {

/// @brief Counts one interpreter fallback at @p rip (no-op unless enabled).
extern "C" void mf_jit_note_fail_hit(std::uint64_t rip) {
    if (stats_enabled()) {
        ++hits()[rip];
    }
}

/// @brief Prints the hottest fallback rips, most frequent first.
extern "C" void mf_jit_dump_fail_hits() {
    if (!stats_enabled()) {
        return;
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> sorted(hits().begin(), hits().end());
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.second > b.second; });
    std::uint64_t total = 0;
    for (const auto& [rip, count] : sorted) {
        static_cast<void>(rip);
        total += count;
    }
    std::println(stderr, "jfail-hits total={} distinct={}", total, sorted.size());
    const std::size_t show = std::min<std::size_t>(sorted.size(), 20);
    for (std::size_t i = 0; i < show; ++i) {
        std::println(stderr, "  hits={} rip=0x{:x}", sorted[i].second, sorted[i].first);
    }
}

} // namespace micro_forge::arch::x86_64
