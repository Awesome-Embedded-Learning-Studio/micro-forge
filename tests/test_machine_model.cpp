/// @file test_machine_model.cpp
/// @brief Architecture-independent facade tests using test doubles, not guest ISAs.

#include <micro_forge/base/run_result.hpp>
#include <micro_forge/basic_engine.hpp>
#include <micro_forge/kernel/snapshot.hpp>
#include <micro_forge/machine_model.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using micro_forge::BasicEngine;
using micro_forge::ConsoleMachine;
using micro_forge::MachineModel;
using micro_forge::RunResult;
using micro_forge::SerialInputMachine;
using micro_forge::SnapshotMachine;
using micro_forge::StopReason;
using micro_forge::kernel::SnapshotError;

constexpr uint64_t kFlashBase = 0x1'0800'0000ULL;
constexpr uint64_t kSramBase = 0x2'2000'0000ULL;

struct SplitMemoryConfig {
    uint64_t flash_base = kFlashBase;
    uint64_t sram_base = kSramBase;
    bool fault = false;
};

// Test double only: two disjoint arrays, no CPU, ports, or instruction decoder.
// Its observable run behavior is a counter, not an implementation of an ISA.
class SplitMemoryTestDouble {
public:
    explicit SplitMemoryTestDouble(SplitMemoryConfig config)
        : config_(config), pc_(config.flash_base + 4), self_(this),
          first_sram_byte_(sram_.front()) {}

    SplitMemoryTestDouble(const SplitMemoryTestDouble&) = delete;
    SplitMemoryTestDouble& operator=(const SplitMemoryTestDouble&) = delete;
    SplitMemoryTestDouble(SplitMemoryTestDouble&&) = delete;
    SplitMemoryTestDouble& operator=(SplitMemoryTestDouble&&) = delete;

    bool load_image(uint64_t address, std::span<const uint8_t> bytes) {
        return load_segment(config_.flash_base, flash_, address, bytes) ||
               load_segment(config_.sram_base, sram_, address, bytes);
    }

    RunResult run_for(uint64_t budget) {
        last_budget = budget;
        ++run_calls;
        if (config_.fault) {
            return {
                .reason = StopReason::kFault,
                .pc = pc_,
                .code = 0x42,
                .detail = config_.sram_base + sram_.size(),
            };
        }
        pc_ += budget;
        return {
            .reason = StopReason::kBudgetExhausted,
            .pc = pc_,
            .code = budget,
            .detail = run_calls,
        };
    }

    [[nodiscard]] const SplitMemoryTestDouble* self() const { return self_; }
    [[nodiscard]] const uint8_t* referenced_sram_byte() const { return &first_sram_byte_; }
    [[nodiscard]] std::span<const uint8_t> flash() const { return flash_; }
    [[nodiscard]] std::span<const uint8_t> sram() const { return sram_; }

    uint64_t last_budget = 0;
    uint64_t run_calls = 0;

private:
    static bool load_segment(uint64_t base, std::span<uint8_t> segment, uint64_t address,
                             std::span<const uint8_t> bytes) {
        if (address < base || address - base > segment.size()) {
            return false;
        }
        const auto offset = static_cast<std::size_t>(address - base);
        if (bytes.size() > segment.size() - offset) {
            return false;
        }
        std::ranges::copy(bytes, segment.subspan(offset).begin());
        return true;
    }

    SplitMemoryConfig config_;
    uint64_t pc_;
    std::array<uint8_t, 16> flash_{};
    std::array<uint8_t, 8> sram_{};
    const SplitMemoryTestDouble* self_;
    uint8_t& first_sram_byte_;
};

// A second test double with a vector-backed image and byte-sized saved state.
// Snapshot bytes and console output are deliberately test-only formats.
class ObservableTestDouble {
public:
    explicit ObservableTestDouble(uint8_t state) : state_(state) {}

    bool load_image(uint64_t address, std::span<const uint8_t> bytes) {
        loaded_address = address;
        image.assign(bytes.begin(), bytes.end());
        return true;
    }

    RunResult run_for(uint64_t budget) {
        return {
            .reason = StopReason::kExited,
            .pc = loaded_address,
            .code = state_,
            .detail = budget,
        };
    }

    [[nodiscard]] std::span<const char> console() const { return output_; }

