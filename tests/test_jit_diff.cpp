// NOLINTBEGIN -- test scaffolding: fork/setenv/env capture are deliberate
/// @file test_jit_diff.cpp
/// @brief Native JIT/interpreter differential tests with process isolation.

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

namespace {

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
    const char* name;
    std::vector<uint8_t> code;
    uint64_t initial;
    uint64_t expected;
    uint64_t flags = kFlags;
    bool store = false;
    uint64_t memory_initial = kLoaded;
    uint64_t source = kStored;
    std::optional<uint64_t> memory_expected = std::nullopt;
    uint64_t base = kData + 0x48;
};

struct State {
    uint64_t rip;
    std::array<uint64_t, 16> gprs;
    std::array<bool, 6> flags;
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
        cpu.set_gpr(Reg::kR9, test.initial);
        cpu.set_gpr(Reg::kR10, test.source);
        cpu.set_gpr(Reg::kR12, test.base);
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
    std::copy_n(ram.data() + kData - 8, out.memory.size(), out.memory.begin());
    out.blocks = cpu.jit_block_count();
    out.insns = cpu.jit_insn_count();
    out.exit = cpu.exit_status().kind;
    EXPECT_EQ(test.expected, cpu.gpr(Reg::kR9));
    EXPECT_EQ(test.flags, cpu.rflags() & kFlags);
    EXPECT_EQ(test.memory_expected.value_or(test.store ? kStored : kLoaded),
              bus.read_u64(kData).value());
}

