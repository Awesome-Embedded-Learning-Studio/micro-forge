#include "sim/virtual_clock.hpp"

namespace micro_forge::sim {

VirtualClock::VirtualClock(std::span<const DomainConfig> domains)
    : sysclk_freq_hz_((domains.empty() || domains[0].freq_hz == 0)
                          ? 1
                          : domains[0].freq_hz) {
    domains_.reserve(domains.size());
    for (const auto& cfg : domains) {
        domains_.push_back({cfg.freq_hz, 0, 0});
    }
}

void VirtualClock::advance(uint64_t cpu_cycles) {
    // INVARIANT: cpu_cycles is a *per-step* delta (single-instruction cycle
    // count, 1–3), the only input from SimulationCoordinator::step. With the
    // largest STM32F1 clock (72 MHz) and cpu_cycles up to 2^32, every product
    // here stays well under 2^64 (max ≈ 2^32 * 1e9 = 4.29e18 < 1.8e19), so
    // uint64_t is bit-identical to the __uint128_t it replaces — without the
    // __udivti3/__umodti3 soft-divide libcalls (x86-64 has no 128-bit divide),
    // which made advance() the #1 measured hotspot (33% of runtime).
    uint64_t ns_product = cpu_cycles * 1'000'000'000ULL + total_ns_residual_;
    total_ns_ += ns_product / sysclk_freq_hz_;
    total_ns_residual_ = ns_product % sysclk_freq_hz_;

    for (auto& d : domains_) {
        if (d.freq_hz == 0) {
            continue;
        }

        // ticks = cpu_cycles * domain_freq / sysclk_freq
        uint64_t product = cpu_cycles * d.freq_hz;
        uint64_t ticks = product / sysclk_freq_hz_;
        uint64_t rem = product % sysclk_freq_hz_;

        d.residual += rem;
        if (d.residual >= sysclk_freq_hz_) {
            ++ticks;
            d.residual -= sysclk_freq_hz_;
        }

        d.elapsed_ticks += ticks;
    }
}

uint64_t VirtualClock::consume_ticks(size_t domain_index) {
    if (domain_index >= domains_.size()) {
        return 0;
    }
    auto& d = domains_[domain_index];
    uint64_t result = d.elapsed_ticks;
    d.elapsed_ticks = 0;
    return result;
}

void VirtualClock::set_domain_freq(size_t domain_index, uint32_t freq_hz) {
    if (domain_index >= domains_.size()) {
        return;
    }
    domains_[domain_index].freq_hz = freq_hz;
}

uint32_t VirtualClock::domain_freq_hz(size_t domain_index) const {
    if (domain_index >= domains_.size()) {
        return 0;
    }
    return domains_[domain_index].freq_hz;
}

} // namespace micro_forge::sim
