/// @file jit_kernel_probe.cpp
/// @brief Native JIT coverage, Cpu::run throughput and flat-image profile.

#include <micro_forge/arch/x86_64/decoder.hpp>
#include <micro_forge/base/jit_translate.hpp>
#include <micro_forge/loader/cinux_loader.hpp>
#include <micro_forge/machine.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <print>
#include <string_view>
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
    if (!micro_forge::loader::boot_cinux(machine.ram(), cpu, image)) {
        std::println(stderr, "boot_cinux refused the image");
        return 2;
    }
    using Clock = std::chrono::steady_clock;
    double run_seconds = 0;
    for (const uint64_t budget : {100'000'000ULL, 500'000'000ULL}) {
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
    return output.good() &&
                   cpu.exit_status().kind == micro_forge::arch::x86_64::ExitStatus::Kind::kHalt
               ? 0
               : 1;
}
