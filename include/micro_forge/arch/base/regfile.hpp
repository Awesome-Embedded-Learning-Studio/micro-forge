#pragma once
#include "micro_forge/device/base/types.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace micro_forge::arch::cpu::reg {

using ::micro_forge::device::data_t;

template <uint16_t RegisterCount>
class Registers {
public:
    enum class RegisterError : uint8_t { kIndexOverflow };
    template <typename T>
    using RegisterExpected = std::expected<T, RegisterError>;

    RegisterExpected<data_t> read(const std::size_t idx) const {
        if (idx >= RegisterCount) {
            return std::unexpected{RegisterError::kIndexOverflow};
        }
        return registers_[idx];
    }

    // Unchecked read for decoder hot paths where the index is statically
    // guaranteed in-range by the Thumb ISA encoding (rd3/rm3/rn3 <= 7,
    // rd4/rm4 <= 15, etc.). Debug asserts to catch a decoder regression;
    // release is a direct array read — no std::expected, no bounds branch —
    // and it removes the value_or(0) error-masking the checked read forced on
    // every operand fetch.
    constexpr inline data_t unchecked(std::size_t idx) const {
        assert(idx < RegisterCount);
        return registers_[idx];
    }

    RegisterExpected<void> write(const std::size_t idx, const data_t data) {
        if (idx >= RegisterCount) {
            return std::unexpected{RegisterError::kIndexOverflow};
        }
        registers_[idx] = data;
        return {};
    }

    virtual void reset() {
        for (auto& each : registers_) {
            each = 0;
        }
    }

    constexpr inline std::size_t size() const noexcept { return RegisterCount; }

private:
    std::array<data_t, RegisterCount> registers_{};
};
} // namespace micro_forge::arch::cpu::reg
