/// @file test_engine.cpp
/// @brief The embedding facade: construct, load, run, observe, snapshot.

#include <gtest/gtest.h>

#include <micro_forge/engine.hpp>

#include <vector>

namespace {

using micro_forge::Engine;

TEST(EngineTest, FacadeRunsASmallProgram) {
    // mov al,0x41; hlt -- enough to prove the facade forwarded the verbs.
    const std::vector<uint8_t> code = {0xB0, 0x41, 0xF4};
    Engine engine(0x1000000);
    ASSERT_TRUE(engine.load_image(0x100000, code));
    engine.machine().boot(0x100000, 0x90000);
    const auto status = engine.run(100);
    // hlt parks the CPU: the run ends without exhausting the budget.
    EXPECT_NE(micro_forge::arch::x86_64::ExitStatus::Kind::kRunning, status.kind);
    EXPECT_EQ(0x41U, engine.machine().cpu().gpr(micro_forge::arch::x86_64::Reg::kRax) & 0xFFU);
}

TEST(EngineTest, SnapshotRoundTripsThroughFacade) {
    Engine engine(0x1000000);
    const std::vector<uint8_t> code = {0xF4}; // hlt
    ASSERT_TRUE(engine.load_image(0x100000, code));
    engine.machine().boot(0x100000, 0x90000);
    (void)engine.run(10);
    const auto blob = engine.snapshot();
    Engine restored(0x1000000);
    EXPECT_FALSE(restored.restore(blob).has_value());
    EXPECT_EQ(engine.machine().cpu().icount(), restored.machine().cpu().icount());
}

TEST(EngineTest, CustomConfigIsAccepted) {
    micro_forge::MachineConfig config;
    config.ram_size = 0x1000000;
    Engine engine(config);
    EXPECT_EQ(0x1000000U, engine.machine().ram_size());
}

} // namespace