void run_child(const DiffCase& test, bool jit, State& out) {
    // Set these BEFORE fork. The parent never executes Cpu::step(), so
    // each child initializes maybe_compile_block's static independently.
    ScopedEnv enabled("MICRO_FORGE_JIT", jit ? "1" : nullptr);
    ScopedEnv add("MICRO_FORGE_JIT_ADD", jit ? "1" : nullptr);
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

class JitDiffTest : public ::testing::TestWithParam<DiffCase> {};

TEST_P(JitDiffTest, MatchesInterpreter) {
    const auto& test = GetParam();
    State interpreter{}, jit{};
    ASSERT_NO_FATAL_FAILURE(run_child(test, false, interpreter));
    ASSERT_NO_FATAL_FAILURE(run_child(test, true, jit));
    EXPECT_EQ(0U, interpreter.blocks);
    EXPECT_EQ(0U, interpreter.insns);
    ASSERT_GT(jit.blocks, 0U);
    ASSERT_GT(jit.insns, 0U);
    // Only the instruction under test can compile; no padding mov can
    // satisfy these counters while the target falls back to interpreting.
    EXPECT_EQ(1U, jit.blocks);
    EXPECT_EQ(1U, jit.insns);
    EXPECT_EQ(interpreter.exit, jit.exit);
    EXPECT_EQ(interpreter.rip, jit.rip);
    for (unsigned i = 0; i < jit.gprs.size(); ++i) {
        EXPECT_EQ(interpreter.gprs[i], jit.gprs[i]) << "GPR " << i;
    }
    constexpr std::array flag_names = {"CF", "OF", "SF", "ZF", "AF", "PF"};
    for (unsigned i = 0; i < jit.flags.size(); ++i) {
        EXPECT_EQ(interpreter.flags[i], jit.flags[i]) << flag_names[i];
    }
    EXPECT_EQ(interpreter.memory, jit.memory);
}

INSTANTIATE_TEST_SUITE_P(
    Native, JitDiffTest,
    ::testing::Values(
        DiffCase{"MovImm32", {0x49, 0xC7, 0xC1, 0x78, 0x56, 0x34, 0x12}, 0, 0x12345678},
        DiffCase{"MovNegativeImm32", {0x49, 0xC7, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF}, 0, UINT64_MAX},
        DiffCase{"AddPositive", {0x49, 0x81, 0xC1, 0x01, 0x01, 0x00, 0x00}, 0xF, 0x110, 0x10},
        DiffCase{
            "AddNegative", {0x49, 0x81, 0xC1, 0x00, 0xFF, 0xFF, 0xFF}, 5, UINT64_MAX - 250, 0x84},
        DiffCase{"AddCarry", {0x49, 0x81, 0xC1, 1, 0, 0, 0}, UINT64_MAX, 0, 0x55},
        DiffCase{"AddOverflow",
                 {0x49, 0x81, 0xC1, 1, 0, 0, 0},
                 0x7FFFFFFFFFFFFFFFULL,
                 0x8000000000000000ULL,
                 0x894},
        DiffCase{"AddNegativeOverflow",
                 {0x49, 0x81, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF},
                 0x8000000000000000ULL,
                 0x7FFFFFFFFFFFFFFFULL,
                 0x805},
        DiffCase{"MovRegReg", {0x4D, 0x89, 0xD1}, 0, kStored}, // mov r9,r10
        DiffCase{"LeaBaseDisp", {0x4D, 0x8D, 0x8C, 0x24, 0xB8, 0xFF, 0xFF, 0xFF}, 0, kData},
        DiffCase{"LoadBaseDisp", {0x4D, 0x8B, 0x8C, 0x24, 0xB8, 0xFF, 0xFF, 0xFF}, 0, kLoaded},
        DiffCase{"LoadRipDisp", {0x4C, 0x8B, 0x0D, 0xF9, 0x2F, 0, 0}, 0, kLoaded},
        DiffCase{
            "StoreBaseDisp", {0x4D, 0x89, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF}, 0, 0, kFlags, true},
        // add [r12-0x48],r10; negative disp32 and all six ADD flags.
        DiffCase{"AddMemCarry",
                 {0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF},
                 0,
                 0,
                 0x55,
                 false,
                 UINT64_MAX,
                 1,
                 0},
        DiffCase{"AddMemOverflow",
                 {0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF},
                 0,
                 0,
                 0x894,
                 false,
                 0x7FFFFFFFFFFFFFFFULL,
                 1,
                 0x8000000000000000ULL},
        DiffCase{"AddMemNegativeOverflow",
                 {0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF},
                 0,
                 0,
                 0x805,
                 false,
                 0x8000000000000000ULL,
                 UINT64_MAX,
                 0x7FFFFFFFFFFFFFFFULL},
        DiffCase{"AddMemAuxCarry",
                 {0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF},
                 0,
                 0,
                 0x10,
                 false,
                 15,
                 1,
                 16},
        DiffCase{"AddMemNoAuxCarry",
                 {0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF},
                 0,
                 0,
                 0,
                 false,
                 7,
                 1,
                 8},
        // Zero displacement/SIB base and high 32 bits of the source.
        DiffCase{"AddMemNoDisp",
                 {0x4D, 0x01, 0x14, 0x24},
                 0,
                 0,
                 4,
                 false,
                 2,
                 0x100000001ULL,
                 0x100000003ULL,
                 kData},
        DiffCase{"AddMemPositiveDisp8",
                 {0x4D, 0x01, 0x54, 0x24, 0x48},
                 0,
                 0,
                 0x44,
                 false,
                 0,
                 0,
                 0,
                 kData - 0x48},
        // Source and base may be the same guest register.
        DiffCase{"AddMemAliasedBase",
                 {0x4D, 0x01, 0x64, 0x24, 0xB8},
                 0,
                 0,
                 4,
                 false,
                 0,
                 kStored,
                 kData + 0x48}),
    [](const ::testing::TestParamInfo<DiffCase>& info) { return info.param.name; });

TEST(JitLayoutTest, RejectsBadRelocationsAndPatches) {
    const auto decoded = micro_forge::arch::x86_64::decode_one(
        std::array<uint8_t, 4>{0x48, 0x8B, 0x48, 8}); // mov rcx,[rax+8]
    ASSERT_TRUE(decoded);
    const auto good = translate_insn(*decoded);
    ASSERT_TRUE(good);
    ASSERT_TRUE(good->has_valid_layout());

    auto bad = *good;
    bad.call_offset = *good->call_offset + 1; // rel32 is not E8
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.cpu_offset = *good->cpu_offset - 1; // immediate must follow 48 BF
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.cpu_offset = 1; // guard subtraction before reading opcode bytes
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.call_offset = std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.cpu_offset.reset();
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches[0].offset = 4; // SIB byte, not the displacement
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches[0].offset = std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches[0].size = 0;
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches.push_back(good->patches.front());
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *good;
    bad.patches[0].offset = *good->cpu_offset; // overlaps Cpu* relocation
    EXPECT_FALSE(bad.has_valid_layout());
}

TEST(JitLayoutTest, RejectsOriginalLeaAndStoreOffsets) {
    const auto lea =
        micro_forge::arch::x86_64::decode_one(std::array<uint8_t, 4>{0x48, 0x8D, 0x48, 8});
    const auto store =
        micro_forge::arch::x86_64::decode_one(std::array<uint8_t, 4>{0x48, 0x89, 0x48, 8});
    ASSERT_TRUE(lea);
    ASSERT_TRUE(store);
    auto bad_lea = translate_insn(*lea);
    auto bad_store = translate_insn(*store);
    ASSERT_TRUE(bad_lea);
    ASSERT_TRUE(bad_store);
    ASSERT_TRUE(bad_lea->has_valid_layout());
    ASSERT_TRUE(bad_store->has_valid_layout());
    bad_lea->patches.back().offset = 16;
    EXPECT_FALSE(bad_lea->has_valid_layout());
    bad_store->call_offset = 36;
    EXPECT_FALSE(bad_store->has_valid_layout());
    bad_store->call_offset = 39;
    bad_store->patches.back().offset = 32;
    EXPECT_FALSE(bad_store->has_valid_layout());
}

TEST(JitLayoutTest, UnsupportedMemoryFormDoesNotBecomeImmediateMov) {
    const auto indexed = micro_forge::arch::x86_64::decode_one(
        std::array<uint8_t, 4>{0x48, 0x8B, 0x04, 0xC3}); // mov rax,[rbx+rax*8]
    ASSERT_TRUE(indexed);
    EXPECT_FALSE(translate_insn(*indexed));
}

TEST(JitLayoutTest, AddMemHasValidFortyEightByteLayout) {
    const auto decoded = micro_forge::arch::x86_64::decode_one(
        std::array<uint8_t, 8>{0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF});
    ASSERT_TRUE(decoded);
    const auto lowered = translate_insn(*decoded);
    ASSERT_TRUE(lowered);
    ASSERT_TRUE(lowered->has_valid_layout());
    EXPECT_EQ(micro_forge::base::JitHelper::kAddMem, lowered->helper);
    ASSERT_EQ(48U, lowered->code.size());
    ASSERT_EQ(26U, lowered->cpu_offset);
    ASSERT_EQ(36U, lowered->call_offset);
    EXPECT_EQ(0x48, lowered->code[24]);
    EXPECT_EQ(0xBF, lowered->code[25]);
    EXPECT_EQ(0x49, lowered->code[34]);
    EXPECT_EQ(0xBB, lowered->code[35]);
    EXPECT_EQ(0x41, lowered->code[44]);
    EXPECT_EQ(0xFF, lowered->code[45]);
    EXPECT_EQ(0xD3, lowered->code[46]);
    EXPECT_EQ(0x5F, lowered->code[47]);
    ASSERT_EQ(3U, lowered->patches.size());
    constexpr std::array<std::size_t, 3> offsets = {5, 13, 20};
    constexpr std::array<uint64_t, 3> values = {80, 96, 0xFFFFFFB8};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        EXPECT_EQ(offsets[i], lowered->patches[i].offset);
        EXPECT_EQ(4U, lowered->patches[i].size);
        EXPECT_EQ(values[i], lowered->patches[i].value);
    }
    // All five holes are disjoint, in bounds and zero before relocation.
    constexpr std::array<std::pair<std::size_t, std::size_t>, 5> holes = {
        {{5, 4}, {13, 4}, {20, 4}, {26, 8}, {36, 8}}};
    std::array<bool, 48> occupied{};
    for (const auto& [offset, size] : holes) {
        ASSERT_LE(offset + size, lowered->code.size());
        for (auto i = offset; i < offset + size; ++i) {
            EXPECT_EQ(0, lowered->code[i]);
            EXPECT_FALSE(occupied[i]);
            occupied[i] = true;
        }
    }
    auto bad = *lowered;
    bad.patches[2].offset = 21; // overlaps the mov rdi opcode at 24
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *lowered;
    bad.patches[1].offset = 26; // overlaps the Cpu* relocation
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *lowered;
    // Original failed attempt listed cpu64 as both a data patch and a
    // relocation. Even a zero-valued patch must not claim that hole twice.
    bad.patches.push_back({.offset = 26, .size = 8, .value = 0});
    EXPECT_FALSE(bad.has_valid_layout());
    bad = *lowered;
    bad.call_offset = 34; // movabs opcode start, not its imm64 hole
    EXPECT_FALSE(bad.has_valid_layout());
}