    void inject_serial(uint8_t value) {
        serial_input.push_back(value);
        state_ = value;
    }

    [[nodiscard]] std::vector<uint8_t> snapshot() const {
        ++snapshot_calls;
        return {state_};
    }

    std::expected<void, SnapshotError> restore(std::span<const uint8_t> blob) {
        ++restore_calls;
        if (blob.size() != 1) {
            return std::unexpected(SnapshotError::kTruncated);
        }
        if (blob.front() == 0xFF) {
            return std::unexpected(SnapshotError::kBadMagic);
        }
        state_ = blob.front();
        return {};
    }

    uint64_t loaded_address = 0;
    std::vector<uint8_t> image;
    std::vector<uint8_t> serial_input;
    mutable unsigned snapshot_calls = 0;
    unsigned restore_calls = 0;

private:
    uint8_t state_;
    std::array<char, 3> output_ = {'o', '\0', 'k'};
};

// Declaration-only test doubles exercise the exact required signatures.
struct RequiredMethods {
    bool load_image(uint64_t, std::span<const uint8_t>);
    RunResult run_for(uint64_t);
};

struct MissingLoad {
    RunResult run_for(uint64_t);
};

struct MissingRun {
    bool load_image(uint64_t, std::span<const uint8_t>);
};

struct WrongLoadReturn {
    int load_image(uint64_t, std::span<const uint8_t>);
    RunResult run_for(uint64_t);
};

struct WrongRunReturn {
    bool load_image(uint64_t, std::span<const uint8_t>);
    uint64_t run_for(uint64_t);
};

struct MutableImageOnly {
    bool load_image(uint64_t, std::span<uint8_t>);
    RunResult run_for(uint64_t);
};

struct NonConstConsole : RequiredMethods {
    std::span<const char> console();
};

struct WrongConsoleReturn : RequiredMethods {
    [[nodiscard]] std::span<char> console() const;
};

struct WrongSerialReturn : RequiredMethods {
    bool inject_serial(uint8_t);
};

struct SnapshotOnly : RequiredMethods {
    [[nodiscard]] std::vector<uint8_t> snapshot() const;
};

struct RestoreOnly : RequiredMethods {
    std::expected<void, SnapshotError> restore(std::span<const uint8_t>);
};

struct NonConstSnapshot : RequiredMethods {
    std::vector<uint8_t> snapshot();
    std::expected<void, SnapshotError> restore(std::span<const uint8_t>);
};

struct WrongSnapshotReturn : RequiredMethods {
    [[nodiscard]] std::span<const uint8_t> snapshot() const;
    std::expected<void, SnapshotError> restore(std::span<const uint8_t>);
};

struct WrongRestoreReturn : RequiredMethods {
    [[nodiscard]] std::vector<uint8_t> snapshot() const;
    bool restore(std::span<const uint8_t>);
};

template <typename T>
concept HasConsole = requires(const T& value) { value.console(); };

template <typename T>
concept HasSerialInput = requires(T& value) { value.inject_serial(uint8_t{}); };

template <typename T>
concept HasSnapshot = requires(const T& value) { value.snapshot(); };

template <typename T>
concept HasRestore = requires(T& value, std::span<const uint8_t> blob) { value.restore(blob); };

using MinimalEngine = BasicEngine<SplitMemoryTestDouble>;
using ObservableEngine = BasicEngine<ObservableTestDouble>;

