#pragma once
#include "autogen/arch_details.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace micro_forge::cpu::reg {

template <uint16_t reg_count> class Registers {
  public:
    enum class RegisterError { IndexOverflow };
    template <typename T> using RegisterExpected =
        std::expected<T, RegisterError>;

    RegisterExpected<data_t> read(const std::size_t idx) const {
        if (idx >= reg_count) {
            return std::unexpected{RegisterError::IndexOverflow};
        }
        return registers[idx];
    }

    // Unchecked read for decoder hot paths where the index is statically
    // guaranteed in-range by the Thumb ISA encoding (rd3/rm3/rn3 <= 7,
    // rd4/rm4 <= 15, etc.). Debug asserts to catch a decoder regression;
    // release is a direct array read — no std::expected, no bounds branch —
    // and it removes the value_or(0) error-masking the checked read forced on
    // every operand fetch.
    constexpr inline data_t unchecked(std::size_t idx) const {
        assert(idx < reg_count);
        return registers[idx];
    }

    RegisterExpected<void> write(const std::size_t idx, const data_t data) {
        if (idx >= reg_count) {
            return std::unexpected{RegisterError::IndexOverflow};
        }
        registers[idx] = data;
        return {};
    }

    virtual void reset() {
        for (auto& each : registers) {
            each = 0;
        }
    }

    constexpr inline std::size_t size() const noexcept { return reg_count; }

  private:
    std::array<data_t, reg_count> registers;
};
} // namespace micro_forge::cpu::reg
