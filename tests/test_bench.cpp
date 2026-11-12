#include <gtest/gtest.h>

#include <micro_forge/kernel/bus/bus.hpp>
#include <micro_forge/kernel/bus/bus_error.hpp>
#include <micro_forge/kernel/memory/ram_region.hpp>
#include <micro_forge/kernel/scheduler.hpp>

#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

namespace {

using micro_forge::kernel::Bus;
using micro_forge::kernel::BusError;
using micro_forge::kernel::RamRegion;
using micro_forge::kernel::Region;
using micro_forge::kernel::Scheduler;
using micro_forge::kernel::Tick;

/// Test-bench CPU: retires one instruction per step, advances the icount
/// clock, and lets the scheduler fire everything due. Real decode lands
/// with the machine cores; this only exercises kernel plumbing.
class FakeCpu {
public:
    explicit FakeCpu(Scheduler& scheduler) : scheduler_(scheduler) {}

    Tick step() {
        ++tick_;
        scheduler_.dispatch(tick_);
        return tick_;
    }

    Tick run_until(Tick limit) {
        while (tick_ < limit) {
            step();
        }
        return tick_;
    }

    [[nodiscard]] Tick tick() const { return tick_; }

private:
    Scheduler& scheduler_;
    Tick tick_ = 0;
};

/// Test-bench timer peripheral: writing the EXPIRE_AT register schedules a
/// one-shot callback that latches the icount at which it fired. Absolute
/// icount programming keeps the stub free of a current-time input; real
/// peripherals will read the clock through their bus interface.
class StubTimer : public Region {
public:
    static constexpr uint64_t kBase = 0x40000000;
    static constexpr uint64_t kRegExpireAt = 0x0;
    static constexpr uint64_t kRegFiredAt = 0x4;
    static constexpr uint64_t kRegSize = 0x8;

    StubTimer(Scheduler& scheduler, Bus& bus) : scheduler_(scheduler) {
        EXPECT_TRUE(bus.map(kBase, kRegSize, *this).has_value());
    }

    // Byte-wise and halfword access is meaningless on a 32-bit register
    // interface.
    [[nodiscard]] std::expected<uint8_t, BusError> read_u8(uint64_t offset) override {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

    [[nodiscard]] std::expected<uint16_t, BusError> read_u16(uint64_t offset) override {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

    [[nodiscard]] std::expected<void, BusError> write_u8(uint64_t offset,
                                                         uint8_t /*value*/) override {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

    [[nodiscard]] std::expected<void, BusError> write_u16(uint64_t offset,
                                                          uint16_t /*value*/) override {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

    [[nodiscard]] std::expected<uint32_t, BusError> read_u32(uint64_t offset) override {
        if (offset == kRegFiredAt) {
            return fired_at_;
        }
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

    // 64-bit access is meaningless on a 32-bit register interface.
    [[nodiscard]] std::expected<uint64_t, BusError> read_u64(uint64_t offset) override {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

    [[nodiscard]] std::expected<void, BusError> write_u32(uint64_t offset,
                                                          uint32_t value) override {
        if (offset == kRegExpireAt) {
            scheduler_.schedule(value, [this, value] { fired_at_ = value; });
            return {};
        }
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

    [[nodiscard]] std::expected<void, BusError> write_u64(uint64_t offset,
                                                          uint64_t /*value*/) override {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
    }

private:
    Scheduler& scheduler_;
    uint32_t fired_at_ = 0;
};

TEST(BenchTest, TimerFiresAtProgrammedIcount) {
    Scheduler scheduler;
    Bus bus;
    RamRegion ram(0x1000);
    ASSERT_TRUE(bus.map(0x00000000, 0x1000, ram).has_value());
    const StubTimer timer(scheduler, bus);

    FakeCpu cpu(scheduler);
    cpu.run_until(10);

    // Program "expire when 110 instructions have retired", then run there.
    ASSERT_TRUE(bus.write_u32(StubTimer::kBase + StubTimer::kRegExpireAt, 110).has_value());
    cpu.run_until(200);
    EXPECT_EQ(200, cpu.tick());

    const auto fired = bus.read_u32(StubTimer::kBase + StubTimer::kRegFiredAt);
    ASSERT_TRUE(fired.has_value());
    EXPECT_EQ(110, *fired);
}

TEST(BenchTest, TimerBeforeProgrammingNeverFires) {
    Scheduler scheduler;
    Bus bus;
    const StubTimer timer(scheduler, bus);

    FakeCpu cpu(scheduler);
    cpu.run_until(1000);

    const auto fired = bus.read_u32(StubTimer::kBase + StubTimer::kRegFiredAt);
    ASSERT_TRUE(fired.has_value());
    EXPECT_EQ(0, *fired);
}

TEST(BenchTest, IdenticalRunsProduceIdenticalTraces) {
    // Batch-1 acceptance: with the same sequence of guest writes, two full
    // runs of the bench produce a bit-identical (icount, fired_at) trace.
    // Every trace entry is read back over the bus, so the loop covers the
    // write -> schedule -> dispatch -> read chain end to end.
    const auto run = [] {
        Scheduler scheduler;
        Bus bus;
        const StubTimer timer(scheduler, bus);
        FakeCpu cpu(scheduler);

        std::vector<std::pair<Tick, uint32_t>> trace;
        for (uint32_t i = 1; i <= 16; ++i) {
            const uint32_t expire = i * 13; // deadlines interleave with the runs
            EXPECT_TRUE(
                bus.write_u32(StubTimer::kBase + StubTimer::kRegExpireAt, expire).has_value());
            cpu.run_until(static_cast<Tick>(i) * 10U);
            const auto fired = bus.read_u32(StubTimer::kBase + StubTimer::kRegFiredAt);
            trace.emplace_back(cpu.tick(), fired.value_or(0xDEADBEEFU));
        }
        cpu.run_until(1000);
        return trace;
    };

    const auto first = run();
    const auto second = run();
    ASSERT_EQ(16U, first.size());
    EXPECT_EQ(first, second);
}

} // namespace
