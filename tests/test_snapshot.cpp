/// @file test_snapshot.cpp
/// @brief Batch-4 acceptance: snapshot round-trip is bit-identical to an
///        uninterrupted run, and the version triple refuses mismatches.

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/kernel/bus.hpp>
#include <micro_forge/kernel/ram_region.hpp>
#include <micro_forge/kernel/scheduler.hpp>
#include <micro_forge/kernel/snapshot.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace {

using micro_forge::arch::x86_64::Cpu;
using micro_forge::arch::x86_64::ExitStatus;
using micro_forge::kernel::Bus;
using micro_forge::kernel::RamRegion;
using micro_forge::kernel::Scheduler;
using micro_forge::kernel::SnapshotError;
using micro_forge::kernel::SnapshotReader;
using micro_forge::kernel::SnapshotWriter;
using micro_forge::kernel::Tick;

constexpr uint64_t kCodeBase = 0x100000;
constexpr uint64_t kStackTop = 0x90000;
constexpr uint32_t kEngineVersion = micro_forge::kernel::pack_engine_version(0, 1, 0);

/// Test-local machine assembly: RAM, CPU, and one periodic device (an
/// event every 7 icounts recording when it fired). Not a public Machine
/// -- batch 5 owns that assembly; this proves the kernel pieces
/// round-trip. The device is wired explicitly (never in the constructor)
/// so a restore target starts with a clean scheduler.
struct Machine {
    Bus bus;
    RamRegion ram{0x1000000};
    Scheduler scheduler;
    Cpu cpu{bus, scheduler};
    std::vector<Tick> device_ticks;
    std::vector<uint64_t> rip_trace;

    explicit Machine(const std::vector<uint8_t>& image) {
        if (!bus.map(0, ram.size(), ram).has_value()) {
            ADD_FAILURE() << "mapping RAM failed";
            return;
        }
        std::ranges::copy(image, ram.data() + kCodeBase);
        cpu.reset(kCodeBase, kStackTop);
        cpu.set_trace([this](uint64_t rip) { rip_trace.push_back(rip); });
    }

    /// Wires the periodic tick device (one chain, every 7 icounts).
    void wire_periodic_device() {
        (void)scheduler.schedule_periodic(5, 7, [this] { device_ticks.push_back(cpu.icount()); });
    }

    /// Seals the full machine state: RAM, then CPU, then scheduler.
    [[nodiscard]] std::vector<uint8_t> snapshot() const {
        SnapshotWriter writer(kEngineVersion);
        ram.save(writer);
        cpu.save(writer);
        scheduler.save(writer);
        return writer.take();
    }

    /// Rebuilds machine contents from @p blob onto this (fresh) object:
    /// RAM and CPU in place, then the event table with rewired callbacks.
    bool restore_from(const std::vector<uint8_t>& blob) {
        auto reader = SnapshotReader::open(blob, kEngineVersion);
        if (!reader) {
            return false;
        }
        if (!ram.restore(*reader)) {
            return false;
        }
        if (!cpu.restore(*reader)) {
            return false;
        }
        const auto events = scheduler.restore_begin(*reader);
        if (!events.has_value()) {
            return false;
        }
        for (uint64_t i = 0; i < *events; ++i) {
            const auto info = Scheduler::restore_read_event(*reader);
            if (!info.has_value()) {
                return false;
            }
            // This machine owns exactly one event family (the periodic
            // tick); a real device bank dispatches on its own ids.
            scheduler.restore_event(*info, [this] { device_ticks.push_back(cpu.icount()); });
        }
        return true;
    }
};

/// One continuous run of the bare client with the periodic device.
struct RunRecord {
    std::vector<uint64_t> rip_trace;
    std::vector<Tick> device_ticks;
    ExitStatus exit{};
    Tick icount = 0;
};

RunRecord run_continuous(const std::vector<uint8_t>& image) {
    Machine machine(image);
    machine.wire_periodic_device();
    RunRecord record;
    record.exit = machine.cpu.run(1ULL << 20U);
    record.icount = machine.cpu.icount();
    record.rip_trace = machine.rip_trace;
    record.device_ticks = machine.device_ticks;
    return record;
}

