/**
 * @file stm32f103.hpp
 * @brief Medium-density STM32F103 machine with 128 KiB Flash and 20 KiB SRAM.
 * @version 0.1.0
 */
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "micro_forge/base/run_result.hpp"

namespace micro_forge::arch::arm::cortex_m3 {
class CortexM3CPU;
}
namespace micro_forge::kernel {
class Bus;
class Scheduler;
} // namespace micro_forge::kernel
namespace micro_forge::device::hooks {
class EventBus;
}
namespace micro_forge::device::stm32f1 {
class Stm32f1Gpio;
}

namespace micro_forge::machines {

/** @brief Firmware and reset failures reported without host exceptions. */
struct Stm32Error {
    enum class Kind : uint8_t {
        kInvalidElf,
        kUnsupportedElf,
        kTruncatedElf,
        kInvalidSegment,
        kUnmappedImage,
        kInvalidStackPointer,
        kInvalidResetHandler,
        kVectorReadFault,
        kCpuResetFailed,
    };
    Kind kind = Kind::kInvalidElf;
    uint64_t address = 0; ///< Guest address or ELF file offset associated with the failure.
};

/**
 * @brief One concrete Cortex-M3 system, not an x86 configuration variant.
 *
 * Models GPIO A/B/C, RCC, AFIO/EXTI, Flash interface, USART1/2, TIM2 and
 * TIM3 and NVIC/SCB/SysTick. Flash programming, DMA and other unlisted peripherals are
 * not implemented. The execution clock is deterministic but not cycle-accurate.
 * Snapshot support is deliberately absent until complete machine state is saved.
 */
class Stm32F103Machine {
public:
    static constexpr uint32_t kFlashBase = 0x0800'0000;
    static constexpr uint32_t kFlashSize = 128 * 1024;
    static constexpr uint32_t kSramBase = 0x2000'0000;
    static constexpr uint32_t kSramSize = 20 * 1024;

    Stm32F103Machine();
    ~Stm32F103Machine();
    Stm32F103Machine(const Stm32F103Machine&) = delete;
    Stm32F103Machine& operator=(const Stm32F103Machine&) = delete;
    Stm32F103Machine(Stm32F103Machine&&) = delete;
    Stm32F103Machine& operator=(Stm32F103Machine&&) = delete;

    /**
     * @brief Places bytes into Flash (including its boot alias) or SRAM.
     * @param[in] address Physical image destination, never an MMIO address.
     * @param[in] image Loader bytes; guest Flash writes remain forbidden.
     * @return False for unmapped, overflowing or cross-region images.
     */
    [[nodiscard]] bool load_image(uint64_t address, std::span<const uint8_t> image);

    /**
     * @brief Loads validated ELF32 little-endian ARM PT_LOADs at physical LMAs.
     * @param[in] image ELF executable; loading does not implicitly reset or run.
     * @return Failure without modifying storage if any segment is invalid.
     */
    [[nodiscard]] std::expected<void, Stm32Error> load_elf(std::span<const uint8_t> image);

    /**
     * @brief Resets devices and CPU, then reads MSP/reset PC from the boot alias.
     * @return Success with CPU running, or a latched reset fault.
     */
    [[nodiscard]] std::expected<void, Stm32Error> reset();

    /**
     * @brief Runs at most the requested CPU instruction-step slots.
     * @param[in] budget Execution slots; exception entry and WFI waiting also
     *                   consume a slot so an asleep guest cannot hang the host.
     * @return Portable stop status with MCU error kind/address on failure.
     */
    RunResult run_for(uint64_t budget);

    /// @brief USART output; remains valid until further output or reset.
    [[nodiscard]] std::span<const char> console() const;
    /// @brief Injects a byte into USART1; its RXNE/IRQ semantics remain observable.
    void inject_serial(uint8_t byte);
    /// @brief Injects a byte into USART2, independently of USART1.
    void inject_serial2(uint8_t byte);
    /**
     * @brief Opts into bounded console/exit semihost calls via BKPT 0xAB.
     * @param[in] enabled Disabled by default; never grants host file or shell access.
     */
    void set_semihosting_enabled(bool enabled);

    /// @brief Architectural controls and fault diagnostics, without an x86 CPU.
    [[nodiscard]] arch::arm::cortex_m3::CortexM3CPU& cpu();
    [[nodiscard]] const arch::arm::cortex_m3::CortexM3CPU& cpu() const;
    /// @brief The shared memory router used by the CPU and every device adapter.
    [[nodiscard]] kernel::Bus& bus();
    /// @brief Host events are scheduled in deterministic CPU-clock ticks.
    [[nodiscard]] kernel::Scheduler& scheduler();
    /// @brief Observes GPIO edges and UART bytes across device reset.
    [[nodiscard]] device::hooks::EventBus& events();
    /// @brief Returns GPIO A/B/C, or null for an unimplemented port.
    [[nodiscard]] device::stm32f1::Stm32f1Gpio* gpio(char port);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace micro_forge::machines
