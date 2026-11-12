/**
 * @file ahci.hpp
 * @brief AHCI controller with one SATA disk (batch 6d).
 * @version 0.1.0
 *
 * The controller exposes the standard register block (one implemented
 * port), and services command-slot submissions the way the kernel
 * builds them: read the command header from guest memory, decode the
 * register H2D FIS (READ/WRITE DMA EXT, IDENTIFY, FLUSH), move the
 * sectors between the attached disk image and the PRDT-described guest
 * pages, then clear the issue bit and raise the port interrupt.
 *
 * The disk image is host bytes supplied by the machine owner (the ext2
 * volume baked from the rootfs); writes land in the same vector, which
 * keeps the image mutable for an exercising test.
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/kernel/memory/register_region.hpp"

namespace micro_forge::kernel {

/**
 * @brief The AHCI MMIO window (one disk on port 0).
 */
class Ahci : public RegisterRegionBase {
public:
    /// @brief Register offsets the driver touches.
    enum Registers : uint16_t {
        kCapabilities = 0x00,     ///< CAP.
        kGlobalControl = 0x04,    ///< GHC.
        kInterruptStatus = 0x08,  ///< IS.
        kPortsImplemented = 0x0C, ///< PI.
        kVersion = 0x10,          ///< VS.
        kPortBase = 0x100,        ///< Port 0 register block.
    };

    /// @brief Port-0 register offsets (relative to kPortBase).
    enum PortRegisters : uint8_t {
        kCommandListBase = 0x00,
        kCommandListBaseUpper = 0x04,
        kFisBase = 0x08,
        kFisBaseUpper = 0x0C,
        kPortInterruptStatus = 0x10,
        kPortInterruptEnable = 0x14,
        kPortCommand = 0x18,
        kPortTaskFile = 0x20,
        kPortSignature = 0x24,
        kPortSataStatus = 0x28,
        kPortCommandIssue = 0x38,
    };

    /**
     * @brief Binds the controller to the guest bus and the disk bytes.
     *
     * @param[in] bus Guest memory bus (command tables and PRDT pages).
     * @param[in] disk The SATA disk image; caller keeps it alive.
     * @param[in] interrupt Vector raiser; called after each completion.
     */
    Ahci(Bus& bus, std::vector<uint8_t>& disk, std::function<void()> interrupt);

    /// @brief Answers the pending command slots (call after CI writes).
    void service();

    /// @brief Sectors completed so far (test observability).
    [[nodiscard]] uint64_t sectors_done() const { return sectors_done_; }
    /// @brief Commands refused so far (unsupported FIS or sizes).
    [[nodiscard]] uint64_t refused() const { return refused_; }

protected:
    /// @brief Initializes the register image after the window binds.
    void prime();
    /// @brief Watches command-issue writes.
    void on_write(uint64_t offset, unsigned width, uint64_t value) override;

private:
    /// One decoded command.
    struct Command {
        uint64_t header_physical = 0;
        uint8_t fis_command = 0;
        uint64_t lba = 0;
        uint16_t count = 0;
        uint64_t data_physical = 0;
        uint32_t data_bytes = 0;
    };
    /// Decodes slot 0 from the command list; false when malformed.
    [[nodiscard]] bool decode(Command& out) const;
    /// Runs one decoded command against the disk.
    void run(const Command& command);

    Bus& bus_;
    std::vector<uint8_t>& disk_;
    std::function<void()> interrupt_;
    std::array<uint8_t, 0x2000> window_{};
    uint32_t port_interrupt_ = 0;
    uint64_t sectors_done_ = 0;
    uint64_t refused_ = 0;
};

} // namespace micro_forge::kernel