std::vector<uint8_t> load_client() {
    std::ifstream file(BARE_CLIENT_BIN, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot open " << BARE_CLIENT_BIN;
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TEST(SnapshotTest, RoundTripContinuesBitIdentically) {
    const auto image = load_client();
    ASSERT_FALSE(image.empty());

    // Reference: one uninterrupted run.
    const RunRecord straight = run_continuous(image);
    ASSERT_EQ(ExitStatus::Kind::kExit, straight.exit.kind);

    // Interrupted: run 30 instructions, snapshot, continue elsewhere.
    Machine first(image);
    first.wire_periodic_device();
    (void)first.cpu.run(30);
    const auto blob = first.snapshot();

    Machine second(image); // fresh objects; no device wired yet
    ASSERT_TRUE(second.restore_from(blob));
    ASSERT_TRUE(second.rip_trace.empty()); // nothing recorded pre-restore
    const ExitStatus second_exit = second.cpu.run(1ULL << 20U);

    std::vector<uint64_t> spliced = first.rip_trace;
    spliced.insert(spliced.end(), second.rip_trace.begin(), second.rip_trace.end());
    std::vector<Tick> ticks = first.device_ticks;
    ticks.insert(ticks.end(), second.device_ticks.begin(), second.device_ticks.end());

    EXPECT_EQ(straight.rip_trace, spliced);
    EXPECT_EQ(straight.device_ticks, ticks);
    EXPECT_EQ(straight.exit.kind, second_exit.kind);
    EXPECT_EQ(straight.exit.code, second_exit.code);
    EXPECT_EQ(straight.icount, second.cpu.icount());
}

TEST(SnapshotTest, ShortSectionReadFailsRestore) {
    // A section payload shorter than its own length claim must fail the
    // component restore (previously no test exercised the false path).
    micro_forge::kernel::SnapshotWriter writer(kEngineVersion);
    writer.u64(16); // claims a 16-byte region
    writer.u8(1);
    writer.u8(2);
    writer.u8(3);
    writer.u8(4); // only 4 bytes follow
    const auto blob = writer.take();
    auto reader = SnapshotReader::open(blob, kEngineVersion);
    ASSERT_TRUE(reader.has_value());
    RamRegion ram(16);
    EXPECT_FALSE(ram.restore(*reader));
}

TEST(SnapshotTest, EngineVersionMismatchIsRefused) {
    const auto image = load_client();
    Machine machine(image);
    machine.wire_periodic_device();
    (void)machine.cpu.run(5);
    const auto blob = machine.snapshot();

    const uint32_t other = micro_forge::kernel::pack_engine_version(9, 9, 9);
    const auto reader = SnapshotReader::open(blob, other);
    ASSERT_FALSE(reader.has_value());
    EXPECT_EQ(SnapshotError::kEngineMismatch, reader.error());
}

TEST(SnapshotTest, BadMagicIsRefused) {
    const auto image = load_client();
    Machine machine(image);
    machine.wire_periodic_device();
    auto blob = machine.snapshot();
    blob[0] = static_cast<std::uint8_t>(blob[0] ^ 0xFFU);

    const auto reader = SnapshotReader::open(blob, kEngineVersion);
    ASSERT_FALSE(reader.has_value());
    EXPECT_EQ(SnapshotError::kBadMagic, reader.error());
}

TEST(SnapshotTest, FormatVersionMismatchIsRefused) {
    const auto image = load_client();
    Machine machine(image);
    machine.wire_periodic_device();
    auto blob = machine.snapshot();
    blob[8] = static_cast<std::uint8_t>(blob[8] ^ 0xFFU); // format-version field

    const auto reader = SnapshotReader::open(blob, kEngineVersion);
    ASSERT_FALSE(reader.has_value());
    EXPECT_EQ(SnapshotError::kFormatMismatch, reader.error());
}

TEST(SnapshotTest, TruncatedBlobIsRefused) {
    const auto image = load_client();
    Machine machine(image);
    machine.wire_periodic_device();
    auto blob = machine.snapshot();
    blob.pop_back();

    const auto reader = SnapshotReader::open(blob, kEngineVersion);
    ASSERT_FALSE(reader.has_value());
    EXPECT_EQ(SnapshotError::kTruncated, reader.error());
}

} // namespace
