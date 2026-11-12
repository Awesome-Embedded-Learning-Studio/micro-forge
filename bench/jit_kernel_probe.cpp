/// @file jit_kernel_probe.cpp
/// @brief Native JIT coverage, Cpu::run throughput and flat-image profile.

#include <cstring>
#include <micro_forge/arch/x86_64/decoder.hpp>
/// @brief Coverage diagnostics exported from the core (env gated).
extern "C" void mf_jit_dump_fail_hits();
#include <micro_forge/base/jit_translate.hpp>
#include <micro_forge/loader/cinux_loader.hpp>
#include <micro_forge/machine.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <print>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(input)), {}};
}

// A flat-image scan includes data; compare with an extracted ELF .text.
void profile(const std::vector<uint8_t>& image) {
    using namespace micro_forge::arch::x86_64;
    uint64_t decoded = 0;
    uint64_t failures = 0;
    uint64_t lowered = 0;
    uint64_t zeros = 0;
    std::array<uint64_t, 9> widths{};
    std::array<uint64_t, 9> base_disp{};
    for (std::size_t pc = 0; pc < image.size();) {
        const auto insn = decode_one(std::span<const uint8_t>(image).subspan(pc));
        if (!insn) {
            ++failures;
            ++pc;
            continue;
        }
        ++decoded;
        if (micro_forge::base::translate_insn(*insn)) {
            ++lowered;
        }
        const auto& memory = insn->ops[0];
        if (insn->mnemonic == Mnemonic::kAdd && memory.kind == Operand::Kind::kMem &&
            insn->ops[1].kind == Operand::Kind::kReg && memory.size < widths.size()) {
            ++widths[memory.size];
            if (memory.has_base && !memory.has_index && !memory.rip_relative) {
                ++base_disp[memory.size];
            }
            if (insn->length == 2 && image[pc] == 0 && image[pc + 1] == 0) {
                ++zeros;
            }
        }
        pc += insn->length;
    }
    std::println("decoded={} failures={} lowered={} zero_zero_adds={}", decoded, failures, lowered,
                 zeros);
    for (const auto width : {1U, 2U, 4U, 8U}) {
        std::println("width={} add_mem_reg={} base_disp={}", width, widths[width],
                     base_disp[width]);
    }
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape) -- bench tool, aborts on OOM
int main(int argc, const char* const* argv) {
    const bool scan = argc == 3 && std::string_view(argv[1]) == "--profile";
    if (!scan && argc != 3 && argc != 4) {
        std::println(stderr, "usage: mf-jit-kernel-probe kernel.bin console.bin [ext2.img]\n"
                             "       mf-jit-kernel-probe --profile image.bin");
        return 2;
    }
    const auto image = read_file(argv[scan ? 2 : 1]);
    if (image.empty()) {
        std::println(stderr, "cannot read kernel image");
        return 2;
    }
    if (scan) {
        profile(image);
        return 0;
    }
    micro_forge::Machine machine(0x10000000);
    if (argc == 4) {
        auto disk = read_file(argv[3]);
        if (disk.empty()) {
            std::println(stderr, "cannot read disk image");
            return 2;
        }
        machine.attach_disk(std::move(disk));
    }
    auto& cpu = machine.cpu();
    // MICRO_FORGE_TRACE_RIP=1: sample interpreter-path RIPs (the residual
    // stream the JIT still hands back) and dump a histogram at exit.
    std::unordered_map<uint64_t, uint64_t> rip_hist;
    const bool trace_rips =
        std::getenv("MICRO_FORGE_TRACE_RIP") != nullptr; // NOLINT(concurrency-mt-unsafe)
    // MICRO_FORGE_DUMP_STATE=1: print all GPRs at the budget cutoff so a JIT
    // run and an interpreter run can be diffed register by register.
    if (std::getenv("MICRO_FORGE_DUMP_STATE") != nullptr) { // NOLINT(concurrency-mt-unsafe)
        for (unsigned r = 0; r < 16; ++r) {
            std::println(stderr, "gpr{:2}={:#018x} rip={:#x} rf={:#x}", r,
                         cpu.gpr(static_cast<micro_forge::arch::x86_64::Reg>(r)), cpu.rip(),
                         cpu.rflags());
        }
    }
    if (trace_rips) {
        cpu.set_trace([&rip_hist](uint64_t rip) { ++rip_hist[rip]; });
    }
    // MICRO_FORGE_TRACE_WIN=lo:hi: dump every step RIP whose icount lands in
    // [lo,hi) — under JIT each sample is a block head — to locate divergence.
    const char* win_env = std::getenv("MICRO_FORGE_TRACE_WIN"); // NOLINT(concurrency-mt-unsafe)
    std::vector<std::pair<uint64_t, uint64_t>> win_rips;        // icount, rip
    if (win_env != nullptr) {
        const char* colon = std::strchr(win_env, ':');
        const uint64_t lo = std::strtoull(win_env, nullptr, 0);
        const uint64_t hi = colon != nullptr ? std::strtoull(colon + 1, nullptr, 0) : lo + 10'000;
        cpu.set_trace([&cpu, &win_rips, lo, hi](uint64_t rip) {
            const uint64_t now = cpu.icount();
            if (now >= lo && now < hi) {
                win_rips.emplace_back(now, rip);
            }
        });
    }
    if (!micro_forge::loader::boot_cinux(machine.ram(), cpu, image)) {
        std::println(stderr, "boot_cinux refused the image");
        return 2;
    }
    using Clock = std::chrono::steady_clock;
    double run_seconds = 0;
    const char* budget_env = std::getenv("MICRO_FORGE_RUN_BUDGET"); // NOLINT(concurrency-mt-unsafe)
    const std::vector<uint64_t> budgets =
        budget_env != nullptr ? std::vector<uint64_t>{std::strtoull(budget_env, nullptr, 0)}
                              : std::vector<uint64_t>{100'000'000ULL, 500'000'000ULL};
    for (const uint64_t budget : budgets) {
        const auto begin = Clock::now();
        const auto status = cpu.run(budget);
        run_seconds += std::chrono::duration<double>(Clock::now() - begin).count();
        const double coverage = cpu.icount() == 0
                                    ? 0.0
                                    : 100.0 * static_cast<double>(cpu.jit_insn_count()) /
                                          static_cast<double>(cpu.icount());
        std::println("kind={} rip=0x{:x} icount={} console={} blocks={} failed={} "
                     "jit_insns={} coverage={:.4f}% run_seconds={:.6f} Mips={:.3f}",
                     static_cast<unsigned>(status.kind), cpu.rip(), cpu.icount(),
                     cpu.console().size(), cpu.jit_block_count(), cpu.jit_failed_count(),
                     cpu.jit_insn_count(), coverage, run_seconds,
                     static_cast<double>(cpu.icount()) / run_seconds / 1e6);
        std::fflush(stdout);
        if (status.kind != micro_forge::arch::x86_64::ExitStatus::Kind::kRunning) {
            break;
        }
    }
    std::ofstream output(argv[2], std::ios::binary);
    output.write(cpu.console().data(), static_cast<std::streamsize>(cpu.console().size()));
    if (trace_rips) {
        std::vector<std::pair<uint64_t, uint64_t>> top(rip_hist.begin(), rip_hist.end());
        std::ranges::sort(top, [](const auto& a, const auto& b) { return a.second > b.second; });
        uint64_t total = 0;
        for (const auto& [rip, n] : top) {
            total += n;
        }
        std::println(stderr, "samples={} unique={}", total, top.size());
        for (std::size_t i = 0; i < top.size() && i < 400; ++i) {
            std::println(stderr, "{} rip=0x{:x}", top[i].second, top[i].first);
        }
    }
    if (win_env != nullptr) {
        for (const auto& [ic, rip] : win_rips) {
            std::println(stderr, "win icount={} rip=0x{:x}", ic, rip);
        }
    }
    mf_jit_dump_fail_hits();
    return output.good() &&
                   cpu.exit_status().kind == micro_forge::arch::x86_64::ExitStatus::Kind::kHalt
               ? 0
               : 1;
}
