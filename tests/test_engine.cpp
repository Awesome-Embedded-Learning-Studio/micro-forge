/// @file test_engine.cpp
/// @brief The embedding facade: construct, load, run, observe, snapshot.

#include "micro_forge/arch/x86_64/cpu.hpp"
#include "micro_forge/arch/x86_64/decoded.hpp"
#include "micro_forge/base/machine_config.hpp"
#include <cstdint>
#include <gtest/gtest.h>

#include <micro_forge/engine.hpp>

#include <array>
#include <type_traits>
#include <utility>
#include <vector>

#include "micro_forge/base/run_result.hpp"
#include "micro_forge/basic_engine.hpp"
#include "micro_forge/machine.hpp"
#include "micro_forge/machine_model.hpp"
#include "micro_forge/machines/x86_pc.hpp"
#include "micro_forge/machines/x86_pc_config.hpp"

namespace {

using micro_forge::Engine;

TEST(EngineTest, FacadeRunsASmallProgram) {
    // mov al,0x41; hlt -- enough to prove the facade forwarded the verbs.
    const std::vector<uint8_t> code = {0xB0, 0x41, 0xF4};
    micro_forge::MachineConfig config;
    config.ram_size = 0x1000000;
    Engine engine(config);
    ASSERT_TRUE(engine.load_image(0x100000, code));
    engine.machine().boot(0x100000, 0x90000);
    const auto status = engine.run(100);
    // hlt parks the CPU: the run ends without exhausting the budget.
    EXPECT_NE(micro_forge::arch::x86_64::ExitStatus::Kind::kRunning, status.kind);
    EXPECT_EQ(0x41U, engine.machine().cpu().gpr(micro_forge::arch::x86_64::Reg::kRax) & 0xFFU);
}

TEST(EngineTest, SnapshotRoundTripsThroughFacade) {
    micro_forge::MachineConfig config;
    config.ram_size = 0x1000000;
    Engine engine(config);
    const std::vector<uint8_t> code = {0xF4}; // hlt
    ASSERT_TRUE(engine.load_image(0x100000, code));
    engine.machine().boot(0x100000, 0x90000);
    engine.run(10);
    const auto blob = engine.snapshot();
    micro_forge::MachineConfig restored_config;
    restored_config.ram_size = 0x1000000;
    Engine restored(restored_config);
    EXPECT_TRUE(restored.restore(blob).has_value());
    EXPECT_EQ(engine.machine().cpu().icount(), restored.machine().cpu().icount());
}

TEST(EngineTest, CustomConfigIsAccepted) {
    micro_forge::MachineConfig config;
    config.ram_size = 0x1000000;
    Engine engine(config);
    EXPECT_EQ(0x1000000U, engine.machine().ram_size());
}

using PcMachine = micro_forge::machines::X86PcMachine;
using PcEngine = micro_forge::BasicEngine<PcMachine>;
using micro_forge::StopReason;

static_assert(micro_forge::MachineModel<PcMachine>);
static_assert(micro_forge::ConsoleMachine<PcMachine>);
static_assert(micro_forge::SerialInputMachine<PcMachine>);
static_assert(micro_forge::SnapshotMachine<PcMachine>);
static_assert(std::is_same_v<micro_forge::Machine, PcMachine>);
static_assert(std::is_same_v<micro_forge::MachineConfig, micro_forge::machines::X86PcConfig>);
static_assert(!std::is_copy_constructible_v<PcMachine>);
static_assert(!std::is_move_constructible_v<PcMachine>);
static_assert(!std::is_copy_assignable_v<PcMachine>);
static_assert(!std::is_move_assignable_v<PcMachine>);
static_assert(std::is_same_v<decltype(std::declval<Engine&>().run(0)),
                             micro_forge::arch::x86_64::ExitStatus>);

TEST(EngineTest, PortablePcFacadePreservesZeroAndFiniteInstructionBudgets) {
    micro_forge::machines::X86PcConfig config;
    config.ram_size = 0x1000000;
    PcEngine engine(std::in_place, config);
    const std::array<uint8_t, 4> code = {0x90, 0x90, 0x90, 0xF4};
    ASSERT_TRUE(engine.load_image(0x100000, code));
    engine.machine().boot(0x100000, 0x90000);

    const auto zero = engine.run(0);
    EXPECT_EQ(StopReason::kBudgetExhausted, zero.reason);
    EXPECT_EQ(0x100000U, zero.pc);
    EXPECT_EQ(0U, engine.machine().cpu().icount());
    const auto budget = engine.run(3);
    EXPECT_EQ(StopReason::kBudgetExhausted, budget.reason);
    EXPECT_EQ(0x100003U, budget.pc);
    EXPECT_EQ(3U, engine.machine().cpu().icount());
    EXPECT_EQ(StopReason::kHalted, engine.run(1).reason);
}

TEST(EngineTest, PortablePcFacadeMapsStopsWithoutDiscardingNativeDiagnostics) {
    struct Sample {
        std::vector<uint8_t> code;
        StopReason reason;
    };
    const std::array samples = {
        Sample{.code = {0xF4}, .reason = StopReason::kHalted},
        // mov edi,42; mov eax,60; syscall
        Sample{
            .code = {0xBF, 42, 0, 0, 0, 0xB8, 60, 0, 0, 0, 0x0F, 0x05},
            .reason = StopReason::kExited,
        },
        // in al,0x40 -- an unmapped port in the bare PC configuration.
        Sample{.code = {0xE4, 0x40}, .reason = StopReason::kFault},
        // movabs rax,0xFEED0000; mov dword ptr [rax],3
        Sample{
            .code = {0x48, 0xB8, 0, 0, 0xED, 0xFE, 0, 0, 0, 0, 0xC7, 0, 3, 0, 0, 0},
            .reason = StopReason::kDebugExit,
        },
    };
    for (const auto& sample : samples) {
        SCOPED_TRACE(static_cast<unsigned>(sample.reason));
        PcEngine engine(std::in_place, uint64_t{0x1000000});
        ASSERT_TRUE(engine.load_image(0x100000, sample.code));
        engine.machine().boot(0x100000, 0x90000);
        const auto result = engine.run(100);
        const auto& native = engine.machine().cpu().exit_status();
        EXPECT_EQ(sample.reason, result.reason);
        EXPECT_EQ(native.rip, result.pc);
        EXPECT_EQ(native.code, result.code);
        EXPECT_EQ(native.detail, result.detail);
        EXPECT_EQ(sample.reason, engine.run(0).reason);
    }
}

TEST(EngineTest, PortablePcSnapshotStillWorksWithTheCompatibilityFacade) {
    micro_forge::machines::X86PcConfig config;
    config.ram_size = 0x1000000;
    PcEngine engine(std::in_place, config);
    const std::array<uint8_t, 4> code = {0x90, 0x90, 0x90, 0xF4};
    ASSERT_TRUE(engine.load_image(0x100000, code));
    engine.machine().boot(0x100000, 0x90000);
    engine.run(2);
    const auto blob = engine.snapshot();

    Engine legacy(config);
    ASSERT_TRUE(legacy.restore(blob).has_value());
    EXPECT_EQ(engine.machine().cpu().rip(), legacy.machine().cpu().rip());
    EXPECT_EQ(engine.machine().cpu().icount(), legacy.machine().cpu().icount());
    PcEngine restored(std::in_place, config);
    ASSERT_TRUE(restored.restore(legacy.snapshot()).has_value());
    EXPECT_EQ(StopReason::kBudgetExhausted, restored.run(0).reason);
    EXPECT_EQ(legacy.machine().cpu().icount(), restored.machine().cpu().icount());
    EXPECT_EQ(micro_forge::arch::x86_64::ExitStatus::Kind::kHalt, legacy.run(10).kind);
    EXPECT_EQ(StopReason::kHalted, restored.run(10).reason);
    EXPECT_EQ(legacy.machine().cpu().icount(), restored.machine().cpu().icount());
}

} // namespace
