/**
 * @file pci_config.hpp
 * @brief PCI configuration space, mechanism 1 (batch 6d).
 * @version 0.1.0
 *
 * Two ports: CONFIG_ADDRESS (0xCF8, 32-bit selector) and
 * CONFIG_DATA (0xCFC, 32-bit window). The v0 space answers vendor
 * 0xFFFF for every function — an empty bus — unless a device is
 * registered on (bus, device, function). The AHCI disk lands here in
 * batch 6d; the enumerator walks the bus and finds exactly what is
 * registered.
 */

#pragma once

#include <array>
#include <cstdint>
#include <map>

#include "micro_forge/kernel/bus/port_space.hpp"

namespace micro_forge::kernel {

/**
 * @brief One PCI function: 256 bytes of configuration space.
 */
class PciFunction {
public:
    /// @brief Reads one configuration dword.
    /// @param[in] offset Register offset (dword-aligned).
    /// @return The dword value.
    [[nodiscard]] uint32_t read(uint8_t offset) const;
    /// @brief Writes one configuration dword.
    /// @param[in] offset Register offset (dword-aligned).
    /// @param[in] value The dword written.
    void write(uint8_t offset, uint32_t value);

private:
    std::array<uint32_t, 64> regs_{};
};

/**
 * @brief The mechanism-1 port pair.
 */
class PciConfig : public PortDevice {
public:
    /// @brief Register one function at (bus, device, function).
    /// @param[in] bus PCI bus number.
    /// @param[in] device Device number (0..31).
    /// @param[in] function Function number (0..7).
    /// @param[in] function_space Caller-owned function state.
    void add_function(uint8_t bus, uint8_t device, uint8_t function, PciFunction& function_space);

    /// @brief Answers one port read (address or data dword).
    /// @param[in] offset Port offset inside the 8-port window.
    /// @param[in] width Access width; 4 is served (byte access folds).
    /// @param[out] out The value read.
    /// @return False for offsets outside the pair.
    bool read(uint16_t offset, unsigned width, uint64_t& out) override;

    /// @brief Answers one port write.
    /// @param[in] offset Port offset inside the 8-port window.
    /// @param[in] width Access width; 4 is served (byte access folds).
    /// @param[in] value The value written.
    /// @return False for offsets outside the pair.
    bool write(uint16_t offset, unsigned width, uint64_t value) override;

private:
    /// Returns the registered function for the current selector, or null.
    [[nodiscard]] PciFunction* current();
    uint32_t address_ = 0;
    std::map<uint32_t, PciFunction*> functions_;
};

} // namespace micro_forge::kernel
