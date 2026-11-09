/**
 * @file test_fast_dispatch.cpp
 * @brief Cached execution versus the generic executor, including MMIO ordering.
 */

#include "cpu_fixture.hpp"

#include <micro_forge/kernel/register_region.hpp>

#include <array>
#include <string>

namespace {

struct Program {
    const char* name;
    std::vector<uint8_t> code;
};

struct RunState {
    std::vector<uint64_t> trace;
    std::vector<uint8_t> memory;
    ExitStatus exit;
    uint64_t event_at = 0;
};

RunState run_program(const Program& program, std::size_t cache_capacity) {
    Bus bus;
    RamRegion ram(0x200000);
    Scheduler scheduler;
    Cpu cpu(bus, scheduler);
    EXPECT_TRUE(bus.map_ram(0, ram.size(), ram).has_value());
    cpu.configure_caches(ram.size(), cache_capacity);
    std::ranges::copy(program.code, ram.data() + kCodeBase);
    ram.data()[kCodeBase + program.code.size()] = 0xF4;
    RunState result;
    // First pass populates the cache. The second resets architectural
    // inputs and runs through the cached executor with identical events.
    for (unsigned pass = 0; pass < 2; ++pass) {
        cpu.reset(kCodeBase, kStackTop);
        for (unsigned reg = 0; reg < 16; ++reg) {
            cpu.set_gpr(static_cast<Reg>(reg), 0x8877665544332200ULL + reg);
        }
        cpu.set_gpr(Reg::kRsp, kStackTop);
        cpu.set_gpr(Reg::kRdi, 0x2000);
        cpu.set_gpr(Reg::kRsi, 0x2100);
        cpu.set_gpr(Reg::kRcx, 3);
        cpu.set_rflags(0xAD7); // exercise both preservation and replacement of flags
        EXPECT_TRUE(cpu.write_msr(0xC0000100U, 0x100));
        EXPECT_TRUE(cpu.write_msr(0xC0000101U, 0x100));
        for (unsigned i = 0; i < 0x400; ++i) {
            ram.data()[0x2000 + i] = static_cast<uint8_t>(i ^ 0xA5U);
        }
        if (pass == 1) {
            scheduler.schedule(3, [&] { result.event_at = cpu.icount(); });
            cpu.set_trace([&](uint64_t rip) {
                result.trace.push_back(rip);
                result.trace.push_back(cpu.rip());
                result.trace.push_back(cpu.rflags());
                result.trace.push_back(cpu.icount());
                for (unsigned reg = 0; reg < 16; ++reg) {
                    result.trace.push_back(cpu.gpr(static_cast<Reg>(reg)));
                }
            });
        }
        result.exit = cpu.run(200);
        EXPECT_EQ(ExitStatus::Kind::kHalt, result.exit.kind);
    }
    if (cache_capacity != 0) {
        EXPECT_GT(cpu.decode_cache_stats().hits, 0U);
    }
    result.memory.assign(ram.data() + 0x2000, ram.data() + 0x2400);
    return result;
}

TEST(FastDispatchTest, CachedAndGenericExecutorsMatchEveryRetirement) {
    const std::vector<Program> programs = {
        {
            .name = "Mov64",
            .code =
                {
                    0x48,
                    0x89,
                    0xD0,
                },
        },
        {
            .name = "Mov32",
            .code =
                {
                    0x89,
                    0xD0,
                },
        },
        {
            .name = "Imm64",
            .code =
                {
                    0x48,
                    0xC7,
                    0xC0,
                    0xFF,
                    0xFF,
                    0xFF,
                    0xFF,
                },
        },
        {
            .name = "Imm32",
            .code =
                {
                    0xB8,
                    0xFF,
                    0xFF,
                    0xFF,
                    0xFF,
                },
        },
        {
            .name = "Load64",
            .code =
                {
                    0x48,
                    0x8B,
                    0x07,
                },
        },
        {
            .name = "Load32",
            .code =
                {
                    0x8B,
                    0x07,
                },
        },
        {
            .name = "Load16",
            .code =
                {
                    0x48,
                    0x0F,
                    0xB7,
                    0x07,
                },
        },
        {
            .name = "Load8",
            .code =
                {
                    0x0F,
                    0xB6,
                    0x07,
                },
        },
        {
            .name = "Store64",
            .code =
                {
                    0x48,
                    0x89,
                    0x17,
                },
        },
        {
            .name = "Store32",
            .code =
                {
                    0x89,
                    0x17,
                },
        },
        {
            .name = "Store16",
            .code =
                {
                    0x66,
                    0x89,
                    0x17,
                },
        },
        {
            .name = "Store8",
            .code =
                {
                    0x88,
                    0x17,
                },
        },
        {
            .name = "StoreImmediate",
            .code =
                {
                    0xC6,
                    0x07,
                    0xFF,
                },
        },
        {
            .name = "HighByteAliasFallback",
            .code =
                {
                    0xB4,
                    0x5A,
                    0x88,
                    0x27,
                },
        },
        {
            .name = "RexByte",
            .code =
                {
                    0x40,
                    0x88,
                    0x37,
                },
        },
        {
            .name = "NarrowDestinationFallback",
            .code =
                {
                    0x66,
                    0x8B,
                    0x07,
                },
        },
        {
            .name = "FsLoad",
            .code =
                {
                    0x64,
                    0x48,
                    0x8B,
                    0x07,
                },
        },
        {
            .name = "GsStore",
            .code =
                {
                    0x65,
                    0x48,
                    0x89,
                    0x17,
                },
        },
        {
            .name = "Lea64",
            .code =
                {
                    0x48,
                    0x8D,
                    0x44,
                    0x8F,
                    0xF8,
                },
        },
        {
            .name = "Lea32",
            .code =
                {
                    0x8D,
                    0x44,
                    0x8F,
                    0xF8,
                },
        },
        {
            .name = "LeaRip",
            .code =
                {
                    0x48,
                    0x8D,
                    0x05,
                    0,
                    0,
                    0,
                    0,
                },
        },
        {
            .name = "LoadRip",
            .code =
                {
                    0x48,
                    0x8B,
                    0x05,
                    3,
                    0,
                    0,
                    0,
                    0xEB,
                    9,
                    0x90,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                },
        },
        {
            .name = "AluImmediate",
            .code =
                {
                    0x48, 0x83, 0xC0, 0xFF, 0x48, 0x83, 0xE8, 0x10, 0x48, 0x83, 0xF8, 0x80,
                    0x48, 0x83, 0xE0, 0xF0, 0x48, 0x83, 0xC8, 0x0F, 0x48, 0x83, 0xF0, 0xFF,
                },
        },
        {
            .name = "AluRegister",
            .code =
                {
                    0x48,
                    0x01,
                    0xD0,
                    0x48,
                    0x29,
                    0xD0,
                    0x48,
                    0x39,
                    0xD0,
                    0x48,
                    0x85,
                    0xD0,
                },
        },
        {
            .name = "AluMemory",
            .code =
                {
                    0x48,
                    0x03,
                    0x07,
                    0x48,
                    0x2B,
                    0x07,
                    0x48,
                    0x3B,
                    0x07,
                    0x48,
                    0x23,
                    0x07,
                },
        },
        {
            .name = "IncDecPreserveCarry",
            .code =
                {
                    0x48,
                    0xFF,
                    0xC0,
                    0x48,
                    0xFF,
                    0xC8,
                },
        },
        {
            .name = "JeJne",
            .code =
                {
                    0x48,
                    0x39,
                    0xC0,
                    0x74,
                    2,
                    0x90,
                    0x90,
                    0x75,
                    1,
                    0x90,
                },
        },
        {
            .name = "OtherConditionsAndJmp",
            .code =
                {
                    0x48,
                    0x39,
                    0xD0,
                    0x72,
                    1,
                    0x90,
                    0x73,
                    1,
                    0x90,
                    0xEB,
                    1,
                    0x90,
                },
        },
        {
            .name = "UnsupportedBankFallback",
            .code =
                {
                    0x0F,
                    0x20,
                    0xC0,
                },
        }, // mov rax, cr0
    };
    for (const Program& program : programs) {
        SCOPED_TRACE(program.name);
        const RunState generic = run_program(program, 0);
        const RunState cached = run_program(program, 512);
        EXPECT_EQ(generic.trace, cached.trace);
        EXPECT_EQ(generic.memory, cached.memory);
        EXPECT_EQ(generic.exit.kind, cached.exit.kind);
        EXPECT_EQ(generic.exit.rip, cached.exit.rip);
        EXPECT_EQ(generic.event_at, cached.event_at);
    }
}

class ReadHookRegion : public micro_forge::kernel::RegisterRegionBase {
public:
    ReadHookRegion(std::span<uint8_t> bytes, Cpu& cpu)
        : RegisterRegionBase(bytes.data(), bytes.size()), cpu_(cpu) {}
    unsigned reads = 0;

protected:
    void on_read(uint64_t offset, unsigned width) override {
        (void)offset;
        EXPECT_EQ(8U, width);
        ++reads;
        cpu_.set_gpr(Reg::kRax, 99);
    }

private:
    Cpu& cpu_;
};

TEST_F(CpuTest, CachedAluKeepsOperandReadOrderForMmioCallbacks) {
    cpu_.configure_caches(ram_.size());
    std::array<uint8_t, 8> registers = {2};
    ReadHookRegion mmio(registers, cpu_);
    ASSERT_TRUE(bus_.map(0x40000000, registers.size(), mmio).has_value());
    load({0x48, 0x03, 0x07}); // add rax, [rdi]; read callback rewrites rax
    cpu_.set_gpr(Reg::kRdi, 0x40000000);
    for (unsigned pass = 0; pass < 2; ++pass) {
        cpu_.set_gpr(Reg::kRax, 5);
        cpu_.set_rip(kCodeBase);
        (void)cpu_.step();
        EXPECT_EQ(7U, cpu_.gpr(Reg::kRax)); // lhs was captured before the MMIO read
        EXPECT_EQ(pass + 1, mmio.reads);
    }
    EXPECT_GT(cpu_.decode_cache_stats().hits, 0U);
}

TEST_F(CpuTest, CachedLoadPreservesFaultRipAndDoesNotRetire) {
    cpu_.configure_caches(ram_.size());
    load({0x48, 0x8B, 0x07}); // mov rax, [rdi]
    cpu_.set_gpr(Reg::kRdi, 0x2000);
    (void)cpu_.step();
    cpu_.set_gpr(Reg::kRdi, 0xDEAD0000);
    cpu_.set_rip(kCodeBase);
    const auto before = cpu_.icount();
    const auto status = cpu_.step();
    EXPECT_EQ(ExitStatus::Kind::kFault, status.kind);
    EXPECT_EQ(kCodeBase, status.rip);
    EXPECT_EQ(0xDEAD0000U, status.code);
    EXPECT_EQ(before, cpu_.icount());
}

} // namespace
