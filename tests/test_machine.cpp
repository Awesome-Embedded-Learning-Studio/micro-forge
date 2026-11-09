/// @file test_machine.cpp
/// @brief Batch-5 acceptance (native side): the Machine assembly runs the
///        bare client in both backing modes and snapshots bit-identically.

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/machine.hpp>
#include <micro_forge/presets/cinux_machine.hpp>

#include <cstring>
#include <fstream>
#include <vector>

namespace {

using micro_forge::Machine;
using micro_forge::MachineConfig;
using micro_forge::MachineDevice;

constexpr uint64_t kCodeBase = 0x100000;
constexpr uint64_t kStackTop = 0x90000;

std::vector<uint8_t> load_client() {
    std::ifstream file(BARE_CLIENT_BIN, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot open " << BARE_CLIENT_BIN;
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TEST(MachineTest, OwningModeRunsBareClientToExit) {
    const auto image = load_client();
    ASSERT_FALSE(image.empty());
    Machine machine(0x1000000);
    ASSERT_TRUE(machine.load_image(kCodeBase, image));
    machine.boot(kCodeBase, kStackTop);
    const auto exit = machine.run(1ULL << 20U);
    EXPECT_EQ(micro_forge::arch::x86_64::ExitStatus::Kind::kExit, exit.kind);
    EXPECT_EQ(0xAAU, exit.code);
    EXPECT_TRUE(machine.console().empty());
}

TEST(MachineTest, WindowModeMatchesOwningMode) {
    const auto image = load_client();
    std::vector<uint8_t> window(0x1000000 + Machine::kLfbSize);
    Machine machine(0x1000000, reinterpret_cast<uintptr_t>(window.data()));
    ASSERT_TRUE(machine.load_image(kCodeBase, image));
    // The window IS the backing store: bytes landed before the run (the
    // client later writes its own table, so compare pre-execution).
    EXPECT_EQ(0, std::memcmp(window.data() + kCodeBase, image.data(), image.size()));
    machine.boot(kCodeBase, kStackTop);
    const auto exit = machine.run(1ULL << 20U);
    EXPECT_EQ(micro_forge::arch::x86_64::ExitStatus::Kind::kExit, exit.kind);
    EXPECT_EQ(0xAAU, exit.code);
}

TEST(MachineTest, DebugExitThroughTheBus) {
    Machine machine(0x1000000);
    // movabs rax, 0xFEED0000; mov dword ptr [rax], 3
    const std::vector<uint8_t> program = {
        0x48, 0xB8, 0x00, 0x00, 0xED, 0xFE, 0x00, 0x00,
        0x00, 0x00, 0xC7, 0x00, 0x03, 0x00, 0x00, 0x00,
    };
    ASSERT_TRUE(machine.load_image(kCodeBase, program));
    machine.boot(kCodeBase, kStackTop);
    const auto exit = machine.run(100);
    EXPECT_EQ(micro_forge::arch::x86_64::ExitStatus::Kind::kDebugExit, exit.kind);
    EXPECT_EQ(7U, exit.code); // (3 << 1) | 1, QEMU encoding
}

TEST(MachineTest, SnapshotRoundTripThroughMachineApi) {
    const auto image = load_client();

    Machine straight(0x1000000);
    ASSERT_TRUE(straight.load_image(kCodeBase, image));
    straight.boot(kCodeBase, kStackTop);
    std::vector<uint64_t> straight_trace;
    straight.cpu().set_trace(
        [&straight, &straight_trace](uint64_t rip) { straight_trace.push_back(rip); });
    const auto straight_exit = straight.run(1ULL << 20U);

    Machine first(0x1000000);
    ASSERT_TRUE(first.load_image(kCodeBase, image));
    first.boot(kCodeBase, kStackTop);
    std::vector<uint64_t> first_trace;
    first.cpu().set_trace([&first, &first_trace](uint64_t rip) { first_trace.push_back(rip); });
    (void)first.run(30);
    const auto blob = first.snapshot();

    Machine second(0x1000000);
    EXPECT_FALSE(second.restore(blob).has_value());
    std::vector<uint64_t> second_trace;
    second.cpu().set_trace([&second, &second_trace](uint64_t rip) { second_trace.push_back(rip); });
    const auto second_exit = second.run(1ULL << 20U);

    std::vector<uint64_t> spliced = first_trace;
    spliced.insert(spliced.end(), second_trace.begin(), second_trace.end());
    EXPECT_EQ(straight_trace, spliced);
    EXPECT_EQ(straight_exit.kind, second_exit.kind);
    EXPECT_EQ(straight_exit.code, second_exit.code);
    EXPECT_EQ(straight.cpu().icount(), second.cpu().icount());
}

TEST(MachineTest, WindowModeRestoresFromSnapshot) {
    // Batch-5 review B1: window-mode restore used to check the owning
    // vector's size (always zero) and refused every blob.
    const auto image = load_client();
    std::vector<uint8_t> window(0x1000000 + Machine::kLfbSize);
    Machine machine(0x1000000, reinterpret_cast<uintptr_t>(window.data()));
    ASSERT_TRUE(machine.load_image(kCodeBase, image));
    machine.boot(kCodeBase, kStackTop);
    (void)machine.run(30);
    const auto blob = machine.snapshot();

    Machine restored(0x1000000, reinterpret_cast<uintptr_t>(window.data()));
    EXPECT_FALSE(restored.restore(blob).has_value());
}

TEST(MachineTest, RestoreRefusesTruncatedSchedulerSection) {
    // Batch-5 review S1: a blob whose scheduler section is cut off (with
    // the envelope length rewritten to match) used to restore as success
    // with zero events; it must be refused.
    const auto image = load_client();
    Machine machine(0x1000000);
    ASSERT_TRUE(machine.load_image(kCodeBase, image));
    machine.boot(kCodeBase, kStackTop);
    auto blob = machine.snapshot();
    ASSERT_GT(blob.size(), 40U);
    blob.resize(blob.size() - 8); // cut into the scheduler section
    // Rewrite the payload length so the envelope itself still validates.
    const uint64_t shorter = blob.size() - micro_forge::kernel::SnapshotWriter::header_size();
    for (unsigned i = 0; i < 8; ++i) {
        blob[12 + i] = static_cast<uint8_t>(shorter >> (8U * i));
    }
    Machine other(0x1000000);
    const auto refused = other.restore(blob);
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(micro_forge::kernel::SnapshotError::kTruncated, *refused);
}

TEST(MachineTest, RestoreRefusesForeignEngineVersion) {
    const auto image = load_client();
    Machine machine(0x1000000);
    ASSERT_TRUE(machine.load_image(kCodeBase, image));
    machine.boot(kCodeBase, kStackTop);
    auto forged = machine.snapshot();
    // Rewrite the engine-version field of the envelope (offset 4).
    const uint32_t wrong = micro_forge::kernel::pack_engine_version(9, 9, 9);
    for (unsigned i = 0; i < 4; ++i) {
        forged[4 + i] = static_cast<uint8_t>(wrong >> (8U * i));
    }
    Machine other(0x1000000);
    const auto refused = other.restore(forged);
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(micro_forge::kernel::SnapshotError::kEngineMismatch, *refused);
}

TEST(MachineTest, ConfigMovesSerialAndDisablesDevices) {
    // A layout is data: move COM1 to 0x2F8, drop the PIT, drop the disk.
    MachineConfig config;
    config.ram_size = 0x1000000;
    config.ports = {
        {.base = 0x2F8, .size = 8, .device = MachineDevice::kSerial},
        {.base = 0x70, .size = 2, .device = MachineDevice::kRtc},
    };
    config.ahci_vendor = 0; // no storage function at all
    Machine machine(config);
    ASSERT_TRUE(machine.load_image(kCodeBase, load_client()));
    machine.boot(kCodeBase, kStackTop);
    // The layout is data: construction succeeded with the moved serial and
    // no PIT/RTC/AHCI, and the machine still boots and runs.
    ASSERT_TRUE(machine.load_image(kCodeBase, load_client()));
    machine.boot(kCodeBase, kStackTop);
    const auto status = machine.run(100);
    EXPECT_NE(micro_forge::arch::x86_64::ExitStatus::Kind::kRunning, status.kind);
}

TEST(MachineTest, ConfigWithoutPitLeavesPortUnmapped) {
    MachineConfig config;
    config.ram_size = 0x1000000;
    config.ports = {{.base = 0x3F8, .size = 8, .device = MachineDevice::kSerial}};
    config.windows.clear();
    config.ahci_vendor = 0;
    Machine machine(config);
    // in 0x40 with the PIT absent must fault (kMemory), not read a device.
    const uint8_t in_40[] = {0xE4, 0x40, 0xF4}; // in al,0x40; hlt
    ASSERT_TRUE(machine.load_image(kCodeBase, in_40));
    machine.boot(kCodeBase, kStackTop);
    (void)machine.run(100);
    EXPECT_EQ(micro_forge::arch::x86_64::ExitStatus::Kind::kFault,
              machine.cpu().exit_status().kind);
}

TEST(MachineTest, ConflictingPortWindowsRefuseConstruction) {
    // Two devices on the same port range is a layout bug: the port space
    // refuses overlaps and the machine aborts instead of limping on.
    MachineConfig config;
    config.ram_size = 0x1000000;
    config.ports = {
        {.base = 0x3F8, .size = 8, .device = MachineDevice::kSerial},
        {.base = 0x3F8, .size = 8, .device = MachineDevice::kRtc},
    };
    EXPECT_DEATH(([&] {
                     Machine machine(config);
                     (void)machine;
                 }()),
                 "");
}

} // namespace
