/**
 * @file x86_pc_config.hpp
 * @brief x86 PC layout: port windows, memory windows and PCI storage identity.
 * @version 0.1.0
 *
 * X86PcMachine owns these devices and resolves the catalogue when wiring.
 * Other boards define their own configuration instead of extending this
 * PC-specific device enumeration. Cinux and Linux are guests of this
 * machine family, not CPU architecture selections.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace micro_forge::machines {

/// @brief Devices the machine can wire. One slot per device instance the
///        X86PcMachine owns today; the enum keeps the config free of pointers.
/**
 * @brief Devices the machine can wire (one slot per owned instance).
 *
 * The enum keeps the layout free of pointers: a config names the device
 * kind, the X86PcMachine resolves it to its own instance when wiring.
 */
enum class X86PcDevice : uint8_t {
    kSerial,    ///< COM1 16550 UART.
    kPit,       ///< 8254 programmable interval timer.
    kBoard,     ///< Port 0x61 board control (speaker gate, refresh bits).
    kRtc,       ///< CMOS real-time clock.
    kPost,      ///< Port 0x80 POST latch.
    kFwCfg,     ///< QEMU fw_cfg data/selector pair.
    kPci,       ///< PCI mechanism-1 config address/data.
    kKbdData,   ///< 8042 data port.
    kKbdStatus, ///< 8042 status/command port.
    kPicMaster, ///< 8259 master PIC.
    kPicSlave,  ///< 8259 slave PIC.
    kAhci,      ///< AHCI controller register block (memory window).
    kLapic,     ///< Local APIC page (memory window).
    kIoApic,    ///< IO-APIC page (memory window).
};

/// @brief One port-space window.
struct X86PcPortWindow {
    uint16_t base = 0; ///< First I/O port.
    uint16_t size = 0; ///< Window length; zero disables the device.
    X86PcDevice device = X86PcDevice::kSerial;
};

/// @brief One guest-physical window served by a device.
struct X86PcMemoryWindow {
    uint64_t base = 0; ///< First guest-physical address.
    uint64_t size = 0; ///< Window length; zero disables the device.
    X86PcDevice device = X86PcDevice::kAhci;
};

/// @brief The full machine layout. RAM is always [0, ram_size + LFB).
struct X86PcConfig {                        // NOLINT(cppcoreguidelines-pro-type-member-init)
    uint64_t ram_size = 0x10000000;         ///< Usable guest RAM in bytes.
    std::optional<uintptr_t> window;        ///< Host window base (wasm); null = owning.
    std::vector<X86PcPortWindow> ports;     ///< Port windows.
    std::vector<X86PcMemoryWindow> windows; ///< Device memory windows.
    /// AHCI PCI identity: the function is registered behind mechanism-1
    /// config space; zero vendor disables the function entirely.
    uint16_t ahci_vendor = 0x8086;
    uint16_t ahci_device = 0x2922;
    uint32_t ahci_class = 0x01060100U;
    uint8_t ahci_irq = 11;
    /// Compile guest blocks to native code as they warm up. The library
    /// default stays false (explicit over implicit); front ends turn it
    /// on through the machine description, like mainstream emulators
    /// default their recompilers on. MICRO_FORGE_JIT overrides either
    /// way (0/false off, anything else on), on x86-64 hosts only.
    bool jit = false;
};

} // namespace micro_forge::machines