static_assert(MachineModel<SplitMemoryTestDouble>);
static_assert(MachineModel<ObservableTestDouble>);
static_assert(MachineModel<RequiredMethods>);
static_assert(!MachineModel<MissingLoad>);
static_assert(!MachineModel<MissingRun>);
static_assert(!MachineModel<WrongLoadReturn>);
static_assert(!MachineModel<WrongRunReturn>);
static_assert(!MachineModel<MutableImageOnly>);
static_assert(!ConsoleMachine<SplitMemoryTestDouble>);
static_assert(!SerialInputMachine<SplitMemoryTestDouble>);
static_assert(!SnapshotMachine<SplitMemoryTestDouble>);
static_assert(ConsoleMachine<ObservableTestDouble>);
static_assert(SerialInputMachine<ObservableTestDouble>);
static_assert(SnapshotMachine<ObservableTestDouble>);
static_assert(!ConsoleMachine<NonConstConsole>);
static_assert(!ConsoleMachine<WrongConsoleReturn>);
static_assert(!SerialInputMachine<WrongSerialReturn>);
static_assert(!SnapshotMachine<SnapshotOnly>);
static_assert(!SnapshotMachine<RestoreOnly>);
static_assert(!SnapshotMachine<NonConstSnapshot>);
static_assert(!SnapshotMachine<WrongSnapshotReturn>);
static_assert(!SnapshotMachine<WrongRestoreReturn>);
static_assert(!HasConsole<MinimalEngine>);
static_assert(!HasSerialInput<MinimalEngine>);
static_assert(!HasSnapshot<MinimalEngine>);
static_assert(!HasRestore<MinimalEngine>);
static_assert(!HasConsole<BasicEngine<NonConstConsole>>);
static_assert(!HasConsole<BasicEngine<WrongConsoleReturn>>);
static_assert(!HasSerialInput<BasicEngine<WrongSerialReturn>>);
static_assert(!HasSnapshot<BasicEngine<SnapshotOnly>>);
static_assert(!HasRestore<BasicEngine<SnapshotOnly>>);
static_assert(!HasSnapshot<BasicEngine<RestoreOnly>>);
static_assert(!HasRestore<BasicEngine<RestoreOnly>>);
static_assert(HasConsole<ObservableEngine>);
static_assert(HasSerialInput<ObservableEngine>);
static_assert(HasSnapshot<ObservableEngine>);
static_assert(HasRestore<ObservableEngine>);
static_assert(!std::default_initializable<SplitMemoryTestDouble>);
static_assert(!std::movable<SplitMemoryTestDouble>);
static_assert(std::constructible_from<MinimalEngine, std::in_place_t, SplitMemoryConfig>);
static_assert(!std::constructible_from<MinimalEngine, std::in_place_t>);
static_assert(!std::constructible_from<MinimalEngine, std::in_place_t, int>);
static_assert(!std::copy_constructible<MinimalEngine>);
static_assert(!std::move_constructible<MinimalEngine>);
static_assert(!std::is_copy_assignable_v<MinimalEngine>);
static_assert(!std::is_move_assignable_v<MinimalEngine>);
// Even a movable machine must not make the facade movable accidentally.
static_assert(std::movable<ObservableTestDouble>);
static_assert(!std::copy_constructible<ObservableEngine>);
static_assert(!std::move_constructible<ObservableEngine>);
static_assert(!std::is_copy_assignable_v<ObservableEngine>);
static_assert(!std::is_move_assignable_v<ObservableEngine>);
static_assert(
    std::same_as<decltype(std::declval<MinimalEngine&>().machine()), SplitMemoryTestDouble&>);
static_assert(std::same_as<decltype(std::declval<const MinimalEngine&>().machine()),
                           const SplitMemoryTestDouble&>);
static_assert(std::same_as<decltype(std::declval<const ObservableEngine&>().console()),
                           std::span<const char>>);
static_assert(
    std::same_as<decltype(std::declval<ObservableEngine&>().inject_serial(uint8_t{})), void>);
static_assert(std::same_as<decltype(std::declval<const ObservableEngine&>().snapshot()),
                           std::vector<uint8_t>>);
static_assert(std::same_as<decltype(std::declval<ObservableEngine&>().restore(
                               std::declval<std::span<const uint8_t>>())),
                           std::expected<void, SnapshotError>>);

TEST(MachineModelTest, DefaultRunResultHasNoStopOrDiagnosticPayload) {
    const RunResult result{};
    EXPECT_EQ(StopReason::kBudgetExhausted, result.reason);
    EXPECT_EQ(0U, result.pc);
    EXPECT_EQ(0U, result.code);
    EXPECT_EQ(0U, result.detail);
}