TEST(JitLayoutTest, AddMemRejectsUnsupportedForms) {
    const std::vector<std::vector<uint8_t>> unsupported = {
        {0x01, 0x08},                         // add dword [rax],ecx
        {0x66, 0x01, 0x08},                   // add word [rax],cx
        {0x00, 0x08},                         // add byte [rax],cl
        {0x48, 0x01, 0x0C, 0xD8},             // add [rax+rbx*8],rcx
        {0x48, 0x01, 0x0D, 0, 0, 0, 0},       // add [rip],rcx
        {0x48, 0x01, 0x0C, 0x25, 0, 0, 0, 0}, // add [abs32],rcx
        {0x64, 0x48, 0x01, 0x08},             // FS base is not in the ABI
        {0x65, 0x48, 0x01, 0x08},             // GS base is not in the ABI
        {0xF0, 0x48, 0x01, 0x08},             // lock add
    };
    for (const auto& bytes : unsupported) {
        const auto decoded = micro_forge::arch::x86_64::decode_one(bytes);
        ASSERT_TRUE(decoded);
        EXPECT_FALSE(translate_insn(*decoded));
    }
    const auto decoded =
        micro_forge::arch::x86_64::decode_one(std::array<uint8_t, 3>{0x48, 0x01, 0x08});
    ASSERT_TRUE(decoded);
    auto insn = *decoded;
    insn.ops[0].base = Reg::kXmm0;
    EXPECT_FALSE(translate_insn(insn));
    insn = *decoded;
    insn.ops[1].reg = Reg::kCr0;
    EXPECT_FALSE(translate_insn(insn));
    insn = *decoded;
    insn.addr_size = 4;
    EXPECT_FALSE(translate_insn(insn));
}

} // namespace

// NOLINTEND
