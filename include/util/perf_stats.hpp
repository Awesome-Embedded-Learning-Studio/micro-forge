// perf_stats.hpp — compile-gated per-subsystem counters for the perf campaign.
//
// PERF-METHODOLOGY.md §3: a zero-overhead (when off) instrumentation layer for
// the measurement infra. When MF_PERF_STATS is undefined (the default for every
// build except the bench), every macro expands to nothing and `reset()`/`dump()`
// are empty inline stubs — the compiler elides them entirely. When defined, the
// counters record a per-instruction cost breakdown (bus accesses, fetches,
// find_region scan length, 16/32-bit mix) that callgrind can't easily surface.
#pragma once

#include <cstdint>
#include <cstdio>

namespace micro_forge::perf_stats {

#ifdef MF_PERF_STATS

// Aggregate counters; one instance per thread (the bench is single-threaded).
struct Counters {
    std::uint64_t bus_reads = 0;        // Bus::read() calls
    std::uint64_t bus_writes = 0;       // Bus::write() calls
    std::uint64_t fetches = 0;          // fetch16() calls (1 per 16-bit halfword)
    std::uint64_t find_region_calls = 0;
    std::uint64_t find_region_iters = 0; // sum of linear-scan iterations
    std::uint64_t instr_16bit = 0;
    std::uint64_t instr_32bit = 0;
};

inline Counters& counters() {
    static thread_local Counters c;
    return c;
}

inline void reset() {
    counters() = Counters{};
}

// Prints a human-readable breakdown to stderr (bench-only path; gated, so the
// bare stdio does not leak into the production build).
inline void dump() {
    const auto& c = counters();
    const std::uint64_t instrs = c.instr_16bit + c.instr_32bit;
    std::fprintf(stderr,
                 "[perf-stats] bus_reads=%llu bus_writes=%llu fetches=%llu\n"
                 "[perf-stats] find_region: calls=%llu iters=%llu "
                 "(avg/call=%.2f)\n"
                 "[perf-stats] instr: 16bit=%llu 32bit=%llu (total=%llu)\n",
                 static_cast<unsigned long long>(c.bus_reads),
                 static_cast<unsigned long long>(c.bus_writes),
                 static_cast<unsigned long long>(c.fetches),
                 static_cast<unsigned long long>(c.find_region_calls),
                 static_cast<unsigned long long>(c.find_region_iters),
                 c.find_region_calls
                     ? static_cast<double>(c.find_region_iters) /
                           static_cast<double>(c.find_region_calls)
                     : 0.0,
                 static_cast<unsigned long long>(c.instr_16bit),
                 static_cast<unsigned long long>(c.instr_32bit),
                 static_cast<unsigned long long>(instrs));
    if (instrs) {
        std::fprintf(stderr,
                     "[perf-stats] per-instr: bus_reads=%.2f fetches=%.2f\n",
                     static_cast<double>(c.bus_reads) /
                         static_cast<double>(instrs),
                     static_cast<double>(c.fetches) /
                         static_cast<double>(instrs));
    }
}

inline void record_find_region(std::uint64_t iters) {
    auto& c = counters();
    ++c.find_region_calls;
    c.find_region_iters += iters;
}

#define MF_PERF_INC(field) (++micro_forge::perf_stats::counters().field)
#define MF_PERF_FIND_REGION(iters)                                          \
    (micro_forge::perf_stats::record_find_region(iters))

#else // !MF_PERF_STATS — zero cost.

#define MF_PERF_INC(field) ((void)0)
// The void-cast on the off-branch references the loop counter so the compiler
// still treats it as used (avoids -Wunused-but-set-variable under -Werror);
// the read is elided, so the cost is zero.
#define MF_PERF_FIND_REGION(iters) ((void)(iters))

inline void reset() {}
inline void dump() {}

#endif // MF_PERF_STATS

} // namespace micro_forge::perf_stats