TEST(MachineModelTest, ConstructsImmovableMachineAtItsFinalAddress) {
    MinimalEngine engine(std::in_place, SplitMemoryConfig{});
    auto& machine = engine.machine();
    const auto& const_engine = engine;
    EXPECT_EQ(std::addressof(machine), machine.self());
    EXPECT_EQ(std::addressof(machine), std::addressof(const_engine.machine()));
    EXPECT_EQ(machine.sram().data(), machine.referenced_sram_byte());

    const std::array<uint8_t, 1> image = {0xA5};
    ASSERT_TRUE(engine.load_image(kSramBase, image));
    EXPECT_EQ(0xA5, *machine.referenced_sram_byte());
    engine.run(3);
    EXPECT_EQ(std::addressof(machine), machine.self());
    EXPECT_EQ(machine.sram().data(), machine.referenced_sram_byte());
}

TEST(MachineModelTest, LoadsDisjointHighAddressSegmentsWithoutMixingTheirStorage) {
    MinimalEngine engine(std::in_place, SplitMemoryConfig{});
    const std::array<uint8_t, 3> flash_image = {0x11, 0x22, 0x33};
    const std::array<uint8_t, 2> sram_image = {0xA1, 0xA2};
    ASSERT_TRUE(engine.load_image(kFlashBase + 5, flash_image));
    ASSERT_TRUE(engine.load_image(kSramBase + 2, sram_image));

    const auto& machine = std::as_const(engine).machine();
    const std::array<uint8_t, 16> expected_flash = {0, 0, 0, 0, 0, 0x11, 0x22, 0x33};
    const std::array<uint8_t, 8> expected_sram = {0, 0, 0xA1, 0xA2};
    EXPECT_TRUE(std::ranges::equal(expected_flash, machine.flash()));
    EXPECT_TRUE(std::ranges::equal(expected_sram, machine.sram()));
}

TEST(MachineModelTest, RejectsUnmappedCrossBoundaryAndOverflowingLoadsWithoutMutation) {
    MinimalEngine engine(std::in_place, SplitMemoryConfig{});
    const std::array<uint8_t, 2> image = {0xA5, 0x5A};
    EXPECT_FALSE(engine.load_image(0, image));
    EXPECT_FALSE(engine.load_image(kFlashBase - 1, image));
    EXPECT_FALSE(engine.load_image(kFlashBase + 15, image));
    EXPECT_FALSE(engine.load_image(kFlashBase + 16, image));
    EXPECT_FALSE(engine.load_image(kSramBase + 7, image));
    EXPECT_FALSE(engine.load_image(kSramBase + 8, image));
    EXPECT_FALSE(engine.load_image(std::numeric_limits<uint64_t>::max(), image));
    EXPECT_TRUE(
        std::ranges::all_of(engine.machine().flash(), [](uint8_t byte) { return byte == 0; }));
    EXPECT_TRUE(
        std::ranges::all_of(engine.machine().sram(), [](uint8_t byte) { return byte == 0; }));
    // Exact ends of both regions remain valid.
    ASSERT_TRUE(engine.load_image(kFlashBase + 14, image));
    ASSERT_TRUE(engine.load_image(kSramBase + 6, image));
    EXPECT_EQ(0x5A, engine.machine().flash().back());
    EXPECT_EQ(0x5A, engine.machine().sram().back());
}

TEST(MachineModelTest, ForwardsConfigurationRatherThanAssumingAMemoryMap) {
    const SplitMemoryConfig config{
        .flash_base = 0x3'8000'0000ULL,
        .sram_base = 0x7'1000'0000ULL,
        .fault = false,
    };
    MinimalEngine engine(std::in_place, config);
    const std::array<uint8_t, 1> image = {0x71};
    EXPECT_FALSE(engine.load_image(kFlashBase, image));
    EXPECT_FALSE(engine.load_image(kSramBase, image));
    ASSERT_TRUE(engine.load_image(config.flash_base, image));
    ASSERT_TRUE(engine.load_image(config.sram_base, image));
    EXPECT_EQ(0x71, engine.machine().flash().front());
    EXPECT_EQ(0x71, engine.machine().sram().front());
    EXPECT_EQ(config.flash_base + 4, engine.run(0).pc);
}

