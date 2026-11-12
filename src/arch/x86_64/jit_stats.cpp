/// @file jit_stats.cpp
/// @brief Coverage diagnostics: how often each failed rip falls back.
///
/// Enabled only when MICRO_FORGE_JIT_FAILSTATS is set; the counters are
/// process-wide and single-threaded like the rest of the core.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <print>
#include <span>
#include <unordered_map>
#include <vector>

#include <micro_forge/base/jit_templates_generated.hpp>

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

bool template_stats_enabled() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CPU
    static const bool on = std::getenv("MICRO_FORGE_JIT_TPLSTATS") != nullptr;
    return on;
}

using TemplateRegistry = std::span<const micro_forge::base::jit_gen::TemplateEntry>;

TemplateRegistry registry() {
    return {std::span(micro_forge::base::jit_gen::kTemplateRegistry)};
}

std::vector<std::uint64_t>& template_hits() {
    static std::vector<std::uint64_t> counts(registry().size(), 0);
    return counts;
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

/// @brief Counts one successful translation that picked the template at @p
///        data (no-op unless MICRO_FORGE_JIT_TPLSTATS is set).
extern "C" void mf_jit_note_template(const std::uint8_t* data) {
    if (!template_stats_enabled()) {
        return;
    }
    const auto entries = registry();
    auto& counts = template_hits();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].data == data) {
            ++counts[i];
            return;
        }
    }
    // Not a registry member (e.g. a hand-built span): ignore quietly.
}

/// @brief Prints per-template translation hits; zero-hit templates are the
///        coverage blind spots no differential case reaches.
extern "C" void mf_jit_dump_template_hits() {
    if (!template_stats_enabled()) {
        return;
    }
    const auto entries = registry();
    const auto& counts = template_hits();
    std::vector<std::size_t> order(entries.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) {
        return counts[a] != counts[b] ? counts[a] > counts[b] : entries[a].name < entries[b].name;
    });
    std::size_t used = 0;
    std::uint64_t total = 0;
    for (const auto count : counts) {
        used += count > 0 ? 1 : 0;
        total += count;
    }
    std::println(stderr, "jtpl total={} used={}/{} zero-hit={}", total, used, entries.size(),
                 entries.size() - used);
    for (const auto i : order) {
        std::println(stderr, "  hits={} tpl={}", counts[i], entries[i].name);
    }
}

} // namespace micro_forge::arch::x86_64
