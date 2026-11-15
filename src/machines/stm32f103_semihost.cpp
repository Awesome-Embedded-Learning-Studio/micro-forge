/**
 * @file stm32f103_semihost.cpp
 * @brief Opt-in debugger console calls without host files, processes or networking.
 * @version 0.1.0
 */
#include "micro_forge/machines/stm32f103.hpp"

#include <cstdint>
#include <expected>
#include <tuple>
#include <vector>

#include "micro_forge/arch/base/cpu.hpp"
#include "micro_forge/base/run_result.hpp"
#include "stm32f103_internal.hpp"

namespace micro_forge::machines {

arch::cpu::CPU::CPUExpected<bool>
Stm32F103Machine::Impl::handle_breakpoint(uint8_t immediate) {
    using CpuError = arch::cpu::CPU::CPUError;
    if (!semihosting_enabled || immediate != 0xAB) {
        return false;
    }
    const uint32_t operation = cpu.register_value(0).value_or(0);
    const uint32_t argument = cpu.register_value(1).value_or(0);
    const auto failure = [this](uint64_t address) -> arch::cpu::CPU::CPUExpected<bool> {
        status = {
            .reason = StopReason::kFault,
            .pc = cpu.pc().value_or(0),
            .code = static_cast<uint64_t>(CpuError::kDataAccessFault),
            .detail = address,
        };
        return std::unexpected(CpuError::kDataAccessFault);
    };
    const auto read_word = [this](uint64_t address) -> std::expected<uint32_t, uint64_t> {
        if (!image_window(address, 4)) {
            return std::unexpected(address);
        }
        const auto word = bus.read_u32(address);
        if (!word) {
            return std::unexpected(word.error().address);
        }
        return *word;
    };
    if (operation == 0x18 || operation == 0x20) { // SYS_EXIT / SYS_EXIT_EXTENDED
        uint32_t reason = argument;
        uint32_t code = reason == 0x20026 ? 0 : 1;
        if (operation == 0x20) {
            const auto reason_word = read_word(argument);
            const auto code_word = read_word(uint64_t{argument} + 4);
            if (!reason_word || !code_word) {
                return failure(!reason_word ? reason_word.error() : code_word.error());
            }
            reason = *reason_word;
            code = *code_word;
        }
        status = {
            .reason = StopReason::kExited,
            .pc = cpu.pc().value_or(0),
            .code = code,
            .detail = reason,
        };
        return true;
    }
    uint64_t pointer = argument;
    uint64_t length = 1;
    bool terminated = false;
    if (operation == 4) { // SYS_WRITE0
        length = 4096;
        terminated = true;
    } else if (operation == 5) { // SYS_WRITE to pre-opened stdout/stderr only
        const auto handle = read_word(argument);
        const auto buffer = read_word(uint64_t{argument} + 4);
        const auto count = read_word(uint64_t{argument} + 8);
        if (!handle) {
            return failure(handle.error());
        }
        if (!buffer) {
            return failure(buffer.error());
        }
        if (!count) {
            return failure(count.error());
        }
        if ((*handle != 1 && *handle != 2) || *count > 65536) {
            std::ignore = cpu.set_register_value(0, *count); // Nothing was written.
            return true;
        }
        pointer = *buffer;
        length = *count;
    } else if (operation != 3) { // SYS_WRITEC is the remaining allowed operation.
        return false;            // No filesystem, shell, process or unbounded debugger calls.
    }
    std::vector<char> output;
    for (uint64_t i = 0; i < length; ++i) {
        const uint64_t address = pointer + i;
        if (!image_window(address, 1)) {
            return failure(address);
        }
        const auto byte = bus.read_u8(address);
        if (!byte) {
            return failure(byte.error().address);
        }
        if (terminated && *byte == 0) {
            terminated = false;
            break;
        }
        output.push_back(static_cast<char>(*byte));
    }
    if (terminated) {
        return failure(pointer + length); // A bounded WRITE0 must find its terminator.
    }
    console.insert(console.end(), output.begin(), output.end());
    std::ignore = cpu.set_register_value(0, 0);
    return true;
}

} // namespace micro_forge::machines