TEST(MachineModelTest, ForwardsBudgetAndEveryRunResultFieldIncludingZeroBudget) {
    MinimalEngine engine(std::in_place, SplitMemoryConfig{});
    const RunResult result = engine.run(7);
    EXPECT_EQ(7U, engine.machine().last_budget);
    EXPECT_EQ(1U, engine.machine().run_calls);
    EXPECT_EQ(StopReason::kBudgetExhausted, result.reason);
    EXPECT_EQ(kFlashBase + 11, result.pc);
    EXPECT_EQ(7U, result.code);
    EXPECT_EQ(1U, result.detail);

    const RunResult zero = engine.run(0);
    EXPECT_EQ(0U, engine.machine().last_budget);
    EXPECT_EQ(2U, engine.machine().run_calls);
    EXPECT_EQ(StopReason::kBudgetExhausted, zero.reason);
    EXPECT_EQ(result.pc, zero.pc);
    EXPECT_EQ(0U, zero.code);
    EXPECT_EQ(2U, zero.detail);
}

TEST(MachineModelTest, PreservesFaultReasonPcCodeAndDetail) {
    MinimalEngine engine(
        std::in_place,
        SplitMemoryConfig{.flash_base = kFlashBase, .sram_base = kSramBase, .fault = true});
    const RunResult result = engine.run(19);
    EXPECT_EQ(19U, engine.machine().last_budget);
    EXPECT_EQ(1U, engine.machine().run_calls);
    EXPECT_EQ(StopReason::kFault, result.reason);
    EXPECT_EQ(kFlashBase + 4, result.pc);
    EXPECT_EQ(0x42U, result.code);
    EXPECT_EQ(kSramBase + 8, result.detail);
}

TEST(MachineModelTest, ForwardsOptionalConsoleAndSerialWithoutCopyingTheConsoleView) {
    ObservableEngine engine(std::in_place, uint8_t{0x21});
    const auto& const_engine = engine;
    const auto output = const_engine.console();
    ASSERT_EQ(3U, output.size());
    EXPECT_EQ('o', output[0]);
    EXPECT_EQ('\0', output[1]);
    EXPECT_EQ('k', output[2]);
    EXPECT_EQ(const_engine.machine().console().data(), output.data());

    engine.inject_serial(0);
    engine.inject_serial(0xFE);
    EXPECT_EQ((std::vector<uint8_t>{0, 0xFE}), engine.machine().serial_input);
    const std::array<uint8_t, 2> image = {0x10, 0x20};
    ASSERT_TRUE(engine.load_image(kSramBase + 123, image));
    EXPECT_EQ((std::vector<uint8_t>{0x10, 0x20}), engine.machine().image);
    const RunResult result = engine.run(31);
    EXPECT_EQ(StopReason::kExited, result.reason);
    EXPECT_EQ(kSramBase + 123, result.pc);
    EXPECT_EQ(0xFEU, result.code);
    EXPECT_EQ(31U, result.detail);
}

TEST(MachineModelTest, ForwardsConstSnapshotAndRestoresSavedState) {
    ObservableEngine engine(std::in_place, uint8_t{0x24});
    const auto& const_engine = engine;
    const auto saved = const_engine.snapshot();
    EXPECT_EQ((std::vector<uint8_t>{0x24}), saved);
    EXPECT_EQ(1U, engine.machine().snapshot_calls);
    engine.inject_serial(0x57);
    EXPECT_EQ(0x57U, engine.run(1).code);

    ASSERT_TRUE(engine.restore(saved).has_value());
    EXPECT_EQ(1U, engine.machine().restore_calls);
    EXPECT_EQ(0x24U, engine.run(1).code);
    EXPECT_EQ(saved, const_engine.snapshot());
    EXPECT_EQ(2U, engine.machine().snapshot_calls);
}

TEST(MachineModelTest, PropagatesDistinctRestoreErrorsWithoutChangingState) {
    ObservableEngine engine(std::in_place, uint8_t{0x36});
    const auto truncated = engine.restore(std::span<const uint8_t>{});
    ASSERT_FALSE(truncated.has_value());
    EXPECT_EQ(SnapshotError::kTruncated, truncated.error());
    const std::array<uint8_t, 1> invalid = {0xFF};
    const auto bad_magic = engine.restore(invalid);
    ASSERT_FALSE(bad_magic.has_value());
    EXPECT_EQ(SnapshotError::kBadMagic, bad_magic.error());
    EXPECT_EQ(2U, engine.machine().restore_calls);
    EXPECT_EQ(0x36U, engine.run(1).code);
    EXPECT_EQ((std::vector<uint8_t>{0x36}), std::as_const(engine).snapshot());
}

} // namespace
