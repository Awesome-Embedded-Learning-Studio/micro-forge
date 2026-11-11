// NOLINTBEGIN -- test scaffolding: fork/setenv/env capture are deliberate
/// @file jit_diff_support.hpp
/// @brief Shared process isolation and complete architectural state comparisons.

#pragma once

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/arch/x86_64/decoder.hpp>
#include <micro_forge/base/jit_translate.hpp>
#include <micro_forge/kernel/ram_region.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace jit_diff {

using micro_forge::arch::x86_64::Cpu;
using micro_forge::arch::x86_64::ExitStatus;
using micro_forge::arch::x86_64::Reg;
using micro_forge::base::translate_insn;

constexpr uint64_t kCode = 0x1000;
constexpr uint64_t kData = 0x4000;
constexpr uint64_t kFlags = 0x8D5; // CF/PF/AF/ZF/SF/OF, initially all set
constexpr uint64_t kLoaded = 0xFEDCBA9876543210ULL;
constexpr uint64_t kStored = 0x8877665544332211ULL;

class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const auto* old = std::getenv(name)) {
            old_ = old;
        }
        const int result = value ? setenv(name, value, 1) : unsetenv(name);
        EXPECT_EQ(0, result);
    }
    ~ScopedEnv() {
        if (old_) {
            (void)setenv(name_, old_->c_str(), 1);
        } else {
            (void)unsetenv(name_);
        }
    }

private:
    const char* name_;
    std::optional<std::string> old_;
};

struct DiffCase {
    std::string name;
    std::vector<uint8_t> code;
    uint64_t initial;
    uint64_t expected;
    uint64_t flags = kFlags;
    bool store = false;
    uint64_t memory_initial = kLoaded;
    uint64_t source = kStored;
    std::optional<uint64_t> memory_expected = std::nullopt;
    uint64_t base = kData + 0x48;
    Reg result_reg = Reg::kR9;
    Reg base_reg = Reg::kR12;
    Reg source_reg = Reg::kR10;
    std::optional<uint64_t> index = std::nullopt;
    Reg index_reg = Reg::kRcx;
    unsigned jit_insns = 1;
};

struct State {
    uint64_t rip;
    std::array<uint64_t, 16> gprs;
    std::array<bool, 6> flags;
    uint64_t rflags;
    std::array<uint8_t, 24> memory; // include both neighbours of the qword
    std::size_t blocks;
    uint64_t insns;
    ExitStatus::Kind exit;
};

void execute_case(const DiffCase& test, State& out) {
    micro_forge::kernel::Bus bus;
    micro_forge::kernel::RamRegion ram(0x10000);
    micro_forge::kernel::Scheduler scheduler;
    ASSERT_TRUE(bus.map(0, ram.size(), ram).has_value());
    Cpu cpu(bus, scheduler);
    cpu.reset(kCode, 0xF000);
    cpu.set_code_range(kCode, kCode + test.code.size());
    std::ranges::copy(test.code, ram.data() + kCode);
    ram.data()[kCode + test.code.size()] = 0xF4; // hlt is interpreter-only

    const auto seed_state = [&] {
        for (unsigned i = 0; i < 16; ++i) {
            cpu.set_gpr(static_cast<Reg>(i), 0x1020304050607080ULL + i * 0x101U);
        }
        cpu.set_gpr(Reg::kRsp, 0xF000);
        cpu.set_gpr(test.result_reg, test.initial);
        cpu.set_gpr(test.source_reg, test.source);
        cpu.set_gpr(test.base_reg, test.base);
        if (test.index) {
            cpu.set_gpr(test.index_reg, *test.index);
        }
        cpu.set_rip(kCode);
        cpu.set_rflags(kFlags | 2U);
        std::fill_n(ram.data() + kData - 8, 24, 0xA5);
        ASSERT_TRUE(bus.write_u64(kData, test.memory_initial).has_value());
    };

    // First visit compiles but executes in the interpreter. Revisit that
    // same RIP with identical architectural inputs to execute the block.
    // Restore memory too: otherwise the warm-up store hides a no-op JIT.
    seed_state();
    ASSERT_EQ(ExitStatus::Kind::kRunning, cpu.step().kind);
    ASSERT_EQ(0U, cpu.jit_insn_count());
    seed_state();
    ASSERT_EQ(ExitStatus::Kind::kHalt, cpu.run(16).kind);

    out.rip = cpu.rip();
    for (unsigned i = 0; i < out.gprs.size(); ++i) {
        out.gprs[i] = cpu.gpr(static_cast<Reg>(i));
    }
    out.flags = {cpu.cf(), cpu.of(), cpu.sf(), cpu.zf(), cpu.af(), cpu.pf()};
    out.rflags = cpu.rflags();
    std::copy_n(ram.data() + kData - 8, out.memory.size(), out.memory.begin());
    out.blocks = cpu.jit_block_count();
    out.insns = cpu.jit_insn_count();
    out.exit = cpu.exit_status().kind;
    EXPECT_EQ(test.expected, cpu.gpr(test.result_reg));
    EXPECT_EQ(test.flags, cpu.rflags() & kFlags);
    EXPECT_EQ(test.memory_expected.value_or(test.store ? kStored : kLoaded),
              bus.read_u64(kData).value());
}

void run_child(const DiffCase& test, bool jit, State& out) {
    // Set these BEFORE fork. The parent never executes Cpu::step(), so
    // each child initializes maybe_compile_block's static independently.
    ScopedEnv enabled("MICRO_FORGE_JIT", jit ? "1" : nullptr);
    ScopedEnv add("MICRO_FORGE_JIT_ADD", nullptr); // JIT alone must enable add-imm
    ScopedEnv trace("MICRO_FORGE_JIT_TRACE", nullptr);
    int pipefd[2];
    ASSERT_EQ(0, pipe(pipefd));
    const pid_t pid = fork();
    if (pid == -1) {
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        FAIL() << "fork failed: " << errno;
    }
    if (pid == 0) {
        (void)close(pipefd[0]);
        (void)alarm(10); // a corrupt template must not hang the test suite
        State state{};
        execute_case(test, state);
        if (::testing::Test::HasFailure()) {
            _exit(1);
        }
        const auto written = write(pipefd[1], &state, sizeof(state));
        _exit(written == static_cast<ssize_t>(sizeof(state)) ? 0 : 2);
    }
    (void)close(pipefd[1]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);
    ssize_t received;
    do {
        received = read(pipefd[0], &out, sizeof(out));
    } while (received == -1 && errno == EINTR);
    (void)close(pipefd[0]);
    ASSERT_EQ(pid, waited);
    ASSERT_TRUE(WIFEXITED(status)) << "child status=" << status << " jit=" << jit;
    ASSERT_EQ(0, WEXITSTATUS(status)) << "jit=" << jit;
    ASSERT_EQ(static_cast<ssize_t>(sizeof(out)), received);
}

} // namespace jit_diff

// NOLINTEND
